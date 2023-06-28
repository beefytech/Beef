#pragma once

#include "../Common.h"
#include "Array.h"
#include "Span.h"

NS_BF_BEGIN;

class Compression
{
public:
	static bool Compress(Span<uint8> inData, Array<uint8>& outData);
	static bool Decompress(Span<uint8> inData, Array<uint8>& outData);
};

NS_BF_END;
