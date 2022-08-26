#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/Deque.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/MultiHashSet.h"
#include "../Beef/BfCommon.h"
#include "BfAst.h"
#include "BfUtil.h"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "MemReporter.h"

namespace llvm
{
	class Type;
	class Function;
}

#define BF_NEW_INT_TYPES

#ifdef BF_NEW_INT_TYPES
#define BF_INT32_NAME "int32"
#else
#define BF_INT32_NAME "int"
#endif

#ifdef BF_PLATFORM_WINDOWS
#define BF_OBJ_EXT ".obj"
#else
#define BF_OBJ_EXT ".o"
#endif

NS_BF_BEGIN

class BfSystem;
class BfCompiler;
class BfTypeReference;
class BfProject;
class BfTypeDef;

struct BfTypeDefMapFuncs;
typedef HashSet<BfProject*> BfProjectSet;

class BfAtom
{
public:
	StringView mString;
	int mRefCount;
	int mPendingDerefCount;
	int mHash;
	uint32 mAtomUpdateIdx;
	bool mIsSystemType;
	Dictionary<BfAtom*, int> mPrevNamesMap;

public:
	~BfAtom();
	const StringView& ToString()
	{
		return mString;
	}
	void ToString(StringImpl& str)
	{
		str += mString;
	}

	void Ref();
};

class BfAtomComposite
{
public:
	BfAtom** mParts;
	int16 mSize;
	int16 mAllocSize;
	bool mOwns;

public:
	BfAtomComposite();
	BfAtomComposite(BfAtomComposite&& rhs);
	BfAtomComposite(const BfAtomComposite& rhs);
	BfAtomComposite(BfAtom* atom);
	BfAtomComposite(const BfAtomComposite& left, const BfAtomComposite& right);
	BfAtomComposite(const BfAtomComposite& left, BfAtom* right);
	~BfAtomComposite();

	void Set(const BfAtomComposite& left, const BfAtomComposite& right);
	void Set(BfAtom** atomsA, int countA, BfAtom** atomsB, int countB);
	BfAtomComposite& operator=(const BfAtomComposite& rhs);
	bool operator==(const BfAtomComposite& other) const;
	bool operator!=(const BfAtomComposite& other) const;
	bool IsValid() const;
	bool IsEmpty() const;
	int GetPartsCount() const;
	String ToString() const;
	void ToString(StringImpl& str) const;
	bool StartsWith(const BfAtomComposite& other) const;
	bool EndsWith(const BfAtomComposite& other) const;
	BfAtomComposite GetSub(int start, int len) const;
	void Reference(const BfAtomComposite& other);

	uint32 GetAtomUpdateIdx();
};

template <const int TBufSize>
class BfAtomCompositeT : public BfAtomComposite
{
public:
	BfAtom* mInternalBuffer[TBufSize];

public:
	BfAtomCompositeT()
	{
		mAllocSize = (int16)TBufSize;
		mParts = mInternalBuffer;
	}

	BfAtomCompositeT(const BfAtomComposite& rhs)
	{
		mAllocSize = (int16)TBufSize;
		mParts = mInternalBuffer;
		*this = rhs;
	}

	BfAtomCompositeT& operator=(const BfAtomComposite& rhs)
	{
		Set(rhs.mParts, rhs.mSize, NULL, 0);
		return *this;
	}
};

class BfSizedAtomComposite : public BfAtomComposite
{
public:
	BfAtom* mInitialAlloc[8];

	BfSizedAtomComposite();
	~BfSizedAtomComposite();
};

struct BfAtomCompositeHash
{
	size_t operator()(const BfAtomComposite& composite) const
	{
		int curHash = 0;
 		for (int i = 0; i < (int)composite.mSize; i++)
 			curHash = ((curHash ^ (int)(intptr)composite.mParts[i]->mHash) << 5) - curHash;
		return curHash;
	}
};

struct BfAtomCompositeEquals
{
	bool operator()(const BfAtomComposite& lhs, const BfAtomComposite& rhs) const
	{
		if (lhs.mSize != rhs.mSize)
			return false;
		for (int i = 0; i < lhs.mSize; i++)
			if (lhs.mParts[i] != rhs.mParts[i])
				return false;
		return true;
	}
};

enum BfWhileSpecializingFlags : int8
{
	BfWhileSpecializingFlag_None = 0,
	BfWhileSpecializingFlag_Type = 1,
	BfWhileSpecializingFlag_Method = 2
};

enum BfCompilerOptionFlags
{
	BfCompilerOptionFlag_EmitDebugInfo = 1,
	BfCompilerOptionFlag_EmitLineInfo = 2,
	BfCompilerOptionFlag_WriteIR = 4,
	BfCompilerOptionFlag_GenerateOBJ = 8,
	BfCompilerOptionFlag_GenerateBitcode = 0x10,
	BfCompilerOptionFlag_ClearLocalVars = 0x20,
	BfCompilerOptionFlag_RuntimeChecks = 0x40,
	BfCompilerOptionFlag_EmitDynamicCastCheck = 0x80,
	BfCompilerOptionFlag_EnableObjectDebugFlags = 0x100,
	BfCompilerOptionFlag_EmitObjectAccessCheck = 0x200,
	BfCompilerOptionFlag_EnableCustodian = 0x400,
	BfCompilerOptionFlag_EnableRealtimeLeakCheck = 0x800,
	BfCompilerOptionFlag_EnableSideStack    = 0x1000,
	BfCompilerOptionFlag_EnableHotSwapping  = 0x2000,
	BfCompilerOptionFlag_IncrementalBuild   = 0x4000,
	BfCompilerOptionFlag_DebugAlloc         = 0x8000,
	BfCompilerOptionFlag_OmitDebugHelpers   = 0x10000,
	BfCompilerOptionFlag_NoFramePointerElim = 0x20000,
	BfCompilerOptionFlag_ArithmeticChecks = 0x40000,
};

enum BfTypeFlags
{
	BfTypeFlags_UnspecializedGeneric = 0x0001,
	BfTypeFlags_SpecializedGeneric = 0x0002,
	BfTypeFlags_Array			= 0x0004,

	BfTypeFlags_Object			= 0x0008,
	BfTypeFlags_Boxed			= 0x0010,
	BfTypeFlags_Pointer			= 0x0020,
	BfTypeFlags_Struct			= 0x0040,
	BfTypeFlags_Interface		= 0x0080,
	BfTypeFlags_Primitive		= 0x0100,
	BfTypeFlags_TypedPrimitive	= 0x0200,
	BfTypeFlags_Tuple			= 0x0400,
	BfTypeFlags_Nullable		= 0x0800,
	BfTypeFlags_SizedArray		= 0x1000,
	BfTypeFlags_Splattable		= 0x2000,
	BfTypeFlags_Union			= 0x4000,
	BfTypeFlags_ConstExpr		= 0x8000,
	//
	BfTypeFlags_WantsMarking    = 0x10000,
	BfTypeFlags_Delegate        = 0x20000,
	BfTypeFlags_Function		= 0x40000,
	BfTypeFlags_HasDestructor   = 0x80000,
	BfTypeFlags_GenericParam	= 0x100000
};

enum BfMethodFlags
{
	BfMethodFlags_Protected = 3,
	BfMethodFlags_Public = 6,
	BfMethodFlags_Static = 0x10,
	BfMethodFlags_Virtual = 0x40,
	BfMethodFlags_ReadOnly = 0x100,
	BfMethodFlags_StdCall = 0x1000,
	BfMethodFlags_FastCall = 0x2000,
	BfMethodFlags_ThisCall = 0x3000,
	BfMethodFlags_Mutating = 0x4000,
	BfMethodFlags_Constructor = 0x8000
};

enum BfObjectFlags : uint8
{
	BfObjectFlag_None = 0,
	BfObjectFlag_MarkIdMask = 0x03,
	BfObjectFlag_Allocated = 0x04,
	BfObjectFlag_StackAlloc = 0x08,
	BfObjectFlag_AppendAlloc = 0x10,
	BfObjectFlag_AllocInfo = 0x20,
	BfObjectFlag_AllocInfo_Short = 0x40,
	BfObjectFlag_Deleted = 0x80,

	BfObjectFlag_StackDeleted = 0x80 // We remove StackAlloc so it doesn't get scanned
};

enum BfCustomAttributeFlags : uint8
{
	BfCustomAttributeFlags_None,
	BfCustomAttributeFlags_DisallowAllowMultiple = 1,
	BfCustomAttributeFlags_NotInherited = 2,
	BfCustomAttributeFlags_ReflectAttribute = 4,
	BfCustomAttributeFlags_AlwaysIncludeTarget = 8
};

enum BfAlwaysIncludeFlags : uint8
{
	BfAlwaysIncludeFlag_None = 0,
	BfAlwaysIncludeFlag_Type = 1,
	BfAlwaysIncludeFlag_IncludeAllMethods = 2,
	BfAlwaysIncludeFlag_AssumeInstantiated = 4,
	BfAlwaysIncludeFlag_All = BfAlwaysIncludeFlag_Type | BfAlwaysIncludeFlag_IncludeAllMethods | BfAlwaysIncludeFlag_AssumeInstantiated
};

enum BfCEOnCompileKind : uint8
{
	BfCEOnCompileKind_None,
	BfCEOnCompileKind_TypeInit,
	BfCEOnCompileKind_TypeDone
};

enum BfPlatformType
{
	BfPlatformType_Unknown,
	BfPlatformType_Windows,
	BfPlatformType_Linux,
	BfPlatformType_macOS,
	BfPlatformType_iOS,
	BfPlatformType_Android,
	BfPlatformType_Wasm
};

enum BfMachineType
{
	BfMachineType_Unknown,
	BfMachineType_x86,
	BfMachineType_x64,
	BfMachineType_ARM,
	BfMachineType_AArch64,
	BfMachineType_Wasm32,
	BfMachineType_Wasm64,
};

enum BfToolsetType
{
	BfToolsetType_GNU,
	BfToolsetType_Microsoft,
	BfToolsetType_LLVM
};

enum BfSIMDSetting
{
	BfSIMDSetting_None,
	BfSIMDSetting_MMX,
	BfSIMDSetting_SSE,
	BfSIMDSetting_SSE2,
	BfSIMDSetting_SSE3,
	BfSIMDSetting_SSE4,
	BfSIMDSetting_SSE41,
	BfSIMDSetting_AVX,
	BfSIMDSetting_AVX2,
};

enum BfAsmKind
{
	BfAsmKind_None,
	BfAsmKind_ATT,
	BfAsmKind_Intel,
};

enum BfOptLevel
{
	BfOptLevel_NotSet = -1,

	BfOptLevel_O0 = 0,
	BfOptLevel_O1,
	BfOptLevel_O2,
	BfOptLevel_O3,
	BfOptLevel_Og,
	BfOptLevel_OgPlus
};

enum BfLTOType
{
	BfLTOType_None = 0,
	BfLTOType_Thin = 1
};

enum BfCFLAAType
{
	BfCFLAAType_None,
	BfCFLAAType_Steensgaard,
	BfCFLAAType_Andersen,
	BfCFLAAType_Both
};

enum BfRelocType
{
	BfRelocType_NotSet,
	BfRelocType_Static,
	BfRelocType_PIC,
	BfRelocType_DynamicNoPIC,
	BfRelocType_ROPI,
	BfRelocType_RWPI,
	BfRelocType_ROPI_RWPI
};

enum BfPICLevel
{
	BfPICLevel_NotSet,
	BfPICLevel_Not,
	BfPICLevel_Small,
	BfPICLevel_Big
};

struct BfCodeGenOptions
{
	bool mIsHotCompile;

	bool mWriteObj;
	bool mWriteBitcode;
	BfAsmKind mAsmKind;
	bool mWriteToLib;
	bool mWriteLLVMIR;

	int16 mVirtualMethodOfs;
	int16 mDynSlotOfs;

	BfRelocType mRelocType;
	BfPICLevel mPICLevel;
	BfSIMDSetting mSIMDSetting;
	BfOptLevel mOptLevel;
	BfLTOType mLTOType;
	int mSizeLevel;
	BfCFLAAType mUseCFLAA;
	bool mUseNewSROA;

	bool mDisableTailCalls;
	bool mDisableUnitAtATime;
	bool mDisableUnrollLoops;
	bool mBBVectorize;
	bool mSLPVectorize;
	bool mLoopVectorize;
	bool mRerollLoops;
	bool mLoadCombine;
	bool mDisableGVNLoadPRE;
	bool mVerifyInput;
	bool mVerifyOutput;
	bool mStripDebug;
	bool mMergeFunctions;
	bool mEnableMLSM;
	bool mRunSLPAfterLoopVectorization;
	bool mUseGVNAfterVectorization;
	bool mEnableLoopInterchange;
	bool mEnableLoopLoadElim;
	bool mExtraVectorizerPasses;
	bool mEnableEarlyCSEMemSSA;
	bool mEnableGVNHoist;
	bool mEnableGVNSink;
	bool mDisableLibCallsShrinkWrap;
	bool mExpensiveCombines;
	bool mEnableSimpleLoopUnswitch;
	bool mDivergentTarget;
	bool mNewGVN;
	bool mRunPartialInlining;
	bool mUseLoopVersioningLICM;
	bool mEnableUnrollAndJam;
	bool mEnableHotColdSplit;

	Val128 mHash;

	BfCodeGenOptions()
	{
		mIsHotCompile = false;
		mWriteObj = true;
		mWriteBitcode = false;
		mAsmKind = BfAsmKind_None;
		mWriteToLib = false;
		mWriteLLVMIR = false;
		mVirtualMethodOfs = 0;
		mDynSlotOfs = 0;

		mRelocType = BfRelocType_NotSet;
		mPICLevel = BfPICLevel_NotSet;
		mSIMDSetting = BfSIMDSetting_None;
		mOptLevel = BfOptLevel_O0;
		mLTOType = BfLTOType_None;
		mSizeLevel = 0;
		mUseCFLAA = BfCFLAAType_None;
		mUseNewSROA = false;

		mDisableTailCalls = false;
		mDisableUnitAtATime = false;
		mDisableUnrollLoops = false;
		mBBVectorize = false;
		mSLPVectorize = false;
		mLoopVectorize = false;
		mRerollLoops = false;
		mLoadCombine = false;
		mDisableGVNLoadPRE = false;
		mVerifyInput = false;
		mVerifyOutput = false;
		mStripDebug = false;
		mMergeFunctions = false;
		mEnableMLSM = false;
		mRunSLPAfterLoopVectorization = false;
		mUseGVNAfterVectorization = false;
		mEnableLoopInterchange = false;
		mEnableLoopLoadElim = true;
		mExtraVectorizerPasses = false;
		mEnableEarlyCSEMemSSA = true;
		mEnableGVNHoist = false;
		mEnableGVNSink = false;
		mDisableLibCallsShrinkWrap = false;
		mExpensiveCombines = false;
		mEnableSimpleLoopUnswitch = false;
		mDivergentTarget = false;
		mNewGVN = false;
		mRunPartialInlining = false;
		mUseLoopVersioningLICM = false;
		mEnableUnrollAndJam = false;
		mEnableHotColdSplit = false;
	}

	void GenerateHash()
	{
		HashContext hashCtx;

		hashCtx.Mixin(mWriteObj);
		hashCtx.Mixin(mWriteBitcode);
		hashCtx.Mixin(mAsmKind);
		hashCtx.Mixin(mWriteToLib);
		hashCtx.Mixin(mWriteLLVMIR);
		hashCtx.Mixin(mVirtualMethodOfs);
		hashCtx.Mixin(mDynSlotOfs);

		hashCtx.Mixin(mRelocType);
		hashCtx.Mixin(mPICLevel);
		hashCtx.Mixin(mSIMDSetting);
		hashCtx.Mixin(mOptLevel);
		hashCtx.Mixin(mLTOType);
		hashCtx.Mixin(mSizeLevel);
		hashCtx.Mixin(mUseCFLAA);
		hashCtx.Mixin(mUseNewSROA);

		hashCtx.Mixin(mDisableTailCalls);
		hashCtx.Mixin(mDisableUnitAtATime);
		hashCtx.Mixin(mDisableUnrollLoops);
		hashCtx.Mixin(mBBVectorize);
		hashCtx.Mixin(mSLPVectorize);
		hashCtx.Mixin(mLoopVectorize);
		hashCtx.Mixin(mRerollLoops);
		hashCtx.Mixin(mLoadCombine);
		hashCtx.Mixin(mDisableGVNLoadPRE);
		hashCtx.Mixin(mVerifyInput);
		hashCtx.Mixin(mVerifyOutput);
		hashCtx.Mixin(mStripDebug);
		hashCtx.Mixin(mMergeFunctions);
		hashCtx.Mixin(mEnableMLSM);
		hashCtx.Mixin(mRunSLPAfterLoopVectorization);
		hashCtx.Mixin(mUseGVNAfterVectorization);
		hashCtx.Mixin(mEnableLoopInterchange);
		hashCtx.Mixin(mEnableLoopLoadElim);
		hashCtx.Mixin(mExtraVectorizerPasses);

		mHash = hashCtx.Finish128();
	}
};

enum BfParamKind : uint8
{
	BfParamKind_Normal,
	BfParamKind_ExplicitThis,
	BfParamKind_Params,
	BfParamKind_DelegateParam,
	BfParamKind_ImplicitCapture,
	BfParamKind_AppendIdx,
	BfParamKind_VarArgs
};

enum BfShow : uint8
{
	BfShow_Show,
	BfShow_HideIndirect,
	BfShow_Hide
};

class BfParameterDef
{
public:
	String mName;
	BfTypeReference* mTypeRef;
	BfParameterDeclaration* mParamDeclaration;
	int mMethodGenericParamIdx;
	BfParamKind mParamKind;
	uint8 mNamePrefixCount; // Number of @'s

public:
	BfParameterDef()
	{
		mTypeRef = NULL;
		mMethodGenericParamIdx = -1;
		mParamKind = BfParamKind_Normal;
		mParamDeclaration = NULL;
		mNamePrefixCount = 0;
	}
	void SetName(BfAstNode* nameNode);
};

class BfMemberDef
{
public:
#ifdef _DEBUG
	StringT<48> mName;
#else
	String mName;
#endif
	BfTypeDef* mDeclaringType;
	BfProtection mProtection;
	uint8 mNamePrefixCount; // Number of @'s
	bool mIsStatic;
	BfShow mShow;
	bool mIsReadOnly;
	bool mHasMultiDefs;

public:
	BfMemberDef()
	{
		mDeclaringType = NULL;
		mProtection = BfProtection_Public;
		mNamePrefixCount = 0;
		mIsStatic = false;
		mShow = BfShow_Show;
		mIsReadOnly = false;
		mHasMultiDefs = false;
	}

	virtual ~BfMemberDef()
	{
	}

	void SetName(BfAstNode* nameNode);
};

class BfFieldDef : public BfMemberDef
{
public:
	int mIdx;
	bool mIsConst; // Note: Consts are also all considered Static
	BfTypeReference* mTypeRef;
	BfProtection mUsingProtection;
	bool mIsInline;
	bool mIsVolatile;
	bool mIsExtern;
	bool mIsAppend;
	bool mIsProperty;
	BfAstNode* mFieldDeclaration;
	// It may seem that fields and properties don't need a 'mNextWithSameName', but with extensions it's possible
	//  to have two libraries which each add a field to a type with the same name
	BfFieldDef* mNextWithSameName;

public:
	BfFieldDef()
	{
		mIdx = 0;
		mIsConst = false;
		mTypeRef = NULL;
		mUsingProtection = BfProtection_Hidden;
		mIsInline = false;
		mIsExtern = false;
		mIsAppend = false;
		mIsVolatile = false;
		mIsProperty = false;
		mFieldDeclaration = NULL;
		mNextWithSameName = NULL;
	}

	bool IsUnnamedTupleField()
	{
		return (mName[0] >= '0') && (mName[0] <= '9');
	}

	bool IsEnumCaseEntry()
	{
		return (mFieldDeclaration != NULL) && (BfNodeIsA<BfEnumEntryDeclaration>(mFieldDeclaration));
	}

	bool IsNonConstStatic()
	{
		return mIsStatic && !mIsConst;
	}

	BfAstNode* GetRefNode()
	{
		if (mFieldDeclaration == NULL)
			return NULL;

		if (auto fieldDeclaration = BfNodeDynCast<BfFieldDeclaration>(mFieldDeclaration))
		{
			if (fieldDeclaration->mNameNode != NULL)
				return fieldDeclaration->mNameNode;
		}

		if (auto paramDeclaration = BfNodeDynCast<BfParameterDeclaration>(mFieldDeclaration))
		{
			if (paramDeclaration->mNameNode != NULL)
				return paramDeclaration->mNameNode;
		}

		return mFieldDeclaration;
	}

	BfFieldDeclaration* GetFieldDeclaration()
	{
		return BfNodeDynCast<BfFieldDeclaration>(mFieldDeclaration);
	}

	BfParameterDeclaration* GetParamDeclaration()
	{
		return BfNodeDynCast<BfParameterDeclaration>(mFieldDeclaration);
	}

	BfExpression* GetInitializer()
	{
		if (auto fieldDecl = GetFieldDeclaration())
			return fieldDecl->mInitializer;
		if (auto paramDecl = GetParamDeclaration())
			return paramDecl->mInitializer;
		return NULL;
	}

	BfAstNode* GetNameNode()
	{
		if (auto fieldDecl = GetFieldDeclaration())
			return fieldDecl->mNameNode;
		if (auto paramDecl = GetParamDeclaration())
			return paramDecl->mNameNode;
		return NULL;
	}
};

class BfPropertyDef : public BfFieldDef
{
public:
	Array<BfMethodDef*> mMethods;
	BfPropertyDef* mNextWithSameName;

public:
	BfPropertyDef()
	{
		mNextWithSameName = NULL;
	}

	bool IsVirtual();
	bool HasExplicitInterface();
	bool IsExpressionBodied();
	BfAstNode* GetRefNode();
};

enum BfGenericParamFlags : uint16
{
	BfGenericParamFlag_None			= 0,
	BfGenericParamFlag_Class		= 1,
	BfGenericParamFlag_Struct		= 2,
	BfGenericParamFlag_StructPtr	= 4,
	BfGenericParamFlag_Enum			= 8,
	BfGenericParamFlag_Interface	= 0x10,
	BfGenericParamFlag_Concrete		= 0x20,
	BfGenericParamFlag_New			= 0x40,
	BfGenericParamFlag_Delete		= 0x80,
	BfGenericParamFlag_Var			= 0x100,
	BfGenericParamFlag_Const		= 0x200,
	BfGenericParamFlag_Equals		= 0x400,
	BfGenericParamFlag_Equals_Op    = 0x800,
	BfGenericParamFlag_Equals_Type  = 0x1000,
	BfGenericParamFlag_Equals_IFace = 0x2000,
	BfGenericParamFlag_ComptypeExpr = 0x4000
};

class BfConstraintDef
{
public:
	BfGenericParamFlags mGenericParamFlags;
	Array<BfAstNode*> mConstraints;

	BfConstraintDef()
	{
		mGenericParamFlags = BfGenericParamFlag_None;
	}

	bool operator==(const BfConstraintDef& other) const
	{
		if (mGenericParamFlags != other.mGenericParamFlags)
			return false;
		if (mConstraints.mSize != other.mConstraints.mSize)
			return false;
		for (int i = 0; i < mConstraints.mSize; i++)
		{
			if (!mConstraints[i]->Equals(other.mConstraints[i]->ToStringView()))
				return false;
		}
		return true;
	}

	bool operator!=(const BfConstraintDef& other) const
	{
		return !(*this == other);
	}
};

class BfGenericParamDef : public BfConstraintDef
{
public:
	String mName;
	Array<BfIdentifierNode*> mNameNodes; // 0 is always the def name

	bool operator==(const BfGenericParamDef& other) const
	{
		if (mName != other.mName)
			return false;
		return *(BfConstraintDef*)this == *(BfConstraintDef*)&other;
	}

	bool operator!=(const BfGenericParamDef& other) const
	{
		return !(*this == other);
	}
};

class BfExternalConstraintDef : public BfConstraintDef
{
public:
	BfTypeReference* mTypeRef;
};

// CTOR is split into two for Objects - Ctor clears and sets up VData, Ctor_Body executes ctor body code
enum BfMethodType : uint8
{
	BfMethodType_Ignore,
	BfMethodType_Normal,
	BfMethodType_PropertyGetter,
	BfMethodType_PropertySetter,
	BfMethodType_CtorCalcAppend,
	BfMethodType_Ctor,
	BfMethodType_CtorNoBody,
	BfMethodType_CtorClear,
	BfMethodType_Init,
	BfMethodType_Dtor,
	BfMethodType_Operator,
	BfMethodType_Mixin,
	BfMethodType_Extension
};

enum BfCallingConvention : uint8
{
	BfCallingConvention_Unspecified,
	BfCallingConvention_Cdecl,
	BfCallingConvention_Stdcall,
	BfCallingConvention_Fastcall,
};

#define BF_METHODNAME_MARKMEMBERS "GCMarkMembers"
#define BF_METHODNAME_MARKMEMBERS_STATIC "GCMarkStaticMembers"
#define BF_METHODNAME_FIND_TLS_MEMBERS "GCFindTLSMembers"
#define BF_METHODNAME_DYNAMICCAST "DynamicCastToTypeId"
#define BF_METHODNAME_DYNAMICCAST_INTERFACE "DynamicCastToInterface"
#define BF_METHODNAME_CALCAPPEND "this$calcAppend"
#define BF_METHODNAME_ENUM_HASFLAG "HasFlag"
#define BF_METHODNAME_ENUM_GETUNDERLYING "get__Underlying"
#define BF_METHODNAME_ENUM_GETUNDERLYINGREF "get__UnderlyingRef"
#define BF_METHODNAME_EQUALS "Equals"
#define BF_METHODNAME_INVOKE "Invoke"
#define BF_METHODNAME_TO_STRING "ToString"
#define BF_METHODNAME_DEFAULT_EQUALS "__Equals"
#define BF_METHODNAME_DEFAULT_STRICT_EQUALS "__StrictEquals"

enum BfOptimize : int8
{
	BfOptimize_Default,
	BfOptimize_Unoptimized,
	BfOptimize_Optimized
};

enum BfImportKind : int8
{
	BfImportKind_None,
	BfImportKind_Import_Unknown,
	BfImportKind_Import_Dynamic,
	BfImportKind_Import_Static,
	BfImportKind_Export
};

enum BfCommutableKind : int8
{
	BfCommutableKind_None,
	BfCommutableKind_Operator,
	BfCommutableKind_Forward,
	BfCommutableKind_Reverse,
};

enum BfComptimeFlags : int8
{
	BfComptimeFlag_None,
	BfComptimeFlag_Comptime = 1,
	BfComptimeFlag_OnlyFromComptime = 2,
	BfComptimeFlag_ConstEval = 4
};

class BfMethodDef : public BfMemberDef
{
public:
	BfAstNode* mMethodDeclaration;
	BfAstNode* mBody;

	BfTypeReference* mExplicitInterface;
	BfTypeReference* mReturnTypeRef;
	Array<BfParameterDef*> mParams;
	Array<BfGenericParamDef*> mGenericParams;
	Array<BfExternalConstraintDef> mExternalConstraints;
	Dictionary<StringView, int>* mParamNameMap;
	BfMethodDef* mNextWithSameName;
	Val128 mFullHash;

	int mIdx;
	int mPropertyIdx;
	BfMethodType mMethodType;
	bool mIsLocalMethod;
	bool mIsVirtual;
	bool mIsOverride;
	bool mIsAbstract;
	bool mIsConcrete;
	bool mIsPartial;
	bool mIsNew;
	bool mCodeChanged;
	bool mWantsBody;
	bool mCLink;
	bool mHasAppend;
	bool mAlwaysInline;
	bool mIsNoReturn;
	bool mIsMutating;
	bool mIsNoSplat;
	bool mIsNoReflect;
	bool mIsSkipCall;
	bool mHasComptime;
	bool mIsOperator;
	bool mIsExtern;
	bool mIsNoDiscard;
	bool mHasExplicitThis;
	bool mAddedAfterEmit;
	BfCommutableKind mCommutableKind;
	BfCheckedKind mCheckedKind;
	BfImportKind mImportKind;
	BfCallingConvention mCallingConvention;

public:
	BfMethodDef()
	{
		mIdx = -1;
		mPropertyIdx = -1;
		mIsLocalMethod = false;
		mIsVirtual = false;
		mIsOverride = false;
		mIsAbstract = false;
		mIsConcrete = false;
		mIsStatic = false;
		mIsNew = false;
		mIsPartial = false;
		mCLink = false;
		mIsNoReturn = false;
		mIsMutating = false;
		mIsNoSplat = false;
		mIsNoReflect = false;
		mIsSkipCall = false;
		mHasComptime = false;
		mIsOperator = false;
		mIsExtern = false;
		mIsNoDiscard = false;
		mHasExplicitThis = false;
		mAddedAfterEmit = false;
		mBody = NULL;
		mExplicitInterface = NULL;
		mReturnTypeRef = NULL;
		mMethodDeclaration = NULL;
		mCodeChanged = false;
		mWantsBody = true;
		mCommutableKind = BfCommutableKind_None;
		mCheckedKind = BfCheckedKind_NotSet;
		mImportKind = BfImportKind_None;
		mMethodType = BfMethodType_Normal;
		mCallingConvention = BfCallingConvention_Unspecified;
		mHasAppend = false;
		mAlwaysInline = false;
		mParamNameMap = NULL;
		mNextWithSameName = NULL;
	}

	virtual ~BfMethodDef();

	static BfImportKind GetImportKindFromPath(const StringImpl& filePath);
	bool HasNoThisSplat() { return mIsMutating || mIsNoSplat; }
	void Reset();
	void FreeMembers();
	BfMethodDeclaration* GetMethodDeclaration();
	BfPropertyMethodDeclaration* GetPropertyMethodDeclaration();
	BfPropertyDeclaration* GetPropertyDeclaration();
	BfAstNode* GetRefNode();
	BfTokenNode* GetMutNode();
	bool HasBody();
	bool IsEmptyPartial();
	bool IsDefaultCtor();
	bool IsCtorOrInit();
	String ToString();
	int GetExplicitParamCount();
	void BuildParamNameMap();
};

class BfOperatorDef : public BfMethodDef
{
public:
	BfOperatorDeclaration* mOperatorDeclaration;

public:
	BfOperatorDef()
	{
		mOperatorDeclaration = NULL;
	}

	bool IsExplicit()
	{
		if (mOperatorDeclaration->mExplicitToken != NULL)
			return mOperatorDeclaration->mExplicitToken->mToken == BfToken_Explicit;
		if (mOperatorDeclaration->mOpTypeToken != NULL)
			return mOperatorDeclaration->mOpTypeToken->mToken == BfToken_Explicit;
		return false;
	}
};

struct BfTypeDefLookupContext
{
public:
	int mBestPri;
	BfTypeDef* mBestTypeDef;
	BfTypeDef* mAmbiguousTypeDef;

public:
	BfTypeDefLookupContext()
	{
		mBestPri = (int)0x80000000;
		mBestTypeDef = NULL;
		mAmbiguousTypeDef = NULL;
	}

	bool HasValidMatch()
	{
		return (mBestPri >= 0) && (mBestTypeDef != NULL);
	}
};

struct BfMemberSetEntry
{
	BfMemberDef* mMemberDef;

	BfMemberSetEntry(BfMemberDef* memberDef)
	{
		mMemberDef = memberDef;
	}

	bool operator==(const BfMemberSetEntry& other) const
	{
		return mMemberDef->mName == other.mMemberDef->mName;
	}

	bool operator==(const StringImpl& other) const
	{
		return mMemberDef->mName == other;
	}
};

class BfTypeDefMemberSet : public HashSet<BfMemberSetEntry>
{
public:
	int mSourceSize;

public:
	BfTypeDefMemberSet()
	{
		mSourceSize = 0;
	}

	void Clear()
	{
		HashSet<BfMemberSetEntry>::Clear();
		mSourceSize = 0;
	}
};

// For partial classes, the first entry in the map will contain the combined data
class BfTypeDef
{
public:
	enum DefState
	{
		DefState_New,
		DefState_Defined,
		DefState_CompositeWithPartials, // Temporary condition
		DefState_AwaitingNewVersion,
		DefState_Signature_Changed,
		DefState_InlinedInternals_Changed, // Code within methods, including inlined methods, changed
		DefState_Internals_Changed, // Only code within a non-inlined methods changed
		DefState_Refresh,
		DefState_Deleted,
		DefState_Emitted,
		DefState_EmittedDirty
	};

public:
	BfTypeDef* mNextRevision;

	BfSystem* mSystem;
	BfProject* mProject;
	BfTypeDeclaration* mTypeDeclaration;
	BfSource* mSource;
	DefState mDefState;
	Val128 mSignatureHash; // Data, methods, etc
	Val128 mFullHash;
	Val128 mInlineHash;

	BfTypeDef* mEmitParent;
	BfTypeDef* mOuterType;
	BfAtomComposite mNamespace;
	BfAtom* mName;
	BfAtom* mNameEx; // Contains extensions like `1 for param counts
	BfAtomComposite mFullName;
	BfAtomComposite mFullNameEx;
	BfProtection mProtection;
	Array<BfAtomComposite> mNamespaceSearch;
	Array<BfTypeReference*> mStaticSearch;
	Array<BfTypeReference*> mInternalAccessSet;
	Array<BfFieldDef*> mFields;
	Array<BfPropertyDef*> mProperties;
	Array<BfMethodDef*> mMethods;
	BfTypeDefMemberSet mMethodSet;
	BfTypeDefMemberSet mFieldSet;
	BfTypeDefMemberSet mPropertySet;
	Array<BfOperatorDef*> mOperators;
	Array<BfGenericParamDef*> mGenericParamDefs;
	Array<BfExternalConstraintDef> mExternalConstraints;
	Array<BfTypeReference*> mBaseTypes;
	Array<BfTypeDef*> mNestedTypes;
	Array<BfDirectStrTypeReference*> mDirectAllocNodes;
	Array<BfTypeDef*> mPartials; // Only valid for mIsCombinedPartial

	int mHash;
	int mPartialIdx;
	int mNestDepth;
	int mDupDetectedRevision; // Error state
	BfTypeCode mTypeCode;
	BfShow mShow;
	bool mIsAlwaysInclude;
	bool mIsNoDiscard;
	bool mIsPartial;
	bool mIsExplicitPartial;
	bool mPartialUsed;
	bool mIsCombinedPartial;
	bool mIsDelegate;
	bool mIsFunction;
	bool mIsClosure;
	bool mIsAbstract;
	bool mIsStatic;
	bool mHasCEOnCompile;
	bool mHasAppendCtor;
	bool mHasCtorNoBody;
	bool mHasExtensionMethods;
	bool mHasOverrideMethods;
	bool mHasUsingFields;
	bool mIsOpaque;
	bool mIsNextRevision;
	bool mInDeleteQueue;
	bool mForceUseNextRevision;

public:
	BfTypeDef()
	{
		Init();
	}

	~BfTypeDef();

	void Init()
	{
		mName = NULL;
		mNameEx = NULL;
		mSystem = NULL;
		mProject = NULL;
		mTypeCode = BfTypeCode_None;
		mShow = BfShow_Show;
		mIsAlwaysInclude = false;
		mIsNoDiscard = false;
		mIsExplicitPartial = false;
		mIsPartial = false;
		mIsCombinedPartial = false;
		mTypeDeclaration = NULL;
		mSource = NULL;
		mDefState = DefState_New;
		mHash = 0;
		mPartialIdx = -1;
		mIsAbstract = false;
		mIsDelegate = false;
		mIsFunction = false;
		mIsClosure = false;
		mIsStatic = false;
		mHasCEOnCompile = false;
		mHasAppendCtor = false;
		mHasCtorNoBody = false;
		mHasExtensionMethods = false;
		mHasOverrideMethods = false;
		mHasUsingFields = false;
		mIsOpaque = false;
		mPartialUsed = false;
		mIsNextRevision = false;
		mInDeleteQueue = false;
		mForceUseNextRevision = false;
		mDupDetectedRevision = -1;
		mNestDepth = 0;
		mEmitParent = NULL;
		mOuterType = NULL;
		mTypeDeclaration = NULL;
		mNextRevision = NULL;
		mProtection = BfProtection_Public;
	}

	BfSource* GetLastSource();
	bool IsGlobalsContainer();
	void Reset();
	void FreeMembers();
	void PopulateMemberSets();
	void ClearMemberSets();
	void RemoveGenericParamDef(BfGenericParamDef* genericParamDef);
	int GetSelfGenericParamCount();
	String ToString();
	BfMethodDef* GetMethodByName(const StringImpl& name, int paramCount = -1);
	BfFieldDef* GetFieldByName(const StringImpl& name);
	bool HasAutoProperty(BfPropertyDeclaration* propertyDeclaration);
	bool ContainsPartial(BfTypeDef* partialTypeDef);
	bool HasParsingFailed();
	String GetAutoPropertyName(BfPropertyDeclaration* propertyDeclaration);
	BfAstNode* GetRefNode();

	bool IsEmitted() { return mEmitParent != NULL; }

	BfTypeDef* GetDefinition(bool getEmitRoot = false)
	{
		if (mEmitParent != NULL)
		{
			if ((getEmitRoot) && (mEmitParent->mIsCombinedPartial))
				return mEmitParent->mPartials[0];
			return mEmitParent;
		}
		return this;
	}

	BfTypeDef* GetLatest()
	{
		if (mEmitParent != NULL)
			return mEmitParent->GetLatest();
		if (mNextRevision != NULL)
			return mNextRevision;
		return this;
	}

	void ReportMemory(MemReporter* memReporter);
	bool NameEquals(BfTypeDef* otherTypeDef);
	bool IsExtension()
	{
		return mTypeCode == BfTypeCode_Extension;
	}
	bool HasSource(BfSource* source);
	bool HasCustomAttributes();
};

struct BfTypeDefMapFuncs : public MultiHashSetFuncs
{
	int GetHash(BfTypeDef* typeDef)
	{
		return GetHash(typeDef->mFullName);
	}

	int GetHash(const BfAtomComposite& name)
	{
		int hash = 0;
		for (int i = 0; i < name.mSize; i++)
		{
			auto atom = name.mParts[i];
			hash = ((hash ^ atom->mHash) << 5) - hash;
		}
		return (hash & 0x7FFFFFFF);
	}

	bool Matches(const BfAtomComposite& name, BfTypeDef* typeDef)
	{
		return name == typeDef->mFullName;
	}

	bool Matches(BfTypeDef* keyTypeDef, BfTypeDef* typeDef)
	{
		return keyTypeDef == typeDef;
	}
};

class BfTypeDefMap : public MultiHashSet<BfTypeDef*, BfTypeDefMapFuncs>
{
public:
	struct SkipEntry
	{
	public:
		int mIndex;
		int mRevision;

	public:
		SkipEntry()
		{
			mIndex = -1;
			mRevision = -1;
		}

		SkipEntry(int index, int revision)
		{
			mIndex = index;
			mRevision = revision;
		}
	};

	Array<SkipEntry> mPartialSkipCache;
	int mRevision;

public:
	BfTypeDefMap()
	{
		mRevision = 1;
	}

	void Add(BfTypeDef* value)
	{
		MultiHashSet::Add(value);
		mRevision++;
	}

	void AddAfter(BfTypeDef* value, Entry* afterEntry)
	{
		MultiHashSet::AddAfter(value, afterEntry);
		mRevision++;
	}

	template <typename TKey>
	bool Remove(const TKey& key)
	{
		bool result = MultiHashSet::Remove(key);
		mRevision++;
		return result;
	}

	Iterator Erase(const Iterator& itr)
	{
		auto result = MultiHashSet::Erase(itr);
		mRevision++;
		return result;
	}

	void Clear()
	{
		MultiHashSet::Clear();
		mRevision++;
	}

	void SetPartialSkipCache(int partialIdx, int mapToIdx)
	{
		while (partialIdx >= mPartialSkipCache.mSize)
			mPartialSkipCache.Add(SkipEntry());
		mPartialSkipCache[partialIdx] = SkipEntry(mapToIdx, mRevision);
	}
};

enum BfTargetType
{
	BfTargetType_BeefConsoleApplication,
	BfTargetType_BeefWindowsApplication,
	BfTargetType_BeefLib,
	BfTargetType_CustomBuild,
	BfTargetType_BeefTest,
	BfTargetType_C_ConsoleApplication,
	BfTargetType_C_WindowsApplication,
	BfTargetType_BeefApplication_StaticLib,
	BfTargetType_BeefApplication_DynamicLib,
	BfTargetType_BeefLib_StaticLib,
	BfTargetType_BeefLib_DynamicLib,
};

enum BfProjectFlags
{
	BfProjectFlags_None           = 0,
	BfProjectFlags_MergeFunctions = 1,
	BfProjectFlags_CombineLoads   = 2,
	BfProjectFlags_VectorizeLoops = 4,
	BfProjectFlags_VectorizeSLP   = 8,
	BfProjectFlags_SingleModule   = 0x10,
	BfProjectFlags_AsmOutput      = 0x20,
	BfProjectFlags_AsmOutput_ATT  = 0x40,
	BfProjectFlags_AlwaysIncludeAll = 0x80,
};

class BfProject
{
public:
	enum DeleteStage
	{
		DeleteStage_None,
		DeleteStage_Queued,
		DeleteStage_AwaitingRefs,
	};

public:
	BfSystem* mSystem;
	String mName;
	String mSafeName;
	String mDirectory;
	Array<BfProject*> mDependencies;
	BfTargetType mTargetType;
	BfCodeGenOptions mCodeGenOptions;
	bool mDisabled;
	bool mSingleModule;
	bool mAlwaysIncludeAll;
	DeleteStage mDeleteStage;
	int mIdx;

	String mStartupObject;
	Array<String> mPreprocessorMacros;
	Dictionary<BfAtomComposite, int> mNamespaces;

	HashSet<BfModule*> mUsedModules;
	HashSet<BfType*> mReferencedTypeData;

	Val128 mBuildConfigHash;
	Val128 mVDataConfigHash;

	bool mBuildConfigChanged;

public:
	BfProject();
	~BfProject();

	bool ContainsReference(BfProject* refProject);
	bool ReferencesOrReferencedBy(BfProject* refProject);
	bool IsTestProject();
};

//CDH TODO move these out to separate header if list gets big/unwieldy
enum BfWarning
{
	BfWarning_CS0108_MemberHidesInherited				= 108,
	BfWarning_CS0114_MethodHidesInherited				= 114,
	BfWarning_CS0162_UnreachableCode					= 162,
	BfWarning_CS0168_VariableDeclaredButNeverUsed		= 168,
	BfWarning_CS0472_ValueTypeNullCompare				= 472,
	BfWarning_CS1030_PragmaWarning						= 1030,
	BfWarning_BF4201_Only7Hex							= 4201,
	BfWarning_BF4202_TooManyHexForInt					= 4202,
	BfWarning_BF4203_UnnecessaryDynamicCast				= 4203,
	BfWarning_BF4204_AddressOfReadOnly					= 4204,
	BfWarning_BF4205_StringInterpolationParam			= 4205,
	BfWarning_BF4206_OperatorCommutableUsage			= 4206,
	BfWarning_C4554_PossiblePrecedenceError				= 4554
};

class BfErrorLocation
{
public:
	String mFile;
	int mLine;
	int mColumn;
};

class BfErrorBase
{
public:
	bool mIsWarning;
	bool mIsDeferred;
	BfSourceData* mSource;
	int mSrcStart;
	int mSrcEnd;
	BfErrorLocation* mLocation;

public:
	BfErrorBase()
	{
		mIsWarning = false;
		mIsDeferred = false;
		mSource = NULL;
		mSrcStart = -1;
		mSrcEnd = -1;
		mLocation = NULL;
	}

	~BfErrorBase();
	void SetSource(BfPassInstance* passInstance, BfSourceData* source);
};

class BfMoreInfo : public BfErrorBase
{
public:
	String mInfo;
};

class BfError : public BfErrorBase
{
public:
	bool mIsAfter;
	bool mIsPersistent;
	BfWhileSpecializingFlags mIsWhileSpecializing;
	bool mIgnore;
	BfProject* mProject;
	String mError;
	int mWarningNumber;
	Array<BfMoreInfo*> mMoreInfo;

public:
	BfError()
	{
		mIsAfter = false;
		mIsPersistent = false;
		mIsWhileSpecializing = BfWhileSpecializingFlag_None;
		mIgnore = false;
		mProject = NULL;
		mWarningNumber = 0;
	}

	~BfError()
	{
		for (auto moreInfo : mMoreInfo)
			delete moreInfo;
	}

	int GetSrcStart()
	{
		return mSrcStart;
	}

	int GetSrcLength()
	{
		return mSrcEnd - mSrcStart;
	}

	int GetSrcEnd()
	{
		return mSrcEnd;
	}
};

class BfSourceClassifier;
class BfAutoComplete;

enum BfFailFlags
{
	BfFailFlag_None = 0,
	BfFailFlag_ShowSpaceChars = 1
};

struct BfErrorEntry
{
public:
	BfErrorBase* mError;

	BfErrorEntry(BfErrorBase* error)
	{
		mError = error;
	}
	size_t GetHashCode() const;
	bool operator==(const BfErrorEntry& other) const;
};

class BfPassInstance
{
public:
	const int sMaxDisplayErrors = 100;
	const int sMaxErrors = 1000;

	BfSystem* mSystem;
	BfCompiler* mCompiler;
	bool mTrimMessagesToCursor;
	int mFailedIdx;
	int mWarnIdx;

	Dictionary<BfSourceData*, String> mSourceFileNameMap;
	HashSet<BfErrorEntry> mErrorSet;
	Array<BfError*> mErrors;
	int mIgnoreCount;
	int mWarningCount;
	int mDeferredErrorCount;
	Deque<String> mOutStream;
	bool mLastWasDisplayed;
	bool mLastWasAdded;
	uint8 mClassifierPassId;
	HashSet<BfSourceData*> mFilterErrorsTo;
	bool mHadSignatureChanges;

public:
	BfPassInstance(BfSystem* bfSystem)
	{
		mTrimMessagesToCursor = false;
		mFailedIdx = 0;
		mWarnIdx = 0;
		mSystem = bfSystem;
		mCompiler = NULL;
		mLastWasDisplayed = false;
		mLastWasAdded = false;
		mClassifierPassId = 0;
		mWarningCount = 0;
		mDeferredErrorCount = 0;
		mIgnoreCount = 0;
		mHadSignatureChanges = false;
	}

	~BfPassInstance();

	void ClearErrors();
	bool HasFailed();
	bool HasMessages();
	void OutputLine(const StringImpl& str);
	bool PopOutString(String* outString);
	bool WantsRangeRecorded(BfSourceData* bfSource, int srcIdx, int srcLen, bool isWarning, bool isDeferred = false);
	bool WantsRangeDisplayed(BfSourceData* bfSource, int srcIdx, int srcLen, bool isWarning, bool isDeferred = false);
	void TrimSourceRange(BfSourceData* source, int startIdx, int& srcLen); // Trim to a single line, in cases when we reference a large multi-line node

	bool HasLastFailedAt(BfAstNode* astNode);

	void MessageAt(const StringImpl& msgPrefix, const StringImpl& error, BfSourceData* bfSource, int srcIdx, int srcLen = 1, BfFailFlags flags = BfFailFlag_None);
	void FixSrcStartAndEnd(BfSourceData* source, int& startIdx, int& endIdx);

	BfError* WarnAt(int warningNumber, const StringImpl& warning, BfSourceData* bfSource, int srcIdx, int srcLen = 1, bool isDeferred = false);
	BfError* Warn(int warningNumber, const StringImpl& warning);
	BfError* Warn(int warningNumber, const StringImpl& warning, BfAstNode* refNode, bool isDeferred = false);
	BfError* DeferWarn(int warningNumber, const StringImpl& warning, BfAstNode* refNode);
	BfError* WarnAfter(int warningNumber, const StringImpl& warning, BfAstNode* refNode);
	BfError* WarnAfterAt(int warningNumber, const StringImpl& error, BfSourceData* bfSource, int srcIdx);

	BfMoreInfo* MoreInfoAt(const StringImpl& info, BfSourceData* bfSource, int srcIdx, int srcLen, BfFailFlags flags = BfFailFlag_None);
	BfMoreInfo* MoreInfo(const StringImpl& info, bool forceQueue = false);
	BfMoreInfo* MoreInfo(const StringImpl& info, BfAstNode* refNode);
	BfMoreInfo* MoreInfoAfter(const StringImpl& info, BfAstNode* refNode);

	BfError* FailAt(const StringImpl& error, BfSourceData* bfSource, int srcIdx, int srcLen = 1, BfFailFlags flags = BfFailFlag_None);
	BfError* FailAfterAt(const StringImpl& error, BfSourceData* bfSource, int srcIdx);
	BfError* Fail(const StringImpl& error);
	BfError* Fail(const StringImpl& error, BfAstNode* refNode);
	BfError* FailAfter(const StringImpl& error, BfAstNode* refNode);
	BfError* DeferFail(const StringImpl& error, BfAstNode* refNode);
	void SilentFail();

	void TryFlushDeferredError();
	void WriteErrorSummary();
};

enum BfOptionFlags
{
	BfOptionFlags_None						= 0,
	BfOptionFlags_RuntimeChecks				= 1,
	BfOptionFlags_InitLocalVariables		= 2,
	BfOptionFlags_EmitDynamicCastCheck		= 4,
	BfOptionFlags_EmitObjectAccessCheck		= 8,
	BfOptionFlags_ArithmeticCheck			= 0x10,

	BfOptionFlags_ReflectAlwaysIncludeType	= 0x20,
	BfOptionFlags_ReflectAlwaysIncludeAll	= 0x40,
	BfOptionFlags_ReflectAssumeInstantiated	= 0x80,
	BfOptionFlags_ReflectBoxing				= 0x100,
	BfOptionFlags_ReflectStaticFields		= 0x200,
	BfOptionFlags_ReflectNonStaticFields	= 0x400,
	BfOptionFlags_ReflectStaticMethods		= 0x800,
	BfOptionFlags_ReflectNonStaticMethods	= 0x1000,
	BfOptionFlags_ReflectConstructors		= 0x2000,
	BfOptionFlags_ReflectAlwaysIncludeFiltered = 0x4000,

	BfOptionFlags_Reflect_MethodMask		= BfOptionFlags_ReflectStaticMethods | BfOptionFlags_ReflectNonStaticMethods | BfOptionFlags_ReflectConstructors,
	BfOptionFlags_Mask = 0x3FFF
};

enum BfFieldFlags
{
	BfFieldFlags_Protected = 3,
	BfFieldFlags_Public = 6,
	BfFieldFlags_Static = 0x10,
	BfFieldFlags_Const = 0x40,
	BfFieldFlags_SpecialName = 0x80,
	BfFieldFlags_EnumPayload = 0x100,
	BfFieldFlags_EnumDiscriminator = 0x200,
	BfFieldFlags_EnumCase = 0x400,
	BfFieldFlags_ReadOnly = 0x800,
	BfFieldFlags_Appended = 0x1000
};

enum BfReflectKind
{
	BfReflectKind_None = 0,
	BfReflectKind_Type = 1,
	BfReflectKind_NonStaticFields = 2,
	BfReflectKind_StaticFields = 4,
	BfReflectKind_DefaultConstructor = 8,
	BfReflectKind_Constructors = 0x10,
	BfReflectKind_StaticMethods = 0x20,
	BfReflectKind_Methods = 0x40,
	BfReflectKind_DynamicBoxing = 0x80,
	BfReflectKind_User = 0x100,
	BfReflectKind_All = 0x1FF,

	BfReflectKind_ApplyToInnerTypes = 0x200
};

class BfTypeOptions
{
public:
	struct MethodFilter
	{
		String mFilter;
		BfOptionFlags mOrFlags;
		BfOptionFlags mAndFlags;
	};

public:
	Array<String> mTypeFilters;
	Array<String> mAttributeFilters;
	Array<int> mMatchedIndices;
	int mSIMDSetting;
	int mOptimizationLevel;
	int mEmitDebugInfo;
	BfOptionFlags mAndFlags;
	BfOptionFlags mOrFlags;
	Array<MethodFilter> mReflectMethodFilters;
	Array<MethodFilter> mReflectMethodAttributeFilters;
	int mAllocStackTraceDepth;

public:
	static int Apply(int val, int applyVal)
	{
		if (applyVal != -1)
			return applyVal;
		return val;
	}

	bool Apply(bool val, BfOptionFlags flags)
	{
		if (val)
			return (mAndFlags & flags) != 0;
		else
			return (mOrFlags & flags) != 0;
	}

	bool HasReflectMethodFilters()
	{
		return !mReflectMethodFilters.IsEmpty() || !mReflectMethodAttributeFilters.IsEmpty();
	}
};

enum BfFindTypeDefFlags
{
	BfFindTypeDefFlag_None,
	BfFindTypeDefFlag_AllowGlobal
};

class BfSystem
{
public:
	int mPtrSize;
	bool mIsResolveOnly;

	CritSect mDataLock; // short-lived, hold only while active modifying data
	// The following are protected by mDataLock:
	HashSet<String> mUsedSafeProjectNames;
	Array<BfProject*> mProjects;
	Array<BfProject*> mProjectDeleteQueue;
	Array<BfParser*> mParsers;
	Array<BfParser*> mParserDeleteQueue;
	Array<BfTypeDef*> mTypeDefDeleteQueue;
	Array<BfTypeOptions> mTypeOptions;
	Array<BfTypeOptions> mMergedTypeOptions;
	int mUpdateCnt;
	bool mWorkspaceConfigChanged;
	Val128 mWorkspaceConfigHash;

	Array<BfCompiler*> mCompilers;

	BfAtom* mGlobalsAtom;
	BfAtom* mEmptyAtom;
	BfAtom* mBfAtom;
	CritSect mSystemLock; // long-lived, hold while compiling
	int mYieldDisallowCount; // We can only yield lock when we are at 0
	volatile int mCurSystemLockPri;
    BfpThreadId mCurSystemLockThreadId;
	volatile int mPendingSystemLockPri;
	uint32 mYieldTickCount;
	int mHighestYieldTime;
	// The following are protected by mSystemLock - can only be accessed by the compiling thread
	Dictionary<String, BfTypeDef*> mSystemTypeDefs;
	BfTypeDefMap mTypeDefs;
	bool mNeedsTypesHandledByCompiler;
	BumpAllocator mAlloc;
	int mAtomCreateIdx;
	Dictionary<StringView, BfAtom*> mAtomMap;
	Array<BfAtom*> mAtomGraveyard;
	uint32 mAtomUpdateIdx;
	int32 mTypeMapVersion; // Increment when we add any new types or namespaces

	OwnedVector<BfMethodDef> mMethodGraveyard;
	OwnedVector<BfFieldDef> mFieldGraveyard;
	OwnedVector<BfDirectStrTypeReference> mDirectTypeRefs;
	OwnedVector<BfRefTypeRef> mRefTypeRefs;

public:
	BfTypeDef* mTypeVoid;
	BfTypeDef* mTypeNullPtr;
	BfTypeDef* mTypeSelf;
	BfTypeDef* mTypeDot;
	BfTypeDef* mTypeVar;
	BfTypeDef* mTypeLet;
	BfTypeDef* mTypeBool;
	BfTypeDef* mTypeIntPtr;
	BfTypeDef* mTypeUIntPtr;
	BfTypeDef* mTypeIntUnknown;
	BfTypeDef* mTypeUIntUnknown;
	BfTypeDef* mTypeInt8;
	BfTypeDef* mTypeUInt8;
	BfTypeDef* mTypeInt16;
	BfTypeDef* mTypeUInt16;
	BfTypeDef* mTypeInt32;
	BfTypeDef* mTypeUInt32;
	BfTypeDef* mTypeInt64;
	BfTypeDef* mTypeUInt64;
	BfTypeDef* mTypeChar8;
	BfTypeDef* mTypeChar16;
	BfTypeDef* mTypeChar32;
	BfTypeDef* mTypeSingle;
	BfTypeDef* mTypeDouble;

	BfDirectStrTypeReference* mDirectVoidTypeRef;
	BfDirectStrTypeReference* mDirectBoolTypeRef;
	BfDirectStrTypeReference* mDirectSelfTypeRef;
	BfDirectStrTypeReference* mDirectSelfBaseTypeRef;
	BfRefTypeRef* mDirectRefSelfBaseTypeRef;
	BfDirectStrTypeReference* mDirectObjectTypeRef;
	BfDirectStrTypeReference* mDirectStringTypeRef;
	BfDirectStrTypeReference* mDirectIntTypeRef;
	BfRefTypeRef* mDirectRefIntTypeRef;
	BfDirectStrTypeReference* mDirectInt32TypeRef;

public:
	BfSystem();
	~BfSystem();

	BfAtom* GetAtom(const StringImpl& string);
	BfAtom* FindAtom(const StringImpl& string); // Doesn't create a ref
	BfAtom* FindAtom(const StringView& string); // Doesn't create a ref
	void ReleaseAtom(BfAtom* atom);
	void ProcessAtomGraveyard();
	void RefAtomComposite(const BfAtomComposite& atomComposite);
	void ReleaseAtomComposite(const BfAtomComposite& atomComposite);
	void SanityCheckAtomComposite(const BfAtomComposite& atomComposite);
	void TrackName(BfTypeDef* typeDef);
	void UntrackName(BfTypeDef* typeDef);

	bool ParseAtomComposite(const StringView& name, BfAtomComposite& composite, bool addRefs = false);

	void CreateBasicTypes();
	bool DoesLiteralFit(BfTypeCode typeCode, int64 value);
	bool DoesLiteralFit(BfTypeCode typeCode, uint64 value);
	bool DoesLiteralFit(BfTypeCode typeCode, const BfVariant& variant);
	BfParser* CreateParser(BfProject* bfProject);
	BfCompiler* CreateCompiler(bool isResolveOnly);
	BfProject* GetProject(const StringImpl& projName);

	BfTypeReference* GetTypeRefElement(BfTypeReference* typeRef);
	BfTypeDef* FilterDeletedTypeDef(BfTypeDef* typeDef);
	bool CheckTypeDefReference(BfTypeDef* typeDef, BfProject* project);
	BfTypeDef* FindTypeDef(const BfAtomComposite& findName, int numGenericArgs = 0, BfProject* project = NULL, const Array<BfAtomComposite>& namespaceSearch = Array<BfAtomComposite>(), BfTypeDef** ambiguousTypeDef = NULL, BfFindTypeDefFlags flags = BfFindTypeDefFlag_None);
	bool FindTypeDef(const BfAtomComposite& findName, int numGenericArgs, BfProject* project, const BfAtomComposite& checkNamespace, bool allowPrivate, BfTypeDefLookupContext* ctx);
	BfTypeDef* FindTypeDef(const StringImpl& typeName, int numGenericArgs = 0, BfProject* project = NULL, const Array<BfAtomComposite>& namespaceSearch = Array<BfAtomComposite>(), BfTypeDef** ambiguousTypeDef = NULL, BfFindTypeDefFlags flags = BfFindTypeDefFlag_None);
	BfTypeDef* FindTypeDef(const StringImpl& typeName, BfProject* project);
	BfTypeDef* FindTypeDefEx(const StringImpl& typeName);
	void ClearTypeDefCache();
	void FindFixitNamespaces(const StringImpl& typeName, int numGenericArgs, BfProject* project, std::set<String>& fixitNamespaces);

	void RemoveTypeDef(BfTypeDef* typeDef);
	//BfTypeDefMap::Iterator RemoveTypeDef(BfTypeDefMap::Iterator typeDefItr);
	void AddNamespaceUsage(const BfAtomComposite& namespaceStr, BfProject* bfProject);
	void RemoveNamespaceUsage(const BfAtomComposite& namespaceStr, BfProject* bfProject);
	bool ContainsNamespace(const BfAtomComposite& namespaceStr, BfProject* bfProject);
	void InjectNewRevision(BfTypeDef* typeDef);
	void AddToCompositePartial(BfPassInstance* passInstance, BfTypeDef* compositeTypeDef, BfTypeDef* partialTypeDef);
	void FinishCompositePartial(BfTypeDef* compositeTypeDef);
	void CopyTypeDef(BfTypeDef* typeDef, BfTypeDef* nextTypeDef);
	void UpdateEmittedTypeDef(BfTypeDef* typeDef);

	BfTypeDef* GetCombinedPartial(BfTypeDef* typeDef);
	BfTypeDef* GetOuterTypeNonPartial(BfTypeDef* typeDef);

	int GetGenericParamIdx(const Array<BfGenericParamDef*>& genericParams, const StringImpl& name);
	int GetGenericParamIdx(const Array<BfGenericParamDef*>& genericParams, BfTypeReference* typeRef);

	void StartYieldSection();
	void CheckLockYield(); // Yields to a higher priority request
	void SummarizeYieldSection();

	void NotifyWillRequestLock(int priority);
	void Lock(int priority);
	void Unlock();
	void AssertWeHaveLock();

	void RemoveDeletedParsers();
	void RemoveOldParsers();
	void RemoveOldData();

	void VerifyTypeDef(BfTypeDef* typeDef);

	BfPassInstance* CreatePassInstance();
	BfTypeOptions* GetTypeOptions(int optionsIdx);
	bool HasTestProjects();
	bool IsCompatibleCallingConvention(BfCallingConvention callConvA, BfCallingConvention callConvB);
};

class AutoDisallowYield
{
public:
	BfSystem* mSystem;
	bool mHeld;

public:
	AutoDisallowYield(BfSystem* system)
	{
		mSystem = system;
		mSystem->mYieldDisallowCount++;
		mHeld = true;
	}

	~AutoDisallowYield()
	{
		if (mHeld)
			mSystem->mYieldDisallowCount--;
	}

	void Release()
	{
		BF_ASSERT(mHeld);
		if (mHeld)
		{
			mHeld = false;
			mSystem->mYieldDisallowCount--;
		}
	}

	void Acquire()
	{
		BF_ASSERT(!mHeld);
		if (!mHeld)
		{
			mHeld = true;
			mSystem->mYieldDisallowCount++;
		}
	}
};

#ifdef _DEBUG

#ifdef BF_PLATFORM_WINDOWS
#define BF_WANTS_LOG_HI
#define BF_WANTS_LOG
#define BF_WANTS_LOG_SYS
//#define BF_WANTS_LOG2
//#define BF_WANTS_LOG_CLANG
#define BF_WANTS_LOG_DBG
#define BF_WANTS_LOG_DBGEXPR
//#define BF_WANTS_LOG_CV
#endif

#else
#define BF_WANTS_LOG_HI
//#define BF_WANTS_LOG
//#define BF_WANTS_LOG_SYS
//#define BF_WANTS_LOG2
//#define BF_WANTS_LOG_CLANG
//#define BF_WANTS_LOG_DBGEXPR
//#define BF_WANTS_LOG_CV
//#define BF_WANTS_LOG_DBG
#endif

#ifdef BF_WANTS_LOG
#define BfLog(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
//#define BfLog(fmt) {} // Nothing
#define BfLog(fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG
#define BfLogX(logIdx, fmt, ...) DoBfLog(logIdx, fmt, ##__VA_ARGS__)
#else
#define BfLogX(logIdx, fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_SYS
#define BfLogSys(sys, fmt, ...) DoBfLog((sys)->mIsResolveOnly ? 1 : 2, fmt, ##__VA_ARGS__)
#define BfLogSysM(fmt, ...) DoBfLog(mSystem->mIsResolveOnly ? 1 : 2, fmt, ##__VA_ARGS__)
#else
#define BfLogSys(...) {} // Nothing
#define BfLogSysM(...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_HI
#define BfLogSysHI(sys, fmt, ...) DoBfLog((sys)->mIsResolveOnly ? 1 : 2, fmt, ##__VA_ARGS__)
#define BfLogSysMHI(fmt, ...) DoBfLog(mSystem->mIsResolveOnly ? 1 : 2, fmt, ##__VA_ARGS__)
#define BfLogDbgHI(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLogSysHI(...) {} // Nothing
#define BfLogSysMHI(...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_CLANG
//#define BfLogClang(fmt) DoBfLog(fmt)
#define BfLogClang(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLogClang(fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_DBG
#define BfLogDbg(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLogDbg(fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_DBGEXPR
#define BfLogDbgExpr(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLogDbgExpr(fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG2
#define BfLog2(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLog2(fmt, ...) {} // Nothing
#endif

#ifdef BF_WANTS_LOG_CV
#define BfLogCv(fmt, ...) DoBfLog(0, fmt, ##__VA_ARGS__)
#else
#define BfLogCv(fmt, ...) {} // Nothing
#endif

void DoBfLog(int fileIdx, const char* fmt ...);

NS_BF_END

namespace std
{
	template <>
	struct hash<Beefy::BfAtomComposite>
	{
		size_t operator()(const Beefy::BfAtomComposite& composite) const
		{
			int curHash = 0;
			for (int i = 0; i < (int)composite.mSize; i++)
				curHash = ((curHash ^ (int)(intptr)composite.mParts[i]->mHash) << 5) - curHash;
			return curHash;
		}
	};

	template <>
	struct hash<Beefy::BfMemberSetEntry>
	{
		size_t operator()(const Beefy::BfMemberSetEntry& entry) const
		{
			return std::hash<Beefy::String>()(entry.mMemberDef->mName);
		}
	};
}

namespace std
{
	template<>
	struct hash<Beefy::BfErrorEntry>
	{
		size_t operator()(const Beefy::BfErrorEntry& val) const
		{
			return val.GetHashCode();
		}
	};
}