#pragma once

#include "ImageData.h"

NS_BF_BEGIN;

class PNGData : public ImageData
{
public:	
	int						mReadPos;

public:		
	PNGData();

	bool					ReadData();							
	bool					WriteToFile(const StringImpl& path);
};

NS_BF_END;
