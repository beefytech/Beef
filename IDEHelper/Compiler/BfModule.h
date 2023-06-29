#pragma once

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/SizedArray.h"
#include "BeefySysLib/Span.h"
#include "BfSourceClassifier.h"
#include "BfAst.h"
#include "BfSystem.h"
#include "BfIRBuilder.h"
#include "BfResolvedTypeUtils.h"
#include "BfUtil.h"
#include <unordered_set>
#include <functional>

#pragma warning(pop)

NS_BF_BEGIN

class BfType;
class BfResolvedType;
class BfExprEvaluator;
class CeEmitContext;
class CeDbgState;
enum BfCeTypeEmitSourceKind : int8;

enum BfPopulateType
{
	BfPopulateType_TypeDef,
	BfPopulateType_Identity,
	BfPopulateType_IdentityNoRemapAlias,
	BfPopulateType_Declaration,
	BfPopulateType_BaseType,
	BfPopulateType_Interfaces_Direct,
	BfPopulateType_AllowStaticMethods,
	BfPopulateType_Interfaces_All,
	BfPopulateType_Data_Soft,
	BfPopulateType_Data,
	BfPopulateType_DataAndMethods,
	BfPopulateType_Full = BfPopulateType_DataAndMethods,

	BfPopulateType_Full_Force
};

enum BfEvalExprFlags : int64
{
	BfEvalExprFlags_None = 0,
	BfEvalExprFlags_ExplicitCast = 1,
	BfEvalExprFlags_NoCast = 2,
	BfEvalExprFlags_NoValueAddr = 4,
	BfEvalExprFlags_PropogateNullConditional = 8,
	BfEvalExprFlags_IgnoreNullConditional = 0x10,
	BfEvalExprFlags_AllowSplat = 0x20,
	BfEvalExprFlags_AllowEnumId = 0x40,
	BfEvalExprFlags_AllowIntUnknown = 0x80,
	BfEvalExprFlags_CreateConditionalScope = 0x100,
	BfEvalExprFlags_PendingPropSet = 0x200,
	BfEvalExprFlags_AllowParamsExpr = 0x400,
	BfEvalExprFlags_AllowRefExpr = 0x800,
	BfEvalExprFlags_AllowOutExpr = 0x1000,
	BfEvalExprFlags_FieldInitializer = 0x2000,
	BfEvalExprFlags_VariableDeclaration = 0x4000,
	BfEvalExprFlags_NoAutoComplete = 0x8000,
	BfEvalExprFlags_AllowNonConst = 0x10000,
	BfEvalExprFlags_StringInterpolateFormat = 0x20000,
	BfEvalExprFlags_NoLookupError = 0x40000,
	BfEvalExprFlags_Comptime = 0x80000,
	BfEvalExprFlags_DisallowComptime = 0x100000,
	BfEvalExprFlags_InCascade = 0x200000,
	BfEvalExprFlags_InferReturnType = 0x400000,
	BfEvalExprFlags_WasMethodRef = 0x800000,
	BfEvalExprFlags_DeclType = 0x1000000,
	BfEvalExprFlags_AllowBase = 0x2000000,
	BfEvalExprFlags_NoCeRebuildFlags = 0x4000000,
	BfEvalExprFlags_FromConversionOp = 0x8000000,
	BfEvalExprFlags_FromConversionOp_Explicit = 0x10000000,
	BfEvalExprFlags_AllowGenericConstValue = 0x20000000,
	BfEvalExprFlags_IsExpressionBody = 0x40000000,
	BfEvalExprFlags_AppendFieldInitializer = 0x80000000,
	BfEvalExprFlags_NameOf = 0x100000000LL,
	BfEvalExprFlags_NameOfSuccess = 0x200000000LL,

	BfEvalExprFlags_InheritFlags = BfEvalExprFlags_NoAutoComplete | BfEvalExprFlags_Comptime | BfEvalExprFlags_DeclType
};

enum BfCastFlags
{
	BfCastFlags_None = 0,
	BfCastFlags_Explicit = 1,
	BfCastFlags_Unchecked = 2,
	BfCastFlags_Internal = 4,
	BfCastFlags_SilentFail = 8,
	BfCastFlags_NoBox = 0x10,
	BfCastFlags_NoBoxDtor = 0x20,
	BfCastFlags_NoInterfaceImpl = 0x40,
	BfCastFlags_NoConversionOperator = 0x80,
	BfCastFlags_FromCompiler = 0x100, // Not user specified
	BfCastFlags_Force = 0x200,
	BfCastFlags_PreferAddr = 0x400,
	BfCastFlags_WarnOnBox = 0x800,
	BfCastFlags_IsCastCheck = 0x1000,
	BfCastFlags_IsConstraintCheck = 0x2000,
	BfCastFlags_WantsConst = 0x4000,
	BfCastFlags_FromComptimeReturn = 0x8000
};

enum BfCastResultFlags : int8
{
	BfCastResultFlags_None = 0,
	BfCastResultFlags_IsAddr = 1,
	BfCastResultFlags_IsTemp = 2
};

enum BfAllocFlags : int8
{
	BfAllocFlags_None = 0,
	BfAllocFlags_RawArray = 1,
	BfAllocFlags_ZeroMemory = 2,
	BfAllocFlags_NoDtorCall = 4,
	BfAllocFlags_NoDefaultToMalloc = 8
};

enum BfProtectionCheckFlags : int8
{
	BfProtectionCheckFlag_None = 0,
	BfProtectionCheckFlag_CheckedProtected = 1,
	BfProtectionCheckFlag_CheckedPrivate = 2,
	BfProtectionCheckFlag_AllowProtected = 4,
	BfProtectionCheckFlag_AllowPrivate = 8,
	BfProtectionCheckFlag_InstanceLookup = 0x10
};

enum BfEmbeddedStatementFlags : int8
{
	BfEmbeddedStatementFlags_None = 0,
	BfEmbeddedStatementFlags_IsConditional = 1,
	BfEmbeddedStatementFlags_IsDeferredBlock = 2,
	BfEmbeddedStatementFlags_Unscoped = 4
};

enum BfLocalVarAssignKind : int8
{
	BfLocalVarAssignKind_None = 0,
	BfLocalVarAssignKind_Conditional = 1,
	BfLocalVarAssignKind_Unconditional = 2
};

class BfLocalVariable
{
public:
	int64 mUnassignedFieldFlags;
	BfType* mResolvedType;
	BfIdentifierNode* mNameNode;
	String mName;
	BfIRValue mAddr;
	BfIRValue mConstValue;
	BfIRValue mValue;
	BfIRMDNode mDbgVarInst;
	BfIRValue mDbgDeclareInst;
	BfIRBlock mDeclBlock;
	int mLocalVarIdx; // Index in mLocals
	int mLocalVarId; // Unique Id for identification (does not get reused, unlike mLocalVarIdx)
	int mCompositeCount;
	int mWrittenToId;
	int mReadFromId;
	int mParamIdx;
	uint8 mNamePrefixCount;
	bool mIsThis;
	bool mHasLocalStructBacking;
	bool mIsStruct;
	bool mIsImplicitParam;
	bool mParamFailed;
	BfLocalVarAssignKind mAssignedKind;
	bool mHadExitBeforeAssign;
	bool mIsReadOnly;
	bool mIsStatic;
	bool mIsSplat;
	bool mIsLowered;
	bool mAllowAddr;
	bool mIsShadow;
	bool mUsedImplicitly; // Passed implicitly to a local method, capture by ref if we can
	bool mNotCaptured;
	bool mIsConst;
	bool mIsBumpAlloc;
	BfLocalVariable* mShadowedLocal;

public:
	BfLocalVariable()
	{
		mUnassignedFieldFlags = 0;
		mResolvedType = NULL;
		mNameNode = NULL;
		mLocalVarIdx = -1;
		mLocalVarId = -1;
		mCompositeCount = -1;
		mParamIdx = -2;
		mNamePrefixCount = 0;
		mIsThis = false;
		mHasLocalStructBacking = false;
		mIsStruct = false;
		mIsImplicitParam = false;
		mParamFailed = false;
		mAssignedKind = BfLocalVarAssignKind_None;
		mHadExitBeforeAssign = false;
		mWrittenToId = -1;
		mReadFromId = -1;
		mIsReadOnly = false;
		mIsStatic = false;
		mIsSplat = false;
		mIsLowered = false;
		mAllowAddr = false;
		mIsShadow = false;
		mUsedImplicitly = false;
		mNotCaptured = false;
		mIsConst = false;
		mIsBumpAlloc = false;
		mShadowedLocal = NULL;
	}

	bool IsParam()
	{
		return mParamIdx != -2;
	}
	void Init();
};

class BfMethodState;
class BfMixinState;
class BfClosureState;

class BfLocalMethod
{
public:
	BfSystem* mSystem;
	BfModule* mModule;
	BfSource* mSource;
	BfMethodDeclaration* mMethodDeclaration;
	String mExpectedFullName;
	String mMethodName;
	BfMethodDef* mMethodDef;
	BfLocalMethod* mOuterLocalMethod;
	BfMethodInstanceGroup* mMethodInstanceGroup;
	BfMethodInstance* mLambdaInvokeMethodInstance;
	BfLambdaBindExpression* mLambdaBindExpr;
	BfMethodState* mDeclMethodState;
	BfIRMDNode mDeclDIScope;
	BfMixinState* mDeclMixinState;
	OwnedVector<BfDirectTypeReference> mDirectTypeRefs;
	bool mDeclOnly;
	bool mDidBodyErrorPass;
	BfLocalMethod* mNextWithSameName;

public:
	BfLocalMethod()
	{
		mModule = NULL;
		mSystem = NULL;
		mSource = NULL;
		mMethodDeclaration = NULL;
		mMethodDef = NULL;
		mOuterLocalMethod = NULL;
		mMethodInstanceGroup = NULL;
		mLambdaInvokeMethodInstance = NULL;
		mLambdaBindExpr = NULL;
		mDeclMethodState = NULL;
		mDeclMixinState = NULL;
		mDeclOnly = false;
		mDidBodyErrorPass = false;
		mNextWithSameName = NULL;
	}
	~BfLocalMethod();
	void Dispose();
};

class BfDeferredCapture
{
public:
	String mName;
	BfTypedValue mValue;
};

class BfDeferredCallEntry
{
public:
	BfDeferredCallEntry* mNext;

	BfAstNode* mSrcNode;
	BfTypedValue mTarget;
	BfModuleMethodInstance mModuleMethodInstance;
	BfIRValue mDeferredAlloca;
	SizedArray<BfIRValue, 2> mOrigScopeArgs;
	SizedArray<BfIRValue, 2> mScopeArgs;
	Array<BfDeferredCapture> mCaptures;
	BfBlock* mDeferredBlock;
	BfAstNode* mEmitRefNode;
	int64 mBlockId;
	int mHandlerCount;
	bool mBypassVirtual;
	bool mDoNullCheck;
	bool mCastThis;
	bool mArgsNeedLoad;
	bool mIgnored;

	SLIList<BfDeferredCallEntry*> mDynList;
	BfIRValue mDynCallTail;

public:
	BfDeferredCallEntry()
	{
		mBypassVirtual = false;
		mDoNullCheck = false;
		mNext = NULL;
		mSrcNode = NULL;
		mDeferredBlock = NULL;
		mEmitRefNode = NULL;
		mBlockId = -1;
		mHandlerCount = 0;
		mArgsNeedLoad = false;
		mCastThis = false;
		mIgnored = false;
	}

	~BfDeferredCallEntry()
	{
		mDynList.DeleteAll();
	}

	bool IsDynList()
	{
		return (bool)mDynCallTail;
	}
};

struct BfDeferredHandler
{
	BfIRBlock mHandlerBlock;
	BfIRBlock mDoneBlock;
};

class BfScopeData;

struct BfAssignedLocal
{
	BfLocalVariable* mLocalVar;
	int mLocalVarField;
	BfLocalVarAssignKind mAssignKind;

	bool operator==(const BfAssignedLocal& second) const
	{
		return (mLocalVar == second.mLocalVar) && (mLocalVarField == second.mLocalVarField) && (mAssignKind == second.mAssignKind);
	}
};

// We use this structure in the case where we have multiple execution paths, then we merge the assigned variables together
//  So when we have "if (check) { a = 1; } else {a = 2; }" we can know that a IS definitely assigned afterwards
class BfDeferredLocalAssignData
{
public:
	BfScopeData* mScopeData;
	int mVarIdBarrier;
	SizedArray<BfAssignedLocal, 4> mAssignedLocals;
	bool mIsChained;
	BfDeferredLocalAssignData* mChainedAssignData;
	bool mHadFallthrough;
	bool mHadReturn;
	bool mHadBreak;
	bool mIsUnconditional;
	bool mIsIfCondition;
	bool mIfMayBeSkipped;
	bool mLeftBlock;
	bool mLeftBlockUncond;

public:
	BfDeferredLocalAssignData(BfScopeData* scopeData = NULL)
	{
		mScopeData = scopeData;
		mVarIdBarrier = -1;
		mHadFallthrough = false;
		mHadReturn = false;
		mHadBreak = false;
		mChainedAssignData = NULL;
		mIsChained = false;
		mIsUnconditional = false;
		mIsIfCondition = false;
		mIfMayBeSkipped = false;
		mLeftBlock = false;
		mLeftBlockUncond = false;
	}

	bool Contains(const BfAssignedLocal& val)
	{
		for (int i = 0; i < (int)mAssignedLocals.mSize; i++)
		{
			auto& check = mAssignedLocals[i];
			if ((check.mLocalVar == val.mLocalVar) && (check.mLocalVarField == val.mLocalVarField) && (check.mAssignKind >= val.mAssignKind))
				return true;
		}
		return false;
	}

	void ExtendFrom(BfDeferredLocalAssignData* outerLocalAssignData, bool doChain = false);
	void BreakExtendChain();
	void SetIntersection(const BfDeferredLocalAssignData& otherLocalAssignData);
	void Validate() const;
	void SetUnion(const BfDeferredLocalAssignData& otherLocalAssignData);
};

enum BfScopeKind
{
	BfScopeKind_Normal,
	BfScopeKind_StatementTarget,
	BfScopeKind_StatementTarget_Conditional,
};

class BfDeferredCallProcessorInstance
{
public:
	BfDeferredCallEntry* mDeferredCallEntry;
	BfIRBlock mProcessorBlock;
	BfIRBlock mContinueBlock;
};

// "Looped" means this scope will execute zero to many times, "Conditional" means zero or one.
// Looped and Conditional are mutually exclusive.  "Dyn" means Looped OR Conditional.
class BfScopeData
{
public:
	BfScopeData* mPrevScope;
	BfScopeKind mScopeKind;
	BfIRMDNode mDIScope;
	BfIRMDNode mDIInlinedAt;
	String mLabel;
	BfIdentifierNode* mLabelNode;
	int mLocalVarStart;
	int mScopeDepth;
	int mMixinDepth;
	int mScopeLocalId;
	bool mIsScopeHead; // For first scope data or for inlined start
	bool mIsLoop;
	bool mIsConditional; // Rarely set - usually we rely on OuterIsConditional or InnerIsConditional
	bool mOuterIsConditional;
	bool mInnerIsConditional;
	bool mHadOuterDynStack;
	bool mAllowTargeting;
	bool mHadScopeValueRetain;
	bool mIsDeferredBlock;
	bool mAllowVariableDeclarations;
	bool mInInitBlock;
	bool mSupressNextUnreachable;
	bool mInConstIgnore;
	bool mIsSharedTempBlock;
	bool mDone;
	BfMixinState* mMixinState;
	BfBlock* mAstBlock;
	BfAstNode* mCloseNode;
	BfExprEvaluator* mExprEvaluator;
	SLIList<BfDeferredCallEntry*> mDeferredCallEntries;
	Array<BfDeferredCallProcessorInstance> mDeferredCallProcessorInstances;
	BfIRValue mBlock;
	BfIRValue mValueScopeStart;
	BfIRValue mSavedStack;
	Array<BfIRValue> mSavedStackUses;
	Array<BfDeferredHandler> mDeferredHandlers; // These get cleared when us our a parent gets new entries added into mDeferredCallEntries
	Array<BfIRBlock> mAtEndBlocks; // Move these to the end after we close scope
	Array<BfIRValue> mDeferredLifetimeEnds;
	BfDeferredLocalAssignData* mExitLocalAssignData;
	BfIRMDNode mAltDIFile;
	BfIRMDNode mAltDIScope;

public:
	BfScopeData()
	{
		mScopeKind = BfScopeKind_Normal;
		mPrevScope = NULL;
		mLocalVarStart = 0;
		mLabelNode = NULL;
		mMixinState = NULL;
		mAstBlock = NULL;
		mCloseNode = NULL;
		mExprEvaluator = NULL;
		mIsScopeHead = false;
		mIsLoop = false;
		mIsConditional = false;
		mOuterIsConditional = false;
		mInnerIsConditional = false;
		mHadOuterDynStack = false;
		mHadScopeValueRetain = false;
		mIsDeferredBlock = false;
		mSupressNextUnreachable = false;
		mAllowTargeting = true;
		mAllowVariableDeclarations = true;
		mInInitBlock = false;
		mInConstIgnore = false;
		mIsSharedTempBlock = false;
		mDone = false;
		mMixinDepth = 0;
		mScopeDepth = 0;
		mScopeLocalId = -1;
		mExitLocalAssignData = NULL;
	}

	~BfScopeData()
	{
		mDeferredCallEntries.DeleteAll();
		delete mExitLocalAssignData;
	}

	BfScopeData* GetHead()
	{
		auto checkScope = this;
		while (!checkScope->mIsScopeHead)
			checkScope = checkScope->mPrevScope;
		return checkScope;
	}

	BfScopeData* GetTargetable()
	{
		if (!mAllowTargeting)
			return mPrevScope->GetTargetable();
		return this;
	}

	bool IsLooped(BfScopeData* scopeData)
	{
		auto checkScope = this;
		while (checkScope != NULL)
		{
			if (checkScope->mIsLoop)
				return true;
			if (checkScope == scopeData)
				break;
			checkScope = checkScope->mPrevScope;
		}
		return false;
	}

	bool IsDyn(BfScopeData* scopeData)
	{
		auto checkScope = this;
		// Scoping to a loop is dynamic - it doesn't have to cross the loop boundary.
		//  Scoping to a loop _body_ is not necessarily dynamic, however.
		while (checkScope != NULL)
		{
			if (checkScope->mIsConditional)
				return true;
			if ((checkScope->mIsLoop) || (checkScope->mInnerIsConditional))
				return true;
			if (checkScope == scopeData)
				break;
			if (checkScope->mOuterIsConditional)
				return true;
			checkScope = checkScope->mPrevScope;
		}
		return false;
	}

	bool CrossesMixin(BfScopeData* scopeData)
	{
		// Check for a transition for having an inlinedAt to not having one
		if (!mDIInlinedAt)
			return false;
		auto checkScope = this;
		while (checkScope != scopeData)
		{
			checkScope = checkScope->mPrevScope;
			if (!checkScope->mDIInlinedAt)
				return true;
		}
		return false;
	}

	void ClearHandlers(BfScopeData* scopeData)
	{
		auto checkScope = this;
		while (true)
		{
			checkScope->mDeferredHandlers.Clear();
			if (checkScope == scopeData)
				break;
			checkScope = checkScope->mPrevScope;
		}
	}

	int GetDepth()
	{
		int depth = 0;
		auto checkScopeData = this;
		while (true)
		{
			checkScopeData = checkScopeData->mPrevScope;
			if (checkScopeData == NULL)
				break;
			depth++;
		}
		return depth;
	}

	bool ExtendLifetime(BfIRValue irValue)
	{
		if (mDeferredLifetimeEnds.Remove(irValue))
		{
			if (mPrevScope != NULL)
				mPrevScope->mDeferredLifetimeEnds.Add(irValue);
			return true;
		}
		return false;
	}
};

struct BfCaptureInfo
{
public:
	struct Entry
	{
		BfCaptureType mCaptureType;
		bool mUsed;
		BfAstNode* mRefNode;
		BfAstNode* mNameNode;

		Entry()
		{
			mCaptureType = BfCaptureType_Copy;
			mUsed = false;
			mRefNode = NULL;
			mNameNode = NULL;
		}
	};

public:
	Array<Entry> mCaptures;
};

class BfAllocTarget
{
public:
	BfScopeData* mScopeData;
	BfAstNode* mRefNode;
	BfTypedValue mCustomAllocator;
	BfScopedInvocationTarget* mScopedInvocationTarget;
	int mAlignOverride;
	BfCaptureInfo* mCaptureInfo;
	bool mIsFriend;

public:
	BfAllocTarget()
	{
		mScopeData = NULL;
		mRefNode = NULL;
		mCustomAllocator = NULL;
		mScopedInvocationTarget = NULL;
		mAlignOverride = -1;
		mIsFriend = false;
		mCaptureInfo = NULL;
	}

	BfAllocTarget(BfScopeData* scopeData)
	{
		mScopeData = scopeData;
		mRefNode = NULL;
		mCustomAllocator = NULL;
		mScopedInvocationTarget = NULL;
		mAlignOverride = -1;
	}

	BfAllocTarget(const BfTypedValue& customAllocator, BfAstNode* refNode)
	{
		mScopeData = NULL;
		mCustomAllocator = customAllocator;
		mRefNode = NULL;
		mScopedInvocationTarget = NULL;
		mAlignOverride = -1;
	}
};

class BfBreakData
{
public:
	BfBreakData* mPrevBreakData;
	BfScopeData* mScope;
	BfIRBlock mIRContinueBlock;
	BfIRBlock mIRBreakBlock;
	BfIRBlock mIRFallthroughBlock;
	BfIRValue mInnerValueScopeStart;
	bool mHadBreak;

public:
	BfBreakData()
	{
		mPrevBreakData = NULL;
		mScope = NULL;
		mHadBreak = false;
	}
};

class BfMixinRecord
{
public:
	BfAstNode * mSource;
};

class BfDeferredLocalMethod
{
public:
	BfLocalMethod* mLocalMethod;
	BfMethodInstance* mMethodInstance;
	Array<BfLocalMethod*> mLocalMethods; // Local methods that were in scope at the time
	Array<BfLocalVariable> mConstLocals;
	Array<BfMixinRecord> mMixinStateRecords;
};

enum BfReturnTypeInferState
{
	BfReturnTypeInferState_None,
	BfReturnTypeInferState_Inferring,
	BfReturnTypeInferState_Fail,
};

class BfClosureState
{
public:
	bool mCapturing;
	bool mCaptureVisitingBody;
	int mCaptureStartAccessId;
	// When we need to look into another local method to determine captures, but we don't want to process local variable declarations or cause infinite recursion
	bool mBlindCapturing;
	bool mDeclaringMethodIsMutating;
	bool mCapturedDelegateSelf;
	BfReturnTypeInferState mReturnTypeInferState;
	BfLocalMethod* mLocalMethod;
	BfClosureInstanceInfo* mClosureInstanceInfo;
	BfMethodDef* mClosureMethodDef;
	BfType* mReturnType;
	BfTypeInstance* mDelegateType;
	BfTypeInstance* mClosureType;
	BfDeferredLocalMethod* mActiveDeferredLocalMethod;
	Array<BfLocalVariable> mConstLocals; // Locals not inserted into the captured 'this'
	HashSet<BfFieldInstance*> mReferencedOuterClosureMembers;
	HashSet<BfMethodInstance*> mLocalMethodRefSet;
	Array<BfMethodInstance*> mLocalMethodRefs;
	Array<BfMethodInstance*> mDeferredProcessLocalMethods;

public:
	BfClosureState()
	{
		mClosureMethodDef = NULL;
		mLocalMethod = NULL;
		mClosureInstanceInfo = NULL;
		mCapturing = false;
		mCaptureVisitingBody = false;
		mCaptureStartAccessId = -1;
		mBlindCapturing = false;
		mDeclaringMethodIsMutating = false;
		mCapturedDelegateSelf = false;
		mReturnTypeInferState = BfReturnTypeInferState_None;
		mActiveDeferredLocalMethod = NULL;
		mReturnType = NULL;
		mDelegateType = NULL;
		mClosureType = NULL;
	}
};

class BfIteratorClassState
{
public:
	BfTypeInstance* mIteratorClass;
	bool mCapturing;

public:
	BfIteratorClassState()
	{
		mCapturing = false;
	}
};

class BfPendingNullConditional
{
public:
	BfIRBlock mPrevBB;
	BfIRBlock mCheckBB;
	BfIRBlock mDoneBB;
	SizedArray<BfIRBlock, 4> mNotNullBBs;
};

class BfAttributeState
{
public:
	enum Flags
	{
		Flag_None,
		Flag_StopOnError = 1,
		Flag_HadError = 2
	};

public:
	Flags mFlags;
	BfAstNode* mSrc;
	BfAttributeTargets mTarget;
	BfCustomAttributes* mCustomAttributes;
	bool mUsed;

	BfAttributeState()
	{
		mSrc = NULL;
		mFlags = Flag_None;
		mTarget = BfAttributeTargets_None;
		mCustomAttributes = NULL;
		mUsed = false;
	}

	~BfAttributeState()
	{
		if (mCustomAttributes != NULL)
			delete mCustomAttributes;
	}
};

class BfMixinState
{
public:
	BfMixinState* mPrevMixinState;
	BfAstNode* mSource;
	BfScopeData* mCallerScope;
	BfScopeData* mTargetScope; // Equals caller scope unless user explicitly calls specifies scope override
	BfFilePosition mInjectFilePosition;
	BfMethodInstance* mMixinMethodInstance;
	BfAstNode* mResultExpr;
	SizedArray<BfType*, 8> mArgTypes;
	SizedArray<BfIRValue, 8> mArgConsts;
	int mLocalsStartIdx;
	bool mUsedInvocationScope;
	bool mHasDeferredUsage;
	bool mCheckedCircularRef;
	bool mDoCircularVarResult;
	bool mUseMixinGenerics;
	BfTypedValue mTarget;
	int mLastTargetAccessId;

public:
	BfMixinState()
	{
		mPrevMixinState = NULL;
		mSource = NULL;
		mCallerScope = NULL;
		mTarget = NULL;
		mMixinMethodInstance = NULL;
		mResultExpr = NULL;
		mLocalsStartIdx = 0;
		mUsedInvocationScope = false;
		mHasDeferredUsage = false;
		mCheckedCircularRef = false;
		mDoCircularVarResult = false;
		mUseMixinGenerics = false;
		mLastTargetAccessId = -1;
	}

	BfMixinState* GetRoot()
	{
		auto curMixin = this;
		while (curMixin->mPrevMixinState != NULL)
			curMixin = curMixin->mPrevMixinState;
		return curMixin;
	}
};

class BfDeferredCallEmitState
{
public:
	BfAstNode* mCloseNode;

public:
	BfDeferredCallEmitState()
	{
		mCloseNode = NULL;
	}
};

class BfTypeLookupError
{
public:
	enum BfErrorKind
	{
		BfErrorKind_None,
		BfErrorKind_Ambiguous,
		BfErrorKind_Inaccessible
	};

public:
	BfErrorKind mErrorKind;
	BfAstNode* mRefNode;
	BfTypeDef* mAmbiguousTypeDef;

public:
	BfTypeLookupError()
	{
		mErrorKind = BfErrorKind_None;
		mRefNode = NULL;
		mAmbiguousTypeDef = NULL;
	}
};

/*struct BfSplatDecompHash
{
	size_t operator()(const std::pair<BfIRValue, int>& val) const
	{
		return (val.first.mId << 4) + val.second;
	}
};

struct BfSplatDecompEquals
{
	bool operator()(const std::pair<BfIRValue, int>& lhs, const std::pair<BfIRValue, int>& rhs) const
	{
		return (lhs.first.mFlags == rhs.first.mFlags) && (lhs.first.mId == rhs.first.mId) && (lhs.second == rhs.second);
	}
};*/

struct BfMethodRefHash
{
	size_t operator()(const BfMethodRef& val) const
	{
		if (val.mTypeInstance == NULL)
			return 0;
		return val.mTypeInstance->mTypeId ^ (val.mMethodNum << 10);
	}
};

class BfConstResolveState
{
public:
	BfMethodInstance* mMethodInstance;
	BfConstResolveState* mPrevConstResolveState;
	bool mInCalcAppend;
	bool mFailed;

	BfConstResolveState()
	{
		mMethodInstance = NULL;
		mPrevConstResolveState = NULL;
		mInCalcAppend = false;
		mFailed = false;
	}
};

struct BfLocalVarEntry
{
	BfLocalVariable* mLocalVar;

	BfLocalVarEntry(BfLocalVariable* localVar)
	{
		mLocalVar = localVar;
	}

	bool operator==(const BfLocalVarEntry& other) const
	{
		return mLocalVar->mName == other.mLocalVar->mName;
	}

	bool operator==(const StringImpl& name) const
	{
		return mLocalVar->mName == name;
	}
};

class BfLambdaCaptureInfo
{
public:
	String mName;
};

class BfLambdaInstance
{
public:
	BfTypeInstance* mDelegateTypeInstance;
	BfTypeInstance* mUseTypeInstance;
	BfClosureType* mClosureTypeInstance;
	BfMixinState* mDeclMixinState;
	BfTypeInstance* mOuterClosure;
	BfIRValue mClosureFunc;
	BfIRValue mDtorFunc;
	bool mCopyOuterCaptures;
	bool mDeclaringMethodIsMutating;
	bool mIsStatic;
	Array<BfLambdaCaptureInfo> mCaptures;
	BfMethodInstance* mMethodInstance;
	BfMethodInstance* mDtorMethodInstance;
	Array<BfLocalVariable> mConstLocals;
	OwnedVector<BfParameterDeclaration> mParamDecls;

public:
	BfLambdaInstance()
	{
		mDelegateTypeInstance = NULL;
		mUseTypeInstance = NULL;
		mClosureTypeInstance = NULL;
		mDeclMixinState = NULL;
		mOuterClosure = NULL;
		mCopyOuterCaptures = false;
		mDeclaringMethodIsMutating = false;
		mIsStatic = false;
		mMethodInstance = NULL;
		mDtorMethodInstance = NULL;
	}

	~BfLambdaInstance()
	{
		auto methodDef = mMethodInstance->mMethodDef;
		delete mMethodInstance;
		delete methodDef;

		if (mDtorMethodInstance != NULL)
		{
			auto methodDef = mDtorMethodInstance->mMethodDef;
			delete mDtorMethodInstance;
			delete methodDef;
		}
	}
};

class BfParentNodeEntry
{
public:
	BfAstNode* mNode;
	BfParentNodeEntry* mPrev;
};

class BfMethodState
{
public:
	enum TempKind
	{
		TempKind_None,
		TempKind_Static,
		TempKind_NonStatic
	};

public:
	BumpAllocator mBumpAlloc;
	BfMethodState* mPrevMethodState; // Only non-null for things like local methods
	BfConstResolveState* mConstResolveState;
	BfMethodInstance* mMethodInstance;
	BfHotDataReferenceBuilder* mHotDataReferenceBuilder;
	BfIRFunction mIRFunction;
	BfIRBlock mIRHeadBlock;
	BfIRBlock mIRInitBlock;
	BfIRBlock mIREntryBlock;
	Array<BfLocalVariable*, AllocatorBump<BfLocalVariable*> > mLocals;
	HashSet<BfLocalVarEntry, AllocatorBump<BfLocalVariable*> > mLocalVarSet;
	Array<BfLocalMethod*> mLocalMethods;
	Dictionary<String, BfLocalMethod*> mLocalMethodMap;
	Dictionary<String, BfLocalMethod*> mLocalMethodCache; // So any lambda 'capturing' and 'processing' stages use the same local method
	Array<BfDeferredLocalMethod*> mDeferredLocalMethods;
	OwnedVector<BfMixinState> mMixinStates;
	Dictionary<BfAstNodeList, BfLambdaInstance*> mLambdaCache;
	Array<BfLambdaInstance*> mDeferredLambdaInstances;
	Array<BfIRValue> mSplatDecompAddrs;
	BfDeferredLocalAssignData* mDeferredLocalAssignData;
	BfProjectSet mVisibleProjectSet;
	int mDeferredLoopListCount;
	int mDeferredLoopListEntryCount;
	HashSet<int> mSkipObjectAccessChecks; // Indexed by BfIRValue value id

	Dictionary<int64, BfType*>* mGenericTypeBindings;

	BfIRMDNode mDIFile;
	bool mInHeadScope; // Is in starting scope of code on entry, controls mStackAllocUncondCount
	BfTypedValue mRetVal;
	BfIRValue mRetValAddr;
	int mCurAppendAlign;
	BfIRValue mDynStackRevIdx; // Increments when we restore the stack, which can invalidate dynSize for dynamic looped allocs
	BfIRBlock mIRExitBlock;
	BfBreakData* mBreakData;
	int mBlockNestLevel; // 0 = top level
	bool mIgnoreObjectAccessCheck;
	bool mDisableChecks;
	BfMixinState* mMixinState;
	BfClosureState* mClosureState;
	BfDeferredCallEmitState* mDeferredCallEmitState;
	BfIteratorClassState* mIteratorClassState;
	BfPendingNullConditional* mPendingNullConditional;
	BfTypeOptions* mMethodTypeOptions; // for [Options] attribute
	BfIRMDNode mDIRetVal;

	BfScopeData mHeadScope;
	BfScopeData* mCurScope;
	BfScopeData* mTailScope; // Usually equals mCurScope
	BfScopeData* mOverrideScope;
	BfAstNode* mEmitRefNode;
	TempKind mTempKind; // Used for var inference, etc
	bool mInDeferredBlock;
	bool mHadReturn;
	bool mHadContinue;
	bool mMayNeedThisAccessCheck;
	bool mLeftBlockUncond; // Definitely left block. mHadReturn also sets mLeftBlock
	bool mLeftBlockCond; // May have left block.
	bool mInPostReturn; // Unreachable code
	bool mCrossingMixin; // ie: emitting dtors in response to a return in a mixin
	bool mNoBind;
	bool mInConditionalBlock; // IE: RHS of ((A) && (B)), indicates an allocation in 'B' won't be dominated by a dtor, for example
	bool mAllowUinitReads;
	bool mDisableReturns;
	bool mCancelledDeferredCall;
	bool mNoObjectAccessChecks;
	int mCurLocalVarId; // Can also refer to a label
	int mCurAccessId; // For checking to see if a block reads from or writes to a local

public:
	BfMethodState()
	{
		mLocals.mAlloc = &mBumpAlloc;
		mLocalVarSet.mAlloc = &mBumpAlloc;

		mMethodInstance = NULL;
		mPrevMethodState = NULL;
		mConstResolveState = NULL;
		mHotDataReferenceBuilder = NULL;
		mHeadScope.mIsScopeHead = true;
		mCurScope = &mHeadScope;
		mTailScope = &mHeadScope;
		mEmitRefNode = NULL;
		mOverrideScope = NULL;
		mHadReturn = false;
		mLeftBlockUncond = false;
		mLeftBlockCond = false;
		mHadContinue = false;
		mMayNeedThisAccessCheck = false;
		mTempKind = TempKind_None;
		mInHeadScope = true;
		mBreakData = NULL;
		mBlockNestLevel = 0;
		mInPostReturn = false;
		mCrossingMixin = false;
		mNoBind = false;
		mIgnoreObjectAccessCheck = false;
		mDisableChecks = false;
		mInConditionalBlock = false;
		mAllowUinitReads = false;
		mDisableReturns = false;
		mCancelledDeferredCall = false;
		mNoObjectAccessChecks = false;
		mInDeferredBlock = false;
		mDeferredLocalAssignData = NULL;
		mCurLocalVarId = 0;
		mCurAccessId = 1;
		mCurAppendAlign = 0;
		mDeferredLoopListCount = 0;
		mDeferredLoopListEntryCount = 0;
		mClosureState = NULL;
		mDeferredCallEmitState = NULL;
		mIteratorClassState = NULL;

		mGenericTypeBindings = NULL;
		mMixinState = NULL;
		mPendingNullConditional = NULL;
		mMethodTypeOptions = NULL;
	}

	~BfMethodState();

	void AddScope(BfScopeData* newScopeData)
	{
		BF_ASSERT(newScopeData != mCurScope);

		mInHeadScope = false;
		newScopeData->mDIScope = mCurScope->mDIScope;
		newScopeData->mDIInlinedAt = mCurScope->mDIInlinedAt;
		newScopeData->mLocalVarStart = mCurScope->mLocalVarStart;
		newScopeData->mExprEvaluator = mCurScope->mExprEvaluator;
		newScopeData->mAltDIFile = mCurScope->mAltDIFile;
		newScopeData->mPrevScope = mCurScope;
		newScopeData->mMixinDepth = mCurScope->mMixinDepth;
		newScopeData->mScopeDepth = mCurScope->mScopeDepth + 1;
		newScopeData->mInConstIgnore = mCurScope->mInConstIgnore;
		mCurScope = newScopeData;
		mTailScope = mCurScope;
	}

	void SetHadReturn(bool hadReturn)
	{
		mHadReturn = hadReturn;
		if (mDeferredLocalAssignData != NULL)
			mDeferredLocalAssignData->mHadReturn = hadReturn;
	}

	BfMethodState* GetRootMethodState()
	{
		auto checkMethodState = this;
		while (checkMethodState->mPrevMethodState != NULL)
			checkMethodState = checkMethodState->mPrevMethodState;
		return checkMethodState;
	}

	BfMethodState* GetNonCaptureState()
	{
		//TODO: Why did this require mLocalMethod to not be null? That means lambda captures we're not crossed over
		auto checkMethodState = this;
		while ((checkMethodState->mPrevMethodState != NULL) && (checkMethodState->mClosureState != NULL) &&
			(checkMethodState->mClosureState->mCapturing) /*&& (checkMethodState->mClosureState->mLocalMethod != NULL)*/)
			checkMethodState = checkMethodState->mPrevMethodState;
		return checkMethodState;
	}

	BfMethodState* GetMethodStateForLocal(BfLocalVariable* localVar);

	bool InMainMixinScope()
	{
		if (mMixinState == NULL)
			return false;
		return mMixinState->mCallerScope == mCurScope->mPrevScope;
	}

	BfMixinState* GetRootMixinState()
	{
		BfMixinState* mixinState = mMixinState;
		while ((mixinState != NULL) && (mixinState->mPrevMixinState != NULL))
		{
			mixinState = mixinState->mPrevMixinState;
		}
		return mixinState;
	}

	BfAstNode* GetRootMixinSource()
	{
		BfMixinState* mixinState = NULL;
		auto checkMethodState = this;
		while (checkMethodState != NULL)
		{
			if (checkMethodState->mMixinState != NULL)
				mixinState = checkMethodState->mMixinState;

			if (checkMethodState->mClosureState != NULL)
			{
				auto activeLocalMethod = checkMethodState->mClosureState->mActiveDeferredLocalMethod;
				if (activeLocalMethod != NULL)
				{
					if (!activeLocalMethod->mMixinStateRecords.IsEmpty())
						return activeLocalMethod->mMixinStateRecords.back().mSource;
				}
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}

		if (mixinState != NULL)
			return mixinState->GetRoot()->mSource;
		return NULL;
	}

	bool HasMixin()
	{
		auto checkMethodState = this;
		while (checkMethodState != NULL)
		{
			if (checkMethodState->mMixinState != NULL)
				return true;
			checkMethodState = checkMethodState->mPrevMethodState;
		}
		return false;
	}

	bool HasNonStaticMixin()
	{
		auto checkMethodState = this;
		while (checkMethodState != NULL)
		{
			if ((checkMethodState->mMixinState != NULL) && (!checkMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mIsStatic))
				return true;
			checkMethodState = checkMethodState->mPrevMethodState;
		}
		return false;
	}

	void LocalDefined(BfLocalVariable* localVar, int fieldIdx = -1, BfLocalVarAssignKind assignKind = BfLocalVarAssignKind_None, bool isFromDeferredAssignData = false);
	void ApplyDeferredLocalAssignData(const BfDeferredLocalAssignData& deferredLocalAssignData);
	void Reset();

	int GetLocalStartIdx()
	{
		if (mMixinState != NULL)
			return mMixinState->mLocalsStartIdx;
		return 0;
	}

	bool IsTemporary()
	{
		return mTempKind != TempKind_None;
	}
};

class BfDeferredMethodCallData;

enum BfValueFlags
{
	BfValueFlags_None = 0,
	BfValueFlags_Boxed = 1,
};

enum BfBuiltInFuncType
{
	BfBuiltInFuncType_PrintF,
	BfBuiltInFuncType_Malloc,
	BfBuiltInFuncType_Free,
	BfBuiltInFuncType_LoadSharedLibraries,

	BfBuiltInFuncType_Count
};

// These are the options that can be applied to individual methods that cause AltModules
//  to be build, since they are exclusive to an LLVMModule; LLVM optimization-related
//  options always apply to entire LLVM modules
struct BfModuleOptions
{
public:
	BfSIMDSetting mSIMDSetting;
	int mEmitDebugInfo;
	BfOptLevel mOptLevel;

	bool operator==(const BfModuleOptions& other)
	{
		return (mSIMDSetting == other.mSIMDSetting) &&
			(mEmitDebugInfo == other.mEmitDebugInfo) &&
			(mOptLevel == other.mOptLevel);
	}

	bool operator!=(const BfModuleOptions& other)
	{
		return !(*this == other);
	}

	BfModuleOptions()
	{
		mSIMDSetting = BfSIMDSetting_None;
		mEmitDebugInfo = false;
		mOptLevel = BfOptLevel_NotSet;
	}
};

struct BfGenericParamSource
{
public:
	BfTypeInstance* mTypeInstance;
	BfMethodInstance* mMethodInstance;
	bool mCheckAccessibility;

public:
	BfGenericParamSource()
	{
		mTypeInstance = NULL;
		mMethodInstance = NULL;
		mCheckAccessibility = true;
	}

	BfGenericParamSource(BfTypeInstance* typeInstance)
	{
		mTypeInstance = typeInstance;
		mMethodInstance = NULL;
		mCheckAccessibility = true;
	}

	BfGenericParamSource(BfMethodInstance* methodInstance)
	{
		mTypeInstance = NULL;
		mMethodInstance = methodInstance;
		mCheckAccessibility = true;
	}

	BfTypeInstance* GetTypeInstance() const
	{
		if (mTypeInstance != NULL)
			return mTypeInstance;
		if (mMethodInstance != NULL)
			return mMethodInstance->GetOwner();
		return NULL;
	}
};

class BfAmbiguityContext
{
public:
	class Entry
	{
	public:
		BfTypeInterfaceEntry* mInterfaceEntry;
		int mMethodIdx;
		Array<BfMethodInstance*> mCandidates;
	};

public:
	BfModule* mModule;
	BfTypeInstance* mTypeInstance;
	bool mIsProjectSpecific;
	bool mIsReslotting;
	Dictionary<int, Entry> mEntries;

public:
	BfAmbiguityContext()
	{
		mModule = NULL;
		mTypeInstance = NULL;
		mIsProjectSpecific = false;
		mIsReslotting = false;
	}
	void Add(int id, BfTypeInterfaceEntry* ifaceEntry, int methodIdx, BfMethodInstance* candidateA, BfMethodInstance* candidateB);
	void Remove(int id);
	void Finish();
};

enum BfDefaultValueKind
{
	BfDefaultValueKind_Const,
	BfDefaultValueKind_Value,
	BfDefaultValueKind_Addr,
	BfDefaultValueKind_Undef
};

class BfModuleFileName
{
public:
	Array<BfProject*> mProjects;
	String mFileName;
	bool mModuleWritten;
	bool mWroteToLib;

	bool operator==(const BfModuleFileName& second) const
	{
		return (mProjects == second.mProjects) && (mFileName == second.mFileName);
	}
};

class BfGlobalLookup
{
public:
	enum Kind
	{
		Kind_All,
		Kind_Field,
		Kind_Method
	};

public:
	Kind mKind;
	String mName;
};

enum BfSrcPosFlags
{
	BfSrcPosFlag_None = 0,
	BfSrcPosFlag_Expression = 1,
	BfSrcPosFlag_NoSetDebugLoc = 2,
	BfSrcPosFlag_Force = 4
};

enum BfDeferredBlockFlags
{
	BfDeferredBlockFlag_None = 0,
	BfDeferredBlockFlag_BypassVirtual = 1,
	BfDeferredBlockFlag_DoNullChecks = 2,
	BfDeferredBlockFlag_SkipObjectAccessCheck = 4,
	BfDeferredBlockFlag_MoveNewBlocksToEnd = 8,
};

enum BfGetCustomAttributesFlags
{
	BfGetCustomAttributesFlags_None = 0,
	BfGetCustomAttributesFlags_AllowNonConstArgs = 1,
	BfGetCustomAttributesFlags_KeepConstsInModule = 2
};

class BfVDataExtEntry
{
public:
	BfTypeInstance* mDeclTypeInst;
	BfTypeInstance* mImplTypeInst;

	bool operator==(const BfVDataExtEntry& rhs)
	{
		return ((mDeclTypeInst == rhs.mDeclTypeInst) && (mImplTypeInst == rhs.mImplTypeInst));
	}
};

#define BFMODULE_FATAL(module, msg) (module)->FatalError((msg), __FILE__, __LINE__)

struct BfCEParseContext
{
	int mFailIdx;
	int mWarnIdx;
};

class BfModule : public BfStructuralVisitor
{
public:
	enum RebuildKind
	{
		RebuildKind_None,
		RebuildKind_SkipOnDemandTypes,
		RebuildKind_All
	};

public:
	Val128 mDataHash;

#ifdef _DEBUG
	StringT<128> mModuleName;
#else
	String mModuleName;
#endif
	Array<BfModuleFileName> mOutFileNames;
	// SpecializedModules contain method specializations with types that come from other projects
	Dictionary<Array<BfProject*>, BfModule*> mSpecializedMethodModules;
	BfModule* mParentModule;
	BfModule* mNextAltModule; // Linked
	BfModuleOptions* mModuleOptions; // Only in altModules

	BfSystem* mSystem;
	BfCompiler* mCompiler;
	BfContext* mContext;
	BfProject* mProject;
	BfIRType mStringLiteralType;

	BfTypeInstance* mCurTypeInstance;
	Dictionary<BfParserData*, BfFileInstance*> mFileInstanceMap;
	Dictionary<String, BfFileInstance*> mNamedFileInstanceMap;
	Array<BfTypeInstance*> mOwnedTypeInstances;

	Dictionary<int, BfIRValue> mStringObjectPool;
	Dictionary<int, BfIRValue> mStringCharPtrPool;
	Array<int> mStringPoolRefs;
	HashSet<int> mUnreifiedStringPoolRefs;

	Array<BfIRBuilder*> mPrevIRBuilders; // Before extensions
	BfIRBuilder* mBfIRBuilder;

	BfMethodState* mCurMethodState;
	BfAttributeState* mAttributeState;
	BfFilePosition mCurFilePosition;
	BfMethodInstance* mCurMethodInstance;
	BfParentNodeEntry* mParentNodeEntry;

	BfIRFunction mBuiltInFuncs[BfBuiltInFuncType_Count];

	Array<BfDllImportEntry> mDllImportEntries;
	Array<int> mImportFileNames;
	Dictionary<BfMethodRef, BfIRValue> mFuncReferences;
	Dictionary<BfFieldRef, BfIRValue> mStaticFieldRefs;
	Dictionary<BfTypeInstance*, BfIRValue> mInterfaceSlotRefs;
	Dictionary<BfTypeInstance*, BfIRValue> mClassVDataRefs;
	Dictionary<BfVDataExtEntry, BfIRValue> mClassVDataExtRefs;
	Dictionary<BfType*, BfIRValue> mTypeDataRefs;
	Dictionary<BfType*, BfIRValue> mDbgRawAllocDataRefs;
	Dictionary<BfMethodInstance*, BfDeferredMethodCallData*> mDeferredMethodCallData;
	HashSet<int64> mDeferredMethodIds;
	HashSet<BfModule*> mModuleRefs;

	BfIRMDNode mDICompileUnit;
	int mRevision;
	int mRebuildIdx;
	int mLastUsedRevision;
	int mExtensionCount;
	int mIncompleteMethodCount;
	int mOnDemandMethodCount;
	int mLastModuleWrittenRevision;
	int mCurLocalMethodId;
	int16 mUsedSlotCount; // -1 = not used, 0 = awaiting
	bool mAddedToCount;
	bool mHasForceLinkMarker;
	bool mIsReified;
	bool mGeneratesCode;
	bool mReifyQueued;
	bool mWantsIRIgnoreWrites;
	bool mHasGenericMethods;
	bool mIsSpecialModule; // vdata, unspecialized, external
	bool mIsComptimeModule;
	bool mIsScratchModule;
	bool mIsSpecializedMethodModuleRoot;
	bool mIsModuleMutable; // Set to false after writing module to disk, can be set back to true after doing extension module
	bool mWroteToLib;
	bool mHadBuildError;
	bool mHadBuildWarning;
	bool mIgnoreErrors;
	bool mHadIgnoredError;
	bool mIgnoreWarnings;
	bool mSetIllegalSrcPosition;
	bool mReportErrors; // Still puts system in error state when set to false
	bool mIsInsideAutoComplete;
	bool mIsHotModule;
	bool mIsDeleting;
	bool mSkipInnerLookup;
	bool mAwaitingInitFinish;
	bool mAwaitingFinish;
	bool mHasFullDebugInfo;
	bool mNoResolveGenericParams;
	bool mHadHotObjectWrites;

public:
	void FatalError(const StringImpl& error, const char* file = NULL, int line = -1, int column = -1);
	void FatalError(const StringImpl& error, BfAstNode* refNode);
	void InternalError(const StringImpl& error, BfAstNode* refNode = NULL, const char* file = NULL, int line = -1);
	void NotImpl(BfAstNode* astNode);
	void AddMethodReference(const BfMethodRef& methodRef, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	bool CheckProtection(BfProtection protection, BfTypeDef* checkType, bool allowProtected, bool allowPrivate);
	void GetAccessAllowed(BfTypeInstance* checkType, bool& allowProtected, bool& allowPrivate);
	bool CheckProtection(BfProtectionCheckFlags& flags, BfTypeInstance* memberOwner, BfProject* memberProject, BfProtection memberProtection, BfTypeInstance* lookupStartType);
	void SetElementType(BfAstNode* astNode, BfSourceElementType elementType);
	bool PreFail();
	void SetFail();
	void VerifyOnDemandMethods();
	bool IsSkippingExtraResolveChecks();
	bool AddErrorContext(StringImpl& errorString, BfAstNode* refNode, BfWhileSpecializingFlags& isWhileSpecializing, bool isWarning);
	CeDbgState* GetCeDbgState();
	BfError* Fail(const StringImpl& error, BfAstNode* refNode = NULL, bool isPersistent = false, bool deferError = false);
	BfError* FailInternal(const StringImpl& error, BfAstNode* refNode = NULL);
	BfError* FailAfter(const StringImpl& error, BfAstNode* refNode);
	BfError* Warn(int warningNum, const StringImpl& warning, BfAstNode* refNode = NULL, bool isPersistent = false, bool showInSpecialized = false);
	void CheckErrorAttributes(BfTypeInstance* typeInstance, BfMethodInstance* methodInstance, BfFieldInstance* fieldInstance, BfCustomAttributes* customAttributes, BfAstNode* targetSrc);
	void CheckRangeError(BfType* type, BfAstNode* refNode);
	bool CheckCircularDataError(bool failTypes = true);
	BfFileInstance* GetFileFromNode(BfAstNode* astNode);
	//void UpdateSrcPos(BfAstNode* astNode, bool setDebugLoc = true, int debugLocOffset = 0, bool force = false);
	void UpdateSrcPos(BfAstNode* astNode, BfSrcPosFlags flags = BfSrcPosFlag_None, int debugLocOffset = 0);
	void UseDefaultSrcPos(BfSrcPosFlags flags = BfSrcPosFlag_None, int debugLocOffset = 0);
	void UpdateExprSrcPos(BfAstNode* astNode, BfSrcPosFlags flags = BfSrcPosFlag_None);
	void SetIllegalSrcPos(BfSrcPosFlags flags = BfSrcPosFlag_None);
	void SetIllegalExprSrcPos(BfSrcPosFlags flags = BfSrcPosFlag_None);
	void GetConstClassValueParam(BfIRValue classVData, SizedArrayImpl<BfIRValue>& typeValueParams);
	BfIRValue GetConstValue(int64 val);
	BfIRValue GetConstValue(int64 val, BfType* type);
	BfIRValue GetConstValue8(int val);
	BfIRValue GetConstValue32(int32 val);
	BfIRValue GetConstValue64(int64 val);
	BfIRValue GetDefaultValue(BfType* type);
	BfTypedValue GetFakeTypedValue(BfType* type);
	BfTypedValue GetDefaultTypedValue(BfType* type, bool allowRef = false, BfDefaultValueKind defaultValueKind = BfDefaultValueKind_Const);
	void FixConstValueParams(BfTypeInstance* typeInst, SizedArrayImpl<BfIRValue>& valueParams, bool fillInPadding = false);
	BfIRValue CreateStringObjectValue(const StringImpl& str, int stringId, bool define);
	BfIRValue CreateStringCharPtr(const StringImpl& str, int stringId, bool define);
	int GetStringPoolIdx(BfIRValue constantStr, BfIRConstHolder* constHolder = NULL);
	String* GetStringPoolString(BfIRValue constantStr, BfIRConstHolder* constHolder = NULL);
	BfIRValue GetStringCharPtr(int stringId, bool force = false);
	BfIRValue GetStringCharPtr(BfIRValue strValue, bool force = false);
	BfIRValue GetStringCharPtr(const StringImpl& str, bool force = false);
	BfIRValue GetStringObjectValue(int idx, bool define, bool force);
	BfIRValue GetStringObjectValue(const StringImpl& str, bool define = false, bool force = false);
	BfIRValue CreateGlobalConstValue(const StringImpl& name, BfIRValue constant, BfIRType type, bool external);
	void VariantToString(StringImpl& str, const BfVariant& variant);
	StringT<128> TypeToString(BfType* resolvedType, Array<String>* genericMethodParamNameOverrides = NULL);
	StringT<128> TypeToString(BfType* resolvedType, BfTypeNameFlags typeNameFlags, Array<String>* genericMethodParamNameOverrides = NULL);
	void DoTypeToString(StringImpl& str, BfType* resolvedType, BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None, Array<String>* genericMethodParamNameOverrides = NULL);
	String FieldToString(BfFieldInstance* fieldInstance);
	StringT<128> MethodToString(BfMethodInstance* methodInst, BfMethodNameFlags methodNameFlags = BfMethodNameFlag_ResolveGenericParamNames, BfTypeVector* typeGenericArgs = NULL, BfTypeVector* methodGenericArgs = NULL);
	void pt(BfType* type);
	void pm(BfMethodInstance* type);
	BfIRType CurrentAddToConstHolder(BfIRType irType);
	void CurrentAddToConstHolder(BfIRValue& irVal);
	void ClearConstData();
	bool HasUnactializedConstant(BfConstant* constant, BfIRConstHolder* constHolder);
	BfTypedValue GetTypedValueFromConstant(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType);
	BfIRValue ConstantToCurrent(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType, bool allowUnactualized = false);
	void ValidateCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeTargets attrTarget);
	void GetCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeDirective* attributesDirective, BfAttributeTargets attrType, BfGetCustomAttributesFlags flags = BfGetCustomAttributesFlags_None, BfCaptureInfo* captureInfo = NULL);
	BfCustomAttributes* GetCustomAttributes(BfAttributeDirective* attributesDirective, BfAttributeTargets attrType, BfGetCustomAttributesFlags flags = BfGetCustomAttributesFlags_None, BfCaptureInfo* captureInfo = NULL);
	BfCustomAttributes* GetCustomAttributes(BfTypeDef* typeDef);
	void FinishAttributeState(BfAttributeState* attributeState);
	void ProcessTypeInstCustomAttributes(int& packing, bool& isUnion, bool& isCRepr, bool& isOrdered, int& alignOverride, BfType*& underlyingArrayType, int& underlyingArraySize);
	void ProcessCustomAttributeData();
	bool TryGetConstString(BfIRConstHolder* constHolder, BfIRValue irValue, StringImpl& str);
	BfVariant TypedValueToVariant(BfAstNode* refNode, const BfTypedValue& value, bool allowUndef = false);

	BfTypedValue FlushNullConditional(BfTypedValue result, bool ignoreNullable = false);
	void NewScopeState(bool createLexicalBlock = true, bool flushValueScope = true); // returns prev scope data
	BfIRValue CreateAlloca(BfType* type, bool addLifetime = true, const char* name = NULL, BfIRValue arraySize = BfIRValue());
	BfIRValue CreateAllocaInst(BfTypeInstance* typeInst, bool addLifetime = true, const char* name = NULL);
	BfDeferredCallEntry* AddStackAlloc(BfTypedValue val, BfIRValue arraySize, BfAstNode* refNode, BfScopeData* scope, bool condAlloca = false, bool mayEscape = false, BfIRBlock valBlock = BfIRBlock());
	void RestoreScoreState_LocalVariables(int localVarStart);
	void RestoreScopeState();
	void MarkDynStack(BfScopeData* scope);
	void SaveStackState(BfScopeData* scope);
	BfIRValue ValueScopeStart();
	void ValueScopeEnd(BfIRValue valueScopeStart);
	BfProjectSet* GetVisibleProjectSet();

	void AddBasicBlock(BfIRBlock bb, bool activate = true);
	void VisitEmbeddedStatement(BfAstNode* stmt, BfExprEvaluator* exprEvaluator = NULL, BfEmbeddedStatementFlags flags = BfEmbeddedStatementFlags_None);
	void VisitCodeBlock(BfBlock* block);
	void VisitCodeBlock(BfBlock* block, BfIRBlock continueBlock, BfIRBlock breakBlock, BfIRBlock fallthroughBlock, bool defaultBreak, bool* hadReturn = NULL, BfLabelNode* labelNode = NULL, bool closeScope = false, BfEmbeddedStatementFlags flags = BfEmbeddedStatementFlags_None);
	void DoForLess(BfForEachStatement* forEachStmt);

	// Util
	void CreateReturn(BfIRValue val);
	void EmitReturn(const BfTypedValue& val);
	void EmitDefaultReturn();
	void EmitDeferredCall(BfModuleMethodInstance moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfDeferredBlockFlags flags = BfDeferredBlockFlag_None);
	bool AddDeferredCallEntry(BfDeferredCallEntry* deferredCallEntry, BfScopeData* scope);
	BfDeferredCallEntry* AddDeferredBlock(BfBlock* block, BfScopeData* scope, Array<BfDeferredCapture>* captures = NULL);
	BfDeferredCallEntry* AddDeferredCall(const BfModuleMethodInstance& moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfScopeData* scope, BfAstNode* srcNode = NULL, bool bypassVirtual = false, bool doNullCheck = false);
	void EmitDeferredCall(BfScopeData* scopeData, BfDeferredCallEntry& deferredCallEntry, bool moveBlocks);
	void EmitDeferredCallProcessor(BfScopeData* scopeData, SLIList<BfDeferredCallEntry*>& callEntries, BfIRValue callTail);
	void EmitDeferredCallProcessorInstances(BfScopeData* scopeData);
	bool CanCast(BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags = BfCastFlags_None);
	bool AreSplatsCompatible(BfType* fromType, BfType* toType, bool* outNeedsMemberCasting);
	BfType* GetClosestNumericCastType(const BfTypedValue& typedVal, BfType* wantType);
	BfTypedValue BoxValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType /*Can be System.Object or interface*/, const BfAllocTarget& allocTarget, BfCastFlags castFlags = BfCastFlags_None);
	BfIRValue CastToFunction(BfAstNode* srcNode, const BfTypedValue& targetValue, BfMethodInstance* methodInstance, BfType* toType, BfCastFlags castFlags = BfCastFlags_None, BfIRValue irFunc = BfIRValue());
	BfIRValue CastToValue(BfAstNode* srcNode, BfTypedValue val, BfType* toType, BfCastFlags castFlags = BfCastFlags_None, BfCastResultFlags* resultFlags = NULL);
	BfTypedValue Cast(BfAstNode* srcNode, const BfTypedValue& val, BfType* toType, BfCastFlags castFlags = BfCastFlags_None);
	BfPrimitiveType* GetIntCoercibleType(BfType* type);
	BfTypedValue GetIntCoercible(const BfTypedValue& typedValue);
	bool WantsDebugInfo();
	BfTypeOptions* GetTypeOptions();
	BfReflectKind GetUserReflectKind(BfTypeInstance* attrType);
	BfReflectKind GetReflectKind(BfReflectKind reflectKind, BfTypeInstance* typeInstance);
	void CleanupFileInstances();
	void AssertErrorState();
	void AssertParseErrorState();
	void InitTypeInst(BfTypedValue typedValue, BfScopeData* scope, bool zeroMemory, BfIRValue dataSize);
	bool IsAllocatorAligned();
	BfIRValue AllocBytes(BfAstNode* refNode, const BfAllocTarget& allocTarget, BfType* type, BfIRValue sizeValue, BfIRValue alignValue, BfAllocFlags allocFlags/*bool zeroMemory, bool defaultToMalloc*/);
	BfIRValue GetMarkFuncPtr(BfType* type);
	BfIRValue GetDbgRawAllocData(BfType* type);
	BfIRValue AllocFromType(BfType* type, const BfAllocTarget& allocTarget, BfIRValue appendSizeValue = BfIRValue(), BfIRValue arraySize = BfIRValue(), int arrayDim = 0, /*bool isRawArrayAlloc = false, bool zeroMemory = true*/BfAllocFlags allocFlags = BfAllocFlags_ZeroMemory, int alignOverride = -1);
	void ValidateAllocation(BfType* type, BfAstNode* refNode);
	bool IsOptimized();
	void EmitAppendAlign(int align, int sizeMultiple = 0);
	BfIRValue AppendAllocFromType(BfType* type, BfIRValue appendSizeValue = BfIRValue(), int appendAllocAlign = 0, BfIRValue arraySize = BfIRValue(), int arrayDim = 0, bool isRawArrayAlloc = false, bool zeroMemory = true);
	bool IsTargetingBeefBackend();
	bool WantsLifetimes();
	bool HasCompiledOutput();
	bool HasExecutedOutput();
	void SkipObjectAccessCheck(BfTypedValue typedVal);
	void EmitObjectAccessCheck(BfTypedValue typedVal);
	void EmitEnsureInstructionAt();
	void EmitDynamicCastCheck(const BfTypedValue& targetValue, BfType* targetType, BfIRBlock trueBlock, BfIRBlock falseBlock, bool nullSucceeds = false);
	void EmitDynamicCastCheck(BfTypedValue typedVal, BfType* type, bool allowNull);
	void CheckStaticAccess(BfTypeInstance* typeInstance);
	BfTypedValue RemoveRef(BfTypedValue typedValue);
	BfTypedValue SanitizeAddr(BfTypedValue typedValue);
	BfTypedValue ToRef(BfTypedValue typedValue, BfRefType* refType = NULL);
	BfTypedValue LoadOrAggregateValue(BfTypedValue typedValue);
	BfTypedValue LoadValue(BfTypedValue typedValue, BfAstNode* refNode = NULL, bool isVolatile = false);
	BfTypedValue PrepareConst(BfTypedValue& typedValue);
	void AggregateSplatIntoAddr(BfTypedValue typedValue, BfIRValue addrVal);
	BfTypedValue AggregateSplat(BfTypedValue typedValue, BfIRValue* valueArrPtr = NULL);
	BfTypedValue MakeAddressable(BfTypedValue typedValue, bool forceMutable = false, bool forceAddressable = false);
	BfTypedValue RemoveReadOnly(BfTypedValue typedValue);
	BfTypedValue CopyValue(const BfTypedValue& typedValue);
	BfIRValue ExtractSplatValue(BfTypedValue typedValue, int componentIdx, BfType* wantType = NULL, bool* isAddr = NULL);
	BfTypedValue ExtractValue(BfTypedValue typedValue, BfFieldInstance* fieldInst, int fieldIdx);
	BfIRValue ExtractValue(BfTypedValue typedValue, int dataIdx);
	BfIRValue CreateIndexedValue(BfType* elementType, BfIRValue value, BfIRValue indexValue, bool isElementIndex = false);
	BfIRValue CreateIndexedValue(BfType* elementType, BfIRValue value, int indexValue, bool isElementIndex = false);
	bool CheckModifyValue(BfTypedValue& typedValue, BfAstNode* refNode, const char* modifyType = NULL);
	BfIRValue GetInterfaceSlotNum(BfTypeInstance* ifaceType);
	void HadSlotCountDependency();
	BfTypedValue GetCompilerFieldValue(const StringImpl& str);
	BfTypedValue GetCompilerFieldValue(const BfTypedValue typedVal);
	BfTypedValue ReferenceStaticField(BfFieldInstance* fieldInstance);
	BfFieldInstance* GetFieldInstance(BfTypeInstance* typeInst, int fieldIdx, const char* fieldName = NULL);
	BfTypedValue GetThis(bool markUsing = true);
	void MarkUsingThis();
	BfLocalVariable* GetThisVariable();
	bool IsInGeneric();
	bool InDefinitionSection();
	bool IsInSpecializedGeneric();
	bool IsInSpecializedSection(); // Either a specialized generic or an injected mixin
	bool IsInUnspecializedGeneric();

	// BfStmtEvaluator.cpp
	virtual void Visit(BfAstNode* astNode) override;
	virtual void Visit(BfIdentifierNode* identifierNode) override;
	virtual void Visit(BfTypeReference* typeRef) override;
	virtual void Visit(BfEmptyStatement* astNode) override;
	virtual void Visit(BfExpression* expressionStmt) override;
	virtual void Visit(BfExpressionStatement* expressionStmt) override;
	virtual void Visit(BfVariableDeclaration* varDecl) override;
	virtual void Visit(BfLocalMethodDeclaration* methodDecl) override;
	virtual void Visit(BfAttributedStatement* attribStmt) override;
	virtual void Visit(BfThrowStatement* throwStmt) override;
	virtual void Visit(BfDeleteStatement* deleteStmt) override;
	virtual void Visit(BfSwitchStatement* switchStmt) override;
	virtual void Visit(BfTryStatement* tryStmt) override;
	virtual void Visit(BfCatchStatement* catchStmt) override;
	virtual void Visit(BfFinallyStatement* finallyStmt) override;
	virtual void Visit(BfCheckedStatement* checkedStmt) override;
	virtual void Visit(BfUncheckedStatement* uncheckedStmt) override;
	void DoIfStatement(BfIfStatement* ifStmt, bool includeTrueStmt, bool includeFalseStmt);
	virtual void Visit(BfIfStatement* ifStmt) override;
	virtual void Visit(BfReturnStatement* returnStmt) override;
	virtual void Visit(BfYieldStatement* yieldStmt) override;
	virtual void Visit(BfBreakStatement* breakStmt) override;
	virtual void Visit(BfContinueStatement* continueStmt) override;
	virtual void Visit(BfFallthroughStatement* fallthroughStmt) override;
	virtual void Visit(BfUsingStatement* usingStmt) override;
	virtual void Visit(BfDoStatement* doStmt) override;
	virtual void Visit(BfRepeatStatement* doStmt) override;
	virtual void Visit(BfWhileStatement* whileStmt) override;
	virtual void Visit(BfForStatement* forStmt) override;
	virtual void Visit(BfForEachStatement* forEachStmt) override;
	virtual void Visit(BfDeferStatement* deferStmt) override;
	virtual void Visit(BfBlock* block) override;
	virtual void Visit(BfUnscopedBlock* block) override;
	virtual void Visit(BfLabeledBlock* labeledBlock) override;
	virtual void Visit(BfRootNode* rootNode) override;
	virtual void Visit(BfInlineAsmStatement* asmStmt) override;

	// Type helpers
	BfGenericExtensionEntry* BuildGenericExtensionInfo(BfTypeInstance* genericTypeInst, BfTypeDef* partialTypeDef);
	bool InitGenericParams(BfType* resolvedTypeRef);
	bool FinishGenericParams(BfType* resolvedTypeRef);
	bool ValidateGenericConstraints(BfAstNode* typeRef, BfTypeInstance* genericTypeInstance, bool ignoreErrors);
	BfType* ResolveGenericMethodTypeRef(BfTypeReference* typeRef, BfMethodInstance* methodInstance, BfGenericParamInstance* genericParamInstance, BfTypeVector* methodGenericArgsOverride);
	bool AreConstraintsSubset(BfGenericParamInstance* checkInner, BfGenericParamInstance* checkOuter);
	bool CheckConstraintState(BfAstNode* refNode);
	void ValidateGenericParams(BfGenericParamKind genericParamKind, Span<BfGenericParamInstance*> genericParams);
	bool ShouldAllowMultipleDefinitions(BfTypeInstance* typeInst, BfTypeDef* firstDeclaringTypeDef, BfTypeDef* secondDeclaringTypeDef);
	void CheckInjectNewRevision(BfTypeInstance* typeInstance);
	void InitType(BfType* resolvedTypeRef, BfPopulateType populateType);
	BfProtection FixProtection(BfProtection protection, BfProject* defProject);
	bool CheckAccessMemberProtection(BfProtection protection, BfTypeInstance* memberType);
	bool CheckDefineMemberProtection(BfProtection protection, BfType* memberType);
	void CheckMemberNames(BfTypeInstance* typeInst);
	void AddDependency(BfType* usedType, BfType* userType, BfDependencyMap::DependencyFlags flags, BfDepContext* depContext = NULL);
	void AddDependency(BfGenericParamInstance* genericParam, BfTypeInstance* usingType);
	void AddCallDependency(BfMethodInstance* methodInstance, bool devirtualized = false);
	void AddFieldDependency(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfType* fieldType);
	void TypeFailed(BfTypeInstance* typeInstance);
	bool IsAttribute(BfTypeInstance* typeInst);
	void PopulateGlobalContainersList(const BfGlobalLookup& globalLookup);
	BfStaticSearch* GetStaticSearch();
	BfInternalAccessSet* GetInternalAccessSet();
	bool CheckInternalProtection(BfTypeDef* usingTypeDef);
	void AddFailType(BfTypeInstance* typeInstance);
	void DeferRebuildType(BfTypeInstance* typeInstance);
	void MarkDerivedDirty(BfTypeInstance* typeInst);
	void CheckAddFailType();
	void PopulateType(BfType* resolvedTypeRef, BfPopulateType populateType = BfPopulateType_Data);
	BfTypeOptions* GetTypeOptions(BfTypeDef* typeDef);
	bool ApplyTypeOptionMethodFilters(bool includeMethod, BfMethodDef* methodDef, BfTypeOptions* typeOptions);
	int GenerateTypeOptions(BfCustomAttributes* customAttributes, BfTypeInstance* typeInstance, bool checkTypeName);
	void SetTypeOptions(BfTypeInstance* typeInstance);
	BfModuleOptions GetModuleOptions();
	BfCheckedKind GetDefaultCheckedKind();
	void FinishCEParseContext(BfAstNode* refNode, BfTypeInstance* typeInstance, BfCEParseContext* ceParseContext);
	BfCEParseContext CEEmitParse(BfTypeInstance* typeInstance, BfTypeDef* declaringType, const StringImpl& src, BfAstNode* refNode, BfCeTypeEmitSourceKind emitSourceKind);
	void UpdateCEEmit(CeEmitContext* ceEmitContext, BfTypeInstance* typeInstance, BfTypeDef* declaringType, const StringImpl& ctxString, BfAstNode* refNode, BfCeTypeEmitSourceKind emitSourceKind);
	void HandleCEAttributes(CeEmitContext* ceEmitContext, BfTypeInstance* typeInst, BfFieldInstance* fieldInstance, BfCustomAttributes* customAttributes, Dictionary<BfTypeInstance*, BfIRValue>& foundAttributes, bool underlyingTypeDeferred);
	void CEMixin(BfAstNode* refNode, const StringImpl& src);
	void ExecuteCEOnCompile(CeEmitContext* ceEmitContext, BfTypeInstance* typeInst, BfCEOnCompileKind onCompileKind, bool underlyingTypeDeferred);
	void DoCEEmit(BfTypeInstance* typeInstance, bool& hadNewMembers, bool underlyingTypeDeferred);
	void DoCEEmit(BfMethodInstance* methodInstance);
	void PopulateUsingFieldData(BfTypeInstance* typeInstance);
	void DoPopulateType_TypeAlias(BfTypeAliasType* typeAlias);
	void DoPopulateType_InitSearches(BfTypeInstance* typeInstance);
	void DoPopulateType_SetGenericDependencies(BfTypeInstance* genericTypeInstance);
	void DoPopulateType_FinishEnum(BfTypeInstance* typeInstance, bool underlyingTypeDeferred, HashContext* dataMemberHashCtx, BfType* unionInnerType);
	void DoPopulateType_CeCheckEnum(BfTypeInstance* typeInstance, bool underlyingTypeDeferred);
	void DoPopulateType(BfType* resolvedTypeRef, BfPopulateType populateType = BfPopulateType_Data);
	static BfModule* GetModuleFor(BfType* type);
	void DoTypeInstanceMethodProcessing(BfTypeInstance* typeInstance);
	void RebuildMethods(BfTypeInstance* typeInstance);
	BfFieldInstance* GetFieldByName(BfTypeInstance* typeInstance, const StringImpl& fieldName, bool isRequired = true, BfAstNode* refNode = NULL);
	void CreateStaticField(BfFieldInstance* fieldInstance, bool isThreadLocal = false);
	void ResolveConstField(BfTypeInstance* typeInst, BfFieldInstance* fieldInstance, BfFieldDef* field, bool forceResolve = false);
	BfTypedValue GetFieldInitializerValue(BfFieldInstance* fieldInstance, BfExpression* initializer = NULL, BfFieldDef* fieldDef = NULL, BfType* fieldType = NULL, bool doStore = false);
	void AppendedObjectInit(BfFieldInstance* fieldInstance);
	void MarkFieldInitialized(BfFieldInstance* fieldInstance);
	bool IsThreadLocal(BfFieldInstance* fieldInstance);
	BfType* ResolveVarFieldType(BfTypeInstance* typeInst, BfFieldInstance* fieldInstance, BfFieldDef* field);
	void FindSubTypes(BfTypeInstance* classType, SizedArrayImpl<int>* outVals, SizedArrayImpl<BfTypeInstance*>* exChecks, bool isInterfacePass);
	BfType* CheckUnspecializedGenericType(BfTypeInstance* genericTypeInst, BfPopulateType populateType);
	BfTypeInstance* GetUnspecializedTypeInstance(BfTypeInstance* typeInst);
	BfArrayType* CreateArrayType(BfType* resolvedType, int dimensions);
	BfSizedArrayType* CreateSizedArrayType(BfType* resolvedType, int size);
	BfUnknownSizedArrayType* CreateUnknownSizedArrayType(BfType* resolvedType, BfType* sizeParam);
	BfPointerType* CreatePointerType(BfType* resolvedType);
	BfPointerType* CreatePointerType(BfTypeReference* typeRef);
	BfConstExprValueType* CreateConstExprValueType(const BfTypedValue& typedValue, bool allowCreate = true);
	BfConstExprValueType* CreateConstExprValueType(const BfVariant& variant, BfType* type, bool allowCreate = true);
	BfBoxedType* CreateBoxedType(BfType* resolvedTypeRef, bool allowCreate = true);
	BfTypeInstance* CreateTupleType(const BfTypeVector& fieldTypes, const Array<String>& fieldNames, bool allowVar = false);
	BfTypeInstance* SantizeTupleType(BfTypeInstance* tupleType);
	BfRefType* CreateRefType(BfType* resolvedTypeRef, BfRefType::RefKind refKind = BfRefType::RefKind_Ref);
	BfModifiedTypeType* CreateModifiedTypeType(BfType* resolvedTypeRef, BfToken modifiedKind);
	BfConcreteInterfaceType* CreateConcreteInterfaceType(BfTypeInstance* interfaceType);
	BfTypeInstance* GetWrappedStructType(BfType* type, bool allowSpecialized = true);
	BfTypeInstance* GetPrimitiveStructType(BfTypeCode typeCode);
	BfPrimitiveType* GetPrimitiveType(BfTypeCode typeCode);
	BfIRType GetIRLoweredType(BfTypeCode loweredTypeCode, BfTypeCode loweredTypeCode2);
	BfMethodRefType* CreateMethodRefType(BfMethodInstance* methodInstance, bool mustAlreadyExist = false);
	BfType* FixIntUnknown(BfType* type);
	void FixIntUnknown(BfTypedValue& typedVal, BfType* matchType = NULL);
	void FixIntUnknown(BfTypedValue& lhs, BfTypedValue& rhs);
	void FixValueActualization(BfTypedValue& typedVal, bool force = false);
	bool TypeEquals(BfTypedValue& val, BfType* type);
	BfTypeDef* ResolveGenericInstanceDef(BfGenericInstanceTypeRef* genericTypeRef, BfType** outType = NULL, BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_None);
	BfType* ResolveType(BfType* lookupType, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_None);
	void ResolveGenericParamConstraints(BfGenericParamInstance* genericParamInstance, bool isUnspecialized, Array<BfTypeReference*>* deferredResolveTypes = NULL);
	String GenericParamSourceToString(const BfGenericParamSource& genericParamSource);
	bool CheckGenericConstraints(const BfGenericParamSource& genericParamSource, BfType* checkArgType, BfAstNode* checkArgTypeRef, BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs = NULL, BfError** errorOut = NULL);
	BfIRValue AllocLocalVariable(BfType* type, const StringImpl& name, bool doLifetimeEnd = true);
	void DoAddLocalVariable(BfLocalVariable* localVar);
	void DoLocalVariableDebugInfo(BfLocalVariable* localVar, bool doAliasValue = false, BfIRValue declareBefore = BfIRValue(), BfIRInitType initType = BfIRInitType_NotSet);
	BfLocalVariable* AddLocalVariableDef(BfLocalVariable* localVarDef, bool addDebugInfo = false, bool doAliasValue = false, BfIRValue declareBefore = BfIRValue(), BfIRInitType initType = BfIRInitType_NotSet);
	bool TryLocalVariableInit(BfLocalVariable* localVar);
	void LocalVariableDone(BfLocalVariable* localVar, bool isMethodExit);
	void CreateRetValLocal();
	void CreateDIRetVal();
	BfTypedValue CreateTuple(const Array<BfTypedValue>& values, const Array<String>& fieldNames);
	void CheckTupleVariableDeclaration(BfTupleExpression* tupleExpr, BfType* initType);
	void HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl, BfTupleExpression* tupleExpr, BfTypedValue initTupleValue, bool isReadOnly, bool isConst, bool forceAddr, BfIRBlock* declBlock = NULL);
	void HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl);
	void HandleCaseEnumMatch_Tuple(BfTypedValue tupleVal, const BfSizedArray<BfExpression*>& arguments, BfAstNode* tooFewRef, BfIRValue phiVal, BfIRBlock& matchedBlockStart,
		BfIRBlock& matchedBlockEnd, BfIRBlock& falseBlockStart, BfIRBlock& falseBlockEnd, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough);
	BfTypedValue TryCaseTupleMatch(BfTypedValue tupleVal, BfTupleExpression* tupleExpr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough);
	BfTypedValue TryCaseEnumMatch(BfTypedValue enumVal, BfTypedValue tagVal, BfExpression* expr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, int& uncondTagId, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough);
	BfTypedValue HandleCaseBind(BfTypedValue enumVal, const BfTypedValue& tagVal, BfEnumCaseBindExpression* bindExpr, BfIRBlock* eqBlock = NULL, BfIRBlock* notEqBlock = NULL, BfIRBlock* matchBlock = NULL, int* outEnumIdx = NULL);
	void TryInitVar(BfAstNode* checkNode, BfLocalVariable* varDecl, BfTypedValue initValue, BfTypedValue& checkResult);
	BfLocalVariable* HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfExprEvaluator* exprEvaluator = NULL);
	BfLocalVariable* HandleVariableDeclaration(BfType* type, BfAstNode* nameNode, BfTypedValue val, bool updateSrcLoc = true, bool forceAddr = false);
	BfLocalVariable* HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfTypedValue val, bool updateSrcLoc = true, bool forceAddr = false);
	void CheckVariableDef(BfLocalVariable* variableDef);
	BfScopeData* FindScope(BfAstNode* scopeName, BfMixinState* curMixinState, bool allowAcrossDeferredBlock);
	BfScopeData* FindScope(BfAstNode* scopeName, bool allowAcrossDeferredBlock);
	BfBreakData* FindBreakData(BfAstNode* scopeName);
	void EmitLifetimeEnds(BfScopeData* scopeData);
	void ClearLifetimeEnds();
	bool HasDeferredScopeCalls(BfScopeData* scope);
	void EmitDeferredScopeCalls(bool useSrcPositions, BfScopeData* scope, BfIRBlock doneBlock = BfIRBlock());
	void MarkScopeLeft(BfScopeData* scopeData, bool isNoReturn = false);
	BfGenericParamType* GetGenericParamType(BfGenericParamKind paramKind, int paramIdx);
	BfType* ResolveGenericType(BfType* unspecializedType, BfTypeVector* typeGenericArguments, BfTypeVector* methodGenericArguments, BfType* selfType, bool allowFail = false);
	BfType* ResolveSelfType(BfType* type, BfType* selfType);
	bool IsUnboundGeneric(BfType* type);
	BfGenericParamInstance* GetGenericTypeParamInstance(int paramIdx);
	BfGenericParamInstance* GetGenericParamInstance(BfGenericParamType* type, bool checkMixinBind = false);
	void GetActiveTypeGenericParamInstances(SizedArray<BfGenericParamInstance*, 4>& genericParamInstance);
	BfGenericParamInstance* GetMergedGenericParamData(BfGenericParamType* type, BfGenericParamFlags& outFlags, BfType*& outTypeConstraint);
	BfTypeInstance* GetBaseType(BfTypeInstance* typeInst);
	void HandleTypeGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, int typeGenericParamIdx);
	void HandleMethodGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, BfMethodDef* methodDef, int typeGenericParamIdx);
	BfType* SafeResolveAliasType(BfTypeAliasType* aliasType);
	bool ResolveTypeResult_Validate(BfAstNode* typeRef, BfType* resolvedTypeRef);
	BfType* ResolveTypeResult(BfTypeReference* typeRef, BfType* resolvedTypeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags);
	void ShowAmbiguousTypeError(BfAstNode* refNode, BfTypeDef* typeDef, BfTypeDef* otherTypeDef);
	void ShowGenericArgCountError(BfAstNode* typeRef, int wantedGenericParams);
	BfTypeDef* GetActiveTypeDef(BfTypeInstance* typeInstanceOverride = NULL, bool useMixinDecl = false, bool useForeignImpl = false); // useMixinDecl is useful for type lookup, but we don't want the decl project to limit what methods the user can call
	BfTypeDef* FindTypeDefRaw(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstance, BfTypeDef* useTypeDef, BfTypeLookupError* error, BfTypeLookupResultCtx* lookupResultCtx = NULL, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfTypeDef* FindTypeDef(const BfAtomComposite& findName, int numGenericArgs = 0, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfTypeDef* FindTypeDef(const StringImpl& typeName, int numGenericArgs = 0, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfTypeDef* FindTypeDef(BfTypeReference* typeRef, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL, int numGenericParams = 0, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfTypedValue TryLookupGenericConstVaue(BfIdentifierNode* identifierNode, BfType* expectingType);
	void CheckTypeRefFixit(BfAstNode* typeRef, const char* appendName = NULL);
	void CheckIdentifierFixit(BfAstNode* node);
	void TypeRefNotFound(BfTypeReference* typeRef, const char* appendName = NULL);
	bool ValidateTypeWildcard(BfAstNode* typeRef, bool isAttributeRef);
	void GetDelegateTypeRefAttributes(BfDelegateTypeRef* delegateTypeRef, BfCallingConvention& callingConvention);
	BfType* ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0, int numGenericArgs = 0);
	BfType* ResolveTypeRefAllowUnboundGenerics(BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, bool resolveGenericParam = true);
	BfType* ResolveTypeRef_Type(BfAstNode* astNode, const BfSizedArray<BfAstNode*>* genericArgs, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfType* ResolveTypeRef(BfAstNode* astNode, const BfSizedArray<BfAstNode*>* genericArgs, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfType* ResolveTypeDef(BfTypeDef* typeDef, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_None);
	BfType* ResolveTypeDef(BfTypeDef* typeDef, const BfTypeVector& genericArgs, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_None);
	BfType* ResolveInnerType(BfType* outerType, BfAstNode* typeRef, BfPopulateType populateType = BfPopulateType_Data, bool ignoreErrors = false, int numGenericArgs = 0, BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_None);
	BfTypeDef* GetCombinedPartialTypeDef(BfTypeDef* type);
	BfTypeInstance* GetOuterType(BfType* type);
	bool IsInnerType(BfType* checkInnerType, BfType* checkOuterType);
	bool IsInnerType(BfTypeDef* checkInnerType, BfTypeDef* checkOuterType);
	bool TypeHasParentOrEquals(BfTypeDef* checkChildTypeDef, BfTypeDef* checkParentTypeDef);
	BfTypeDef* FindCommonOuterType(BfTypeDef* type, BfTypeDef* type2);
	bool TypeIsSubTypeOf(BfTypeInstance* srcType, BfTypeInstance* wantType, bool checkAccessibility = true);
	bool TypeIsSubTypeOf(BfTypeInstance* srcType, BfTypeDef* wantType);
	int GetTypeDistance(BfType* fromType, BfType* toType);
	bool IsTypeMoreSpecific(BfType* leftType, BfType* rightType);
	bool GetBasePropertyDef(BfPropertyDef*& propDef, BfTypeInstance*& typeInst);

	// Method helpers
	void CheckInterfaceMethod(BfMethodInstance* methodInstance);
	void CreateDelegateInvokeMethod();
	BfType* GetDelegateReturnType(BfType* delegateType);
	BfMethodInstance* GetDelegateInvokeMethod(BfTypeInstance* typeInstance);
	String GetLocalMethodName(const StringImpl& baseName, BfAstNode* anchorNode, BfMethodState* declMethodState, BfMixinState* declMixinState);
	BfMethodDef* GetLocalMethodDef(BfLocalMethod* localMethod);
	BfModuleMethodInstance GetLocalMethodInstance(BfLocalMethod* localMethod, const BfTypeVector& methodGenericArguments, BfMethodInstance* methodInstance = NULL, bool force = false);
	int GetLocalInferrableGenericArgCount(BfMethodDef* methodDef);
	void GetMethodCustomAttributes(BfMethodInstance* methodInstance);
	void SetupIRFunction(BfMethodInstance* methodInstance, StringImpl& mangledName, bool isTemporaryFunc, bool* outIsIntrinsic);
	void CheckHotMethod(BfMethodInstance* methodInstance, const StringImpl& mangledName);
	void StartMethodDeclaration(BfMethodInstance* methodInstance, BfMethodState* prevMethodState);
	void DoMethodDeclaration(BfMethodDeclaration* methodDeclaration, bool isTemporaryFunc, bool addToWorkList = true);
	void AddMethodToWorkList(BfMethodInstance* methodInstance);
	bool IsInterestedInMethod(BfTypeInstance* typeInstance, BfMethodDef* methodDef);
	void CalcAppendAlign(BfMethodInstance* methodInst);
	BfTypedValue TryConstCalcAppend(BfMethodInstance* methodInst, SizedArrayImpl<BfIRValue>& args, bool force = false);
	BfTypedValue CallBaseCtorCalc(bool constOnly);
	void EmitCtorCalcAppend();
	void CreateStaticCtor();
	BfIRValue CreateDllImportGlobalVar(BfMethodInstance* methodInstance, bool define = false);
	void CreateDllImportMethod();
	BfIRCallingConv GetIRCallingConvention(BfMethodInstance* methodInstance);
	void SetupIRMethod(BfMethodInstance* methodInstance, BfIRFunction func, bool isInlined);
	void EmitInitBlocks(const std::function<void(BfAstNode*)>& initBlockCallback);
	void EmitCtorBody(bool& skipBody);
	void EmitDtorBody();
	void EmitEnumToStringBody();
	void EmitTupleToStringBody();
	void EmitGCMarkAppended(BfTypedValue markVal);
	void EmitGCMarkValue(BfTypedValue& thisValue, BfType* checkType, int memberDepth, int curOffset, HashSet<int>& objectOffsets, BfModuleMethodInstance markFromGCThreadMethodInstance, bool isAppendObject = false);
	void EmitGCMarkValue(BfTypedValue markVal, BfModuleMethodInstance markFromGCThreadMethodInstance);
	void EmitGCMarkMembers();
	void EmitGCFindTLSMembers();
	void EmitIteratorBlock(bool& skipBody);
	void EmitEquals(BfTypedValue leftValue, BfTypedValue rightValue, BfIRBlock exitBB, bool strictEquals);
	void CreateFakeCallerMethod(const String& funcName);
	void CallChainedMethods(BfMethodInstance* methodInstance, bool reverse = false);
	void AddHotDataReferences(BfHotDataReferenceBuilder* builder);
	void ProcessMethod_SetupParams(BfMethodInstance* methodInstance, BfType* thisType, bool wantsDIData, SizedArrayImpl<BfIRMDNode>* diParams);
	void ProcessMethod_ProcessDeferredLocals(int startIdx = 0);
	void ProcessMethod(BfMethodInstance* methodInstance, bool isInlineDup = false, bool forceIRWrites = false);
	void CreateDynamicCastMethod();
	void CreateDelegateEqualsMethod();
	void CreateValueTypeEqualsMethod(bool strictEquals);
	BfIRFunction GetIntrinsic(BfMethodInstance* methodInstance, bool reportFailure = false);
	BfIRFunction GetBuiltInFunc(BfBuiltInFuncType funcType);
	BfIRValue CreateFunctionFrom(BfMethodInstance* methodInstance, bool tryExisting, bool isInlined);
	void EvaluateWithNewConditionalScope(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfEvalExprFlags flags);
	BfTypedValue CreateValueFromExpression(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfType* wantTypeRef = NULL, BfEvalExprFlags flags = BfEvalExprFlags_None, BfType** outOrigType = NULL);
	BfTypedValue CreateValueFromExpression(BfExpression* expr, BfType* wantTypeRef = NULL, BfEvalExprFlags flags = BfEvalExprFlags_None, BfType** outOrigType = NULL);
	BfTypedValue GetOrCreateVarAddr(BfExpression* expr);
	BfMethodInstance* GetRawMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName = NULL);
	BfMethodInstance* GetRawMethodInstance(BfTypeInstance* typeInstance, BfMethodDef* methodDef);
	BfMethodInstance* GetRawMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false, bool allowMixin = false);
	BfMethodInstance* GetUnspecializedMethodInstance(BfMethodInstance* methodInstance, bool useUnspecializedType = true); // Unspecialized owner type and unspecialized method type
	int GetGenericParamAndReturnCount(BfMethodInstance* methodInstance);
	BfModule* GetSpecializedMethodModule(const SizedArrayImpl<BfProject*>& projectList);
	BfModuleMethodInstance GetMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName = NULL, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	BfModuleMethodInstance GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false);
	BfModuleMethodInstance GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, const Array<BfType*>& paramTypes, bool checkBase = false);
	BfModuleMethodInstance GetInternalMethod(const StringImpl& methodName, int paramCount = -1);
	BfOperatorInfo* GetOperatorInfo(BfTypeInstance* typeInstance, BfOperatorDef* operatorDef);
	BfType* CheckOperator(BfTypeInstance* typeInstance, BfOperatorDef* operatorDef, const BfTypedValue& lhs, const BfTypedValue& rhs);
	bool IsMethodImplementedAndReified(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false);
	bool HasMixin(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount, bool checkBase = false);
	bool CompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB); // Doesn't compare return types nor static
	bool StrictCompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB); // Compares return types and static
	bool IsCompatibleInterfaceMethod(BfMethodInstance* methodA, BfMethodInstance* methodB);
	void UniqueSlotVirtualMethod(BfMethodInstance* methodInstance);
	void CompareDeclTypes(BfTypeInstance* typeInst, BfTypeDef* newDeclType, BfTypeDef* prevDeclType, bool& isBetter, bool& isWorse);
	bool SlotVirtualMethod(BfMethodInstance* methodInstance, BfAmbiguityContext* ambiguityContext = NULL);
	void CheckOverridenMethod(BfMethodInstance* methodInstance, BfMethodInstance* methodOverriden);
	bool SlotInterfaceMethod(BfMethodInstance* methodInstance);
	void SetMethodDependency(BfMethodInstance* methodInstance);
	BfModuleMethodInstance ReferenceExternalMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	BfModule* GetOrCreateMethodModule(BfMethodInstance* methodInstance);
	BfModuleMethodInstance GetMethodInstance(BfTypeInstance* typeInst, BfMethodDef* methodDef, const BfTypeVector& methodGenericArguments, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None, BfTypeInstance* foreignType = NULL);
	BfModuleMethodInstance GetMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	BfMethodInstance* GetOuterMethodInstance(BfMethodInstance* methodInstance); // Only useful for local methods
	void SetupMethodIdHash(BfMethodInstance* methodInstance);
	bool CheckUseMethodInstance(BfMethodInstance* methodInstance, BfAstNode* refNode);

	// Type Data
	BfIRValue CreateClassVDataGlobal(BfTypeInstance* typeInstance, int* outNumElements = NULL, String* outMangledName = NULL);
	BfIRValue GetClassVDataPtr(BfTypeInstance* typeInstance);
	BfIRValue CreateClassVDataExtGlobal(BfTypeInstance* declTypeInst, BfTypeInstance* implTypeInst, int startVirtIdx);
	BfIRValue CreateTypeDataRef(BfType* type);
	void EncodeAttributeData(BfTypeInstance* typeInstance, BfType* argType, BfIRValue arg, SizedArrayImpl<uint8>& data, Dictionary<int, int>& usedStringIdMap);
	BfIRValue CreateFieldData(BfFieldInstance* fieldInstance, int customAttrIdx);
	BfIRValue CreateTypeData(BfType* type, Dictionary<int, int>& usedStringIdMap, bool forceReflectFields, bool needsTypeData, bool needsTypeNames, bool needsVData);
	BfIRValue FixClassVData(BfIRValue value);

public:
	BfModule(BfContext* context, const StringImpl& moduleName);
	virtual ~BfModule();

	void Init(bool isFullRebuild = true);
	bool WantsFinishModule();
	bool IsHotCompile();
	void FinishInit();
	void CalcGeneratesCode();
	void ReifyModule();
	void UnreifyModule();
	void Cleanup();
	void StartNewRevision(RebuildKind rebuildKind = RebuildKind_All, bool force = false);
	void PrepareForIRWriting(BfTypeInstance* typeInst);
	void SetupIRBuilder(bool dbgVerifyCodeGen);
	void EnsureIRBuilder(bool dbgVerifyCodeGen = false);
	void DbgFinish();
	BfIRValue CreateForceLinkMarker(BfModule* module, String* outName);
	void ClearModuleData(bool clearTransientData = true);
	void DisownMethods();
	void ClearModule();
	void StartExtension(); // For new method specializations
	bool Finish();
	void RemoveModuleData();

	void ReportMemory(MemReporter* memReporter);
};

class BfAutoParentNodeEntry
{
public:
	BfModule* mModule;
	BfParentNodeEntry mParentNodeEntry;
	BfAutoParentNodeEntry(BfModule* module, BfAstNode* node)
	{
		mModule = module;
		mParentNodeEntry.mNode = node;
		mParentNodeEntry.mPrev = module->mParentNodeEntry;
		module->mParentNodeEntry = &mParentNodeEntry;
	}
	~BfAutoParentNodeEntry()
	{
		mModule->mParentNodeEntry = mParentNodeEntry.mPrev;
	}
};

class BfVDataModule : public BfModule
{
public:
	HashSet<int> mDefinedStrings;

public:
	BfVDataModule(BfContext* context) : BfModule(context, StringImpl::MakeRef("vdata"))
	{
	}
};

NS_BF_END

namespace std
{
	template<>
	struct hash<Beefy::BfMethodRef>
	{
		size_t operator()(const Beefy::BfMethodRef& val) const
		{
			if (val.mTypeInstance == NULL)
				return 0;
			return val.mTypeInstance->mTypeId ^ (val.mMethodNum << 10);
		}
	};

	template<>
	struct hash<Beefy::BfVDataExtEntry>
	{
		size_t operator()(const Beefy::BfVDataExtEntry& val) const
		{
			return ((size_t)(val.mDeclTypeInst) * 17) ^ (size_t)(val.mDeclTypeInst);
		}
	};

	template<>
	struct hash<Beefy::BfLocalVarEntry>
	{
		size_t operator()(const Beefy::BfLocalVarEntry& val) const
		{
			return std::hash<Beefy::String>()(val.mLocalVar->mName);
		}
	};
}
