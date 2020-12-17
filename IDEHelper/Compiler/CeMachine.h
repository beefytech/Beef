#pragma once

#include "BfSystem.h"
#include "BfModule.h"
#include "BeefySysLib/util/Heap.h"
#include "BeefySysLib/util/AllocDebug.h"

NS_BF_BEGIN

class BfMethodInstance;
class BeModule;
class BeContext;
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

typedef int addr_ce;

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

#define CEOP_SIZED_UNUMERIC(OPNAME) \
	CeOp_##OPNAME##_U8, \
	CeOp_##OPNAME##_U16, \
	CeOp_##OPNAME##_U32, \
	CeOp_##OPNAME##_U64

#define CEOP_SIZED_NUMERIC_PLUSF(OPNAME) \
	CeOp_##OPNAME##_I8, \
	CeOp_##OPNAME##_I16, \
	CeOp_##OPNAME##_I32, \
	CeOp_##OPNAME##_I64, \
	CeOp_##OPNAME##_F32, \
	CeOp_##OPNAME##_F64

enum CeErrorKind
{
	CeErrorKind_None,
	CeErrorKind_GlobalVariable,
	CeErrorKind_FunctionPointer,
	CeErrorKind_Intrinsic
};

enum CeOp : int16
{
	CeOp_InvalidOp,
	CeOp_Ret,
	CeOp_Jmp,
	CeOp_JmpIf,
	CeOp_JmpIfNot,

	CeOp_Error,	
	CeOp_DynamicCastCheck,
	CeOp_GetString,
	CeOp_Malloc,
	CeOp_Free,
	
	CeOp_MemSet,
	CeOp_MemSet_Const,
	CeOp_MemCpy,
	
	CeOp_FrameAddr_32,
	CeOp_FrameAddr_64,

	CeOp_Zero,
	CEOP_SIZED(Const),
	CEOP_SIZED(Load),
	CEOP_SIZED(Store),
	CEOP_SIZED(Move),
	CEOP_SIZED(Push),
	CEOP_SIZED(Pop),

	CeOp_AdjustSP,
	CeOp_AdjustSPNeg,
	CeOp_AdjustSPConst,
	CeOp_GetSP,
	CeOp_SetSP,
	CeOp_Call,
	CeOp_Call_Virt,
	CeOp_Call_IFace,
	
	CeOp_Conv_I8_I16,
	CeOp_Conv_I8_I32,
	CeOp_Conv_I8_I64,
	CeOp_Conv_I8_F32,
	CeOp_Conv_I8_F64,
	CeOp_Conv_I16_I32,
	CeOp_Conv_I16_I64,
	CeOp_Conv_I16_F32,
	CeOp_Conv_I16_F64,
	CeOp_Conv_I32_I64,
	CeOp_Conv_I32_F32,
	CeOp_Conv_I32_F64,	
	CeOp_Conv_I64_F32,
	CeOp_Conv_I64_F64,
	CeOp_Conv_U8_U16,
	CeOp_Conv_U8_U32,
	CeOp_Conv_U8_U64,
	CeOp_Conv_U8_F32,
	CeOp_Conv_U8_F64,
	CeOp_Conv_U16_U32,
	CeOp_Conv_U16_U64,
	CeOp_Conv_U16_F32,
	CeOp_Conv_U16_F64,
	CeOp_Conv_U32_U64,
	CeOp_Conv_U32_F32,
	CeOp_Conv_U32_F64,
	CeOp_Conv_U64_F32,
	CeOp_Conv_U64_F64,
	CeOp_Conv_F32_I8,
	CeOp_Conv_F32_I16,
	CeOp_Conv_F32_I32,
	CeOp_Conv_F32_I64,
	CeOp_Conv_F32_F64,
	CeOp_Conv_F64_I8,
	CeOp_Conv_F64_I16,
	CeOp_Conv_F64_I32,
	CeOp_Conv_F64_I64,
	CeOp_Conv_F64_F32,

	CEOP_SIZED_NUMERIC_PLUSF(AddConst),
	CEOP_SIZED_NUMERIC_PLUSF(Add),
	CEOP_SIZED_NUMERIC_PLUSF(Sub),
	CEOP_SIZED_NUMERIC_PLUSF(Mul),	
	CEOP_SIZED_NUMERIC_PLUSF(Div),
	CEOP_SIZED_UNUMERIC(Div),
	CEOP_SIZED_NUMERIC_PLUSF(Mod),
	CEOP_SIZED_UNUMERIC(Mod),
	CEOP_SIZED_NUMERIC(And),
	CEOP_SIZED_NUMERIC(Or),
	CEOP_SIZED_NUMERIC(Xor),
	CEOP_SIZED_NUMERIC(Shl),
	CEOP_SIZED_NUMERIC(Shr),
	CEOP_SIZED_UNUMERIC(Shr),

	CEOP_SIZED_NUMERIC_PLUSF(Cmp_EQ),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_NE),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_SLT),
	CEOP_SIZED_NUMERIC(Cmp_ULT),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_SLE),
	CEOP_SIZED_NUMERIC(Cmp_ULE),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_SGT),
	CEOP_SIZED_NUMERIC(Cmp_UGT),
	CEOP_SIZED_NUMERIC_PLUSF(Cmp_SGE),
	CEOP_SIZED_NUMERIC(Cmp_UGE),

	CEOP_SIZED_NUMERIC_PLUSF(Neg),
	CeOp_Not_I1,
	CEOP_SIZED_NUMERIC(Not),

	CeOp_COUNT
};

struct CeEmitEntry
{
	int mCodePos;
	int mFile;
	int mLine;
	int mColumn;
};

class CeFunctionInfo
{
public:
	String mName;
	BfMethodInstance* mMethodInstance;
	BfMethodRef mMethodRef;
	CeFunction* mCeFunction;
	int mRefCount;

public:
	CeFunctionInfo()
	{
		mMethodInstance = NULL;
		mCeFunction = NULL;
		mRefCount = 0;
	}
};

class CeCallEntry
{
public:
	CeFunctionInfo* mFunctionInfo;
	int mBindRevision;
	CeFunction* mFunction;

public:
	CeCallEntry()
	{
		mFunctionInfo = NULL;
		mBindRevision = -1;
		mFunction = NULL;
	}
};

class CeStringEntry
{
public:
	int mStringId;
	int mBindExecuteId;
	addr_ce mStringAddr;

public:
	CeStringEntry()
	{
		mStringId = -1;
		mBindExecuteId = -1;
		mStringAddr = 0;
	}
};

enum CeFunctionKind
{
	CeFunctionKind_Normal,
	CeFunctionKind_Extern,
	CeFunctionKind_OOB,
	CeFunctionKind_FatalError,
	CeFunctionKind_DebugWrite,
	CeFunctionKind_DebugWrite_Int,
};

class CeFunction
{
public:
	CeFunctionInfo* mCeFunctionInfo;
	BfMethodInstance* mMethodInstance;
	CeFunctionKind mFunctionKind;
	bool mInitialized;
	bool mFailed;		
	Array<uint8> mCode;	
	Array<String> mFiles;
	Array<CeEmitEntry> mEmitTable;
	Array<CeCallEntry> mCallTable;
	Array<CeStringEntry> mStringTable;
	Array<BfType*> mTypeTable;
	String mGenError;
	int mFrameSize;	

public:
	CeFunction()
	{
		mCeFunctionInfo = NULL;
		mFunctionKind = CeFunctionKind_Normal;
		mInitialized = false;
		mMethodInstance = NULL;
		mFailed = false;
		mFrameSize = 0;
	}	

	~CeFunction();
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
	CeOperandKind_Immediate
};

class CeOperand
{
public:
	CeOperandKind mKind;
	union
	{
		int mFrameOfs;
		int mBlockIdx;
		int mImmediate;
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

	bool IsImmediate()
	{
		return mKind == CeOperandKind_Immediate;
	}
};

#define BF_CE_STACK_SIZE 1024*1024
#define BF_CE_MAX_MEMORY 128*1024*1024

enum CeOperandInfoKind
{
	CEOI_None,
	CEOI_FrameRef,
	CEOI_IMM8,
	CEOI_IMM16,
	CEOI_IMM32,
	CEOI_IMM64,
	CEOI_IMMF32,
	CEOI_IMMF64,
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
	Dictionary<int, int> mStringMap;
	
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
	CeOperand EmitConst(int64 val, int size);
	CeOperand GetOperand(BeValue* value, bool allowAlloca = false, bool allowImmediate = false);
	CeSizeClass GetSizeClass(int size);
	int GetCodePos();

	void HandleParams();
	
	void Emit(uint8 val);
	void Emit(CeOp val);
	void EmitSizedOp(CeOp val, int size);
	void Emit(int32 val);
	void Emit(int64 val);
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
	addr_ce mStackAddr;
	addr_ce mFrameAddr;
	uint8* mInstPtr;

public:
	CeFrame()
	{
		mFunction = NULL;
		mStackAddr = NULL;
		mFrameAddr = NULL;
		mInstPtr = NULL;
	}
};

class CeFunctionRef
{
	//CeFunction* ;
};

class CeMachine
{
public:
	Dictionary<BfMethodInstance*, CeFunctionInfo*> mFunctions;
	Dictionary<String, CeFunctionInfo*> mNamedFunctionMap;
	BfCompiler* mCompiler;
	BfModule* mCeModule;
	int mRevision;
	int mExecuteId;

	ContiguousHeap* mHeap;
	Array<CeFrame> mCallStack;
	Array<uint8> mMemory;
	Dictionary<int, addr_ce> mStringMap;
	
	BfAstNode* mCurTargetSrc;
	BfModule* mCurModule;	

public:
	CeMachine(BfCompiler* compiler);
	~CeMachine();
		
	BfError* Fail(const CeFrame& curFrame, const StringImpl& error);

	void Init();	
	uint8* CeMalloc(int size);
	bool CeFree(addr_ce addr);

	BeContext* GetBeContext();
	BeModule* GetBeModule();
	void DerefMethodInfo(CeFunctionInfo* ceFunctionInfo);
	void RemoveMethod(BfMethodInstance* methodInstance);
	int GetConstantSize(BfConstant* constant);
	void WriteConstant(uint8* ptr, BfConstant* constant);
	void CreateFunction(BfMethodInstance* methodInstance, CeFunction* ceFunction);		
	bool Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr);

	void PrepareFunction(CeFunction* methodInstance);	
	CeFunction* GetFunction(BfMethodInstance* methodInstance, BfIRValue func, bool& added);
	CeFunction* GetPreparedFunction(BfMethodInstance* methodInstance);

public:
	void CompileStarted();
	void QueueMethod(BfMethodInstance* methodInstance, BfIRValue func);
	void QueueMethod(BfModuleMethodInstance moduleMethodInstance);
	BfTypedValue Call(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags);
};

NS_BF_END
