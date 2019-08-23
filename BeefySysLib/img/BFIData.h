#pragma once

#include "Common.h"
#include "ImageData.h"

NS_BF_BEGIN;

class BFIData : ImageData
{
public:
	void					Compress(ImageData* source);
};

NS_BF_END;
