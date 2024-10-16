#pragma once

#include "../Beef/BfCommon.h"
#include "../Compiler/BfUtil.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeModule.h"
#include "BeefySysLib/MemStream.h"
#include "../X64.h"

NS_BF_BEGIN

class BeMCAddInst
{
public:
};

struct BeVTrackingBits
{
	// Memory is allocated by BeVTrackingContext
	bool IsSet(int idx);
	void Set(int idx);
	void Clear(int idx);
};

struct BeVTrackingValue
{
public:
	BeVTrackingBits* mEntry;
	int mNumEntries;

	BeVTrackingValue(int maxSize)
	{
		mNumEntries = (maxSize + 31) / 32;
		mEntry = (BeVTrackingBits*)(new int32[mNumEntries]);
		memset(mEntry, 0, mNumEntries * 4);
	}

	~BeVTrackingValue()
	{
		delete[](int32*)mEntry;
	}

	void Clear()
	{
		memset(mEntry, 0, mNumEntries * 4);
	}
};

class BeVTrackingList
{
public:
	int mSize;
	int mNumChanges;
	int mEntries[1];

	struct Iterator
	{
	public:
		BeVTrackingList* mList;
		int mIdx;

	public:
		Iterator(BeVTrackingList* list)
		{
			mList = list;
			mIdx = 0;
		}

		Iterator& operator++()
		{
			mIdx++;
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return itr.mIdx != mIdx;
		}

		bool operator==(const Iterator& itr) const
		{
			return itr.mIdx == mIdx;
		}

		int operator*()
		{
			return mList->mEntries[mIdx];
		}
	};

	/*struct DiffIterator
	{
	public:
		BeVTrackingList* mList0;
		BeVTrackingList* mList1;
		int mIdx0;
		int mIdx1;

		DiffIterator(BeVTrackingList* list0, BeVTrackingList* list1)
		{
			mList0 = list0;
			mList1 = list1;
			mIdx0 = 0;
			mIdx1 = 0;
		}

		DiffIterator& operator++()
		{
		}
	};*/

	Iterator begin()
	{
		return Iterator(this);
	}

	Iterator end()
	{
		auto itr = Iterator(this);
		itr.mIdx = mSize;
		return itr;
	}

	int GetChange(int idx)
	{
		return mEntries[mSize + idx];
	}

	bool ContainsChange(int value)
	{
		for (int i = 0; i < mNumChanges; i++)
			if (mEntries[mSize + i] == value)
				return true;
		return false;
	}
};

class BeMCBlock;
class BeMCPhi;

struct BeMemSetInfo
{
public:
	int32 mSize;
	int8 mAlign;
	uint8 mValue;
};

struct BeMemCpyInfo
{
public:
	int32 mSize;
	int8 mAlign;
};

struct BeVRegPair
{
	// Negative means "addr of", positive means "value of"
	int32 mVRegIdx0;
	int32 mVRegIdx1;
};

struct BeCmpResult
{
	BeCmpKind mCmpKind;
	int mResultVRegIdx;
	bool mInverted;

	BeCmpResult()
	{
		mCmpKind = BeCmpKind_None;
		mResultVRegIdx = -1;
		mInverted = false;
	}
};

struct BeNotResult;

enum BeMCOperandKind
{
	BeMCOperandKind_None,
	BeMCOperandKind_NativeReg,
	BeMCOperandKind_VReg,
	BeMCOperandKind_VRegAddr,
	BeMCOperandKind_VRegLoad,
	BeMCOperandKind_Symbol,
	BeMCOperandKind_SymbolAddr,
	BeMCOperandKind_Immediate_i8,
	BeMCOperandKind_Immediate_i16,
	BeMCOperandKind_Immediate_i32,
	BeMCOperandKind_Immediate_i64,
	BeMCOperandKind_Immediate_HomeSize,
	BeMCOperandKind_Immediate_Null,
	BeMCOperandKind_Immediate_f32,
	BeMCOperandKind_Immediate_f64,
	BeMCOperandKind_Immediate_f32_Packed128,
	BeMCOperandKind_Immediate_f64_Packed128,
	BeMCOperandKind_Immediate_int32x4,
	BeMCOperandKind_ConstAgg,
	BeMCOperandKind_Block,
	BeMCOperandKind_Label,
	BeMCOperandKind_CmpKind,
	BeMCOperandKind_CmpResult,
	BeMCOperandKind_NotResult,
	BeMCOperandKind_MemSetInfo,
	BeMCOperandKind_MemCpyInfo,
	BeMCOperandKind_VRegPair,
	BeMCOperandKind_Phi,
	BeMCOperandKind_PreserveFlag
};

enum BeMCPreserveFlag
{
	BeMCPreserveFlag_None,
	BeMCPreserveFlag_NoRestore
};

class BeMCOperand
{
public:
	enum EncodeFlag
	{
		EncodeFlag_Addr = 0x80000000,
		EncodeFlag_Symbol = 0x40000000,
		EncodeFlag_IdxMask = 0x0FFFFFFF
	};

	BeMCOperandKind mKind;
	union
	{
		X64CPURegister mReg;
		int mVRegIdx;
		int mFrameObjNum;
		int64 mImmediate;
		BeType* mType;
		float mImmF32;
		double mImmF64;
		BeMCBlock* mBlock;
		BeMCPhi* mPhi;
		BeNotResult* mNotResult;
		BeConstant* mConstant;
		int mLabelIdx;
		int mSymbolIdx;
		BeCmpKind mCmpKind;
		int mCmpResultIdx;
		BeMemSetInfo mMemSetInfo;
		BeMemCpyInfo mMemCpyInfo;
		BeVRegPair mVRegPair;
		BeMCPreserveFlag mPreserveFlag;
	};

public:
	BeMCOperand()
	{
		mKind = BeMCOperandKind_None;
	}

	operator bool() const
	{
		return mKind != BeMCOperandKind_None;
	}

	bool IsVReg() const
	{
		return (mKind == BeMCOperandKind_VReg);
	}

	bool IsVRegAny() const
	{
		return (mKind == BeMCOperandKind_VReg) || (mKind == BeMCOperandKind_VRegAddr) || (mKind == BeMCOperandKind_VRegLoad);
	}

	bool MayBeMemory()
	{
		return (mKind >= BeMCOperandKind_VReg) && (mKind <= BeMCOperandKind_SymbolAddr);
	}

	bool IsImmediate() const
	{
		return ((mKind >= BeMCOperandKind_Immediate_i8) && (mKind <= BeMCOperandKind_Immediate_f64_Packed128));
	}

	bool IsImmediateInt() const
	{
		return ((mKind >= BeMCOperandKind_Immediate_i8) && (mKind <= BeMCOperandKind_Immediate_HomeSize));
	}

	bool IsImmediateFloat() const
	{
		return ((mKind >= BeMCOperandKind_Immediate_f32) && (mKind <= BeMCOperandKind_Immediate_int32x4));
	}

	bool IsNativeReg() const
	{
		return mKind == BeMCOperandKind_NativeReg;
	}

	bool IsSymbol() const
	{
		return (mKind == BeMCOperandKind_Symbol) || (mKind == BeMCOperandKind_SymbolAddr);
	}

	bool operator==(const BeMCOperand& other) const
	{
		if (mKind != other.mKind)
			return false;

		// Either intptr or int
		if (mKind == BeMCOperandKind_CmpKind)
			return mCmpKind == other.mCmpKind;
		if (mKind == BeMCOperandKind_Immediate_f32)
			return mImmF32 == other.mImmF32;
		if (mKind == BeMCOperandKind_Immediate_f64)
			return mImmF64 == other.mImmF64;
		if ((mKind >= BeMCOperandKind_Immediate_i8) && (mKind <= BeMCOperandKind_Block))
			return mImmediate == other.mImmediate;
		if (mKind == BeMCOperandKind_Immediate_Null)
			return mType == other.mType;
		return mVRegIdx == other.mVRegIdx;
	}

	bool operator!=(const BeMCOperand& other) const
	{
		return !(*this == other);
	}

	bool IsZero() const
	{
		if (mKind == BeMCOperandKind_None)
			return true;
		if ((mKind >= BeMCOperandKind_Immediate_i8) && (mKind <= BeMCOperandKind_Immediate_i64) && (mImmediate == 0))
			return true;
		if (mKind == BeMCOperandKind_Immediate_Null)
			return true;
		return false;
	}

	int64 GetImmediateInt() const
	{
		if ((mKind >= BeMCOperandKind_Immediate_i8) && (mKind <= BeMCOperandKind_Immediate_i64))
			return mImmediate;
		return 0;
	}

	double GetImmediateDouble() const
	{
		if (mKind == BeMCOperandKind_Immediate_f32)
			return mImmF32;
		if (mKind == BeMCOperandKind_Immediate_f64)
			return mImmF64;
		return 0;
	}

	static BeMCOperand FromEncoded(int vregIdx)
	{
		BeMCOperand operand;
		operand.mVRegIdx = vregIdx & EncodeFlag_IdxMask;
		if ((vregIdx & EncodeFlag_Addr) != 0)
		{
			if ((vregIdx & EncodeFlag_Symbol) != 0)
				operand.mKind = BeMCOperandKind_SymbolAddr;
			else
				operand.mKind = BeMCOperandKind_VRegAddr;
		}
		else
		{
			if ((vregIdx & EncodeFlag_Symbol) != 0)
				operand.mKind = BeMCOperandKind_Symbol;
			else
				operand.mKind = BeMCOperandKind_VReg;
		}

		return operand;
	}

	static BeMCOperand ToLoad(const BeMCOperand& operand)
	{
		BeMCOperand loadedOperand = operand;
		if (loadedOperand.mKind == BeMCOperandKind_VReg)
			loadedOperand.mKind = BeMCOperandKind_VRegLoad;
		else if (loadedOperand.mKind == BeMCOperandKind_VRegAddr)
			loadedOperand.mKind = BeMCOperandKind_VReg;
		else if (loadedOperand.mKind == BeMCOperandKind_SymbolAddr)
			loadedOperand.mKind = BeMCOperandKind_Symbol;
		else
			BF_FATAL("Bad");
		return loadedOperand;
	}

	static BeMCOperand FromVReg(int vregIdx)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_VReg;
		operand.mVRegIdx = vregIdx;
		return operand;
	}

	static BeMCOperand FromVRegAddr(int vregIdx)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_VRegAddr;
		operand.mVRegIdx = vregIdx;
		return operand;
	}

	static BeMCOperand FromLabel(int labelIdx)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_Label;
		operand.mLabelIdx = labelIdx;
		return operand;
	}

	static BeMCOperand FromCmpKind(BeCmpKind cmpKind)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_CmpKind;
		operand.mCmpKind = cmpKind;
		return operand;
	}

	static BeMCOperand FromReg(X64CPURegister reg)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_NativeReg;
		operand.mReg = reg;
		return operand;
	}

	static BeMCOperand FromImmediate(int64 immediate)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_Immediate_i64;
		operand.mImmediate = immediate;
		return operand;
	}

	static BeMCOperand FromBlock(BeMCBlock* block)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_Block;
		operand.mBlock = block;
		return operand;
	}

	static BeMCOperand FromSymbol(int symIdx)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_Symbol;
		operand.mSymbolIdx = symIdx;
		return operand;
	}

	static BeMCOperand FromSymbolAddr(int symIdx)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_SymbolAddr;
		operand.mSymbolIdx = symIdx;
		return operand;
	}

	static BeMCOperand FromPreserveFlag(BeMCPreserveFlag preserveFlag)
	{
		BeMCOperand operand;
		operand.mKind = BeMCOperandKind_PreserveFlag;
		operand.mPreserveFlag = preserveFlag;
		return operand;
	}
};

struct BeNotResult
{
	BeValue* mValue;
};

enum BeMCInstKind
{
	// Psuedo instructions, must be legalized
	BeMCInstKind_None,
	BeMCInstKind_Def,
	BeMCInstKind_DefLoad,
	BeMCInstKind_DefPhi,
	BeMCInstKind_DbgDecl,
	BeMCInstKind_LifetimeExtend,
	BeMCInstKind_LifetimeStart,
	BeMCInstKind_LifetimeEnd,
	BeMCInstKind_LifetimeSoftEnd,
	BeMCInstKind_ValueScopeSoftEnd,
	BeMCInstKind_ValueScopeHardEnd,
	BeMCInstKind_Label,
	BeMCInstKind_CmpToBool,
	BeMCInstKind_MemSet,
	BeMCInstKind_MemCpy,
	BeMCInstKind_FastCheckStack,
	BeMCInstKind_TLSSetup,
	BeMCInstKind_PreserveVolatiles,
	BeMCInstKind_RestoreVolatiles,
	BeMCInstKind_Unwind_PushReg,
	BeMCInstKind_Unwind_SaveXMM,
	BeMCInstKind_Unwind_Alloc,
	BeMCInstKind_Unwind_SetBP,
	BeMCInstKind_Rem,
	BeMCInstKind_IRem,

	// Legal instructions
	BeMCInstKind_Nop,
	BeMCInstKind_Unreachable,
	BeMCInstKind_EnsureInstructionAt,
	BeMCInstKind_DbgBreak,
	BeMCInstKind_MFence,
	BeMCInstKind_Mov,
	BeMCInstKind_MovRaw,
	BeMCInstKind_MovSX,
	BeMCInstKind_XChg,
	BeMCInstKind_XAdd,
	BeMCInstKind_CmpXChg,
	BeMCInstKind_Load,
	BeMCInstKind_Store,
	BeMCInstKind_Push,
	BeMCInstKind_Pop,
	BeMCInstKind_Neg,
	BeMCInstKind_Not,
	BeMCInstKind_Add,
	BeMCInstKind_Sub,
	BeMCInstKind_Mul,
	BeMCInstKind_IMul,
	BeMCInstKind_Div,
	BeMCInstKind_IDiv,
	BeMCInstKind_Cmp,
	BeMCInstKind_And,
	BeMCInstKind_Or,
	BeMCInstKind_Xor,
	BeMCInstKind_Shl,
	BeMCInstKind_Shr,
	BeMCInstKind_Sar,
	BeMCInstKind_Test,
	BeMCInstKind_CondBr,
	BeMCInstKind_Br,
	BeMCInstKind_Ret,
	BeMCInstKind_Call,

	BeMCInstKind_COUNT
};

struct BeVTrackingBits;

struct BeMCVRegArray
{
	int mSize;
	int mVRegIndices[1];
};

struct BeVRegLastUseRecord
{
	int mVRegIdx;
	BeVRegLastUseRecord* mNext;
};

class BeMCInst
{
public:
	BeMCInstKind mKind;
	bool mDisableShortForm;

	BeMCOperand mResult;
	BeMCOperand mArg0;
	BeMCOperand mArg1;

	BeVTrackingList* mLiveness;
	BeVTrackingList* mVRegsInitialized;
	BeVRegLastUseRecord* mVRegLastUseRecord;
	BeDbgLoc* mDbgLoc;

	bool IsDef()
	{
		return (mKind == BeMCInstKind_Def) || (mKind == BeMCInstKind_DefLoad);
	}

	bool IsAssignment()
	{
		return (IsMov()) || (mKind == BeMCInstKind_Load);
	}

	bool IsMov()
	{
		return (mKind >= BeMCInstKind_Mov) && (mKind <= BeMCInstKind_MovSX);
	}

	bool IsMul()
	{
		return (mKind == BeMCInstKind_Mul) || (mKind == BeMCInstKind_IMul);
	}

	bool IsCommutable()
	{
		return (mKind == BeMCInstKind_Add) || (mKind == BeMCInstKind_Mul) || (mKind == BeMCInstKind_IMul);
	}

	bool IsShift()
	{
		return (mKind >= BeMCInstKind_Shl) && (mKind <= BeMCInstKind_Sar);
	}

	bool IsInformational()
	{
		return (mKind <= BeMCInstKind_Label);
	}

	bool IsPsuedo()
	{
		return (mKind <= BeMCInstKind_Unwind_SetBP);
	}

	BeMCOperand* GetDest()
	{
		if (mResult)
			return &mResult;

		switch (mKind)
		{
		case BeMCInstKind_Rem:
		case BeMCInstKind_IRem:
		case BeMCInstKind_MemSet:
		case BeMCInstKind_MemCpy:
		case BeMCInstKind_Mov:
		case BeMCInstKind_MovSX:
		case BeMCInstKind_Store:
		case BeMCInstKind_Pop:
		case BeMCInstKind_Neg:
		case BeMCInstKind_Add:
		case BeMCInstKind_Sub:
		case BeMCInstKind_Mul:
		case BeMCInstKind_IMul:
		case BeMCInstKind_Div:
		case BeMCInstKind_IDiv:
		case BeMCInstKind_And:
		case BeMCInstKind_Or:
		case BeMCInstKind_Xor:
		case BeMCInstKind_Shl:
		case BeMCInstKind_Shr:
		case BeMCInstKind_Sar:
			return &mArg0;
		}

		return NULL;
	}
};

class BeMCBlock
{
public:
	String mName;
	Array<BeMCInst*> mInstructions;
	int mLabelIdx;
	Array<BeMCBlock*> mPreds;
	Array<BeMCBlock*> mSuccs;
	int mBlockIdx;
	int mMaxDeclBlockId; // If blocks merge, this is the highest index
	bool mIsLooped;
	bool mHasFakeBr;
	BeVTrackingList* mSuccLiveness;
	BeVTrackingList* mSuccVRegsInitialized;
	BeVTrackingList* mPredVRegsInitialized;

	// a lexical block can end in more than one MCBlock, in the case of branching
	Array<BeDbgLexicalBlock*> mDbgEndingLexicalBlock;

public:
	BeMCBlock()
	{
		mLabelIdx = -1;
		mIsLooped = false;
		mHasFakeBr = false;
		mBlockIdx = -1;
		mMaxDeclBlockId = -1;
		mSuccLiveness = NULL;
		mSuccVRegsInitialized = NULL;
		mPredVRegsInitialized = NULL;
	}

	void AddPred(BeMCBlock* pred);
	int FindLabelInstIdx(int labelIdx);
};

class BeMCContext;

class BeInstEnumerator
{
public:
	BeMCContext* mContext;
	BeMCBlock* mBlock;
	int mReadIdx;
	int mWriteIdx;
	bool mRemoveCurrent;

public:
	BeInstEnumerator(BeMCContext* context, BeMCBlock* block)
	{
		mContext = context;
		mBlock = block;
		mReadIdx = 0;
		mWriteIdx = 0;
		mRemoveCurrent = false;
	}

	~BeInstEnumerator()
	{
		if (mReadIdx >= mBlock->mInstructions.mSize)
		{
			mBlock->mInstructions.mSize = mWriteIdx;
		}
	}

	void Next();

	bool HasMore()
	{
		return mReadIdx < mBlock->mInstructions.mSize;
	}

	void RemoveCurrent()
	{
		mRemoveCurrent = true;
	}

	BeMCInst* Get()
	{
		return mBlock->mInstructions[mReadIdx];
	}
};

class BeMCPhiValue
{
public:
	BeMCBlock* mBlockFrom;
	BeMCOperand mValue;
	int mLandingLabel;
};

class BeMCPhi
{
public:
	BeMCBlock* mBlock;
	Array<BeMCPhiValue> mValues;
	int mIdx;

	BeMCOperand mBrTrue;
	BeMCOperand mBrFalse;
};

enum BeMCRMMode
{
	BeMCRMMode_Invalid,
	BeMCRMMode_Direct,
	BeMCRMMode_Deref
};

enum BeMCValueScopeRetainKind : uint8
{
	BeMCValueScopeRetainKind_None,
	BeMCValueScopeRetainKind_Soft,
	BeMCValueScopeRetainKind_Hard
};

class BeMCVRegInfo
{
public:
	X64CPURegister mReg;
	X64CPURegister mNaturalReg; // From param
	BeType* mType;
	int mAlign;
	int mFrameOffset; // 0 = 'RBP' (probably first local var or saved RBP), 8 means retAddr
	bool mRegNumPinned;
	bool mHasDynLife;
	bool mDoConservativeLife; // Keep alive through 'init' as well as 'uninit'
	bool mIsExpr; // Not an actual value, something like 'mRelTo + mRelOffset'
	bool mWantsExprActualize;
	bool mWantsExprOffsetActualize;
	bool mChainLifetimeEnd; // Kill relTo's when we are killed
	bool mForceMem;
	bool mForceReg;
	bool mSpilled;
	BeMCValueScopeRetainKind mValueScopeRetainedKind;
	bool mAwaitingDbgStart;
	bool mHasAmbiguousInitialization;
	bool mIsRetVal;
	bool mForceMerge; // Original param, not the 'debug holder'
	bool mDefOnFirstUse;
	bool mDisableR11; // Special case when this vreg is reserved for temporary (it's the last volatile int reg)
	bool mDisableR12; // Special case when this vreg is used in an ModRM scale, which isn't allowed
	bool mDisableR13; // Special case when this vreg is used in an ModRM scale, which isn't allowed
	bool mDisableRAX; // Special case when RAX must be preserved (ie: For IDIV)
	bool mDisableRDX; // Special case when RDX must be preserved (ie: For IDIV)
	bool mDisableEx; // Disable any registers that require a REX
	int mVRegAffinity; // Try to match the mReg of this vreg
	BeMCOperand mRelTo;
	int mRelOffsetScale;
	BeMCOperand mRelOffset;
	int mVolatileVRegSave;
	BeDbgVariable* mDbgVariable;

	bool mFoundLastUse;
	bool mMustExist; // Regs we must be able to debug
	// Must be refreshed with RefreshRefCounts
	int mRefCount;
	int mAssignCount;

public:
	BeMCVRegInfo()
	{
		mAlign = -1;
		mRegNumPinned = false;
		mReg = X64Reg_None;
		mNaturalReg = X64Reg_None;
		mType = NULL;
		mIsExpr = false;
		mWantsExprActualize = false;
		mWantsExprOffsetActualize = false;
		mChainLifetimeEnd = false;
		mFrameOffset = INT_MIN;
		mHasDynLife = false;
		mDoConservativeLife = false;
		mForceMem = false;
		mSpilled = false;
		mValueScopeRetainedKind = BeMCValueScopeRetainKind_None;
		mForceReg = false;
		mAwaitingDbgStart = false;
		mHasAmbiguousInitialization = false;
		mForceMerge = false;
		mDefOnFirstUse = false;
		mDisableR11 = false;
		mDisableR12 = false;
		mDisableR13 = false;
		mDisableRAX = false;
		mDisableRDX = false;
		mDisableEx = false;
		mVRegAffinity = -1;
		mRelOffsetScale = 1;
		mDbgVariable = NULL;
		mRefCount = -1;
		mAssignCount = -1;
		mVolatileVRegSave = -1;
		mMustExist = false;
		mIsRetVal = false;
		mFoundLastUse = false;
	}

	bool CanEliminate()
	{
		return (!mIsExpr) && (!mMustExist);
	}

	bool CanUserModify()
	{
		if (mDbgVariable == NULL)
			return false;
		// In Beef, the debugger cannot modify param values (just as the language user can't)
		return mDbgVariable->mParamNum == -1;
	}

	bool IsDirectRelTo()
	{
		return ((mRelTo.mKind == BeMCOperandKind_VReg) || (mRelTo.mKind == BeMCOperandKind_NativeReg)) &&
			(mRelOffset.IsZero()) && (mRelOffsetScale == 1);
	}

	bool IsDirectRelToAny()
	{
		return (mRelTo) && (mRelOffset.IsZero()) && (mRelOffsetScale == 1);
	}

	void SetRetVal()
	{
		//BF_ASSERT(!mForceMem);
		mForceReg = true;
		mIsRetVal = true;
	}
};

enum BeMCInstForm
{
	BeMCInstForm_Unknown,
	BeMCInstForm_R8_RM8,
	BeMCInstForm_R16_RM16,
	BeMCInstForm_R32_RM32,
	BeMCInstForm_R64_RM64,
	BeMCInstForm_R8_RM64_ADDR,
	BeMCInstForm_R16_RM64_ADDR,
	BeMCInstForm_R32_RM64_ADDR,
	BeMCInstForm_R64_RM64_ADDR,
	BeMCInstForm_RM8_R8,
	BeMCInstForm_RM16_R16,
	BeMCInstForm_RM32_R32,
	BeMCInstForm_RM64_R64,
	BeMCInstForm_RM64_R8_ADDR,
	BeMCInstForm_RM64_R16_ADDR,
	BeMCInstForm_RM64_R32_ADDR,
	BeMCInstForm_RM64_R64_ADDR,
	BeMCInstForm_RM8_IMM8,
	BeMCInstForm_RM16_IMM8,
	BeMCInstForm_RM32_IMM8,
	BeMCInstForm_RM64_IMM8,
	BeMCInstForm_RM16_IMM16,
	BeMCInstForm_RM32_IMM16,
	BeMCInstForm_RM64_IMM16,
	BeMCInstForm_RM32_IMM32,
	BeMCInstForm_RM64_IMM32,
	BeMCInstForm_RM64_IMM64,
	BeMCInstForm_RM8,
	BeMCInstForm_RM16,
	BeMCInstForm_RM32,
	BeMCInstForm_RM64,
	BeMCInstForm_IMM32,
	BeMCInstForm_R8,
	BeMCInstForm_R16,
	BeMCInstForm_R32,
	BeMCInstForm_R64,

	BeMCInstForm_XMM32_IMM,
	BeMCInstForm_XMM64_IMM,
	BeMCInstForm_XMM32_FRM32,
	BeMCInstForm_XMM64_FRM32,
	BeMCInstForm_XMM32_FRM64,
	BeMCInstForm_XMM64_FRM64,
	BeMCInstForm_FRM32_XMM32,
	BeMCInstForm_FRM64_XMM32,
	BeMCInstForm_FRM32_XMM64,
	BeMCInstForm_FRM64_XMM64,
	BeMCInstForm_FRM128_XMM128,
	BeMCInstForm_XMM32_RM32,
	BeMCInstForm_XMM64_RM32,
	BeMCInstForm_XMM32_RM64,
	BeMCInstForm_XMM64_RM64,
	BeMCInstForm_XMM128_RM128,
	BeMCInstForm_R32_F32,
	BeMCInstForm_R64_F32,
	BeMCInstForm_R32_F64,
	BeMCInstForm_R64_F64,

	BeMCInstForm_Symbol,
	BeMCInstForm_SymbolAddr
};

class BeCOFFObject;
class BeMCContext;

enum BeTrackKind
{
	BetTrackKind_Default = 0,

	// Liveness
	BeTrackKind_Exists = 0,

	// Initialized
	BeTrackKind_Initialized = 0,
	BeTrackKind_Uninitialized = 1, // Tracks from DbgDecl through to initialization

	BeTrackKind_COUNT = 2
};

// BeVTrackingEntry is immutable -- the Set/Clear/Merge methods allocate new entries if
//  the requested change produces a new liveness set
class BeVTrackingContext
{
public:
	struct Stats
	{
		int mBitsBytes;
		int mListBytes;
		int mSuccBytes;

		Stats()
		{
			mBitsBytes = 0;
			mListBytes = 0;
			mSuccBytes = 0;
		}
	};

public:
	Stats mStats;
	BumpAllocator mAlloc;
	BeMCContext* mMCContext;
	int mNumEntries;
	int mNumItems;
	int mNumBlocks; // # of entries in mBits
	int mTrackKindCount;

public:
	BeVTrackingContext(BeMCContext* mcContext);

	void Init(int numItems);
	void Clear();
	int GetBitsBytes();
	int GetIdx(int baseIdx, BeTrackKind trackKind);
	void Print(BeVTrackingList* list);

	BeVTrackingList* AllocEmptyList();
	BeVTrackingList* AddFiltered(BeVTrackingList* list, SizedArrayImpl<int>& indices, bool perserveChangeList);
	BeVTrackingList* AddFiltered(BeVTrackingList* list, int idx, bool perserveChangeList);
	BeVTrackingList* Add(BeVTrackingList* list, const SizedArrayImpl<int>& indices, bool perserveChangeList);
	BeVTrackingList* Add(BeVTrackingList* list, int idx, bool perserveChangeList);
	BeVTrackingList* ClearFiltered(BeVTrackingList* list, const SizedArrayImpl<int>& indices);
	BeVTrackingList* Modify(BeVTrackingList* list, const SizedArrayImpl<int>& adds, const SizedArrayImpl<int>& removes, SizedArrayImpl<int>& filteredAdds, SizedArrayImpl<int>& filteredRemoves, bool preserveChanges = false);
	int FindIndex(BeVTrackingList* entry, int val);
	bool IsSet(BeVTrackingList* entry, int idx);
	bool IsSet(BeVTrackingList* entry, int idx, BeTrackKind trackKind);
	BeVTrackingList* Clear(BeVTrackingList* list, const SizedArrayImpl<int>& indices);
	bool IsEmpty(BeVTrackingList* list);
	BeVTrackingList* Merge(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom);
	BeVTrackingList* MergeChanges(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom);
	BeVTrackingList* SetChanges(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom);
	BeVTrackingList* RemoveChange(BeVTrackingList* prevDestEntry, int idx);
};

class BeMCLoopDetector
{
public:
	class Node
	{
	public:
		BeVTrackingList* mPredBlocksSeen;

	public:
		Node()
		{
			mPredBlocksSeen = NULL;
		}
	};

	BeVTrackingContext mTrackingContext;
	BeMCContext* mMCContext;
	Array<Node> mNodes;

	BfBitSet mBitSet;

public:
	BeMCLoopDetector(BeMCContext* context);
	void DetectLoops(BeMCBlock* mcBlock, BeVTrackingList* blocksSeen);

	void DetectLoops(BeMCBlock* mcBlock);
	void DetectLoops();
};

class BeMCColorizer
{
public:
	struct Node
	{
		HashSet<int> mEdges;
		int mGraphEdgeCount;
		bool mInGraph;
		bool mSpilled;
		bool mWantsReg;
		int mRegCost[X64Reg_COUNT];
		int mLowestRegCost;
		int mMemCost;
		//int mActualVRegIdx;

		Node()
		{
			Prepare();
		}

		void AdjustRegCost(X64CPURegister reg, int adjust)
		{
			BF_ASSERT((int)reg >= 0);
			BF_ASSERT((int)reg < X64Reg_COUNT);
			int newCost = mRegCost[(int)reg] + adjust;
			mRegCost[(int)reg] = newCost;
			if (newCost < mLowestRegCost)
				mLowestRegCost = newCost;
		}

		void Prepare()
		{
			mWantsReg = false;
			mInGraph = false;
			mSpilled = false;
			mGraphEdgeCount = 0;
			mLowestRegCost = 0;
			mMemCost = 0;
			memset(mRegCost, 0, sizeof(mRegCost));
			//mActualVRegIdx = -1;
		}
	};

	struct BlockInfo
	{
		HashSet<X64CPURegister> mSuccLiveRegs;
		bool mEntryCount;

		BlockInfo()
		{
			mEntryCount = 0;
		}
	};

	enum RegKind
	{
		RegKind_Ints,
		RegKind_Floats,
	};

	Array<BlockInfo> mBlockInfo;
	Array<Node> mNodes;
	BeMCContext* mContext;
	bool mReserveParamRegs;

public:
	BeMCColorizer(BeMCContext* mcContext);

	void Prepare();
	void AddEdge(int vreg0, int vreg1);
	void PropogateMemCost(const BeMCOperand& operand, int memCost);
	void GenerateRegCosts();
	void AssignRegs(RegKind regKind); // Returns false if we had spills - we need to rerun
	bool Validate();
};

struct BeMCDefState
{
public:
	BeMCInst* mInst;
	BeMCBlock* mBlock;

public:
	BeMCDefState()
	{
		mInst = NULL;
		mBlock = NULL;
	}
};

// This chains remaps for InstCombine
class BeMCRemapper
{
public:
	struct Node
	{
		int mTo;
		int mFrom;

		Node()
		{
			mTo = -1;
			mFrom = -1;
		}
	};

	Array<Node> mNodes;

public:

	void Init(int size)
	{
		mNodes.Resize(size);
	}

	void Add(int from, int toOrig)
	{
		auto fromNode = &mNodes[from];
		if (fromNode->mTo != -1)
			return; // Silently fail if we've already remapped

		// Chain to end
		int to = toOrig;
		while (true)
		{
			auto toNode = &mNodes[to];
			if (toNode->mFrom != -1)
			{
				to = toNode->mFrom;
				continue;
			}

			toNode->mFrom = from;
			fromNode->mTo = to;
			return;
		}
	}

	int GetHead(int idx)
	{
		while (true)
		{
			auto node = &mNodes[idx];
			if (node->mTo == -1)
				return idx;
			idx = node->mTo;
		}
	}

	int GetNext(int idx)
	{
		return mNodes[idx].mFrom;
	}
};

struct BeMCSwitchEntry
{
	BeMCBlock* mBlock;
	int mOfs;
};

enum BeMCNativeTypeCode
{
	BeMCNativeTypeCode_Int8,
	BeMCNativeTypeCode_Int16,
	BeMCNativeTypeCode_Int32,
	BeMCNativeTypeCode_Int64,
	BeMCNativeTypeCode_Float,
	BeMCNativeTypeCode_Double,
	BeMCNativeTypeCode_M128,
	BeMCNativeTypeCode_M256,
	BeMCNativeTypeCode_M512,

	BeMCNativeTypeCode_COUNT
};

enum BeMCBinIdentityKind
{
	BeMCBinIdentityKind_None,
	BeMCBinIdentityKind_Right_IsOne,
	BeMCBinIdentityKind_Right_IsZero,
	BeMCBinIdentityKind_Right_IsOne_Result_Zero,
	BeMCBinIdentityKind_Any_IsOne,
	BeMCBinIdentityKind_Any_IsZero
};

struct BeMCLivenessStats
{
	int mCalls;
	int mHandledCalls;
	int mInstructions;
};

class BeVTrackingGenContext
{
public:
	struct Entry
	{
		bool mGenerateQueued;
		int mGenerateCount;

		Entry()
		{
			mGenerateQueued = false;
			mGenerateCount = 0;
		}
	};

	int mCalls;
	int mHandledCalls;
	int mInstructions;
	Array<Entry> mBlocks;
	BeVTrackingList* mEmptyList;
	BumpAllocator mAlloc;

public:
	BeVTrackingGenContext()
	{
		mCalls = 0;
		mHandledCalls = 0;
		mInstructions = 0;
		mEmptyList = NULL;
	}
};

struct BeRMParamsInfo
{
	X64CPURegister mRegA;
	X64CPURegister mRegB;
	int mBScale;
	int mDisp;
	BeMCRMMode mMode;

	int mErrorVReg;
	int mVRegWithScaledOffset;

	BeRMParamsInfo()
	{
		mRegA = X64Reg_None;
		mRegB = X64Reg_None;
		mBScale = 1;
		mDisp = 0;
		mMode = BeMCRMMode_Invalid;

		mErrorVReg = -1;
		mVRegWithScaledOffset = -1;
	}
};

enum BeMCOverflowCheckKind
{
	BeMCOverflowCheckKind_None,
	BeMCOverflowCheckKind_B,
	BeMCOverflowCheckKind_O
};

// This class only processes one function per instantiation
class BeMCContext
{
public:
	BeCOFFObject* mCOFFObject;
	DynMemStream& mOut;
	bool mDebugging;
	bool mFailed;
	int mDetectLoopIdx;

	BeVTrackingContext mLivenessContext;
	BeVTrackingContext mVRegInitializedContext;
	OwnedVector<BeMCBlock> mMCBlockAlloc;
	OwnedVector<BeMCPhi> mPhiAlloc;
	BeMCColorizer mColorizer;

	BumpAllocator mAlloc;
	Array<BeMCBlock*> mBlocks;
	Dictionary<BeValue*, BeMCOperand> mValueToOperand;
	BeType* mNativeIntType;
	BeModule* mModule;
	BeMCBlock* mActiveBlock;
	BeMCInst* mActiveInst;
	int* mInsertInstIdxRef;
	BeBlock* mActiveBeBlock;
	BeFunction* mBeFunction;
	BeDbgFunction* mDbgFunction;
	Array<BeMCVRegInfo*> mVRegInfo;
	Array<BeCmpResult> mCmpResults;
	int mCompositeRetVRegIdx;
	int mTLSVRegIdx;
	int mLegalizationIterations;

	Array<int> mCallArgVRegs[BeMCNativeTypeCode_COUNT];

	int mStackSize;
	bool mUseBP;
	bool mHasVAStart;
	int mCurLabelIdx;
	int mCurPhiIdx;
	int mMaxCallParamCount; // -1 if we have no calls
	Array<X64CPURegister> mParamsUsedRegs;
	HashSet<X64CPURegister> mUsedRegs;
	BeDbgLoc* mCurDbgLoc;
	BeVTrackingList* mCurVRegsInit;
	BeVTrackingList* mCurVRegsLive;
	Array<int> mTextRelocs;
	Array<BeMCSwitchEntry> mSwitchEntries;

	Dictionary<int, X64CPURegister> mDbgPreferredRegs;

public:
	void NotImpl();
	void Fail(const StringImpl& str);
	void AssertFail(const StringImpl& str, int line);
	void SoftFail(const StringImpl& str, BeDbgLoc* dbgLoc = NULL);
	void ToString(BeMCInst* inst, String& str, bool showVRegFlags, bool showVRegDetails);
	String ToString(const BeMCOperand& operand);
	String ToString(bool showVRegFlags = true, bool showVRegDetails = false);
	void Print(bool showVRegFlags, bool showVRegDetails);
	void Print();
	BeMCOperand GetOperand(BeValue* value, bool allowMetaResult = false, bool allowFail = false, bool skipForceVRegAddr = false); // Meta results are PHIs or CmpResults
	BeMCOperand CreateNot(const BeMCOperand& operand);
	BeMCOperand TryToVector(BeValue* value);
	BeType* GetType(const BeMCOperand& operand);
	bool AreTypesEquivalent(BeType* type0, BeType* type1);
	void AddRelRefs(BeMCOperand& operand, int refCount);
	int FindSafeInstInsertPos(int instIdx, bool forceSafeCheck = false);
	BeMCInst* AllocInst(int insertIdx = -1);
	BeMCInst* AllocInst(BeMCInstKind instKind, int insertIdx = -1);
	BeMCInst* AllocInst(BeMCInstKind instKind, const BeMCOperand& arg0, int insertIdx = -1);
	BeMCInst* AllocInst(BeMCInstKind instKind, const BeMCOperand& arg0, const BeMCOperand& arg1, int insertIdx = -1);
	void MergeInstFlags(BeMCInst* prevInst, BeMCInst* inst, BeMCInst* nextInst);
	void RemoveInst(BeMCBlock* block, int instIdx, bool needChangesMerged = true, bool removeFromList = true);
	BeMCOperand AllocBinaryOp(BeMCInstKind instKind, const BeMCOperand & lhs, const BeMCOperand & rhs, BeMCBinIdentityKind identityKind, BeMCOverflowCheckKind overflowCheckKind = BeMCOverflowCheckKind_None);
	BeMCOperand GetCallArgVReg(int argIdx, BeTypeCode typeCode);
	BeMCOperand CreateCall(const BeMCOperand& func, const SizedArrayImpl<BeMCOperand>& args, BeType* retType = NULL, BfIRCallingConv callingConv = BfIRCallingConv_CDecl, bool structRet = false, bool noReturn = false, bool isVarArg = false);
	BeMCOperand CreateCall(const BeMCOperand& func, const SizedArrayImpl<BeValue*>& args, BeType* retType = NULL, BfIRCallingConv callingConv = BfIRCallingConv_CDecl, bool structRet = false, bool noReturn = false, bool isVarArg = false);
	BeMCOperand CreateLoad(const BeMCOperand& mcTarget);
	void CreateStore(BeMCInstKind instKind, const BeMCOperand& val, const BeMCOperand& ptr);
	void CreateMemSet(const BeMCOperand& addr, uint8 val, int size, int align);
	void CreateMemCpy(const BeMCOperand& dest, const BeMCOperand& src, int size, int align);
	void CreateTableSwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx);
	void CreateBinarySwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx);
	void CreateCondBr(BeMCBlock* mcBlock, BeMCOperand& testVal, const BeMCOperand& trueBlock, const BeMCOperand& falseBlock);
	void CreatePhiAssign(BeMCBlock* mcBlock, const BeMCOperand& testVal, const BeMCOperand& result, const BeMCOperand& doneLabel);
	BeMCOperand GetImmediate(int64 val);
	BeMCOperand OperandToAddr(const BeMCOperand& operand);
	BeMCOperand GetVReg(int regNum);
	BeMCOperand AllocVirtualReg(BeType* type, int refCount = -1, bool mustBeReg = false);
	int GetUnderlyingVReg(int vregIdx);
	bool HasForceRegs(const BeMCOperand& operand);
	bool OperandsEqual(const BeMCOperand& op0, const BeMCOperand& op1, bool exact = false);
	bool ContainsNonOffsetRef(const BeMCOperand& checkOperand, const BeMCOperand& findOperand);
	BeMCInst* CreateDefineVReg(const BeMCOperand& vreg, int insertIdx = -1);
	int CreateLabel(int insertIdx = -1, int labelIdx = -1);
	bool FindTarget(const BeMCOperand& loc, BeMCBlock*& outBlock, int& outInstIdx);
	BeMCOperand AllocRelativeVirtualReg(BeType* type, const BeMCOperand& relTo, const BeMCOperand& relOffset, int relScale);
	BeMCVRegInfo* GetVRegInfo(const BeMCOperand& operand);
	bool HasSymbolAddr(const BeMCOperand& operand);
	BeMCOperand ReplaceWithNewVReg(BeMCOperand& operand, int& instIdx, bool isInput, bool mustBeReg = true, bool preserveDeref = false);
	BeMCOperand RemapOperand(BeMCOperand& operand, BeMCRemapper& regRemaps);
	bool IsLive(BeVTrackingList* liveRegs, int vregIdx, BeMCRemapper& regRemaps);
	void AddRegRemap(int from, int to, BeMCRemapper& regRemaps, bool allowRemapToDbgVar = false);
	bool GetEncodedOperand(const BeMCOperand& operand, int& vregAddrIdx);
	bool HasPointerDeref(const BeMCOperand& operand);
	bool AreOperandsEquivalent(const BeMCOperand& op1, const BeMCOperand& op2, BeMCRemapper& regRemaps);
	bool CouldBeReg(const BeMCOperand& operand);
	bool CheckForce(BeMCVRegInfo* vregInfo);

	void MarkLive(BeVTrackingList* liveRegs, SizedArrayImpl<int>& newRegs, BeVTrackingList*& initRegs, const BeMCOperand& operand);
	BeVTrackingList* MergeLiveRegs(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom);
	void GenerateLiveness(BeMCBlock* block, BeVTrackingGenContext* genCtx, bool& modifiedBlockBefore, bool& modifiedBlockAfter);
	void GenerateLiveness();
	void IntroduceVRegs(const BeMCOperand& newVReg, BeMCBlock* block, int startInstIdx, int lastInstIdx);
	void VRegSetInitialized(BeMCBlock* mcBlock, BeMCInst* inst, const BeMCOperand& operand, SizedArrayImpl<int>& addVec, SizedArrayImpl<int>& removeVec, bool deepSet, bool doSet);
	bool CheckVRegEqualityRange(BeMCBlock * mcBlock, int instIdx, const BeMCOperand & vreg0, const BeMCOperand & vreg1, BeMCRemapper& regRemaps, bool onlyCheckFirstLifetime = false);
	BeMCInst* FindSafePreBranchInst(BeMCBlock* mcBlock);
	void SimpleInitializedPass();
	void DoLastUsePassHelper(BeMCInst* inst, const BeMCOperand& operand);
	void InitializedPassHelper(BeMCBlock* block, BeVTrackingGenContext* genCtx, bool& modifiedBefore, bool& modifiedAfter);
	void GenerateVRegInitFlags(BeVTrackingGenContext& genCtx);
	void DoInitInjectionPass();
	void ReplaceVRegsInit(BeMCBlock* mcBlock, int instIdx, BeVTrackingList* prevList, BeVTrackingList* newList);
	void FixVRegInitFlags(BeMCInst* inst, const BeMCOperand& operand);
	void SetVTrackingValue(BeMCOperand& operand, BeVTrackingValue& vTrackingValue);

	bool IsVolatileReg(X64CPURegister reg);
	bool IsXMMReg(X64CPURegister reg);
	X64CPURegister ResizeRegister(X64CPURegister reg, int numBits);
	X64CPURegister ResizeRegister(X64CPURegister reg, BeType* type);
	X64CPURegister GetFullRegister(X64CPURegister reg);
	bool IsAddress(BeMCOperand& operand);
	bool IsAddressable(BeMCOperand& operand);
	bool IsVRegExpr(BeMCOperand& operand);
	void FixOperand(BeMCOperand& operand, int depth = 0);
	BeMCOperand GetFixedOperand(const BeMCOperand& operand);
	uint8 GetREX(const BeMCOperand& op0, const BeMCOperand& op1, bool is64Bit);
	void EmitREX(const BeMCOperand& op0, const BeMCOperand& op1, bool is64Bit);

	uint8 EncodeRegNum(X64CPURegister regNum);
	int GetRegSize(int regNum);
	void ValidateRMResult(const BeMCOperand& operand, BeRMParamsInfo& rmInfo, bool doValidate = true);
	void GetRMParams(const BeMCOperand& operand, BeRMParamsInfo& rmInfo, bool doValidate = true);
	void DisableRegister(const BeMCOperand& operand, X64CPURegister reg);
	void MarkInvalidRMRegs(const BeMCOperand& operand);
	void GetUsedRegs(const BeMCOperand& operand, X64CPURegister& regA, X64CPURegister& regB); // Expands regs
	BeMCRMMode GetRMForm(const BeMCOperand& operand, bool& isMulti);
	void GetValAddr(const BeMCOperand& operand, X64CPURegister& reg, int& offset);
	int GetHighestVRegRef(const BeMCOperand& operand);
	BeMCInstForm GetInstForm(BeMCInst* inst);
	BeMCInstForm ToIMM16(BeMCInstForm instForm);
	BeMCInstForm ToIMM32OrSmaller(BeMCInstForm instForm);
	void SetCurrentInst(BeMCInst* mcInst);
	int FindPreserveVolatiles(BeMCBlock* mcBlock, int instIdx);
	int FindRestoreVolatiles(BeMCBlock* mcBlock, int instIdx);

	uint8 GetJumpOpCode(BeCmpKind cmpKind, bool isLong);
	BeMCOperand IntXMMGetPacked(BeMCOperand arg, BeVectorType* vecType);
	void Emit(uint8 val);
	void EmitModRM(int mod, int reg, int rm);
	void EmitModRMRelStack(int rx, int regOffset, int scale);
	void EmitModRMRel(int rx, X64CPURegister regA, X64CPURegister regB, int bScale, int relOffset);
	void EmitModRM(int rx, BeMCOperand rm, int relocOfs = 0);
	void EmitModRM(BeMCOperand r, BeMCOperand rm, int relocOfs = 0);
	void EmitModRM_Addr(BeMCOperand r, BeMCOperand rm);
	void EmitModRM_XMM_IMM(int rx, BeMCOperand& imm);
	void EmitInstPrefix(BeMCInstForm instForm, BeMCInst* inst);
	void EmitInst(BeMCInstForm instForm, uint16 codeBytes, BeMCInst* inst);
	void EmitInst(BeMCInstForm instForm, uint16 codeBytes, uint8 rx, BeMCInst* inst);
	void EmitStdInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode_rm_r, uint8 opcode_r_rm, uint8 opcode_rm_imm, uint8 opcode_rm_imm_rx);
	void EmitStdInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode_rm_r, uint8 opcode_r_rm, uint8 opcode_rm_imm, uint8 opcode_rm_imm_rx, uint8 opcode_rm_imm8, uint8 opcode_rm_imm8_rx);
	bool EmitStdXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode);
	bool EmitStdXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode, uint8 opcode_dest_frm);
	bool EmitPackedXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode);
	bool EmitIntXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode);
	bool EmitIntBitwiseXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode);
	void EmitAggMov(const BeMCOperand& dest, const BeMCOperand& src);

	void DoTLSSetup();
	void DoChainedBlockMerge();
	void DoSplitLargeBlocks();
	void DetectLoops();
	void DoLastUsePass();
	bool DoInitializedPass();
	void RefreshRefCounts();
	void DoInstCombinePass();
	void DoRegAssignPass();
	void DoFrameObjPass();
	void DoActualization();
	void DoLoads();
	bool DoLegalization();
	void DoSanityChecking();
	void DoBlockCombine();
	bool DoJumpRemovePass();
	void DoRegFinalization();
	void DoCodeEmission();

	void HandleParams();

public:
	BeMCContext(BeCOFFObject* coffObject);
	void Generate(BeFunction* function);
};

NS_BF_END
