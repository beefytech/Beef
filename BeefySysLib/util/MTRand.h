#pragma once

#include "Common.h"

NS_BF_BEGIN;

#define MTRAND_N 624

class MTRand
{
	unsigned long mt[MTRAND_N]; /* the array for the state vector  */
	int mti;

public:
	MTRand(const std::string& theSerialData);
	MTRand(unsigned long seed);
	MTRand();

	void SRand(const std::string& theSerialData);
	void SRand(unsigned long seed);	
	unsigned long Next();	
	unsigned long Next(unsigned long range);	
	float Next(float range);

	String Serialize();
};

NS_BF_END