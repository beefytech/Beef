/*
Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. The names of its contributors may not be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Any feedback is very welcome.
http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)
*/

#include "MTRand.h"

USING_NS_BF;

/* Period parameters */
#define MTRAND_M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

/* Tempering parameters */
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)


MTRand::MTRand(const std::string& theSerialData)
{
	SRand(theSerialData);
	mti = MTRAND_N + 1; /* mti==MTRAND_N+1 means mt[MTRAND_N] is not initialized */
}

MTRand::MTRand(unsigned long seed)
{
	SRand(seed);
}

MTRand::MTRand()
{
	SRand(4357);
}

void MTRand::SRand(const std::string& theSerialData)
{
	if (theSerialData.size() == MTRAND_N * 4)
	{
		memcpy(mt, theSerialData.c_str(), MTRAND_N * 4);
	}
	else
		SRand(4357);
}

void MTRand::SRand(unsigned long seed)
{
	if (seed == 0)
		seed = 4357;

	/* setting initial seeds to mt[MTRAND_N] using         */
	/* the generator Line 25 of Table 1 in          */
	/* [KNUTH 1981, The Art of Computer Programming */
	/*    Vol. 2 (2nd Ed.), pp102]                  */
	mt[0] = seed & 0xffffffffUL;
	for (mti = 1; mti < MTRAND_N; mti++)
	{
		mt[mti] =
			(1812433253UL * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + mti);
		/* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
		/* In the previous versions, MSBs of the seed affect   */
		/* only MSBs of the array mt[].                        */
		/* 2002/01/09 modified by Makoto Matsumoto             */
		mt[mti] &= 0xffffffffUL;
		/* for >32 bit machines */
	}
}

unsigned long MTRand::Next()
{
	unsigned long y;
	static unsigned long mag01[2] = { 0x0, MATRIX_A };
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

	if (mti >= MTRAND_N) { /* generate MTRAND_N words at one time */
		int kk;

		for (kk = 0; kk < MTRAND_N - MTRAND_M; kk++) {
			y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
			mt[kk] = mt[kk + MTRAND_M] ^ (y >> 1) ^ mag01[y & 0x1UL];
		}
		for (; kk < MTRAND_N - 1; kk++) {
			y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
			mt[kk] = mt[kk + (MTRAND_M - MTRAND_N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
		}
		y = (mt[MTRAND_N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
		mt[MTRAND_N - 1] = mt[MTRAND_M - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];

		mti = 0;
	}

	y = mt[mti++];
	y ^= TEMPERING_SHIFT_U(y);
	y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
	y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
	y ^= TEMPERING_SHIFT_L(y);

	y &= 0x7FFFFFFF;

	/*char aStr[256];
	sprintf(aStr, "Rand=%d\r\n", y);
	OutputDebugString(aStr);*/

	return y;
}

unsigned long MTRand::Next(unsigned long range)
{
	return Next() % range;
}

String MTRand::Serialize()
{
	String aString;

	aString.Append(' ', MTRAND_N * 4);
	memcpy((char*)aString.c_str(), mt, MTRAND_N * 4);

	return aString;
}