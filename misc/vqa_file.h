#pragma once

#include "cc_file.h"
#include "cc_structures.h"
#include "fname.h"
#include "video_decoder.h"

#pragma comment(lib, "vfw32.lib")

class Cvqa_file : public Ccc_file  
{
public:
	Cvideo_decoder* decoder();
	int post_open();
	int extract_as_avi(const string& name, HWND hwnd);
	int extract_as_pcx(const Cfname& name, t_file_type ft);
	int extract_as_wav(const string& name);
	// Decode the entire VQA audio track into a self-contained WAV buffer.
	// Shared by extract_as_wav and the in-Mixer popup playback path.
	int decode_audio_to_wav(Cvirtual_binary& out);
	// Frames per second derived from samplerate / (audio samples per video
	// frame). Falls back to 15 fps when the audio track is missing or the
	// header math degenerates.
	double frame_rate();
	int read_chunk_header();
	int read_chunk(void* data);
	Cvirtual_binary read_chunk();
	void set_empty_chunk();
	int skip_chunk();

	Cvqa_file():
		Ccc_file(false)
	{
	}

	bool is_valid()
	{
		int size = get_size();
		/*
		if (get_data())
			memcpy(&m_header, get_data(), sizeof(t_vqa_header));
		*/
		return !(sizeof(t_vqa_header) > size ||
			m_header.file_header.id != vqa_file_id ||
			m_header.id != vqa_form_id);
	}

	int get_c_channels() const
	{
		return m_header.c_channels;
	}

	int get_c_frames() const
	{
		return m_header.c_frames;
	}

	int get_chunk_id() const
	{
		return m_chunk_header.id;
	}

	int get_chunk_size() const
	{
		return m_chunk_header.size;
	}

	int get_cx() const
	{
		return m_header.cx;
	}

	int get_cy() const
	{
		return m_header.cy;
	}

	int get_cx_block() const
	{
		return m_header.cx_block;
	}

	int get_cy_block() const
	{
		return m_header.cy_block;
	}

	const t_vqa_header& header() const
	{
		return m_header;
	}

	int get_samplerate() const
	{
		return m_header.samplerate;
	}

	bool is_audio_chunk() const
	{
		return (get_chunk_id() & vqa_t_mask) == vqa_snd_id;
	}

	bool is_video_chunk() const
	{
		return (get_chunk_id()) == vqa_vqfr_id;
	}

	int get_cbits_pixel() const
	{
		return m_header.video_flags & 0x10 ? 16 : 8;
	}

	// Codebook group size (CBPZ chunks per full-codebook commit). VQHD
	// payload byte 13, i.e. high byte of unknown3. Falls back to 8 which
	// is what the original XCC decoder hardcoded.
	int get_groupframes() const
	{
		int g = (m_header.unknown3 >> 8) & 0xff;
		return g ? g : 8;
	}

	int get_cbits_sample() const
	{
		return m_header.cbits_sample;
	}
private:
	t_vqa_chunk_header m_chunk_header;
	t_vqa_header m_header;
};
