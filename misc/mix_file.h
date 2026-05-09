#pragma once

#include "cc_file.h"
#include "cc_structures.h"

class Cmix_file : public Ccc_file  
{
public:
	ostream& extract_as_text(ostream&) const;
	virtual int post_open();
	string get_name(int id);
	static int get_id(t_game game, string name);
	int get_index(unsigned int id) const;
	using Ccc_file::get_size;
	using Ccc_file::vdata;
	Cvirtual_binary get_vdata(int id);
	Cvirtual_binary get_vdata(const string& name);
	virtual bool is_valid();
	void close();
	Cmix_file();

	static void enable_ft_support()
	{
		assert(!m_ft_support);
		m_ft_support = true;
	}

	void enable_mix_expansion()
	{
		assert(!m_mix_expansion);
		m_mix_expansion = true;
	}

	auto get_c_files() const
	{
		return m_index.size();
	}

	t_game get_game() const
	{
		return m_game;
	}

	void set_game(t_game game)
	{
		m_game = game;
	}

	t_file_type get_type(int id)
	{
		// Corrupted / encrypted-header MIXes can leave m_index_ft empty
		// even when m_index is populated (post_open's per-file probe is
		// gated on `vdata().size() == get_size()`). Fall back to ft_unknown
		// instead of indexing past end and crashing the listview.
		int i = get_index(id);
		if (i < 0 || static_cast<size_t>(i) >= m_index_ft.size())
			return ft_unknown;
		return m_index_ft[i];
	}

	int get_id(int index) const
	{
		return m_index[index].id;
	}

	int get_offset(unsigned int id) const
	{
		assert(get_index(id) != -1);
		return m_index[get_index(id)].offset;
	}

	int get_size(unsigned int id) const
	{
		assert(get_index(id) != -1);
		return m_index[get_index(id)].size;
	}

	bool has_checksum() const
	{
		return m_has_checksum;	
	}

	bool is_encrypted() const
	{
		return m_is_encrypted;	
	}

	int rawflags() const
	{
		return m_rawflagvalue;
	}

	const t_mix_index_entry* index() const
	{
		return &m_index[0];
	}
protected:
	using t_id_index = map<int, int>;

	static bool m_ft_support;

	t_game m_game;
	bool m_mix_expansion = false;
	bool m_is_encrypted;
	bool m_has_checksum;
	int m_rawflagvalue;
	vector<t_mix_index_entry> m_index;
	vector<t_file_type> m_index_ft;
	t_id_index m_id_index;
};
