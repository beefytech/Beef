#pragma once

#include "Common.h"
#include "ImageData.h"

NS_BF_BEGIN;

class JPEGHuffmanTable;
class JPEGComponentInfo;
class JPEGDataStream;

class JPEGData : public ImageData
{
public:
	bool					ReadData();
	void					Compress(int quality);
};

NS_BF_END;
