#pragma once

#include "../Common.h"
#include "../Span.h"

NS_BF_BEGIN


class ZipFile
{
public:
	class Data;

public:
	Data* mData;

public:
	ZipFile();
	~ZipFile();

	bool Open(const StringImpl& filePath);
	bool Create(const StringImpl& filePath);
	bool Close();
	bool IsOpen();
	bool Add(const StringImpl& fileName, Span<uint8> data);
	bool Get(const StringImpl& fileName, Array<uint8>& data);
};


NS_BF_END