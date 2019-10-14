#include "DataStream.h"

USING_NS_BF;

DataStream::DataStream()
{
	mBigEndian = false;
	mBitPos = 0;
	mReadBitIntPos = -1000;
}

int8 DataStream::ReadInt8()
{
	int8 anInt8;
	Read(&anInt8, sizeof(int8));
	return anInt8;
}

uint8 DataStream::ReadUInt8()
{
	return (uint8) ReadInt8();
}

int16 DataStream::ReadInt16()
{
	int16 anInt16;
	Read(&anInt16, sizeof(int16));
	if (mBigEndian)
		return FromBigEndian(anInt16);
	return anInt16;
}

uint16 DataStream::ReadUInt16()
{
	return (uint16) ReadInt16();
}

int32 DataStream::ReadInt32()
{
	int32 anInt32;
	Read(&anInt32, sizeof(int32));
	if (mBigEndian)
		return FromBigEndian(anInt32);
	return anInt32;
}

int64 Beefy::DataStream::ReadInt64()
{
	int64 anInt64;
	Read(&anInt64, sizeof(int64));
	//if (mBigEndian)
		//return FromBigEndian(anInt32);
	return anInt64;
}


float DataStream::ReadFloat()
{
	if (mBigEndian)
	{
		int32 anInt32;
		Read(&anInt32, sizeof(int32));
		anInt32 = FromBigEndian(anInt32);
		return *((float*) &anInt32);
	}
	else
	{
		float aFloat;
		Read(&aFloat, sizeof(float));
		return aFloat;
	}
}

double DataStream::ReadDouble()
{
	if (mBigEndian)
	{
		int64 anInt64;
		Read(&anInt64, sizeof(int64));
		anInt64 = FromBigEndian(anInt64);
		return *((double*) &anInt64);
	}
	else
	{
		double aDouble;
		Read(&aDouble, sizeof(double));
		return aDouble;
	}
}

String DataStream::ReadAscii8SizedString()
{
	int size = (int) (uint8) ReadInt8();

	String aString;
	aString.Append(' ', size);
	Read((void*) aString.c_str(), size);

	return aString;
}

String DataStream::ReadAscii32SizedString()
{
	int size = (int) ReadInt32();
	if (size == 0)
		return String();

	String aString;
	aString.Append(' ', size);
	Read((void*) aString.c_str(), size);

	return aString;
}

String DataStream::ReadUnicode32SizedString()
{
	int size = ReadInt32();

	UTF16String aString;	
	aString.ResizeRaw(size + 1);
	aString[size] = 0;
	Read((void*)aString.c_str(), size * 2);

	if (mBigEndian)
		for (int i = 0; i < (int) aString.length(); i++)
			aString[i] = FromBigEndian((int16) aString[i]);

	return UTF8Encode(aString);
}

String DataStream::ReadSZ()
{
	String str;
	while (true)
	{
		char c = (char)ReadUInt8();
		if (c == 0)
			break;
		str += c;
	}
	return str;
}

void DataStream::SyncBitPos()
{
	mBitPos = GetPos() * 8;
}

void DataStream::SyncBytePos()
{
	SetPos((mBitPos+7) / 8);
}


//int bitMaskR[32] = {
static int LowerBitMask[33] = {0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 
	0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF, 0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 
	0x7FFFFFFF, (int)0xFFFFFFFF};

int DataStream::ReadUBits(int bits)
{
	int val = 0;
	int valBitPos = 0;

	while (true)
	{
		int bitsAvail = (mReadBitIntPos + 32) - mBitPos;
		if ((bitsAvail > 0) && (bitsAvail <= 32))
		{
			int copyBits = std::min(bits - valBitPos, bitsAvail);
			val |= ((mCurBitInt >> (32 - bitsAvail)) & LowerBitMask[copyBits]) << valBitPos;
			valBitPos += copyBits;
			mBitPos += copyBits;
		}

		if (valBitPos == bits)
			break;
		
		mReadBitIntPos = mBitPos & ~7;
		SetPos(mReadBitIntPos / 8);
		Read(&mCurBitInt, 4);		
	}

	return val;
}

void DataStream::SeekBits(int bits)
{
	mBitPos += bits;
}

void Beefy::DataStream::Write(int32 val)
{
	Write(&val, sizeof(int32));
}

void Beefy::DataStream::Write(int64 val)
{
	Write(&val, sizeof(int64));
}

void Beefy::DataStream::Write(const StringImpl& val)
{
	Write((int)val.length());
	Write((void*)val.c_str(), (int)val.length());
}

void DataStream::Write(DataStream& refStream)
{
	int size = refStream.GetSize();
	uint8* data = new uint8[size];
	refStream.SetPos(0);
	refStream.Read(data, size);
	Write(data, size);
	delete [] data;
}

void DataStream::WriteSNZ(const StringImpl& val)
{
	Write((void*)val.c_str(), (int)val.length());
}

void DataStream::WriteSZ(const StringImpl& val)
{
	Write((void*)val.c_str(), (int)val.length() + 1);
}

void Beefy::DataStream::Write(float val)
{
	Write(&val, sizeof(float));
}

void DataStream::Write(uint8 val)
{
	Write(&val, 1);
}

void DataStream::Write(int8 val)
{
	Write((uint8)val);
}

void DataStream::Write(int16 val)
{
	Write(&val, 2);
}

void DataStream::WriteZeros(int size)
{
	int sizeLeft = size;
	while (sizeLeft > 0)
	{
		if (sizeLeft >= 8)
		{
			Write((int64)0);
			sizeLeft -= 8;
		}
		else if (sizeLeft >= 4)
		{
			Write((int32)0);
			sizeLeft -= 4;
		}
		else if (sizeLeft >= 2)
		{
			Write((int16)0);
			sizeLeft -= 2;
		}
		else
		{
			Write((int8)0);
			sizeLeft -= 1;
		}
	}
}

void DataStream::Align(int alignSize)
{
	int curPos = GetPos();
	int alignBytesLeft = (alignSize - (curPos % alignSize)) % alignSize;
	WriteZeros(alignBytesLeft);	
}
