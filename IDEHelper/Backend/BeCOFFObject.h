#pragma once

#include "BeMCContext.h"
#include "BeefySysLib/FileStream.h"
#include "../Compiler/BfCodeGen.h"
#include "BeefySysLib/MemStream.h"

NS_BF_BEGIN

enum BeMCSymbolKind
{
	BeMCSymbolKind_AuxPlaceholder,
	BeMCSymbolKind_SectionDef,
	BeMCSymbolKind_SectionCOFFDef,
	BeMCSymbolKind_SectionRef,
	BeMCSymbolKind_External,
	BeMCSymbolKind_Function,
	BeMCSymbolKind_COMDAT
};

class BeMCSymbol
{
public:
	String mName;
	BeType* mType;
	bool mIsStatic;
	bool mIsTLS;
	BeMCSymbolKind mSymKind;
	int mValue;	
	int mIdx;
	int mSectionNum;

	BeFunction* mBeFunction;

public:
	BeMCSymbol()
	{
		mType = NULL;
		mIsStatic = false;
		mIsTLS = false;
		mSymKind = BeMCSymbolKind_External;
		mValue = 0;
		mIdx = -1;
		mSectionNum = 0;

		mBeFunction = NULL;
	}
};

enum BeMCRelocationKind
{
	BeMCRelocationKind_NONE,
	BeMCRelocationKind_ADDR32NB,
	BeMCRelocationKind_ADDR64,
	BeMCRelocationKind_REL32,
	BeMCRelocationKind_SECREL,
	BeMCRelocationKind_SECTION,
};

class BeMCRelocation
{
public:
	BeMCRelocationKind mKind;
	int mOffset;
	int mSymTableIdx;
};

class BeCOFFSection
{
public:
	DynMemStream mData;
	Array<BeMCRelocation> mRelocs;
	int mSymbolIdx;
	int mSectionIdx;
	String mSectName;
	int mCharacteristics;
	int mSizeOverride;
	int mAlign;

public:
	BeCOFFSection()
	{
		mSymbolIdx = -1;
		mSectionIdx = -1;
		mCharacteristics = 0;
		mSizeOverride = 0;
		mAlign = 0;
	}
};

struct BeInlineLineBuilder
{
public:
	Array<uint8> mData;
	int mCurLine;
	int mCurCodePos;
	BeDbgLoc* mStartDbgLoc;
	bool mEnded;
	Array<BeDbgVariable*> mVariables;

public:
	BeInlineLineBuilder();

	void Compress(int val);
	void WriteSigned(int val);
	void Update(BeDbgCodeEmission* codeEmission);
	void Start(BeDbgCodeEmission* codeEmission);
	void End(BeDbgCodeEmission* codeEmission);
};

struct COFFArgListRef
{
public:
	BeDbgFunction* mFunc;

	COFFArgListRef()
	{
		mFunc = NULL;
	}

	COFFArgListRef(BeDbgFunction* func)
	{
		mFunc = func;
	}

	bool operator==(const COFFArgListRef& rhsRef) const
	{
		auto lhs = mFunc;
		auto rhs = rhsRef.mFunc;

		int lhsParamOfs = lhs->HasThis() ? 1 : 0;
		int rhsParamOfs = rhs->HasThis() ? 1 : 0;
		if (lhs->mType->mParams.size() - lhsParamOfs != rhs->mType->mParams.size() - rhsParamOfs)
			return false;
		for (int i = 0; i < (int)lhs->mType->mParams.size() - lhsParamOfs; i++)
			if (lhs->mType->mParams[i + lhsParamOfs] != rhs->mType->mParams[i + rhsParamOfs])
				return false;
		return true;
	}
};

struct COFFFuncTypeRef
{
public:
	BeDbgFunction* mFunc;

	COFFFuncTypeRef()
	{
		mFunc = NULL;
	}

	COFFFuncTypeRef(BeDbgFunction* func)
	{
		mFunc = func;
	}

	bool operator==(const COFFFuncTypeRef& rhsRef) const
	{
		auto lhs = mFunc;
		auto rhs = rhsRef.mFunc;

		if (lhs->mIsStaticMethod != rhs->mIsStaticMethod)
			return false;
		if (lhs->mScope != rhs->mScope)
			return false;
		if (lhs->mType->mReturnType != rhs->mType->mReturnType)
			return false;
		if (lhs->mType->mParams.size() != rhs->mType->mParams.size())
			return false;
		for (int i = 0; i < (int)lhs->mType->mParams.size(); i++)
			if (lhs->mType->mParams[i] != rhs->mType->mParams[i])
				return false;		
		return true;
	}
};

NS_BF_END;

namespace std
{
	template <>
	struct hash<Beefy::COFFArgListRef>
	{
		size_t operator()(const Beefy::COFFArgListRef& val)
		{
			auto func = val.mFunc;
			intptr curHash = 0;
			for (int paramIdx = func->HasThis() ? 1 : 0; paramIdx < (int)func->mType->mParams.size(); paramIdx++)
				curHash = Beefy::Hash64((intptr)func->mType->mParams[paramIdx], curHash);
			return curHash;
		}
	};

	template <>
	struct hash<Beefy::COFFFuncTypeRef>
	{
		size_t operator()(const Beefy::COFFFuncTypeRef& val)
		{
			auto func = val.mFunc;
			intptr curHash = (intptr)func->mType->mReturnType;
			for (int paramIdx = 0; paramIdx < (int)func->mType->mParams.size(); paramIdx++)
				curHash = Beefy::Hash64((intptr)func->mType->mParams[paramIdx], curHash);
			return curHash;
		}
	};
}

NS_BF_BEGIN;

class BeCOFFObject
{
public:
	bool mWriteToLib;

	PerfManager* mPerfManager;
	DataStream* mStream;
	BumpAllocator mAlloc;
	BeModule* mBeModule;	
	OwnedVector<BeMCSymbol> mSymbols;	
	OwnedVector<BeCOFFSection> mDynSects;
	uint32 mTimestamp;

	BeCOFFSection mTextSect;
	BeCOFFSection mDataSect;
	BeCOFFSection mRDataSect;	
	BeCOFFSection mBSSSect;	
	BeCOFFSection mTLSSect;	
	BeCOFFSection mPDataSect;
	BeCOFFSection mXDataSect;
	BeCOFFSection mDebugSSect;
	BeCOFFSection mDebugTSect;
	BeCOFFSection mDirectiveSect;
	DynMemStream mStrTable;
	int mBSSPos;
	Array<BeCOFFSection*> mUsedSections;
	Dictionary<BeValue*, BeMCSymbol*> mSymbolMap;
	Dictionary<String, BeMCSymbol*> mNamedSymbolMap;	
	HashSet<COFFArgListRef> mArgListSet;	
	HashSet<COFFFuncTypeRef> mFuncTypeSet;
	Deque<BeFunction*> mFuncWorkList;
	int mTTagStartPos;
	int mSTagStartPos;
	int mCurTagId;
	int mSectionStartPos;
	int mCurStringId;
	int mCurJumpTableIdx;
	bool mTypesLocked;		
	String mDirectives;

public:	
	void ToString(BeMDNode* mdNode, String& str);
	int GetCVRegNum(X64CPURegister reg, int bits);

	void DbgTAlign();
	void DbgTStartTag();
	void DbgTEndTag();
	void DbgEncodeConstant(DynMemStream& memStream, int64 val);
	void DbgEncodeString(DynMemStream& memStream, const StringImpl& str);	
	void DbgMakeFuncType(BeDbgFunction* dbgFunc);
	void DbgMakeFunc(BeDbgFunction* dbgFunc);
	int DbgGetTypeId(BeDbgType* dbgType, bool doDefine = false);	
	void DbgGenerateTypeInfo();

	void DbgSAlign();
	void DbgSStartTag();
	void DbgSEndTag();
	void DbgOutputLocalVar(BeDbgFunction* dbgFunc, BeDbgVariable* dbgVar);
	void DbgOutputLocalVars(BeInlineLineBuilder* curInlineBuilder, BeDbgFunction* func);
	void DbgStartSection(int sectionNum);
	void DbgEndSection();
	void DbgStartVarDefRange(BeDbgFunction* dbgFunc, BeDbgVariable* dbgVar, const BeDbgVariableLoc& varLoc, int offset, int range);
	void DbgEndLineBlock(BeDbgFunction* dbgFunc, const Array<BeDbgCodeEmission>& emissions, int blockStartPos, int emissionStartIdx, int lineCount);
	void DbgGenerateModuleInfo();	
	void InitSect(BeCOFFSection& sect, const StringImpl& name, int characteristics, bool addNow, bool makeSectSymbol);
	void WriteConst(BeCOFFSection& sect, BeConstant* constVal);
	
	void Generate(BeModule* module);

public:
	BeCOFFObject();		
	void Finish();

	bool Generate(BeModule* module, const StringImpl& fileName);	
	BeMCSymbol* GetSymbol(BeValue* value, bool allowCreate = true);
	BeMCSymbol* GetSymbolRef(const StringImpl& name);
	void MarkSectionUsed(BeCOFFSection& sect, bool getSectSymbol = false);
	BeMCSymbol* GetCOMDAT(const StringImpl& name, void* data, int size, int align);
	BeCOFFSection* CreateSect(const StringImpl& name, int characteristics, bool makeSectSymbol = true);
};

NS_BF_END
