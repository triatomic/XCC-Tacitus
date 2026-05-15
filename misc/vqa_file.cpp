#include "stdafx.h"
#include "vqa_file.h"

#include <vfw.h>
#include "image_file.h"
#include "pcx_decode.h"
#include "string_conversion.h"
#include "vqa_decode.h"
#include "wav_file.h"
#include "wav_structures.h"
#include "xcc_log.h"

class Cvqa_decoder : public Cvideo_decoder
{
public:
	int cb_pixel() const
	{
		return m_f.get_cbits_pixel() == 8 ? 1 : 3;
	}

	int cf() const
	{
		return m_f.get_c_frames();
	}

	int cx() const
	{
		return m_f.get_cx();
	}

	int cy() const
	{
		return m_f.get_cy();
	}

	int decode(void* d)
	{
		if (m_frame_i >= cf())
			return 1;
		// VQFL (codebook) chunks live between VQFR frame chunks in both
		// 8-bit and 16-bit VQAs (TS/Firestorm 8-bit VQAs rely on them).
		// The original 8-bit branch skipped them, leaving the codebook
		// stale and producing garbage-block frames.
		while (!m_f.is_video_chunk())
		{
			if (m_f.get_chunk_id() == vqa_vqfl_id)
				m_vqa_d.decode_vqfl_chunk(m_f.read_chunk());
			else
				m_f.skip_chunk();
		}
		t_palette_entry* pal = (cb_pixel() == 1) ? m_palette : NULL;
		m_vqa_d.decode_vqfr_chunk(m_f.read_chunk().data(), m_frame.write_start(cb_image()), pal);
		if (d)
			m_frame.read(d);
		m_frame_i++;
		return 0;
	}

	const t_palette_entry* palette() const
	{
		return m_palette;
	}

	int seek(int f)
	{
		if (f == m_frame_i)
			return 0;
		if (f < m_frame_i || m_frame_i == -1)
		{
			m_f.seek(sizeof(t_vqa_header));
			m_f.read_chunk_header();
			m_vqa_d.start_decode(m_f.header());
			if (cb_pixel() != 1)
			{
				DDPIXELFORMAT pf;
				pf.dwRGBAlphaBitMask = 0;
				pf.dwRBitMask = 0x0000ff;
				pf.dwGBitMask = 0x00ff00;
				pf.dwBBitMask = 0xff0000;
				m_vqa_d.set_pf(pf, 3);
			}
			m_frame_i = 0;
		}
		while (m_frame_i < f && !decode(NULL))
			;
		return 0;
	}

	Cvqa_decoder(const Cvqa_file& f)
	{
		m_f.load(f);
		m_frame_i = -1;
		seek(0);
	}
private:
	Cvqa_decode m_vqa_d;
	Cvqa_file m_f;
	Cvirtual_binary m_frame;
	int m_frame_i;
	t_palette m_palette;
};

Cvideo_decoder* Cvqa_file::decoder()
{
	return new Cvqa_decoder(*this);
}

int Cvqa_file::post_open()
{
	int error = read(&m_header, sizeof(t_vqa_header));
	return error ? error : read_chunk_header();
}

static void flip_frame(const byte* s, byte* d, int cx, int cy, int cb_pixel)
{
	int cb_line = cx * cb_pixel;
	const byte* r = s;
	byte* w = d + cb_line * cy;
	while (cy--)
	{
		w -= cb_line;
		memcpy(w, r, cb_line);
		r += cb_line;
	}
}

static int process_audio_chunk_for_avi(Cvqa_file& f, Cvqa_decode& vqa_d, int& audio_i, PAVISTREAM a)
{
	int error = 0;
	short* aud_out;
	int size = f.get_chunk_size();
	if (f.get_chunk_id() >> 24 == '0')
	{
		aud_out = new short[size / 2];
		f.read_chunk(aud_out);
		size /= 4;
	}
	else
	{
		aud_out = new short[2 * size];
		vqa_d.decode_snd2_chunk(f.read_chunk().data(), size, aud_out);
	}
	if (AVIStreamWrite(a, audio_i, 2 * size, aud_out, 4 * size, 0, NULL, NULL))
		error = 1;
	else
		audio_i += 2 * size;
	delete[] aud_out;
	return error;
}

int Cvqa_file::extract_as_avi(const string& name, HWND hwnd)
{
	int error = 0;
	Cvqa_decode vqa_d;
	vqa_d.start_decode(header());
	int cx = get_cx();
	int cy = get_cy();
	AVIFileInit();
	PAVIFILE f = NULL;
	PAVISTREAM v = NULL;
	PAVISTREAM a = NULL;
	PAVISTREAM vc = NULL;
	if (AVIFileOpen(&f, name.c_str(), OF_CREATE | OF_WRITE, NULL))
		error = 1;
	else
	{
		AVISTREAMINFO vi;
		memset(&vi, 0, sizeof(AVISTREAMINFO));
		vi.fccType = streamtypeVIDEO; 
		vi.fccHandler = 0; 
		vi.dwFlags = get_cbits_pixel() == 8 ? AVISTREAMINFO_FORMATCHANGES : 0; 
		vi.dwScale = 1; 
		vi.dwRate = 15;
		vi.dwLength = get_c_frames();
		SetRect(&vi.rcFrame, 0, 0, cx, cy);
		if (AVIFileCreateStream(f, &v, &vi))
			error = 2;
		else
		{	
			AVISTREAMINFO ai;
			memset(&ai, 0, sizeof(AVISTREAMINFO));
			ai.fccType = streamtypeAUDIO; 
			ai.dwFlags = 0;
			ai.dwScale = 1;
			ai.dwRate = get_samplerate();
			ai.dwSampleSize = 2 * get_c_channels();
			if (get_c_channels() && AVIFileCreateStream(f, &a, &ai))
				error = 3;
			else
			{	
				AVICOMPRESSOPTIONS* vco = new AVICOMPRESSOPTIONS;
				if (!AVISaveOptions(hwnd, 0, 1, &v, &vco))
					error = 4;
				else 
				{
					if (AVIMakeCompressedStream(&vc, v, vco, NULL))
						error = 5;
					else				
					{
						int audio_i = 0;
						PCMWAVEFORMAT af;
						memset(&af, 0, sizeof(PCMWAVEFORMAT));
						af.wf.wFormatTag = WAVE_FORMAT_PCM;
						af.wf.nChannels = get_c_channels();
						af.wf.nSamplesPerSec = get_samplerate();
						af.wf.nAvgBytesPerSec = 2 * get_c_channels() * get_samplerate();
						af.wf.nBlockAlign = 2 * get_c_channels();
						af.wBitsPerSample = 16;
						if (get_c_channels() && AVIStreamSetFormat(a, 0, &af, sizeof(PCMWAVEFORMAT)))
							error = 6;
						else
						{
							const int cb_vf = sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD);
							BITMAPINFO* vf = reinterpret_cast<BITMAPINFO*>(new byte[cb_vf]);
							memset(vf, 0, sizeof(BITMAPINFOHEADER));
							vf->bmiHeader.biSize = sizeof(BITMAPINFOHEADER); 
							vf->bmiHeader.biWidth = cx; 
							vf->bmiHeader.biHeight = cy; 
							vf->bmiHeader.biPlanes = 1;
							vf->bmiHeader.biBitCount = get_cbits_pixel() == 8 ? 8 : 24;
							vf->bmiHeader.biCompression = BI_RGB ;
							vf->bmiHeader.biSizeImage = 0; 
							if (get_cbits_pixel() == 8)
							{
								t_palette palette;
								byte* frame = new byte[cx * cy];
								byte* frame_flipped = new byte[cx * cy];
								for (int i = 0; i < get_c_frames(); i++)
								{
									while (!is_video_chunk())
									{
										if (is_audio_chunk())
										{
											if (process_audio_chunk_for_avi(*this, vqa_d, audio_i, a))
											{
												error = 7;
												break;
											}
										}
										else
											skip_chunk();
									}
									if (error)
										break;
									vqa_d.decode_vqfr_chunk(read_chunk().data(), frame, palette);
									flip_frame(frame, frame_flipped, cx, cy, 1);
									for (int j = 0; j < 256; j++)
									{
										vf->bmiColors[j].rgbRed = palette[j].r;
										vf->bmiColors[j].rgbGreen = palette[j].g;
										vf->bmiColors[j].rgbBlue = palette[j].b;
									}
									// xcc_log::write_line("Writing frame " + n(i));
									if (!i && AVIStreamSetFormat(vc, 0, vf, cb_vf))
									{
										error = 8;
										break;
									}
									if (AVIStreamWrite(vc, i, 1, frame_flipped, cx * cy, 0, NULL, NULL))
									{
										error = 9;
										break;
									}
								}
								delete[] frame_flipped;
								delete[] frame;
							}
							else
							{
								if (AVIStreamSetFormat(vc, 0, vf, sizeof(BITMAPINFOHEADER)))
									error = 10;
								else
								{								
									DDPIXELFORMAT pf;
									pf.dwRGBAlphaBitMask = 0;
									pf.dwRBitMask = 0xff0000;
									pf.dwGBitMask = 0x00ff00;
									pf.dwBBitMask = 0x0000ff;
									vqa_d.set_pf(pf, 3);
									byte* frame = new byte[3 * cx * cy];
									byte* frame_flipped = new byte[3 * cx * cy];
									for (int i = 0; i < get_c_frames(); i++)
									{
										if (get_chunk_id() == vqa_vqfl_id)
											vqa_d.decode_vqfl_chunk(read_chunk());
										while (!is_video_chunk())
										{
											if (is_audio_chunk())
											{
												if (process_audio_chunk_for_avi(*this, vqa_d, audio_i, a))
												{
													error = 11;
													break;
												}
											}
											else
												skip_chunk();
										}
										vqa_d.decode_vqfr_chunk(read_chunk().data(), frame, NULL);
										flip_frame(frame, frame_flipped, cx, cy, 3);
										// xcc_log::write_line("Writing frame " + n(i));
										if (AVIStreamWrite(vc, i, 1, frame_flipped, 3 * cx * cy, 0, NULL, NULL))
										{
											error = 12;
											break;
										}
									}
									delete[] frame_flipped;
									delete[] frame;
								}
							}
							delete[] vf;
						}
					}
					AVISaveOptionsFree(1, &vco);
				}
			}
		}
	}
	if (vc)
		AVIStreamRelease(vc);
	if (a)
		AVIStreamRelease(a);
	if (v)
		AVIStreamRelease(v);
	if (f)
		AVIFileRelease(f);
	AVIFileExit();
	return error;
}

int Cvqa_file::extract_as_pcx(const Cfname& name, t_file_type ft)
{
	int error = 0;
	Cvqa_decode vqa_d;
	vqa_d.start_decode(header());
	int cx = get_cx();
	int cy = get_cy();
	if (get_cbits_pixel() == 8)
	{
		t_palette palette;
		byte* frame = new byte[cx * cy];
		for (int i = 0; i < get_c_frames(); i++)
		{
			while (!is_video_chunk())
				skip_chunk();
			vqa_d.decode_vqfr_chunk(read_chunk().data(), frame, palette);
			Cfname t = name;
			t.set_title(name.get_ftitle() + " " + nwzl(4, i));
			error = image_file_write(t, ft, frame, palette, cx, cy);
			if (error)
				break;
		}
		delete[] frame;
	}
	else
	{
		DDPIXELFORMAT pf;
		pf.dwRGBAlphaBitMask = 0;
		pf.dwRBitMask = 0x0000ff;
		pf.dwGBitMask = 0x00ff00;
		pf.dwBBitMask = 0xff0000;
		vqa_d.set_pf(pf, 3);
		byte* frame = new byte[3 * cx * cy];
		for (int i = 0; i < get_c_frames(); i++)
		{
			if (get_chunk_id() == vqa_vqfl_id)
				vqa_d.decode_vqfl_chunk(read_chunk());
			while (!is_video_chunk())
				skip_chunk();
			vqa_d.decode_vqfr_chunk(read_chunk().data(), frame, NULL);
			Cfname t = name;
			t.set_title(name.get_ftitle() + " " + nwzl(4, i));
			error = image_file_write(t, ft, frame, NULL, cx, cy);
			if (error)
				break;
		}
		delete[] frame;
	}
	return error;
}

struct t_list_entry
{
	int c_samples;
	short* audio;
};

int Cvqa_file::decode_audio_to_wav(Cvirtual_binary& out)
{
	if (!get_c_channels())
		return 1;
	using t_list = vector<t_list_entry>;
	t_list list;
	int cs_remaining = 0;
	Cvqa_decode vqa_d;
	vqa_d.start_decode(header());
	for (int i = 0; i < get_c_frames(); i++)
	{
		while (1)
		{
			if (is_audio_chunk())
			{
				t_list_entry e;
				int size = get_chunk_size();
				if (get_chunk_id() >> 24 == '0')
				{
					e.c_samples = size >> 1;
					e.audio = new short[size / 2];
					read_chunk(e.audio);
					size /= 4;
				}
				else
				{
					e.c_samples = size << 1;
					e.audio = new short[2 * size];
					vqa_d.decode_snd2_chunk(read_chunk().data(), size, e.audio);
				}
				cs_remaining += e.c_samples;
				list.push_back(e);
			}
			else if (is_video_chunk())
				break;
			else
				skip_chunk();
		}
		skip_chunk();
	}
	if (cs_remaining <= 0)
	{
		out = Cvirtual_binary();
		return 1;
	}
	// Coalesce into one PCM buffer, then run the canonical WAV-PCM
	// writer so the result parses cleanly through Cwav_file::process
	// (which is what xap_play2 uses to validate the buffer before
	// pushing it to DirectSound). Rolling our own RIFF header was the
	// reason audio was silent — the file_header.size used sample count
	// instead of bytes and tripped up the parser.
	const int cb_pcm = cs_remaining << 1;
	std::vector<short> pcm;
	pcm.reserve(cs_remaining);
	for (auto& i : list)
	{
		pcm.insert(pcm.end(), i.audio, i.audio + i.c_samples);
		delete[] i.audio;
	}
	Cvirtual_file w = wav_pcm_file_write(pcm.data(), cb_pcm, get_samplerate(), 2, get_c_channels());
	out = w.read();
	return 0;
}

double Cvqa_file::frame_rate()
{
	// Count audio samples between the first two video chunks; FPS =
	// samplerate / samples_per_frame. Falls back to 15 fps if the stream
	// has no audio or never reaches a second video chunk.
	const double fallback = 15.0;
	if (!get_c_channels() || !get_samplerate())
		return fallback;
	const long long save_p = get_p();
	const t_vqa_chunk_header save_chunk = { vqa_file_id, 0 }; // placeholder; we restore via seek
	(void)save_chunk;
	int samples = 0;
	bool saw_video = false;
	Cvqa_decode probe;
	probe.start_decode(header());
	for (int guard = 0; guard < 4096; guard++)
	{
		if (is_video_chunk())
		{
			if (saw_video)
				break;
			// Reset the sample counter at the first video chunk. The audio
			// that precedes VQFR0 (FINF prefetch + first chunk's worth of
			// samples in some VQAs) doesn't represent a single frame's
			// worth — RA1 VQAs stuff a multi-second audio prefetch there.
			// Counting it inflates samples_per_frame, which makes the
			// reported fps absurdly low (1-2 fps), and that drives the
			// dialog timer at ~half-second intervals. We want only the
			// audio between VQFR0 and VQFR1.
			samples = 0;
			saw_video = true;
			skip_chunk();
			continue;
		}
		if (!saw_video)
		{
			// Pre-VQFR0 audio is the prefetch buffer; skip without counting.
			skip_chunk();
			continue;
		}
		if (is_audio_chunk())
		{
			int size = get_chunk_size();
			if (get_chunk_id() >> 24 == '0')
				samples += size >> 1;
			else
				samples += size << 1;
			skip_chunk();
		}
		else
			skip_chunk();
	}
	// Restore stream position so subsequent decoding starts from frame 0.
	seek(sizeof(t_vqa_header));
	read_chunk_header();
	(void)save_p;
	if (samples <= 0)
		return fallback;
	const int per_frame = samples / get_c_channels();
	if (per_frame <= 0)
		return fallback;
	return static_cast<double>(get_samplerate()) / per_frame;
}

int Cvqa_file::extract_as_wav(const string& name)
{
	Cvirtual_binary wav;
	int error = decode_audio_to_wav(wav);
	if (error)
		return error;
	return wav.save(name);
}

int Cvqa_file::read_chunk_header()
{
	if (get_p() & 1)
		skip(1);
	int error = read(&m_chunk_header, sizeof(t_vqa_chunk_header));
	m_chunk_header.size = reverse(m_chunk_header.size);
	return error;
}

int Cvqa_file::read_chunk(void* data)
{
	int error = read(data, get_chunk_size());
	return error ? error : read_chunk_header();
}

Cvirtual_binary Cvqa_file::read_chunk()
{
	Cvirtual_binary d;
	return read_chunk(d.write_start(get_chunk_size())) ? Cvirtual_binary() : d;
}

void Cvqa_file::set_empty_chunk()
{
	m_chunk_header.size = 0;
}

int Cvqa_file::skip_chunk()
{
	skip(get_chunk_size());
	return read_chunk_header();	
}
