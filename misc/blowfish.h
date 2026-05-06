#pragma once

#include <vartypes.h>
#include <xcc/data_ref.h>

class Cblowfish
{
public:
	void Submit_Key(data_ref key);

	int Encrypt(void const* plaintext,  void* cyphertext, int length);
	int Decrypt(void const* cyphertext, void* plaintext, int length);

	/*
	**	This is the maximum key length supported.
	*/
	enum
	{
		MAX_KEY_LENGTH = 56
	};

private:
	bool IsKeyed;

	void Sub_Key_Encrypt(unsigned int& left, unsigned int& right);

	void Process_Block(void const* plaintext, void* cyphertext, unsigned int const* ptable);

	enum
	{
		ROUNDS = 16,        // Feistal round count (16 is standard).
		BYTES_PER_BLOCK = 8 // The number of bytes in each cypher block (don't change).
	};

	/*
	**	Initialization data for sub keys. The initial values are constant and
	**	filled with a number generated from pi. Thus they are not random but
	**	they don't hold a weak pattern either.
	*/
	static unsigned int const P_Init[(int)ROUNDS + 2];
	static unsigned int const S_Init[4][UCHAR_MAX + 1];

	/*
	**	Permutation tables for encryption and decryption.
	*/
	unsigned int P_Encrypt[(int)ROUNDS + 2];
	unsigned int P_Decrypt[(int)ROUNDS + 2];

	/*
	**	S-Box tables (four).
	*/
	unsigned int bf_S[4][UCHAR_MAX + 1];
};
