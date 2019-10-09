#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/HashSet.h"
#include "../BumpList.h"
#include "../Compiler/BfIRBuilder.h"
#include "BeContext.h"
#include "../X64.h"

NS_BF_BEGIN

class BeContext;

class BeValue;
class BeCallInst;

class BeValue;
class BeBlock;
class BeArgument;
class BeInst;
class BeNopInst;
class BeUnreachableInst;
class BeEnsureInstructionAtInst;
class BeUndefValueInst;
class BeExtractValueInst;
class BeInsertValueInst;
class BeNumericCastInst;
class BeBitCastInst;
class BeNegInst;
class BeNotInst;
class BeBinaryOpInst;
class BeCmpInst;
class BeObjectAccessCheckInst;
class BeAllocaInst;
class BeLifetimeExtendInst;
class BeAliasValueInst;
class BeLifetimeStartInst;
class BeLifetimeEndInst;
class BeLifetimeFenceInst;
class BeValueScopeStartInst;
class BeValueScopeRetainInst;
class BeValueScopeEndInst;
class BeLoadInst;
class BeStoreInst;
class BeSetCanMergeInst;
class BeMemSetInst;
class BeFenceInst;
class BeStackSaveInst;
class BeStackRestoreInst;
class BeGEPInst;
class BeBrInst;
class BeCondBrInst;
class BePhiIncoming;
class BePhiInst;
class BeSwitchInst;
class BeRetInst;
class BeCallInst;

class BeDbgVariable;
class BeDbgDeclareInst;

class BeValueVisitor
{
public:
	void VisitChild(BeValue* value);

	virtual void Visit(BeValue* beValue) {}
	virtual void Visit(BeBlock* beBlock) {}
	virtual void Visit(BeArgument* beArgument) {}	
	virtual void Visit(BeInst* beInst) {}
	virtual void Visit(BeNopInst* nopInst) {}
	virtual void Visit(BeUnreachableInst* unreachableInst) {}
	virtual void Visit(BeEnsureInstructionAtInst* ensureCodeAtInst) {}
	virtual void Visit(BeUndefValueInst* undefValue) {}
	virtual void Visit(BeExtractValueInst* extractValue) {}
	virtual void Visit(BeInsertValueInst* insertValue) {}
	virtual void Visit(BeNumericCastInst* castInst) {}	
	virtual void Visit(BeBitCastInst* castInst) {}	
	virtual void Visit(BeNegInst* negInst) {}	
	virtual void Visit(BeNotInst* notInst) {}	
	virtual void Visit(BeBinaryOpInst* binaryOpInst) {}
	virtual void Visit(BeFenceInst* fenceInst) {}
	virtual void Visit(BeStackSaveInst* stackSaveInst) {}
	virtual void Visit(BeStackRestoreInst* stackRestoreInst) {}
	virtual void Visit(BeCmpInst* cmpInst) {}
	virtual void Visit(BeObjectAccessCheckInst* objectAccessCheckInst) {}
	virtual void Visit(BeAllocaInst* allocaInst) {}
	virtual void Visit(BeAliasValueInst* aliasValueInst) {}
	virtual void Visit(BeLifetimeExtendInst* lifetimeExtendInst) {}
	virtual void Visit(BeLifetimeStartInst* lifetimeStartInst) {}
	virtual void Visit(BeLifetimeEndInst* lifetimeEndInst) {}
	virtual void Visit(BeLifetimeFenceInst* lifetimeFenceInst) {}
	virtual void Visit(BeValueScopeStartInst* valueScopeStartInst) {}
	virtual void Visit(BeValueScopeRetainInst* valueScopeRetainInst) {}
	virtual void Visit(BeValueScopeEndInst* valueScopeEndInst) {}
	virtual void Visit(BeLoadInst* allocaInst) {}
	virtual void Visit(BeStoreInst* storeInst) {}
	virtual void Visit(BeSetCanMergeInst* setCanMergeInst) {}
	virtual void Visit(BeMemSetInst* memSetInst) {}
	virtual void Visit(BeGEPInst* gepInst) {}
	virtual void Visit(BeBrInst* brInst) {}
	virtual void Visit(BeCondBrInst* condBrInst) {}
	virtual void Visit(BePhiIncoming* phiIncomingInst) {}
	virtual void Visit(BePhiInst* phiInst) {}
	virtual void Visit(BeSwitchInst* switchInst) {}
	virtual void Visit(BeRetInst* retInst) {}
	virtual void Visit(BeCallInst* callInst) {}
	
	//virtual void Visit(BeDbgVariable* dbgVariable) {}	
	virtual void Visit(BeDbgDeclareInst* dbgDeclareInst) {}
};

class BeModule;
class BeFunction;
class BeDbgLoc;

class BeInliner : public BeValueVisitor
{
public:
	BumpAllocator* mAlloc;
	OwnedVector<BeValue>* mOwnedValueVec;
	Dictionary<BeValue*, BeValue*> mValueMap;	
	Dictionary<BeDbgLoc*, BeDbgLoc*> mInlinedAtMap;
	
	BeModule* mModule;
	BeFunction* mSrcFunc;
	BeFunction* mDestFunc;
	BeCallInst* mCallInst;	
	BeBlock* mDestBlock;	
	BeDbgLoc* mSrcDbgLoc;	
	BeDbgLoc* mDestDbgLoc;

public:
	BeInliner()
	{
		mSrcDbgLoc = NULL;
		mDestDbgLoc = NULL;
	}

	BeValue* Remap(BeValue* srcValue);
	BeDbgLoc* ExtendInlineDbgLoc(BeDbgLoc* srcInlineAt);
	void AddInst(BeInst* destInst, BeInst* srcInst);

	template <typename T>
	T* AllocInst(T* srcInst)
	{
		auto inst = mAlloc->Alloc<T>();
		AddInst(inst, srcInst);
		return inst;
	}

	template <typename T>
	T* AllocInstOwned(T* srcInst)
	{
		auto inst = mOwnedValueVec->Alloc<T>();
		AddInst(inst, srcInst);
		return inst;
	}	

	virtual void Visit(BeValue* beValue) override;
	virtual void Visit(BeBlock* beBlock) override;
	virtual void Visit(BeArgument* beArgument) override;
	virtual void Visit(BeInst* beInst) override;
	virtual void Visit(BeNopInst* nopInst) override;
	virtual void Visit(BeUnreachableInst* unreachableInst) override;
	virtual void Visit(BeEnsureInstructionAtInst* ensureCodeAtInst) override;
	virtual void Visit(BeUndefValueInst* undefValue) override;
	virtual void Visit(BeExtractValueInst* extractValue) override;
	virtual void Visit(BeInsertValueInst* insertValue) override;
	virtual void Visit(BeNumericCastInst* castInst) override;
	virtual void Visit(BeBitCastInst* castInst) override;
	virtual void Visit(BeNegInst* negInst) override;
	virtual void Visit(BeNotInst* notInst) override;
	virtual void Visit(BeBinaryOpInst* binaryOpInst) override;
	virtual void Visit(BeCmpInst* cmpInst) override;
	virtual void Visit(BeFenceInst* fenceInst) override;
	virtual void Visit(BeStackSaveInst* stackSaveInst) override;
	virtual void Visit(BeStackRestoreInst* stackRestoreInst) override;
	virtual void Visit(BeObjectAccessCheckInst* objectAccessCheckInst) override;
	virtual void Visit(BeAllocaInst* allocaInst) override;
	virtual void Visit(BeAliasValueInst* aliasValueInst) override;
	virtual void Visit(BeLifetimeStartInst* lifetimeStartInst) override;
	virtual void Visit(BeLifetimeExtendInst* lifetimeExtendInst) override;
	virtual void Visit(BeLifetimeEndInst* lifetimeEndInst) override;
	virtual void Visit(BeLifetimeFenceInst* lifetimeFenceInst) override;
	virtual void Visit(BeValueScopeStartInst* valueScopeStartInst) override;
	virtual void Visit(BeValueScopeRetainInst* valueScopeRetainInst) override;
	virtual void Visit(BeValueScopeEndInst* valueScopeEndInst) override;
	virtual void Visit(BeLoadInst* allocaInst) override;
	virtual void Visit(BeStoreInst* storeInst) override;
	virtual void Visit(BeSetCanMergeInst* setCanMergeInst) override;
	virtual void Visit(BeMemSetInst* memSetInst) override;
	virtual void Visit(BeGEPInst* gepInst) override;
	virtual void Visit(BeBrInst* brInst) override;
	virtual void Visit(BeCondBrInst* condBrInst) override;
	virtual void Visit(BePhiIncoming* phiIncomingInst) override;
	virtual void Visit(BePhiInst* phiInst) override;
	virtual void Visit(BeSwitchInst* switchInst) override;
	virtual void Visit(BeRetInst* retInst) override;
	virtual void Visit(BeCallInst* callInst) override;
		
	virtual void Visit(BeDbgDeclareInst* dbgDeclareInst) override;
};

class BeValue : public BeHashble
{
public:
#ifdef _DEBUG
	bool mLifetimeEnded;
	bool mWasRemoved;
	BeValue()
	{
		mLifetimeEnded = false;
		mWasRemoved = false;
	}
#endif

	virtual ~BeValue()
	{

	}

	static const int TypeId = 0;
	virtual void Accept(BeValueVisitor* beVisitor) = 0;
	virtual bool TypeIdIsA(int typeId) = 0;		
	virtual BeValue* DynCast(int typeId)
	{
		if (TypeIdIsA(typeId))
			return this;
		return NULL;
	}
	static bool ClassIsA(int typeId)
	{
		return typeId == 0;
	}
	virtual int GetTypeId() { return TypeId; }


public:
	virtual BeType* GetType()
	{
		return NULL;
	}

	virtual void SetName(const StringImpl& name)
	{

	}
};

#define BE_VALUE_TYPE(name, TBase) static const int TypeId = __LINE__; \
	virtual void Accept(BeValueVisitor* beVisitor) override { beVisitor->Visit(this); } \
	static bool ClassIsA(int typeId) { return (typeId == TypeId) || TBase::ClassIsA(typeId); } \
	virtual bool TypeIdIsA(int typeId) override { return ClassIsA(typeId); } \
	virtual int GetTypeId() override { return TypeId; } \
	TBase* ToBase() { return (TBase*)this; }

template <typename T>
T* BeValueDynCast(BeValue* value)
{
	if (value == NULL)
		return NULL;
	BeValue* result = value->DynCast(T::TypeId);
	return (T*)result;
}

class BeBlock;
class BeInst;
class BeModule;
class BeDbgLoc;
class BeMDNode;
class BeGlobalVariable;

class BeConstant : public BeValue
{
public:
	BE_VALUE_TYPE(BeConstant, BeValue);
		
	BeType* mType;	
	union
	{
		bool mBool;
		int64 mInt64;
		int32 mInt32;
		int16 mInt16;
		int8 mInt8;
		uint64 mUInt64;
		uint32 mUInt32;
		uint16 mUInt16;
		uint8 mUInt8;
		uint8 mChar;
		uint32 mChar32;
		double mDouble;		
		//BeType* mTypeParam;
		//BeGlobalVariable* mGlobalVar;
		BeConstant* mTarget;
	};

	bool IsNull()
	{
		if (mType->mTypeCode == BeTypeCode_NullPtr)
			return true;
		return false;
	}

	virtual BeType* GetType();	
	virtual void GetData(Array<uint8>& data);
	virtual void HashContent(BeHashContext& hashCtx) override;
};

class BeCastConstant : public BeConstant
{
public:
	BE_VALUE_TYPE(BeCastConstant, BeConstant);

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mType->HashReference(hashCtx);
		mTarget->HashReference(hashCtx);
	}
};

class BeGEPConstant : public BeConstant
{
public:
	BE_VALUE_TYPE(BeGEPConstant, BeConstant);	
	int mIdx0;
	int mIdx1;

	virtual BeType* GetType();

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);		
		mTarget->HashReference(hashCtx);
		hashCtx.Mixin(mIdx0);
		hashCtx.Mixin(mIdx1);
	}
};

class BeStructConstant : public BeConstant
{
public:
	BE_VALUE_TYPE(BeStructConstant, BeConstant);	

	SizedArray<BeConstant*, 4> mMemberValues;

	virtual void GetData(Array<uint8>& data) override;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mMemberValues.size());
		for (auto member : mMemberValues)
			member->HashReference(hashCtx);
	}
};

class BeStringConstant : public BeConstant
{
public:
	BE_VALUE_TYPE(BeStringConstant, BeConstant);	

	String mString;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mString);		
	}
};

class BeGlobalVariable : public BeConstant
{
public:
	BE_VALUE_TYPE(BeGlobalVariable, BeConstant);

	BeModule* mModule;
	String mName;
	BeConstant* mInitializer;
	BfIRLinkageType mLinkageType;
	bool mIsConstant;
	bool mIsTLS;
	int mAlign;
	bool mUnnamedAddr;

	virtual BeType* GetType();	
	
	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mName);
		if (mInitializer != NULL)
			mInitializer->HashReference(hashCtx);
		hashCtx.Mixin(mLinkageType);
		hashCtx.Mixin(mIsConstant);
		hashCtx.Mixin(mIsTLS);
		hashCtx.Mixin(mAlign);
		hashCtx.Mixin(mUnnamedAddr);
	}	
};

class BeFunctionParam
{
public:
	String mName;
	bool mStructRet;
	bool mNoAlias;
	bool mNoCapture;
	bool mZExt;
	int mDereferenceableSize;

	BeFunctionParam()
	{
		mStructRet = false;
		mNoAlias = false;
		mNoCapture = false;
		mZExt = false;
		mDereferenceableSize = -1;
	}
};

class BeDbgFunction;

class BeIntrinsic : public BeValue
{
public:
	BE_VALUE_TYPE(BeIntrinsic, BeValue);
	
	BfIRIntrinsic mKind;
	BeType* mReturnType;

	BeIntrinsic()
	{		
		mReturnType = NULL;
	}

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mKind);
	}	
};

class BeFunction : public BeConstant
{
public:
	BE_VALUE_TYPE(BeFunction, BeConstant);

	BeModule* mModule;	
	String mName;
	BfIRLinkageType mLinkageType;	
	bool mAlwaysInline;		
	bool mNoUnwind;
	bool mUWTable;
	bool mNoReturn;
	bool mDidInlinePass;
	bool mNoFramePointerElim;
	bool mIsDLLExport;
	bool mIsDLLImport;
	BfIRCallingConv mCallingConv;
	Array<BeBlock*> mBlocks;		
	Array<BeFunctionParam> mParams;
	BeDbgFunction* mDbgFunction;
	BeGlobalVariable* mRemapBindVar;
	int mCurElementId;

public:
	BeFunction()
	{
		mCallingConv = BfIRCallingConv_CDecl;
		mLinkageType = BfIRLinkageType_External;
		mModule = NULL;
		mDbgFunction = NULL;
		mAlwaysInline = false;
		mDidInlinePass = false;		
		mNoUnwind = false;
		mUWTable = false;
		mNoReturn = false;
		mNoFramePointerElim = false;
		mIsDLLExport = false;
		mIsDLLImport = false;
		mRemapBindVar = NULL;
		mCurElementId = 0;
	}	

	BeFunctionType* GetFuncType()
	{
		BF_ASSERT(mType->IsPointer());
		return (BeFunctionType*)(((BePointerType*)mType)->mElementType);
	}

	bool IsDecl()
	{
		return mBlocks.size() == 0;
	}

	bool HasStructRet()
	{
		return (!mParams.IsEmpty()) && (mParams[0].mStructRet);
	}
	
	virtual void HashContent(BeHashContext& hashCtx) override;
};

class BeBlock : public BeValue
{
public:
	BE_VALUE_TYPE(BeBlock, BeValue);

	String mName;
	Array<BeInst*> mInstructions;
	BeFunction* mFunction;

public:
	bool IsEmpty();	

	virtual void HashContent(BeHashContext& hashCtx) override;
};


//////////////////////////////////////////////////////////////////////////

class BeInst : public BeValue
{
public:
	BE_VALUE_TYPE(BeInst, BeValue);

	BeBlock* mParentBlock;
	const char* mName;
	BeDbgLoc* mDbgLoc;
	
public:	
	BeContext* GetContext();
	BeModule* GetModule();

	virtual bool CanBeReferenced()
	{
		return GetType() != NULL;
	}

	virtual void SetName(const StringImpl& name) override;	

	BeInst()
	{
		mParentBlock = NULL;
		mName = NULL;
		mDbgLoc = NULL;
	}

	virtual void HashInst(BeHashContext& hashCtx) = 0;
	virtual void HashContent(BeHashContext& hashCtx) override;	
};

class BeNopInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeNopInst, BeInst);

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
	}
};

class BeUnreachableInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeUnreachableInst, BeInst);

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
	}
};

class BeEnsureInstructionAtInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeEnsureInstructionAtInst, BeInst);

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
	}
};

class BeUndefValueInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeUndefValueInst, BeInst);

	BeType* mType;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mType->HashReference(hashCtx);
	}
};

class BeExtractValueInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeExtractValueInst, BeInst);

	BeValue* mAggVal;	
	int mIdx;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mAggVal->HashReference(hashCtx);
		hashCtx.Mixin(mIdx);
	}
};

class BeInsertValueInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeInsertValueInst, BeInst);

	BeValue* mAggVal;
	BeValue* mMemberVal;
	int mIdx;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mAggVal->HashReference(hashCtx);
		mMemberVal->HashReference(hashCtx);
		hashCtx.Mixin(mIdx);
	}
};

class BeNumericCastInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeNumericCastInst, BeInst);

	BeValue* mValue;
	BeType* mToType;
	bool mValSigned;
	bool mToSigned;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
		mToType->HashReference(hashCtx);
		hashCtx.Mixin(mValSigned);
		hashCtx.Mixin(mToSigned);
	}
};

class BeBitCastInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeBitCastInst, BeInst);

	BeValue* mValue;
	BeType* mToType;
	
	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
		mToType->HashReference(hashCtx);
	}
};

class BeNegInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeNegInst, BeInst);

	BeValue* mValue;	
	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
	}
};

class BeNotInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeNotInst, BeInst);

	BeValue* mValue;
	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
	}
};

enum BeBinaryOpKind
{
	BeBinaryOpKind_None,
	BeBinaryOpKind_Add,
	BeBinaryOpKind_Subtract,
	BeBinaryOpKind_Multiply,
	BeBinaryOpKind_SDivide,
	BeBinaryOpKind_UDivide,
	BeBinaryOpKind_SModulus,
	BeBinaryOpKind_UModulus,
	BeBinaryOpKind_BitwiseAnd,
	BeBinaryOpKind_BitwiseOr,
	BeBinaryOpKind_ExclusiveOr,
	BeBinaryOpKind_LeftShift,	
	BeBinaryOpKind_RightShift,	
	BeBinaryOpKind_ARightShift,
	BeBinaryOpKind_Equality,
	BeBinaryOpKind_InEquality,
	BeBinaryOpKind_GreaterThan,
	BeBinaryOpKind_LessThan,
	BeBinaryOpKind_GreaterThanOrEqual,
	BeBinaryOpKind_LessThanOrEqual,		
};

class BeBinaryOpInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeBinaryOpInst, BeInst);

	BeBinaryOpKind mOpKind;
	BeValue* mLHS;
	BeValue* mRHS;

	virtual BeType* GetType() override;	

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mOpKind);
		mLHS->HashReference(hashCtx);
		mRHS->HashReference(hashCtx);
	}
};

enum BeCmpKind
{
	BeCmpKind_None,

	BeCmpKind_SLT,
	BeCmpKind_ULT,
	BeCmpKind_SLE,
	BeCmpKind_ULE,
	BeCmpKind_EQ,
	BeCmpKind_NE,
	BeCmpKind_SGT,
	BeCmpKind_UGT,
	BeCmpKind_SGE,
	BeCmpKind_UGE
};

class BeCmpInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeCmpInst, BeInst);

	BeValue* mLHS;
	BeValue* mRHS;
	BeCmpKind mCmpKind;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mLHS->HashReference(hashCtx);
		mRHS->HashReference(hashCtx);
		hashCtx.Mixin(mCmpKind);
	}
};

class BeObjectAccessCheckInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeObjectAccessCheckInst, BeInst);

	BeValue* mValue;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
	}
};

class BeAllocaInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeAllocaInst, BeInst);

	BeType* mType;
	BeValue* mArraySize;
	bool mNoChkStk;
	bool mForceMem;

public:
	virtual BeType* GetType() override; 

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mType->HashReference(hashCtx);
		if (mArraySize != NULL)
			mArraySize->HashReference(hashCtx);
		hashCtx.Mixin(mNoChkStk);
		hashCtx.Mixin(mForceMem);
	}
};

class BeAliasValueInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeAliasValueInst, BeInst);

	BeValue* mPtr;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mPtr->HashReference(hashCtx);
	}

	virtual BeType* GetType() override
	{
		return mPtr->GetType();
	}
};

class BeLifetimeStartInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeLifetimeStartInst, BeInst);

	BeValue* mPtr;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mPtr->HashReference(hashCtx);
	}
};

class BeLifetimeEndInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeLifetimeEndInst, BeInst);

	BeValue* mPtr;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mPtr->HashReference(hashCtx);
	}
};

class BeLifetimeFenceInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeLifetimeFenceInst, BeInst);

	BeBlock* mFenceBlock; // Lifetime is blocked from extending into the end of this block
	BeValue* mPtr;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mFenceBlock->HashReference(hashCtx);
		mPtr->HashReference(hashCtx);
	}
};

class BeLifetimeExtendInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeLifetimeExtendInst, BeInst);

	BeValue* mPtr;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mPtr->HashReference(hashCtx);
	}
};

class BeValueScopeStartInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeValueScopeStartInst, BeInst);

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);		
	}
};

class BeValueScopeRetainInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeValueScopeRetainInst, BeInst);

	BeValue* mValue;	

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
	}
};

class BeValueScopeEndInst : public BeInst
{
public:	
	BE_VALUE_TYPE(BeValueScopeEndInst, BeInst);

	BeValueScopeStartInst* mScopeStart;
	bool mIsSoft;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mScopeStart->HashReference(hashCtx);
		hashCtx.Mixin(mIsSoft);
	}
};

class BeLoadInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeLoadInst, BeInst);

	BeValue* mTarget;
	bool mIsVolatile;

public:
	virtual BeType* GetType() override;	

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mTarget->HashReference(hashCtx);
	}
};

class BeStoreInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeStoreInst, BeInst);

	BeValue* mVal;
	BeValue* mPtr;
	bool mIsVolatile;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mVal->HashReference(hashCtx);
		mPtr->HashReference(hashCtx);
	}
};

class BeSetCanMergeInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeSetCanMergeInst, BeInst);

	BeValue* mVal;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mVal->HashReference(hashCtx);
	}
};

class BeMemSetInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeGEPInst, BeInst);

	BeValue* mAddr;
	BeValue* mVal;
	BeValue* mSize;
	int mAlignment;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mAddr->HashReference(hashCtx);
		mVal->HashReference(hashCtx);
		mSize->HashReference(hashCtx);
		hashCtx.Mixin(mAlignment);
	}
};

class BeFenceInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeFenceInst, BeInst);

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);		
	}
};

class BeStackSaveInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeStackSaveInst, BeInst);

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);		
	}

	virtual BeType* GetType() override
	{
		return GetContext()->GetPrimitiveType(BeTypeCode_NullPtr);
	}
};

class BeStackRestoreInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeStackRestoreInst, BeInst);

	BeValue* mStackVal;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mStackVal->HashReference(hashCtx);
	}
};

class BeGEPInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeGEPInst, BeInst);

	BeValue* mPtr;
	BeValue* mIdx0;
	BeValue* mIdx1;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mPtr->HashReference(hashCtx);
		mIdx0->HashReference(hashCtx);
		if (mIdx1 != NULL)
			mIdx1->HashReference(hashCtx);
	}
};

class BeBrInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeBrInst, BeInst);

	BeBlock* mTargetBlock;
	bool mNoCollapse;
	bool mIsFake;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mTargetBlock->HashReference(hashCtx);
		hashCtx.Mixin(mNoCollapse);
		hashCtx.Mixin(mIsFake);
	}
};

class BeCondBrInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeCondBrInst, BeInst);

	BeValue* mCond;
	BeBlock* mTrueBlock;
	BeBlock* mFalseBlock;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mCond->HashReference(hashCtx);
		mTrueBlock->HashReference(hashCtx);
		mFalseBlock->HashReference(hashCtx);
	}
};

class BePhiIncoming : public BeValue
{
public:
	BE_VALUE_TYPE(BePhiIncoming, BeValue);

	BeBlock* mBlock;
	BeValue* mValue;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mBlock->HashReference(hashCtx);
		mValue->HashReference(hashCtx);
	}
};

class BePhiInst : public BeInst
{
public:
	BE_VALUE_TYPE(BePhiInst, BeInst);

	BeType* mType;
	SizedArray<BePhiIncoming*, 4> mIncoming;

	virtual BeType* GetType() override;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mType->HashReference(hashCtx);
		hashCtx.Mixin(mIncoming.size());
		for (auto incoming : mIncoming)
		{
			incoming->mBlock->HashReference(hashCtx);
			incoming->mValue->HashReference(hashCtx);
		}
	}
};

class BeSwitchCase
{
public:
	BeConstant* mValue;
	BeBlock* mBlock;	
};

class BeSwitchInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeSwitchInst, BeInst);

	BeValue* mValue;
	BeBlock* mDefaultBlock;
	Array<BeSwitchCase> mCases;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mValue->HashReference(hashCtx);
		mDefaultBlock->HashReference(hashCtx);
		for (auto& caseVal : mCases)
		{
			caseVal.mValue->HashReference(hashCtx);
			caseVal.mBlock->HashReference(hashCtx);
		}
	}
};

class BeRetInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeRetInst, BeInst);

	BeValue* mRetValue;

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		if (mRetValue != NULL)
			mRetValue->HashReference(hashCtx);
	}
};

class BeCallInst : public BeInst
{
public:
	struct Arg
	{
		BeValue* mValue;
		int mDereferenceableSize;
		bool mStructRet;
		bool mZExt;
		bool mNoAlias;
		bool mNoCapture;		
		Arg()
		{
			mValue = NULL;
			mStructRet = false;
			mZExt = false;
			mNoAlias = false;
			mNoCapture = false;
			mDereferenceableSize = -1;
		}
	};

public:
	BE_VALUE_TYPE(BeCallInst, BeInst);	

	BeValue* mInlineResult;
	BeValue* mFunc;
	SizedArray<Arg, 4> mArgs;
	BfIRCallingConv mCallingConv;
	bool mNoReturn;
	bool mTailCall;		

	virtual BeType* GetType() override;

	BeCallInst()
	{
		mInlineResult = NULL;
		mFunc = NULL;
		mCallingConv = BfIRCallingConv_CDecl;
		mNoReturn = false;
		mTailCall = false;		
	}

	virtual void HashInst(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		if (mInlineResult != NULL)
			mInlineResult->HashReference(hashCtx);
		mFunc->HashReference(hashCtx);
		for (auto& arg : mArgs)
		{			
			arg.mValue->HashReference(hashCtx);
			hashCtx.Mixin(arg.mStructRet);
			hashCtx.Mixin(arg.mZExt);
		}
		hashCtx.Mixin(mCallingConv);
		hashCtx.Mixin(mNoReturn);
		hashCtx.Mixin(mTailCall);		
	}

	bool HasStructRet()
	{
		return (!mArgs.IsEmpty()) && (mArgs[0].mStructRet);
	}
};

class BeArgument : public BeValue
{
public:
	BE_VALUE_TYPE(BeArgument, BeValue);

	BeModule* mModule;
	int mArgIdx;

	virtual BeType* GetType() override;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mArgIdx);
	}
};

struct BeDumpContext
{
public:
	BeModule* mModule;
	Dictionary<BeValue*, String> mValueNameMap;
	Dictionary<String, int> mSeenNames;

	void ToString(StringImpl& str, BeValue* value, bool showType = true, bool mdDrillDown = false);
	void ToString(StringImpl& str, BeType* type);
	void ToString(StringImpl& str, BeDbgFunction* dbgFunction, bool showScope);
	static void ToString(StringImpl& str, int val);
	static void ToString(StringImpl& str, BeCmpKind cmpKind);
	static void ToString(StringImpl& str, BeBinaryOpKind opKind);

	String ToString(BeValue* value, bool showType = true, bool mdDrillDown = false);
	String ToString(BeType* type);
	String ToString(BeDbgFunction* dbgFunction);
	static String ToString(int val);
	static String ToString(BeCmpKind cmpKind);
	static String ToString(BeBinaryOpKind opKind);

public:
	BeDumpContext()
	{
		mModule = NULL;
	}
};

//////////////////////////////////////////////////////////////////////////

class BeDbgVariable;

class BeDbgDeclareInst : public BeInst
{
public:
	BE_VALUE_TYPE(BeDbgDeclareInst, BeInst);

public:
	BeDbgVariable* mDbgVar;
	BeValue* mValue;
	bool mIsValue;

	virtual void HashInst(BeHashContext& hashCtx) override;
	
};

class BeMDNode : public BeValue
{
public:
	BE_VALUE_TYPE(BeMDNode, BeValue);

public:
	virtual ~BeMDNode()
	{

	}

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
	}
};

class BeDbgFile;

class BeDbgLoc : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgLoc, BeMDNode);

public:
	int mLine;
	int mColumn;
	BeMDNode* mDbgScope;
	BeDbgLoc* mDbgInlinedAt;
	int mIdx;	
	bool mHadInline;

public:
	BeDbgLoc()
	{		
	}

	int GetInlineDepth();	
	int GetInlineMatchDepth(BeDbgLoc* other);
	BeDbgLoc* GetInlinedAt(int idx = 0);
	BeDbgLoc* GetRoot();
	BeDbgFunction* GetDbgFunc();
	BeDbgFile* GetDbgFile();	

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mLine);
		hashCtx.Mixin(mColumn);
		if (mDbgScope != NULL)
			mDbgScope->HashReference(hashCtx);
		else
			hashCtx.Mixin(-1);
		if (mDbgInlinedAt != NULL)
			mDbgInlinedAt->HashReference(hashCtx);		
	}
};

class BeDbgLexicalBlock : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgLexicalBlock, BeMDNode);

public:
	BeDbgFile* mFile;
	BeMDNode* mScope;
	BeBlock* mLastBeBlock;
	int mId;

	virtual void HashContent(BeHashContext& hashCtx) override;	
};

class BeDbgNamespace : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgNamespace, BeMDNode);

public:
	BeMDNode* mScope;
	String mName;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mScope->HashReference(hashCtx);
		hashCtx.MixinStr(mName);
	}
};

class BeDbgType : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgType, BeMDNode);
	
public:
	int mSize;
	int mAlign;
	int mCvDeclTypeId;
	int mCvDefTypeId;
	BumpList<BeDbgType*> mDerivedTypes;

	BeDbgType()
	{
		mSize = -1;
		mAlign = -1;
		mCvDeclTypeId = -1;
		mCvDefTypeId = -1;
	}

	BeDbgType* FindDerivedType(int typeId)
	{
		for (auto derivedType : mDerivedTypes)
		{
			if (derivedType->GetTypeId() == typeId)
				return derivedType;
		}
		return NULL;
	}

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mSize);
		hashCtx.Mixin(mAlign);
	}
};

class BeDbgBasicType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgBasicType, BeDbgType);

public:
	String mName;	
	int mEncoding;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mSize);
		hashCtx.Mixin(mAlign);
		hashCtx.MixinStr(mName);
		hashCtx.Mixin(mEncoding);
	}
};

class BeDbgArrayType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgArrayType, BeDbgType);

public:
	BeDbgType* mElement;	
	int mNumElements;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mSize);
		hashCtx.Mixin(mAlign);
		hashCtx.Mixin(mNumElements);
		mElement->HashReference(hashCtx);				
	}
};

class BeDbgArtificialType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgArtificialType, BeDbgType);

public:
	BeDbgType* mElement;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mElement->HashReference(hashCtx);
	}
};

class BeDbgConstType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgConstType, BeDbgType);

public:
	BeDbgType* mElement;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mElement->HashReference(hashCtx);
	}
};

class BeDbgReferenceType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgReferenceType, BeDbgType);

public:
	BeDbgType* mElement;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mElement->HashReference(hashCtx);
	}
};

class BeDbgPointerType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgPointerType, BeDbgType);		

public:
	BeDbgType* mElement;	

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mElement->HashReference(hashCtx);
	}
};

class BeDbgInheritance : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgInheritance, BeMDNode);

public:
	BeDbgType* mBaseType;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mBaseType->HashReference(hashCtx);
	}
};

class BeDbgStructMember : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgStructMember, BeMDNode);

public:
	String mName;
	BeDbgType* mType;
	int mFlags;
	int mOffset;
	bool mIsStatic;
	BeValue* mStaticValue;	

public:
	BeDbgStructMember()
	{
		mType = NULL;
		mFlags = 0;
		mOffset = -1;
		mIsStatic = false;
		mStaticValue = NULL;
	}

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mName);
		mType->HashReference(hashCtx);
		hashCtx.Mixin(mFlags);
		hashCtx.Mixin(mOffset);
		hashCtx.Mixin(mIsStatic);
		if (mStaticValue != NULL)
			mStaticValue->HashReference(hashCtx);
	}
};

class BeDbgFunctionType : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgFunctionType, BeMDNode);

public:
	BeDbgType* mReturnType;
	Array<BeDbgType*> mParams;
	
public:
	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		if (mReturnType != NULL)
			mReturnType->HashReference(hashCtx);
		hashCtx.Mixin(mParams.size());
		for (auto param : mParams)
			param->HashReference(hashCtx);
	}
};

/*class BeDbgVariableRange
{
public:
	enum Kind
	{
		Kind_None,
		Kind_Reg, // Direct reg usage
		Kind_Indexed // [RBP+8] type usage (spilled)
	};

public:
	int mCodeStartOfs;
	int mCodeRange;
	Kind mKind;
	X64CPURegister mReg;
	int mOfs;

public:
	BeDbgVariableRange()
	{
		mCodeStartOfs = -1;
		mCodeRange = -1;
		mKind = Kind_None;
		mReg = X64Reg_None;
		mOfs = 0;
	}
};*/

class BeDbgVariableRange
{
public:
	int mOffset;
	int mLength;
};

class BeDbgVariableLoc
{
public:
	enum Kind
	{
		Kind_None,
		Kind_Reg, // Direct reg usage
		Kind_Indexed, // [RBP+8] type usage (spilled)
		Kind_SymbolAddr
	};

	Kind mKind;	
	X64CPURegister mReg;	
	int mOfs;	

public:
	BeDbgVariableLoc()
	{
		mKind = Kind_None;
		mReg = X64Reg_None;
		mOfs = 0;
	}
};

class BeDbgVariable : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgVariable, BeMDNode);

public:
	String mName;
	BeDbgType* mType;	
	BeValue* mValue;
	int mParamNum;
	BfIRInitType mInitType;
	BfIRInitType mPendingInitType;
	bool mPendingInitDef;
	BeMDNode* mScope;

	BeDbgLoc* mDeclDbgLoc;
	BeDbgVariableLoc mPrimaryLoc;
	BeDbgVariableLoc mSavedLoc;	
	int mDeclStart;
	int mDeclEnd;	
	int mDeclMCBlockId;	
	bool mDeclLifetimeExtend;
	bool mDbgLifeEnded;
	bool mIsValue; // Value vs Addr	

	Array<BeDbgVariableRange> mSavedRanges;
	Array<BeDbgVariableRange> mGaps;

public:
	BeDbgVariable()
	{
		mType = NULL;
		mValue = NULL;
		mParamNum = -1;
		mInitType = BfIRInitType_NotSet;
		mPendingInitType = BfIRInitType_NotNeeded;
		mPendingInitDef = false;
		mScope = NULL;
		mDeclDbgLoc = NULL;
		mDeclStart = -1;
		mDeclEnd = -1;
		mDeclMCBlockId = -1;
		mIsValue = false;
		mDbgLifeEnded = false;
		mDeclLifetimeExtend = false;
	}

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mName);
		mType->HashReference(hashCtx);
		if (mValue != NULL)
			mValue->HashReference(hashCtx);
		hashCtx.Mixin(mParamNum);
		hashCtx.Mixin(mInitType);
		hashCtx.Mixin(mPendingInitType);
		if (mScope != NULL)
			mScope->HashReference(hashCtx);
		if (mDeclDbgLoc != NULL)
			mDeclDbgLoc->HashReference(hashCtx);		

		// The others only get filled in after generation -- not part of hash
	}
};

class BeDbgFile;

class BeDbgCodeEmission
{
public:
	BeDbgLoc* mDbgLoc;
	int mPos;
};

class BeDbgFunction : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgFunction, BeMDNode);

public:
	int mIdx;

	BeMDNode* mScope;
	BeDbgFile* mFile;
	int mLine;
	String mName;
	String mLinkageName;
	BeDbgFunctionType* mType;
	//Array<BeDbgVariable*> mParams;
	Array<BeDbgType*> mGenericArgs;
	Array<BeConstant*> mGenericConstValueArgs;
	BeFunction* mValue;
	bool mIsLocalToUnit;
	bool mIsStaticMethod;
	bool mIncludedAsMember;
	int mFlags;
	int mVK;
	int mVIndex;	
		
	Array<BeDbgVariable*> mVariables;
	int mPrologSize;
	int mCodeLen;
	Array<BeDbgCodeEmission> mEmissions;

	int mCvTypeId;
	int mCvFuncId;
	int mCvArgListId;

public:
	BeDbgFunction()
	{
		mIdx = -1;
		mScope = NULL;
		mFile = NULL;
		mLine = -1;
		mType = NULL;
		mValue = NULL;
		mFlags = 0;
		mIsLocalToUnit = false;
		mVK = -1;
		mVIndex = -1;
		mIsStaticMethod = true;		
		mIncludedAsMember = false;
		mPrologSize = 0;		
		mCodeLen = -1;
		mCvTypeId = -1;
		mCvFuncId = -1;
		mCvArgListId = -1;
	}

	BeDbgType* GetParamType(int paramIdx)
	{
		/*if (!mParams.empty())
			return mParams[paramIdx]->mType;*/
		if (paramIdx < (int)mVariables.size())
		{
			auto param = mVariables[paramIdx];
			if (param->mParamNum == paramIdx)
				return param->mType;
		}
		return mType->mParams[paramIdx];
	}

	bool HasThis()
	{
		return !mIsStaticMethod;

		/*bool matchLLVM = false;

		// This matches the LLVM CV emitter
		if (matchLLVM)
		{
			return (BeValueDynCast<BeDbgType>(mScope) != NULL) &&
				(mType->mParams.size() > 0);
		}
		else
		{
			return ((mVariables.size() > 0) && (mVariables[0]->mName == "this"));
		}*/
	}
	
	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.Mixin(mLine);
		hashCtx.MixinStr(mName);
		hashCtx.MixinStr(mLinkageName);
		mType->HashReference(hashCtx);
		for (auto genericArg : mGenericArgs)
			genericArg->HashReference(hashCtx);
		for (auto genericConstValueArgs : mGenericArgs)
			genericConstValueArgs->HashReference(hashCtx);
		if (mValue != NULL)
			mValue->HashReference(hashCtx);
		hashCtx.Mixin(mIsLocalToUnit);
		hashCtx.Mixin(mIsStaticMethod);
		hashCtx.Mixin(mFlags);
		hashCtx.Mixin(mVK);
		hashCtx.Mixin(mVIndex);
		hashCtx.Mixin(mVariables.size());
		for (auto& variable : mVariables)
		{
			if (variable == NULL)
				hashCtx.Mixin(-1);
			else
				variable->HashReference(hashCtx);
		}
		hashCtx.Mixin(mPrologSize);
		hashCtx.Mixin(mCodeLen);
	}
};

class BeDbgInlinedScope : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgInlinedScope, BeMDNode);
	BeMDNode* mScope;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		mScope->HashReference(hashCtx);
	}
};

class BeDbgFile;

class BeDbgStructType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgStructType, BeDbgType);

public:
	BeMDNode* mScope;
	String mName;
	BeDbgType* mDerivedFrom;
	Array<BeDbgStructMember*> mMembers;
	Array<BeDbgFunction*> mMethods;
	bool mIsFullyDefined;
	bool mIsStatic;
	BeDbgFile* mDefFile;
	int mDefLine;

public:
	BeDbgStructType()
	{
		mScope = NULL;		
		mDerivedFrom = NULL;
		mIsStatic = false;
		mIsFullyDefined = false;
		mDefFile = NULL;
		mDefLine = 0;
	}

	void SetMembers(SizedArrayImpl<BeMDNode*>& members);

	virtual void HashContent(BeHashContext& hashCtx) override;
};

class BeDbgEnumMember : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgEnumMember, BeMDNode);

public:
	String mName;
	int64 mValue;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mName);
		hashCtx.Mixin(mValue);
	}
};

class BeDbgEnumType : public BeDbgType
{
public:
	BE_VALUE_TYPE(BeDbgEnumType, BeDbgType);

public:
	BeMDNode* mScope;
	String mName;
	BeDbgType* mElementType;
	bool mIsFullyDefined;
	Array<BeDbgEnumMember*> mMembers;

public:
	BeDbgEnumType()
	{
		mScope = NULL;
		mElementType = NULL;
		mIsFullyDefined = false;
	}

	void SetMembers(SizedArrayImpl<BeMDNode*>& members);

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		mScope->HashReference(hashCtx);
		hashCtx.MixinStr(mName);
		if (mElementType != NULL)
			mElementType->HashReference(hashCtx);
		hashCtx.Mixin(mIsFullyDefined);
		for (auto member : mMembers)
			member->HashReference(hashCtx);
	}
};

class BeDbgFile : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgFile, BeMDNode);

public:
	String mFileName;
	String mDirectory;
	int mIdx;	

	void ToString(String& str);	

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mFileName);
		hashCtx.MixinStr(mDirectory);
	}
};

class BeDbgGlobalVariable : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgGlobalVariable, BeMDNode);

	BeMDNode* mContext;
	String mName;
	String mLinkageName;
	BeDbgFile* mFile;
	int mLineNum;
	BeDbgType* mType;
	bool mIsLocalToUnit;
	BeConstant* mValue;
	BeMDNode* mDecl;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(TypeId);
		hashCtx.MixinStr(mName);
		hashCtx.MixinStr(mLinkageName);
		if (mFile != NULL)
			mFile->HashReference(hashCtx);
		hashCtx.Mixin(mLineNum);
		mType->HashReference(hashCtx);
		hashCtx.Mixin(mIsLocalToUnit);
		if (mValue != NULL)
			mValue->HashReference(hashCtx);
		if (mDecl != NULL)
			mDecl->HashReference(hashCtx);
	}
};

class BeDbgModule : public BeMDNode
{
public:
	BE_VALUE_TYPE(BeDbgModule, BeMDNode);

public:
	BeModule* mBeModule;
	String mFileName;
	String mDirectory;
	String mProducer;
		
	OwnedVector<BeDbgFile> mFiles;
	OwnedVector<BeDbgNamespace> mNamespaces;
	OwnedVector<BeDbgGlobalVariable> mGlobalVariables;

	OwnedVector<BeDbgType> mTypes;
	Array<BeDbgFunction*> mFuncs; // Does not include methods in structs

	virtual void HashContent(BeHashContext& hashCtx) override;	
	BeDbgReferenceType* CreateReferenceType(BeDbgType* dbgType);
};

//////////////////////////////////////////////////////////////////////////

class BeDbgModule;

class BeModule
{
public:
	BeIRCodeGen* mBeIRCodeGen;
	Array<BeConstant*> mConfigConsts32;
	Array<BeConstant*> mConfigConsts64;

	BeFunction* mActiveFunction;
	BumpAllocator mAlloc;
	OwnedVector<BeValue> mOwnedValues; // Those that need dtors
	OwnedVector<BeGlobalVariable> mGlobalVariables;
	BeContext* mContext;
	String mModuleName;
	String mTargetTriple;
	BeBlock* mActiveBlock;
	int mInsertPos;
	BeDbgLoc* mCurDbgLoc;
	BeDbgLoc* mPrevDbgLocInline;
	BeDbgLoc* mLastDbgLoc;	
	Array<BeArgument*> mArgs;	
	Array<BeFunction*> mFunctions;	
	Dictionary<String, BeFunction*> mFunctionMap;
	int mCurDbgLocIdx;
	int mCurLexBlockId;	

	BeDbgModule* mDbgModule;

public:	
	void AddInst(BeInst* inst);	
	static void ToString(StringImpl& str, BeType* type);
	static void StructToString(StringImpl& str, BeStructType* type);

	template <typename T>
	T* AllocInst()
	{
		T* newInst = mAlloc.Alloc<T>();
		AddInst(newInst);
		return newInst;
	}

	template <typename T>
	T* AllocInstOwned()
	{
		T* newInst = mOwnedValues.Alloc<T>();
		AddInst(newInst);
		return newInst;
	}

public:
	BeModule(const StringImpl& moduleName, BeContext* context);		
	~BeModule();

	void Hash(BeHashContext& hashCtx);

	String ToString(BeFunction* func = NULL);
	void Print();
	void Print(BeFunction* func);
	void PrintValue(BeValue* val);

	void DoInlining(BeFunction* func);
	void DoInlining();

	static BeCmpKind InvertCmp(BeCmpKind cmpKind);	
	static BeCmpKind SwapCmpSides(BeCmpKind cmpKind);	
	void SetActiveFunction(BeFunction* function);
	BeArgument* GetArgument(int arg);
	BeBlock* CreateBlock(const StringImpl& name);	
	void AddBlock(BeFunction* function, BeBlock* block);
	void RemoveBlock(BeFunction* function, BeBlock* block);
	BeBlock* GetInsertBlock();
	void SetInsertPoint(BeBlock* block);
	void SetInsertPointAtStart(BeBlock* block);
	BeFunction* CreateFunction(BeFunctionType* funcType, BfIRLinkageType linkageType, const StringImpl& name);
	BeDbgLoc* GetCurrentDebugLocation();
	void SetCurrentDebugLocation(BeDbgLoc* dbgLoc);
	void SetCurrentDebugLocation(int line, int column, BeMDNode* diScope, BeDbgLoc* diInlinedAt);

	///
	BeNopInst* CreateNop();
	BeUndefValueInst* CreateUndefValue(BeType* type);
	BeNumericCastInst* CreateNumericCast(BeValue* value, BeType* toType, bool valSigned, bool toSigned);	
	BeBitCastInst* CreateBitCast(BeValue* value, BeType* toType);;
	BeCmpInst* CreateCmp(BeCmpKind cmpKind, BeValue* lhs, BeValue* rhs);	
	BeBinaryOpInst* CreateBinaryOp(BeBinaryOpKind opKind, BeValue* lhs, BeValue* rhs);

	BeAllocaInst* CreateAlloca(BeType* type);
	BeLoadInst* CreateLoad(BeValue* value, bool isVolatile);
	BeLoadInst* CreateAlignedLoad(BeValue* value, int alignment, bool isVolatile);
	BeStoreInst* CreateStore(BeValue* val, BeValue* ptr, bool isVolatile);
	BeStoreInst* CreateAlignedStore(BeValue* val, BeValue* ptr, int alignment, bool isVolatile);
	BeGEPInst* CreateGEP(BeValue* ptr, BeValue* idx0, BeValue* idx1);

	BeBrInst* CreateBr(BeBlock* block);	
	BeCondBrInst* CreateCondBr(BeValue* cond, BeBlock* trueBlock, BeBlock* falseBlock);
	BeRetInst* CreateRetVoid();
	BeRetInst* CreateRet(BeValue* value);	
	BeCallInst* CreateCall(BeValue* func, const SizedArrayImpl<BeValue*>& args);

	BeConstant* GetConstant(BeType* type, double floatVal);
	BeConstant* GetConstant(BeType* type, int64 intVal);
	BeConstant* GetConstant(BeType* type, bool boolVal);
	BeConstant* GetConstantNull(BePointerType* type);			
};

NS_BF_END