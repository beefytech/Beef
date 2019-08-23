#pragma once

#include "DataStream.h"

NS_BF_BEGIN

class CachedDataStream : public DataStream
{
public:
	const static int CHUNK_SIZE = 8192;

	DataStream* mStream;
	uint8 mChunk[CHUNK_SIZE];

	uint8* mDataPtr;
	uint8* mDataEnd;

public:
	void Flush();

public:
	CachedDataStream(DataStream* stream);
	~CachedDataStream();	

	virtual bool			Eof() override;
	virtual int				GetSize() override;
	virtual void			Read(void* ptr, int size) override;
	virtual void			Write(void* ptr, int size) override;

	virtual int				GetPos() override;
	virtual void			Seek(int size) override;
	virtual void			SetPos(int pos) override;
};

NS_BF_END