#pragma warning(disable:4996)

#define MAX_SEG_COUNT 1024

#include "BlContext.h"
#include "BlCvParser.h"
#include "BlCodeView.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "../Compiler/BfAstAllocator.h"
#include "../COFFData.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/CachedDataStream.h"
#include "../Backend/BeCOFFObject.h"
#include "../Compiler/BfDemangler.h"
#include <time.h>
#include <iostream>
#include <stdlib.h>
#include <direct.h>
#include "codeview/cvinfo.h"

#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

USING_NS_BF;

//#include "BeefySysLib/third_party/sparsehash/google/sparsehash/densehashtable.h"

//////////////////////////////////////////////////////////////////////////

BlChunk* BlSegment::AddChunk(void* data, int size, int characteristics, int& offset)
{	
	if (size == 0)
		return NULL;

	if ((characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0)
	{
		BF_ASSERT(data == NULL);
	}
	else
	{
		BF_ASSERT(data != NULL);
	}

	int align = 1;
	int chAlign = characteristics & IMAGE_SCN_ALIGN_MASK;
	if (chAlign == IMAGE_SCN_ALIGN_8192BYTES)
		align = 8192;
	else if (chAlign == IMAGE_SCN_ALIGN_4096BYTES)
		align = 4096;
	else if (chAlign == IMAGE_SCN_ALIGN_2048BYTES)
		align = 2048;
	else if (chAlign == IMAGE_SCN_ALIGN_1024BYTES)
		align = 1024;
	else if (chAlign == IMAGE_SCN_ALIGN_512BYTES)
		align = 512;
	else if (chAlign == IMAGE_SCN_ALIGN_256BYTES)
		align = 256;
	else if (chAlign == IMAGE_SCN_ALIGN_128BYTES)
		align = 128;
	else if (chAlign == IMAGE_SCN_ALIGN_64BYTES)
		align = 64;
	else if (chAlign == IMAGE_SCN_ALIGN_32BYTES)
		align = 32;
	else if (chAlign == IMAGE_SCN_ALIGN_16BYTES)
		align = 16;
	else if (chAlign == IMAGE_SCN_ALIGN_8BYTES)
		align = 8;
	else if (chAlign == IMAGE_SCN_ALIGN_4BYTES)
		align = 4;
	else if (chAlign == IMAGE_SCN_ALIGN_2BYTES)
		align = 2;

	int prevSize = mCurSize;
	mCurSize = (mCurSize + (align - 1)) & ~(align - 1);
	offset = mCurSize;	
	mCurSize += size;
	mAlign = std::max(mAlign, align);
	if (data == NULL)
		return NULL;	
	
	BlChunk blChunk;
	blChunk.mOffset = offset;
	blChunk.mAlignPad = offset - prevSize;
	blChunk.mSize = size;
	blChunk.mData = data;
	blChunk.mObjectDataIdx = -1;	

	mChunks.push_back(blChunk);
	auto blChunkPtr = &mChunks.back();	
	return blChunkPtr;
}

//////////////////////////////////////////////////////////////////////////

void BlPeRelocs::Add(uint32 addr, int fixupType)
{
	int ofs = addr - mBlockAddr;
	if ((mLenPtr == NULL) || (ofs >= 0x1000))
	{
		if ((mLenPtr != NULL) && (*mLenPtr % 4 == 2))
		{
			(*mLenPtr) += 2;
			int16 alignVal = 0;
			mData.Write(&alignVal, 2);
		}

		mBlockAddr = addr;
		mData.Write(&mBlockAddr, 4);
		mLenPtr = (int32*)mData.mWriteCurPtr;
		int32 len = 8 + 2;
		mData.Write(&len, 4);
		int16 val = fixupType << 12;
		mData.Write(&val, 2);
	}
	else
	{
		int16 val = (ofs) | (fixupType << 12);
		mData.Write(&val, 2); 
		(*mLenPtr) += 2;
	}
}

//////////////////////////////////////////////////////////////////////////

static void FixLibName(String& libName)
{
	if ((libName.length() > 4) && (stricmp(libName.c_str() + libName.length() - 4, ".lib") != 0))
		libName += ".lib";	
}

//////////////////////////////////////////////////////////////////////////

BlContext::BlContext()
{
	mNumObjFiles = 0;
	mNumLibs = 0;
	mNumImportedObjs = 0;
	mNumWeakSymbols = 0;

#ifdef BL_PERFTIME
	gPerfManager = new PerfManager();
	gPerfManager->StartRecording();
#endif

	//mOutName = "c:\\temp\\out.exe";
	//mOutName = "c:\\temp\\TestDLL2.dll";
	//mOutName = "c:\\temp\\TestDLL2ABCDEFGHIJKMNOPQRSTUVWXYZ.dll";		
	mCodeView = NULL;
	mSymTable.mContext = this;	
	
	mDebugKind = 0;
	mImageBase = 0x140000000LL;
	mVerbose = false;
	mNoDefaultLib = false;
	mHasFixedBase = false;
	mHasDynamicBase = true;
	mHighEntropyVA = true;
	mIsNXCompat = true;
	mIsTerminalServerAware = true;
	mPeVersionMajor = 0;
	mPeVersionMinor = 0;
	mFailed = false;
	mErrorCount = 0;
	mIsDLL = false;
	mPeSubsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
	mStackReserve = 0x100000;
	mStackCommit = 0x1000;
	mHeapReserve = 0x100000;
	mHeapCommit = 0x1000;		
	mTimestamp = (int)time(0);
	mProcessingSegIdx = 0;
	mIDataSeg = NULL;
	mBSSSeg = NULL;
	mNumFixedSegs = 0;
	mDbgSymSectsFound = 0;
	mDbgSymSectsUsed = 0;
	mTextSeg = NULL;
	mRDataSeg = NULL;
	mDataSeg = NULL;

	mSegments.reserve(MAX_SEG_COUNT);
}

BlContext::~BlContext()
{
	if (mCodeView != NULL)
	{
		mCodeView->StopWorkThreads();
		delete mCodeView;
	}	

#ifdef BL_PERFTIME
	if (gPerfManager->IsRecording())
	{
		gPerfManager->StopRecording(true);
	}
#endif
}

BlSymHash BlContext::Hash(const char* symName)
{
	BlSymHash symHash;
	Hash128(symName, (int)strlen(symName));
	symHash.mName = symName;
	return symHash;
}

String BlContext::ObjectDataIdxToString(int objectDataIdx)
{
	if (objectDataIdx == -1)
		return "<unknown>";
	auto objectData = mObjectDatas[objectDataIdx];

	String name;
	if (objectData->mLib != NULL)
	{
		name = "\"";
		name += GetFileName(objectData->mLib->mFileName);
		name += "\" (";
		name += GetFileName(objectData->mName);
		name += ")";						
	}
	else
	{
		name = "\"";
		name += GetFileName(objectData->mName);
		name += "\"";
	}
	
	return name;
}

String BlContext::GetSymDisp(const StringImpl&name)
{	
	String str;
	if ((name.empty()) || (name[0] != '?'))
	{
		str += "\"";
		str += name;
		str += "\"";
		return str;
	}

	str += "\""; 
	str += BfDemangler::Demangle(name, DbgLanguage_Unknown);
	str += "\" (";
	str += name;
	str += ")";
	return str;
}

String BlContext::FixFilePath(const StringImpl& path, const StringImpl& actualFileName)
{
	String actualPath;
	int lastSlash = BF_MAX((int)path.LastIndexOf('\\'), (int)path.LastIndexOf('/'));
	if (lastSlash != -1)
		actualPath.Append(path.c_str(), lastSlash);
	for (int i = 0; i < (int)actualPath.length(); i++)
		if (actualPath[i] == '/')
			actualPath[i] = '\\';
	actualPath += actualFileName;
	return actualPath;
}

void BlContext::Fail(const StringImpl& error)
{
	AutoCrit autoCrit(mCritSect);
	mErrorCount++;
	if (mErrorCount > 100)
		return;

	std::cerr << "ERROR: " << error.c_str() << std::endl;
	mFailed = true;
}

void BlContext::Warn(const StringImpl& error)
{
	AutoCrit autoCrit(mCritSect);
	mErrorCount++;
	std::cerr << "WARNING: " << error.c_str() << std::endl;
}

void BlContext::Fail(BlObjectData* objectData, const StringImpl& error)
{
	AutoCrit autoCrit(mCritSect);
	Fail(StrFormat("%s in %s", error.c_str(), ObjectDataIdxToString(objectData->mIdx).c_str()));
}

void BlContext::AssertFailed()
{
	BF_ASSERT(mFailed);
}

void BlContext::NotImpl()
{
	BF_FATAL("Not implemented");
}

void BlContext::AddSegment(BlSegment* seg)
{
	seg->mContext = this;
	seg->mSegmentIdx = (int)mSegments.size();

	if (mSegments.size() >= MAX_SEG_COUNT)
	{
		Fail("Maximum segment count exceeded");
		return;
	}

	mSegments.push_back(seg);	
	mOrderedSegments.push_back(seg);
}

void BlContext::LoadFiles()
{
	BL_AUTOPERF("BlContext::LoadFiles");
	
	BfSizedVector<BlObjectDataSectInfo, 16> sectInfos;	
	BfSizedVector<BlObjectDataSymInfo, 256> symInfos;
	
	String tempStr;

	BfSizedVector<int, 16> orderedRelocSects;

	while (true)
	{
		orderedRelocSects.clear();

		if (mObjectDataWorkQueue.empty())
			break;
		auto objectData = mObjectDataWorkQueue.back();		
		mObjectDataWorkQueue.pop_back();

		uint8* data = (uint8*)objectData->mData;
		PEFileHeader* fileHeader = (PEFileHeader*)data;
				
		int nameLen = (int)strlen(objectData->mName);
		if (nameLen > 4)
		{
			const char* ext = objectData->mName + nameLen - 4;
			if (stricmp(ext, ".res") == 0)
			{
				LoadResourceData(objectData);
				continue;
			}
			else if (stricmp(ext, ".def") == 0)
			{
				LoadDefData(objectData);
				continue;
			}
		}
		
		bool dbgTestFile = false;//strcmp(objectData->mName, "Internal.obj") == 0;

		if (fileHeader->mMachine != PE_MACHINE_X64)
		{
			if ((fileHeader->mMachine == 0) && (fileHeader->mNumberOfSections <= 1))
			{
				// This is an object that contains no code - probably just weak externals
			}
			else
			{
				Fail(objectData, "Invalid object file format");				
				continue;
			}
		}

		data += sizeof(PEFileHeader);
		PESectionHeader* sectionHeaderArr = (PESectionHeader*)data;				
		data += fileHeader->mNumberOfSections * sizeof(PESectionHeader);

		BlCvParser cvParser(this);
		
		auto _CommitSection = [&](int objSectionNum, BlObjectDataSectInfo& sectInfo)
		{
			auto sectionHeader = &sectionHeaderArr[objSectionNum];

			if (sectInfo.mSegmentIdx == -1)
			{
				if (sectInfo.mState == BlSectInfoState_QueuedDebugT)
				{
					cvParser.AddTypeData(sectionHeader);
					sectInfo.mState = (BlSectInfoState)0;
				}
				else if (sectInfo.mState == BlSectInfoState_QueuedDebugS)
				{					
					mDbgSymSectsUsed++;
					cvParser.AddSymbolData(sectionHeader);
					sectInfo.mState = (BlSectInfoState)0;
				}
				else
					BF_FATAL("Error");
				return;
			}
			
			auto section = mSegments[sectInfo.mSegmentIdx];			
			
			uint8* sectData = NULL;
			if (sectionHeader->mPointerToRawData != NULL)
				sectData = (uint8*)objectData->mData + sectionHeader->mPointerToRawData;
			BlChunk* blChunk = section->AddChunk(sectData, sectionHeader->mSizeOfRawData, sectionHeader->mCharacteristics, sectInfo.mSegmentOffset);
			if (blChunk != NULL)
			{
				sectInfo.mChunkData = blChunk->mData;
				blChunk->mObjectDataIdx = objectData->mIdx;
			}
			orderedRelocSects.push_back(objSectionNum);

			if ((sectionHeader->mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
			{
				if (mCodeView != NULL)
					cvParser.AddContribution(sectInfo.mSegmentIdx, sectInfo.mSegmentOffset, sectionHeader->mSizeOfRawData, sectionHeader->mCharacteristics);
			}			
		};

		BlSegmentInfo sectionInfo;

		char* strTab = (char*)objectData->mData + fileHeader->mPointerToSymbolTable + fileHeader->mNumberOfSymbols * sizeof(PE_SymInfo);		
		if (mCodeView != NULL)
			cvParser.AddModule(objectData, strTab);

		sectInfos.clear();
		for (int sectIdx = 0; sectIdx < fileHeader->mNumberOfSections; sectIdx++)
		{
			BlSegment* section = NULL;

			BlObjectDataSectInfo sectInfo = { -1 };

			auto sectionHeader = &sectionHeaderArr[sectIdx];
			bool isComdat = (sectionHeader->mCharacteristics & IMAGE_SCN_LNK_COMDAT) != 0;

			if (sectionHeader->mPointerToRawData != 0)
				data = (uint8*)objectData->mData + sectionHeader->mPointerToRawData;			

			char* sectionName = sectionHeader->mName;
			if (sectionName[0] == '/')
			{
				int idx = atoi(sectionName + 1);
				sectionName = &strTab[idx];
			}
			
			if (strncmp(sectionName, ".xdata", 6) == 0)
			{
				
			}

			if (strncmp(sectionName, ".drectve", 5) == 0)
			{				
				char* cmdStart = NULL;
				
				auto _HandleParam = [&] (const StringImpl& cmd, const StringImpl& arg)
				{
					if (cmd == "/DEFAULTLIB")
					{
						String libName = arg;
						int lastDot = (int)libName.LastIndexOf('.');
						if (lastDot == -1)
							libName += ".lib";
						AddFile(libName, BlObjectDataKind_DirectiveLib);
					}
					else if (cmd == "/DISALLOWLIB")
					{
						String libName = arg;						
						FixLibName(libName);
						String libCanonName = FixPathAndCase(libName);
						// Make sure we haven't already used this library, if so then throw an error, otherwise
						// just add it to the seen-list so we don't add it later
						auto itr = mSeenLibs.find(libCanonName);
						if (itr != mSeenLibs.end())
						{
							Fail(StrFormat("Disallowed lib '%s', disallowed in %s, has been used", arg.c_str(), ObjectDataIdxToString(objectData->mIdx).c_str()));
						}
						auto insertPair = mNoDefaultLibs.insert(libCanonName);
					}
					else if (cmd == "/MERGE")
					{
						int equalIdx = (int)arg.IndexOf('=');
						if (equalIdx == -1)
							return;
						String fromName = arg.Substring(0, equalIdx);
						String toName = arg.Substring(equalIdx + 1);
						mSectionMerges[fromName] = toName;
					}
					else if (cmd == "/ALTERNATENAME")
					{
						int equalIdx = (int)arg.IndexOf('=');
						if (equalIdx == -1)
							return;
						String fromName = arg.Substring(0, equalIdx);
						String toName = arg.Substring(equalIdx + 1);
						mSectionMerges[fromName] = toName;
						
						auto blSymbol = mSymTable.Add(fromName.c_str());
						if (blSymbol->mKind == BlSymKind_Undefined)
						{
							int remapIdx = (int)mRemapStrs.size();
							mRemapStrs.push_back(toName);
							
							blSymbol->mKind = BlSymKind_WeakSymbol;
							blSymbol->mParam = remapIdx;
							blSymbol->mObjectDataIdx = objectData->mIdx;
						}
					}
					else if (cmd == "/EXPORT")
					{
						const StringImpl& exportName = arg;
						auto pair = mExports.insert(std::make_pair(exportName, BlExport()));
						if (!pair.second)
						{
							Warn(StrFormat("Export '%s' specified multiple times; using first specification", exportName.c_str()));
						}
					}
					else if (cmd == "/FAILIFMISMATCH")
					{

					}
					else if (cmd == "/EDITANDCONTINUE")
					{

					}
					else if (cmd == "/THROWINGNEW")
					{

					}
					else if (cmd == "/GUARDSYM")
					{

					}
					else if (cmd == "/INCLUDE")
					{
						mForcedSyms.push_back(arg);
					}
					else
					{
						Fail(StrFormat("Invalid link option \"%s\" in %s", cmd.c_str(), ObjectDataIdxToString(objectData->mIdx).c_str()));
					}
				};

/*#ifdef _DEBUG
				String dbgOut = "Directives for ";
				dbgOut += ObjectDataIdxToString(objectData->mIdx);
				dbgOut += ": ";
				dbgOut.insert(dbgOut.end(), (char*)data, (char*)data + sectionHeader->mSizeOfRawData);
				dbgOut += "\n";
				OutputDebugStrF(dbgOut.c_str());
#endif*/
				
				StringT<256> curCmd;
				StringT<256> curArg;
				bool inArg = false;				
				bool inQuote = false;
				bool prevSlash = false;
				for (char* cPtr = (char*)data; cPtr < (char*)data + sectionHeader->mSizeOfRawData; cPtr++)
				{
					char c = *cPtr;					
					if (c == 0)
						break;
					if (c == '"')
					{
						inQuote = !inQuote;
					}
					else if ((c == ' ') && (!inQuote))
					{
						if (!curCmd.empty())
						{
							_HandleParam(curCmd, curArg);
							curCmd.clear();
							curArg.clear();
							inArg = false;
						}
					}
					else if ((c == ':') && (!inQuote))
					{
						inArg = true;
					}
					else if (inArg)
						curArg += c;
					else
						curCmd += toupper(c);
				}
				if (!curCmd.empty())
					_HandleParam(curCmd, curArg);
			}
			else if (strcmp(sectionName, ".debug$T") == 0)
			{
				if (mCodeView != NULL)
				{
					sectInfo.mState = BlSectInfoState_QueuedDebugT;
					if (!isComdat)
						_CommitSection(sectIdx, sectInfo);
				}				
			}
			else if (strcmp(sectionName, ".debug$S") == 0)
			{
				mDbgSymSectsFound++;
				if (mCodeView != NULL)
				{
					sectInfo.mState = BlSectInfoState_QueuedDebugS;
					if (!isComdat)
						_CommitSection(sectIdx, sectInfo);
				}					
			}						
			else
			{
				if (strcmp(sectionName, ".bss") == 0)
				{					
					sectionName = (char*)mBSSSeg->mName.c_str();
				}

				auto _CompareSectionInfo = [] (BlSegmentInfo* lhs, BlSegmentInfo* rhs)
				{
					return lhs->mName < rhs->mName;
				};
				
				sectionInfo.mName = sectionName;
				auto itr = std::lower_bound(mOrderedSegments.begin(), mOrderedSegments.end(), &sectionInfo, _CompareSectionInfo);
				if (itr != mOrderedSegments.end())
				{
					section = *itr;
					if (section->mName != sectionName)
						section = NULL;
				}
				if (section == NULL)
				{
					section = mDynSegments.Alloc();
					section->mName = sectionName;
					section->mContext = this;
					section->mSegmentIdx = (int)mSegments.size();				
					mSegments.push_back(section);
					mOrderedSegments.push_back(section);

					std::sort(mOrderedSegments.begin(), mOrderedSegments.end(), _CompareSectionInfo);
				}
			}

			

			/*else if (strncmp(sectionHeader->mName, ".text", 5) == 0)
			{
				section = &mTextSect;
			}
			else if (strncmp(sectionHeader->mName, ".rdata", 6) == 0)
			{
				section = &mRDataSect;
			}
			else if (strncmp(sectionHeader->mName, ".bss", 4) == 0)
			{
				section = &mBSSSect;
			}*/



			if (section != NULL)
			{
				// Add the alignment flags later
				section->mCharacteristics |= (sectionHeader->mCharacteristics & ~IMAGE_SCN_ALIGN_MASK);
				sectInfo.mSegmentIdx = section->mSegmentIdx;

				if (isComdat)
				{
					sectInfo.mState = BlSectInfoState_QueuedData;
				}
				else
				{
					/*uint8* sectData = NULL;
					if (sectionHeader->mPointerToRawData != NULL)
						sectData = (uint8*)objectData->mData + sectionHeader->mPointerToRawData;
					BlChunk* blChunk = section->AddChunk(sectData, sectionHeader->mSizeOfRawData, sectionHeader->mCharacteristics, sectInfo.mSectionOffset);
					if (blChunk != NULL)
						sectInfo.mChunkData = blChunk->mData;
					orderedRelocSects.push_back(sectIdx);*/
					_CommitSection(sectIdx, sectInfo);
				}				
			}

			data += sectionHeader->mSizeOfRawData;			
			 
			if (sectionHeader->mPointerToRawData != 0)			
				sectInfo.mRelocData = (uint8*)objectData->mData + sectionHeader->mPointerToRelocations;			

			sectInfos.push_back(sectInfo);
		}

		data = (uint8*)objectData->mData + fileHeader->mPointerToSymbolTable;		

		PE_SymInfo* symInfoArr = (PE_SymInfo*)data;		

		//////// Read symbols
		// This is the primary symbol creation loop
		{
			BL_AUTOPERF("BlContext::LoadFiles ReadSymbols");

			symInfos.clear();
			symInfos.resize(fileHeader->mNumberOfSymbols);
			for (int symIdx = 0; symIdx < (int)fileHeader->mNumberOfSymbols; symIdx++)
			{				
				auto& symInfo = symInfos[symIdx];

				PE_SymInfo* objSym = (PE_SymInfo*)data;
				data += sizeof(PE_SymInfo);

				if ((objSym->mNumOfAuxSymbols != 0) && (objSym->mStorageClass == IMAGE_SYM_CLASS_STATIC))
				{
					BF_ASSERT(objSym->mSectionNum >= 1);
					auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
					PE_SymInfoAux* symAuxInfo = (PE_SymInfoAux*)data;
					sectInfo.mSelectNum = symAuxInfo->mNumber;
					sectInfo.mSelection = symAuxInfo->mSelection;					
					symInfo.mKind = BlObjectDataSymInfo::Kind_Section;
					symInfo.mSectionNum = objSym->mSectionNum;
					symIdx += objSym->mNumOfAuxSymbols;
					data += sizeof(PE_SymInfo) * objSym->mNumOfAuxSymbols;
					continue;
				}

				if (objSym->mSectionNum == 0xFFFF)
				{
					symInfo.mSegmentIdx = -1;
					symInfo.mSegmentOffset = objSym->mValue;
					// Do anything with this? @comp.id, @feat.00
					// Rich Signature stuff.
				}
				else if (objSym->mSectionNum != 0)
				{
					auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
					symInfo.mSegmentIdx = sectInfo.mSegmentIdx;
					int sectOffset = sectInfo.mSegmentOffset;
					symInfo.mSegmentOffset = sectInfo.mSegmentOffset + objSym->mValue;
				}

				if (objSym->mStorageClass == IMAGE_SYM_CLASS_LABEL)
				{
					auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
					if (sectInfo.mChunkData == (void*)-1)
					{
						// Don't let labels cause COMDAT commits
						continue;
					}
				}

				if ((objSym->mStorageClass != IMAGE_SYM_CLASS_EXTERNAL) &&
					(objSym->mStorageClass != IMAGE_SYM_CLASS_EXTERNAL_DEF) &&
					(objSym->mStorageClass != IMAGE_SYM_CLASS_WEAK_EXTERNAL))
				{
					if (objSym->mSectionNum != 0xFFFF)
					{
						auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
						if (sectInfo.mChunkData == (void*)-1)
						{
							_CommitSection(objSym->mSectionNum - 1, sectInfo);
							symInfo.mSegmentOffset += sectInfo.mSegmentOffset;
						}
					}
					BF_ASSERT(objSym->mSectionNum != 0);
					continue;
				}

				char* symName;
				if (objSym->mName[0] == 0)
				{
					symName = strTab + objSym->mNameOfs[1];
				}
				else
				{
					symName = objSym->mName;
					if (symName[7] != 0) // Do we have to zero-terminate ourselves?
					{
						symName = (char*)mAlloc.AllocBytes(9);
						memcpy(symName, objSym->mName, 8);
					}
				}

				if ((objSym->mSectionNum == 0) && (objSym->mStorageClass != IMAGE_SYM_CLASS_WEAK_EXTERNAL))
				{
					if (objSym->mValue > 0)
					{
						// Zero-initialized value, where mValue is the size.  I've only seen this occur in C
						//  files with zero-initialized local static variables										
						auto blSymbol = mSymTable.Add(symName);
						if (blSymbol->mObjectDataIdx != -1)
							continue;

						int align = std::min(objSym->mValue, 16);													
						mBSSSeg->mAlign = std::max(mBSSSeg->mAlign, align);
						mBSSSeg->mCurSize = BF_ALIGN(mBSSSeg->mCurSize, align);
						symInfo.mSegmentIdx = mBSSSeg->mSegmentIdx;
						symInfo.mSegmentOffset = mBSSSeg->mCurSize;
						mBSSSeg->mCurSize += objSym->mValue;

						blSymbol->mObjectDataIdx = objectData->mIdx;
						blSymbol->mSegmentIdx = symInfo.mSegmentIdx;
						blSymbol->mSegmentOffset = symInfo.mSegmentOffset;

						continue;
					}
					else
					{
						// External reference					
						symInfo.mSym = mSymTable.Add(symName);
						if (symInfo.mSym->mKind >= 0)
						{
							symInfo.mSegmentIdx = symInfo.mSym->mSegmentIdx;
							symInfo.mSegmentOffset = symInfo.mSym->mSegmentOffset;
						}
						else
						{
							symInfo.mSegmentIdx = -1;
						}
						continue;
					}
				}

				// Export
				auto blSymbol = mSymTable.Add(symName);
				if (blSymbol->mObjectDataIdx != -1)
				{
					bool usePrevSymbol = false;
					if (objSym->mStorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL)
					{
						symIdx += objSym->mNumOfAuxSymbols;
						data += sizeof(PE_SymInfo) * objSym->mNumOfAuxSymbols;
						usePrevSymbol = true; // Everything is higher priority than a weak external
					}
					else
					{
						BlObjectData* prevObjectData = mObjectDatas[blSymbol->mObjectDataIdx];
						if (blSymbol->mKind == BlSymKind_WeakSymbol)
							usePrevSymbol = false; // Always prefer a non-weak symbol
						else if (prevObjectData->mKind > objectData->mKind)
							usePrevSymbol = true; // Previous has higher priority, leave it
						else if ((mBSSSeg != NULL) && (blSymbol->mSegmentIdx == mBSSSeg->mSegmentIdx))
						{
							// This came from an implicit "sized UNDEF" symbol, so allow it to be overriden with 
							//  actual data
							usePrevSymbol = false;
						}
						else if (prevObjectData->mKind == objectData->mKind)
						{
							auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
							if (sectInfo.mSelection == IMAGE_COMDAT_SELECT_ANY)
							{
								usePrevSymbol = true;
							}
							else if (sectInfo.mSelection == IMAGE_COMDAT_SELECT_LARGEST)
							{
								//TODO: Find actual chunk
								usePrevSymbol = true;
							}
							else if (sectInfo.mSelection > 2)
							{
								Fail(StrFormat("Unsupported COMDAT selection '%d' found on symbol '%s' in %s", sectInfo.mSelection, GetSymDisp(symName).c_str(), ObjectDataIdxToString(objectData->mIdx).c_str()));
							}
							else
							{
								// Ambiguous priority, throw error
								Fail(StrFormat("Duplicate symbol %s found in %s, first defined in %s", GetSymDisp(symName).c_str(), ObjectDataIdxToString(objectData->mIdx).c_str(),
									ObjectDataIdxToString(blSymbol->mObjectDataIdx).c_str()));
							}
						}
					}
					if (usePrevSymbol)
					{
						if (blSymbol->mSegmentIdx >= 0)
						{
							symInfo.mSegmentIdx = blSymbol->mSegmentIdx;
							symInfo.mSegmentOffset = blSymbol->mSegmentOffset;
						}
						else
						{
							symInfo.mSym = blSymbol;
						}
						continue;
					}
				}

				if (objSym->mStorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL)
				{
					struct _WeakExtern
					{
						int32 mTagIndex;
						int32 mCharacteristics;
					};

					_WeakExtern* weakExtern = (_WeakExtern*)data;
					PE_SymInfo* refSymInfo = &symInfoArr[weakExtern->mTagIndex];

					char* refSymName;
					if (refSymInfo->mName[0] == 0)
					{
						refSymName = strTab + refSymInfo->mNameOfs[1];
					}
					else
					{
						refSymName = refSymInfo->mName;
						if (refSymName[7] != 0) // Do we have to zero-terminate ourselves?
						{
							refSymName = (char*)mAlloc.AllocBytes(9);
							memcpy(refSymName, refSymInfo->mName, 8);
						}
					}

					symInfo.mSym = blSymbol;

					int remapIdx = (int)mRemapStrs.size();
					mRemapStrs.push_back(refSymName);
					mNumWeakSymbols++;

					mForcedSyms.push_back(refSymName);

					blSymbol->mKind = BlSymKind_WeakSymbol;
					blSymbol->mParam = remapIdx;
					blSymbol->mObjectDataIdx = objectData->mIdx;

					symIdx += objSym->mNumOfAuxSymbols;
					data += sizeof(PE_SymInfo) * objSym->mNumOfAuxSymbols;
					continue;
				}

				auto& sectInfo = sectInfos[objSym->mSectionNum - 1];
				if (sectInfo.mChunkData == (void*)-1)
				{
					_CommitSection(objSym->mSectionNum - 1, sectInfo);
					symInfo.mSegmentOffset += sectInfo.mSegmentOffset;
				}

				blSymbol->mObjectDataIdx = objectData->mIdx;
				blSymbol->mSegmentIdx = symInfo.mSegmentIdx;
				blSymbol->mSegmentOffset = symInfo.mSegmentOffset;
			}
		}

		// Any loaded COMDAT section with section associativities need to load those associated sections now
		//  This most often occurs with a debug$S entry for a COMDAT function, such as a template method.
		//  We only want to include the static debug info if the function is chosen.
		while (true)
		{
			// We may need to iterate multiple times, if there are chained dependencies and a new commited
			//  depdency occurs after a pending dependent entry
			bool commitedNew = false;

			for (int sectIdx = 0; sectIdx < fileHeader->mNumberOfSections; sectIdx++)
			{
				auto sectionHeader = &sectionHeaderArr[sectIdx];
				bool isComdat = (sectionHeader->mCharacteristics & IMAGE_SCN_LNK_COMDAT) != 0;

				if (!isComdat)
					continue;

				auto& sectInfo = sectInfos[sectIdx];
				if (sectInfo.IsUsed())
					continue;

				if (sectInfo.mSelection == IMAGE_COMDAT_SELECT_ASSOCIATIVE)
				{
					auto checkSelectInfo = sectInfos[sectInfo.mSelectNum - 1];
					if (checkSelectInfo.IsUsed())
					{
						_CommitSection(sectIdx, sectInfo);
						commitedNew = true;						
					}
				}
			}

			if (!commitedNew)
				break;
		}
		
		// Queue relocs		
		{
			BL_AUTOPERF("BlContext::LoadFiles Relocs");

			for (int ordIdx = 0; ordIdx < (int)orderedRelocSects.size(); ordIdx++)
			{
				int sectIdx = orderedRelocSects[ordIdx];

				auto sectionHeader = &sectionHeaderArr[sectIdx];
				auto& sectInfo = sectInfos[sectIdx];
				if (sectInfo.mSegmentIdx == -1)
					continue;
				if (sectInfo.mChunkData == (void*)-1)
				{
					// Unused COMDAT, or is data				
					continue;
				}

				auto seg = mSegments[sectInfo.mSegmentIdx];
				data = (uint8*)sectInfo.mRelocData;

				bool isDebugSymSect = seg->mName == ".debug$S";
				bool hadDeferredSyms = false;

				int lastRelocOffset = 0;
				if (!seg->mRelocs.empty())
					lastRelocOffset = seg->mRelocs.back().mOutOffset;

				for (int relocIdx = 0; relocIdx < sectionHeader->mNumberOfRelocations; relocIdx++)
				{
					COFFRelocation* reloc = (COFFRelocation*)data;
					data += sizeof(COFFRelocation);

					auto& sym = symInfos[reloc->mSymbolTableIndex];
					int outOffset = sectInfo.mSegmentOffset + (int)reloc->mVirtualAddress;

					if (sym.mSegmentIdx == -1)
					{
						//NotImpl();
						/*auto symInfo = &symInfoArr[reloc->mSymbolTableIndex];
						auto targetSectInfo = sectInfos[symInfo->mSectionNum - 1];
						sym.mSectionIdx = targetSectInfo.mSectionIdx;
						if (targetSectInfo.mChunkData == (void*)-1)
						{
							_CommitComdat(symInfo->mSectionNum - 1, targetSectInfo);
							sym.mSectionOffset += targetSectInfo.mSectionOffset;
						}*/
					}

					if (outOffset < lastRelocOffset)
					{
						NotImpl();
					}
					
					BlReloc blReloc;
					blReloc.mOutOffset = outOffset;
					if (sym.mSym != NULL)
					{
						/*if ((isDebugSymSect) && (sym.mSym->mSegmentIdx >= 0) && (sym.mSym->mSegmentIdx < mNumFixedSegs))
						{
							blReloc.mSegmentIdx = sym.mSym->mSegmentIdx;
							blReloc.mSegmentOffset = sym.mSym->mSegmentOffset;
							blReloc.mFlags = BeRelocFlags_Loc;
						}
						else*/
						{
							hadDeferredSyms = true;
							blReloc.mSym = sym.mSym;
							blReloc.mFlags = (BeRelocFlags)(BeRelocFlags_Sym | BeRelocFlags_SymIsNew);
						}
						
					}
					else
					{
						BF_ASSERT(sym.mSegmentIdx != -1);

						if (sym.mKind == BlObjectDataSymInfo::Kind_Section)
						{
							auto& sectInfo = sectInfos[sym.mSectionNum - 1];
							blReloc.mSegmentIdx = sectInfo.mSegmentIdx;
							blReloc.mSegmentOffset = sectInfo.mSegmentOffset;
						}
						else
						{
							blReloc.mSegmentIdx = sym.mSegmentIdx;
							blReloc.mSegmentOffset = sym.mSegmentOffset;
						}
						blReloc.mFlags = BeRelocFlags_Loc;
					}

					/*if (sym.mIsFunc)
						blReloc.mFlags = (BeRelocFlags)(blReloc.mFlags | BeRelocFlags_Func);*/

					switch (reloc->mType)
					{
					case IMAGE_REL_AMD64_ADDR32NB:
						blReloc.mKind = BlRelocKind_ADDR32NB;
						//blReloc.mParam = *(int32*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_ADDR64:
						//TODO: 
						blReloc.mKind = BlRelocKind_ADDR64;
						//blReloc.mParam = *(int64*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_REL32:
						blReloc.mKind = BlRelocKind_REL32;
						//blReloc.mParam = *(int32*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_REL32_1:
						blReloc.mKind = BlRelocKind_REL32_1;
						//blReloc.mParam = *(int32*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_REL32_4:
						blReloc.mKind = BlRelocKind_REL32_4;
						//blReloc.mParam = *(int32*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_SECREL:
						blReloc.mKind = BlRelocKind_SECREL;
						//blReloc.mParam = *(int32*)((uint8*)sectInfo.mChunkData + reloc->mVirtualAddress);
						break;
					case IMAGE_REL_AMD64_SECTION:
						NotImpl();
						break;
					default:
						NotImpl();
						break;
					}

					lastRelocOffset = outOffset;
					seg->mRelocs.push_back(blReloc);
				}				
			}			
		}

		if (mCodeView != NULL)
		{
			BL_AUTOPERF("BlContext::LoadFiles Cv FinishModule");
			cvParser.FinishModule(sectionHeaderArr, sectInfos, symInfoArr, symInfos);
		}

		//TODO: Make so we can unmap memory-mapped data.  Currently we can't do that because of string pointers in symbols
	}
}

bool BlContext::HandleLibSymbolRef(BlSymbol* sym, int objectDataIds)
{
	const char* name = sym->mName;

	BlObjectData* objectData = mObjectDatas[objectDataIds];

	if ((objectData->mLoadState == BlObjectDataLoadState_UnloadedLib) || (objectData->mLoadState == BlObjectDataLoadState_UnloadedThinLib))
	{
		mNumImportedObjs++;

		bool isThinLib = objectData->mLoadState == BlObjectDataLoadState_UnloadedThinLib;

		BlLibMemberHeader* header = (BlLibMemberHeader*)objectData->mData;
		if ((header->mName[0] == '/') || (header->mName[0] == '\\'))
		{
			objectData->mName = objectData->mLib->mStrTable + atoi(header->mName + 1);
			for (char* cPtr = objectData->mName; true; cPtr++)
			{
				// Thin string table entries end with "/\n" for some reason.
				char c = *cPtr;
				if (c == 0)
					break;
				if (c == '\n')
				{
					char* endC = cPtr;
					if (cPtr[-1] == '/')
						endC = cPtr - 1;
					char* newStr = (char*)mAlloc.AllocBytes((int)(endC - objectData->mName + 1));
					memcpy(newStr, objectData->mName, (int)(endC - objectData->mName));
					newStr[endC - objectData->mName] = 0;
					objectData->mName = newStr;
					break;
				}
			}
		}
		else
		{
			char* slashPos = strchr(header->mName, '/');
			BF_ASSERT(slashPos != NULL);
			int len = (int)(slashPos - header->mName);
			objectData->mName = (char*)mAlloc.AllocBytes(len + 1);
			memcpy(objectData->mName, header->mName, len);
		}

		if (isThinLib)
		{
			auto mappedFile = mMappedFiles.Alloc();

			String objPath = GetAbsPath(objectData->mName, GetFileDir(objectData->mLib->mFileName));
			if (!mappedFile->Open(objPath))
			{
				Fail(StrFormat("Failed to open thin archive member \"%s\" from \"%s\"", objectData->mName, objectData->mLib->mFileName.c_str()));
			}
			else
			{
				objectData->mData = mappedFile->mData;
				objectData->mSize = mappedFile->mFileSize;
				BF_ASSERT(objectData->mData != NULL);
				mObjectDataWorkQueue.push_back(objectData);
			}
		}
		else
		{
			objectData->mSize = atoi(header->mSize);
			objectData->mData = (uint8*)objectData->mData + sizeof(BlLibMemberHeader);

			PEImportObjectHeader* importObjectHeader = (PEImportObjectHeader*)objectData->mData;
			if ((importObjectHeader->mSig1 == 0) && (importObjectHeader->mSig2 == IMPORT_OBJECT_HDR_SIG2))
			{
				uint8* data = (uint8*)objectData->mData;
				data += sizeof(PEImportObjectHeader);
				char* symName = (char*)data;
				char* dllName = symName + strlen(symName) + 1;

				String dllFixedName = FixPathAndCase(dllName);
				BlImportFile* blImportFile;
				auto itr = mImportFiles.find(dllFixedName);
				if (itr != mImportFiles.end())
				{
					blImportFile = &itr->second;
				}
				else
				{
					blImportFile = &(mImportFiles.insert(std::make_pair(dllFixedName, BlImportFile())).first->second);
				}

				//OutputDebugStrF("Imported: %s:%s\n", dllName, symName);

				int objType = (importObjectHeader->mType) & 3;
				int nameType = importObjectHeader->mType >> 2;

				auto blLookup = blImportFile->mLookups.Alloc();
				if (nameType != IMPORT_OBJECT_ORDINAL)
					blLookup->mName = symName;
				blLookup->mHint = importObjectHeader->mHint;

				if (strncmp(name, "__imp_", 6) == 0)
					sym->mKind = BlSymKind_ImportImp;
				else
					sym->mKind = BlSymKind_Import;
				sym->mParam = (int)mImportLookups.size();
				mImportLookups.push_back(blLookup);

				objectData->mLoadState = BlObjectDataLoadState_ImportObject;
				objectData->mImportSym = sym;
			}
			else
			{
				BF_ASSERT(objectData->mData != NULL);
				mObjectDataWorkQueue.push_back(objectData);
			}
		}
		return true;
	}
	else if (objectData->mLoadState == BlObjectDataLoadState_ImportObject)
	{
		// We have both a "_imp_<X>" and "<X>" import name, so link those together
		BF_ASSERT((objectData->mImportSym->mKind == BlSymKind_Import) || (objectData->mImportSym->mKind == BlSymKind_ImportImp));
		if (strncmp(name, "__imp_", 6) == 0)
			sym->mKind = BlSymKind_ImportImp;
		else
			sym->mKind = BlSymKind_Import;
		sym->mParam = objectData->mImportSym->mParam;
		return true;
	}
	else
	{		
		return false;
	}
}

BlSymbol* BlContext::ProcessSymbol(BlSymbol* sym)
{
	// Try to load symbol from a lib if we need to
	bool madeProgress = false;

	while (sym->mKind == BlSymKind_WeakSymbol)
	{
		sym = mSymTable.Add(mRemapStrs[sym->mParam].c_str());
		madeProgress = true;
	}

	if (sym->mKind == BlSymKind_LibSymbolRef)
	{
		madeProgress |= HandleLibSymbolRef(sym, sym->mParam);
	}
	else if (sym->mKind == BlSymKind_LibSymbolGroupRef)
	{
		// This symbol is reported in multiple libraries
		for (auto objectDataIdx : mImportGroups[sym->mParam]->mObjectDatas)
		{
			madeProgress |= HandleLibSymbolRef(sym, objectDataIdx);
			if (madeProgress)
			{				
				// We only want to load ONE new object file, otherwise we could end up with multiple definitions
				break;
			}
		}
	}	
	else
		madeProgress = true;

	if ((!madeProgress) && (mObjectDataWorkQueue.empty()))
	{
		// If the mObjectDataWorkQueue is empty, that means we already tried to load a lib's object file
		// that was supposed to define this symbol, but it didn't.  This shouldn't happen in a well-formed lib
		// unless we have a bug.
		BF_ASSERT("Library error?" == 0);
		sym->mKind = BlSymKind_Undefined;
	}
	return sym;
}

BlSymbol* BlContext::ResolveSym(const char* name, bool throwError)
{	
	auto sym = mSymTable.Add(name);
	if ((throwError) && (sym->mKind == BlSymKind_Undefined))
	{
		BlSymNotFound symNotFound;
		symNotFound.mSym = sym;
		symNotFound.mOutOffset = -1;
		symNotFound.mSegmentIdx = -1;
		mSymNotFounds.push_back(symNotFound);
		return sym;
	}

	sym = ProcessSymbol(sym);

	return sym;
}

uint64 BlContext::GetSymAddr(BlSymbol* sym)
{
	if (sym->mSegmentIdx < 0)
	{
		if (sym->mKind == BlSymKind_Absolute)
			return sym->mSegmentOffset;
		return 0;
	}

	auto segment = mSegments[sym->mSegmentIdx];	
	return segment->mRVA + sym->mSegmentOffset;
}

void BlContext::AddAbsoluteSym(const char * name, int val)
{	
	auto sym = mSymTable.Add(name);	
	sym->mObjectDataIdx = -1;
	sym->mKind = BlSymKind_Absolute;
	sym->mParam = val;
}

void BlContext::PlaceSections()
{
	std::multimap<String, BlSegment*> groupedSections;

	mSectionMerges[".text"] = "!0.text";
	mSectionMerges[".rdata"] = "!1.rdata";
	mSectionMerges[".data"] = "!2.data";
	mSectionMerges[".bss"] = "!3.bss";
	mSectionMerges[".pdata"] = "!4.pdata";
	mSectionMerges[".idata"] = "!5.idata";
	mSectionMerges[".gfids"] = "!6.gfids";
	mSectionMerges[".00cfg"] = "!7.00cfg";
	mSectionMerges[".rsrc"] = "!9.rsrc";
	mSectionMerges[".reloc"] = "!9.reloc";
	mSectionMerges[".tls"] = "!A.tls";
	mSectionMerges[".xdata"] = "!B.xdata";

	for (auto sect : mOrderedSegments)
	{
		auto sectName = sect->mName;
		int dollarPos = (int)sectName.IndexOf('$');
		if (dollarPos != -1)
			sectName.RemoveToEnd(dollarPos);

		while (true)
		{
			auto mergeItr = mSectionMerges.find(sectName);
			if (mergeItr == mSectionMerges.end())
				break;
			sectName = mergeItr->second;
		}

		groupedSections.insert(std::make_pair(sectName, sect));
	}
	
	BlOutSection* outSection = NULL;
	int outSectionIdx = -1;
	for (auto pair : groupedSections)
	{
		auto baseName = pair.first;
		auto sect = pair.second;
		if (baseName[0] == '!')
			baseName.Remove(0, 2);

		if ((outSection == NULL) || (outSection->mName != baseName))
		{
			outSection = mOutSections.Alloc();
			outSection->mName = baseName;			
			outSectionIdx++;
			outSection->mIdx = outSectionIdx;
		}

		BF_ASSERT((sect->mOutSectionIdx == -1) || (sect->mOutSectionIdx == outSectionIdx));
		sect->mOutSectionIdx = outSectionIdx;
		outSection->mSegments.push_back(sect);
		outSection->mCharacteristics |= sect->mCharacteristics;
		//outSection->mRawSize = (outSection->mRawSize + (sect->mAlign - 1)) & ~(sect->mAlign - 1);
		outSection->mAlign = std::max(outSection->mAlign, sect->mAlign);
		//outSection->mRawSize += sect->mCurSize;
	}
	
	int32 curRVA = 0x1000;
	int rawDataPos = (int)sizeof(PEHeader);
	rawDataPos += 0xB0; // DOS program
	rawDataPos += (int)sizeof(PE_NTHeaders64);
	rawDataPos += (int)mOutSections.size() * sizeof(PESectionHeader);
	rawDataPos = (rawDataPos + (512 - 1)) & ~(512 - 1);
	
	for (auto outSection : mOutSections)
	{		
		bool isUninitialized = false;
		outSection->mRawDataPos = rawDataPos;
		outSection->mRVA = curRVA;
		for (auto sect : outSection->mSegments)
		{			
			if ((!isUninitialized) && ((sect->mCharacteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0))
			{
				// This is for when transition from .data to .bss
				int endRVA = (curRVA + (512 - 1)) & ~(512 - 1);
				outSection->mRawSize = endRVA - outSection->mRVA;
				isUninitialized = true;
			}
			curRVA = (curRVA + (sect->mAlign - 1)) & ~(sect->mAlign - 1);
			sect->mRVA = curRVA;
			curRVA += sect->mCurSize;
		}
		// Virtual size is NOT size-aligned
		outSection->mVirtualSize = curRVA - outSection->mRVA;
		if (!isUninitialized)
		{
			curRVA = (curRVA + (512 - 1)) & ~(512 - 1);
			outSection->mRawSize = curRVA - outSection->mRVA;
		}
		curRVA = (curRVA + (4096 - 1)) & ~(4096 - 1);		
		rawDataPos += outSection->mRawSize;

		int characteristics = outSection->mCharacteristics;
		if (outSection->mAlign == 8192) characteristics |= IMAGE_SCN_ALIGN_8192BYTES;
		else if (outSection->mAlign == 4096) characteristics |= IMAGE_SCN_ALIGN_4096BYTES;
		else if (outSection->mAlign == 2048) characteristics |= IMAGE_SCN_ALIGN_2048BYTES;
		else if (outSection->mAlign == 1024) characteristics |= IMAGE_SCN_ALIGN_1024BYTES;
		else if (outSection->mAlign == 512) characteristics |= IMAGE_SCN_ALIGN_512BYTES;
		else if (outSection->mAlign == 256) characteristics |= IMAGE_SCN_ALIGN_256BYTES;
		else if (outSection->mAlign == 128) characteristics |= IMAGE_SCN_ALIGN_128BYTES;
		else if (outSection->mAlign == 64) characteristics |= IMAGE_SCN_ALIGN_64BYTES;
		else if (outSection->mAlign == 32) characteristics |= IMAGE_SCN_ALIGN_32BYTES;
		else if (outSection->mAlign == 16) characteristics |= IMAGE_SCN_ALIGN_16BYTES;
		else if (outSection->mAlign == 8) characteristics |= IMAGE_SCN_ALIGN_8BYTES;
		else if (outSection->mAlign == 4) characteristics |= IMAGE_SCN_ALIGN_4BYTES;
		else if (outSection->mAlign == 2) characteristics |= IMAGE_SCN_ALIGN_2BYTES;
		outSection->mCharacteristics = characteristics;
	}	
}

void BlContext::ResolveRelocs()
{
	BL_AUTOPERF("BlContext::ResolveRelocs");

	int startProcessingSegIdx = mProcessingSegIdx;

	while (true)
	{		
		auto seg = mSegments[mProcessingSegIdx];
		//for (auto& reloc : seg->mRelocs)

		while (seg->mResolvedRelocIdx < (int)seg->mRelocs.size())
		{
			auto& reloc = seg->mRelocs[seg->mResolvedRelocIdx];
			if ((reloc.mFlags & BeRelocFlags_Sym) != 0)
			{
				
			}

			if ((reloc.mFlags & (BeRelocFlags_SymIsNew | BeRelocFlags_Sym)) == (BeRelocFlags_SymIsNew | BeRelocFlags_Sym))
			{
				auto sym = reloc.mSym;
				sym = ProcessSymbol(sym);
				reloc.mSym = sym;
				
				if (sym->mKind == BlSymKind_Undefined)
				{					
					BlSymNotFound symNotFound;
					symNotFound.mOutOffset = reloc.mOutOffset;
					symNotFound.mSegmentIdx = seg->mSegmentIdx;
					symNotFound.mSym = sym;
					mSymNotFounds.push_back(symNotFound);

					//reloc.mFlags = (BeRelocFlags)(reloc.mFlags & ~BeRelocFlags_SymIsN | BeRelocFlags_Sym);
					//reloc.mSym = sym;					
				}
				else
				{
					bool didLocSet = false;
					if ((sym->mObjectDataIdx != -1) && (sym->mSegmentIdx >= 0))
					{
						BlObjectData* symObjectData = mObjectDatas[sym->mObjectDataIdx];
						if (symObjectData->mKind == BlObjectDataKind_SpecifiedObj)
						{
							// If this sym comes from a specified object file then it can't be overwritten, we can resolve now
							reloc.mFlags = (BeRelocFlags)(reloc.mFlags & ~BeRelocFlags_Sym | BeRelocFlags_Loc);
							reloc.mSegmentIdx = sym->mSegmentIdx;
							reloc.mSegmentOffset = sym->mSegmentOffset;
							didLocSet = true;
						}
					}
					
					/*if (!didLocSet)
					{
						reloc.mFlags = (BeRelocFlags)(reloc.mFlags & ~BeRelocFlags_SymName | BeRelocFlags_Sym);
						reloc.mSym = sym;
					}*/
				}
			}			

			if ((reloc.mFlags & BeRelocFlags_SymIsNew) != 0)
				reloc.mFlags = (BeRelocFlags)(reloc.mFlags & ~BeRelocFlags_SymIsNew);

			// Handle any work queue items and then retry this reloc on next pass through ResolveRelocs
			if (!mObjectDataWorkQueue.empty())
				return;

			seg->mResolvedRelocIdx++;
		}

		mProcessingSegIdx = (mProcessingSegIdx + 1) % (int)mSegments.size();
		if (mProcessingSegIdx == startProcessingSegIdx)
			break;
	}
}

static bool IsResIdNamed(const char16_t* id)
{
	return ((uintptr)id >= 0x10000);
}

static const char16_t* ParseResStr(uint8*& data)
{
	char16_t& firstC = GET(char16_t);
	if (firstC == 0xFFFF)
	{
		int16 id = GET(int16);
		return (const char16_t*)(intptr)id;
	}

	while (true)
	{
		char16_t& nextC = GET(char16_t);
		if (nextC == 0)
			break;
	}

	return &firstC;
}

void BlContext::LoadResourceData(BlObjectData* objectData)
{
	uint8* data = (uint8*)objectData->mData;
	uint8* dataEnd = data + objectData->mSize;

	while (data < dataEnd)
	{		
		uint32 dataSize = GET(uint32);
		uint32 headerSize = GET(uint32);

		const char16_t* resType = ParseResStr(data);		
		const char16_t* resName = ParseResStr(data);
		PTR_ALIGN(data, (uint8*)objectData->mData, 4);

		uint32 dataVersion = GET(uint32);
		uint16 memoryFlags = GET(uint16);
		uint16 languageId = GET(uint16);
		uint32 version = GET(uint32);
		uint32 characteristics = GET(uint32);

		if (dataSize == 0)
		{
			PTR_ALIGN(data, (uint8*)objectData->mData, 4);
			continue;
		}

		// Index by type
		BlResDirectory* typeResDir;
		auto pair = mRootResDirectory.mEntries.insert(std::make_pair(resType, (BlResEntry*)NULL));
		if (pair.second)
		{
			typeResDir = mResEntries.Alloc<BlResDirectory>();
			typeResDir->mIsDir = true;
			pair.first->second = typeResDir;
		}
		else
			typeResDir = (BlResDirectory*)pair.first->second;
		
		// Index by name
		BlResDirectory* nameResDir;
		pair = typeResDir->mEntries.insert(std::make_pair(resName, (BlResEntry*)NULL));
		if (pair.second)
		{
			nameResDir = mResEntries.Alloc<BlResDirectory>();
			nameResDir->mIsDir = true;
			pair.first->second = nameResDir;
		}
		else
			nameResDir = (BlResDirectory*)pair.first->second;

		// Index by language
		pair = nameResDir->mEntries.insert(std::make_pair((const char16_t*)(intptr)languageId, (BlResEntry*)NULL));
		if (pair.second)
		{
			auto resData = mResEntries.Alloc<BlResData>();
			resData->mIsDir = false;
			pair.first->second = resData;
			resData->mData = (uint8*)data;
			resData->mSize = dataSize;
		}

		data += dataSize;
		PTR_ALIGN(data, (uint8*)objectData->mData, 4);
	}
}

void BlContext::LoadDefData(BlObjectData* objectData)
{
	uint8* data = (uint8*)objectData->mData;
	uint8* dataEnd = data + objectData->mSize;
	char* cmdStart = (char*)data;
	bool isSep = false;

	std::vector<String> args;
	int lineNum = 1;

	auto _Fail = [&] (const StringImpl& err)
	{
		Fail(StrFormat("%s in %s on line %d", err.c_str(), ObjectDataIdxToString(objectData->mIdx).c_str(), lineNum));
	};

	bool inExports = false;
	while (true)
	{
		char c;
		auto cPtr = (char*)data;
		if (data == dataEnd)
			c = 0;
		else
			c = GET(char);
		
		bool newIsSep = (c == ',') || (c == '=') || (c == '.');

		if ((isspace(c)) || (c == 0) || (newIsSep) || (isSep))
		{
			if (cmdStart != NULL)
			{
				String str((char*)cmdStart, cPtr);
				args.emplace_back(str);
				cmdStart = NULL;
			}
		}
		isSep = newIsSep;
		
		if ((cmdStart == NULL) && (!isspace(c)))
		{
			cmdStart = cPtr;
		}				

		if (((c == '\r') || (c == '\n') || (c == 0)) && 
			(!args.empty()))
		{
			if (args[0] == "LIBRARY")
			{
				// Ignore
			}
			else if (args[0] == "EXPORTS")
			{
				inExports = true;
				if (args.size() != 1)
					_Fail("Invalid argument");
			}
			else if (args[0] == "HEAPSIZE")
			{
				if (args.size() == 2)
				{
					int reserve = strtol(args[1].c_str(), NULL, 0);
					if (reserve != 0)
						mHeapReserve = BF_ALIGN(reserve, 4);
				}
				else if (args.size() == 4)
				{
					int reserve = strtol(args[1].c_str(), NULL, 0);
					if (args[2] != ",")
						_Fail("Invalid arguments");
					int commit = strtol(args[3].c_str(), NULL, 0);

					if (reserve != 0)
						mHeapReserve = BF_ALIGN(reserve, 4);
					if (commit != 0)
						mHeapCommit = BF_ALIGN(commit, 4);
				}
				else
				{
					_Fail("Invalid number of arguments for HEAPSIZE");
				}
			}
			else if (args[0] == "STACKSIZE")
			{
				if (args.size() == 2)
				{
					int reserve = strtol(args[1].c_str(), NULL, 0);
					if (reserve != 0)
						mHeapReserve = BF_ALIGN(reserve, 4);
				}
				else if (args.size() == 4)
				{
					int reserve = strtol(args[1].c_str(), NULL, 0);
					if (args[2] != ",")
						_Fail("Invalid arguments");
					int commit = strtol(args[3].c_str(), NULL, 0);

					if (reserve != 0)
						mStackReserve = BF_ALIGN(reserve, 4);
					if (commit != 0)
						mStackCommit = BF_ALIGN(commit, 4);
				}
				else
				{
					_Fail("Invalid number of arguments for STACKSIZE");
				}
			}
			else if (args[0] == "VERSION")
			{
				if (args.size() == 2)
				{
					mPeVersionMajor = strtol(args[1].c_str(), NULL, 0);					
				}
				else if (args.size() == 4)
				{
					mPeVersionMajor = strtol(args[1].c_str(), NULL, 0);
					if (args[2] != ".")
						_Fail("Invalid arguments");
					mPeVersionMinor = strtol(args[3].c_str(), NULL, 0);					
				}
				else
				{
					_Fail("Invalid number of arguments for VERSION");
				}
			}
			else if (inExports)
			{
				BlExport blExport;

				String name = args[0];
				
				for (int argIdx = 1; argIdx < (int)args.size(); argIdx++)
				{					
					auto& arg = args[argIdx];
					if ((argIdx == 1) && (arg == "="))
					{
						argIdx++;
						blExport.mSrcName = args[argIdx];
						continue;
					}

					if (arg[0] == '@')
					{
						blExport.mOrdinal = strtol(arg.c_str() + 1, NULL, 0);
						blExport.mOrdinalSpecified = true;
					}
					else if (arg == "NONAME")
					{
						blExport.mNoName = true;
					}
					else if (arg == "PRIVATE")
					{
						blExport.mIsPrivate = true;
					}
					else if (arg == "DATA")
					{
						blExport.mIsData = true;
					}
					else
						_Fail("Invalid export argument");
				}

				auto pair = mExports.insert(std::make_pair(name, blExport));
				if (!pair.second)
				{
					Warn(StrFormat("Export '%s' specified multiple times; using first specification", name.c_str()));
				}
				//mExports[name] = export;
			}
			else
			{
				_Fail("Unrecognized command");
			}
			// Process			
			args.clear();
		}

		if (c == '\n')
			lineNum++;

		if (c == 0)
			break;
	}
}

void BlContext::Init(bool is64Bit, bool isDebug)
{
	char cwd[MAX_PATH];
	_getcwd(cwd, MAX_PATH);
	AddSearchPath(cwd);
	AddSearchPath("C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\lib\\amd64\\");
	AddSearchPath("C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\atlmfc\\lib\\amd64\\");
	AddSearchPath("C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.10240.0\\ucrt\\x64\\");
	AddSearchPath("C:\\Program Files (x86)\\Windows Kits\\8.1\\lib\\winv6.3\\um\\x64\\");

	if (!mNoDefaultLib)
	{
		for (auto& libNameSrc : mDefaultLibs)
		{
			String libName = libNameSrc;
			FixLibName(libName);
			String libCanonName = FixPathAndCase(libName);
			auto itr = mNoDefaultLibs.find(libCanonName);
			if (itr == mNoDefaultLibs.end())
				AddFile(libName, BlObjectDataKind_DefaultLib);
		}

		/*AddFile("libcmtd.lib", BlObjectDataKind_DefaultLib);
		AddFile("oldnames.lib", BlObjectDataKind_DefaultLib);
		AddFile("vcruntimed.lib", BlObjectDataKind_DefaultLib);
		AddFile("ucrtd.lib", BlObjectDataKind_DefaultLib);*/
	}
}

void BlContext::AddSearchPath(const StringImpl& directory)
{
	String dir = directory;
	if (!dir.empty())
	{
		char endC = dir[dir.length() - 1];
		if ((endC != '\\') && (endC != '/'))
			dir += "\\";
	}
	mSearchPaths.push_back(dir);
}

bool BlContext::DoAddFile(const StringImpl& path, BlObjectDataKind objectDataKind)
{
	auto mappedFile = mMappedFiles.Alloc();
	if (!mappedFile->Open(path))
	{
		Fail(StrFormat("Failed to open file: %s", path.c_str()));
		return false;
	}
	BF_ASSERT(mappedFile->mData != NULL);

	bool isLib = (objectDataKind != BlObjectDataKind_SpecifiedObj);

	if (isLib)
	{
		BL_AUTOPERF("BlContext::AddFile ReadLib");

		mNumLibs++;
				
		bool isThinLib = (isLib) && (strncmp((char*)mappedFile->mData, "!<thin>\n", 8) == 0);
		if ((!isThinLib) && (strncmp((char*)mappedFile->mData, "!<arch>\n", 8) != 0))
		{
			Fail(StrFormat("Invalid lib file format: %s", path.c_str()));
			return false;
		}

		uint8* data = (uint8*)mappedFile->mData;
		uint8* dataEnd = data + mappedFile->mFileSize;
		data += 8;

		std::vector<int> objectDataIndices;
		const int minMemberSize = sizeof(BlLibMemberHeader);
		objectDataIndices.resize(mappedFile->mFileSize / minMemberSize + 1, -1);

		auto blLib = mLibs.Alloc();
		blLib->mFileName = path;

		int memberIdx = 0;
		while (data < dataEnd)
		{
			BlLibMemberHeader* header = (BlLibMemberHeader*)data;

			//BfBitSet archUsed;
			//archUsed.Init(mappedFile->mFileSize);

			data += sizeof(BlLibMemberHeader);
			if ((strncmp(header->mName, "/ ", 2) == 0) && (memberIdx == 0))
			{
				int numSymbols = FromBigEndian(*(int32*)data);
				data += 4;

				uint8* strTab = data + numSymbols * 4;
				for (int symIdx = 0; symIdx < numSymbols; symIdx++)
				{
					//OutputDebugStrF("LibSym: %s:%s\n", fileName.c_str(), (const char*)strTab);

					auto blSymbol = mSymTable.Add((const char*)strTab);
					strTab += strlen((char*)strTab) + 1;
					if (blSymbol->mKind == BlSymKind_LibSymbolRef)
					{
						int groupId = (int)mImportGroups.size();
						auto importGroup = mImportGroups.Alloc();
						importGroup->mObjectDatas.push_back(blSymbol->mParam);
						blSymbol->mKind = BlSymKind_LibSymbolGroupRef;
						blSymbol->mParam = groupId;
					}

					if ((blSymbol->mKind != BlSymKind_Undefined) && (blSymbol->mKind != BlSymKind_WeakSymbol) && 
						(blSymbol->mKind != BlSymKind_LibSymbolGroupRef))
						continue;

					int offset = FromBigEndian(((int32*)data)[symIdx]);
					int memberNum = offset / minMemberSize;

					int objectDataIdx = objectDataIndices[memberNum];
					if (objectDataIdx == -1)
					{
						objectDataIdx = (int)mObjectDatas.size();
						auto objectData = mObjectDatas.Alloc();
						if (objectDataKind == BlObjectDataKind_SpecifiedObj)
							objectData->mKind = BlObjectDataKind_SpecifiedLib;
						else
							objectData->mKind = objectDataKind;
						objectData->mIdx = objectDataIdx;
						objectData->mLib = blLib;
						objectData->mData = (uint8*)mappedFile->mData + offset;
						objectData->mLoadState = isThinLib ? BlObjectDataLoadState_UnloadedThinLib : BlObjectDataLoadState_UnloadedLib;
						objectDataIndices[memberNum] = objectDataIdx;
					}
					if (blSymbol->mKind == BlSymKind_LibSymbolGroupRef)
					{
						auto importGroup = mImportGroups[blSymbol->mParam];
						importGroup->mObjectDatas.push_back(objectDataIdx);
					}
					else
					{
						blSymbol->mKind = BlSymKind_LibSymbolRef;
						blSymbol->mParam = objectDataIdx;
						blSymbol->mObjectDataIdx = -1;
					}
				}
			}
			if (strncmp(header->mName, "// ", 3) == 0)
			{
				blLib->mStrTable = (char*)data;
				break;
			}

			int len = atoi(header->mSize);
			len = (len + 1) & ~1; // Even addr
			data = (uint8*)header + sizeof(BlLibMemberHeader) + len;

			memberIdx++;
		}
	}
	else
	{
		mNumObjFiles++;

		int objectDataIdx = (int)mObjectDatas.size();
		auto objectData = mObjectDatas.Alloc();
		objectData->mKind = objectDataKind;
		objectData->mIdx = objectDataIdx;
		objectData->mName = (char*)mAlloc.AllocBytes((int)path.length() + 1);
		memcpy(objectData->mName, path.c_str(), path.length() + 1);
		objectData->mData = mappedFile->mData;
		objectData->mSize = mappedFile->mFileSize;
		BF_ASSERT(objectData->mData != NULL);
		mObjectDataWorkQueue.push_back(objectData);
	}

	return true;
}

bool BlContext::AddFile(const StringImpl& fileName, BlObjectDataKind objectDataKind)
{
	BL_AUTOPERF("BlContext::AddFile");

	bool isLib = false;
	int dotPos = (int)fileName.LastIndexOf('.');
	if ((dotPos != -1) && (strncmp(fileName.c_str() + dotPos, ".lib", 4) == 0))
	{
		String lookupStr = FixPathAndCase(fileName);
		if (!mSeenLibs.insert(lookupStr).second)
			return true; // Already seen
		isLib = true;
		if (objectDataKind == BlObjectDataKind_SpecifiedObj)
			objectDataKind = BlObjectDataKind_SpecifiedLib;
	}

	bool isAbsPath = (fileName.length() >= 2) &&
		((fileName[0] == '/') || (fileName[0] == '\\') || (fileName[1] == ':'));

	if (isAbsPath)
	{
		if (FileExists(fileName))
			return DoAddFile(fileName, objectDataKind);
	}
	else
	{
		String path;
		String actualFileName;
		for (auto& searchPath : mSearchPaths)
		{
			path = searchPath;
			path += fileName;
			if (FileExists(path, &actualFileName))
			{
				path = FixFilePath(path, actualFileName);
				return DoAddFile(path, objectDataKind);
			}
		}
	}

	Fail(StrFormat("Unable to locate file: %s", fileName.c_str()));
	return false;
}

void BlContext::PopulateIData_LookupTable(BlSegment* iDataSect, int& hintNameTableSize)
{
	int importLookupTableSize = ((int)mImportLookups.size() + (int)mImportFiles.size()) * 8;
	int importDirectoryTableSize = ((int)mImportFiles.size() + 1) * 20;
	int hintNameTableOfs = importLookupTableSize + importDirectoryTableSize + importLookupTableSize;

	// Import lookup table
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFile = importFilePair.second;
		for (auto lookup : importFile.mLookups)
		{			
			if (lookup->mName.empty())
			{
				mImportStream.Write((int64)0x8000000000000000LL | lookup->mHint);
			}
			else
			{
				mImportStream.Write((int64)iDataSect->mRVA + hintNameTableOfs + hintNameTableSize);
				hintNameTableSize += 2;
				hintNameTableSize += (int)lookup->mName.length() + 1;
				if ((hintNameTableSize % 2) == 1)
					hintNameTableSize++;
			}			
		}

		mImportStream.Write((int64)0);
	}
}

void BlContext::PopulateIData(BlSegment* iDataSect)
{	
	int importLookupTableSize = ((int)mImportLookups.size() + (int)mImportFiles.size()) * 8;

	int hintNameTableSize = 0;
	PopulateIData_LookupTable(iDataSect, hintNameTableSize);

	int idataIdx = 0;
	int thunkIdx = 0;
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFile = importFilePair.second;
		for (auto lookup : importFile.mLookups)
		{
			lookup->mThunkIdx = thunkIdx++;
			lookup->mIDataIdx = idataIdx++;
		}
		idataIdx++; // For NULL entry
	}
	
	int importDirectoryTableSize = ((int)mImportFiles.size() + 1) * 20;
	int importDirOfs = importLookupTableSize;
	int importLookupOfs = importDirOfs + importDirectoryTableSize;
	int hintNameTableOfs = importLookupTableSize + importDirectoryTableSize + importLookupTableSize;
	int strTableOfs = hintNameTableOfs + hintNameTableSize;

	// Import directory
	int curLookupIdx = 0;
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFileName = importFilePair.first;
		auto& importFile = importFilePair.second;
		mImportStream.Write((int32)(iDataSect->mRVA + importLookupOfs + curLookupIdx*8)); // Import lookup table RVA
		mImportStream.Write((int32)0); // Timestamp
		mImportStream.Write((int32)0); // Forwarder chain
		mImportStream.Write((int32)(iDataSect->mRVA + strTableOfs)); // Name RVA
		mImportStream.Write((int32)(iDataSect->mRVA + curLookupIdx * 8)); // Import address table RVA
		curLookupIdx += (int)importFile.mLookups.size() + 1;
		strTableOfs += (int)importFileName.length() + 1;
	}
	
	// Empty import directory entry terminates
	mImportStream.WriteZeros(20);

	hintNameTableSize = 0;
	PopulateIData_LookupTable(iDataSect, hintNameTableSize);

	// Hint/Name Table
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFile = importFilePair.second;
		for (auto lookup : importFile.mLookups)
		{
			if (!lookup->mName.empty())						
			{
				mImportStream.Write((int16)lookup->mHint);
				mImportStream.Write((void*)lookup->mName.c_str(), (int)lookup->mName.length() + 1);				
				if ((mImportStream.GetPos() % 2) == 1)
					mImportStream.Write((uint8)0);
			}
		}
	}

	// Str table
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFileName = importFilePair.first;
		mImportStream.Write((void*)importFileName.c_str(), (int)importFileName.length() + 1);
	}

	BF_ASSERT(mImportStream.GetPos() == strTableOfs);

	BlChunk chunk;
	chunk.mOffset = 0;
	chunk.mAlignPad = 0;
	chunk.mData = (void*)&mImportStream.mData[0];
	chunk.mSize = mImportStream.GetSize();
	iDataSect->mChunks.push_back(chunk);
	BF_ASSERT(iDataSect->mCurSize == chunk.mSize);
}

BlSegment* BlContext::CreateIData()
{
	auto iDataSect = mDynSegments.Alloc();
	iDataSect->mName = ".idata";
	iDataSect->mContext = this;
	iDataSect->mSegmentIdx = (int)mSegments.size();
	iDataSect->mAlign = 8;
	iDataSect->mCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	mSegments.push_back(iDataSect);
	mOrderedSegments.push_back(iDataSect);	
	
	int hintNameTableSize = 0;
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFile = importFilePair.second;
		for (auto lookup : importFile.mLookups)
		{
			if (!lookup->mName.empty())			
			{				
				hintNameTableSize += 2;
				hintNameTableSize += (int)lookup->mName.length() + 1;
				if ((hintNameTableSize % 2) == 1)
					hintNameTableSize++;
			}
		}
	}

	int importLookupTableSize = ((int)mImportLookups.size() + (int)mImportFiles.size()) * 8;
	int importDirectoryTableSize = ((int)mImportFiles.size() + 1) * 20;	
	int hintNameTableOfs = importLookupTableSize + importDirectoryTableSize + importLookupTableSize;
	int strTableOfs = hintNameTableOfs + hintNameTableSize;

	// Import directory
	int curLookupIdx = 0;
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFileName = importFilePair.first;		
		strTableOfs += (int)importFileName.length() + 1;
	}

	iDataSect->mCurSize = strTableOfs;	
	return iDataSect;
}

BlSegment* BlContext::CreateThunkData()
{
	if (mImportLookups.empty())
		return NULL;

	auto thunkSect = mDynSegments.Alloc();
	thunkSect->mName = ".text$thunk";	
	thunkSect->mContext = this;
	thunkSect->mSegmentIdx = (int)mSegments.size();
	thunkSect->mAlign = 8;
	thunkSect->mCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
	mSegments.push_back(thunkSect);
	mOrderedSegments.push_back(thunkSect);

	thunkSect->mCurSize = (int)mImportLookups.size() * 6;
	return thunkSect;
}

void BlContext::PopulateExportData(BlSegment* exportSeg)
{	
	std::vector<uint32> rvaMap;

	String libName = GetFileName(mOutName);
	DynMemStream strTab;

	int numNames = 0;
	int maxOrdinal = 0;
	for (auto& exportPair : mExports)
	{
		const StringImpl& name = exportPair.first;
		auto& blExport = exportPair.second;
		String symName;
		if (!blExport.mSrcName.empty())
			symName = blExport.mSrcName;
		else
			symName = name;
		if (!blExport.mNoName)
			numNames++;

		int ordIdx = blExport.mOrdinal - 1;
		while (ordIdx >= rvaMap.size())
			rvaMap.push_back(0);

		auto sym = ResolveSym(symName.c_str(), false);
		if (sym == NULL)
		{
			Fail(StrFormat("Unable to find exported symbol '%s'", symName.c_str()));
		}
		else
		{
			rvaMap[ordIdx] = (uint32)GetSymAddr(sym);
		}
	}

	int numEntries = maxOrdinal - 1;
	
	int32 funcRVA = exportSeg->mRVA + (int)sizeof(IMAGE_EXPORT_DIRECTORY);
	int32 namePtrTabRVA = funcRVA + (int)rvaMap.size() * 4;
	int32 ordTabRVA = namePtrTabRVA + numNames * 4;
	int32 strTabRVA = ordTabRVA + numNames * 2;

	// Export Directory Table
	IMAGE_EXPORT_DIRECTORY exportDir = { 0 };	
	exportDir.Name = strTabRVA + strTab.GetSize();
	strTab.Write((void*)libName.c_str(), (int)libName.length() + 1);
	exportDir.Base = 1;
	exportDir.NumberOfFunctions = (uint32)rvaMap.size();
	exportDir.NumberOfNames = numNames;
	exportDir.AddressOfFunctions = funcRVA;
	exportDir.AddressOfNames = namePtrTabRVA;
	exportDir.AddressOfNameOrdinals = ordTabRVA;
	mExportStream.WriteT(exportDir);

	// Export Address Table
	mExportStream.Write((void*)&rvaMap[0], (int)rvaMap.size() * 4);
	
	// Name Pointer Table
	for (auto& exportPair : mExports)
	{
		const StringImpl& name = exportPair.first;
		auto& blExport = exportPair.second;
		if (!blExport.mNoName)
		{
			mExportStream.Write((int32)(strTabRVA + strTab.GetSize()));
			strTab.Write((void*)name.c_str(), (int)name.length() + 1);
		}
	}
	
	// Export Ordinal Table
	for (auto& exportPair : mExports)
	{		
		auto& blExport = exportPair.second;
		if (!blExport.mNoName)
			mExportStream.Write((int16)(blExport.mOrdinal - 1));
	}

	// String table data
	mExportStream.Write(strTab.GetPtr(), strTab.GetSize());

	BlChunk chunk;
	chunk.mOffset = 0;
	chunk.mAlignPad = 0;
	chunk.mData = mExportStream.GetPtr();
	chunk.mSize = mExportStream.GetSize();
	exportSeg->mChunks.push_back(chunk);
	BF_ASSERT(exportSeg->mCurSize == chunk.mSize);
}

BlSegment* BlContext::CreateExportData()
{
	if (mExports.size() == 0)
		return NULL;

	String libName = GetFileName(mOutName);

	int curOrdinal = 1;
	int maxOrdinal = 0;
	std::unordered_map<int, const String*> usedIds;
	
	std::vector<int> ordinalMap;

	int strTableSize = (int)libName.length() + 1;
	int numNames = 0;

	for (auto& exportPair : mExports)
	{
		auto& name = exportPair.first;
		auto& blExport = exportPair.second;
		if (blExport.mOrdinal > 0)
		{
			maxOrdinal = std::max(maxOrdinal, blExport.mOrdinal);
			auto pair = usedIds.insert(std::make_pair(blExport.mOrdinal, &name));
			if (!pair.second)
			{
				Fail(StrFormat("Export '%s' and export '%s' both specified ordinal %d", pair.first->second->c_str(), exportPair.first.c_str(), blExport.mOrdinal));
				blExport.mOrdinal = -1;
			}			
		}
		if (!blExport.mNoName)
		{
			strTableSize += (int)name.length() + 1;
			numNames++;
		}
	}

	for (auto& exportPair : mExports)
	{		
		const StringImpl& name = exportPair.first;	
		auto& blExport = exportPair.second;
		if (blExport.mOrdinal <= 0)
		{
			while (usedIds.find(curOrdinal) != usedIds.end())
				curOrdinal++;
			blExport.mOrdinal = curOrdinal++;
			maxOrdinal = std::max(maxOrdinal, blExport.mOrdinal);
		}
	}
	
	int numEntries = maxOrdinal;

	auto thunkSect = mDynSegments.Alloc();
	thunkSect->mName = ".rdata$export";
	thunkSect->mContext = this;
	thunkSect->mSegmentIdx = (int)mSegments.size();
	thunkSect->mAlign = 8;
	thunkSect->mCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	mSegments.push_back(thunkSect);
	mOrderedSegments.push_back(thunkSect);

	thunkSect->mCurSize = sizeof(IMAGE_EXPORT_DIRECTORY) + (numEntries * 4) + (numNames * 4) + (numNames * 2) + strTableSize;
	return thunkSect;
}

void BlContext::PopulateCvInfoData(BlSegment* cvInfoSect)
{
	mCvInfoStream.Write((int32)'SDSR');
	mCvInfoStream.WriteT(mCodeView->mSignature);
	mCvInfoStream.Write(mCodeView->mAge);

	String pdbName = mCodeView->mMsf.mFileName;
	//String pdbName = "C:\\proj\\TestCPP\\x64\\Debug\\TestCPP.pdb";
	mCvInfoStream.Write((void*)pdbName.c_str(), (int)pdbName.length() + 1);

	BlChunk chunk;
	chunk.mOffset = 0;
	chunk.mAlignPad = 0;
	chunk.mData = (void*)&mCvInfoStream.mData[0];
	chunk.mSize = mCvInfoStream.GetSize();
	cvInfoSect->mChunks.push_back(chunk);
}

BlSegment* BlContext::CreateCvInfoData()
{
	auto cvInfoSect = mDynSegments.Alloc();
	cvInfoSect->mName = ".rdata$cvInfo";
	cvInfoSect->mContext = this;
	cvInfoSect->mSegmentIdx = (int)mSegments.size();
	cvInfoSect->mAlign = 8;
	cvInfoSect->mCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	mSegments.push_back(cvInfoSect);
	mOrderedSegments.push_back(cvInfoSect);

	cvInfoSect->mCurSize = 4 + 4 + 16 + (int)mCodeView->mMsf.mFileName.length() + 1;
	return cvInfoSect;
}

void BlContext::PopulateDebugDirData(BlSegment* debugDirSect, BlSegment* cvInfoSect)
{
	IMAGE_DEBUG_DIRECTORY debugDir = { 0 };
	debugDir.TimeDateStamp = mTimestamp;
	debugDir.Type = 2;
	debugDir.SizeOfData = cvInfoSect->mCurSize;
	debugDir.AddressOfRawData = cvInfoSect->mRVA;
	auto outSection = mOutSections[cvInfoSect->mOutSectionIdx];
	debugDir.PointerToRawData = (cvInfoSect->mRVA - outSection->mRVA) + outSection->mRawDataPos;
	mDebugDirStream.WriteT(debugDir);
	
	BlChunk chunk;
	chunk.mOffset = 0;
	chunk.mAlignPad = 0;
	chunk.mData = (void*)&mDebugDirStream.mData[0];
	chunk.mSize = mDebugDirStream.GetSize();
	debugDirSect->mChunks.push_back(chunk);
	BF_ASSERT(debugDirSect->mCurSize == chunk.mSize);	
}

BlSegment* BlContext::CreateDebugDirData()
{		
	auto debugDirSect = mDynSegments.Alloc();
	debugDirSect->mName = ".rdata$debugDir";
	debugDirSect->mContext = this;
	debugDirSect->mSegmentIdx = (int)mSegments.size();
	debugDirSect->mAlign = 8;
	debugDirSect->mCharacteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	mSegments.push_back(debugDirSect);
	mOrderedSegments.push_back(debugDirSect);
	
	debugDirSect->mCurSize = sizeof(IMAGE_DEBUG_DIRECTORY);
	return debugDirSect;
}

void BlContext::GetResDataStats(BlResDirectory* resDir, int& dirCount, int& dataCount, int& symsStrsSize, int& dataSize)
{	
	for (auto& pair : resDir->mEntries)
	{
		auto resId = pair.first;
		if (IsResIdNamed(resId))
			symsStrsSize += 2 + (int)wcslen((wchar_t*)resId) * 2;
		auto entry = pair.second;
		if (entry->mIsDir)
		{
			dirCount++;
			GetResDataStats((BlResDirectory*)entry, dirCount, dataCount, symsStrsSize, dataSize);
		}
		else
		{
			BlResData* resData = (BlResData*)entry;
			dataCount++;
			dataSize += resData->mSize;
		}
	}
}

void BlContext::CreateResData(BlSegment* resSeg, BlResDirectory* resDir, int resDirSize, int symsStrSize, DynMemStream& symStrsStream, DynMemStream& dataEntryStream)
{
	int numNamedEntries = 0;
	int numIdEntries = 0;
	for (auto& pair : resDir->mEntries)
	{
		auto resId = pair.first;
		if (IsResIdNamed(resId))
			numNamedEntries++;
		else
			numIdEntries++;
	}

	mResDataStream.Write(0); // Characteristics (always 0)
	mResDataStream.Write(0); // Timestamp
	mResDataStream.Write(0); // Version
	mResDataStream.Write((int16)numNamedEntries); // Number of named entries
	mResDataStream.Write((int16)numIdEntries); // Number of id entries

	int curEntriesPos = mResDataStream.GetPos();
	mResDataStream.WriteZeros((int)resDir->mEntries.size() * 8);

	for (int pass = 0; pass < 2; pass++)
	{
		for (auto& pair : resDir->mEntries)
		{
			auto resId = pair.first;
			if ((IsResIdNamed(resId)) == (pass == 0))
			{
				if (pass == 0)
				{
					// Name
					*(int32*)(&mResDataStream.mData[curEntriesPos]) = 0x80000000 | (resDirSize + symStrsStream.GetSize());
					int strLen = (int)wcslen((wchar_t*)resId);
					symStrsStream.Write((int16)strLen);
					symStrsStream.Write((uint8*)resId, strLen * 2);
				}
				else
					*(int32*)(&mResDataStream.mData[curEntriesPos]) = (int16)(intptr)resId;

				auto resEntry = pair.second;
				if (resEntry->mIsDir)
				{
					*(int32*)(&mResDataStream.mData[curEntriesPos + 4]) = 0x80000000 | mResDataStream.GetPos();
					CreateResData(resSeg, (BlResDirectory*)resEntry, resDirSize, symsStrSize, symStrsStream, dataEntryStream);
				}
				else
				{
					BlResData* resData = (BlResData*)resEntry;
					*(int32*)(&mResDataStream.mData[curEntriesPos + 4]) = (resDirSize + symsStrSize) + dataEntryStream.GetSize();

					dataEntryStream.Write((int32)(resSeg->mRVA + resSeg->mCurSize));
					dataEntryStream.Write((int32)resData->mSize);
					dataEntryStream.Write((int32)0); // Codepage
					dataEntryStream.Write((int32)0); // Reserved

					BlChunk chunk;
					chunk.mOffset = -1;
					chunk.mAlignPad = 0;
					chunk.mData = resData->mData;	
					chunk.mSize = resData->mSize;
					resSeg->mChunks.push_back(chunk);

					resSeg->mCurSize += resData->mSize;
				}

				curEntriesPos += 8;
			}
		}
	}
}


void BlContext::PopulateResData(BlSegment* resSeg)
{		
	int prevSize = resSeg->mCurSize;

	// Insert fake chunk
	{
		BlChunk chunk;
		chunk.mOffset = 0;
		chunk.mAlignPad = 0;
		chunk.mData = 0;
		chunk.mSize = 0;
		resSeg->mChunks.push_back(chunk);
	}

	int dirCount = 0;
	int dataCount = 0;
	int symsStrSize = 0;
	int dataSize = 0;
	GetResDataStats(&mRootResDirectory, dirCount, dataCount, symsStrSize, dataSize);
	int resDirSize = 16 + ((16 + 8) * dirCount) + ((8) * dataCount);
	resSeg->mCurSize = resDirSize + symsStrSize + (dataCount * 16);

	DynMemStream symStrsStream;
	DynMemStream dataEntryStream;
	CreateResData(resSeg, &mRootResDirectory, resDirSize, symsStrSize, symStrsStream, dataEntryStream);
	BF_ASSERT(mResDataStream.GetSize() == resDirSize);

	if (symStrsStream.GetSize() != 0)
		mResDataStream.Write(symStrsStream.GetPtr(), symStrsStream.GetSize());
	if (dataEntryStream.GetSize() != 0)
		mResDataStream.Write(dataEntryStream.GetPtr(), dataEntryStream.GetSize());

	BlChunk& chunk = resSeg->mChunks.front();	
	chunk.mData = (void*)&mResDataStream.mData[0];
	chunk.mSize = mResDataStream.GetSize();	
	BF_ASSERT(resSeg->mCurSize == prevSize);
}

BlSegment* BlContext::CreateSegment(const StringImpl& name, int characteristics, int align)
{
	auto seg = mDynSegments.Alloc();
	seg->mName = name;
	seg->mContext = this;
	seg->mSegmentIdx = (int)mSegments.size();
	seg->mAlign = align;
	seg->mCharacteristics = characteristics;
	AddSegment(seg);
	return seg;
}

BlSegment* BlContext::CreateResData()
{
	String manifest;
	manifest += "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>\n";
	manifest += "<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>\n";
	manifest += " <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n";
    manifest += "  <security>\n";
	manifest += "   <requestedPrivileges>\n";
	manifest += "    <requestedExecutionLevel ";
	manifest += mManifestUAC;
	manifest += " />\n";
	manifest += "   </requestedPrivileges>\n";
	manifest += "  </security>\n";
	manifest += " </trustInfo>\n";
	manifest += "</assembly>\n";
	mManifestData = manifest;

	auto idResDir = mResEntries.Alloc<BlResDirectory>();
	auto langDir = mResEntries.Alloc<BlResDirectory>();	
	auto manifestData = mResEntries.Alloc<BlResData>();
	manifestData->mIsDir = false;
	manifestData->mData = (void*)mManifestData.c_str();
	manifestData->mSize = (int)mManifestData.length();

	mRootResDirectory.mEntries[(const char16_t*)(intptr)24] = idResDir;
	idResDir->mEntries[(const char16_t*)(intptr)0] = langDir;	
	langDir->mEntries[(const char16_t*)(intptr)1033] = manifestData;

	auto resSeg = CreateSegment(".rsrc", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, 8);
	
	int dirCount = 0;
	int dataCount = 0;
	int symsStrSize = 0;
	int dataSize = 0;
	GetResDataStats(&mRootResDirectory, dirCount, dataCount, symsStrSize, dataSize);	
	int resDirSize = 16 + ((16 + 8) * dirCount) + ((8) * dataCount);
	resSeg->mCurSize = resDirSize + symsStrSize + (dataCount * 16) + dataSize;
	
	return resSeg;
}


void BlContext::PopulateThunkData(BlSegment* thunkSect, BlSegment* iDataSect)
{
	for (auto& importFilePair : mImportFiles)
	{
		auto& importFile = importFilePair.second;
		for (auto lookup : importFile.mLookups)
		{
			// jmp qword ptr [Rel32]
			mThunkStream.Write((uint8)0xFF);
			mThunkStream.Write((uint8)0x25);
			int32 targetAddr = iDataSect->mRVA + (lookup->mIDataIdx * 8);
			targetAddr -= thunkSect->mRVA + (lookup->mThunkIdx * 6) + 6;
			mThunkStream.Write((int32)targetAddr);
		}
	}	

	BlChunk chunk;
	chunk.mOffset = 0;
	chunk.mAlignPad = 0;
	chunk.mData = (void*)&mThunkStream.mData[0];
	chunk.mSize = mThunkStream.GetSize();
	thunkSect->mChunks.push_back(chunk);
	BF_ASSERT(thunkSect->mCurSize == chunk.mSize);
}

namespace BeefyDbg64
{
	void TestPDB(const StringImpl& fileName);
}

void BlContext::WriteOutSection(BlOutSection* outSection, DataStream* st)
{
	int startPos = st->GetPos();

	auto _FillBytes = [&](int size)
	{
		if ((outSection->mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
		{
			uint64 fillData = 0xCCCCCCCCCCCCCCCCLL;
			int alignLeft = size;
			while (alignLeft > 0)
			{
				int alignWrite = std::min(alignLeft, 8);
				st->Write(&fillData, alignWrite);
				alignLeft -= alignWrite;
			}
		}
		else
		{
			st->WriteZeros(size);
		}
	};

	for (auto sect : outSection->mSegments)
	{
		int curPos = st->GetPos();
		int wantPos = (curPos + (sect->mAlign - 1)) & ~(sect->mAlign - 1);
		_FillBytes(wantPos - curPos);

		int sectStartPos = wantPos;
		BF_ASSERT(outSection->mRVA + sectStartPos - startPos == sect->mRVA);

		BlReloc* nextReloc = NULL;
		int curRelocIdx = 0;
		if (!sect->mRelocs.empty())
			nextReloc = &sect->mRelocs[0];
		int curSectOfs = 0;
		
		for (auto& chunk : sect->mChunks)
		{			
			BF_ASSERT(st->GetPos() == sectStartPos + curSectOfs);
			_FillBytes(chunk.mAlignPad);

			curSectOfs += chunk.mAlignPad;

			BF_ASSERT((chunk.mOffset == curSectOfs) || (chunk.mOffset == -1));

			uint8* data = (uint8*)chunk.mData;
			int sizeLeft = chunk.mSize;

			while (sizeLeft > 0)
			{
				if (nextReloc != NULL)
				{
					int relocOfs = nextReloc->mOutOffset - curSectOfs;
					BF_ASSERT(relocOfs >= 0);
					if (relocOfs < sizeLeft)
					{
						st->Write(data, relocOfs);
						curSectOfs += relocOfs;
						data += relocOfs;
						sizeLeft -= relocOfs;

						int64 relocOutVal = 0;
						int relocOutSize = 0;

						int resolvedAddr;
						bool wasAbs = false;
						if (nextReloc->mSegmentIdx == (int)BlSymKind_ImageBaseRel)
						{
							resolvedAddr = nextReloc->mSegmentOffset;
						}
						else if (nextReloc->mSegmentIdx == (int)BlSymKind_Absolute)
						{
							resolvedAddr = nextReloc->mSegmentOffset;
							wasAbs = true;
						}
						else
						{
							if (nextReloc->mKind == BlRelocKind_SECREL)
							{
								BlSegment* resolvedSect = mSegments[nextReloc->mSegmentIdx];
								auto outSection = mOutSections[resolvedSect->mOutSectionIdx];
								relocOutSize = 4;
								relocOutVal = (resolvedSect->mRVA - outSection->mRVA) + nextReloc->mSegmentOffset + *(int32*)data;
							}
							else
							{
								BlSegment* resolvedSect = mSegments[nextReloc->mSegmentIdx];
								resolvedAddr = resolvedSect->mRVA + nextReloc->mSegmentOffset;
							}
						}
						
						switch (nextReloc->mKind)
						{
						case BlRelocKind_ADDR32NB:
							relocOutVal = resolvedAddr + *(int32*)data;
							relocOutSize = 4;
							break;
						case BlRelocKind_ADDR64:
							if (wasAbs)
							{
								relocOutVal = resolvedAddr + *(int32*)data;
								// In cases like loadcfg.obj, __guard_flags gets truncated at the end
								relocOutVal = std::min(8, sizeLeft);
							}
							else
							{
								if (!mHasFixedBase)
									mPeRelocs.Add(sect->mRVA + nextReloc->mOutOffset, IMAGE_REL_BASED_DIR64);
								relocOutVal = mImageBase + resolvedAddr + *(int32*)data;
								relocOutSize = 8;
							}
							break;
						case BlRelocKind_REL32:
							relocOutVal = resolvedAddr + *(int32*)data;
							relocOutVal -= sect->mRVA + curSectOfs + 4;
							relocOutSize = 4;
							break;
						case BlRelocKind_REL32_1:
							relocOutVal = resolvedAddr + *(int32*)data;
							relocOutVal -= sect->mRVA + curSectOfs + 5;
							relocOutSize = 4;
							break;
						case BlRelocKind_REL32_4:
							relocOutVal = resolvedAddr + *(int32*)data;
							relocOutVal -= sect->mRVA + curSectOfs + 8;
							relocOutSize = 4;
							break;
						case BlRelocKind_SECREL:
							// Handled
							break;
						default:
							NotImpl();
							break;
						}

						st->Write(&relocOutVal, relocOutSize);
						curSectOfs += relocOutSize;
						data += relocOutSize;
						sizeLeft -= relocOutSize;

						curRelocIdx++;
						if (curRelocIdx < (int)sect->mRelocs.size())
							nextReloc = &sect->mRelocs[curRelocIdx];
						else
							nextReloc = NULL;
						continue;
					}
				}

				st->Write(data, sizeLeft);
				curSectOfs += sizeLeft;
				sizeLeft = 0;
				break;
			}

			BF_ASSERT(sizeLeft == 0);
			//TODO:
		}
	}	
}

static void FormatNZ(char* toStr, const char* fmt ...)
{
	char buffer[32];

	va_list argList;
	va_start(argList, fmt);
	int result = _vsprintf_l(buffer, fmt, NULL, argList);
	va_end(argList);

	memcpy(toStr, buffer, result);
}

// If this fails, then investigate the SectionRef vs SectionDef rules in BeCOFFObject::MarkSectionUsed
void BlContext::WriteDLLLib(const StringImpl& libName)
{
	FileStream fs;
	if (!fs.Open(libName, "wb"))
	{
		Fail(StrFormat("Unable to create file \"%s\"", libName.c_str()));
		return;
	}

	//mExports.clear();

	struct _LibSym
	{	
		int mOfsIdx;
		int mDataPos;
		BlExport* mExport;
		String mName;
		String mExportName;
		_LibSym* mImp;
		void* mData;
		int mDataSize;

		_LibSym()
		{
			mOfsIdx = -1;
			mDataPos = -1;
			mExport = NULL;
			mImp = NULL;
			mData = NULL;
			mDataSize = 0;
		}
	};

	OwnedVector<_LibSym> libSyms;

	String dllName = GetFileName(mOutName);
	String baseName = dllName.Substring(0, dllName.length() - 4);

	auto _CreateDebugSect = [] (BeCOFFObject& coffObject)
	{		
		// Not actually needed
		return;
		
		/*auto debugSect = coffObject.CreateSect(".debug$S", IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE, false);
		unsigned char hexData[66] = {
			0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x54, 0x65, 0x73,
			0x74, 0x44, 0x6C, 0x6C, 0x32, 0x2E, 0x64, 0x6C, 0x6C, 0x27, 0x00, 0x13, 0x10, 0x07, 0x00, 0x00,
			0x00, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x97, 0x5E, 0x12,
			0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20, 0x28, 0x52, 0x29, 0x20, 0x4C, 0x49,
			0x4E, 0x4B
		};
		debugSect->mData.Write(hexData, 0x42);*/
	};

	String importDescriptorName = "__IMPORT_DESCRIPTOR_" + baseName;
	String nullImportDescriptorName = "__NULL_IMPORT_DESCRIPTOR";
	String nullThunkDataName = "\x7F";
	nullThunkDataName += baseName;
	nullThunkDataName += "_NULL_THUNK_DATA";

	// 
	DynMemStream importDescriptorStream;
	{		
		BeCOFFObject coffObject;
		coffObject.mStream = &importDescriptorStream;
		coffObject.mTimestamp = mTimestamp;

		// iData entry
		auto idata2Sect = coffObject.CreateSect(".idata$2", IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
		idata2Sect->mData.WriteZeros(0x14);
				
		// Dll name entry
		auto idata6Sect = coffObject.CreateSect(".idata$6", IMAGE_SCN_ALIGN_2BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, false);
		idata6Sect->mData.WriteSZ(dllName);
		idata6Sect->mData.Align(2);

		BeMCSymbol* nameSectSym = coffObject.mSymbols.Alloc();
		nameSectSym->mSymKind = BeMCSymbolKind_External;
		nameSectSym->mIsStatic = true;
		nameSectSym->mSectionNum = idata6Sect->mSectionIdx + 1;		
		nameSectSym->mName = ".idata$6";
		nameSectSym->mIdx = (int)coffObject.mSymbols.size() - 1;

		BeMCSymbol* coffSym = coffObject.mSymbols.Alloc();
		coffSym->mSymKind = BeMCSymbolKind_External;
		coffSym->mSectionNum = idata2Sect->mSectionIdx + 1;
		coffSym->mName = importDescriptorName;
		coffSym->mIsStatic = false;

		BeMCSymbol* idata4Sym = coffObject.mSymbols.Alloc();
		idata4Sym->mSymKind = BeMCSymbolKind_SectionRef;		
		idata4Sym->mName = ".idata$4";		
		idata4Sym->mIdx = (int)coffObject.mSymbols.size() - 1;

		BeMCSymbol* idata5Sym = coffObject.mSymbols.Alloc();
		idata5Sym->mSymKind = BeMCSymbolKind_SectionRef;
		idata5Sym->mName = ".idata$5";
		idata5Sym->mIdx = (int)coffObject.mSymbols.size() - 1;

		BeMCSymbol* nullImportDescSym = coffObject.mSymbols.Alloc();
		nullImportDescSym->mSymKind = BeMCSymbolKind_External;
		nullImportDescSym->mName = nullImportDescriptorName;

		BeMCSymbol* nullThunkDataSym = coffObject.mSymbols.Alloc();
		nullThunkDataSym->mSymKind = BeMCSymbolKind_External;
		nullThunkDataSym->mName = nullThunkDataName;

		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = 0x0C;
		//reloc.mSymTableIdx = idata6Sect->mSymbolIdx;
		reloc.mSymTableIdx = nameSectSym->mIdx;
		idata2Sect->mRelocs.push_back(reloc);
		
		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = 0x00;
		reloc.mSymTableIdx = idata4Sym->mIdx;
		idata2Sect->mRelocs.push_back(reloc);

		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = 0x10;
		reloc.mSymTableIdx = idata5Sym->mIdx;
		idata2Sect->mRelocs.push_back(reloc);

		_CreateDebugSect(coffObject);

		coffObject.Finish();

		_LibSym* libSym = libSyms.Alloc();
		libSym->mName = importDescriptorName;
		libSym->mData = importDescriptorStream.GetPtr();
		libSym->mDataSize = importDescriptorStream.GetSize();
	}

	DynMemStream nullImportDescriptorStream;
	{		
		BeCOFFObject coffObject;
		coffObject.mStream = &nullImportDescriptorStream;
		coffObject.mTimestamp = mTimestamp;

		// iData entry terminator
		auto idata3Sect = coffObject.CreateSect(".idata$3", IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
		idata3Sect->mData.WriteZeros(0x14);
		
		BeMCSymbol* coffSym = coffObject.mSymbols.Alloc();
		coffSym->mSymKind = BeMCSymbolKind_External;
		coffSym->mSectionNum = idata3Sect->mSectionIdx + 1;
		coffSym->mName = nullImportDescriptorName;
		coffSym->mIsStatic = false;

		_CreateDebugSect(coffObject);

		coffObject.Finish();

		_LibSym* libSym = libSyms.Alloc();
		libSym->mName = nullImportDescriptorName;
		libSym->mData = nullImportDescriptorStream.GetPtr();
		libSym->mDataSize = nullImportDescriptorStream.GetSize();
	}

	DynMemStream nullThunkDataStream;
	{	
		BeCOFFObject coffObject;
		coffObject.mStream = &nullThunkDataStream;
		coffObject.mTimestamp = mTimestamp;

		// iData entry terminator
		auto idata5Sect = coffObject.CreateSect(".idata$5", IMAGE_SCN_ALIGN_8BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
		idata5Sect->mData.WriteZeros(0x8);

		auto idata4Sect = coffObject.CreateSect(".idata$4", IMAGE_SCN_ALIGN_8BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
		idata4Sect->mData.WriteZeros(0x8);

		BeMCSymbol* coffSym = coffObject.mSymbols.Alloc();
		coffSym->mSymKind = BeMCSymbolKind_External;
		coffSym->mSectionNum = idata5Sect->mSectionIdx + 1;
		coffSym->mName = nullThunkDataName;
		coffSym->mIsStatic = false;

		_CreateDebugSect(coffObject);

		coffObject.Finish();

		_LibSym* libSym = libSyms.Alloc();
		libSym->mName = nullThunkDataName;
		libSym->mData = nullThunkDataStream.GetPtr();
		libSym->mDataSize = nullThunkDataStream.GetSize();
	}

	for (auto& exportPair : mExports)
	{
		auto& blExport = exportPair.second;
		if (blExport.mIsPrivate)
			continue;

		_LibSym* impSym = libSyms.Alloc();
		impSym->mExport = &exportPair.second;
		impSym->mName = "__imp_" + exportPair.first;				
		impSym->mExportName = exportPair.first;
		impSym->mExport = &blExport;
		impSym->mDataSize = (int)sizeof(PEImportObjectHeader) + (int)dllName.length() + 1 + (int)impSym->mExportName.length() + 1;

		if (!blExport.mIsData)
		{
			_LibSym* refSym = libSyms.Alloc();
			refSym->mName = exportPair.first;
			refSym->mImp = impSym;
		}
	}

	// Sort symbol names, required by Second Linker Member
	std::sort(libSyms.begin(), libSyms.end(), [] (const _LibSym* lhs, const _LibSym* rhs)
	{
		return lhs->mName < rhs->mName;
	});
	
	int numExports = (int)libSyms.size();
	int namesLen = 0;	

	DynMemStream syms0;	
	int ofsIdx = 0;
	for (auto& libSym : libSyms)
	{						
		namesLen += (int)libSym->mName.length() + 1;				
		if (libSym->mDataSize > 0)
			libSym->mOfsIdx = ofsIdx++;
	}

	syms0.Write(numExports);

	int headerSize = 8;
	fs.WriteSNZ("!<arch>\n");
	int expectedPos = headerSize;
	
	int member0Size = sizeof(BlLibMemberHeader) + 4 + (numExports * 4) + (namesLen);	
	int member1Size = sizeof(BlLibMemberHeader) + 4 + (ofsIdx * 4) + 4 + (numExports * 2) + (namesLen);
	int nameMemberSize = 0;

	if (dllName.length() > 16)
	{
		nameMemberSize = sizeof(BlLibMemberHeader) + dllName.length() + 1;
	}

	int startExpOfs = expectedPos + BF_ALIGN(member0Size, 2) + BF_ALIGN(member1Size, 2) + BF_ALIGN(nameMemberSize, 2);
	
	int curExpOfs = startExpOfs;
	for (auto& libSym : libSyms)
	{		
		if (libSym->mDataSize > 0)
		{
			int exportSize = (int)sizeof(BlLibMemberHeader) + libSym->mDataSize;
			libSym->mDataPos = curExpOfs;			
			curExpOfs += BF_ALIGN(exportSize, 2);
		}
	}

	BlLibMemberHeader member;
	// First Linker Member (unordered symbol list)	
	member.Init();	
	FormatNZ(member.mName, "/");
	FormatNZ(member.mDate, "%d", mTimestamp);
	FormatNZ(member.mMode, "0");
	FormatNZ(member.mSize, "%d", member0Size - (int)sizeof(BlLibMemberHeader));
	fs.WriteT(member);
	fs.Write(ToBigEndian(numExports));
	for (auto libSym : libSyms)
	{
		if (libSym->mDataPos >= 0)
			fs.Write(ToBigEndian(libSym->mDataPos));		
		else
			fs.Write(ToBigEndian(libSym->mImp->mDataPos));		
	}
	for (auto& libSym : libSyms)
	{
		fs.WriteSZ(libSym->mName);		
	}
	expectedPos += member0Size;
	BF_ASSERT(fs.GetPos() == expectedPos);
	fs.Align(2);
	expectedPos = fs.GetPos();

	// Second Linker Member (ordered symbol map)
	curExpOfs = startExpOfs;
	member.Init();	
	FormatNZ(member.mName, "/");
	FormatNZ(member.mDate, "%d", mTimestamp);
	FormatNZ(member.mMode, "0");
	FormatNZ(member.mSize, "%d", member1Size - (int)sizeof(BlLibMemberHeader));
	fs.WriteT(member);
	fs.Write(ofsIdx);
	for (auto libSym : libSyms)
	{
		if (libSym->mDataSize <= 0)
			continue;
		int exportSize = (int)sizeof(BlLibMemberHeader) + libSym->mDataSize;
		fs.Write(curExpOfs);
		curExpOfs += BF_ALIGN(exportSize, 2);
	}
	fs.Write(numExports);	
	for (auto libSym : libSyms)
	{
		if (libSym->mOfsIdx >= 0)
			fs.Write((int16)(libSym->mOfsIdx + 1));
		else
		{
			BF_ASSERT(libSym->mImp != NULL);
			fs.Write((int16)(libSym->mImp->mOfsIdx + 1));
		}
	}
	for (auto libSym : libSyms)
	{				
		fs.WriteSZ(libSym->mName);		
	}	
	expectedPos += member1Size;	
	BF_ASSERT(fs.GetPos() == expectedPos);
	fs.Align(2);
	expectedPos = fs.GetPos();	

	if (nameMemberSize != 0)
	{
		member.Init();
		FormatNZ(member.mName, "//");
		FormatNZ(member.mDate, "%d", mTimestamp);
		FormatNZ(member.mMode, "0");
		FormatNZ(member.mSize, "%d", nameMemberSize - (int)sizeof(BlLibMemberHeader));
		fs.WriteT(member);
		fs.WriteSZ(dllName);
		fs.Align(2);
	}

	// Create import entries
	curExpOfs = startExpOfs;
	for (auto libSym : libSyms)
	{
		if (libSym->mDataSize <= 0)
			continue;
		BF_ASSERT(fs.GetPos() == curExpOfs);
		int exportSize = (int)sizeof(BlLibMemberHeader) + libSym->mDataSize;
		member.Init();
		if (nameMemberSize != 0)
			FormatNZ(member.mName, "/0"); // Reference name in string table
		else
			FormatNZ(member.mName, "%s/", dllName.c_str());
		FormatNZ(member.mDate, "%d", mTimestamp);
		FormatNZ(member.mMode, "0");
		FormatNZ(member.mSize, "%d", exportSize - (int)sizeof(BlLibMemberHeader));
		fs.WriteT(member);
		if (libSym->mData != NULL)
		{		
			fs.Write(libSym->mData, libSym->mDataSize);
		}
		else
		{
			BF_ASSERT(libSym->mExport != NULL);					
			PEImportObjectHeader importHdr = { 0 };
			importHdr.mSig1 = 0;
			importHdr.mSig2 = IMPORT_OBJECT_HDR_SIG2;
			importHdr.mVersion = 0;
			importHdr.mMachine = PE_MACHINE_X64;
			importHdr.mTimeDateStamp = mTimestamp;
			importHdr.mDataSize = (int)libSym->mExportName.length() + 1 + (int)dllName.length() + 1;
			importHdr.mHint = libSym->mExport->mOrdinal;
			if (!libSym->mExport->mOrdinalSpecified)
				importHdr.mType |= IMPORT_OBJECT_NAME << 2;
			if (libSym->mExport->mIsData)
				importHdr.mType |= IMPORT_OBJECT_DATA;
			fs.WriteT(importHdr);
			fs.WriteSZ(libSym->mExportName);
			fs.WriteSZ(dllName);
		}
		if ((exportSize % 2) == 1)
			fs.Write((uint8)0xA);
		curExpOfs += BF_ALIGN(exportSize, 2);
	}
}

void BlContext::PrintStats()
{
	OutputDebugStrF("Symbols:        %d\n", mSymTable.mMap.size());
	OutputDebugStrF("Obj files:      %d\n", mNumObjFiles);
	OutputDebugStrF("Libs:           %d\n", mNumLibs);
	OutputDebugStrF("Imported objs:  %d\n", mNumImportedObjs);
	OutputDebugStrF("Weak symbols:   %d\n", mNumWeakSymbols);	
	OutputDebugStrF("Syms not found: %d\n", mSymNotFounds.size());
	OutputDebugStrF("Import Groups:  %d\n", mImportGroups.size());	
	
	if (mCodeView != NULL)
	{
		OutputDebugStrF("PDB TPI Types:  %d\n", mCodeView->mTPI.mCurTagId - 0x1000);
		OutputDebugStrF("PDB IPI Types:  %d\n", mCodeView->mIPI.mCurTagId - 0x1000);
		OutputDebugStrF("PDB Globals:    %d\n", mCodeView->mGlobals.mNumEntries);

		OutputDebugStrF("DbgSymSectsFound: %d\n", mDbgSymSectsFound);
		OutputDebugStrF("DbgSymSectsUsed: %d\n", mDbgSymSectsUsed);

		OutputDebugStrF("Stat_ParseTagFuncs: %d\n", mCodeView->mStat_ParseTagFuncs);
		OutputDebugStrF("Stat_TypeMapInserts: %d\n", mCodeView->mStat_TypeMapInserts);
	}
	
}

void BlContext::Link()
{	
	//smap.insert()

	//BeefyDbg64::TestPDB("C:\\temp\\out.pdbf");
	//BeefyDbg64::TestPDB("C:\\proj\\TestCPP\\x64\\Debug\\TestCPP.pdb");
	//BeefyDbg64::TestPDB("C:\\llvm-3.9.0\\bin64\\Debug\\bin\\Clang.pdb");
	//	
	BL_AUTOPERF("BlContext::Link");

	BF_ASSERT(mCodeView == NULL);
		
	/*mTextSeg = CreateSegment(".text", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, 1);	
	mTextSeg->mOutSectionIdx = 0;
	mRDataSeg = CreateSegment(".rdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, 1);
	mRDataSeg->mOutSectionIdx = 1;
	mDataSeg = CreateSegment(".data", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, 1);
	mDataSeg->mOutSectionIdx = 2;
	// We add the BSS data to the end of .data
	//mBSSSeg = CreateSegment(".data$\xFF", IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, 1);
	// We used to name .BSS as ".data$\xFF" to make sure it appended to .data, but now we keep those segments separate so we can
	//  parallelize the COFF generation*/
	mBSSSeg = CreateSegment(".bss", IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, 1);
	//mBSSSeg->mOutSectionIdx = 3;

	//mNumFixedSegs = 4;

	if ((mImageBase & 0xFFFF) != 0)
		Fail(StrFormat("Image base, specified as 0x%p, must be a multiple of 64k (0x10000)", mImageBase));

	// Make sure this is set to 'false' unless debugging
	bool deferThreads = false;

	if (mDebugKind != 0)
	{
		mCodeView = new BlCodeView();
		mCodeView->mContext = this;
		if (!deferThreads)
			mCodeView->StartWorkThreads();
	}

	if (mOutName.empty())
	{
		char cwd[MAX_PATH];
		_getcwd(cwd, MAX_PATH);
		mOutName = String(cwd) + "\\" + GetFileName(cwd);
		if (mIsDLL)
			mOutName += ".dll";
		else
			mOutName += ".exe";
	}

	// Create PDB
	if (mCodeView != NULL)
	{
		if (mPDBPath.empty())
		{
			String pdbFileName = mOutName;
			pdbFileName.RemoveToEnd(pdbFileName.length() - 4);
			pdbFileName += ".pdb";
			mPDBPath = pdbFileName;
		}
		// Use .pdbf for PDB file
		//  The rationale is to allow alternating between the MS linker and the Beef linker.  The MS linker
		//  currently chokes while trying to update PDBs built by the Beef linker
		mPDBPath += "f";

		if (!mCodeView->Create(mPDBPath))
		{
			Fail(StrFormat("Failed to create \"%s\"",  mPDBPath.c_str()));
			return;
		}
	}

	auto imageBaseSym = mSymTable.Add("__ImageBase");
	//TODO: Set up ImageBase properly
	imageBaseSym->mObjectDataIdx = -1;
	imageBaseSym->mKind = BlSymKind_ImageBaseRel;
	imageBaseSym->mSegmentOffset = 0;

	// We do not support /guard:cf (control flow protection) yet.
	// Define CFG symbols anyway so that we can link MSVC 2015 CRT.
	AddAbsoluteSym("__guard_fids_table", 0);
	AddAbsoluteSym("__guard_fids_count", 0);
	AddAbsoluteSym("__guard_flags", 0x100);

	BlSymbol* entrySym = NULL;
	BlSymbol* configSym = NULL;

	int workLoopIdx = 0;

	String entryPointName;
	if (!mEntryPoint.empty())
		entryPointName = mEntryPoint;
	else if (mIsDLL)
		entryPointName = "_DllMainCRTStartup";
	else if (mPeSubsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
		entryPointName = "mainCRTStartup";
	else if (mPeSubsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
		entryPointName = "WinMainCRTStartup";

	while (!mObjectDataWorkQueue.empty())
	{		
		LoadFiles();
		ResolveRelocs();

		for (auto& forcedSym : mForcedSyms)
			ResolveSym(forcedSym.c_str());
		mForcedSyms.clear();

		if (workLoopIdx == 0)
		{
			if (!entryPointName.empty())
				entrySym = ResolveSym(entryPointName.c_str());
			configSym = ResolveSym("_load_config_used");
		}

		if (mObjectDataWorkQueue.empty())
		{
			for (auto& symNotFound : mSymNotFounds)
			{
				if ((symNotFound.mSym->mKind == BlSymKind_LibSymbolRef) || (symNotFound.mSym->mKind == BlSymKind_LibSymbolGroupRef))
					ProcessSymbol(symNotFound.mSym);
			}
		}

		workLoopIdx++;
	}	

	// Check for any unresolved symbols
	for (auto& symNotFound : mSymNotFounds)
	{
		auto sym = symNotFound.mSym;
		if (sym->mKind != BlSymKind_Undefined)
			continue;

		int curOffset = 0;
		int objectDataIdx = -1;

		if (symNotFound.mSegmentIdx != -1)
		{
			auto seg = mSegments[symNotFound.mSegmentIdx];

			// Find the chunk that this symbol is referenced from
			for (auto& chunk : seg->mChunks)
			{
				curOffset += chunk.mAlignPad;
				BF_ASSERT(curOffset == chunk.mOffset);
				if ((symNotFound.mOutOffset >= curOffset) && (symNotFound.mOutOffset < curOffset + chunk.mSize))
				{
					objectDataIdx = chunk.mObjectDataIdx;
					break;
				}

				curOffset += chunk.mSize;
			}
		}

		if (objectDataIdx != -1)
		{
			Fail(StrFormat("Unable to find symbol %s referenced from %s", GetSymDisp(sym->mName).c_str(), ObjectDataIdxToString(objectDataIdx).c_str()));
		}
		else
		{
			Fail(StrFormat("Unable to find symbol %s", GetSymDisp(sym->mName).c_str()));
		}
	}

	if (mFailed)
		return;

	mIDataSeg = CreateIData();
	BlSegment* thunkSeg = CreateThunkData();
	BlSegment* cvInfoSeg = CreateCvInfoData();
	BlSegment* debugDirSeg = NULL;
	if (mCodeView != NULL)
		debugDirSeg = CreateDebugDirData();
	BlSegment* resSeg = CreateResData();	
	BlSegment* exportSeg = CreateExportData();

	PlaceSections();

	PopulateIData(mIDataSeg);
	if (thunkSeg != NULL)
		PopulateThunkData(thunkSeg, mIDataSeg);
	if (cvInfoSeg != NULL)
		PopulateCvInfoData(cvInfoSeg);
	PopulateDebugDirData(debugDirSeg, cvInfoSeg);
	PopulateResData(resSeg);
	if (exportSeg != NULL)
		PopulateExportData(exportSeg);

	if (mFailed)
		return;

	// The .reloc table is a special case because we don't know its size until after we process
	//  the relocations of all the other sections
	auto lastOutSection = mOutSections.back();
	auto relocOutSection = mOutSections.Alloc();
	relocOutSection->mIdx = (int)mOutSections.size() - 1;
	relocOutSection->mName = ".reloc";
	relocOutSection->mCharacteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;
	relocOutSection->mRVA = lastOutSection->mRVA + lastOutSection->mVirtualSize;
	relocOutSection->mRVA = (relocOutSection->mRVA + (0x1000 - 1)) & ~(0x1000 - 1);
	relocOutSection->mRawDataPos = lastOutSection->mRawDataPos + lastOutSection->mRawSize;
	relocOutSection->mRawSize = 0;
	relocOutSection->mVirtualSize = 0;

	// Finish relocs.  Primary reloc loop.
	{
		BL_AUTOPERF("BlContext::Link Finish Relocs");

		for (auto sect : mSegments)
		{
			for (auto& reloc : sect->mRelocs)
			{
				int sectionIdx = -1;
				int sectionOffset = 0;

				int64 outRVA = sect->mRVA + reloc.mOutOffset;
				
				if ((reloc.mFlags & BeRelocFlags_Sym) != 0)
				{
					auto sym = reloc.mSym;

					if (sym->mKind == BlSymKind_WeakSymbol)
					{
						sym = mSymTable.Add(mRemapStrs[sym->mParam].c_str());
						if (sym->mKind == BlSymKind_Undefined)
						{
							Fail(StrFormat("Unable to resolve weak symbol %s", GetSymDisp(sym->mName).c_str()));
							continue;
						}
					}

					reloc.mFlags = (BeRelocFlags)(reloc.mFlags & ~BeRelocFlags_Sym | BeRelocFlags_Loc);
					if (sym->mKind == BlSymKind_Import)
					{
						// It's a thunk
						reloc.mSegmentIdx = thunkSeg->mSegmentIdx;
						auto lookup = mImportLookups[sym->mParam];
						reloc.mSegmentOffset = lookup->mThunkIdx * 6;
						continue;
					}
					if (sym->mKind == BlSymKind_ImportImp)
					{
						BF_ASSERT((reloc.mFlags & BeRelocFlags_Func) == 0);

						reloc.mSegmentIdx = mIDataSeg->mSegmentIdx;
						auto lookup = mImportLookups[sym->mParam];
						reloc.mSegmentOffset = lookup->mIDataIdx * 8;
						continue;
					}
					else if ((sym->mKind == BlSymKind_ImageBaseRel) || (sym->mKind == BlSymKind_Absolute))
					{
						// Fine
					}					
					else if (sym->mKind < 0)
					{
						AssertFailed();
						Fail(StrFormat("Unable to find symbol %s", GetSymDisp(sym->mName).c_str()));
					}

					reloc.mSegmentIdx = sym->mSegmentIdx;
					reloc.mSegmentOffset = sym->mSegmentOffset;

				}
				else
				{
					sectionIdx = reloc.mSegmentIdx;
					sectionOffset = reloc.mSegmentOffset;
				}
			}
		}
	}

	SysFileStream fs;	
	//if (!fs.Open(mOutName, GENERIC_WRITE | GENERIC_READ))	

	if (!fs.Open(mOutName, BfpFileCreateKind_CreateAlways, (BfpFileCreateFlags)(BfpFileCreateFlag_Read | BfpFileCreateFlag_Write)))
	{
		Fail(StrFormat("Unable to create file \"%s\"", mOutName.c_str()));
		return;
	}

	PEHeader hdr = { 0 };
	hdr.e_magic = PE_DOS_SIGNATURE;
	hdr.e_cblp = 0x90;
	hdr.e_cp = 0x03;
	hdr.e_cparhdr = 4;
	hdr.e_maxalloc = 0xFFFF;
	hdr.e_sp = 0xB8;
	hdr.e_lfarlc = 0x40;
	hdr.e_lfanew = 0xF0;

	PE_NTHeaders64 ntHdr = { 0 };
	ntHdr.mSignature = PE_NT_SIGNATURE;
	ntHdr.mFileHeader.mMachine = PE_MACHINE_X64;
	ntHdr.mFileHeader.mNumberOfSections = (int)mOutSections.size();	
	ntHdr.mFileHeader.mTimeDateStamp = mTimestamp;
	ntHdr.mFileHeader.mSizeOfOptionalHeader = 0xF0;	
	ntHdr.mFileHeader.mCharacteristics = IMAGE_FILE_LARGE_ADDRESS_AWARE | IMAGE_FILE_EXECUTABLE_IMAGE;
	if (mIsDLL)
		ntHdr.mFileHeader.mCharacteristics |= IMAGE_FILE_DLL;
	if (mHasFixedBase)
		ntHdr.mFileHeader.mCharacteristics |= IMAGE_FILE_RELOCS_STRIPPED;
	ntHdr.mOptionalHeader.mMagic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	ntHdr.mOptionalHeader.mMajorLinkerVersion = 0xE;
	
	// Export Directory
	if (exportSeg != NULL)
	{
		ntHdr.mOptionalHeader.mDataDirectory[0].mVirtualAddress = exportSeg->mRVA;
		ntHdr.mOptionalHeader.mDataDirectory[0].mSize = exportSeg->mCurSize;
	}

	auto tlsUsedSym = mSymTable.Add("_tls_used");
	if (tlsUsedSym->mKind != BlSymKind_Undefined)
	{
		// TLS Directory
		ntHdr.mOptionalHeader.mDataDirectory[9].mVirtualAddress = (uint32)GetSymAddr(tlsUsedSym);
		ntHdr.mOptionalHeader.mDataDirectory[9].mSize = 0x28;
	}
	
	// Debug Directory
	ntHdr.mOptionalHeader.mDataDirectory[6].mVirtualAddress = debugDirSeg->mRVA;
	ntHdr.mOptionalHeader.mDataDirectory[6].mSize = debugDirSeg->mCurSize;

	// Load Config Directory
	ntHdr.mOptionalHeader.mDataDirectory[10].mVirtualAddress = (uint32)GetSymAddr(configSym);
	ntHdr.mOptionalHeader.mDataDirectory[10].mSize = 0x94;	

	BlOutSection* pdataSection = NULL;

	int imageSize = 0;
	for (auto outSection : mOutSections)
	{
		if ((outSection->mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
			ntHdr.mOptionalHeader.mSizeOfCode += outSection->mVirtualSize;
		if ((outSection->mCharacteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) != 0)
			ntHdr.mOptionalHeader.mSizeOfInitializedData += outSection->mVirtualSize;
		if ((outSection->mCharacteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0)
			ntHdr.mOptionalHeader.mSizeOfUninitializedData += outSection->mVirtualSize;

		int dirIdx = -1;
		if (outSection->mName == ".pdata")
		{
			pdataSection = outSection;			
			dirIdx = 3; // Exception Directory
		}
		else if (outSection->mName == ".rsrc")
		{
			dirIdx = 2; // Resource Directory
		}
		else if (outSection->mName == ".idata")
		{
			dirIdx = 1; // Import Directory
			int importLookupTableSize = ((int)mImportLookups.size() + (int)mImportFiles.size()) * 8;
			ntHdr.mOptionalHeader.mDataDirectory[dirIdx].mVirtualAddress = outSection->mRVA + importLookupTableSize;
			ntHdr.mOptionalHeader.mDataDirectory[dirIdx].mSize = outSection->mVirtualSize;

			dirIdx = 12;	 // Import Address Table
		}

		if (dirIdx != -1)
		{
			ntHdr.mOptionalHeader.mDataDirectory[dirIdx].mVirtualAddress = outSection->mRVA;
			ntHdr.mOptionalHeader.mDataDirectory[dirIdx].mSize = outSection->mVirtualSize;
		}
		//TODO: Set up data directories

		imageSize = (outSection->mRVA + outSection->mVirtualSize + (0x1000 - 1)) & ~(0x1000 - 1);
	}
		
	if (entrySym != NULL)
	{
		auto entrySection = mSegments[entrySym->mSegmentIdx];
		ntHdr.mOptionalHeader.mAddressOfEntryPoint = entrySection->mRVA + entrySym->mSegmentOffset;
	}
	ntHdr.mOptionalHeader.mBaseOfCode = 0x1000;
	ntHdr.mOptionalHeader.mImageBase = mImageBase;
	ntHdr.mOptionalHeader.mSectionAlignment = 0x1000;
	ntHdr.mOptionalHeader.mFileAlignment = 0x200;
	ntHdr.mOptionalHeader.mMajorOperatingSystemVersion = 6;
	ntHdr.mOptionalHeader.mMinorOperatingSystemVersion = 0;
	ntHdr.mOptionalHeader.mMinorImageVersion = 0;
	ntHdr.mOptionalHeader.mMajorImageVersion = 0;
	ntHdr.mOptionalHeader.mMajorSubsystemVersion = 6;
	ntHdr.mOptionalHeader.mMinorSubsystemVersion = 0;
	ntHdr.mOptionalHeader.mSizeOfImage = imageSize;
	ntHdr.mOptionalHeader.mSizeOfHeaders = 0x400;
	if (mIsDLL)
		ntHdr.mOptionalHeader.mSubsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
	else
		ntHdr.mOptionalHeader.mSubsystem = mPeSubsystem;
	ntHdr.mOptionalHeader.mDllCharacteristics = 0;	
	if (mIsTerminalServerAware)
		ntHdr.mOptionalHeader.mDllCharacteristics |= IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE;
	if (mIsNXCompat)
		ntHdr.mOptionalHeader.mDllCharacteristics |= IMAGE_DLLCHARACTERISTICS_NX_COMPAT;
	if (mHasDynamicBase)
		ntHdr.mOptionalHeader.mDllCharacteristics |= IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE;
	if (mHighEntropyVA)
		ntHdr.mOptionalHeader.mDllCharacteristics |= IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA;
	ntHdr.mOptionalHeader.mSizeOfStackReserve = mStackReserve;
	ntHdr.mOptionalHeader.mSizeOfStackCommit = mStackCommit;
	ntHdr.mOptionalHeader.mSizeOfHeapReserve = mHeapReserve;
	ntHdr.mOptionalHeader.mSizeOfHeapCommit = mHeapCommit;
	ntHdr.mOptionalHeader.mNumberOfRvaAndSizes = 0x10;
		
	fs.WriteT(hdr);

	uint8 dosProgram[0xB0] = {
		0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68,
		0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
		0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20,
		0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x87, 0x9B, 0xD3, 0xE0, 0xC3, 0xFA, 0xBD, 0xB3, 0xC3, 0xFA, 0xBD, 0xB3, 0xC3, 0xFA, 0xBD, 0xB3,
		0x77, 0x66, 0x52, 0xB3, 0xC1, 0xFA, 0xBD, 0xB3, 0xF8, 0xA4, 0xBC, 0xB2, 0xC0, 0xFA, 0xBD, 0xB3,
		0xF8, 0xA4, 0xBE, 0xB2, 0xC1, 0xFA, 0xBD, 0xB3, 0xF8, 0xA4, 0xB8, 0xB2, 0xD2, 0xFA, 0xBD, 0xB3,
		0xF8, 0xA4, 0xB9, 0xB2, 0xCF, 0xFA, 0xBD, 0xB3, 0x1E, 0x05, 0x76, 0xB3, 0xC1, 0xFA, 0xBD, 0xB3,
		0xC3, 0xFA, 0xBC, 0xB3, 0xEF, 0xFA, 0xBD, 0xB3, 0x54, 0xA4, 0xB8, 0xB2, 0xC2, 0xFA, 0xBD, 0xB3,
		0x51, 0xA4, 0x42, 0xB3, 0xC2, 0xFA, 0xBD, 0xB3, 0x54, 0xA4, 0xBF, 0xB2, 0xC2, 0xFA, 0xBD, 0xB3,
		0x52, 0x69, 0x63, 0x68, 0xC3, 0xFA, 0xBD, 0xB3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	fs.WriteT(dosProgram);

	int ntHdrFilePos = fs.GetPos();
	fs.WriteT(ntHdr);	
	
	int numSections = (int)mOutSections.size();
	int rawDataPos = (fs.GetPos() + (numSections * sizeof(PESectionHeader))  + (512 - 1)) & ~(512 - 1);	
	int rawDataStart = rawDataPos;

	int32 sectStartPos = fs.GetPos();
	

	// This doesn't include reloc data, it's just an estimate for optimization
	int wantSize = mOutSections.back()->mRawDataPos + mOutSections.back()->mRawSize;
	fs.SetSizeFast(wantSize);

	for (auto outSection : mOutSections)
	{
		PESectionHeader sectHdr = { 0 };

		if (outSection->mName.length() <= 8)
		{
			memcpy(sectHdr.mName, outSection->mName.c_str(), (int)outSection->mName.length());
		}
		else
			NotImpl();
		BF_ASSERT(rawDataPos == outSection->mRawDataPos);
		sectHdr.mVirtualAddress = outSection->mRVA;
		sectHdr.mVirtualSize = outSection->mVirtualSize;
		sectHdr.mSizeOfRawData = outSection->mRawSize;
		sectHdr.mPointerToRawData = rawDataPos;
		sectHdr.mCharacteristics = outSection->mCharacteristics;		
		rawDataPos += outSection->mRawSize;
		fs.WriteT(sectHdr);

		if (mCodeView != NULL)
			mCodeView->mSectionHeaders.WriteT(sectHdr);
	}
	
	fs.Align(512);
	BF_ASSERT(fs.GetPos() == rawDataStart);
	
	if (mCodeView != NULL)
	{
		//BL_AUTOPERF("BlContext::Link CodeViewFinish");
		//mCodeView->Finish();
	}

	int itrIdx = 0;
	
	// Actually write section data
	for (auto outSection : mOutSections)
	{
		BL_AUTOPERF("BlContext::Link Write OutSections");

		int startPos = fs.GetPos();
	
		if (outSection == relocOutSection)
		{
			outSection->mVirtualSize = mPeRelocs.mData.GetSize();
			outSection->mRawSize = (outSection->mVirtualSize + (512 - 1)) & ~(512 - 1);
			mPeRelocs.mData.Read(fs, mPeRelocs.mData.GetSize());

			// Reloc Directory
			ntHdr.mOptionalHeader.mDataDirectory[5].mVirtualAddress = outSection->mRVA;
			ntHdr.mOptionalHeader.mDataDirectory[5].mSize = outSection->mVirtualSize;
			
			
			//sectHdr.mVirtualAddress = ;

			imageSize = (outSection->mRVA + outSection->mVirtualSize + (0x1000 - 1)) & ~(0x1000 - 1);
			ntHdr.mOptionalHeader.mSizeOfImage = imageSize;

			int endPos = fs.GetPos();
			fs.SetPos(ntHdrFilePos);
			fs.WriteT(ntHdr);

			fs.SetPos(sectStartPos + ((int)mOutSections.size() - 1)*sizeof(PESectionHeader));
			PESectionHeader sectHdr = { 0 };
			strcpy(sectHdr.mName, ".reloc");
			sectHdr.mVirtualAddress = outSection->mRVA;
			sectHdr.mVirtualSize = outSection->mVirtualSize;
			sectHdr.mSizeOfRawData = outSection->mRawSize;
			sectHdr.mPointerToRawData = outSection->mRawDataPos;
			sectHdr.mCharacteristics = outSection->mCharacteristics;
			fs.WriteT(sectHdr);

			fs.SetPos(endPos);
		}
		else if (outSection == pdataSection)
		{
			// We need to sort the pdata addresses, but we can sort until after we apply all the relocs which 
			//  doesn't occur until WriteOutSection
			struct _PDataEntry
			{
				uint32 mStartAddr;
				uint32 mEndAddr;
				uint32 mUnwindData;
			};
			std::vector<_PDataEntry> pdataVector;
			pdataVector.resize(outSection->mVirtualSize / 12);

			MemStream memStream((void*)&pdataVector[0], outSection->mVirtualSize, false);
			WriteOutSection(outSection, &memStream);

			std::sort(pdataVector.begin(), pdataVector.end(), [] (const _PDataEntry& lhs, const _PDataEntry& rhs)
			{
				return lhs.mStartAddr < rhs.mStartAddr;
			});

			fs.Write((void*)&pdataVector[0], outSection->mVirtualSize);
		}
		else
		{
			CachedDataStream cachedStream(&fs);
			WriteOutSection(outSection, &cachedStream);
		}
		
		fs.Align(512);
		int actualLen = fs.GetPos() - startPos;
		BF_ASSERT(actualLen == outSection->mRawSize);		
	}

	if (mCodeView != NULL)
	{
		if (deferThreads)
		{
			bool noThreads = false;
			if (noThreads)
			{
				//
				{
					BL_AUTOPERF("Run TypeWorkThread");
					mCodeView->mTypeWorkThread.mTypesDone = true;
					mCodeView->mTypeWorkThread.Run();
				}
				//
				{
					BL_AUTOPERF("Run ModuleWorkThread");
					mCodeView->mModuleWorkThread.mModulesDone = true;
					mCodeView->mModuleWorkThread.Run();
				}
			}
			else
			{
				mCodeView->StartWorkThreads();
			}			
		}

		BL_AUTOPERF("BfContext::Link StopWorkingThread");
		mCodeView->Finish();
		mCodeView->StopWorkThreads();
	}

	if (mIsDLL)
	{
		BL_AUTOPERF("BfContext::Link WriteDLLLib");

		if (mImpLibName.empty())
		{
			String libFileName = mOutName;
			libFileName.RemoveToEnd(libFileName.length() - 4);
			libFileName += ".lib";
			mImpLibName = libFileName;
		}
		WriteDLLLib(mImpLibName);
	}

#ifdef BL_PERFTIME
	PrintStats();
#endif

	/*if (mCodeView != NULL)
		BeefyDbg64::TestPDB(mCodeView->mFileName);*/
}

//////////////////////////////////////////////////////////////////////////

BF_EXPORT BlContext* BF_CALLTYPE BlContext_Create()
{
	return new BlContext();
}

BF_EXPORT void BF_CALLTYPE BlContext_Delete(BlContext* blContext)
{
	BL_AUTOPERF("BlContext_Delete");
	
	delete blContext;
}

BF_EXPORT void BF_CALLTYPE BlContext_Init(BlContext* blContext, bool is64Bit, bool isDebug)
{
	blContext->Init(is64Bit, isDebug);
}

BF_EXPORT void BF_CALLTYPE BlContext_AddSearchPath(BlContext* blContext, const char* directory)
{
	blContext->AddSearchPath(directory);
}

BF_EXPORT bool BF_CALLTYPE BlContext_AddFile(BlContext* blContext, const char* fileName)
{
	return blContext->AddFile(fileName, BlObjectDataKind_SpecifiedObj);
}

BF_EXPORT bool BF_CALLTYPE BlContext_Link(BlContext* blContext)
{
	blContext->Link();
	return !blContext->mFailed;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetOutName(BlContext* blContext, const char* outName)
{
	blContext->mOutName = outName;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetEntryPoint(BlContext* blContext, const char* entryPoint)
{
	blContext->mEntryPoint = entryPoint;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetImpLib(BlContext* blContext, const char* impLibName)
{
	blContext->mImpLibName = impLibName;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetDebug(BlContext* blContext, int debugMode)
{
	blContext->mDebugKind = debugMode;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetPDBName(BlContext* blContext, const char* pdbPath)
{
	blContext->mPDBPath = pdbPath;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetIsDLL(BlContext* blContext)
{
	blContext->mIsDLL = true;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetVerbose(BlContext* blContext, bool enabled)
{
	blContext->mVerbose = true;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetNoDefaultLib(BlContext* blContext, bool enabled)
{
	blContext->mNoDefaultLib = enabled;
}

BF_EXPORT void BF_CALLTYPE BlContext_AddNoDefaultLib(BlContext* blContext, const char* name)
{	
	String libName = name;
	FixLibName(libName);
	String libCanonName = FixPathAndCase(libName);
	blContext->mNoDefaultLibs.insert(libCanonName);
}

BF_EXPORT void BF_CALLTYPE BlContext_AddDefaultLib(BlContext* blContext, const char* name)
{
	blContext->mDefaultLibs.push_back(name);
}

BF_EXPORT void BF_CALLTYPE BlContext_SetImageBase(BlContext* blContext, int64 base)
{
	blContext->mImageBase = base;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetFixedBase(BlContext* blContext, bool enabled)
{
	blContext->mHasFixedBase = enabled;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetDynamicBase(BlContext* blContext, bool enabled)
{
	blContext->mHasDynamicBase = enabled;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetHighEntropyVA(BlContext* blContext, bool enabled)
{
	blContext->mHighEntropyVA = enabled;
}

BF_EXPORT void BF_CALLTYPE BlContext_ManifestUAC(BlContext* blContext, const char* manifestUAC)
{
	blContext->mManifestUAC = manifestUAC;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetSubsystem(BlContext* blContext, int subsystem)
{
	blContext->mPeSubsystem = subsystem;
}

BF_EXPORT void BF_CALLTYPE BlContext_SetStack(BlContext* blContext, int reserve, int commit)
{
	if (reserve > 0)
		blContext->mStackReserve = BF_ALIGN(reserve, 4);
	if (commit > 0)
		blContext->mStackCommit = BF_ALIGN(commit, 4);
}

BF_EXPORT void BF_CALLTYPE BlContext_SetHeap(BlContext* blContext, int reserve, int commit)
{
	if (reserve > 0)
		blContext->mHeapReserve = BF_ALIGN(reserve, 4);
	if (commit > 0)
		blContext->mHeapCommit = BF_ALIGN(commit, 4);
}
