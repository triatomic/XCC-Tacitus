#include "stdafx.h"
#include "xap.h"

#include "aud_decode.h"
#include "aud_file.h"
#include "ima_adpcm_wav_decode.h"
#include "ogg_file.h"
#include "voc_file.h"
#include "wav_file.h"
#include "string_conversion.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// Audio playback runs on a detached worker thread. The worker owns its
// LPDIRECTSOUNDBUFFER for the entire lifetime — the main thread NEVER
// releases the buffer, only signals stop via the atomic flag below.
// This avoids the use-after-free that crashed when xap_play released
// `dsb` while the worker was mid-call in dsb->GetStatus().
//
// dsb_mutex guards reads/writes of the shared `dsb` pointer (so xap_play's
// "stop the currently playing buffer" call is safe against the worker
// transitioning Play -> exit).
LPDIRECTSOUNDBUFFER dsb;
static std::mutex g_dsb_mutex;
static std::atomic<bool> g_stop_requested{false};
// Pause: worker keeps the buffer alive but the buffer itself is Stop()'d at
// a known byte offset. Resume re-Plays from that offset. Distinct from
// stop_requested so the worker loop doesn't tear down on pause.
static std::atomic<bool> g_pause_requested{false};
// Total bytes in the active buffer; published by the worker after the buffer
// is created so the main thread can compute progress + clamp seeks. 0 = idle.
static std::atomic<DWORD> g_buf_bytes{0};
// Bytes per second of the active stream, for converting position <-> time.
static std::atomic<DWORD> g_bytes_per_sec{0};
// Block alignment of the active stream (1 for 8-bit mono, 2 for 16-bit mono
// / 8-bit stereo, 4 for 16-bit stereo). Used by xap_seek to clamp seek
// targets onto sample boundaries — landing mid-sample on a 16-bit stream
// produces a tick from the byte-swapped half-sample at the seek point.
static std::atomic<DWORD> g_block_align{1};
// Coarse cooldown so successive xap_play calls (e.g. from a held key, or a
// fast user mashing Space) can't race the worker thread.
static std::chrono::steady_clock::time_point g_xap_last_call;

static int xap_play2(LPDIRECTSOUND ds, Cvirtual_binary s, string currentFile)
{
	xapFilePlaying = currentFile;
	Ccc_file f(true);
	f.load(s);
	t_file_type ft = f.get_file_type();

	int c_channels;
	int cb_sample;
	int samplerate;
	int c_samples;

	switch (ft)
	{
	case ft_aud:
		{
			Caud_file f;
			f.load(s);
			c_channels = 1;
			cb_sample = f.get_cb_sample();
			c_samples = f.get_c_samples();
			samplerate = f.get_samplerate();
			break;
		}
	case ft_ogg:
		{
			Cogg_file f;
			f.load(s);
			c_channels = f.get_c_channels();
			cb_sample = 2;
			c_samples = f.get_c_samples();
			samplerate = f.get_samplerate();
			break;
		}
	case ft_voc:
		{
			Cvoc_file f;
			f.load(s);
			c_channels = 1;
			cb_sample = 1;
			c_samples = f.get_c_samples();
			samplerate = f.get_samplerate();
			break;
		}
	case ft_wav:
		{
			Cwav_file f;
			f.load(s);
			if (f.process())
				return 0x105;
			const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
			c_channels = format_chunk.c_channels;
			switch (format_chunk.tag)
			{
			case 1:
				cb_sample = format_chunk.cbits_sample >> 3;
				c_samples = f.get_data_header().size / (cb_sample * format_chunk.c_channels);
				break;
			case 0x11:
				if (format_chunk.cbits_sample != 4)
					return 0x107;
				cb_sample = 2;
				// IMA-ADPCM: don't trust fact_chunk.c_samples — Westwood-era
				// .wav files in MIX archives often have a zero or otherwise
				// bogus value here, which made cb_audio = 0 and led to the
				// downstream memcpy crash. Compute from data size instead:
				// the format packs 2 samples per byte and each decoded
				// sample is 16-bit, so per-channel samples = data*2/channels.
				c_samples = f.get_data_header().size * 2 / format_chunk.c_channels;
				break;
			default:
				return 0x106;
			}
			samplerate = format_chunk.samplerate;
			break;
		}
	default:
		return 0x100;
	}
	int cb_audio = c_channels * cb_sample * c_samples;

	WAVEFORMATEX wfdesc;
	ZeroMemory(&wfdesc, sizeof(WAVEFORMATEX));
	wfdesc.wFormatTag = WAVE_FORMAT_PCM;
	wfdesc.nChannels = c_channels;
	wfdesc.nSamplesPerSec = samplerate;
	wfdesc.wBitsPerSample = cb_sample << 3;
	wfdesc.nBlockAlign = wfdesc.nChannels * wfdesc.wBitsPerSample >> 3;
	wfdesc.nAvgBytesPerSec = wfdesc.nSamplesPerSec * wfdesc.nBlockAlign;

	DSBUFFERDESC dsbdesc;
	ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_GLOBALFOCUS;
	dsbdesc.dwBufferBytes = cb_audio;
	dsbdesc.lpwfxFormat = (LPWAVEFORMATEX)&wfdesc;

	// Local buffer owned by this worker thread. Published to the global
	// `dsb` (under mutex) so xap_play can call Stop() on it; never freed
	// by the main thread.
	LPDIRECTSOUNDBUFFER local_dsb = NULL;
	if (ds->CreateSoundBuffer(&dsbdesc, &local_dsb, NULL))
		return 0x101;
	{
		std::lock_guard<std::mutex> lock(g_dsb_mutex);
		dsb = local_dsb;
		g_stop_requested.store(false);
		g_pause_requested.store(false);
		g_buf_bytes.store(cb_audio);
		g_bytes_per_sec.store(wfdesc.nAvgBytesPerSec);
		g_block_align.store(wfdesc.nBlockAlign ? wfdesc.nBlockAlign : 1);
	}
	void* p1;
	DWORD s1;
	int error = 0;
	if (local_dsb->Lock(0, 0, &p1, &s1, NULL, NULL, DSBLOCK_ENTIREBUFFER))
		error = 0x102;
	else
	{
		switch (ft)
		{
		case ft_aud:
		{
			Caud_file f;
			f.load(s);
			f.decode().read(p1);
			break;
		}
		case ft_ogg:
		{
			Cogg_file f;
			f.load(s);
			Cvirtual_audio audio;
			if (!f.decode(audio))
				memcpy(p1, audio.audio(), audio.cb_audio());
			break;
		}
		case ft_voc:
		{
			Cvoc_file f;
			f.load(s);
			memcpy(p1, f.get_sound_data(), cb_audio);
			break;
		}
		case ft_wav:
		{
			Cwav_file f;
			f.load(s);
			f.process();
			switch (f.get_format_chunk().tag)
			{
			case 1:
				f.seek(f.get_data_ofs());
				f.read(p1, cb_audio);
				break;
			case 0x11:
			{
				// IMA-ADPCM block size varies per file (Westwood used 256,
				// 512, 1024, etc.). Read it from the format chunk's
				// block_align field instead of hardcoding 512*channels —
				// passing the wrong block size makes the decoder read
				// chunk-header bytes at the wrong offsets and produce
				// garbled output (sounds like loud noise).
				int block = f.get_format_chunk().block_align;
				if (block <= 0)
					block = 512 * c_channels;
				Cima_adpcm_wav_decode decode;
				decode.load(f.get_data() + f.get_data_ofs(), f.get_data_size(), c_channels, block);
				// Decoder's reported byte count is the truth; cb_audio was
				// computed from a rough formula and overshoots slightly
				// (header bytes counted as data). Copy at most what the
				// decoder produced, then zero the rest of the buffer so the
				// uninitialized tail doesn't play as loud noise.
				int cb_copy = cb_audio;
				if (decode.cb_data() < cb_copy) cb_copy = decode.cb_data();
				if (static_cast<DWORD>(cb_copy) > s1) cb_copy = static_cast<int>(s1);
				memcpy(p1, decode.data(), cb_copy);
				if (static_cast<DWORD>(cb_copy) < s1)
					memset(static_cast<byte*>(p1) + cb_copy, 0, s1 - cb_copy);
				break;
			}
			}
			break;
		}
		}
		HRESULT dsr;
		if (local_dsb->Unlock(p1, s1, NULL, NULL))
			error = 0x103;
		else if (local_dsb->Play(0, 0, 0))
			error = 0x104;
		else
		{
			DWORD status;
			// Loop exits on stop_requested OR natural end of playback.
			// Pause is invisible to the loop: when paused, the buffer's
			// PLAYING bit is clear but pause_requested is set, so we keep
			// spinning until either the user resumes (back to PLAYING) or
			// stops/seeks. Reads to local_dsb here are safe because the
			// buffer is owned by this thread for its full lifetime.
			for (;;)
			{
				if (g_stop_requested.load())
					break;
				dsr = local_dsb->GetStatus(&status);
				if (dsr != DS_OK)
					break;
				const bool playing = (status & DSBSTATUS_PLAYING) != 0;
				const bool paused = g_pause_requested.load();
				if (!playing && !paused)
					break;	// natural end of playback
				Sleep(50);
			}
		}
	}
	// Worker exit — release ours. Unpublish from the global pointer first
	// (under mutex) so any concurrent xap_play stop attempt sees NULL and
	// no-ops rather than touching a buffer we're about to release.
	{
		std::lock_guard<std::mutex> lock(g_dsb_mutex);
		if (dsb == local_dsb)
		{
			dsb = NULL;
			g_buf_bytes.store(0);
			g_bytes_per_sec.store(0);
			g_block_align.store(1);
			g_pause_requested.store(false);
		}
	}
	if (local_dsb)
	{
		local_dsb->Stop();
		local_dsb->Release();
	}
	xapFilePlaying = "";
	return error;
}

void xap_play(LPDIRECTSOUND ds, Cvirtual_binary s, string currentFile)
{
	using clock = std::chrono::steady_clock;
	// 200 ms cooldown between successive calls. Absorbs Space-held auto-
	// repeat at the keystroke level and gives the previous worker time to
	// observe the stop signal and unpublish itself from the global dsb.
	auto now = clock::now();
	if (now - g_xap_last_call < std::chrono::milliseconds(200))
		return;
	g_xap_last_call = now;

	// Stop any currently-playing buffer. We never call Release() here —
	// the worker thread owns the buffer and will release it when the
	// playback loop exits (the stop flag breaks the loop). Stop() under
	// mutex is safe because the worker keeps its local_dsb alive across
	// the loop and only releases after we've cleared the global pointer.
	bool was_playing = false;
	{
		std::lock_guard<std::mutex> lock(g_dsb_mutex);
		if (dsb)
		{
			g_stop_requested.store(true);
			dsb->Stop();
			was_playing = true;
		}
	}

	if (xapFilePlaying == currentFile)
	{
		// Same-file toggle: stop only, don't restart.
		xapFilePlaying = "";
		return;
	}

	// Switching files (or starting fresh). Wait briefly for the previous
	// worker to observe the stop signal and unpublish itself, so the new
	// thread doesn't race the old one over the global dsb pointer.
	if (was_playing)
	{
		for (int i = 0; i < 20; i++)
		{
			{
				std::lock_guard<std::mutex> lock(g_dsb_mutex);
				if (!dsb)
					break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	xapFilePlaying = "";
	thread([ds, s, currentFile]()
		{
			xap_play2(ds, s, currentFile);
		}).detach();
}

void xap_pause()
{
	std::lock_guard<std::mutex> lock(g_dsb_mutex);
	if (!dsb || g_pause_requested.load())
		return;
	g_pause_requested.store(true);
	dsb->Stop();	// remembers internal play cursor; resume re-Plays from it
}

void xap_resume()
{
	std::lock_guard<std::mutex> lock(g_dsb_mutex);
	if (!dsb || !g_pause_requested.load())
		return;
	g_pause_requested.store(false);
	dsb->Play(0, 0, 0);	// resumes from saved cursor
}

bool xap_is_paused()
{
	return g_pause_requested.load();
}

double xap_get_progress()
{
	std::lock_guard<std::mutex> lock(g_dsb_mutex);
	if (!dsb)
		return -1.0;
	const DWORD total = g_buf_bytes.load();
	if (total == 0)
		return -1.0;
	DWORD play_cursor = 0;
	DWORD write_cursor = 0;
	if (dsb->GetCurrentPosition(&play_cursor, &write_cursor) != DS_OK)
		return -1.0;
	if (play_cursor >= total)
		return 1.0;
	return static_cast<double>(play_cursor) / static_cast<double>(total);
}

double xap_get_duration()
{
	const DWORD total = g_buf_bytes.load();
	const DWORD bps = g_bytes_per_sec.load();
	if (total == 0 || bps == 0)
		return -1.0;
	return static_cast<double>(total) / static_cast<double>(bps);
}

void xap_seek(double progress)
{
	if (progress < 0.0) progress = 0.0;
	if (progress > 1.0) progress = 1.0;
	std::lock_guard<std::mutex> lock(g_dsb_mutex);
	if (!dsb)
		return;
	const DWORD total = g_buf_bytes.load();
	if (total == 0)
		return;
	// Round to a true sample boundary using the format's nBlockAlign. For
	// 8-bit mono that's 1 (no-op); for 16-bit stereo that's 4. Landing
	// mid-sample produces a swapped-byte tick at the seek point that
	// reads as audible distortion. The 4-byte alignment we used before
	// was wrong for 8-bit mono streams (Westwood VOC/AUD).
	const DWORD align = g_block_align.load();
	DWORD bytes = static_cast<DWORD>(progress * total);
	if (align > 1) bytes -= bytes % align;
	if (bytes + align > total) bytes = total > align ? total - align : 0;
	// Stop before repositioning. SetCurrentPosition is documented as safe
	// while playing, but on some drivers the hardware mixer pops as it
	// adjusts the read pointer. Stop->SetPos->Play (only if we were
	// playing, not paused) avoids the glitch.
	DWORD status = 0;
	dsb->GetStatus(&status);
	const bool was_playing = (status & DSBSTATUS_PLAYING) != 0;
	if (was_playing)
		dsb->Stop();
	dsb->SetCurrentPosition(bytes);
	if (was_playing && !g_pause_requested.load())
		dsb->Play(0, 0, 0);
}

void xap_stop()
{
	std::lock_guard<std::mutex> lock(g_dsb_mutex);
	if (!dsb)
		return;
	g_stop_requested.store(true);
	g_pause_requested.store(false);	// don't keep worker alive on a stop
	dsb->Stop();
}
