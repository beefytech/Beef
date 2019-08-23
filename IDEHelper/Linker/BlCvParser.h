#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "../Compiler/BfUtil.h"
#include <unordered_map>

NS_BF_BEGIN

struct PESectionHeader;
struct PE_SymInfo;
class BlContext;
class BlObjectData;
class BlCodeView;
class BlCvModuleInfo;
class BlReloc;
struct BlObjectDataSectInfo;
struct BlObjectDataSymInfo;
struct COFFRelocation;

class BlCvParser
{
public:
	BlContext* mContext;
	BlCodeView* mCodeView;

	BlCvModuleInfo* mCurModule;
	const BfSizedVectorRef<BlObjectDataSymInfo>* mSyms;
	BfSizedVector<PESectionHeader*, 1> mTypeSects;
	BfSizedVector<PESectionHeader*, 1> mSymSects;

	std::vector<int16> mChksumOfsToFileIdx;

public:
	void NotImpl();
	void Fail(const StringImpl& err);
	static int64 CvParseConstant(uint16 constVal, uint8*& data);
	static int64 CvParseConstant(uint8*& data);
	static const char* CvParseString(uint8*& data);	
	bool MapAddress(void* symbolData, void* cvLoc, BlReloc& outReloc, COFFRelocation*& nextReloc);	
	bool TryReloc(void* symbolData, void* cvLoc, int32* outVal, COFFRelocation*& nextReloc);		
	void ParseSymbolData(void* symbolData, int size, void* relocData, int numRelocs);

public:
	BlCvParser(BlContext* context);

	void AddModule(BlObjectData* objectData, const char* strTab);
	void AddTypeData(PESectionHeader* sect);
	void AddSymbolData(PESectionHeader* sects);
	void ParseTypeData(void * typeData, int size);
	void AddContribution(int blSectionIdx, int blSectionOfs, int size, int characteristics);
	
	void FinishModule(PESectionHeader* sectHeaderArr, const BfSizedVectorRef<BlObjectDataSectInfo>& sectInfos, PE_SymInfo* symInfo, const BfSizedVectorRef<BlObjectDataSymInfo>& syms);
};

NS_BF_END
