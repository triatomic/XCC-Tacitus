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
			// Honor a stop signal from xap_play in addition to the natural
			// end of playback. Reads to local_dsb here are safe because
			// the buffer is owned by this thread for its full lifetime.
			while (dsr = local_dsb->GetStatus(&status), DS_OK == dsr
				&& (status & DSBSTATUS_PLAYING)
				&& !g_stop_requested.load())
			{
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
			dsb = NULL;
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
