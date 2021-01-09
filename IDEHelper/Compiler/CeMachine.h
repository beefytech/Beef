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
class BeConstant;
class BeInst;
class BeDbgFile;
class BePhiInst;
class BeFunction;
class BeSwitchInst;
class BeGlobalVariable;
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
	CeErrorKind_Error,
	CeErrorKind_GlobalVariable,
	CeErrorKind_FunctionPointer,
	CeErrorKind_Intrinsic
};

enum CeOp : int16
{
	CeOp_InvalidOp,
	CeOp_Ret,
	CeOp_SetRetType,
	CeOp_Jmp,
	CeOp_JmpIf,
	CeOp_JmpIfNot,

	CeOp_Error,	
	CeOp_DynamicCastCheck,
	CeOp_GetReflectType,
	CeOp_GetString,
	CeOp_Malloc,
	CeOp_Free,
	
	CeOp_MemSet,
	CeOp_MemSet_Const,
	CeOp_MemCpy,
	
	CeOp_FrameAddr_32,
	CeOp_FrameAddr_64,
	CeOp_FrameAddrOfs_32,

	CeOp_ConstData,
	CeOp_ConstDataRef,
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
	CeOp_GetStaticField,
	CeOp_GetMethod,
	CeOp_GetMethod_Inner,
	CeOp_GetMethod_Virt,
	CeOp_GetMethod_IFace,
	CeOp_Call,
	
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

	~CeFunctionInfo();
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
	CeFunctionKind_Malloc,
	CeFunctionKind_Free,
	CeFunctionKind_FatalError,
	CeFunctionKind_DebugWrite,
	CeFunctionKind_DebugWrite_Int,
	CeFunctionKind_GetReflectType,
	CeFunctionKind_GetReflectTypeById,
	CeFunctionKind_EmitDefinition,
	CeFunctionKind_Sleep,
	CeFunctionKind_Char32_ToLower,
	CeFunctionKind_Char32_ToUpper,
	CeFunctionKind_Char32_IsLower,
	CeFunctionKind_Char32_IsUpper,
	CeFunctionKind_Char32_IsWhiteSpace_EX,
	CeFunctionKind_Char32_IsLetterOrDigit,
	CeFunctionKind_Char32_IsLetter,
	CeFunctionKind_Char32_IsNumber,
};

class CeConstStructFixup
{
public:
	enum Kind
	{
		Kind_None,
		Kind_StringPtr,
		Kind_StringCharPtr,
	};

public:
	Kind mKind;
	int mValue;
	int mOffset;
};

class CeConstStructData
{
public:
	Val128 mHash;
	Array<uint8> mData;
	Array<uint8> mFixedData;
	Array<CeConstStructFixup> mFixups;
	addr_ce mAddr;
	int mBindExecuteId;
	bool mQueueFixups;

public:
	CeConstStructData()
	{
		mBindExecuteId = -1;
		mAddr = 0;
		mQueueFixups = false;
	}
};

class CeInnerFunctionInfo
{
public:
	String mName;
	BeFunction* mBeFunction;
	CeFunction* mOwner;
};

class CeStaticFieldEntry
{
public:
	String mName;
	int mTypeId;
	int mSize;
	addr_ce mAddr;
	int mBindExecuteId;

public:
	CeStaticFieldEntry()
	{
		mTypeId = -1;
		mSize = 0;
		mAddr = 0;		
		mBindExecuteId = -1;		
	}
};

class CeFunction
{
public:
	CeFunctionInfo* mCeFunctionInfo;
	CeInnerFunctionInfo* mCeInnerFunctionInfo;
	BfMethodInstance* mMethodInstance;	
	CeFunctionKind mFunctionKind;
	bool mGenerating;
	bool mInitialized;
	bool mFailed;
	bool mIsVarReturn;
	Array<uint8> mCode;	
	Array<String> mFiles;
	Array<CeEmitEntry> mEmitTable;
	Array<CeCallEntry> mCallTable;
	Array<CeStringEntry> mStringTable;
	Array<CeConstStructData> mConstStructTable;
	Array<CeStaticFieldEntry> mStaticFieldTable;
	Array<BfType*> mTypeTable;
	Array<CeFunction*> mInnerFunctions;
	String mGenError;
	int mFrameSize;	
	int mMaxReturnSize;
	int mId;

public:
	CeFunction()
	{
		mCeFunctionInfo = NULL;
		mCeInnerFunctionInfo = NULL;
		mFunctionKind = CeFunctionKind_Normal;
		mGenerating = false;
		mInitialized = false;
		mMethodInstance = NULL;
		mFailed = false;
		mIsVarReturn = false;
		mFrameSize = 0;
		mMaxReturnSize = 0;
		mId = -1;
	}	

	~CeFunction();
};

enum CeEvalFlags
{
	CeEvalFlags_None = 0,
	CeEvalFlags_Cascade = 1,
	CeEvalFlags_PersistantError = 2,
	CeEvalFlags_DeferIfNotOnlyError = 4,
};

enum CeOperandKind
{
	CeOperandKind_None,
	CeOperandKind_FrameOfs,
	CeOperandKind_AllocaAddr,
	CeOperandKind_Block,
	CeOperandKind_Immediate,
	CeOperandKind_ConstStructTableIdx,	
	CeOperandKind_CallTableIdx
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
		int mCallTableIdx;
		int mStructTableIdx;
		BeConstant* mConstant;
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

#define BF_CE_STACK_SIZE 4*1024*1024
#define BF_CE_MAX_MEMORY 128*1024*1024
#define BF_CE_MAX_CARRYOVER_MEMORY BF_CE_STACK_SIZE + 1024*1024
#define BF_CE_MAX_CARRYOVER_HEAP 1024*1024

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
	CeBuilder* mParentBuilder;
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
	Dictionary<BeConstant*, int> mConstDataMap;
	Dictionary<BeFunction*, int> mInnerFunctionMap;
	Dictionary<BeGlobalVariable*, int> mStaticFieldMap;
	
public:
	CeBuilder()
	{
		mParentBuilder = NULL;
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
	CeErrorKind EmitConst(Array<uint8>& arr, BeConstant* constant);
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
	void EmitZeroes(int size);
	void EmitJump(CeOp op, const CeOperand& block);
	void EmitBinarySwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx);	

	void EmitFrameOffset(const CeOperand& val);
	void FlushPhi(CeBlock* ceBlock, int targetBlockIdx);

	void EmitBinaryOp(CeOp iOp, CeOp fOp, const CeOperand& lhs, const CeOperand& rhs, CeOperand& result);
	void EmitUnaryOp(CeOp iOp, CeOp fOp, const CeOperand& val, CeOperand& result);
	void EmitSizedOp(CeOp op, const CeOperand& operand, CeOperand* result, bool allowNonStdSize);
	void ProcessMethod(BfMethodInstance* methodInstance, BfMethodInstance* dupMethodInstance);
	void Build();
};

class CeFrame
{
public:
	CeFunction* mFunction;
	addr_ce mStackAddr;
	addr_ce mFrameAddr;
	uint8* mInstPtr;
	BfType* mReturnType;

public:
	CeFrame()
	{
		mFunction = NULL;
		mStackAddr = 0;
		mFrameAddr = 0;
		mInstPtr = NULL;
		mReturnType = NULL;
	}
};

class CeStaticFieldInfo
{
public:
	BfFieldInstance* mFieldInstance;	
	addr_ce mAddr;

public:
	CeStaticFieldInfo()
	{
		mFieldInstance = NULL;		
		mAddr = 0;
	}
};

class CeAppendAllocInfo
{
public:
	BfModule* mModule;
	BfIRValue mAllocValue;
	BfIRValue mAppendSizeValue;
};

class CeEmitContext
{
public:
	BfType* mType;	
	String mEmitData;

	CeEmitContext()
	{
		mType = NULL;		
	}
};

class CeMachine
{
public:
	Dictionary<BfMethodInstance*, CeFunctionInfo*> mFunctions;
	Dictionary<String, CeFunctionInfo*> mNamedFunctionMap;
	Dictionary<int, CeFunction*> mFunctionIdMap; // Only used for 32-bit		

	BfCompiler* mCompiler;
	BfModule* mCeModule;
	int mRevision;
	int mRevisionExecuteTime;
	int mExecuteId;	
	int mCurFunctionId;

	// These are only valid for the current execution
	ContiguousHeap* mHeap;
	Array<CeFrame> mCallStack;
	Array<uint8> mMemory;
	Dictionary<int, addr_ce> mStringMap;
	int mStringCharsOffset;
	Dictionary<int, addr_ce> mReflectMap;
	Dictionary<Val128, addr_ce> mConstDataMap;	
	Dictionary<String, CeStaticFieldInfo> mStaticFieldMap;
	HashSet<int> mStaticCtorExecSet;
	CeAppendAllocInfo* mAppendAllocInfo;	
	
	CeEmitContext* mCurEmitContext;
	CeEvalFlags mCurEvalFlags;
	CeBuilder* mCurBuilder;
	CeFunction* mPreparingFunction;
	CeFrame* mCurFrame;
	BfAstNode* mCurTargetSrc;
	BfMethodInstance* mCurMethodInstance;
	BfModule* mCurModule;	
	BfType* mCurExpectingType;	

public:
	CeMachine(BfCompiler* compiler);
	~CeMachine();
	
	BfError* Fail(const StringImpl& error);
	BfError* Fail(const CeFrame& curFrame, const StringImpl& error);

	void Init();	
	uint8* CeMalloc(int size);
	bool CeFree(addr_ce addr);
	addr_ce CeAllocArray(BfArrayType* arrayType, int count, addr_ce& elemsAddr);	
	addr_ce GetReflectType(int typeId);
	addr_ce GetString(int stringId);
	addr_ce GetConstantData(BeConstant* constant);
	BfType* GetBfType(int typeId);	
	void PrepareConstStructEntry(CeConstStructData& constStructData);
	bool CheckMemory(addr_ce addr, int32 size);
	bool GetStringFromStringView(addr_ce addr, StringImpl& str);

	BeContext* GetBeContext();
	BeModule* GetBeModule();
	void DerefMethodInfo(CeFunctionInfo* ceFunctionInfo);
	void RemoveMethod(BfMethodInstance* methodInstance);		
	bool WriteConstant(BfModule* module, addr_ce addr, BfConstant* constant, BfType* type, bool isParams = false);
	CeErrorKind WriteConstant(CeConstStructData& data, BeConstant* constVal);
	BfIRValue CreateConstant(BfModule* module, uint8* ptr, BfType* type, BfType** outType = NULL);
	void CreateFunction(BfMethodInstance* methodInstance, CeFunction* ceFunction);		
	bool Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr, BfType*& returnType);	

	void PrepareFunction(CeFunction* methodInstance, CeBuilder* parentBuilder);	
	void MapFunctionId(CeFunction* ceFunction);

	void CheckFunctions();
	CeFunction* GetFunction(BfMethodInstance* methodInstance, BfIRValue func, bool& added);
	CeFunction* GetPreparedFunction(BfMethodInstance* methodInstance);
	
public:
	void CompileStarted();
	void CompileDone();
	void QueueMethod(BfMethodInstance* methodInstance, BfIRValue func);
	void QueueMethod(BfModuleMethodInstance moduleMethodInstance);
	void QueueStaticField(BfFieldInstance* fieldInstance, const StringImpl& mangledFieldName);

	void SetAppendAllocInfo(BfModule* module, BfIRValue allocValue, BfIRValue appendSizeValue);
	void ClearAppendAllocInfo();

	BfTypedValue Call(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags, BfType* expectingType);
};

NS_BF_END
