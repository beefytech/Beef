
#pragma warning(disable:4996)
#include "BeLibManger.h"
#include "BeefySysLib/util/BeefPerf.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

BeLibManager gBfLibManager;

void BeLibEntry::AddSymbol(const StringImpl& sym)
{
	mSymbols.push_back(sym);
}

//////////////////////////////////////////////////////////////////////////

BeLibFile::BeLibFile()
{
	mFailed = false;
}

BeLibFile::~BeLibFile()
{
	for (auto& entry : mEntries)
		delete entry.mValue;
	for (auto& entry : mOldEntries)
		delete entry.mValue;
}

bool BeLibFile::ReadLib()
{
	BP_ZONE("BeLibFile::ReadLib");

	char fileId[8];
	mOldFileStream.Read(fileId, 8);
	if (strncmp(fileId, "!<arch>\n", 8) != 0)
		return false;
	
	const char* libStrTable = NULL;

	Dictionary<int, BeLibEntry*> pendingLibEntryMap;

	int memberIdx = 0;
	while (true)
	{
		int headerFilePos = mOldFileStream.GetPos();

		BeLibMemberHeader header;
		mOldFileStream.Read(&header, sizeof(header));
		if (mOldFileStream.mReadPastEnd)
			break;

		int len = atoi(header.mSize);

		if (strncmp(header.mName, "/ ", 2) == 0)
		{
			if (memberIdx == 0)
			{
				uint8* data = new uint8[len];
				mOldFileStream.Read(data, len);

				int numSymbols = FromBigEndian(*(int32*)data);
				
				uint8* strTab = data + 4 + numSymbols * 4;

				for (int symIdx = 0; symIdx < numSymbols; symIdx++)
				{					
					const char* str = (char*)strTab;
					strTab += strlen((char*)strTab) + 1;

					int offset = FromBigEndian(((int32*)(data + 4))[symIdx]);
					
					BeLibEntry* pendingEntry;					
					
					BeLibEntry** pendingEntryPtr = NULL;
					if (!pendingLibEntryMap.TryAdd(offset, NULL, &pendingEntryPtr))
					{
						pendingEntry = *pendingEntryPtr;
					}
					else
					{
						pendingEntry = new BeLibEntry();
						pendingEntry->mLibFile = this;
						*pendingEntryPtr = pendingEntry;
					}

					pendingEntry->mSymbols.push_back(str);
				}

				delete data;
			}
			else
			{

			}
		}
		else if (strncmp(header.mName, "// ", 3) == 0)
		{
			libStrTable = new char[len];
			mOldFileStream.Read((uint8*)libStrTable, len);
		}
		else
		{
			String fileName;
			if (header.mName[0] == '/')
			{
				int tabIdx = atoi(&header.mName[1]);
				for (int checkIdx = tabIdx; true; checkIdx++)
				{
					char c = libStrTable[checkIdx];
					if ((c == 0) || (c == '/'))
					{
						fileName.Append(&libStrTable[tabIdx], checkIdx - tabIdx);
						break;
					}
				}				
			}
			else
			{
				for (int i = 0; i < 16; i++)
				{
					if (header.mName[i] == '/')
						fileName.Append(&header.mName[0], i);
				}
			}
			
			BeLibEntry* libEntry = NULL;
			
			if (!pendingLibEntryMap.TryGetValue(headerFilePos, &libEntry))
			{
				libEntry = new BeLibEntry();
				libEntry->mLibFile = this;
			}

			BeLibEntry** namedEntry;
			if (mOldEntries.TryAdd(fileName, NULL, &namedEntry))
			{
				*namedEntry = libEntry;
			}
			else
			{
				auto prevEntry = *namedEntry;
				libEntry->mNextWithSameName = prevEntry->mNextWithSameName;
				prevEntry->mNextWithSameName = libEntry;
			}

			libEntry->mName = fileName;
			libEntry->mOldDataPos = headerFilePos;
			libEntry->mLength = len;
		}

		len = BF_ALIGN(len, 2); // Even addr
		mOldFileStream.SetPos(headerFilePos + sizeof(BeLibMemberHeader) + len);

		memberIdx++;
	}

	for (auto& entry : pendingLibEntryMap)
	{
		auto libEntry = entry.mValue;
		// Not used?
		if (libEntry->mOldDataPos == -1)
			delete libEntry;
	}

	delete libStrTable;

	return true;
}

bool BeLibFile::Init(const StringImpl& filePath, bool moveFile)
{	
	bool isInitialized = false;		

	if (FileExists(filePath))
	{		
		String altName;
		if (moveFile)
		{
			altName = filePath + "_";
			if (!::MoveFileA(filePath.c_str(), altName.c_str()))
			{
				::DeleteFileA(altName.c_str());
				if (!::MoveFileA(filePath.c_str(), altName.c_str()))
				{
					mFilePath = filePath;
					return false;
				}
			}
		}
		else
			altName = filePath;
		mOldFilePath = altName;

		if (!mOldFileStream.Open(altName, "rb"))
			return false;
		
		if (!ReadLib())
			return false;
	}	
	
	String newLibName = filePath;	
	mFilePath = newLibName;
	
	return true;
}

bool BeLibFile::Finish()
{
	BP_ZONE("BeLibFile::Finish");

	//mOldEntries.clear();
	
	Dictionary<String, BeLibEntry*>* libEntryMaps[2] = { &mEntries, &mOldEntries };	
	
	Array<BeLibEntry*> libEntries;

	bool isAllReferenced = true;
	for (auto entryMap : libEntryMaps)
	{
		for (auto& libEntryPair : *entryMap)
		{
			auto libEntry = libEntryPair.mValue;
			if (libEntry->mReferenced)
				libEntries.push_back(libEntry);
			else
				isAllReferenced = false;
		}
	}

	if ((isAllReferenced) && (mEntries.IsEmpty()) && (mOldFileStream.IsOpen()))
	{
		mOldFileStream.Close();
		String altName = mFilePath + "_";
		if (::MoveFileA(altName.c_str(), mFilePath.c_str()))
		{
			return true; // There are no changes
		}
	}

	if (!mFileStream.Open(mFilePath, "wb"))
	{		
		mFailed = true;
		return false;
	}

	mFileStream.Write("!<arch>\n", 8);

	std::sort(libEntries.begin(), libEntries.end(), 
			[&](BeLibEntry* lhs, BeLibEntry* rhs)
		{
			return lhs->mName < rhs->mName;
		});

	int longNamesSize = 0;

	int tabSize = 4; // num symbols	
	int numSymbols = 0;
	for (auto libEntry : libEntries)
	{
		if (libEntry->mName.length() > 15)
		{				
			longNamesSize += (int)libEntry->mName.length() + 2;
		}

		for (auto& sym : libEntry->mSymbols)
		{
			numSymbols++;
			tabSize += 4; // Offset
			tabSize += (int)sym.length() + 1; // String table
		}	
	}
	
	// Determine where all these entries will be placed
	int predictPos = mFileStream.GetPos() + sizeof(BeLibMemberHeader) + BF_ALIGN(tabSize, 2);

	if (longNamesSize > 0)
		predictPos += sizeof(BeLibMemberHeader) + BF_ALIGN(longNamesSize, 2);	

	for (auto libEntry : libEntries)
	{
		libEntry->mNewDataPos = predictPos;
			
		predictPos += sizeof(BeLibMemberHeader);
		predictPos += BF_ALIGN(libEntry->mLength, 2);	
	}

	int tabStartPos = mFileStream.GetPos();
	
	
	BeLibMemberHeader header;
	header.Init("/", "0", tabSize);	
	mFileStream.WriteT(header);

	mFileStream.Write(ToBigEndian((int32)numSymbols));
	// Offset table
	for (auto libEntry : libEntries)
	{
		for (auto& sym : libEntry->mSymbols)
		{
			mFileStream.Write((int32)ToBigEndian(libEntry->mNewDataPos));
		}		
	}

	// String map table
	for (auto libEntry : libEntries)
	{
		for (auto& sym : libEntry->mSymbols)
		{
			mFileStream.Write((uint8*)sym.c_str(), (int)sym.length() + 1);
		}	
	}

	int actualTabSize = mFileStream.GetPos() - tabStartPos - sizeof(BeLibMemberHeader);
	
	//return true;

	if ((tabSize % 2) != 0)
		mFileStream.Write((uint8)0);

	BF_ASSERT(actualTabSize == tabSize);

	// Create long names table
	if (longNamesSize > 0)
	{
		header.Init("//", "0", longNamesSize);		
		mFileStream.WriteT(header);

		for (auto libEntry : libEntries)
		{
			if (libEntry->mName.length() > 15)				
			{
				mFileStream.Write((uint8*)libEntry->mName.c_str(), (int)libEntry->mName.length());
				mFileStream.Write("/\n", 2);
			}			
		}

		if ((longNamesSize % 2) != 0)
			mFileStream.Write((uint8)'\n');
	}

	int longNamesPos = 0;
	for (auto libEntry : libEntries)
	{
		int actualPos = mFileStream.GetPos();
		BF_ASSERT(actualPos == libEntry->mNewDataPos);

		String entryName;

		if (libEntry->mName.length() > 15)
		{			
			char idxStr[32]; 
			_itoa(longNamesPos, idxStr, 10);				
			entryName = "/";
			entryName += idxStr;
			longNamesPos += (int)libEntry->mName.length() + 2;
		}
		else
		{
			entryName = libEntry->mName;
			entryName += "/";
		}

		header.Init(entryName.c_str(), "644", libEntry->mLength);	
		mFileStream.WriteT(header);

		if (libEntry->mOldDataPos != -1)
		{
			uint8* data = new uint8[libEntry->mLength];
			mOldFileStream.SetPos(libEntry->mOldDataPos + sizeof(BeLibMemberHeader));
			mOldFileStream.Read(data, libEntry->mLength);
			mFileStream.Write(data, libEntry->mLength);
			delete data;
		}
		else if (libEntry->mData.size() != 0)
		{
			mFileStream.Write((uint8*)&libEntry->mData[0], (int)libEntry->mData.size());				
		}

		if ((libEntry->mLength % 2) != 0)
			mFileStream.Write((uint8)0);	
	}

	mFileStream.Close();
	mOldFileStream.Close();
	::DeleteFileA(mOldFilePath.c_str());

	return true;
}

//////////////////////////////////////////////////////////////////////////

BeLibManager::BeLibManager()
{
	/*BeLibFile libFile;
	libFile.Init("Hey");*/

	/*BeLibFile libFile;
	libFile.Init("c:/beef/IDE/mintest/build/Debug_Win64/minlib/minlib__.lib");
	libFile.Finish();*/

	/*BeLibFile libFile2;
	libFile2.Init("c:\\temp\\Beefy2D.lib_new");*/
}

BeLibManager::~BeLibManager()
{
	Clear();
}

void BeLibManager::Clear()
{
	BP_ZONE("BeLibManager::Clear");

	for (auto& entry : mLibFiles)
		delete entry.mValue;
	mLibFiles.Clear();
}

BeLibEntry* BeLibManager::AddFile(const StringImpl& filePath, void* data, int size)
{
	BP_ZONE("BeLibManager::AddFile");

	String fileDir = GetFileDir(filePath);
	String fileName = GetFileName(filePath);

	String fixedFileDir = FixPathAndCase(fileDir);

	AutoCrit autoCrit(mCritSect);

	BeLibFile* libFile = NULL;

// 	if ((data == NULL) && (!mLibFiles.ContainsKey(fixedFileDir)))
// 		return NULL;

	BeLibFile** libFilePtr = NULL;
	if (!mLibFiles.TryAdd(fixedFileDir, NULL, &libFilePtr))
	{
		libFile = *libFilePtr;
	}
	else
	{		
		libFile = new BeLibFile();
		*libFilePtr = libFile;

		String libPath = GetLibFilePath(filePath);
		libFile->Init(libPath, true);
	}

	if (libFile->mFailed)
		return NULL;

	BeLibEntry* oldEntry = NULL;
	if (libFile->mOldEntries.TryGetValue(fileName, &oldEntry))
	{
		if (data == NULL)
		{
			oldEntry->mReferenced = true;
			return oldEntry;
		}

		delete oldEntry;
		libFile->mOldEntries.Remove(fileName);
	}
	else
	{
		if (data == NULL)
			return NULL;
	}

	BeLibEntry* libEntry = NULL;
	BeLibEntry** libEntryPtr = NULL;
	libFile->mEntries.TryAdd(fileName, NULL, &libEntryPtr);
	if (*libEntryPtr != NULL)
	{
		// It's possible that we rebuild a type (generic, probably), decide we don't have any refs so we delete the type,
		//  but then we specialize methods and then have to recreate it.  Thus two entries here.
		delete *libEntryPtr;
	}	
	libEntry = new BeLibEntry();
	libEntry->mLibFile = libFile;
	*libEntryPtr = libEntry;

	libEntry->mReferenced = true;
	libEntry->mName = fileName;
	libEntry->mData.Insert(0, (uint8*)data, size);
	libEntry->mLength = size;
	
	return libEntry;
}

bool BeLibManager::AddUsedFileName(const StringImpl& fileName)
{
	return AddFile(fileName, NULL, -1) != NULL;
}

void BeLibManager::Finish()
{
	BP_ZONE("BeLibManager::Finish");

	AutoCrit autoCrit(mCritSect);

	for (auto& libPair : mLibFiles)
	{
		auto libFile = libPair.mValue;
		if (!libFile->mFilePath.IsEmpty())
		{
			if (!libFile->Finish())
			{
				mErrors.Add(StrFormat("Failed to write lib file '%s'", libFile->mFilePath.c_str()));
			}
		}
		delete libFile;
	}
	mLibFiles.Clear();
}

String BeLibManager::GetLibFilePath(const StringImpl& objFilePath)
{
	String fileDir = RemoveTrailingSlash(GetFileDir(objFilePath));
	return fileDir + "/" + GetFileName(fileDir) + "__.lib";
}

BeLibManager* BeLibManager::Get()
{
	return &gBfLibManager;
}

