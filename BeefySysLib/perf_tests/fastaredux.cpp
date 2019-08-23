#pragma warning(disable:4838)
#pragma warning(disable:4305)

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MIN(x, y) ((x < y) ? x : y)
#define LINELEN 60
#define SLOTS 4095

struct aminoacid {
	char c;
	float p;
};

static struct aminoacid *lu[SLOTS + 1];

static void repeat_fasta(const char *alu, size_t n)
{
	const size_t alulen = strlen(alu);
	assert(alulen < 1024);
	char buf[1024 + LINELEN];
	size_t pos = 0, bytes;

	memcpy(buf, alu, alulen);
	memcpy(buf + alulen, alu, LINELEN);
	while (n) {
		bytes = MIN(LINELEN, n);
		//fwrite_unlocked(buf + pos, bytes, 1, stdout);
		//putchar_unlocked('\n');
		pos += bytes;
		if (pos > alulen)
			pos -= alulen;
		n -= bytes;
	}
}

static void acc_probs(struct aminoacid *table)
{
	struct aminoacid *iter = table;

	while ((++iter)->c) {
		iter->p += (iter - 1)->p;
	}
	for (int i = 0; i <= SLOTS; ++i) {
		while (i > (table->p * SLOTS))
			++table;
		lu[i] = table;
	}
}

static float rng(float max)
{
	const unsigned int IM = 139968, IA = 3877, IC = 29573;
	static unsigned int seed = 42;

	seed = (seed * IA + IC) % IM;
	return max * seed / IM;
}

static char nextc()
{
	float r;
	struct aminoacid *iter;

	r = rng(1.0f);
	iter = lu[(int) (r * SLOTS)];
	while (iter->p < r)
		++iter;
	return iter->c;
}

static void random_fasta(struct aminoacid *table, size_t n)
{
	size_t i, lines = n / LINELEN;
	const size_t chars_left = n % LINELEN;
	char buf[LINELEN + 1];

	while (lines--) {
		for (i = 0; i < LINELEN; ++i) {
			buf[i] = nextc();
		}
		buf[i] = '\n';
		//fwrite_unlocked(buf, i + 1, 1, stdout);
	}
	for (i = 0; i < chars_left; ++i)
		buf[i] = nextc();
	//fwrite_unlocked(buf, i, 1, stdout);
}

void FastaRedux(int n)
{
	//const size_t n = (argc > 1) ? atoi(argv[1]) : 1000;
	const char alu [] =
		"GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTG"
		"GGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGA"
		"GACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAA"
		"AATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAAT"
		"CCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAAC"
		"CCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTG"
		"CACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";
	struct aminoacid iub [] = {
		{ 'a', 0.27 },
		{ 'c', 0.12 },
		{ 'g', 0.12 },
		{ 't', 0.27 },
		{ 'B', 0.02 },
		{ 'D', 0.02 },
		{ 'H', 0.02 },
		{ 'K', 0.02 },
		{ 'M', 0.02 },
		{ 'N', 0.02 },
		{ 'R', 0.02 },
		{ 'S', 0.02 },
		{ 'V', 0.02 },
		{ 'W', 0.02 },
		{ 'Y', 0.02 },
		{ 0, 0 }
	};
	struct aminoacid homosapiens [] = {
		{ 'a', 0.3029549426680 },
		{ 'c', 0.1979883004921 },
		{ 'g', 0.1975473066391 },
		{ 't', 0.3015094502008 },
		{ 0, 0 }
	};

	//fputs_unlocked(">ONE Homo sapiens alu\n", stdout);
	repeat_fasta(alu, n * 2);
	//fputs_unlocked(">TWO IUB ambiguity codes\n", stdout);
	acc_probs(iub);
	random_fasta(iub, n * 3);
	//fputs_unlocked(">THREE Homo sapiens frequency\n", stdout);
	acc_probs(homosapiens);
	random_fasta(homosapiens, n * 5);
	//putchar_unlocked('\n');
}