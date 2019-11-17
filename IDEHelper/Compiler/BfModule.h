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

enum BfPopulateType
{	
	BfPopulateType_TypeDef,
	BfPopulateType_Identity,
	BfPopulateType_IdentityNoRemapAlias,
	BfPopulateType_Declaration,
	BfPopulateType_BaseType,
	BfPopulateType_Interfaces,
	BfPopulateType_Data,
	BfPopulateType_DataAndMethods,
	BfPopulateType_Full = BfPopulateType_DataAndMethods	,
	BfPopulateType_Full_Force
};

enum BfEvalExprFlags
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
	BfEvalExprFlags_NoAutoComplete = 0x8000
};

enum BfCastFlags
{
	BfCastFlags_None = 0,
	BfCastFlags_Explicit = 1,
	BfCastFlags_Unchecked = 2,
	BfCastFlags_Internal = 4,
	BfCastFlags_SilentFail = 8,
	BfCastFlags_NoBoxDtor = 0x10,
	BfCastFlags_NoConversionOperator = 0x20,
	BfCastFlags_FromCompiler = 0x40, // Not user specified
	BfCastFlags_Force = 0x80,
	BfCastFlags_PreferAddr = 0x100,
	BfCastFlags_WarnOnBox = 0x200
};

enum BfCastResultFlags
{
	BfCastResultFlags_None = 0,
	BfCastResultFlags_IsAddr = 1,
	BfCastResultFlags_IsTemp = 2
};

enum BfAllocFlags
{
	BfAllocFlags_None = 0,
	BfAllocFlags_RawArray = 1,
	BfAllocFlags_ZeroMemory = 2,
	BfAllocFlags_NoDtorCall = 4,
	BfAllocFlags_NoDefaultToMalloc = 8
};

enum BfProtectionCheckFlags
{
	BfProtectionCheckFlag_None = 0,
	BfProtectionCheckFlag_CheckedProtected = 1,
	BfProtectionCheckFlag_CheckedPrivate = 2,
	BfProtectionCheckFlag_AllowProtected = 4,
	BfProtectionCheckFlag_AllowPrivate = 8,
};

enum BfEmbeddedStatementFlags
{
	BfEmbeddedStatementFlags_None = 0,
	BfEmbeddedStatementFlags_IsConditional = 1,
	BfEmbeddedStatementFlags_IsDeferredBlock = 2
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
	bool mIsThis;
	bool mHasLocalStructBacking;
	bool mIsStruct;	
	bool mIsImplicitParam;
	bool mParamFailed;
	bool mIsAssigned;		
	bool mIsReadOnly;
	bool mIsSplat;
	bool mIsLowered;
	bool mAllowAddr;
	bool mIsShadow;
	bool mUsedImplicitly; // Passed implicitly to a local method, capture by ref if we can	
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
		mIsThis = false;
		mHasLocalStructBacking = false;
		mIsStruct = false;		
		mIsImplicitParam = false;
		mParamFailed = false;
		mIsAssigned = false;		
		mWrittenToId = -1;
		mReadFromId = -1;
		mIsReadOnly = false;
		mIsSplat = false;
		mIsLowered = false;
		mAllowAddr = false;
		mIsShadow = false;
		mUsedImplicitly = false;		
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
		mNextWithSameName = NULL;
	}
	~BfLocalMethod();
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
	SizedArray<BfIRValue, 1> mScopeArgs;
	Array<BfDeferredCapture> mCaptures;	
	BfBlock* mDeferredBlock;
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

// "Looped" means this scope will execute zero to many times, "Conditional" means zero or one.
// Looped and Conditional are mutually exclusive.  "Dyn" means Looped OR Conditional.
class BfScopeData
{
public:
	BfScopeData* mPrevScope;	
	BfIRMDNode mDIScope;
	BfIRMDNode mDIInlinedAt;	
	String mLabel;
	BfAstNode* mLabelNode;
	int mLocalVarStart;		
	int mScopeDepth;
	int mMixinDepth;	
	bool mIsScopeHead; // For first scope data or for inlined start
	bool mIsLoop;
	bool mIsConditional; // Rarely set - usually we rely on OuterIsConditional or InnerIsConditional
	bool mOuterIsConditional;
	bool mInnerIsConditional;
	bool mHadOuterDynStack;
	bool mAllowTargeting;
	bool mHadScopeValueRetain;	
	bool mIsDeferredBlock;
	BfBlock* mAstBlock;
	BfAstNode* mCloseNode;
	BfExprEvaluator* mExprEvaluator;
	SLIList<BfDeferredCallEntry*> mDeferredCallEntries;
	BfIRValue mBlock;
	BfIRValue mValueScopeStart;
	BfIRValue mSavedStack;	
	Array<BfIRValue> mSavedStackUses;
	Array<BfDeferredHandler> mDeferredHandlers; // These get cleared when us our a parent gets new entries added into mDeferredCallEntries
	Array<BfIRBlock> mAtEndBlocks; // Move these to the end after we close scope
	Array<BfIRValue> mDeferredLifetimeEnds;
	BfIRMDNode mAltDIFile;
	BfIRMDNode mAltDIScope;	

public:	
	BfScopeData()
	{
		mPrevScope = NULL;
		mLocalVarStart = 0;
		mLabelNode = NULL;
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
		mAllowTargeting = true;
		mMixinDepth = 0;
		mScopeDepth = 0;
	}	

	~BfScopeData()
	{
		mDeferredCallEntries.DeleteAll();
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
};

class BfAllocTarget
{
public:
	BfScopeData* mScopeData;
	BfAstNode* mRefNode;
	BfTypedValue mCustomAllocator;
	BfScopedInvocationTarget* mScopedInvocationTarget;

public:
	BfAllocTarget()
	{
		mScopeData = NULL;
		mRefNode = NULL;
		mCustomAllocator = NULL;
		mScopedInvocationTarget = NULL;
	}

	BfAllocTarget(BfScopeData* scopeData)
	{
		mScopeData = scopeData;
		mRefNode = NULL;
		mCustomAllocator = NULL;
		mScopedInvocationTarget = NULL;
	}

	BfAllocTarget(const BfTypedValue& customAllocator, BfAstNode* refNode)
	{
		mScopeData = NULL;
		mCustomAllocator = customAllocator;
		mRefNode = NULL;
		mScopedInvocationTarget = NULL;
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

struct BfAssignedLocal
{
	BfLocalVariable* mLocalVar;
	int mLocalVarField;

	bool operator==(const BfAssignedLocal& second) const
	{
		return (mLocalVar == second.mLocalVar) && (mLocalVarField == second.mLocalVarField);
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
	bool mIsUnconditional;
	bool mIsIfCondition;
	bool mIfMayBeSkipped;

public:
	BfDeferredLocalAssignData(BfScopeData* scopeData = NULL)
	{
		mScopeData = scopeData;
		mVarIdBarrier = -1;
		mHadFallthrough = false;
		mHadReturn = false;
		mChainedAssignData = NULL;
		mIsChained = false;
		mIsUnconditional = false;
		mIsIfCondition = false;
		mIfMayBeSkipped = false;
	}

	void ExtendFrom(BfDeferredLocalAssignData* outerLocalAssignData, bool doChain = false);
	void BreakExtendChain();
	void SetIntersection(const BfDeferredLocalAssignData& otherLocalAssignData);
	void Validate() const;
	void SetUnion(const BfDeferredLocalAssignData& otherLocalAssignData);	
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

class BfClosureState
{
public:
	bool mCapturing;
	bool mCaptureVisitingBody;
	int mCaptureStartAccessId;
	// When we need to look into another local method to determine captures, but we don't want to process local variable declarations or cause infinite recursion
	bool mBlindCapturing;
	bool mDeclaringMethodIsMutating;	
	BfLocalMethod* mLocalMethod;
	BfClosureInstanceInfo* mClosureInstanceInfo;
	BfMethodDef* mClosureMethodDef;
	BfType* mReturnType;
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
		mActiveDeferredLocalMethod = NULL;		
		mReturnType = NULL;
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
};

class BfAttributeState
{	
public:	
	BfAttributeTargets mTarget;
	BfCustomAttributes* mCustomAttributes;		
	bool mUsed;	

	BfAttributeState()
	{
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
	BfMethodInstance* mMixinMethodInstance;
	BfAstNode* mResultExpr;
	int mLocalsStartIdx;
	bool mUsedInvocationScope;
	bool mHasDeferredUsage;
	BfTypedValue mTarget;
	int mLastTargetAccessId;

public:
	BfMixinState()
	{
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

	BfConstResolveState()
	{
		mMethodInstance = NULL;
		mPrevConstResolveState = NULL;
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
		mIsStatic = false;
		mMethodInstance = NULL;
		mDtorMethodInstance = NULL;
	}

	~BfLambdaInstance()
	{
		delete mMethodInstance->mMethodDef;
		delete mMethodInstance;
		
		if (mDtorMethodInstance != NULL)
		{
			delete mDtorMethodInstance->mMethodDef;
			delete mDtorMethodInstance;			
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
	BfMethodState* mPrevMethodState; // Only non-null for things like local methods
	BfConstResolveState* mConstResolveState;	
	BfMethodInstance* mMethodInstance;	
	BfHotDataReferenceBuilder* mHotDataReferenceBuilder;
	BfIRFunction mIRFunction;	
	BfIRBlock mIRHeadBlock;
	BfIRBlock mIRInitBlock;
	BfIRBlock mIREntryBlock;
	Array<BfLocalVariable*> mLocals;
	HashSet<BfLocalVarEntry> mLocalVarSet;
	Array<BfLocalMethod*> mLocalMethods;
	Dictionary<String, BfLocalMethod*> mLocalMethodMap;
	Dictionary<String, BfLocalMethod*> mLocalMethodCache; // So any lambda 'capturing' and 'processing' stages use the same local method			
	Array<BfDeferredLocalMethod*> mDeferredLocalMethods;
	OwnedVector<BfMixinState> mMixinStates;
	Dictionary<BfAstNode*, BfLambdaInstance*> mLambdaCache;
	Array<BfLambdaInstance*> mDeferredLambdaInstances;
	Array<BfIRValue> mSplatDecompAddrs;	
	BfDeferredLocalAssignData* mDeferredLocalAssignData;
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
	bool mIsEmbedded; // Is an embedded statement (ie: if () stmt) not wrapped in a block
	bool mIgnoreObjectAccessCheck;	
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
	bool mCancelledDeferredCall;	
	bool mNoObjectAccessChecks;
	int mCurLocalVarId;
	int mCurAccessId; // For checking to see if a block reads from or writes to a local

public:
	BfMethodState()
	{		
		mMethodInstance = NULL;		
		mPrevMethodState = NULL;
		mConstResolveState = NULL;
		mHotDataReferenceBuilder = NULL;
		mHeadScope.mIsScopeHead = true;
		mCurScope = &mHeadScope;
		mTailScope = &mHeadScope;
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
		mIsEmbedded = false;
		mIgnoreObjectAccessCheck = false;
		mInConditionalBlock = false;		
		mAllowUinitReads = false;
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
		auto checkMethodState = this;
		while ((checkMethodState->mPrevMethodState != NULL) && (checkMethodState->mClosureState != NULL) && 
			(checkMethodState->mClosureState->mCapturing) && (checkMethodState->mClosureState->mLocalMethod != NULL))
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

	void LocalDefined(BfLocalVariable* localVar, int fieldIdx = -1);
	void ApplyDeferredLocalAssignData(const BfDeferredLocalAssignData& deferredLocalAssignData);	
	void Reset();

	int GetLocalStartIdx()
	{
		if (mMixinState != NULL)
			return mMixinState->mLocalsStartIdx;
		return 0;
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

enum BfResolveTypeRefFlags
{
	BfResolveTypeRefFlag_None = 0,
	BfResolveTypeRefFlag_NoResolveGenericParam = 1,
	BfResolveTypeRefFlag_AllowRef = 2,
	BfResolveTypeRefFlag_AllowRefGeneric = 4,
	BfResolveTypeRefFlag_IgnoreLookupError = 8,
	BfResolveTypeRefFlag_AllowGenericTypeParamConstValue = 0x10,
	BfResolveTypeRefFlag_AllowGenericMethodParamConstValue = 0x20,
	BfResolveTypeRefFlag_AllowGenericParamConstValue = 0x10 | 0x20,
	BfResolveTypeRefFlag_AutoComplete = 0x40,
	BfResolveTypeRefFlag_FromIndirectSource = 0x80, // Such as a type alias or a generic parameter 
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

	String mModuleName;
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
	bool mReifyQueued;
	bool mWantsIRIgnoreWrites;
	bool mHasGenericMethods;
	bool mIsSpecialModule; // vdata, unspecialized, external
	bool mIsScratchModule;
	bool mIsSpecializedMethodModuleRoot;
	bool mIsModuleMutable; // Set to false after writing module to disk, can be set back to true after doing extension module	
	bool mWroteToLib;
	bool mHadBuildError;
	bool mHadBuildWarning;	
	bool mIgnoreErrors;
	bool mIgnoreWarnings;	
	bool mSetIllegalSrcPosition;
	bool mHadIgnoredError;
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
	void NotImpl(BfAstNode* astNode);	
	void AddMethodReference(const BfMethodRef& methodRef, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	bool CheckProtection(BfProtection protection, bool allowProtected, bool allowPrivate);
	void GetAccessAllowed(BfTypeInstance* checkType, bool& allowProtected, bool& allowPrivate);
	bool CheckProtection(BfProtectionCheckFlags& flags, BfTypeInstance* memberOwner, BfProtection memberProtection, BfTypeInstance* lookupStartType);
	void SetElementType(BfAstNode* astNode, BfSourceElementType elementType);	
	BfError* Fail(const StringImpl& error, BfAstNode* refNode = NULL, bool isPersistent = false);
	BfError* FailAfter(const StringImpl& error, BfAstNode* refNode);	
	BfError* Warn(int warningNum, const StringImpl& warning, BfAstNode* refNode = NULL, bool isPersistent = false);
	void CheckRangeError(BfType* type, BfAstNode* refNode);
	bool CheckCircularDataError();
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
	BfIRValue CreateStringObjectValue(const StringImpl& str, int stringId, bool define);
	BfIRValue CreateStringCharPtr(const StringImpl& str, int stringId, bool define);
	int GetStringPoolIdx(BfIRValue constantStr, BfIRConstHolder* constHolder = NULL);
	String* GetStringPoolString(BfIRValue constantStr, BfIRConstHolder* constHolder = NULL);
	BfIRValue GetStringCharPtr(int stringId);
	BfIRValue GetStringCharPtr(BfIRValue strValue);	
	BfIRValue GetStringCharPtr(const StringImpl& str);
	BfIRValue GetStringObjectValue(int idx);
	BfIRValue GetStringObjectValue(const StringImpl& str, bool define = false);
	BfIRValue CreateGlobalConstValue(const StringImpl& name, BfIRValue constant, BfIRType type, bool external);
	void VariantToString(StringImpl& str, const BfVariant& variant);
	StringT<128> TypeToString(BfType* resolvedType);
	StringT<128> TypeToString(BfType* resolvedType, BfTypeNameFlags typeNameFlags, Array<String>* genericMethodParamNameOverrides = NULL);	
	void DoTypeToString(StringImpl& str, BfType* resolvedType, BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None, Array<String>* genericMethodParamNameOverrides = NULL);
	String MethodToString(BfMethodInstance* methodInst, BfMethodNameFlags methodNameFlags = BfMethodNameFlag_ResolveGenericParamNames, BfTypeVector* methodGenericArgs = NULL);
	void CurrentAddToConstHolder(BfIRValue& irVal);
	void ClearConstData();
	BfTypedValue GetTypedValueFromConstant(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType);
	BfIRValue ConstantToCurrent(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType);
	void ValidateCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeTargets attrTarget);
	void GetCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeDirective* attributesDirective, BfAttributeTargets attrType);
	BfCustomAttributes* GetCustomAttributes(BfAttributeDirective* attributesDirective, BfAttributeTargets attrType);
	void ProcessTypeInstCustomAttributes(bool& isPacked, bool& isUnion, bool& isCRepr, bool& isOrdered);
	void ProcessCustomAttributeData();	
	bool TryGetConstString(BfIRConstHolder* constHolder, BfIRValue irValue, StringImpl& str);
	BfVariant TypedValueToVariant(BfAstNode* refNode, const BfTypedValue& value, bool allowUndef = false);

	BfTypedValue FlushNullConditional(BfTypedValue result, bool ignoreNullable = false);	
	void NewScopeState(bool createLexicalBlock = true, bool flushValueScope = true); // returns prev scope data
	BfIRValue CreateAlloca(BfType* type, bool addLifetime = true, const char* name = NULL, BfIRValue arraySize = BfIRValue());
	BfIRValue CreateAllocaInst(BfTypeInstance* typeInst, bool addLifetime = true, const char* name = NULL);
	void AddStackAlloc(BfTypedValue val, BfAstNode* refNode, BfScopeData* scope, bool condAlloca = false, bool mayEscape = false);
	void RestoreScoreState_LocalVariables();
	void RestoreScopeState();	
	void MarkDynStack(BfScopeData* scope);
	void SaveStackState(BfScopeData* scope);
	BfIRValue ValueScopeStart();
	void ValueScopeEnd(BfIRValue valueScopeStart);

	void AddBasicBlock(BfIRBlock bb, bool activate = true);	
	void VisitEmbeddedStatement(BfAstNode* stmt, BfExprEvaluator* exprEvaluator = NULL, BfEmbeddedStatementFlags flags = BfEmbeddedStatementFlags_None);
	void VisitCodeBlock(BfBlock* block);
	void VisitCodeBlock(BfBlock* block, BfIRBlock continueBlock, BfIRBlock breakBlock, BfIRBlock fallthroughBlock, bool defaultBreak, bool* hadReturn = NULL, BfLabelNode* labelNode = NULL, bool closeScope = false);	
	void DoForLess(BfForEachStatement* forEachStmt);

	// Util
	void CreateReturn(BfIRValue val);	
	void EmitReturn(BfIRValue val);
	void EmitDefaultReturn();
	void EmitDeferredCall(BfModuleMethodInstance moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfDeferredBlockFlags flags = BfDeferredBlockFlag_None);
	bool AddDeferredCallEntry(BfDeferredCallEntry* deferredCallEntry, BfScopeData* scope);
	void AddDeferredBlock(BfBlock* block, BfScopeData* scope, Array<BfDeferredCapture>* captures = NULL);
	BfDeferredCallEntry* AddDeferredCall(const BfModuleMethodInstance& moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfScopeData* scope, BfAstNode* srcNode = NULL, bool bypassVirtual = false, bool doNullCheck = false);	
	void EmitDeferredCall(BfDeferredCallEntry& deferredCallEntry);
	void EmitDeferredCallProcessor(SLIList<BfDeferredCallEntry*>& callEntries, BfIRValue callTail);
	bool CanImplicitlyCast(BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags = BfCastFlags_None);
	bool AreSplatsCompatible(BfType* fromType, BfType* toType, bool* outNeedsMemberCasting);
	BfTypedValue BoxValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType /*Can be System.Object or interface*/, const BfAllocTarget& allocTarget, bool callDtor = true);
	BfIRValue CastToFunction(BfAstNode* srcNode, BfMethodInstance* methodInstance, BfType* toType, BfCastFlags castFlags = BfCastFlags_None);
	BfIRValue CastToValue(BfAstNode* srcNode, BfTypedValue val, BfType* toType, BfCastFlags castFlags = BfCastFlags_None, BfCastResultFlags* resultFlags = NULL);
	BfTypedValue Cast(BfAstNode* srcNode, const BfTypedValue& val, BfType* toType, BfCastFlags castFlags = BfCastFlags_None);
	BfPrimitiveType* GetIntCoercibleType(BfType* type);
	BfTypedValue GetIntCoercible(const BfTypedValue& typedValue);
	bool WantsDebugInfo();
	BfTypeOptions* GetTypeOptions();
	void CleanupFileInstances();
	void AssertErrorState();
	void AssertParseErrorState();
	void InitTypeInst(BfTypedValue typedValue, BfScopeData* scope, bool zeroMemory, BfIRValue dataSize);	
	BfIRValue AllocBytes(BfAstNode* refNode, const BfAllocTarget& allocTarget, BfType* type, BfIRValue sizeValue, BfIRValue alignValue, BfAllocFlags allocFlags/*bool zeroMemory, bool defaultToMalloc*/);
	BfIRValue GetMarkFuncPtr(BfType* type);
	BfIRValue GetDbgRawAllocData(BfType* type);
	BfIRValue AllocFromType(BfType* type, const BfAllocTarget& allocTarget, BfIRValue appendSizeValue = BfIRValue(), BfIRValue arraySize = BfIRValue(), int arrayDim = 0, /*bool isRawArrayAlloc = false, bool zeroMemory = true*/BfAllocFlags allocFlags = BfAllocFlags_ZeroMemory, int alignOverride = -1);
	void ValidateAllocation(BfType* type, BfAstNode* refNode);
	bool IsOptimized();
	void EmitAlign(BfIRValue& appendCurIdx, int align);
	void EmitAppendAlign(int align, int sizeMultiple = 0);
	BfIRValue AppendAllocFromType(BfType* type, BfIRValue appendSizeValue = BfIRValue(), int appendAllocAlign = 0, BfIRValue arraySize = BfIRValue(), int arrayDim = 0, bool isRawArrayAlloc = false, bool zeroMemory = true);	
	bool IsTargetingBeefBackend();
	bool WantsLifetimes();
	bool HasCompiledOutput();
	void SkipObjectAccessCheck(BfTypedValue typedVal);
	void EmitObjectAccessCheck(BfTypedValue typedVal);	
	void EmitEnsureInstructionAt();
	void EmitDynamicCastCheck(const BfTypedValue& targetValue, BfType* targetType, BfIRBlock trueBlock, BfIRBlock falseBlock, bool nullSucceeds = false);
	void EmitDynamicCastCheck(BfTypedValue typedVal, BfType* type, bool allowNull);
	void CheckStaticAccess(BfTypeInstance* typeInstance);
	BfFieldInstance* GetFieldByName(BfTypeInstance* typeInstance, const StringImpl& fieldName);
	BfTypedValue RemoveRef(BfTypedValue typedValue);
	BfTypedValue LoadOrAggregateValue(BfTypedValue typedValue);
	BfTypedValue LoadValue(BfTypedValue typedValue, BfAstNode* refNode = NULL, bool isVolatile = false);	
	void AggregateSplatIntoAddr(BfTypedValue typedValue, BfIRValue addrVal);
	BfTypedValue AggregateSplat(BfTypedValue typedValue, BfIRValue* valueArrPtr = NULL);	
	BfTypedValue MakeAddressable(BfTypedValue typedValue);
	BfTypedValue RemoveReadOnly(BfTypedValue typedValue);
	BfIRValue ExtractSplatValue(BfTypedValue typedValue, int componentIdx, BfType* wantType = NULL, bool* isAddr = NULL);
	BfTypedValue ExtractValue(BfTypedValue typedValue, BfFieldInstance* fieldInst, int fieldIdx);
	BfIRValue ExtractValue(BfTypedValue typedValue, int dataIdx);
	BfIRValue CreateIndexedValue(BfType* elementType, BfIRValue value, BfIRValue indexValue, bool isElementIndex = false);
	BfIRValue CreateIndexedValue(BfType* elementType, BfIRValue value, int indexValue, bool isElementIndex = false);
	bool CheckModifyValue(BfTypedValue& typedValue, BfAstNode* refNode, const char* modifyType = NULL);
	BfIRValue GetInterfaceSlotNum(BfTypeInstance* ifaceType);
	void HadSlotCountDependency();
	BfTypedValue ReferenceStaticField(BfFieldInstance* fieldInstance);
	BfTypedValue GetThis();
	BfLocalVariable* GetThisVariable();
	bool IsInGeneric();
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
	virtual void Visit(BfLabeledBlock* labeledBlock) override;
	virtual void Visit(BfRootNode* rootNode) override;
	virtual void Visit(BfInlineAsmStatement* asmStmt) override;

	// Type helpers	
	BfGenericExtensionEntry* BuildGenericExtensionInfo(BfGenericTypeInstance* genericTypeInst, BfTypeDef* partialTypeDef);
	bool BuildGenericParams(BfType* resolvedTypeRef);
	bool ValidateGenericConstraints(BfTypeReference* typeRef, BfGenericTypeInstance* genericTypeInstance, bool ignoreErrors);
	bool AreConstraintsSubset(BfGenericParamInstance* checkInner, BfGenericParamInstance* checkOuter);
	bool ShouldAllowMultipleDefinitions(BfTypeInstance* typeInst, BfTypeDef* firstDeclaringTypeDef, BfTypeDef* secondDeclaringTypeDef);
	void CheckInjectNewRevision(BfTypeInstance* typeInstance);
	bool InitType(BfType* resolvedTypeRef, BfPopulateType populateType);
	bool CheckAccessMemberProtection(BfProtection protection, BfType* memberType);
	bool CheckDefineMemberProtection(BfProtection protection, BfType* memberType);	
	void CheckMemberNames(BfTypeInstance* typeInst);	
	void AddDependency(BfType* usedType, BfType* usingType, BfDependencyMap::DependencyDependencyFlag flags);
	void AddCallDependency(BfMethodInstance* methodInstance, bool devirtualized = false);
	void AddFieldDependency(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfType* fieldType);		
	void TypeFailed(BfTypeInstance* typeInstance);
	bool IsAttribute(BfTypeInstance* typeInst);
	void PopulateGlobalContainersList(const BfGlobalLookup& globalLookup);
	void AddFailType(BfTypeInstance* typeInstance);
	void MarkDerivedDirty(BfTypeInstance* typeInst);
	void CheckAddFailType();
	bool PopulateType(BfType* resolvedTypeRef, BfPopulateType populateType = BfPopulateType_Data);
	int GenerateTypeOptions(BfCustomAttributes* customAttributes, BfTypeInstance* typeInstance, bool checkTypeName);
	void SetTypeOptions(BfTypeInstance* typeInstance);
	BfModuleOptions GetModuleOptions();
	BfCheckedKind GetDefaultCheckedKind();
	bool DoPopulateType(BfType* resolvedTypeRef, BfPopulateType populateType = BfPopulateType_Data);
	static BfModule* GetModuleFor(BfType* type);
	void DoTypeInstanceMethodProcessing(BfTypeInstance* typeInstance);
	void RebuildMethods(BfTypeInstance* typeInstance);
	void CreateStaticField(BfFieldInstance* fieldInstance, bool isThreadLocal = false);
	void ResolveConstField(BfTypeInstance* typeInst, BfFieldInstance* fieldInstance, BfFieldDef* field, bool forceResolve = false);
	BfTypedValue GetFieldInitializerValue(BfFieldInstance* fieldInstance, BfExpression* initializer = NULL, BfFieldDef* fieldDef = NULL, BfType* fieldType = NULL);
	void MarkFieldInitialized(BfFieldInstance* fieldInstance);
	bool IsThreadLocal(BfFieldInstance* fieldInstance);
	BfType* ResolveVarFieldType(BfTypeInstance* typeInst, BfFieldInstance* fieldInstance, BfFieldDef* field);	
	void FindSubTypes(BfTypeInstance* classType, SizedArrayImpl<int>* outVals, SizedArrayImpl<BfTypeInstance*>* exChecks, bool isInterfacePass);
	BfType* CheckUnspecializedGenericType(BfGenericTypeInstance* genericTypeInst, BfPopulateType populateType);
	BfTypeInstance* GetUnspecializedTypeInstance(BfTypeInstance* typeInst);
	BfArrayType* CreateArrayType(BfType* resolvedType, int dimensions);
	BfSizedArrayType* CreateSizedArrayType(BfType* resolvedType, int size);
	BfUnknownSizedArrayType* CreateUnknownSizedArrayType(BfType* resolvedType, BfType* sizeParam);
	BfPointerType* CreatePointerType(BfType* resolvedType);
	BfPointerType* CreatePointerType(BfTypeReference* typeRef);
	BfConstExprValueType* CreateConstExprValueType(const BfTypedValue& typedValue);
	BfBoxedType* CreateBoxedType(BfType* resolvedTypeRef);	
	BfTupleType* CreateTupleType(const BfTypeVector& fieldTypes, const Array<String>& fieldNames);
	BfTupleType* SantizeTupleType(BfTupleType* tupleType);
	BfRefType* CreateRefType(BfType* resolvedTypeRef, BfRefType::RefKind refKind = BfRefType::RefKind_Ref);
	BfRetTypeType* CreateRetTypeType(BfType* resolvedTypeRef);
	BfConcreteInterfaceType* CreateConcreteInterfaceType(BfTypeInstance* interfaceType);
	BfTypeInstance* GetWrappedStructType(BfType* type, bool allowSpecialized = true);
	BfTypeInstance* GetPrimitiveStructType(BfTypeCode typeCode);
	BfPrimitiveType* GetPrimitiveType(BfTypeCode typeCode);	
	BfMethodRefType* CreateMethodRefType(BfMethodInstance* methodInstance, bool mustAlreadyExist = false);
	BfType* FixIntUnknown(BfType* type);
	void FixIntUnknown(BfTypedValue& typedVal);	
	void FixIntUnknown(BfTypedValue& lhs, BfTypedValue& rhs);
	BfTypeDef* ResolveGenericInstanceDef(BfGenericInstanceTypeRef* genericTypeRef);	
	BfType* ResolveType(BfType* lookupType, BfPopulateType populateType = BfPopulateType_Data);	
	void ResolveGenericParamConstraints(BfGenericParamInstance* genericParamInstance, bool isUnspecialized);
	String GenericParamSourceToString(const BfGenericParamSource& genericParamSource);
	bool CheckGenericConstraints(const BfGenericParamSource& genericParamSource, BfType* checkArgType, BfAstNode* checkArgTypeRef, BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs = NULL, BfError** errorOut = NULL);
	BfIRValue AllocLocalVariable(BfType* type, const StringImpl& name, bool doLifetimeEnd = true);
	void DoAddLocalVariable(BfLocalVariable* localVar);
	BfLocalVariable* AddLocalVariableDef(BfLocalVariable* localVarDef, bool addDebugInfo = false, bool doAliasValue = false, BfIRValue declareBefore = BfIRValue(), BfIRInitType initType = BfIRInitType_NotSet);	
	bool TryLocalVariableInit(BfLocalVariable* localVar);
	void LocalVariableDone(BfLocalVariable* localVar, bool isMethodExit);
	void CreateDIRetVal();
	void CheckTupleVariableDeclaration(BfTupleExpression* tupleExpr, BfType* initType);
	void HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl, BfTupleExpression* tupleExpr, BfTypedValue initTupleValue, bool isReadOnly, bool isConst, bool forceAddr, BfIRBlock* declBlock = NULL);
	void HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl);
	void HandleCaseEnumMatch_Tuple(BfTypedValue tupleVal, const BfSizedArray<BfExpression*>& arguments, BfAstNode* tooFewRef, BfIRValue phiVal, BfIRBlock& matchedBlock, BfIRBlock falseBlock, bool& hadConditional, bool clearOutOnMismatch);
	BfTypedValue TryCaseEnumMatch(BfTypedValue enumVal, BfTypedValue tagVal, BfExpression* expr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, int& uncondTagId, bool& hadConditional, bool clearOutOnMismatch);
	BfTypedValue HandleCaseBind(BfTypedValue enumVal, const BfTypedValue& tagVal, BfEnumCaseBindExpression* bindExpr, BfIRBlock* eqBlock = NULL, BfIRBlock* notEqBlock = NULL, BfIRBlock* matchBlock = NULL, int* outEnumIdx = NULL);
	void TryInitVar(BfAstNode* checkNode, BfLocalVariable* varDecl, BfTypedValue initValue, BfTypedValue& checkResult);
	BfLocalVariable* HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfExprEvaluator* exprEvaluator = NULL);
	BfLocalVariable* HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfTypedValue val, bool updateSrcLoc = true, bool forceAddr = false);
	void CheckVariableDef(BfLocalVariable* variableDef);		
	BfScopeData* FindScope(BfAstNode* scopeName, BfMixinState* curMixinState, bool allowAcrossDeferredBlock);
	BfScopeData* FindScope(BfAstNode* scopeName, bool allowAcrossDeferredBlock);
	BfBreakData* FindBreakData(BfAstNode* scopeName);
	void EmitLifetimeEnds(BfScopeData* scopeData);
	void ClearLifetimeEnds();
	bool HasDeferredScopeCalls(BfScopeData* scope);	
	void EmitDeferredScopeCalls(bool useSrcPositions, BfScopeData* scope, BfIRBlock doneBlock = BfIRBlock());	
	void MarkScopeLeft(BfScopeData* scopeData);
	BfGenericParamType* GetGenericParamType(BfGenericParamKind paramKind, int paramIdx);
	BfType* ResolveGenericType(BfType* unspecializedType, const BfTypeVector& methodGenericArguments, bool allowFail = false);	
	bool IsUnboundGeneric(BfType* type);
	BfGenericParamInstance* GetGenericTypeParamInstance(int paramIdx);
	BfGenericParamInstance* GetGenericParamInstance(BfGenericParamType* type);	
	BfTypeInstance* GetBaseType(BfTypeInstance* typeInst);
	void HandleTypeGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, int typeGenericParamIdx);
	void HandleMethodGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, BfMethodDef* methodDef, int typeGenericParamIdx);
	BfType* ResolveTypeResult(BfTypeReference* typeRef, BfType* resolvedTypeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags);
	void ShowAmbiguousTypeError(BfAstNode* refNode, BfTypeDef* typeDef, BfTypeDef* otherTypeDef);
	void ShowGenericArgCountError(BfTypeReference* typeRef, int wantedGenericParams);	
	BfTypeDef* GetActiveTypeDef(BfTypeInstance* typeInstanceOverride = NULL, bool useMixinDecl = false); // useMixinDecl is useful for type lookup, but we don't want the decl project to limit what methods the user can call
	BfTypeDef* FindTypeDefRaw(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstance, BfTypeDef* useTypeDef, BfTypeLookupError* error);
	BfTypeDef* FindTypeDef(const BfAtomComposite& findName, int numGenericArgs = 0, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL);
	BfTypeDef* FindTypeDef(const StringImpl& typeName, int numGenericArgs = 0, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL);
	BfTypeDef* FindTypeDef(BfTypeReference* typeRef, BfTypeInstance* typeInstanceOverride = NULL, BfTypeLookupError* error = NULL, int numGenericParams = 0);
	BfTypedValue TryLookupGenericConstVaue(BfIdentifierNode* identifierNode, BfType* expectingType);
	void CheckTypeRefFixit(BfAstNode* typeRef, const char* appendName = NULL);
	void CheckIdentifierFixit(BfAstNode* node);
	void TypeRefNotFound(BfTypeReference* typeRef, const char* appendName = NULL);
	bool ValidateTypeWildcard(BfTypeReference* typeRef, bool isAttributeRef);
	BfType* ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfType* ResolveTypeRefAllowUnboundGenerics(BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, bool resolveGenericParam = true);
	BfType* ResolveTypeRef(BfAstNode* astNode, const BfSizedArray<BfTypeReference*>* genericArgs, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	//BfType* ResolveTypeRef(BfIdentifierNode* identifier, const BfSizedArray<BfTypeReference*>& genericArgs, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	BfType* ResolveTypeDef(BfTypeDef* typeDef, BfPopulateType populateType = BfPopulateType_Data);
	BfType* ResolveTypeDef(BfTypeDef* typeDef, const BfTypeVector& genericArgs, BfPopulateType populateType = BfPopulateType_Data);
	BfType* ResolveInnerType(BfType* outerType, BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, bool ignoreErrors = false);
	BfType* ResolveInnerType(BfType* outerType, BfIdentifierNode* identifier, BfPopulateType populateType = BfPopulateType_Data, bool ignoreErrors = false);
	BfTypeDef* GetCombinedPartialTypeDef(BfTypeDef* type);
	BfTypeInstance* GetOuterType(BfType* type);
	bool IsInnerType(BfType* checkInnerType, BfType* checkOuterType);	
	bool IsInnerType(BfTypeDef* checkInnerType, BfTypeDef* checkOuterType);
	bool TypeHasParent(BfTypeDef* checkChildTypeDef, BfTypeDef* checkParentTypeDef);
	BfTypeDef* FindCommonOuterType(BfTypeDef* type, BfTypeDef* type2);	
	bool TypeIsSubTypeOf(BfTypeInstance* srcType, BfTypeInstance* wantType, bool checkAccessibility = true);
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
	void DoMethodDeclaration(BfMethodDeclaration* methodDeclaration, bool isTemporaryFunc, bool addToWorkList = true);
	void AddMethodToWorkList(BfMethodInstance* methodInstance);
	bool IsInterestedInMethod(BfTypeInstance* typeInstance, BfMethodDef* methodDef);
	void CalcAppendAlign(BfMethodInstance* methodInst);
	BfTypedValue TryConstCalcAppend(BfMethodInstance* methodInst, SizedArrayImpl<BfIRValue>& args);
	BfTypedValue CallBaseCtorCalc(bool constOnly);
	void EmitCtorCalcAppend();	
	void CreateStaticCtor();	
	BfIRValue CreateDllImportGlobalVar(BfMethodInstance* methodInstance, bool define = false);
	void CreateDllImportMethod();			
	BfIRCallingConv GetCallingConvention(BfTypeInstance* typeInst, BfMethodDef* methodDef);
	BfIRCallingConv GetCallingConvention(BfMethodInstance* methodInstance);
	void SetupIRMethod(BfMethodInstance* methodInstance, BfIRFunction func, bool isInlined);
	void EmitCtorBody(bool& skipBody);
	void EmitDtorBody();
	void EmitEnumToStringBody();
	void EmitTupleToStringBody();		
	void EmitGCMarkValue(BfTypedValue& thisValue, BfType* checkType, int memberDepth, int curOffset, HashSet<int>& objectOffsets, BfModuleMethodInstance markFromGCThreadMethodInstance);
	void EmitGCMarkValue(BfTypedValue markVal, BfModuleMethodInstance markFromGCThreadMethodInstance);
	void EmitGCMarkMembers();
	void EmitGCFindTLSMembers();
	void EmitIteratorBlock(bool& skipBody);
	void EmitEquals(BfTypedValue leftValue, BfTypedValue rightValue, BfIRBlock exitBB);
	void CreateFakeCallerMethod(const String& funcName);
	void CallChainedMethods(BfMethodInstance* methodInstance, bool reverse = false);
	void AddHotDataReferences(BfHotDataReferenceBuilder* builder);
	void ProcessMethod_SetupParams(BfMethodInstance* methodInstance, BfType* thisType, bool wantsDIData, SizedArrayImpl<BfIRMDNode>* diParams);
	void ProcessMethod_ProcessDeferredLocals(int startIdx = 0);
	void ProcessMethod(BfMethodInstance* methodInstance, bool isInlineDup = false);
	void CreateDynamicCastMethod();
	void CreateValueTypeEqualsMethod();	
	BfIRFunction GetIntrinsic(BfMethodInstance* methodInstance, bool reportFailure = false);
	BfIRFunction GetBuiltInFunc(BfBuiltInFuncType funcType);
	BfIRValue CreateFunctionFrom(BfMethodInstance* methodInstance, bool tryExisting, bool isInlined);	
	void EvaluateWithNewScope(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfEvalExprFlags flags);
	BfTypedValue CreateValueFromExpression(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfType* wantTypeRef = NULL, BfEvalExprFlags flags = BfEvalExprFlags_None, BfType** outOrigType = NULL);
	BfTypedValue CreateValueFromExpression(BfExpression* expr, BfType* wantTypeRef = NULL, BfEvalExprFlags flags = BfEvalExprFlags_None, BfType** outOrigType = NULL);
	BfTypedValue GetOrCreateVarAddr(BfExpression* expr);
	BfMethodInstance* GetRawMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName = NULL);	
	BfMethodInstance* GetRawMethodInstance(BfTypeInstance* typeInstance, BfMethodDef* methodDef);
	BfMethodInstance* GetRawMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false, bool allowMixin = false);
	BfMethodInstance* GetUnspecializedMethodInstance(BfMethodInstance* methodInstance); // Unspecialized owner type and unspecialized method type	
	int GetGenericParamAndReturnCount(BfMethodInstance* methodInstance);	
	BfModule* GetSpecializedMethodModule(const Array<BfProject*>& projectList);	
	BfModuleMethodInstance GetMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName = NULL);	
	BfModuleMethodInstance GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false);
	BfModuleMethodInstance GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, const Array<BfType*>& paramTypes, bool checkBase = false);
	BfModuleMethodInstance GetInternalMethod(const StringImpl& methodName, int paramCount = -1);
	bool IsMethodImplementedAndReified(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount = -1, bool checkBase = false);
	bool HasMixin(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount, bool checkBase = false);
	bool CompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB); // Doesn't compare return types
	bool IsCompatibleInterfaceMethod(BfMethodInstance* methodA, BfMethodInstance* methodB);
	void UniqueSlotVirtualMethod(BfMethodInstance* methodInstance);	
	void CompareDeclTypes(BfTypeDef* newDeclType, BfTypeDef* prevDeclType, bool& isBetter, bool& isWorse);
	bool SlotVirtualMethod(BfMethodInstance* methodInstance, BfAmbiguityContext* ambiguityContext = NULL);	
	bool SlotInterfaceMethod(BfMethodInstance* methodInstance);	
	BfModuleMethodInstance ReferenceExternalMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	BfModule* GetOrCreateMethodModule(BfMethodInstance* methodInstance);
	BfModuleMethodInstance GetMethodInstance(BfTypeInstance* typeInst, BfMethodDef* methodDef, const BfTypeVector& methodGenericArguments, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None, BfTypeInstance* foreignType = NULL);	
	BfModuleMethodInstance GetMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None);
	BfMethodInstance* GetOuterMethodInstance(BfMethodInstance* methodInstance); // Only useful for local methods
	void SetupMethodIdHash(BfMethodInstance* methodInstance);

	// Type Data
	BfIRValue CreateClassVDataGlobal(BfTypeInstance* typeInstance, int* outNumElements = NULL, String* outMangledName = NULL);
	BfIRValue GetClassVDataPtr(BfTypeInstance* typeInstance);
	BfIRValue CreateClassVDataExtGlobal(BfTypeInstance* declTypeInst, BfTypeInstance* implTypeInst, int startVirtIdx);
	BfIRValue CreateTypeDataRef(BfType* type);
	BfIRValue CreateTypeData(BfType* type, Dictionary<int, int>& usedStringIdMap, bool forceReflectFields, bool needsTypeData, bool needsTypeNames, bool needsVData);
	BfIRValue FixClassVData(BfIRValue value);

public:
	BfModule(BfContext* context, const StringImpl& moduleName);
	virtual ~BfModule();

	void Init(bool isFullRebuild = true);		
	bool WantsFinishModule();
	void FinishInit();
	void ReifyModule();
	void UnreifyModule();
	void Cleanup();
	void StartNewRevision(RebuildKind rebuildKind = RebuildKind_All, bool force = false);
	void PrepareForIRWriting(BfTypeInstance* typeInst);
	void EnsureIRBuilder(bool dbgVerifyCodeGen = false);
	void DbgFinish();
	BfIRValue CreateForceLinkMarker(BfModule* module, String* outName);
	void ClearModuleData();	
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
