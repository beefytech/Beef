#pragma once

#include "DebugCommon.h"
#include "CPU.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/SLIList.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/MappedFile.h"
#include "BeefySysLib/DataStream.h"
#include "Compiler/BfAst.h"
#include "Compiler/BfUtil.h"
#include "BumpList.h"
#include "RadixMap.h"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include "Debugger.h"
#include "StrHashMap.h"
#include "DbgTypeMap.h"
#include "COFFData.h"
#include "X86Target.h"

NS_BF_DBG_BEGIN

using namespace Beefy;

class DbgModule;

enum DbgTypeCode : uint8
{
	DbgType_Void,
	DbgType_Root,
	DbgType_Null,
	DbgType_i8,
	DbgType_u8,
	DbgType_i16,
	DbgType_u16,
	DbgType_i32,
	DbgType_u32,
	DbgType_i64,
	DbgType_u64,
	DbgType_i128,
	DbgType_u128,
	DbgType_Single,
	DbgType_Double,
	DbgType_Float96,
	DbgType_Float128,
	DbgType_ComplexFloat,
	DbgType_ComplexDouble,
	DbgType_ComplexDouble96,
	DbgType_ComplexDouble128,
	DbgType_SChar,
	DbgType_SChar16,
	DbgType_SChar32,
	DbgType_UChar,
	DbgType_UChar16,
	DbgType_UChar32,
	DbgType_Utf8,
	DbgType_Utf16,
	DbgType_Utf32,
	DbgType_Bool,
	DbgType_Namespace,
	DbgType_Enum,
	DbgType_VTable,
	DbgType_Bitfield,
	DbgType_Class,
	DbgType_Struct,
	DbgType_Union,
	DbgType_TypeDef,

#ifdef BF_DBG_32
	DbgType_IntPtr_Alias = DbgType_i32,
	DbgType_UIntPtr_Alias = DbgType_u32,
#else
	DbgType_IntPtr_Alias = DbgType_i64,
	DbgType_UIntPtr_Alias = DbgType_u64,
#endif
	DbgType_DefinitionEnd = DbgType_TypeDef,

	DbgType_Ptr,
	DbgType_PtrToMember,
	DbgType_SizedArray,
	DbgType_Ref,
	DbgType_RValueReference,
	DbgType_Const,
	DbgType_Volatile,
	DbgType_Unaligned,
	DbgType_Restrict,
	DbgType_Subroutine,
	DbgType_Unspecified,
	DbgType_RawText,

	DbgType_RegGroup,

	DbgType_COUNT,
};

class DebugTarget;
class WinDebugger;
class DbgType;
class DbgBlock;

enum DbgClassType : uint8
{
	DbgClassType_None,
	DbgClassType_CompileUnit,
	DbgClassType_Subprogram,
	DbgClassType_Type,
	DbgClassType_Member,
	DbgClassType_Block,
	DbgClassType_Variable,
};

enum DbgFileExistKind : uint8
{
	DbgFileExistKind_NotChecked,
	DbgFileExistKind_NotFound,
	DbgFileExistKind_HasOldSourceCommand,
	DbgFileExistKind_Found
};

enum DbgModuleKind : uint8
{
	DbgModuleKind_Module,
	DbgModuleKind_HotObject,
	DbgModuleKind_FromLocateSymbol
};

class DbgCompileUnit;

struct DbgSectionData
{
	uint8* mData;
	int mSize;
};

class DbgDebugData
{
public:
	DbgCompileUnit* mCompileUnit;
	int mTagIdx;

public:
	DbgDebugData()
	{
		mCompileUnit = NULL;
	}

#ifdef _DEBUG
	virtual ~DbgDebugData()
	{
	}
#endif
};

enum DbgLocationLenKind
{
	DbgLocationLenKind_SegPlusOffset = -0x80
};

class DbgVariable : public DbgDebugData
{
public:
	static const DbgClassType ClassType = DbgClassType_Variable;

	const char* mName;
	const char* mLinkName;
	addr_target mRangeStart;
	int64 mConstValue;
	DbgType* mType;
	const uint8* mLocationData;
	int mRangeLen;
	int mMemberOffset;
	int8 mLocationLen;
	uint8 mBitSize;
	uint8 mBitOffset;
	bool mIsExtern;
	bool mIsParam;
	bool mIsMember;
	bool mIsStatic;
	bool mIsConst;
	bool mInAutoStaticMap;
	bool mSigNoPointer; // Signature was without pointer, mType has pointer

	addr_target mStaticCachedAddr;
	DbgVariable* mNext;

public:
	DbgVariable()
	{
		mBitSize = 0;
		mBitOffset = 0;
		mIsParam = false;
		mLocationData = NULL;
		mLocationLen = 0;
		mIsStatic = false;
		mIsConst = false;
		mConstValue = 0;
		mMemberOffset = 0;
		mInAutoStaticMap = false;
		mStaticCachedAddr = 0;
	}

	const char* GetMappedName()
	{
		if (mLinkName != NULL)
			return mLinkName;
		return mName;
	}
};

class DbgType;
class DbgDataMap;

typedef std::map<int, Array<int> > DwAsmDebugLineMap;

class DbgBlock : public DbgDebugData
{
public:
	static const DbgClassType ClassType = DbgClassType_Block;

	addr_target mLowPC; // If LowPC is -1 then mHighPC is index into debugRanges
	addr_target mHighPC;

	SLIList<DbgVariable*> mVariables;
	SLIList<DbgBlock*> mSubBlocks;

	DwAsmDebugLineMap* mAsmDebugLineMap; // empty unless inline asm is used

	DbgBlock* mNext;

	bool mAutoStaticVariablesProcessed;

public:
	DbgBlock()
	{
		mLowPC = 0;
		mHighPC = 0;
		mAutoStaticVariablesProcessed = false;
	}

	bool IsEmpty()
	{
		return mLowPC == mHighPC;
	}
};

enum DbgMethodType
{
	DbgMethodType_Ctor,
	DbgMethodType_Normal,
};

class DbgModule;
class DbgSrcFile;
class DbgSrcFileReference;
class DbgSubprogram;

class DbgLineData
{
public:
	uint32 mRelAddress;
	int32 mLine;
	int16 mColumn;
	uint16 mContribSize;
	uint16 mCtxIdx;

	bool IsStackFrameSetup()
	{
		return mColumn != -2;
	}
};

struct DbgLineDataEx
{
public:
	DbgLineData* mLineData;
	DbgSubprogram* mSubprogram;

	DbgLineDataEx()
	{
		mLineData = NULL;
		mSubprogram = NULL;
	}

	DbgLineDataEx(DbgLineData* lineData, DbgSubprogram* subprogram)
	{
		mLineData = lineData;
		mSubprogram = subprogram;
	}

	addr_target GetAddress();
	DbgSrcFile* GetSrcFile();

	bool operator==(DbgLineData* lineData)
	{
		return lineData == mLineData;
	}

	bool IsNull()
	{
		return mLineData == NULL;
	}
};

class DbgLineInfoCtx
{
public:
	DbgSubprogram* mInlinee;
	DbgSrcFile* mSrcFile;
};

class DbgLineInfo
{
public:
	DbgLineInfoCtx* mContexts;
	BfSizedArray<DbgLineData> mLines;
	bool mHasInlinees;
};

class HotReplacedLineInfo
{
public:
	struct Entry
	{
		DbgLineInfo* mLineInfo;
		DbgSubprogram* mSubprogram;
	};

	Array<Entry> mEntries;
};

class DbgInlineeInfo
{
public:
	DbgSubprogram* mInlineParent;
	DbgSubprogram* mRootInliner;
	DbgLineData mFirstLineData;
	DbgLineData mLastLineData;
	uint32 mInlineeId;
	int mInlineDepth;
};

class DbgSubprogram : public DbgDebugData
{
public:
	static const DbgClassType ClassType = DbgClassType_Subprogram;

#ifdef BF_DBG_32
	enum LocalBaseRegKind : uint8
	{
		LocalBaseRegKind_None,
		LocalBaseRegKind_VFRAME,
		LocalBaseRegKind_EBP,
		LocalBaseRegKind_EBX
	};
#else
	enum LocalBaseRegKind : uint8
	{
		LocalBaseRegKind_None,
		LocalBaseRegKind_RSP,
		LocalBaseRegKind_RBP,
		LocalBaseRegKind_R13
	};
#endif

	enum HotReplaceKind : uint8
	{
		HotReplaceKind_None = 0,
		HotReplaceKind_Replaced = 1,
		HotReplaceKind_Orphaned = 2, // Module was hot replaced but a new version of the subprogram wasn't found
		HotReplaceKind_Invalid = 3 // Mangles matched but arguments were incompatible
	};

	const char* mName;
	const char* mLinkName;
	int mTemplateNameIdx;
	int mFrameBaseLen;
	int mPrologueSize;
	const uint8* mFrameBaseData;
	DbgBlock mBlock;
	int mDeferredInternalsSize;
	int mVTableLoc;
	int mStepFilterVersion;
	LocalBaseRegKind mParamBaseReg;
	LocalBaseRegKind mLocalBaseReg;
	bool mHasQualifiedName:1;
	bool mIsStepFiltered:1;
	bool mIsStepFilteredDefault:1;
	bool mVirtual:1;
	bool mHasThis:1;
	bool mNeedLineDataFixup:1;
	bool mIsOptimized:1;
	bool mHasLineAddrGaps:1; // There are gaps of addresses which are not covered by lineinfo
	HotReplaceKind mHotReplaceKind;
	DbgLineInfo* mLineInfo;
	DbgInlineeInfo* mInlineeInfo;
	DbgType* mParentType;
	DbgType* mReturnType;
	DbgMethodType mMethodType;
	BfProtection mProtection;
	BfCheckedKind mCheckedKind;
	SLIList<DbgVariable*> mParams;

	DbgSubprogram* mNext;

public:
	DbgSubprogram()
	{
		mName = NULL;
		mLinkName = NULL;
		mHasThis = false;
		mNeedLineDataFixup = true;
		mHotReplaceKind = HotReplaceKind_None;
		mHasLineAddrGaps = false;
		mPrologueSize = -1;
		mParentType = NULL;
		mInlineeInfo = NULL;
		mFrameBaseData = NULL;
		mFrameBaseLen = 0;
		mReturnType = NULL;
		mNext = NULL;
		mMethodType = DbgMethodType_Normal;
		mProtection = BfProtection_Public;
		mCheckedKind = BfCheckedKind_NotSet;
		mVTableLoc = -1;
		mStepFilterVersion = -1;
	}

	~DbgSubprogram();

	void ToString(StringImpl& str, bool internalName);
	String ToString();
	DbgLineData* FindClosestLine(addr_target addr, DbgSubprogram** inlinedSubprogram = NULL, DbgSrcFile** srcFile = NULL, int* outLineIdx = NULL);
	DbgType* GetParent();
	DbgType* GetTargetType(); // usually mParentType except for closures
	DbgLanguage GetLanguage();
	bool Equals(DbgSubprogram* checkMethod, bool allowThisMismatch = false);
	int GetParamCount();
	String GetParamName(int paramIdx);
	bool IsGenericMethod();
	bool ThisIsSplat();
	bool IsLambda();

	DbgSubprogram* GetRootInlineParent()
	{
		if (mInlineeInfo == NULL)
			return this;
		return mInlineeInfo->mRootInliner;
	}

	int GetInlineDepth()
	{
		int inlineDepth = 0;
		auto checkSubprogram = this;
		while (checkSubprogram->mInlineeInfo != NULL)
		{
			checkSubprogram = checkSubprogram->mInlineeInfo->mInlineParent;
			inlineDepth++;
		}
		if (mInlineeInfo != NULL)
			BF_ASSERT(inlineDepth == mInlineeInfo->mInlineDepth);
		return inlineDepth;
	}

	addr_target GetLineAddr(const DbgLineData& lineData);
	DbgSubprogram* GetLineInlinee(const DbgLineData& lineData);
	DbgSrcFile* GetLineSrcFile(const DbgLineData& lineData);
	bool HasValidLines();
	void PopulateSubprogram();
};

class DbgSubprogramMapEntry
{
public:
	addr_target mAddress;
	DbgSubprogram* mEntry;
	DbgSubprogramMapEntry* mNext;
};

class DbgExceptionDirectoryEntry
{
public:
	DbgExceptionDirectoryEntry* mNext;
	DbgModule* mDbgModule;
	addr_target mAddress;
	int mOrigAddressOffset;
	int mAddressLength;
	int mExceptionPos;
};

class DbgBaseTypeEntry
{
public:
	DbgType* mBaseType;
	DbgBaseTypeEntry* mNext;
	int mThisOffset;
	int mVTableOffset;

public:
	DbgBaseTypeEntry()
	{
		mVTableOffset = -1;
	}
};

enum DbgTypePriority
{
	DbgTypePriority_Normal,
	DbgTypePriority_Unique,
	DbgTypePriority_Primary_Implicit,
	DbgTypePriority_Primary_Explicit
};

struct DbgMethodNameEntry
{
	const char* mName;
	int mCompileUnitId;
	DbgMethodNameEntry* mNext;
};

enum DbgExtType : int8
{
	DbgExtType_Unknown,
	DbgExtType_Normal,
	DbgExtType_BfObject,
	DbgExtType_BfPayloadEnum,
	DbgExtType_BfUnion,
	DbgExtType_Interface
};

class DbgType : public DbgDebugData
{
public:
	static const DbgClassType ClassType = DbgClassType_Type;
	DbgType* mParent;
	BumpList<DbgType*> mAlternates; // From other compile units
	DbgType* mPrimaryType;

	DbgTypeCode mTypeCode;
	SLIList<DbgBaseTypeEntry*> mBaseTypes;
	DbgType* mTypeParam;
	SLIList<DbgVariable*> mMemberList;

	DbgType* mPtrType;

	DbgBlock* mBlockParam;
	SLIList<DbgMethodNameEntry*> mMethodNameList;
	SLIList<DbgSubprogram*> mMethodList;
	SLIList<DbgType*> mSubTypeList;
	BumpList<DbgType*> mUsingNamespaces;
	SLIList<DbgSubprogram*> mHotReplacedMethodList; // Old methods
	DbgType* mHotNewType; // Only non-null during actual hotloading

	const char* mName;
	const char* mTypeName;
	intptr mSize; // In bytes
	int mTemplateNameIdx;
	int mAlign;
	int mTypeIdx;
	uint16 mDefinedMembersSize;
	uint16 mMethodsWithParamsCount;
	bool mIsIncomplete:1; // Not fully loaded
	bool mIsPacked:1;
	bool mNeedsGlobalsPopulated:1;
	bool mHasGlobalsPopulated:1;
	bool mIsDeclaration:1;
	bool mHasStaticMembers:1;
	bool mHasVTable:1;
	bool mFixedName:1;
	DbgLanguage mLanguage;
	DbgExtType mExtType;
	DbgTypePriority mPriority; // Is the one stored in the type map
	bool mSizeCalculated;

	DbgType* mNext;

public:
	DbgType();
	~DbgType();

	//uint64 GetHash();
	DbgType* ResolveTypeDef();
	bool Equals(DbgType* dbgType);

	bool IsRoot();
	bool IsNull();
	bool IsVoid();
	bool IsValuelessType();
	bool IsPrimitiveType();
	bool IsStruct();
	bool IsValueType();
	bool IsTypedPrimitive();
	bool IsBoolean();
	bool IsInteger();
	bool IsIntegral();
	bool IsChar();
	bool IsChar(DbgLanguage language);
	bool IsNamespace();
	bool IsFloat();
	bool IsCompositeType();
	bool WantsRefThis(); // Beef valuetypes want 'this' by ref, Objects and C++ want 'this' by pointer
	bool IsBfObjectPtr();
	bool IsBfObject();
	bool IsBfPayloadEnum();
	bool IsBfEnum();
	bool IsBfTuple();
	bool IsBfUnion();
	bool HasCPPVTable();
	bool IsBaseBfObject();
	bool IsInterface();
	bool IsEnum();
	bool IsSigned();
	bool IsRef();
	bool IsConst();
	bool IsPointer(bool includeBfObjectPointer = true);
	bool HasPointer(bool includeBfObjectPointer = true);
	bool IsPointerOrRef(bool includeBfObjectPointer = true);
	bool IsSizedArray();
	bool IsAnonymous();
	bool IsGlobalsContainer();

	DbgExtType CalcExtType();
	DbgLanguage GetLanguage();
	void FixName();
	void PopulateType();
	DbgModule* GetDbgModule();
	DbgType* GetUnderlyingType();
	DbgType* GetPrimaryType();
	DbgType* GetBaseType();
	DbgType* GetRootBaseType();
	DbgType* RemoveModifiers(bool* hadRef = NULL);
	String ToStringRaw(DbgLanguage language = DbgLanguage_Unknown);
	void ToString(StringImpl& str, DbgLanguage language, bool allowDirectBfObject, bool internalName);
	String ToString(DbgLanguage language = DbgLanguage_Unknown, bool allowDirectBfObject = false);
	intptr GetByteCount();
	intptr GetStride();
	int GetAlign();
	void EnsureMethodsMapped();
};

class DbgBitfieldType : public DbgType
{
public:
	int mPosition;
	int mLength;
};

/*enum DbgDerivedTypeRefKind
{
	DbgDerivedTypeRefKind_Ptr
};

class DbgDerivedTypeRef : public DbgType
{
public:
	DbgType* mRefType;
};*/

class DbgLineDataBuilder
{
public:
	class SubprogramRecord
	{
	public:
		Array<DbgLineInfoCtx, AllocatorBump<DbgLineInfoCtx> > mContexts;
		Array<DbgLineData, AllocatorBump<DbgLineData> > mLines;
		int mCurContext;
		bool mHasInlinees;
	};

	BumpAllocator mAlloc;
	DbgModule* mDbgModule;
	Dictionary<DbgSubprogram*, SubprogramRecord*> mRecords;
	DbgSubprogram* mCurSubprogram;
	SubprogramRecord* mCurRecord;

public:
	DbgLineDataBuilder(DbgModule* dbgModule);
	// The pointer returned is invalid after the next add
	DbgLineData* Add(DbgCompileUnit* compileUnit, DbgLineData& lineData, DbgSrcFile* srcFile, DbgSubprogram* inlinee);
	void Commit();
};

class DbgLineDataState : public DbgLineData
{
public:
	int mOpIndex;
	int mDiscriminator;
	int mIsa;
	bool mBasicBlock;
	bool mIsStmt;
};

class DbgSrcFileReference
{
public:
	DbgCompileUnit* mCompileUnit;
	DbgSrcFile* mSrcFile;
};

class DbgDeferredSrcFileReference
{
public:
	DbgModule* mDbgModule;
	int mCompileUnitId;
};

typedef Array<DbgLineData*> LineDataVector;

class DbgLineDataList
{
public:
	LineDataVector mLineData;
};

enum DbgHashKind
{
	DbgHashKind_None,
	DbgHashKind_MD5,
	DbgHashKind_SHA256
};

class DbgSrcFile
{
public:
	String mFilePath;
	String mLocalPath;
	bool mHadLineData;
	bool mHasLineDataFromMultipleModules;
	bool mVerifiedPath;
	DbgFileExistKind mFileExistKind;
	int mStepFilterVersion;
	DbgHashKind mHashKind;
	uint8 mHash[32];
	Array<DbgDeferredSrcFileReference> mDeferredRefs;
	Array<DbgSubprogram*> mLineDataRefs;
	Array<HotReplacedLineInfo*> mHotReplacedDbgLineInfo; // Indexing starts at -1

public:
	DbgSrcFile()
	{
		mHasLineDataFromMultipleModules = false;
		mHadLineData = false;
		mVerifiedPath = false;
		mHashKind = DbgHashKind_None;
		mFileExistKind = DbgFileExistKind_NotChecked;
		mStepFilterVersion = 0;

		//mLineData.Reserve(64);
	}

	bool IsBeef();

	~DbgSrcFile();

	void RemoveDeferredRefs(DbgModule* debugModule);
	void RemoveLines(DbgModule* debugModule);
	void RemoveLines(DbgModule* debugModule, DbgSubprogram* dbgSubprogram, bool isHotReplaced);
	void RehupLineData();
	void VerifyPath();
	const String& GetLocalPath();
	void GetHash(String& hashStr);
};

class DwCommonFrameDescriptor
{
public:
	DbgModule* mDbgModule;
	const char* mAugmentation;
	int mAugmentationLength;
	int mPointerSize;
	int mSegmentSize;
	int mCodeAlignmentFactor;
	int mDataAlignmentFactor;
	int mReturnAddressColumn;
	const uint8* mInstData;
	int mInstLen;
	int mAddressPointerEncoding;
	addr_target mLSDARoutine;
	int mLSDAPointerEncodingFDE;

public:
	DwCommonFrameDescriptor()
	{
		mPointerSize = -1;
		mSegmentSize = -1;
		mDbgModule = NULL;
		mAugmentationLength = 0;
		mAddressPointerEncoding = 0;
		mLSDARoutine = 0;
		mLSDAPointerEncodingFDE = 0;
	}
};

class DwFrameDescriptor
{
public:
	addr_target mLowPC;
	addr_target mHighPC;
	const uint8* mInstData;
	int mInstLen;
	DwCommonFrameDescriptor* mCommonFrameDescriptor;
	int mAddressPointerEncoding;
	addr_target mLSDARoutine;

public:
	DwFrameDescriptor()
	{
		mAddressPointerEncoding = 0;
		mLSDARoutine = 0;
	}
};

class DbgModule;

class DbgCompileUnitContrib
{
public:
	DbgCompileUnitContrib* mNext;
	DbgModule* mDbgModule;
	addr_target mAddress;
	int mCompileUnitId;
	int mLength;
};

class DbgCompileUnit
{
public:
	static const DbgClassType ClassType = DbgClassType_CompileUnit;

	DbgModule* mDbgModule;
	DbgLanguage mLanguage;
	DbgBlock* mGlobalBlock;
	DbgType* mGlobalType;
	Array<DbgSrcFileReference> mSrcFileRefs;
	String mName;
	String mProducer;
	String mCompileDir;
	addr_target mLowPC;
	addr_target mHighPC;
	bool mNeedsLineDataFixup;
	bool mWasHotReplaced;
	bool mIsMaster;

	SLIList<DbgSubprogram*> mOrphanMethods;

public:
	DbgCompileUnit(DbgModule* dbgModule);

	virtual ~DbgCompileUnit()
	{
	}
};

//static int gDbgSymbol_Idx = 0;
class DbgSymbol
{
public:
	//int mDbgIdx;
	DbgSymbol()
	{
		//mDbgIdx = ++gDbgSymbol_Idx;
	}

public:
	const char* mName;
	addr_target mAddress;
	DbgModule* mDbgModule;
	DbgSymbol* mNext;
};

class DbgSection
{
public:
	addr_target mAddrStart;
	addr_target mAddrLength;
	bool mWritingEnabled;
	bool mIsExecutable;
	int mOldProt;

public:
	DbgSection()
	{
		mAddrStart = 0;
		mAddrLength = 0;
		mWritingEnabled = false;
		mIsExecutable = false;
		mOldProt = 0;
	}
};

struct DbgCharPtrHash
{
	size_t operator()(const char* val) const
	{
		int curHash = 0;
		const char* curHashPtr = val;
		while (*curHashPtr != 0)
		{
			curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
			curHashPtr++;
		}
		return curHash;
	}
};

struct DbgCharPtrEquals
{
	bool operator()(const char* lhs, const char* rhs) const
	{
		return strcmp(lhs, rhs) == 0;
	}
};

struct DbgFileNameHash
{
	size_t operator()(const char* val) const
	{
		int curHash = 0;
		const char* curHashPtr = val;
		while (*curHashPtr != 0)
		{
			char c = *curHashPtr;
#ifdef _WIN32
			c = toupper((uint8)c);
			if (c == '\\')
				c = '/';
#endif
			curHash = ((curHash ^ c) << 5) - curHash;
			curHashPtr++;
		}
		return curHash;
	}
};

struct DbgFileNameEquals
{
	bool operator()(const char* lhs, const char* rhs) const
	{
#ifdef _WIN32
		while (true)
		{
			char lc = toupper((uint8)*(lhs++));
			if (lc == '\\')
				lc = '/';
			char rc = toupper((uint8)*(rhs++));
			if (rc == '\\')
				rc = '/';
			if (lc != rc)
				return false;
			if (lc == 0)
				return true;
		}
#else
		return strcmp(lhs, rhs) == 0;
#endif
	}
};

struct DbgAutoStaticEntry
{
	String mFullName;
	DbgVariable* mVariable;
	uint64 mAddrStart;
	uint64 mAddrLen;
};

class DbgHotTargetSection
{
public:
	uint8* mData;
	int mDataSize;
	addr_target mTargetSectionAddr;
	int mImageOffset;
	int mTargetSectionSize;
	int mPointerToRelocations;
	int mNumberOfRelocations;
	bool mNoTargetAlloc;
	bool mCanExecute;
	bool mCanWrite;

public:
	DbgHotTargetSection()
	{
		mData = NULL;
		mDataSize = 0;
		mImageOffset = 0;
		mTargetSectionAddr = 0;
		mTargetSectionSize = 0;
		mPointerToRelocations = 0;
		mNumberOfRelocations = 0;
		mCanExecute = false;
		mCanWrite = false;
		mNoTargetAlloc = false;
	}

	~DbgHotTargetSection()
	{
	}
};

typedef Dictionary<String, DbgType*> DbgTypeMapType;
typedef Dictionary<String, std::pair<uint64, uint64> > DbgAutoValueMapType;
typedef Dictionary<addr_target, int> DbgAutoStaticEntryBucketMap;

struct DbgDeferredHotResolve
{
public:
	DbgHotTargetSection* mHotTargetSection;
	String mName;
	addr_target mNewAddr;
	COFFRelocation mReloc;
};

class DbgDataMap
{
public:
	// All entries we want to put in this map are at least 4 bytes apart
	static const int IDX_DIV = 4;
	DbgDebugData** mAddrs;
	int mOffset;
	int mSize;

	DbgDataMap(int startIdx, int endIdx)
	{
		mOffset = startIdx;
		mSize = (((endIdx - startIdx) + IDX_DIV - 1) / IDX_DIV) + 4; // Add a few extra at the end
		mAddrs = new DbgDebugData*[mSize];
		memset(mAddrs, 0, sizeof(DbgDebugData*)*mSize);
	}

	~DbgDataMap()
	{
		delete mAddrs;
	}

	void Set(int tagIdx, DbgDebugData* debugData)
	{
		BF_ASSERT(debugData->mTagIdx == 0);
		debugData->mTagIdx = tagIdx;

		int mapIdx = (tagIdx - mOffset) / IDX_DIV;
		BF_ASSERT(mapIdx < mSize);

		// Search for empty slot
		while (true)
		{
			BF_ASSERT(mapIdx < mSize);
			if (mAddrs[mapIdx] == NULL)
			{
				mAddrs[mapIdx] = debugData;
				break;
			}
			mapIdx++;
		}
	}

	template <typename T>
	T Get(int tagIdx)
	{
		int mapIdx = (tagIdx - mOffset) / IDX_DIV;

		// Search for right slot
		while (mapIdx < mSize)
		{
			DbgDebugData* checkData = mAddrs[mapIdx];
			if (checkData == NULL)
				return NULL;
			if (checkData->mTagIdx == tagIdx)
				return (T)checkData;
			mapIdx++;
		}
		return NULL;
	}
};

class WdStackFrame;

enum DbgModuleLoadState
{
	DbgModuleLoadState_NotLoaded,
	DbgModuleLoadState_Failed,
	DbgModuleLoadState_Loaded
};

enum DbgEvalLocFlags
{
	DbgEvalLocFlag_None = 0,
	DbgEvalLocFlag_DisallowReg = 1,
	DbgEvalLocFlag_IsParam = 2
};

struct DbgSizedArrayEntry
{
	DbgType* mElementType;
	int mCount;

	bool operator==(const DbgSizedArrayEntry& other) const
	{
		return (other.mElementType == mElementType) &&
			(other.mCount == mCount);
	}
};

class DbgModule
{
public:
	static const int ImageBlockSize = 4096;

	WinDebugger* mDebugger;
	DebugTarget* mDebugTarget;
	DbgModuleLoadState mLoadState;
	MappedFile* mMappedImageFile;
	MemReporter* mMemReporter;

	const uint8* mDebugLineData;
	const uint8* mDebugInfoData;
	const uint8* mDebugPubNames;
	const uint8* mDebugFrameData;
	const uint8* mDebugLocationData;
	const uint8* mDebugRangesData;
	addr_target mDebugFrameAddress;
	addr_target mCodeAddress;
	const uint8* mDebugAbbrevData;
	const uint8* mDebugStrData;
	const uint8** mDebugAbbrevPtrData;
	Array<DbgSectionData> mExceptionDirectory;
	const uint8* mEHFrameData;
	const char* mStringTable;
	const uint8* mSymbolData;
	addr_target mEHFrameAddress;
	addr_target mTLSAddr;
	addr_target mTLSExtraAddr;
	int mTLSSize;
	int mTLSExtraSize;
	addr_target mTLSIndexAddr;
	DbgFlavor mDbgFlavor;

	bool mParsedGlobalsData;
	bool mParsedSymbolData;
	bool mParsedTypeData;
	bool mPopulatedStaticVariables;
	bool mParsedFrameDescriptors;

	bool mMayBeOld; // If we had to load debug info from the SymCache or a SymServer then it may be old
	bool mDeleting;
	bool mFailed;
	int mId;
	int mHotIdx;
	String mFilePath;
	String mDisplayName;
	uint32 mTimeStamp;
	uint32 mExpectedFileSize;
	int mStartSubprogramIdx;
	int mEndSubprogramIdx;
	int mStartTypeIdx;
	int mEndTypeIdx;
	uintptr mPreferredImageBase;
	uintptr mImageBase;
	uint32 mImageSize;
	uintptr mEntryPoint;
	String mVersion;
	String* mFailMsgPtr;

	DbgType* mBfTypeType;
	intptr mBfTypesInfoAddr;

	DbgModuleMemoryCache* mOrigImageData;
	DbgCompileUnit* mMasterCompileUnit;
	StrHashMap<DbgVariable*> mGlobalVarMap; // Dedups entries into mMasterCompileUnit

	BumpAllocator mAlloc;

	std::list<DwAsmDebugLineMap> mAsmDebugLineMaps;
	Array<DbgSection> mSections;
	Dictionary<addr_target, int> mSecRelEncodingMap;
	Array<addr_target> mSecRelEncodingVec;
	bool mCheckedBfObject;
	bool mBfObjectHasFlags;
	DbgModuleKind mModuleKind;
	bool mIsDwarf64;

	HashSet<DbgSrcFile*> mSrcFileDeferredRefs;
	Array<addr_target> mSectionRVAs;
	SLIList<DbgSymbol*> mDeferredSymbols;
	Beefy::OwnedVector<DbgDeferredHotResolve> mDeferredHotResolveList;
	Array<DbgHotTargetSection*> mHotTargetSections;
	HashSet<DbgType*> mHotPrimaryTypes; // List of types where we have entries in mHotReplacedMethodList
	DbgCompileUnit mDefaultCompileUnit;
	Dictionary<DbgType*, DbgType*> mConstTypes;
	Dictionary<String, DbgFileExistKind> mFileExistsCache;
	Dictionary<DbgSizedArrayEntry, DbgType*> mSizedArrayTypes;

	int mAllocSizeData;
	Array<const uint8*> mOwnedSectionData;

public:
	Array<DbgSrcFile*> mEmptySrcFiles;
	Array<DbgCompileUnit*> mCompileUnits;
	Array<DbgVariable*> mStaticVariables;

	DbgType* mCPrimitiveTypes[DbgType_COUNT];
	DbgType* mBfPrimitiveTypes[DbgType_COUNT];
	const char* mPrimitiveStructNames[DbgType_COUNT];
	DbgTypeMap mTypeMap;

	Array<DbgType*> mTypes;
	Array<DbgSubprogram*> mSubprograms;

	StrHashMap<DbgSymbol*> mSymbolNameMap;
	std::unordered_map<const char*, DbgVariable*, DbgCharPtrHash, DbgCharPtrEquals> mStaticVariableMap;

public:
	virtual void ParseGlobalsData();
	virtual void ParseSymbolData();
	virtual void ParseTypeData();
	virtual DbgCompileUnit* ParseCompileUnit(int compileUnitId);
	virtual void ParseCompileUnits() {}
	virtual void MapCompileUnitMethods(DbgCompileUnit* compileUnit);
	virtual void MapCompileUnitMethods(int compileUnitId);
	virtual void PopulateType(DbgType* dbgType);
	virtual void PopulateTypeGlobals(DbgType* dbgType);
	virtual void PopulateSubprogram(DbgSubprogram* dbgSubprogram) { }
	virtual void FixupInlinee(DbgSubprogram* dbgSubprogram) {}
	virtual void PopulateStaticVariableMap();
	virtual void ProcessDebugInfo();
	virtual bool CanGetOldSource() { return false; }
	virtual String GetOldSourceCommand(const StringImpl& path) { return ""; }
	virtual bool GetEmitSource(const StringImpl& filePath, String& outText) { return false; }
	virtual bool DbgIsStrMutable(const char* str) { return true; } // Always assume its a copy
	virtual addr_target LocateSymbol(const StringImpl& name) { return 0; }
	virtual DbgSubprogram* FindSubprogram(DbgType* dbgType, const char* methodName);
	const char* GetStringTable(DataStream* stream, int stringTablePos);

	virtual void Fail(const StringImpl& error);
	virtual void SoftFail(const StringImpl& error);
	virtual void HardFail(const StringImpl& error);
	void FindTemplateStr(const char*& name, int& templateNameIdx);
	void TempRemoveTemplateStr(const char*& name, int& templateNameIdx);
	void ReplaceTemplateStr(const char*& name, int& templateNameIdx);

	char* DbgDupString(const char* str, const char* allocName = NULL);
	DbgModule* GetLinkedModule();
	addr_target GetTargetImageBase();
	addr_target RemapAddr(addr_target addr);
	template <typename T>
	T GetOrCreate(int idx, DbgDataMap& dataMap);
	DbgType* GetOrCreateType(int typeIdx, DbgDataMap& typeMap);
	//void SplitName(const char* inName, const char*& outBaseName, const char*& outTemplateParams, bool alwaysDup = false);
	void MapSubprogram(DbgSubprogram* dbgSubprogram);
	bool ParseDWARF(const uint8*& dataPtr);
	void ParseAbbrevData(const uint8* data);
	void ParseExceptionData();
	void ParseDebugFrameData();
	void ParseEHFrameData();
	void FlushLineData(DbgSubprogram* curSubprogram, std::list<DbgLineData>& queuedLineData);
	DbgSrcFile* AddSrcFile(DbgCompileUnit* compileUnit, const String& srcFilePath);
	void AddLineData(DbgCompileUnit* dwCompileUnit, DbgLineData& lineData, DbgSubprogram*& curSubProgram, std::list<DbgLineData>& queuedLineData);
	bool ParseDebugLineInfo(const uint8*& data, int compileUnitIdx);
	void FixupInnerTypes(int startingTypeIdx);
	void MapTypes(int startingTypeIdx);
	void CreateNamespaces();
	bool IsObjectFile() { return mModuleKind != DbgModuleKind_Module; }
	bool IsHotSwapObjectFile() { return mModuleKind == DbgModuleKind_HotObject; }
	bool IsHotSwapPreserve(const String& name);
	addr_target GetHotTargetAddress(DbgHotTargetSection* hotTargetSection);
	uint8* GetHotTargetData(addr_target address);
	void DoReloc(DbgHotTargetSection* hotTargetSection, COFFRelocation& coffReloc, addr_target resolveSymbolAddr, PE_SymInfo* symInfo);
	void ParseHotTargetSections(DataStream* stream, addr_target* resovledSymbolAddrs);
	void CommitHotTargetSections();
	void HotReplaceType(DbgType* newType);
	void ProcessHotSwapVariables();
	virtual bool LoadPDB(const String& pdbPath, uint8 wantGuid[16], int32 wantAge) { return false; }
	virtual bool CheckSection(const char* name, uint8* sectionData, int sectionSize) { return false; }
	virtual void PreCacheImage() {}
	virtual void PreCacheDebugInfo() {}
	virtual bool RequestImage() { return false; }
	virtual bool HasPendingDebugInfo() { return false; }
	virtual bool RequestDebugInfo(bool allowRemote = true) { return false; }
	virtual bool WantsAutoLoadDebugInfo() { return false; }
	virtual DbgFileExistKind CheckSourceFileExist(const StringImpl& path);
	virtual void ParseFrameDescriptors() {}

	template <typename T>
	T ReadValue(const uint8*& data, int form, int refOffset = 0, const uint8** extraData = NULL, const uint8* startData = NULL);

	void EnableWriting(addr_target address);
	void RevertWritingEnable();

public:
	DbgModule(DebugTarget* debugTarget);
	virtual ~DbgModule();

	static bool CanRead(DataStream* stream, DebuggerResult* outResult);

	bool ReadCOFF(DataStream* stream, DbgModuleKind dbgModuleKind);
	void RemoveTargetData();
	virtual void ReportMemory(MemReporter* memReporter);

	int64 GetImageSize();
	virtual void FinishHotSwap();
	addr_target ExecuteOps(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, CPURegisters* registers, DbgAddrType* outAddrType, DbgEvalLocFlags flags, addr_target* pushValue = NULL);
	virtual intptr EvaluateLocation(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, DbgAddrType* outAddrType, DbgEvalLocFlags flags = DbgEvalLocFlag_None);

	//const uint8* CopyOrigImageData(addr_target address, int length);

	DbgType* FindTypeHelper(const String& typeName, DbgType* checkType);
	DbgType* FindType(const String& typeName, DbgType* contextType = NULL, DbgLanguage language = DbgLanguage_Unknown, bool bfObjectPtr = false);
	DbgTypeMap::Entry* FindType(const char* typeName, DbgLanguage language);

	DbgType* GetPointerType(DbgType* innerType);
	DbgType* GetConstType(DbgType* innerType);
	DbgType* GetPrimaryType(DbgType* dbgType);
	DbgType* GetPrimitiveType(DbgTypeCode typeCode, DbgLanguage language);
	DbgType* GetPrimitiveStructType(DbgTypeCode typeCode);
	DbgType* GetInnerTypeOrVoid(DbgType* dbgType);
	DbgType* GetSizedArrayType(DbgType* elementType, int count);
};

NS_BF_END

namespace std
{
	template<>
	struct hash<NS_BF_DBG::DbgSizedArrayEntry>
	{
		size_t operator()(const NS_BF_DBG::DbgSizedArrayEntry& val) const
		{
			return (size_t)val.mElementType ^ (val.mCount << 10);
		}
	};
}