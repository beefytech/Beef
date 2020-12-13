#pragma once

#include "BfSystem.h"
#include "BfModule.h"

NS_BF_BEGIN

class BfMethodInstance;
class BeDbgLoc;
class BeType;
class BeValue;
class BeInst;
class BeDbgFile;
class BePhiInst;
class BeFunction;
class BeSwitchInst;
class CeMachine;
class CeFunction;

#define CEOP_SIZED(OPNAME) \
	CeOp_##OPNAME##_8, \
	CeOp_##OPNAME##_16, \
	CeOp_##OPNAME##_32, \
	CeOp_##OPNAME##_64, \
	CeOp_##OPNAME##_X

#define CEOP_SIZED_NUMERIC(OPNAME) \
	CeOp_##OPNAME##_I8, \
	CeOp_##OPNAME##_I16, \
	CeOp_##OPNAME##_I32, \
	CeOp_##OPNAME##_I64

#define CEOP_SIZED_NUMERIC_PLUSF(OPNAME) \
	CeOp_##OPNAME##_I8, \
	CeOp_##OPNAME##_I16, \
	CeOp_##OPNAME##_I32, \
	CeOp_##OPNAME##_I64, \
	CeOp_##OPNAME##_F32, \
	CeOp_##OPNAME##_F64

enum CeOp : int16
{
	CeOp_InvalidOp,
	CeOp_Ret,
	CeOp_Jmp,
	CeOp_JmpIf,
	CeOp_JmpIfNot,

	CeOp_FrameAddr32,
	CeOp_FrameAddr64,

	CEOP_SIZED(Const),
	CEOP_SIZED(Load),
	CEOP_SIZED(Store),	
	CEOP_SIZED(Move),
	CEOP_SIZED(Push),
	
	CeOp_AdjustSP,
	CeOp_Call,

	CeOp_Conv_I32_I64,

	CEOP_SIZED_NUMERIC_PLUSF(Add),	
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_EQ),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_SLT),

	CEOP_SIZED_NUMERIC_PLUSF(Neg),

	CeOp_COUNT
};

struct CeEmitEntry
{
	int mCodePos;
	int mFile;
	int mLine;
	int mColumn;
};

class CeCallEntry
{
public:
	String mFunctionName;
	int mBindRevision;
	CeFunction* mFunction;

public:
	CeCallEntry()
	{
		mBindRevision = -1;
		mFunction = NULL;
	}
};

class CeFunction
{
public:
	BfMethodInstance* mMethodInstance;
	String mName;
	bool mInitialized;
	bool mFailed;		
	Array<uint8> mCode;	
	Array<String> mFiles;
	Array<CeEmitEntry> mEmitTable;
	Array<CeCallEntry> mCallTable;
	String mGenError;
	int mFrameSize;

public:
	CeFunction()
	{
		mInitialized = false;
		mMethodInstance = NULL;
		mFailed = false;
		mFrameSize = 0;
	}	
};

enum CeEvalFlags
{
	CeEvalFlags_None = 0	
};

enum CeOperandKind
{
	CeOperandKind_None,
	CeOperandKind_FrameOfs,
	CeOperandKind_AllocaAddr,
	CeOperandKind_Block,
};

class CeOperand
{
public:
	CeOperandKind mKind;
	union
	{
		int mFrameOfs;
		int mBlockIdx;
	};
	BeType* mType;

public:
	CeOperand()
	{
		mKind = CeOperandKind_None;
		mFrameOfs = 0;
		mType = NULL;
	}

	operator bool() const
	{
		return mKind != CeOperandKind_None;
	}
};

#define BF_CE_STACK_SIZE 1024*1024

enum CeOperandInfoKind
{
	CEOI_None,
	CEOI_FrameRef,
	CEOI_IMM8,
	CEOI_IMM16,
	CEOI_IMM32,
	CEOI_IMM64,
	CEOI_IMM_VAR,
	CEOI_JMPREL
};

enum CeSizeClass
{
	CeSizeClass_8,
	CeSizeClass_16,
	CeSizeClass_32,
	CeSizeClass_64,
	CeSizeClass_X,
};

class CeDumpContext
{


public:	
	CeFunction* mCeFunction;
	String mStr;
	uint8* mStart;
	uint8* mPtr;
	uint8* mEnd;

public:
	void DumpOperandInfo(CeOperandInfoKind operandInfoKind);

	void Dump();	
};

struct CePhiOutgoing
{
	BeValue* mPhiValue;
	BePhiInst* mPhiInst;	
	int mPhiBlockIdx;
};

class CeBlock
{
public:
	int mEmitOfs;
	Array<CePhiOutgoing> mPhiOutgoing;

public:
	CeBlock()
	{
		mEmitOfs = -1;		
	}
};

class CeJumpEntry
{
public:
	int mEmitPos;
	int mBlockIdx;
};

class CeBuilder
{
public:	
	CeMachine* mCeMachine;
	CeFunction* mCeFunction;
	BeFunction* mBeFunction;	
	CeOperand mReturnVal;	
	BeType* mIntPtrType;
	int mPtrSize;	

	String mError;
	BeDbgLoc* mCurDbgLoc;
	Array<CeBlock> mBlocks;	
	Array<CeJumpEntry> mJumpTable;
	Dictionary<BeValue*, CeOperand> mValueToOperand;
	int mFrameSize;
	Dictionary<BeDbgFile*, int> mDbgFileMap;
	Dictionary<BeFunction*, int> mFunctionMap;
	
public:
	CeBuilder()
	{
		mPtrSize = 0;
		mCeFunction = NULL;
		mBeFunction = NULL;
		mCeMachine = NULL;
		mCurDbgLoc = NULL;
		mFrameSize = 0;
	}
	
	void Fail(const StringImpl& error);

	CeOperand FrameAlloc(BeType* type);
	CeOperand GetOperand(BeValue* value, bool allowAlloca = false);
	CeSizeClass GetSizeClass(int size);
	int GetCodePos();

	void HandleParams();

	void Emit(uint8 val);	
	void Emit(CeOp val);
	void Emit(int32 val);
	void Emit(bool val);
	void Emit(void* ptr, int size);
	void EmitJump(CeOp op, const CeOperand& block);
	void EmitBinarySwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx);	

	void EmitFrameOffset(const CeOperand& val);
	void FlushPhi(CeBlock* ceBlock, int targetBlockIdx);

	void EmitBinaryOp(CeOp iOp, CeOp fOp, const CeOperand& lhs, const CeOperand& rhs, CeOperand& result);
	void EmitUnaryOp(CeOp iOp, CeOp fOp, const CeOperand& val, CeOperand& result);
	void EmitSizedOp(CeOp op, const CeOperand& operand, CeOperand* result, bool allowNonStdSize);
	void Build();
};

class CeFrame
{
public:
	CeFunction* mFunction;
	uint8* mStackPtr;	
	uint8* mFramePtr;
	uint8* mInstPtr;

public:
	CeFrame()
	{
		mFunction = NULL;
		mStackPtr = NULL;
		mFramePtr = NULL;
		mInstPtr = NULL;
	}
};

class CeMachine
{
public:
	Dictionary<BfMethodInstance*, CeFunction*> mFunctions;
	Dictionary<String, CeFunction*> mNamedFunctionMap;
	BfCompiler* mCompiler;
	BfModule* mCeModule;
	int mRevision;
	
	Array<CeFrame> mCallStack;
	Array<uint8> mMemory;	
	uint8* mStackMin;
	Array<CeFunction*> mWorkQueue;

public:
	CeMachine(BfCompiler* compiler);
	~CeMachine();
	
	void Init();	
	void RemoveMethod(BfMethodInstance* methodInstance);
	int GetConstantSize(BfConstant* constant);
	void WriteConstant(uint8* ptr, BfConstant* constant);
	void CreateFunction(BfMethodInstance* methodInstance, CeFunction* ceFunction);		
	bool Execute();

	void PrepareFunction(CeFunction* methodInstance);
	void ProcessWorkQueue();
	CeFunction* GetFunction(BfMethodInstance* methodInstance);

public:
	void CompileStarted();
	void QueueMethod(BfMethodInstance* methodInstance);
	BfTypedValue Call(BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags);
};

NS_BF_END
