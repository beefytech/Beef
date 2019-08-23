#pragma once

#include "Common.h"

NS_BF_BEGIN;

class DataStream
{
public:
	bool mBigEndian;
	int mBitPos;
	uint32 mCurBitInt;
	int mReadBitIntPos; // in bits

protected:
	uint32					GetBitInt(int bitPos);

public:
	DataStream();
	virtual ~DataStream() {}

	virtual bool			Eof() = 0;
	virtual int				GetSize() = 0;
	virtual void			Read(void* ptr, int size) = 0;
	virtual void			Write(void* ptr, int size) = 0;
	virtual void			WriteZeros(int size);
	virtual void			Align(int size);

	virtual int				GetPos() = 0;
	virtual void			Seek(int size) = 0;
	virtual void			SetPos(int pos) = 0;

	virtual int8			ReadInt8();
	virtual uint8			ReadUInt8();
	virtual int16			ReadInt16();
	virtual uint16			ReadUInt16();
	virtual int32			ReadInt32();
	virtual int64			ReadInt64();
	virtual float			ReadFloat();
	virtual double			ReadDouble();
	virtual String			ReadAscii8SizedString();	
	virtual String			ReadAscii32SizedString();	
	virtual String			ReadUnicode32SizedString();
	virtual String			ReadSZ();

	template <typename T>
	void ReadT(const T& val)
	{
		Read((void*) &val, sizeof(T));
	}

	virtual void			Write(float val);
	virtual void			Write(uint8 val);
	virtual void			Write(int8 val);
	virtual void			Write(int16 val);
	virtual void			Write(int32 val);
	virtual void			Write(int64 val);
	virtual void			Write(const StringImpl& val);
	virtual void			Write(DataStream& refStream);
	virtual void			WriteSNZ(const StringImpl& val);
	virtual void			WriteSZ(const StringImpl& val);
	
	template <typename T>
	void WriteT(const T& val)
	{
		Write((void*)&val, sizeof(T));
	}
	
	virtual void			SyncBitPos();
	virtual void			SyncBytePos();	
	virtual int				ReadUBits(int bits);	
	virtual void			SeekBits(int bits);	
};

NS_BF_END;
