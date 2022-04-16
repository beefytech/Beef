#include "ZipFile.h"

extern "C"
{
#include "miniz/miniz.h"
}

USING_NS_BF;

class ZipFile::Data
{
public:
	bool mIsWriter;
	mz_zip_archive mZip;
};

ZipFile::ZipFile()
{
	mData = NULL;
}

ZipFile::~ZipFile()
{
	if (mData != NULL)
		Close();
}

bool ZipFile::Open(const StringImpl& filePath)
{
	if (mData != NULL)
		Close();

	mData = new ZipFile::Data();
	memset(mData, 0, sizeof(ZipFile::Data));	
	if (!mz_zip_reader_init_file(&mData->mZip, filePath.c_str(), 0))
		return false;

	return true;
}

bool ZipFile::Create(const StringImpl& filePath)
{
	if (mData != NULL)
		Close();

	mData = new ZipFile::Data();	
	memset(mData, 0, sizeof(ZipFile::Data));	
	if (!mz_zip_writer_init_file(&mData->mZip, filePath.c_str(), 0))	
	{
		delete mData;
		mData = NULL;
		return false;
	}

	mData->mIsWriter = true;
	return true;
}

bool ZipFile::Close()
{
	if (mData == NULL)
		return false;
	
	if (mData->mIsWriter)
	{
		if (!mz_zip_writer_finalize_archive(&mData->mZip))
			return false;
		if (!mz_zip_writer_end(&mData->mZip))
			return false;
	}
	else
	{
		if (!mz_zip_reader_end(&mData->mZip))
			return false;
	}

	return true;
}

bool ZipFile::IsOpen()
{
	return mData != NULL;
}

bool ZipFile::Add(const StringImpl& fileName, Span<uint8> data)
{
	if (mData == NULL)
		return false;

	if (!mz_zip_writer_add_mem(&mData->mZip, fileName.c_str(), data.mVals, data.mSize, MZ_NO_COMPRESSION))
		return false;

	return true;
}


bool ZipFile::Get(const StringImpl& fileName, Array<uint8>& data)
{
	if (mData == NULL)
		return false;

	int idx = mz_zip_reader_locate_file(&mData->mZip, fileName.c_str(), NULL, 0);
	if (idx < 0)
		return false;

	size_t size = 0;
	void* ptr = mz_zip_reader_extract_to_heap(&mData->mZip, idx, &size, 0);
	data.Insert(data.mSize, (uint8*)ptr, (intptr)size);
	return true;
}