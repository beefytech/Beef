#include "CeMachine.h"
#include "BfModule.h"
#include "BfCompiler.h"
#include "BfIRBuilder.h"
#include "BfParser.h"
#include "BfReducer.h"
#include "BfExprEvaluator.h"
#include "../Backend/BeIRCodeGen.h"
extern "C"
{
#include "BeefySysLib/third_party/utf8proc/utf8proc.h"
}

#define CE_ENABLE_HEAP

USING_NS_BF;

enum CeOpInfoFlag
{
	CeOpInfoFlag_None,
	CeOpInfoFlag_SizeX,
};

struct CeOpInfo
{
	const char* mName;
	CeOperandInfoKind mResultKind;
	CeOperandInfoKind mOperandA;
	CeOperandInfoKind mOperandB;
	CeOperandInfoKind mOperandC;
	CeOpInfoFlag mFlags;
};

#define CEOPINFO_SIZED_1(OPNAME, OPINFOA) \
	{OPNAME "_8", OPINFOA}, \
	{OPNAME "_16", OPINFOA}, \
	{OPNAME "_32", OPINFOA}, \
	{OPNAME "_64", OPINFOA}, \
	{OPNAME "_X", OPINFOA, CEOI_None, CEOI_None, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_8", OPINFOA, OPINFOB}, \
	{OPNAME "_16", OPINFOA, OPINFOB}, \
	{OPNAME "_32", OPINFOA, OPINFOB}, \
	{OPNAME "_64", OPINFOA, OPINFOB}, \
	{OPNAME "_X",  OPINFOA, OPINFOB, CEOI_None, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_64", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_X", OPINFOA, OPINFOB, OPINFOC, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_NUMERIC_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_I8", OPINFOA, OPINFOB}, \
	{OPNAME "_I16", OPINFOA, OPINFOB}, \
	{OPNAME "_I32", OPINFOA, OPINFOB}, \
	{OPNAME "_I64", OPINFOA, OPINFOB}
#define CEOPINFO_SIZED_NUMERIC_PLUSF_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_I8", OPINFOA, OPINFOB}, \
	{OPNAME "_I16", OPINFOA, OPINFOB}, \
	{OPNAME "_I32", OPINFOA, OPINFOB}, \
	{OPNAME "_I64", OPINFOA, OPINFOB}, \
	{OPNAME "_F32", OPINFOA, OPINFOB}, \
	{OPNAME "_F64", OPINFOA, OPINFOB}
#define CEOPINFO_SIZED_NUMERIC_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_I8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I64", OPINFOA, OPINFOB, OPINFOC}
#define CEOPINFO_SIZED_UNUMERIC_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_U8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_U16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_U32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_U64", OPINFOA, OPINFOB, OPINFOC}
#define CEOPINFO_SIZED_NUMERIC_PLUSF_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_I8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_I64", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_F32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_F64", OPINFOA, OPINFOB, OPINFOC}
#define CEOPINFO_SIZED_FLOAT_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_F32", OPINFOA, OPINFOB}, \
	{OPNAME "_F64", OPINFOA, OPINFOB}
#define CEOPINFO_SIZED_FLOAT_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_F32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_F64", OPINFOA, OPINFOB, OPINFOC}

static CeOpInfo gOpInfo[] =
{
	{"InvalidOp"},
	{"Ret"},
	{"SetRet", CEOI_None, CEOI_IMM32},
	{"Jmp", CEOI_None, CEOI_JMPREL},
	{"JmpIf", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"JmpIfNot", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"Error", CEOI_None, CEOI_IMM32},	
	{"DynamicCastCheck", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32},
	{"GetReflectType", CEOI_FrameRef, CEOI_IMM32},
	{"GetString", CEOI_FrameRef, CEOI_IMM32},
	{"Malloc", CEOI_FrameRef, CEOI_FrameRef},
	{"Free", CEOI_None, CEOI_FrameRef},

	{"MemSet", CEOI_None, CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef},
	{"MemSet_Const", CEOI_None, CEOI_FrameRef, CEOI_IMM8, CEOI_IMM32},
	{"MemCpy", CEOI_None, CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddr_32", CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddr_64", CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddrOfs_32", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32},
	{"ConstData", CEOI_FrameRef, CEOI_IMM32},
	{"ConstDataRef", CEOI_FrameRef, CEOI_IMM32},
	{"Zero", CEOI_None, CEOI_FrameRef, CEOI_IMM32},	
		
	{"Const_8", CEOI_FrameRef, CEOI_IMM8},
	{"Const_16", CEOI_FrameRef, CEOI_IMM16},
	{"Const_32", CEOI_FrameRef, CEOI_IMM32},
	{"Const_64", CEOI_FrameRef, CEOI_IMM64},
	{"Const_X", CEOI_FrameRef, CEOI_IMM_VAR},

	CEOPINFO_SIZED_2("Load", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_3("Store", CEOI_None, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_3("Move", CEOI_None, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_2("Push", CEOI_None, CEOI_FrameRef),
	CEOPINFO_SIZED_1("Pop", CEOI_FrameRef),

	{"AdjustSP", CEOI_None, CEOI_FrameRef},
	{"AdjustSPNeg", CEOI_None, CEOI_FrameRef},
	{"AdjustSPConst", CEOI_None, CEOI_IMM32},
	{"CeOp_GetSP", CEOI_FrameRef},
	{"CeOp_SetSP", CEOI_None, CEOI_FrameRef},
	{"GetStaticField", CEOI_FrameRef, CEOI_IMM32},
	{"GetMethod", CEOI_FrameRef, CEOI_IMM32},
	{"GetMethod_Inner", CEOI_FrameRef, CEOI_IMM32},
	{"GetMethod_Virt", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32},
	{"GetMethod_IFace", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32, CEOI_IMM32},
	{"Call", CEOI_None, CEOI_FrameRef},

	{"CeOp_Conv_I8_I16", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I8_I32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I8_I64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I8_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I8_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I16_I32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I16_I64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I16_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I16_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I32_I64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I32_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I32_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I64_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_I64_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U8_U16", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U8_U32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U8_U64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U8_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U8_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U16_U32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U16_U64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U16_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U16_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U32_U64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U32_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U32_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U64_F32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_U64_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F32_I8", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F32_I16", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F32_I32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F32_I64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F32_F64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F64_I8", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F64_I16", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F64_I32", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F64_I64", CEOI_FrameRef, CEOI_FrameRef},
	{"CeOp_Conv_F64_F32", CEOI_FrameRef, CEOI_FrameRef},
	
	CEOPINFO_SIZED_NUMERIC_PLUSF_2("Abs", CEOI_FrameRef, CEOI_FrameRef),
	{"AddConst_I8", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM8},
	{"AddConst_I16", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM16},
	{"AddConst_I32", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32},
	{"AddConst_I64", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM64},
	{"AddConst_F32", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMMF32},
	{"AddConst_F64", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMMF64},	
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Add", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Sub", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Mul", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("SDiv", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("UDiv", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("SMod", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("UMod", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("And", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Or", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Xor", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Shl", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Shr", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_UNUMERIC_3("Shr", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	
	CEOPINFO_SIZED_FLOAT_2("Acos", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Asin", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Atan", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_3("Atan2", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Ceiling", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Cos", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Cosh", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Exp", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Floor", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Log", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Log10", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_3("Pow", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Round", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Sin", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Sinh", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Sqrt", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Tan", CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_FLOAT_2("Tanh", CEOI_FrameRef, CEOI_FrameRef),

	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_EQ", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_NE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SLT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_ULT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SLE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_ULE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SGT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_UGT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SGE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_UGE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),	
	CEOPINFO_SIZED_NUMERIC_PLUSF_2("Neg", CEOI_FrameRef, CEOI_FrameRef),
	{"Not_I1", CEOI_FrameRef, CEOI_FrameRef},
	CEOPINFO_SIZED_NUMERIC_2("Not", CEOI_FrameRef, CEOI_FrameRef),
};

static_assert(BF_ARRAY_COUNT(gOpInfo) == (int)CeOp_COUNT, "gOpName incorrect size");

//////////////////////////////////////////////////////////////////////////

static int FloatToString(float d, char* outStr)
{
	sprintf(outStr, "%1.9g", d);
	int len = (int)strlen(outStr);
	for (int i = 0; outStr[i] != 0; i++)
	{
		if (outStr[i] == '.')
		{
			int checkC = len - 1;
			while (true)
			{
				char c = outStr[checkC];
				if (c == '.')
				{
					return checkC;
				}
				else if (c != '0')
				{
					for (int j = i + 1; j <= checkC; j++)
						if (outStr[j] == 'e')
							return len;
					return checkC + 1;
				}
				checkC--;
			}
		}
	}
	return len;
}

static int DoubleToString(double d, char* outStr)
{
	sprintf(outStr, "%1.17g", d);
	int len = (int)strlen(outStr);
	for (int i = 0; outStr[i] != 0; i++)
	{
		if (outStr[i] == '.')
		{
			int checkC = len - 1;
			while (true)
			{
				char c = outStr[checkC];
				if (c == '.')
				{
					return checkC;
				}
				else if (c == 'e')
				{
					return len;
				}
				else if (c != '0')
				{
					for (int j = i + 1; j <= checkC; j++)
						if (outStr[j] == 'e')
							return len;
					return checkC + 1;
				}
				checkC--;
			}
		}
	}
	return len;
}

//////////////////////////////////////////////////////////////////////////

CeFunction::~CeFunction()
{
	BF_ASSERT(mId == -1);
	for (auto innerFunc : mInnerFunctions)
		delete innerFunc;
	delete mCeInnerFunctionInfo;

	BfLogSys(mCeMachine->mCompiler->mSystem, "CeFunction::~CeFunction %p\n", this);
}

void CeFunction::Print()
{
	CeDumpContext dumpCtx;
	dumpCtx.mCeFunction = this;
	dumpCtx.mStart = &mCode[0];
	dumpCtx.mPtr = dumpCtx.mStart;
	dumpCtx.mEnd = dumpCtx.mPtr + mCode.mSize;
	dumpCtx.Dump();

	String methodName;
	if (mMethodInstance != NULL)
		methodName = mMethodInstance->GetOwner()->mModule->MethodToString(mMethodInstance);
	OutputDebugStrF("Code for %s:\n%s\n", methodName.c_str(), dumpCtx.mStr.c_str());
}

//////////////////////////////////////////////////////////////////////////

CeFunctionInfo::~CeFunctionInfo()
{
	delete mCeFunction;
}

//////////////////////////////////////////////////////////////////////////

#define CE_GET(T) *((T*)(mPtr += sizeof(T)) - 1)

void CeDumpContext::DumpOperandInfo(CeOperandInfoKind operandInfoKind)
{
	switch (operandInfoKind)
	{
	case CEOI_FrameRef:
		{
			int32 addr = CE_GET(int32);
			char str[64];
			if (addr >= 0)
				sprintf(str, "FR+0x%X", addr);
			else
				sprintf(str, "FR-0x%X", -addr);
			mStr += str;
		}
		break;
	case CEOI_IMM8:
		{
			int32 val = CE_GET(int8);
			char str[64];
			sprintf(str, "%d", val);
			mStr += str;
		}
		break;
	case CEOI_IMM16:
		{
			int32 val = CE_GET(int16);
			char str[64];
			sprintf(str, "%d", val);
			mStr += str;
		}
		break;
	case CEOI_IMM32:
		{
			int32 val = CE_GET(int32);
			char str[64];
			sprintf(str, "%d", val);
			mStr += str;
		}
		break;
	case CEOI_IMM64:
		{
			int64 val = CE_GET(int64);
			char str[64];
			sprintf(str, "%lld", (long long)val);
			mStr += str;
		}
		break;
	case CEOI_IMM_VAR:
		{
			mStr += '[';
			int32 size = CE_GET(int32);
			for (int i = 0; i < size; i++)
			{				
				if (i != 0)
					mStr += ", ";
				uint8 val = CE_GET(uint8);
				char str[64];
				sprintf(str, "%X", val);
				mStr += str;
			}
			mStr += ']';			
		}
		break;
	case CEOI_JMPREL:
		{
			int32 val = CE_GET(int32);
			char str[64];
			sprintf(str, "JMP:%04X", (int32)(val + (mPtr - mStart)));
			mStr += str;
		}
		break;
	}
}

void CeDumpContext::Dump()
{
	if (!mCeFunction->mGenError.IsEmpty())
		mStr += StrFormat("Gen Error: %s\n", mCeFunction->mGenError.c_str());
	mStr += StrFormat("Frame Size: %d\n", mCeFunction->mFrameSize);

	uint8* start = mPtr;
	int curEmitIdx = 0;
	CeEmitEntry* curEmitEntry = NULL;
	
	while (mPtr < mEnd)
	{
		int ofs = mPtr - start;
		
		while ((curEmitIdx < mCeFunction->mEmitTable.mSize - 1) && (ofs >= mCeFunction->mEmitTable[curEmitIdx + 1].mCodePos))
			curEmitIdx++;
		if (curEmitIdx < mCeFunction->mEmitTable.mSize)
			curEmitEntry = &mCeFunction->mEmitTable[curEmitIdx];

		CeOp op = CE_GET(CeOp);

		CeOpInfo& opInfo = gOpInfo[op];

		mStr += StrFormat("%04X: ", ofs);

		int32 sizeX = -1;
		if ((opInfo.mFlags & CeOpInfoFlag_SizeX) != 0)
		{
			sizeX = CE_GET(int);
		}

		if (opInfo.mResultKind != CEOI_None)
		{
			DumpOperandInfo(opInfo.mResultKind);
			mStr += " = ";
		}

		mStr += opInfo.mName;

		if (sizeX != -1)
		{
			mStr += StrFormat(":%d", sizeX);
		}

		if (opInfo.mOperandA != CEOI_None)
		{
			mStr += " ";
			DumpOperandInfo(opInfo.mOperandA);
		}

		if (opInfo.mOperandB != CEOI_None)
		{
			mStr += ", ";
			DumpOperandInfo(opInfo.mOperandB);
		}

		if (opInfo.mOperandC != CEOI_None)
		{
			mStr += ", ";
			DumpOperandInfo(opInfo.mOperandC);
		}

		if ((curEmitEntry != NULL) && (curEmitEntry->mFile != -1))
		{						
			mStr += StrFormat("  @%d[%s:%d]", curEmitIdx, GetFileName(mCeFunction->mFiles[curEmitEntry->mFile]).c_str(), 
				curEmitEntry->mLine + 1, curEmitEntry->mColumn + 1);
		}

		mStr += "\n";		
	}
}

//////////////////////////////////////////////////////////////////////////

void CeBuilder::Fail(const StringImpl& str)
{
	if (!mCeFunction->mGenError.IsEmpty())
		return;

	String errStr = StrFormat("Failure during const code generation of %s: %s", mBeFunction->mName.c_str(), str.c_str());
	if (mCurDbgLoc != NULL)
	{
		String filePath;
		mCurDbgLoc->GetDbgFile()->GetFilePath(filePath);
		errStr += StrFormat(" at line %d:%d in %s", mCurDbgLoc->mLine + 1, mCurDbgLoc->mColumn + 1, filePath.c_str());
	}

	mCeFunction->mGenError = errStr;
}

void CeBuilder::Emit(uint8 val)
{
	mCeFunction->mCode.Add((uint8)val);
}

void CeBuilder::Emit(CeOp val)
{
	*(CeOp*)mCeFunction->mCode.GrowUninitialized(sizeof(CeOp)) = val;
}

void CeBuilder::EmitSizedOp(CeOp val, int size)
{
	CeSizeClass sizeClass = GetSizeClass(size);
	Emit((CeOp)(val + sizeClass));	
	if (sizeClass == CeSizeClass_X)	
		Emit((int32)size);
}

void CeBuilder::Emit(int32 val)
{
	*(int32*)mCeFunction->mCode.GrowUninitialized(4) = val;
}

void CeBuilder::Emit(int64 val)
{
	*(int64*)mCeFunction->mCode.GrowUninitialized(8) = val;
}

void CeBuilder::Emit(bool val)
{
	BF_FATAL("Invalid emit");
}

void CeBuilder::Emit(void* ptr, int size)
{
	memcpy(mCeFunction->mCode.GrowUninitialized(size), ptr, size);
}

void CeBuilder::EmitZeroes(int size)
{
	for (int i = 0; i < size; i++)
		Emit((uint8)0);
}

void CeBuilder::EmitJump(CeOp op, const CeOperand& block)
{
	BF_ASSERT(block.mKind == CeOperandKind_Block);
	Emit(op);

	CeJumpEntry jumpEntry;
	jumpEntry.mBlockIdx = block.mBlockIdx;
	jumpEntry.mEmitPos = GetCodePos();
	mJumpTable.Add(jumpEntry);

	Emit((int32)0);
}

void CeBuilder::EmitBinarySwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx)
{
	// This is an empirically determined binary switching limit
	if (endIdx - startIdx >= 18)
	{
// 		int gteLabel = mCurLabelIdx++;
// 
// 		auto mcDefaultBlock = GetOperand(switchInst->mDefaultBlock);
// 
// 		int midIdx = startIdx + (endIdx - startIdx) / 2;
// 		auto& switchCase = switchInst->mCases[midIdx];
// 		auto switchBlock = GetOperand(switchCase.mBlock);
// 		auto mcValue = GetOperand(switchInst->mValue);
// 		auto valueType = GetType(mcValue);
// 
// 		AllocInst(BeMCInstKind_Cmp, mcValue, GetOperand(switchCase.mValue));
// 		AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(gteLabel), BeMCOperand::FromCmpKind(BeCmpKind_SGE));
// 		switchBlock.mBlock->AddPred(mActiveBlock);
// 
// 		CreateBinarySwitchSection(switchInst, startIdx, midIdx);
// 		AllocInst(BeMCInstKind_Br, mcDefaultBlock);
// 		CreateLabel(-1, gteLabel);
// 		CreateBinarySwitchSection(switchInst, midIdx, endIdx);
// 		return;
	}

	for (int caseIdx = startIdx; caseIdx < endIdx; caseIdx++)
	{
		auto& switchCase = switchInst->mCases[caseIdx];
		auto switchBlock = GetOperand(switchCase.mBlock);
		auto mcValue = GetOperand(switchInst->mValue);
		CeOperand result;
		EmitBinaryOp(CeOp_Cmp_EQ_I8, CeOp_Cmp_EQ_F32, mcValue, GetOperand(switchCase.mValue), result);
		EmitJump(CeOp_JmpIf, switchBlock);
		EmitFrameOffset(result);
	}
}

int CeBuilder::GetCodePos()
{
	return (int)mCeFunction->mCode.size();
}

void CeBuilder::EmitFrameOffset(const CeOperand& val)
{
	BF_ASSERT(val.mKind == CeOperandKind_FrameOfs);
	Emit((int32)val.mFrameOfs);
}

void CeBuilder::FlushPhi(CeBlock* ceBlock, int targetBlockIdx)
{
	for (int i = 0; i < (int)ceBlock->mPhiOutgoing.size(); i++)	
	{
		auto& phiOutgoing = ceBlock->mPhiOutgoing[i];
		if (phiOutgoing.mPhiBlockIdx == targetBlockIdx)
		{
			auto targetPhi = GetOperand(phiOutgoing.mPhiInst);
			auto mcVal = GetOperand(phiOutgoing.mPhiValue);
			EmitSizedOp(CeOp_Move_8, mcVal, NULL, true);
			EmitFrameOffset(targetPhi);

			ceBlock->mPhiOutgoing.RemoveAt(i);

			break;
		}
	}
}

void CeBuilder::EmitBinaryOp(CeOp iOp, CeOp fOp, const CeOperand& lhs, const CeOperand& rhs, CeOperand& result)
{
	CeOp op = iOp;
	if (lhs.mType->IsIntable())
	{		
		if (lhs.mType->mSize == 1)
			op = iOp;
		else if (lhs.mType->mSize == 2)
			op = (CeOp)(iOp + 1);
		else if (lhs.mType->mSize == 4)
			op = (CeOp)(iOp + 2);
		else if (lhs.mType->mSize == 8)
			op = (CeOp)(iOp + 3);
		else
			Fail("Invalid int operand size");
	}
	else if (lhs.mType->IsFloat())
	{
		BF_ASSERT(fOp != CeOp_InvalidOp);
		if (lhs.mType->mSize == 4)
			op = fOp;		
		else if (lhs.mType->mSize == 8)
			op = (CeOp)(fOp + 1);
		else
			Fail("Invalid float operand size");
	}
	else
		Fail("Invalid binary operand");
	Emit(op);
		
	if (!result)
		result = FrameAlloc(lhs.mType);
	EmitFrameOffset(result);
	EmitFrameOffset(lhs);
	EmitFrameOffset(rhs);
}

void CeBuilder::EmitUnaryOp(CeOp iOp, CeOp fOp, const CeOperand& val, CeOperand& result)
{
	CeOp op = iOp;
	if (val.mType->IsIntable())
	{
		if (val.mType->mSize == 1)
			op = iOp;
		else if (val.mType->mSize == 2)
			op = (CeOp)(iOp + 1);
		else if (val.mType->mSize == 4)
			op = (CeOp)(iOp + 2);
		else if (val.mType->mSize == 8)
			op = (CeOp)(iOp + 3);
		else
			Fail("Invalid int operand size");
	}
	else if (val.mType->IsFloat())
	{
		if (val.mType->mSize == 4)
			op = fOp;
		else if (val.mType->mSize == 8)
			op = (CeOp)(fOp + 1);
		else
			Fail("Invalid float operand size");
	}
	else
		Fail("Invalid unary operand");
	Emit(op);

	result = FrameAlloc(val.mType);
	EmitFrameOffset(result);
	EmitFrameOffset(val);	
}

void CeBuilder::EmitSizedOp(CeOp baseOp, const CeOperand& operand, CeOperand* outResult, bool allowNonStdSize)
{
	bool isStdSize = true;
	CeOp op = CeOp_InvalidOp;

	if (operand.mType->mSize == 1)
		op = baseOp;
	else if (operand.mType->mSize == 2)
		op = (CeOp)(baseOp + 1);
	else if (operand.mType->mSize == 4)
		op = (CeOp)(baseOp + 2);
	else if (operand.mType->mSize == 8)
		op = (CeOp)(baseOp + 3);
	else
	{
		isStdSize = false;
		op = (CeOp)(baseOp + 4);
	}
	
	Emit(op);

	if (!isStdSize)
	{
		if (!allowNonStdSize)
			Fail("Invalid operand size");
		Emit((int32)operand.mType->mSize);
	}

	if (outResult != NULL)
	{
		*outResult = FrameAlloc(operand.mType);
		EmitFrameOffset(*outResult);
	}	

	EmitFrameOffset(operand);		
}

CeOperand CeBuilder::FrameAlloc(BeType* type)
{
	mFrameSize += type->mSize;

	CeOperand result;
	result.mKind = CeOperandKind_FrameOfs;
	result.mFrameOfs = -mFrameSize;
	result.mType = type;	
	return result;
}

CeOperand CeBuilder::EmitConst(int64 val, int size)
{
	BeType* type = mIntPtrType;
	switch (size)
	{
	case 1:
		type = mCeMachine->GetBeContext()->GetPrimitiveType(BeTypeCode_Int8);
		break;
	case 2:
		type = mCeMachine->GetBeContext()->GetPrimitiveType(BeTypeCode_Int16);
		break;
	case 4:
		type = mCeMachine->GetBeContext()->GetPrimitiveType(BeTypeCode_Int32);
		break;
	case 8:
		type = mCeMachine->GetBeContext()->GetPrimitiveType(BeTypeCode_Int64);
		break;
	default:
		Fail("Bad const size");
	}

	auto result = FrameAlloc(type);

	EmitSizedOp(CeOp_Const_8, type->mSize);
	EmitFrameOffset(result);
	Emit(&val, size);
	return result;
}

CeOperand CeBuilder::GetOperand(BeValue* value, bool allowAlloca, bool allowImmediate)
{
	if (value == NULL)
		return CeOperand();
	
	BeType* errorType = mIntPtrType;
	CeErrorKind errorKind = CeErrorKind_None;

	switch (value->GetTypeId())
	{
	case BeGlobalVariable::TypeId:
		{
			auto globalVar = (BeGlobalVariable*)value;
			if (globalVar->mName.StartsWith("__bfStrObj"))
			{
				int stringId = atoi(globalVar->mName.c_str() + 10);

				int* stringTableIdxPtr = NULL;
				if (mStringMap.TryAdd(stringId, NULL, &stringTableIdxPtr))
				{
					*stringTableIdxPtr = (int)mCeFunction->mStringTable.size();
					CeStringEntry ceStringEntry;
					ceStringEntry.mStringId = stringId;
					mCeFunction->mStringTable.Add(ceStringEntry);
				}

				auto result = FrameAlloc(mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType));

				Emit(CeOp_GetString);
				EmitFrameOffset(result);
				Emit((int32)*stringTableIdxPtr);
				return result;
			}
			else if (globalVar->mName.StartsWith("__bfStrData"))
			{
				int stringId = atoi(globalVar->mName.c_str() + 11);

				int* stringTableIdxPtr = NULL;
				if (mStringMap.TryAdd(stringId, NULL, &stringTableIdxPtr))
				{
					*stringTableIdxPtr = (int)mCeFunction->mStringTable.size();
					CeStringEntry ceStringEntry;
					ceStringEntry.mStringId = stringId;
					mCeFunction->mStringTable.Add(ceStringEntry);
				}

				auto result = FrameAlloc(mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType));

				Emit(CeOp_GetString);
				EmitFrameOffset(result);
				Emit((int32)*stringTableIdxPtr);
				
				BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(
					mCeMachine->mCeModule->mCompiler->mStringTypeDef, BfPopulateType_Data);

				Emit(CeOp_AddConst_I32);
				EmitFrameOffset(result);
				EmitFrameOffset(result);
				Emit((int32)stringTypeInst->mInstSize);

				return result;
			}

			BfFieldInstance** fieldInstancePtr = NULL;
			if (mStaticFieldInstanceMap.TryGetValue(globalVar->mName, &fieldInstancePtr))
			{				
				int* staticFieldTableIdxPtr = NULL;
				if (mStaticFieldMap.TryAdd(globalVar, NULL, &staticFieldTableIdxPtr))
				{
					CeStaticFieldEntry staticFieldEntry;
					staticFieldEntry.mTypeId = (*fieldInstancePtr)->mOwner->mTypeId;
					staticFieldEntry.mName = globalVar->mName;					
					staticFieldEntry.mSize = globalVar->mType->mSize;
					*staticFieldTableIdxPtr = (int)mCeFunction->mStaticFieldTable.size();
					mCeFunction->mStaticFieldTable.Add(staticFieldEntry);
				}

				auto result = FrameAlloc(mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType));

				Emit(CeOp_GetStaticField);
				EmitFrameOffset(result);
				Emit((int32)*staticFieldTableIdxPtr);

				return result;
			}

			if (globalVar->mInitializer != NULL)
			{
				auto result = GetOperand(globalVar->mInitializer, false, true);
				if (result.mKind == CeOperandKind_ConstStructTableIdx)
				{
					auto& constTableEntry = mCeFunction->mConstStructTable[result.mStructTableIdx];
					auto ptrType = mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType);
					auto dataResult = FrameAlloc(ptrType);
					Emit(CeOp_ConstDataRef);
					EmitFrameOffset(dataResult);
					Emit((int32)result.mCallTableIdx);
					return dataResult;
				}

				return result;
			}

			errorKind = CeErrorKind_GlobalVariable;
			errorType = mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType);
		}
		break;
	case BeCastConstant::TypeId:
		{
 			auto constant = (BeCastConstant*)value;
 
 			CeOperand mcOperand;
 			auto result = GetOperand(constant->mTarget);
			result.mType = constant->mType;
			return result;
		}
		break;
	case BeConstant::TypeId:
		{
			uint64 u64Val = 0;
			float fVal = 0;
			void* dataPtr = NULL;
			int dataSize = 0;

			auto constant = (BeConstant*)value;
			CeOperand mcOperand;
			switch (constant->mType->mTypeCode)
			{
			case BeTypeCode_Int8:
			case BeTypeCode_Int16:
			case BeTypeCode_Int32:
			case BeTypeCode_Int64:
				if (allowImmediate)
				{
					CeOperand result;
					result.mKind = CeOperandKind_Immediate;
					result.mImmediate = constant->mInt32;
					result.mType = constant->mType;
					return result;
				}

			case BeTypeCode_Boolean:			
			case BeTypeCode_Double:
				dataPtr = &constant->mUInt64;
				dataSize = constant->mType->mSize;
				break;
			case BeTypeCode_Float:
				fVal = (float)constant->mDouble;
				dataPtr = &fVal;
				dataSize = 4;
				break;
			case BeTypeCode_Pointer:
			{
				if (constant->mTarget == NULL)
				{
					dataPtr = &u64Val;
					dataSize = mPtrSize;
				}
				else
				{
 					auto relTo = GetOperand(constant->mTarget);
					if (relTo)
					{
						auto result = relTo;
						result.mType = constant->mType;
						return result;
					}
 
//  					if (relTo.mKind == CeOperandKind_Immediate_Null)
//  					{
//  						mcOperand.mKind = CeOperandKind_Immediate_Null;
//  						mcOperand.mType = constant->mType;
//  						return mcOperand;
//  					}
//  
//  					mcOperand = AllocVirtualReg(constant->mType);
//  					auto vregInfo = GetVRegInfo(mcOperand);
//  					vregInfo->mDefOnFirstUse = true;
//  					vregInfo->mRelTo = relTo;
//  					vregInfo->mIsExpr = true;
//  
 					//return mcOperand;
				}
			}
			break;
 			case BeTypeCode_Struct:
 			case BeTypeCode_SizedArray:
 			case BeTypeCode_Vector:
				{
					auto beType = constant->mType;
					auto result = FrameAlloc(beType);
					Emit(CeOp_Zero);
					EmitFrameOffset(result);
					Emit((int32)beType->mSize);
					return result;
				}
 				//mcOperand.mImmediate = constant->mInt64;
 				//mcOperand.mKind = CeOperandKind_Immediate_i64;
 				break;
// 			default:
// 				Fail("Unhandled constant type");
			}

			if (dataSize != 0)
			{
				auto beType = constant->mType;
				auto result = FrameAlloc(beType);

				CeSizeClass sizeClass = GetSizeClass(dataSize);
				Emit((CeOp)(CeOp_Const_8 + sizeClass));
				EmitFrameOffset(result);
				if (sizeClass == CeSizeClass_X)						
					Emit((int32)dataSize);
				if (dataPtr != 0)
					Emit(dataPtr, dataSize);				
				else
				{
					for (int i = 0; i < dataSize; i++)
						Emit((uint8)0);
				}

				return result;
			}						
		}
		break;
	case BeStructConstant::TypeId:
		{
			int* constDataPtr = NULL;
			auto structConstant = (BeStructConstant*)value;
			if (mConstDataMap.TryAdd(structConstant, NULL, &constDataPtr))
			{												
				CeConstStructData constStructData;
				constStructData.mQueueFixups = true;
				errorKind = mCeMachine->WriteConstant(constStructData, structConstant, NULL);
				if (errorKind == CeErrorKind_None)
				{
					*constDataPtr = (int)mCeFunction->mConstStructTable.size();
					mCeFunction->mConstStructTable.Add(constStructData);
				}
				else
				{
					*constDataPtr = -1;
				}
			}

			if (*constDataPtr != -1)
			{
				if (!allowImmediate)
				{
					auto result = FrameAlloc(structConstant->mType);
					Emit(CeOp_ConstData);
					EmitFrameOffset(result);
					Emit((int32)*constDataPtr);
					return result;
				}
				else
				{
					CeOperand result;
					result.mKind = CeOperandKind_ConstStructTableIdx;
					result.mCallTableIdx = *constDataPtr;
					result.mType = structConstant->mType;
					return result;
				}
			}
			else
			{				
				errorKind = CeErrorKind_GlobalVariable;
			}
		}
		break;	
	case BeGEPConstant::TypeId:
		{
 			auto gepConstant = (BeGEPConstant*)value;
 
 			auto mcVal = GetOperand(gepConstant->mTarget);
 
 			BePointerType* ptrType = (BePointerType*)mcVal.mType;
 			BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
 
 			auto result = mcVal;
 
 			// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.				
 			int64 byteOffset = 0;
 			BeType* elementType = NULL;
 			byteOffset += gepConstant->mIdx0 * ptrType->mElementType->mSize;
 
 			if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
 			{
 				BeStructType* structType = (BeStructType*)ptrType->mElementType;
 				auto& structMember = structType->mMembers[gepConstant->mIdx1];
 				elementType = structMember.mType;
 				byteOffset = structMember.mByteOffset;
 			}
 			else
 			{
 				BF_ASSERT(ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray);
 				auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
 				elementType = arrayType->mElementType;
 				byteOffset = gepConstant->mIdx1 * elementType->GetStride();
 			}
 
 			auto elementPtrType = mCeMachine->GetBeContext()->GetPointerTo(elementType);
			result = FrameAlloc(elementPtrType);
			EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
			EmitFrameOffset(result);
			EmitFrameOffset(mcVal);
			Emit(&byteOffset, mPtrSize);

 			return result;
		}
		break;
	case BeExtractValueConstant::TypeId:
		{
			// Note: this only handles zero-aggregates
 			auto extractConstant = (BeExtractValueConstant*)value;
 			auto elementType = extractConstant->GetType();
 
 			auto mcVal = GetOperand(extractConstant->mTarget); 			
 
 			BeConstant beConstant;
 			beConstant.mType = elementType;
 			beConstant.mUInt64 = 0;
 			return GetOperand(&beConstant);
		}
		break;
	case BeFunction::TypeId:		
		{
			auto beFunction = (BeFunction*)value;

			int* callIdxPtr = NULL;
			if (mFunctionMap.TryAdd(beFunction, NULL, &callIdxPtr))
			{
				CeFunctionInfo* ceFunctionInfo = NULL;
				mCeMachine->mNamedFunctionMap.TryGetValue(beFunction->mName, &ceFunctionInfo);
				if (ceFunctionInfo != NULL)
					ceFunctionInfo->mRefCount++;
				else
				{
					auto checkBuilder = this;
					if (checkBuilder->mParentBuilder != NULL)
						checkBuilder = checkBuilder->mParentBuilder;

					int innerFunctionIdx = 0;
					if (checkBuilder->mInnerFunctionMap.TryGetValue(beFunction, &innerFunctionIdx))
					{
						auto innerFunction = checkBuilder->mCeFunction->mInnerFunctions[innerFunctionIdx];
						if (!innerFunction->mInitialized)
							mCeMachine->PrepareFunction(innerFunction, checkBuilder);

						CeOperand result = FrameAlloc(mCeMachine->GetBeContext()->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));
						Emit(CeOp_GetMethod_Inner);
						EmitFrameOffset(result);
						Emit((int32)innerFunctionIdx);
						return result;
					}

					Fail(StrFormat("Unable to locate method %s", beFunction->mName.c_str()));
				}

				CeCallEntry callEntry;
				callEntry.mFunctionInfo = ceFunctionInfo;
				*callIdxPtr = (int)mCeFunction->mCallTable.size();
				mCeFunction->mCallTable.Add(callEntry);
			}

			if (allowImmediate)
			{
				CeOperand result;
				result.mKind = CeOperandKind_CallTableIdx;
				result.mCallTableIdx = *callIdxPtr;
				return result;
			}

			CeOperand result = FrameAlloc(mCeMachine->GetBeContext()->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));
			Emit(CeOp_GetMethod);
			EmitFrameOffset(result);
			Emit((int32)*callIdxPtr);
			return result;
		}
		break;
	case BeCallInst::TypeId:
		{
// 			auto callInst = (BeCallInst*)value;
// 			if (callInst->mInlineResult != NULL)
// 				return GetOperand(callInst->mInlineResult);
		}
		break;	
	}

	CeOperand* operandPtr = NULL;
	mValueToOperand.TryGetValue(value, &operandPtr);
	
	if (errorKind != CeErrorKind_None)
	{
		Emit(CeOp_Error);
		Emit((int32)errorKind);
	}
	else
	{
		if (operandPtr == NULL)
		{
			BeDumpContext dumpCtx;
			String str;
			dumpCtx.ToString(str, value);
			Fail(StrFormat("Unable to find bevalue for operand: %s", str.c_str()));
		}
	}
	if (operandPtr == NULL)
	{
		return FrameAlloc(errorType);
	}

	auto operand = *operandPtr;

	if ((operand.mKind == CeOperandKind_AllocaAddr) && (!allowAlloca))
	{
		auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
		
		auto ptrType = mCeMachine->GetBeContext()->GetPointerTo(operand.mType);
		auto result = FrameAlloc(ptrType);
		Emit((mPtrSize == 4) ? CeOp_FrameAddr_32 : CeOp_FrameAddr_64);
		EmitFrameOffset(result);
		Emit((int32)operand.mFrameOfs);
		return result;
	}

	return operand;
}

CeSizeClass CeBuilder::GetSizeClass(int size)
{
	switch (size)
	{
	case 1:
		return CeSizeClass_8;
	case 2:
		return CeSizeClass_16;
	case 4:
		return CeSizeClass_32;
	case 8:
		return CeSizeClass_64;
	default:
		return CeSizeClass_X;
	}
}

void CeBuilder::HandleParams()
{
 	auto beModule = mBeFunction->mModule;
// 	int regIdxOfs = 0;
// 	int paramOfs = 0;
	auto retType = mBeFunction->GetFuncType()->mReturnType;
	
	int frameOffset = 0;

	if (mCeFunction->mMaxReturnSize > 0)
	{				
		mReturnVal.mKind = CeOperandKind_AllocaAddr;
		mReturnVal.mFrameOfs = frameOffset;
		frameOffset += mCeFunction->mMaxReturnSize;
	}

	int paramOfs = 0;
	//for (int paramIdx = (int)mBeFunction->mParams.size() - 1; paramIdx >= 0; paramIdx--)

	for (int paramIdx = 0; paramIdx < mBeFunction->mParams.size(); paramIdx++)
	{
		auto funcType = mBeFunction->GetFuncType();
		auto& typeParam = funcType->mParams[paramIdx + paramOfs];
		auto& param = mBeFunction->mParams[paramIdx + paramOfs];
		auto beArg = beModule->GetArgument(paramIdx + paramOfs);
		auto paramType = typeParam.mType;		

		CeOperand ceOperand;
		ceOperand.mKind = CeOperandKind_FrameOfs;
		ceOperand.mFrameOfs = frameOffset;
		ceOperand.mType = paramType;

		frameOffset += paramType->mSize;

		mValueToOperand[beArg] = ceOperand;
	}
}

void CeBuilder::ProcessMethod(BfMethodInstance* methodInstance, BfMethodInstance* dupMethodInstance)
{
	SetAndRestoreValue<BfMethodState*> prevMethodStateInConstEval(mCeMachine->mCeModule->mCurMethodState, NULL);

	auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
	auto irBuilder = mCeMachine->mCeModule->mBfIRBuilder;
	auto beModule = irCodeGen->mBeModule;
		
	dupMethodInstance->mIsReified = true;
	dupMethodInstance->mInCEMachine = false; // Only have the original one
	
	mCeMachine->mCeModule->mHadBuildError = false;
	auto irState = irBuilder->GetState();
	auto beState = irCodeGen->GetState();
	mCeMachine->mCeModule->ProcessMethod(dupMethodInstance, true);
	irCodeGen->SetState(beState);
	irBuilder->SetState(irState);
}

void CeBuilder::Build()
{
	auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
	auto irBuilder = mCeMachine->mCeModule->mBfIRBuilder;
	auto beModule = irCodeGen->mBeModule;

	mCeFunction->mFailed = true;

	auto methodInstance = mCeFunction->mMethodInstance;
	
	if (methodInstance != NULL)
	{
		BfMethodInstance dupMethodInstance;
		dupMethodInstance.CopyFrom(methodInstance);
		auto methodDef = methodInstance->mMethodDef;

		bool isGenericVariation = (methodInstance->mIsUnspecializedVariation) || (methodInstance->GetOwner()->IsUnspecializedTypeVariation());				
		int dependentGenericStartIdx = 0;
		if ((((methodInstance->mMethodInfoEx != NULL) && ((int)methodInstance->mMethodInfoEx->mMethodGenericArguments.size() > dependentGenericStartIdx)) ||
			((methodInstance->GetOwner()->IsGenericTypeInstance()) && (!isGenericVariation) && (!methodInstance->mMethodDef->mIsLocalMethod))))
		{
			auto unspecializedMethodInstance = mCeMachine->mCeModule->GetUnspecializedMethodInstance(methodInstance, !methodInstance->mMethodDef->mIsLocalMethod);
			if (!unspecializedMethodInstance->mHasBeenProcessed)
			{
				BfMethodInstance dupUnspecMethodInstance;
				dupUnspecMethodInstance.CopyFrom(unspecializedMethodInstance);
				ProcessMethod(unspecializedMethodInstance, &dupUnspecMethodInstance);
				dupMethodInstance.GetMethodInfoEx()->mGenericTypeBindings = dupUnspecMethodInstance.mMethodInfoEx->mGenericTypeBindings;
			}
		}

		// Clear this so we can properly get QueueStaticField calls
		mCeMachine->mCeModule->mStaticFieldRefs.Clear();

		int startFunctionCount = (int)beModule->mFunctions.size();
		ProcessMethod(methodInstance, &dupMethodInstance);		
		
		if (!dupMethodInstance.mIRFunction)
		{
			mCeFunction->mFailed = true;
			return;
		}		
		mBeFunction = (BeFunction*)irCodeGen->GetBeValue(dupMethodInstance.mIRFunction.mId);

		mIntPtrType = irCodeGen->mBeContext->GetPrimitiveType((mPtrSize == 4) ? BeTypeCode_Int32 : BeTypeCode_Int64);

		for (int funcIdx = startFunctionCount; funcIdx < (int)beModule->mFunctions.size(); funcIdx++)
		{
			auto beFunction = beModule->mFunctions[funcIdx];
			if (beFunction == mBeFunction)
				continue;
			if (beFunction->mBlocks.IsEmpty())
				continue;

			CeFunction* innerFunction = new CeFunction();			
			innerFunction->mCeMachine = mCeMachine;
			innerFunction->mIsVarReturn = beFunction->mIsVarReturn;
			innerFunction->mCeInnerFunctionInfo = new CeInnerFunctionInfo();
			innerFunction->mCeInnerFunctionInfo->mName = beFunction->mName;
			innerFunction->mCeInnerFunctionInfo->mBeFunction = beFunction;
			innerFunction->mCeInnerFunctionInfo->mOwner = mCeFunction;
			mInnerFunctionMap[beFunction] = (int)mCeFunction->mInnerFunctions.size();
			mCeFunction->mInnerFunctions.Add(innerFunction);
			mCeMachine->MapFunctionId(innerFunction);
		}

		if (!mCeFunction->mCeFunctionInfo->mName.IsEmpty())
		{
			BF_ASSERT(mCeFunction->mCeFunctionInfo->mName == mBeFunction->mName);
		}
		else
		{
			mCeFunction->mCeFunctionInfo->mName = mBeFunction->mName;
			mCeMachine->mNamedFunctionMap[mCeFunction->mCeFunctionInfo->mName] = mCeFunction->mCeFunctionInfo;
		}

		if (mCeMachine->mCeModule->mHadBuildError)
		{
			mCeFunction->mGenError = "Method had errors";
			mCeMachine->mCeModule->mHadBuildError = false;
			return;
		}
	}
	else
	{
		BF_ASSERT(mCeFunction->mCeInnerFunctionInfo != NULL);
		mBeFunction = mCeFunction->mCeInnerFunctionInfo->mBeFunction;
		BF_ASSERT(mBeFunction != NULL);
		mCeFunction->mCeInnerFunctionInfo->mBeFunction = NULL;
	}

	SetAndRestoreValue<BeFunction*> prevBeFunction(beModule->mActiveFunction, mBeFunction);	
		
	// Create blocks
	for (int blockIdx = 0; blockIdx < (int)mBeFunction->mBlocks.size(); blockIdx++)			
	{
		auto beBlock = mBeFunction->mBlocks[blockIdx];

		CeBlock ceBlock;
		mBlocks.Add(ceBlock);

		CeOperand ceOperand;
		ceOperand.mKind = CeOperandKind_Block;
		ceOperand.mBlockIdx = blockIdx;
		mValueToOperand[beBlock] = ceOperand;
	}

	// Instruction pre-pass
	for (int blockIdx = 0; blockIdx < (int)mBeFunction->mBlocks.size(); blockIdx++)
	{
		auto beBlock = mBeFunction->mBlocks[blockIdx];
		auto& ceBlock = mBlocks[blockIdx];
		
		for (int instIdx = 0; instIdx < (int)beBlock->mInstructions.size(); instIdx++)
		{
			auto inst = beBlock->mInstructions[instIdx];
			
			int instType = inst->GetTypeId();

			switch (instType)
			{
			case BePhiInst::TypeId:
				{					
					auto castedInst = (BePhiInst*)inst;

					auto resultType = castedInst->GetType();

					auto phiResult = FrameAlloc(resultType);
					mValueToOperand[castedInst] = phiResult;

					for (auto& phiIncoming : castedInst->mIncoming)
					{
						auto incomingBlockOpr = GetOperand(phiIncoming->mBlock);
						auto& incomingBlock = mBlocks[incomingBlockOpr.mBlockIdx];

						CePhiOutgoing phiOutgoing;
						phiOutgoing.mPhiValue = phiIncoming->mValue;
						phiOutgoing.mPhiInst = castedInst;
						phiOutgoing.mPhiBlockIdx = blockIdx;						
						incomingBlock.mPhiOutgoing.Add(phiOutgoing);
					}
				}
				break;
			case BeRetInst::TypeId:
			case BeSetRetInst::TypeId:
				{
					auto castedInst = (BeRetInst*)inst;
					if (castedInst->mRetValue != NULL)
					{
						auto retType = castedInst->mRetValue->GetType();						
						mCeFunction->mMaxReturnSize = BF_MAX(retType->mSize, mCeFunction->mMaxReturnSize);
					}
				}
				break;
			}
		}
	}

	// Primary instruction pass
	BeDbgLoc* prevEmitDbgPos = NULL;
	bool inHeadAlloca = true;
	for (int blockIdx = 0; blockIdx < (int)mBeFunction->mBlocks.size(); blockIdx++)
	{
		auto beBlock = mBeFunction->mBlocks[blockIdx];
		auto ceBlock = &mBlocks[blockIdx];
		
		ceBlock->mEmitOfs = GetCodePos();

		if (blockIdx == 0)
			HandleParams();		

		for (int instIdx = 0; instIdx < (int)beBlock->mInstructions.size(); instIdx++)
		{
			auto inst = beBlock->mInstructions[instIdx];
			CeOperand result;

			int startCodePos = GetCodePos();

			mCurDbgLoc = inst->mDbgLoc;			

			int instType = inst->GetTypeId();

			switch (instType)
			{
			case BeAllocaInst::TypeId:
			case BeNumericCastInst::TypeId:
			case BeBitCastInst::TypeId:
				break;
			default:
				inHeadAlloca = false;
				break;
			}

			switch (instType)
			{
			case BeEnsureInstructionAtInst::TypeId:
			case BeNopInst::TypeId:
			case BeDbgDeclareInst::TypeId:
			case BeLifetimeStartInst::TypeId:
			case BeLifetimeEndInst::TypeId:
			case BeLifetimeExtendInst::TypeId:
			case BeValueScopeStartInst::TypeId:
			case BeValueScopeEndInst::TypeId:
			case BeValueScopeRetainInst::TypeId:
			case BeComptimeGetVirtualFunc::TypeId:
			case BeComptimeGetInterfaceFunc::TypeId:
				break;
			case BeUnreachableInst::TypeId:
				Emit(CeOp_InvalidOp);
				break;
			case BeUndefValueInst::TypeId:
				{
					auto castedInst = (BeUndefValueInst*)inst;
					result = FrameAlloc(castedInst->mType);
				}
				break;
			case BeAllocaInst::TypeId:
				{
					auto castedInst = (BeAllocaInst*)inst;

					CeOperand ceSize;
					ceSize.mKind = CeOperandKind_Immediate;
					ceSize.mImmediate = castedInst->mType->mSize;
					ceSize.mType = mIntPtrType;
					bool isAligned16 = false;
					int align = castedInst->mAlign;
					BeType* allocType = castedInst->mType;
					bool preservedVolatiles = false;					

					if (castedInst->mArraySize != NULL)
					{
						auto mcArraySize = GetOperand(castedInst->mArraySize, false, true);
						if (mcArraySize.IsImmediate())
						{
							ceSize.mImmediate = ceSize.mImmediate * mcArraySize.mImmediate;
						}
						else
						{							
							inHeadAlloca = false;
							if (ceSize.mImmediate == 1)
							{
								ceSize = mcArraySize;
							}
							else
							{
								ceSize = EmitConst(ceSize.mImmediate, mcArraySize.mType->mSize);

								EmitSizedOp(CeOp_Mul_I8, ceSize.mType->mSize);
								EmitFrameOffset(ceSize);
								EmitFrameOffset(ceSize);
								EmitFrameOffset(mcArraySize);
							}
						}
					}

					if (inHeadAlloca)
					{
						BF_ASSERT(ceSize.mKind == CeOperandKind_Immediate);
						mFrameSize += ceSize.mImmediate;

						result.mKind = CeOperandKind_AllocaAddr;
						result.mFrameOfs = -mFrameSize;
						result.mType = castedInst->mType;
					}
					else
					{
						if (ceSize.mKind == CeOperandKind_Immediate)
						{
							Emit(CeOp_AdjustSPConst);
							Emit((int32)-ceSize.mImmediate);
						}
						else
						{
							Emit(CeOp_AdjustSPNeg);
							EmitFrameOffset(ceSize);
						}
						
						auto ptrType = beModule->mContext->GetPointerTo(allocType);

						result = FrameAlloc(ptrType);
						Emit(CeOp_GetSP);
						EmitFrameOffset(result);						
					}
				}
				break;
			case BeLoadInst::TypeId:
				{
					auto castedInst = (BeLoadInst*)inst;
					auto ceTarget = GetOperand(castedInst->mTarget, true);

					if (ceTarget.mKind == CeOperandKind_AllocaAddr)
					{
						if (inst->mRefCount <= 1)
						{
							result = ceTarget;
							result.mKind = CeOperandKind_FrameOfs;
						}
						else
						{							
							ceTarget.mKind = CeOperandKind_FrameOfs;
							result = FrameAlloc(ceTarget.mType);
							EmitSizedOp(CeOp_Move_8, ceTarget, NULL, true);
							Emit((int32)result.mFrameOfs);							
						}
					}
					else
					{
						BF_ASSERT(ceTarget.mType->IsPointer());
						auto pointerType = (BePointerType*)ceTarget.mType;
						auto elemType = pointerType->mElementType;
						
						CeOperand refOperand = ceTarget;
						refOperand.mType = elemType;						
						EmitSizedOp(CeOp_Load_8, refOperand, &result, true);
					}
				}
				break;
			case BeBinaryOpInst::TypeId:
				{
					auto castedInst = (BeBinaryOpInst*)inst;
					auto ceLHS = GetOperand(castedInst->mLHS);
					auto ceRHS = GetOperand(castedInst->mRHS);

					switch (castedInst->mOpKind)
					{
					case BeBinaryOpKind_Add:
						EmitBinaryOp(CeOp_Add_I8, CeOp_Add_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_Subtract:
						EmitBinaryOp(CeOp_Sub_I8, CeOp_Sub_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_Multiply:
						EmitBinaryOp(CeOp_Mul_I8, CeOp_Mul_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_SDivide:
						EmitBinaryOp(CeOp_Div_I8, CeOp_Div_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_UDivide:
						EmitBinaryOp(CeOp_Div_U8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_SModulus:
						EmitBinaryOp(CeOp_Mod_I8, CeOp_Mod_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_UModulus:
						EmitBinaryOp(CeOp_Mod_U8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_BitwiseAnd:
						EmitBinaryOp(CeOp_And_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_BitwiseOr:
						EmitBinaryOp(CeOp_Or_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_ExclusiveOr:
						EmitBinaryOp(CeOp_Xor_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_LeftShift:
						EmitBinaryOp(CeOp_Shl_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_RightShift:
						EmitBinaryOp(CeOp_Shr_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_ARightShift:
						EmitBinaryOp(CeOp_Shr_U8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					default:
						Fail("Invalid binary op");
					}
				}
				break;
			case BeBitCastInst::TypeId:
				{
					auto castedInst = (BeBitCastInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue, false, true);
					if (castedInst->mToType->IsInt())
					{
						BF_ASSERT(castedInst->mToType->mSize == 8);
					}
					else
						BF_ASSERT(castedInst->mToType->IsPointer());
					auto toType = castedInst->mToType;

					if (mcValue.IsImmediate())
					{
						if (mcValue.mImmediate == 0)
						{
							CeOperand newImmediate;
							newImmediate.mKind = CeOperandKind_Immediate;
							newImmediate.mType = toType;
							result = newImmediate;
						}
						else
						{
							// Non-zero constant.  Weird case, just do an actual MOV
							result = FrameAlloc(toType);							
							EmitSizedOp(CeOp_Const_8, result, NULL, true);
							int64 val = mcValue.mImmediate;
							Emit(&val, toType->mSize);
						}
					}
					else
					{
						if (toType->mSize != mcValue.mType->mSize)
							Fail("Invalid bitcast");

						result = mcValue;
						result.mType = toType;
					}
				}
				break;
			case BeNumericCastInst::TypeId:
				{
					auto castedInst = (BeNumericCastInst*)inst;
					auto ceValue = GetOperand(castedInst->mValue);
					auto fromType = ceValue.mType;
					if (fromType == castedInst->mToType)
					{
						// If it's just a sign change then leave it alone
						result = ceValue;
					}
					else
					{
						auto toType = castedInst->mToType;
						if ((toType->IsIntable()) && (fromType->IsIntable()) && (toType->mSize <= fromType->mSize))
						{
							// For truncating values, no actual instructions are needed
							// Note that a copy is not needed because of SSA rules
							result = ceValue;
							result.mType = toType;							
						}
						else
						{							
							result = FrameAlloc(toType);

							CeOp op = CeOp_InvalidOp;
							
							BeTypeCode fromTypeCode = fromType->mTypeCode;
							BeTypeCode toTypeCode = toType->mTypeCode;
														
							if ((castedInst->mValSigned) && (castedInst->mToSigned))
							{
								switch (fromTypeCode)
								{
								case BeTypeCode_Int8:
									switch (toTypeCode)
									{
									case BeTypeCode_Int16:
										op = CeOp_Conv_I8_I16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_I8_I32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_I8_I64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_I8_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I8_F64;
										break;
									}
									break;
								case BeTypeCode_Int16:
									switch (toTypeCode)
									{
									case BeTypeCode_Int32:
										op = CeOp_Conv_I16_I32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_I16_I64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_I16_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I16_F64;
										break;
									}
									break;
								case BeTypeCode_Int32:
									switch (toTypeCode)
									{
									case BeTypeCode_Int64:
										op = CeOp_Conv_I32_I64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_I32_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I32_F64;
										break;
									}
									break;
								case BeTypeCode_Int64:
									switch (toTypeCode)
									{									
									case BeTypeCode_Float:
										op = CeOp_Conv_I64_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I64_F64;
										break;
									}
									break;
								case BeTypeCode_Float:
									switch (toTypeCode)
									{
									case BeTypeCode_Int8:
										op = CeOp_Conv_F32_I8;
										break;
									case BeTypeCode_Int16:
										op = CeOp_Conv_F32_I16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_F32_I32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_F32_I64;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_F32_F64;
										break;
									}
									break;
								case BeTypeCode_Double:
									switch (toTypeCode)
									{
									case BeTypeCode_Int8:
										op = CeOp_Conv_F64_I8;
										break;
									case BeTypeCode_Int16:
										op = CeOp_Conv_F64_I16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_F64_I32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_F64_I64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_F64_F32;
										break;
									}
									break;
								}
							}
							else
							{
								switch (fromTypeCode)
								{
								case BeTypeCode_Int8:
									switch (toTypeCode)
									{
									case BeTypeCode_Int16:
										op = CeOp_Conv_U8_U16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_U8_U32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_U8_U64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_I8_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I8_F64;
										break;
									}
									break;
								case BeTypeCode_Int16:
									switch (toTypeCode)
									{
									case BeTypeCode_Int32:
										op = CeOp_Conv_U16_U32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_U16_U64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_U16_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_U16_F64;
										break;
									}
									break;
								case BeTypeCode_Int32:
									switch (toTypeCode)
									{
									case BeTypeCode_Int64:
										op = CeOp_Conv_U32_U64;
										break;
									case BeTypeCode_Float:
										op = CeOp_Conv_U32_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_U32_F64;
										break;
									}
									break;
								case BeTypeCode_Int64:
									switch (toTypeCode)
									{
									case BeTypeCode_Float:
										op = CeOp_Conv_I64_F32;
										break;
									case BeTypeCode_Double:
										op = CeOp_Conv_I64_F64;
										break;
									}
									break;
								}
							}
							
							if (op == CeOp_InvalidOp)
							{
								Fail("Invalid conversion op");
							}
							else
							{
								Emit(op);								
								EmitFrameOffset(result);
								EmitFrameOffset(ceValue);
							}							
						}						
					}
				}
				break;
			case BeStoreInst::TypeId:
				{
					auto castedInst = (BeStoreInst*)inst;
					auto mcVal = GetOperand(castedInst->mVal);
					auto mcPtr = GetOperand(castedInst->mPtr, true);

					if (mcPtr.mKind == CeOperandKind_AllocaAddr)
					{
						EmitSizedOp(CeOp_Move_8, mcVal, NULL, true);
						Emit((int32)mcPtr.mFrameOfs);
					}
					else
					{						
						EmitSizedOp(CeOp_Store_8, mcVal, NULL, true);
						EmitFrameOffset(mcPtr);
					}
				}
				break;
			case BeRetInst::TypeId:
			case BeSetRetInst::TypeId:
				{					
					auto castedInst = (BeRetInst*)inst;
					if (castedInst->mRetValue != NULL)
					{
						auto mcVal = GetOperand(castedInst->mRetValue);
						if (mcVal.mType->mSize > 0)
						{
							BF_ASSERT(mReturnVal.mKind == CeOperandKind_AllocaAddr);
							EmitSizedOp(CeOp_Move_8, mcVal, NULL, true);
							Emit((int32)mReturnVal.mFrameOfs);
						}
					}

					if (instType == BeRetInst::TypeId)
						Emit(CeOp_Ret);
					else
					{
						auto setRetInst = (BeSetRetInst*)inst;
						Emit(CeOp_SetRetType);
						Emit((int32)setRetInst->mReturnTypeId);
					}
				}
				break;			
			case BeCmpInst::TypeId:
				{
					auto castedInst = (BeCmpInst*)inst;
					auto ceLHS = GetOperand(castedInst->mLHS);
					auto ceRHS = GetOperand(castedInst->mRHS);

					CeOp iOp = CeOp_InvalidOp;
					CeOp fOp = CeOp_InvalidOp;

					switch (castedInst->mCmpKind)
					{
					case BeCmpKind_EQ:
						iOp = CeOp_Cmp_EQ_I8;
						fOp = CeOp_Cmp_EQ_F32;
						break;
					case BeCmpKind_NE:
						iOp = CeOp_Cmp_NE_I8;
						fOp = CeOp_Cmp_NE_F32;
						break;
						
					case BeCmpKind_SLT:
						iOp = CeOp_Cmp_SLT_I8;
						fOp = CeOp_Cmp_SLT_F32;
						break;
					case BeCmpKind_ULT:
						iOp = CeOp_Cmp_ULT_I8;
						break;
					case BeCmpKind_SLE:
						iOp = CeOp_Cmp_SLE_I8;
						fOp = CeOp_Cmp_SLE_F32;
						break;
					case BeCmpKind_ULE:
						iOp = CeOp_Cmp_ULE_I8;
						break;

					case BeCmpKind_SGT:
						iOp = CeOp_Cmp_SGT_I8;
						fOp = CeOp_Cmp_SGT_F32;
						break;
					case BeCmpKind_UGT:
						iOp = CeOp_Cmp_UGT_I8;
						break;
					case BeCmpKind_SGE:
						iOp = CeOp_Cmp_SGE_I8;
						fOp = CeOp_Cmp_SGE_F32;
						break;
					case BeCmpKind_UGE:
						iOp = CeOp_Cmp_UGE_I8;
						break;
					}

					if (iOp == CeOp_InvalidOp)
					{
						Fail("Invalid cmp");
						break;
					}

					auto boolType = inst->GetType();
					result = FrameAlloc(boolType);
					EmitBinaryOp(iOp, fOp, ceLHS, ceRHS, result);

// 					auto mcInst = AllocInst(BeMCInstKind_Cmp, mcLHS, mcRHS);
// 
// 					auto cmpResultIdx = (int)mCmpResults.size();
// 					BeCmpResult cmpResult;
// 					cmpResult.mCmpKind = castedInst->mCmpKind;
// 					mCmpResults.push_back(cmpResult);
// 
// 					result.mKind = BeMCOperandKind_CmpResult;
// 					result.mCmpResultIdx = cmpResultIdx;
// 
// 					mcInst->mResult = result;
				}
				break;
			case BeGEPInst::TypeId:
				{
					auto castedInst = (BeGEPInst*)inst;

					auto ceVal = GetOperand(castedInst->mPtr);
					auto ceIdx0 = GetOperand(castedInst->mIdx0, false, true);

					BePointerType* ptrType = (BePointerType*)ceVal.mType;
					BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

					result = ceVal;
					if (castedInst->mIdx1 != NULL)
					{
						// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
						BF_ASSERT(castedInst->mIdx0);

						auto ceIdx1 = GetOperand(castedInst->mIdx1, false, true);
						if (!ceIdx1.IsImmediate())
						{
							// This path is used when we have a const array that gets indexed by a non-const index value
							if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
							{
								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;

								auto elementPtrType = beModule->mContext->GetPointerTo(arrayType->mElementType);
								
								if (ceIdx1.IsImmediate())
								{
									if (ceIdx1.mImmediate == 0)
									{
										result = ceVal;
										result.mType = elementPtrType;
									}
									else
									{
										auto ptrValue = FrameAlloc(elementPtrType);
										result = ptrValue;

										result = FrameAlloc(elementPtrType);
										EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
										EmitFrameOffset(result);
										EmitFrameOffset(ceVal);
										Emit((int32)(ceIdx1.mImmediate * arrayType->mElementType->GetStride()));
										if (mPtrSize == 8)
											Emit((int32)0);
									}
								}
								else
								{
									auto ptrValue = FrameAlloc(elementPtrType);
									result = ptrValue;

									if (ceIdx1.mType->mSize < 4)
									{
										auto ceNewIdx = FrameAlloc(mIntPtrType);
										if (mIntPtrType->mSize == 8)
										{
											if (ceIdx1.mType->mSize == 1)
												Emit(CeOp_Conv_I8_I64);
											else
												Emit(CeOp_Conv_I16_I64);
										}
										else
										{
											if (ceIdx1.mType->mSize == 1)
												Emit(CeOp_Conv_I8_I32);
											else
												Emit(CeOp_Conv_I16_I32);
										}
										EmitFrameOffset(ceNewIdx);
										EmitFrameOffset(ceIdx1);

										ceIdx1 = ceNewIdx;
									}

									result = FrameAlloc(elementPtrType);

									if (mPtrSize == 4)
									{
										auto mcElementSize = FrameAlloc(mIntPtrType);
										Emit(CeOp_Const_32);
										EmitFrameOffset(mcElementSize);
										Emit((int32)arrayType->mElementType->GetStride());

										auto ofsValue = FrameAlloc(mIntPtrType);
										Emit(CeOp_Mul_I32);
										EmitFrameOffset(ofsValue);
										EmitFrameOffset(ceIdx1);
										EmitFrameOffset(mcElementSize);

										Emit(CeOp_Add_I32);
										EmitFrameOffset(result);
										EmitFrameOffset(ceVal);
										EmitFrameOffset(ofsValue);
									}
									else
									{										
										auto mcElementSize = FrameAlloc(mIntPtrType);
										Emit(CeOp_Const_64);
										EmitFrameOffset(mcElementSize);
										Emit((int64)arrayType->mElementType->GetStride());

										auto ofsValue = FrameAlloc(mIntPtrType);
										Emit(CeOp_Mul_I64);
										EmitFrameOffset(ofsValue);
										EmitFrameOffset(ceIdx1);
										EmitFrameOffset(mcElementSize);
										
										Emit(CeOp_Add_I64);
										EmitFrameOffset(result);
										EmitFrameOffset(ceVal);
										EmitFrameOffset(ofsValue);
									}									
								}
 							}
 							else
 								Fail("Invalid GEP");							
						}
						else
						{
							BF_ASSERT(ceIdx1.IsImmediate());
							int byteOffset = 0;
							BeType* elementType = NULL;
							if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
							{
								BeStructType* structType = (BeStructType*)ptrType->mElementType;
								auto& structMember = structType->mMembers[ceIdx1.mImmediate];
								elementType = structMember.mType;
								byteOffset = structMember.mByteOffset;
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
							{
								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = ceIdx1.mImmediate * elementType->GetStride();
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_Vector)
							{
								auto arrayType = (BeVectorType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = ceIdx1.mImmediate * elementType->GetStride();
							}
							else
							{
								Fail("Invalid gep target");
							}
							
							auto elementPtrType = beModule->mContext->GetPointerTo(elementType);

							if (byteOffset != 0)
							{
								result = FrameAlloc(elementPtrType);
								EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								Emit((int32)byteOffset);
								if (mPtrSize == 8)
									Emit((int32)0);
							}
							else
							{
								result.mType = elementPtrType;
							}
						}
					}
					else
					{						
 						CeOperand mcRelOffset;
 						int relScale = 1;
 						if (ceIdx0.IsImmediate())
						{
							int byteOffset = ceIdx0.mImmediate * ptrType->mElementType->GetStride();
							if (byteOffset != 0)
							{
								result = FrameAlloc(ptrType);
								EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								Emit((int32)byteOffset);
								if (mPtrSize == 8)
									Emit((int32)0);
							}							
						}
						else
						{
							result = FrameAlloc(ptrType);
							if (mPtrSize == 4)
							{
								auto mcElementSize = FrameAlloc(mIntPtrType);
								Emit(CeOp_Const_32);
								EmitFrameOffset(mcElementSize);
								Emit((int32)ptrType->mElementType->GetStride());

								auto ofsValue = FrameAlloc(mIntPtrType);
								Emit(CeOp_Mul_I32);
								EmitFrameOffset(ofsValue);
								EmitFrameOffset(ceIdx0);
								EmitFrameOffset(mcElementSize);

								Emit(CeOp_Add_I32);
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								EmitFrameOffset(ofsValue);
							}
							else
							{
								auto mcElementSize = FrameAlloc(mIntPtrType);
								Emit(CeOp_Const_64);
								EmitFrameOffset(mcElementSize);
								Emit((int64)ptrType->mElementType->GetStride());

								auto ofsValue = FrameAlloc(mIntPtrType);
								Emit(CeOp_Mul_I64);
								EmitFrameOffset(ofsValue);
								EmitFrameOffset(ceIdx0);
								EmitFrameOffset(mcElementSize);

								Emit(CeOp_Add_I64);
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								EmitFrameOffset(ofsValue);
							}
						}						
					}
				}
				break;
			case BeExtractValueInst::TypeId:
				{
					auto castedInst = (BeExtractValueInst*)inst;
					
					BeConstant* constant = BeValueDynCast<BeConstant>(castedInst->mAggVal);
					CeOperand mcAgg;
					
					if (constant != NULL)
					{
						result.mImmediate = 0;
						BeType* wantDefaultType = NULL;
						if (constant->mType->IsStruct())
						{
							BeStructType* structType = (BeStructType*)constant->mType;
							auto& member = structType->mMembers[castedInst->mIdx];
							wantDefaultType = member.mType;
						}
						else if (constant->mType->IsSizedArray())
						{
							BeSizedArrayType* arrayType = (BeSizedArrayType*)constant->mType;
							wantDefaultType = arrayType->mElementType;
						}

						if (wantDefaultType != NULL)
						{
// 							switch (wantDefaultType->mTypeCode)
// 							{
// 							case BeTypeCode_Boolean:
// 							case BeTypeCode_Int8:
// 								result.mKind = BeMCOperandKind_Immediate_i8;
// 								break;
// 							case BeTypeCode_Int16:
// 								result.mKind = BeMCOperandKind_Immediate_i16;
// 								break;
// 							case BeTypeCode_Int32:
// 								result.mKind = BeMCOperandKind_Immediate_i32;
// 								break;
// 							case BeTypeCode_Int64:
// 								result.mKind = BeMCOperandKind_Immediate_i64;
// 								break;
// 							case BeTypeCode_Float:
// 								result.mKind = BeMCOperandKind_Immediate_f32;
// 								break;
// 							case BeTypeCode_Double:
// 								result.mKind = BeMCOperandKind_Immediate_f64;
// 								break;
// 							case BeTypeCode_Pointer:
// 								result.mKind = BeMCOperandKind_Immediate_Null;
// 								result.mType = wantDefaultType;
// 								break;
// 							case BeTypeCode_Struct:
// 							case BeTypeCode_SizedArray:
// 								{
// 									auto subConst = mAlloc.Alloc<BeConstant>();
// 									subConst->mType = wantDefaultType;
// 									result.mConstant = subConst;
// 									result.mKind = BeMCOperandKind_ConstAgg;
// 								}
// 								break;
// 							default:
// 								NotImpl();
// 							}
							Fail("Unhandled extract");
						}

						break;
					}
					else
					{
						mcAgg = GetOperand(castedInst->mAggVal);
					}

					auto aggType = mcAgg.mType;
					int byteOffset = 0;
					BeType* memberType = NULL;

					if (aggType->IsSizedArray())
					{
						auto sizedArray = (BeSizedArrayType*)aggType;
						memberType = sizedArray->mElementType;
						byteOffset = memberType->GetStride() * castedInst->mIdx;
					}
					else
					{
						BF_ASSERT(aggType->IsStruct());
						BeStructType* structType = (BeStructType*)aggType;
						auto& structMember = structType->mMembers[castedInst->mIdx];
						byteOffset = structMember.mByteOffset;
						memberType = structMember.mType;
					}
					
					if (byteOffset != 0)
					{
						auto ptrVal = FrameAlloc(beModule->mContext->GetPrimitiveType(BeTypeCode_Int32));
						Emit(CeOp_FrameAddrOfs_32);
						EmitFrameOffset(ptrVal);
						EmitFrameOffset(mcAgg);
						Emit((int32)byteOffset);

 						result = FrameAlloc(memberType);
						EmitSizedOp(CeOp_Load_8, memberType->mSize);
						EmitFrameOffset(result);
						EmitFrameOffset(ptrVal);
					}
					else
					{
						result = mcAgg;
						result.mType = memberType;
					}
				}
				break;
			case BeBrInst::TypeId:
				{
					auto castedInst = (BeBrInst*)inst;
					auto targetBlock = GetOperand(castedInst->mTargetBlock);
					
					BF_ASSERT(targetBlock.mKind == CeOperandKind_Block);

					FlushPhi(ceBlock, targetBlock.mBlockIdx);

					if (targetBlock.mBlockIdx == blockIdx + 1)
					{
						// Do nothing - just continuing to next block
						break;
					}

					EmitJump(CeOp_Jmp, targetBlock);					
				}
				break;
			case BeCondBrInst::TypeId:
				{
					auto castedInst = (BeCondBrInst*)inst;
					auto testVal = GetOperand(castedInst->mCond, true);
					auto trueBlock = GetOperand(castedInst->mTrueBlock);
					auto falseBlock = GetOperand(castedInst->mFalseBlock);
					
					FlushPhi(ceBlock, trueBlock.mBlockIdx);
					
					EmitJump(CeOp_JmpIf, trueBlock);					
					EmitFrameOffset(testVal);

					FlushPhi(ceBlock, falseBlock.mBlockIdx);

					EmitJump(CeOp_Jmp, falseBlock);					
				}
				break;
			case BePhiInst::TypeId:
				result = GetOperand(inst);
				BF_ASSERT(result);
				break;
			case BeNegInst::TypeId:
				{
					auto castedInst = (BeNegInst*)inst;
					auto ceValue = GetOperand(castedInst->mValue);
					EmitUnaryOp(CeOp_Neg_I8, CeOp_Neg_F32, ceValue, result);
				}
				break;
			case BeNotInst::TypeId:
				{
					auto castedInst = (BeNotInst*)inst;
					auto ceValue = GetOperand(castedInst->mValue);
					if (ceValue.mType->mTypeCode == BeTypeCode_Boolean)
						EmitUnaryOp(CeOp_Not_I1, CeOp_InvalidOp, ceValue, result);
					else
						EmitUnaryOp(CeOp_Not_I8, CeOp_InvalidOp, ceValue, result);
				}
				break;
			case BeSwitchInst::TypeId:
				{
					auto castedInst = (BeSwitchInst*)inst;

					std::stable_sort(castedInst->mCases.begin(), castedInst->mCases.end(), [&](const BeSwitchCase& lhs, const BeSwitchCase& rhs)
						{
							return lhs.mValue->mInt64 < rhs.mValue->mInt64;
						});

					int numVals = castedInst->mCases.size();

					if (numVals > 0)
					{						
						EmitBinarySwitchSection(castedInst, 0, castedInst->mCases.size());
					}

					auto mcDefaultBlock = GetOperand(castedInst->mDefaultBlock);
					EmitJump(CeOp_Jmp, mcDefaultBlock);					
				}
				break;
			case BeCallInst::TypeId:
				{
					auto castedInst = (BeCallInst*)inst;					
					BeType* returnType = NULL;
					bool isVarArg = false;
					bool useAltArgs = false;
					
					CeOperand ceFunc;
					BeFunctionType* beFuncType = NULL;
					CeOperand virtTarget;
					int ifaceTypeId = -1;
					int virtualTableIdx = -1;

					if (auto intrin = BeValueDynCast<BeIntrinsic>(castedInst->mFunc))
					{
						switch (intrin->mKind)
						{
						case BfIRIntrinsic_Abs:													
							EmitUnaryOp(CeOp_Abs_I8, CeOp_Abs_F32, GetOperand(castedInst->mArgs[0].mValue), result);							
							break;
						case BfIRIntrinsic_Cast:
							{
								result = GetOperand(castedInst->mArgs[0].mValue);
								result.mType = intrin->mReturnType;
							}
							break;
						case BfIRIntrinsic_MemCpy:
							{
								CeOperand ceDestPtr = GetOperand(castedInst->mArgs[0].mValue);
								CeOperand ceSrcPtr = GetOperand(castedInst->mArgs[1].mValue);
								CeOperand ceSize = GetOperand(castedInst->mArgs[2].mValue);

								Emit(CeOp_MemCpy);
								EmitFrameOffset(ceDestPtr);
								EmitFrameOffset(ceSrcPtr);
								EmitFrameOffset(ceSize);
							}
							break;


						case BfIRIntrinsic_AtomicFence:
							// Nothing to do
							break;
						case BfIRIntrinsic_AtomicAdd:
							EmitBinaryOp(CeOp_Add_I8, CeOp_Add_F32, GetOperand(castedInst->mArgs[0].mValue), GetOperand(castedInst->mArgs[1].mValue), result);							
							break;
						case BfIRIntrinsic_AtomicOr:
							EmitBinaryOp(CeOp_Or_I8, CeOp_InvalidOp, GetOperand(castedInst->mArgs[0].mValue), GetOperand(castedInst->mArgs[1].mValue), result);
							break;
						case BfIRIntrinsic_AtomicSub:
							EmitBinaryOp(CeOp_Sub_I8, CeOp_Sub_F32, GetOperand(castedInst->mArgs[0].mValue), GetOperand(castedInst->mArgs[1].mValue), result);
							break;
						case BfIRIntrinsic_AtomicXor:
							EmitBinaryOp(CeOp_Xor_I8, CeOp_InvalidOp, GetOperand(castedInst->mArgs[0].mValue), GetOperand(castedInst->mArgs[1].mValue), result);
							break;

						default:
							Emit(CeOp_Error);
							Emit((int32)CeErrorKind_Intrinsic);
							break;
						}
					}
					else if (auto beFunction = BeValueDynCast<BeFunction>(castedInst->mFunc))
					{
						beFuncType = beFunction->GetFuncType();

						CeFunctionInfo* ceFunctionInfo = NULL;
						mCeMachine->mNamedFunctionMap.TryGetValue(beFunction->mName, &ceFunctionInfo);
						if (ceFunctionInfo != NULL)
						{
							CeOp ceOp = CeOp_InvalidOp;

							if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_NotSet)
								mCeMachine->CheckFunctionKind(ceFunctionInfo->mCeFunction);

							if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Abs)
								ceOp = CeOp_Abs_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Acos)
								ceOp = CeOp_Acos_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Asin)
								ceOp = CeOp_Asin_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Atan)
								ceOp = CeOp_Atan_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Atan2)
								ceOp = CeOp_Atan2_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Ceiling)
								ceOp = CeOp_Ceiling_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Cos)
								ceOp = CeOp_Cos_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Cosh)
								ceOp = CeOp_Cosh_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Exp)
								ceOp = CeOp_Exp_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Floor)
								ceOp = CeOp_Floor_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Log)
								ceOp = CeOp_Log_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Log10)
								ceOp = CeOp_Log10_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Pow)
								ceOp = CeOp_Pow_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Round)
								ceOp = CeOp_Round_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Sin)
								ceOp = CeOp_Sin_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Sinh)
								ceOp = CeOp_Sinh_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Sqrt)
								ceOp = CeOp_Sqrt_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Tan)
								ceOp = CeOp_Tan_F32;
							else if (ceFunctionInfo->mCeFunction->mFunctionKind == CeFunctionKind_Math_Tanh)
								ceOp = CeOp_Tanh_F32;

							if (ceOp != CeOp_InvalidOp)
							{
								if (beFuncType->mReturnType->mSize == 8)
									ceOp = (CeOp)(ceOp + 1);

								result = FrameAlloc(beFuncType->mReturnType);
								if (beFuncType->mParams.size() == 1)
								{
									auto arg0 = GetOperand(castedInst->mArgs[0].mValue);
									Emit(ceOp);
									EmitFrameOffset(result);
									EmitFrameOffset(arg0);
								}
								else
								{
									auto arg0 = GetOperand(castedInst->mArgs[0].mValue);
									auto arg1 = GetOperand(castedInst->mArgs[1].mValue);
									Emit(ceOp);
									EmitFrameOffset(result);
									EmitFrameOffset(arg0);
									EmitFrameOffset(arg1);
								}
								
								break;
							}
						}

						if (beFunction->mName == "malloc")
						{
							result = FrameAlloc(beFuncType->mReturnType);
							auto ceSize = GetOperand(castedInst->mArgs[0].mValue);
							Emit(CeOp_Malloc);
							EmitFrameOffset(result);
							EmitFrameOffset(ceSize);
							break;
						}

						if (beFunction->mName == "free")
						{
							auto cePtr = GetOperand(castedInst->mArgs[0].mValue);
							Emit(CeOp_Free);
							EmitFrameOffset(cePtr);
							break;
						}

						ceFunc = GetOperand(beFunction, false, true);
					}
					else if (auto beGetVirtualFunc = BeValueDynCast<BeComptimeGetVirtualFunc>(castedInst->mFunc))
					{
						virtTarget = GetOperand(beGetVirtualFunc->mValue);
						virtualTableIdx = beGetVirtualFunc->mVirtualTableIdx;
						
						auto resultType = beGetVirtualFunc->GetType();
						BF_ASSERT(resultType->IsPointer());
						beFuncType = (BeFunctionType*)((BePointerType*)resultType)->mElementType;
					}
					else if (auto beGetInterfaceFunc = BeValueDynCast<BeComptimeGetInterfaceFunc>(castedInst->mFunc))
					{
						virtTarget = GetOperand(beGetInterfaceFunc->mValue);
						ifaceTypeId = beGetInterfaceFunc->mIFaceTypeId;
						virtualTableIdx = beGetInterfaceFunc->mMethodIdx;

						auto resultType = beGetInterfaceFunc->GetType();
						BF_ASSERT(resultType->IsPointer());
						beFuncType = (BeFunctionType*)((BePointerType*)resultType)->mElementType;
					}
					else
					{
						ceFunc = GetOperand(castedInst->mFunc, false, true);						
 						auto funcType = castedInst->mFunc->GetType();
 						if (funcType->IsPointer())
 						{
 							auto ptrType = (BePointerType*)funcType;
 							if (ptrType->mElementType->mTypeCode == BeTypeCode_Function)
 							{
 								beFuncType = (BeFunctionType*)ptrType->mElementType; 								
 							}
 						}						
					}					

					if ((ceFunc) || (virtualTableIdx != -1))
					{
						CeOperand thisOperand;

						int stackAdjust = 0;

						for (int argIdx = (int)castedInst->mArgs.size() - 1; argIdx >= 0; argIdx--)
						{
							auto& arg = castedInst->mArgs[argIdx];
							auto ceArg = GetOperand(arg.mValue);
							if (argIdx == 0)
								thisOperand = ceArg;
							EmitSizedOp(CeOp_Push_8, ceArg, NULL, true);
							stackAdjust += ceArg.mType->mSize;
						}

						if (beFuncType->mReturnType->mSize > 0)
						{
							Emit(CeOp_AdjustSPConst);
							Emit((int32)-beFuncType->mReturnType->mSize);							
						}

						if (!ceFunc)
							ceFunc = FrameAlloc(beModule->mContext->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));

						if (ifaceTypeId != -1)
						{
							Emit(CeOp_GetMethod_IFace);
							EmitFrameOffset(ceFunc);
							EmitFrameOffset(thisOperand);
							Emit((int32)ifaceTypeId);
							Emit((int32)virtualTableIdx);
						}
						else if (virtualTableIdx != -1)
						{
							Emit(CeOp_GetMethod_Virt);
							EmitFrameOffset(ceFunc);
							EmitFrameOffset(thisOperand);
							Emit((int32)virtualTableIdx);
						}
						
						if (ceFunc.mKind == CeOperandKind_CallTableIdx)
						{
							CeOperand result = FrameAlloc(mCeMachine->GetBeContext()->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));
							Emit(CeOp_GetMethod);
							EmitFrameOffset(result);
							Emit((int32)ceFunc.mCallTableIdx);
							
							ceFunc = result;
						}

						Emit(CeOp_Call);
						EmitFrameOffset(ceFunc);

						if (beFuncType->mReturnType->mSize > 0)
						{
							result = FrameAlloc(beFuncType->mReturnType);
							EmitSizedOp(CeOp_Pop_8, result, NULL, true);
						}

						if (stackAdjust > 0)
						{
							Emit(CeOp_AdjustSPConst);
							Emit(stackAdjust);
						}
					}
				}
				break;
			case BeMemSetInst::TypeId:
				{
					auto castedInst = (BeMemSetInst*)inst;
					auto ceAddr = GetOperand(castedInst->mAddr);
					
					if (auto constVal = BeValueDynCast<BeConstant>(castedInst->mVal))
					{
						if (auto constSize = BeValueDynCast<BeConstant>(castedInst->mSize))
						{							
							if (constVal->mUInt8 == 0)
							{
								Emit(CeOp_MemSet_Const);
								EmitFrameOffset(ceAddr);
								Emit((uint8)0);
								Emit((int32)constSize->mUInt32);
								break;
							}							
						}
					}

					auto ceVal = GetOperand(castedInst->mVal);
					auto ceSize = GetOperand(castedInst->mSize);

					Emit(CeOp_MemSet);
					EmitFrameOffset(ceAddr);
					EmitFrameOffset(ceVal);
					EmitFrameOffset(ceSize);					
				}
				break;
			case BeFenceInst::TypeId:				
				break;
			case BeStackSaveInst::TypeId:
				{
					result = FrameAlloc(mIntPtrType);
					Emit(CeOp_GetSP);
					EmitFrameOffset(result);					
				}
				break;
			case BeStackRestoreInst::TypeId:
				{
					auto castedInst = (BeStackRestoreInst*)inst;

					auto mcStackVal = GetOperand(castedInst->mStackVal);
					Emit(CeOp_SetSP);
					EmitFrameOffset(mcStackVal);
				}
				break;
			case BeComptimeError::TypeId:
				{
					auto castedInst = (BeComptimeError*)inst;
					Emit(CeOp_Error);
					Emit(castedInst->mError);
				}
				break;
			case BeComptimeGetType::TypeId:
				{
					auto castedInst = (BeComptimeGetType*)inst;
					result.mKind = CeOperandKind_Immediate;
					result.mImmediate = castedInst->mTypeId;
					result.mType = beModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
				}
				break;
			case BeComptimeGetReflectType::TypeId:
				{
					auto castedInst = (BeComptimeGetReflectType*)inst;
					auto ptrType = beModule->mContext->GetVoidPtrType();
					result = FrameAlloc(ptrType);

					Emit(CeOp_GetReflectType);
					EmitFrameOffset(result);					
					Emit((int32)castedInst->mTypeId);
				}
				break;
			case BeComptimeDynamicCastCheck::TypeId:
				{
					auto castedInst = (BeComptimeDynamicCastCheck*)inst;
					auto mcValue = GetOperand(castedInst->mValue);

					auto ptrType = beModule->mContext->GetVoidPtrType();
					result = FrameAlloc(ptrType);

					Emit(CeOp_DynamicCastCheck);
					EmitFrameOffset(result);
					EmitFrameOffset(mcValue);
					Emit((int32)castedInst->mTypeId);
				}
				break;
			default:
				Fail("Unhandled instruction");
				return;
			}

			if (result.mKind != CeOperandKind_None)
				mValueToOperand[inst] = result;

			if ((startCodePos != GetCodePos()) && (prevEmitDbgPos != mCurDbgLoc))
			{				
				prevEmitDbgPos = mCurDbgLoc;

				int fileIdx = -1;
				BeDbgFile* dbgFile = NULL;
				if (mCurDbgLoc != NULL)
				{
					auto dbgFile = mCurDbgLoc->GetDbgFile();
					int* valuePtr = NULL;
					if (mDbgFileMap.TryAdd(dbgFile, NULL, &valuePtr))
					{
						fileIdx = (int)mCeFunction->mFiles.size();
						String filePath = dbgFile->mDirectory;
						filePath.Append(DIR_SEP_CHAR);
						filePath += dbgFile->mFileName;
						mCeFunction->mFiles.Add(filePath);
						*valuePtr = fileIdx;
					}
					else
						fileIdx = *valuePtr;
				}

				CeEmitEntry emitEntry;
				emitEntry.mCodePos = startCodePos;
				emitEntry.mFile = fileIdx;
				if (mCurDbgLoc != NULL)
				{
					emitEntry.mLine = mCurDbgLoc->mLine;
					emitEntry.mColumn = mCurDbgLoc->mColumn;
				}
				else
				{
					emitEntry.mLine = -1;
					emitEntry.mColumn = -1;
				}
				mCeFunction->mEmitTable.Add(emitEntry);				
			}
		}
	}

	for (auto& jumpEntry : mJumpTable)
	{
		auto& ceBlock = mBlocks[jumpEntry.mBlockIdx];
		*((int32*)(&mCeFunction->mCode[0] + jumpEntry.mEmitPos)) = ceBlock.mEmitOfs - jumpEntry.mEmitPos - 4;
	}

	if (mCeFunction->mCode.size() == 0)
	{
		Fail("No method definition available");
		return;
	}

	if (mCeFunction->mGenError.IsEmpty())
		mCeFunction->mFailed = false;
	mCeFunction->mFrameSize = mFrameSize;	
}

//////////////////////////////////////////////////////////////////////////

CeContext::CeContext()
{
	mCurEvalFlags = CeEvalFlags_None;
	mCeMachine = NULL;
	mReflectTypeIdOffset = -1;
	mExecuteId = -1;

	mCurTargetSrc = NULL;	
	mHeap = new	ContiguousHeap();
	mCurFrame = NULL;
	mCurModule = NULL;
	mCurMethodInstance = NULL;
	mCurExpectingType = NULL;
	mCurEmitContext = NULL;
}

CeContext::~CeContext()
{
	delete mHeap;
}

BfError* CeContext::Fail(const StringImpl& error)
{
	auto bfError = mCurModule->Fail(StrFormat("Unable to comptime %s", mCurModule->MethodToString(mCurMethodInstance).c_str()), mCurTargetSrc, (mCurEvalFlags & CeEvalFlags_PersistantError) != 0);
	if (bfError == NULL)
		return NULL;
	mCeMachine->mCompiler->mPassInstance->MoreInfo(error, mCeMachine->mCompiler->GetAutoComplete() != NULL);
	return bfError;
}

BfError* CeContext::Fail(const CeFrame& curFrame, const StringImpl& str)
{
	auto bfError = mCurModule->Fail(StrFormat("Unable to comptime %s", mCurModule->MethodToString(mCurMethodInstance).c_str()), mCurTargetSrc, 
		(mCurEvalFlags & CeEvalFlags_PersistantError) != 0,
		((mCurEvalFlags & CeEvalFlags_DeferIfNotOnlyError) != 0) && !mCurModule->mHadBuildError);
	if (bfError == NULL)
		return NULL;
	
	auto passInstance = mCeMachine->mCompiler->mPassInstance;
	
	for (int stackIdx = mCallStack.size(); stackIdx >= 0; stackIdx--)
	{
		bool isHeadEntry = stackIdx == mCallStack.size();
		auto* ceFrame = (isHeadEntry) ? &curFrame : &mCallStack[stackIdx];

		auto ceFunction = ceFrame->mFunction;		

		int i = 0;
		CeEmitEntry* emitEntry = NULL;

		if (!ceFunction->mCode.IsEmpty())
		{
			int lo = 0;
			int hi = ceFunction->mEmitTable.size() - 1;
			int instIdx = ceFrame->mInstPtr - &ceFunction->mCode[0] - 1;
			while (lo <= hi)
			{
				i = (lo + hi) / 2;
				emitEntry = &ceFunction->mEmitTable.mVals[i];
				//int c = midVal <=> value;
				if (emitEntry->mCodePos == instIdx) break;
				if (emitEntry->mCodePos < instIdx)
					lo = i + 1;
				else
					hi = i - 1;
			}
			if ((emitEntry != NULL) && (emitEntry->mCodePos > instIdx) && (i > 0))
				emitEntry = &ceFunction->mEmitTable.mVals[i - 1];
		}

		StringT<256> err;
		if (isHeadEntry)
		{
			err = str;
			err += " ";
		}
				
		auto contextMethodInstance = mCurModule->mCurMethodInstance;
		if (stackIdx > 1)
		{
			auto func = mCallStack[stackIdx - 1].mFunction;
			contextMethodInstance = func->mCeFunctionInfo->mMethodInstance;			
		}

		err += StrFormat("in comptime ");
		
		//
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCeMachine->mCeModule->mCurTypeInstance, (contextMethodInstance != NULL) ? contextMethodInstance->GetOwner() : NULL);
			SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCeMachine->mCeModule->mCurMethodInstance, contextMethodInstance);

			if (ceFunction->mMethodInstance != NULL)
				err += mCeMachine->mCeModule->MethodToString(ceFunction->mMethodInstance, BfMethodNameFlag_OmitParams);
			else
			{
				err += mCeMachine->mCeModule->MethodToString(ceFunction->mCeInnerFunctionInfo->mOwner->mMethodInstance, BfMethodNameFlag_OmitParams);
			}
		}
		 
		if ((emitEntry != NULL) && (emitEntry->mFile != -1))
			err += StrFormat(" at line% d:%d in %s", emitEntry->mLine + 1, emitEntry->mColumn + 1, ceFunction->mFiles[emitEntry->mFile].c_str());

		auto moreInfo = passInstance->MoreInfo(err, mCeMachine->mCeModule->mCompiler->GetAutoComplete() != NULL);
		if ((moreInfo != NULL) && (emitEntry != NULL))
		{
			BfErrorLocation* location = new BfErrorLocation();
			location->mFile = ceFunction->mFiles[emitEntry->mFile];
			location->mLine = emitEntry->mLine;
			location->mColumn = emitEntry->mColumn;
			moreInfo->mLocation = location;
		}		
	}

	return bfError;
}

//////////////////////////////////////////////////////////////////////////


uint8* CeContext::CeMalloc(int size)
{
#ifdef CE_ENABLE_HEAP
	auto heapRef = mHeap->Alloc(size);	
	auto ceAddr = BF_CE_STACK_SIZE + heapRef;
	int sizeDelta = (ceAddr + size) - mMemory.mSize;
	if (sizeDelta > 0)
		mMemory.GrowUninitialized(sizeDelta);
	return mMemory.mVals + ceAddr;
#else
	return mMemory.GrowUninitialized(size);
#endif
}

bool CeContext::CeFree(addr_ce addr)
{
#ifdef CE_ENABLE_HEAP
	ContiguousHeap::AllocRef heapRef = addr - BF_CE_STACK_SIZE;
	return mHeap->Free(heapRef);
#else
	return true;
#endif
}

addr_ce CeContext::CeAllocArray(BfArrayType* arrayType, int count, addr_ce& elemsAddr)
{
	mCeMachine->mCeModule->PopulateType(arrayType);

	BfType* elemType = arrayType->GetUnderlyingType();
	auto countOffset = arrayType->mBaseType->mFieldInstances[0].mDataOffset;
	auto elemOffset = arrayType->mFieldInstances[0].mDataOffset;	

	int allocSize = elemOffset + elemType->GetStride() * count;

	uint8* mem = CeMalloc(allocSize);

	memset(mem, 0, allocSize);

	*(int32*)(mem) = arrayType->mTypeId;
	*(int32*)(mem + countOffset) = count;

	elemsAddr = (addr_ce)(mem + elemOffset - mMemory.mVals);

	return (addr_ce)(mem - mMemory.mVals);
}

addr_ce CeContext::GetConstantData(BeConstant* constant)
{
	auto writeConstant = constant;
	if (auto gvConstant = BeValueDynCast<BeGlobalVariable>(writeConstant))
	{
		if (gvConstant->mInitializer != NULL)
			writeConstant = gvConstant->mInitializer;
	}

	CeConstStructData structData;
	auto result = mCeMachine->WriteConstant(structData, writeConstant, this);
	BF_ASSERT(result == CeErrorKind_None);

	uint8* ptr = CeMalloc(structData.mData.mSize);
	memcpy(ptr, structData.mData.mVals, structData.mData.mSize);
	return (addr_ce)(ptr - mMemory.mVals);
}

addr_ce CeContext::GetReflectType(int typeId)
{
	addr_ce* addrPtr = NULL;
	if (!mReflectMap.TryAdd(typeId, NULL, &addrPtr))
		return *addrPtr;

	auto ceModule = mCeMachine->mCeModule;
	SetAndRestoreValue<bool> ignoreWrites(ceModule->mBfIRBuilder->mIgnoreWrites, false);

	if (ceModule->mContext->mBfTypeType == NULL)
		ceModule->mContext->ReflectInit();

	if ((uintptr)typeId >= (uintptr)mCeMachine->mCeModule->mContext->mTypes.mSize)
		return 0;
	auto bfType = mCeMachine->mCeModule->mContext->mTypes[typeId];
	if (bfType == NULL)
		return 0;

	if (bfType->mDefineState != BfTypeDefineState_CETypeInit)
		ceModule->PopulateType(bfType, BfPopulateType_DataAndMethods);

	Dictionary<int, int> usedStringMap;
	auto irData = ceModule->CreateTypeData(bfType, usedStringMap, true, true, true, false);		
	
	BeValue* beValue = NULL;
	if (auto constant = mCeMachine->mCeModule->mBfIRBuilder->GetConstant(irData))
	{
		if (constant->mConstType == BfConstType_BitCast)
		{
			auto bitcast = (BfConstantBitCast*)constant;
			constant = mCeMachine->mCeModule->mBfIRBuilder->GetConstantById(bitcast->mTarget);
		}
		if (constant->mConstType == BfConstType_GlobalVar)
		{
			auto globalVar = (BfGlobalVar*)constant;
			beValue = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen->GetBeValue(globalVar->mStreamId);
		}
	}

	if (auto constant = BeValueDynCast<BeConstant>(beValue))
		*addrPtr = GetConstantData(constant);

	// We need to 'get' again because we might have resized	
	return *addrPtr;
}

addr_ce CeContext::GetReflectType(const String& typeName)
{
	if (mCeMachine->mTempParser == NULL)
	{
		mCeMachine->mTempPassInstance = new	BfPassInstance(mCeMachine->mCompiler->mSystem);
		mCeMachine->mTempParser = new BfParser(mCeMachine->mCompiler->mSystem);
		mCeMachine->mTempParser->mIsEmitted = true;
		mCeMachine->mTempParser->SetSource(NULL, 4096);

		mCeMachine->mTempReducer = new BfReducer();
		mCeMachine->mTempReducer->mPassInstance = mCeMachine->mTempPassInstance;
		mCeMachine->mTempReducer->mSource = mCeMachine->mTempParser;
		mCeMachine->mTempReducer->mAlloc = mCeMachine->mTempParser->mAlloc;
		mCeMachine->mTempReducer->mSystem = mCeMachine->mCompiler->mSystem;
	}

	int copyLen = BF_MIN(typeName.mLength, 4096);
	memcpy((char*)mCeMachine->mTempParser->mSrc, typeName.c_str(), copyLen);
	((char*)mCeMachine->mTempParser->mSrc)[copyLen] = 0;
	mCeMachine->mTempParser->mSrcLength = typeName.mLength;
	mCeMachine->mTempParser->mSrcIdx = 0;
	mCeMachine->mTempParser->Parse(mCeMachine->mTempPassInstance);

	BfType* type = NULL;
	if (mCeMachine->mTempParser->mRootNode->mChildArr.mSize > 0)
	{
		mCeMachine->mTempReducer->mVisitorPos = BfReducer::BfVisitorPos(mCeMachine->mTempParser->mRootNode);
		mCeMachine->mTempReducer->mVisitorPos.mReadPos = 0;
		auto typeRef = mCeMachine->mTempReducer->CreateTypeRef(mCeMachine->mTempParser->mRootNode->mChildArr[0]);
		if ((mCeMachine->mTempPassInstance->mErrors.mSize == 0) && (mCeMachine->mTempReducer->mVisitorPos.mReadPos == mCeMachine->mTempParser->mRootNode->mChildArr.mSize - 1))
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mCeMachine->mCeModule->mIgnoreErrors, true);
			type = mCeMachine->mCeModule->ResolveTypeRefAllowUnboundGenerics(typeRef, BfPopulateType_Identity);
		}
	}

	mCeMachine->mTempPassInstance->ClearErrors();

	if (type == NULL)
		return 0;
	return GetReflectType(type->mTypeId);
}

int CeContext::GetTypeIdFromType(addr_ce typeAddr)
{
	if (!CheckMemory(typeAddr, 8))
		return 0;

	if (mReflectTypeIdOffset == -1)
	{
		auto typeTypeInst = mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mTypeTypeDef)->ToTypeInstance();
		auto typeIdField = typeTypeInst->mTypeDef->GetFieldByName("mTypeId");
		mReflectTypeIdOffset = typeTypeInst->mFieldInstances[typeIdField->mIdx].mDataOffset;
	}

	return *(int32*)(mMemory.mVals + typeAddr + mReflectTypeIdOffset);
}

addr_ce CeContext::GetReflectSpecializedType(addr_ce unspecializedTypeAddr, addr_ce typeArgsSpanAddr)
{
	BfType* unspecializedType = GetBfType(GetTypeIdFromType(unspecializedTypeAddr));
	if (unspecializedType == NULL)
		return 0;

	BfTypeInstance* unspecializedTypeInst = unspecializedType->ToGenericTypeInstance();
	if (unspecializedType == NULL)
		return 0;

	int ptrSize = mCeMachine->mCompiler->mSystem->mPtrSize;
	if (!CheckMemory(typeArgsSpanAddr, ptrSize * 2))
		return 0;
	addr_ce spanPtr = *(addr_ce*)(mMemory.mVals + typeArgsSpanAddr);
	int32 spanSize = *(int32*)(mMemory.mVals + typeArgsSpanAddr + ptrSize);
	if (spanSize < 0)
		return 0;
	if (!CheckMemory(spanPtr, spanSize * ptrSize))
		return 0;

	Array<BfType*> typeGenericArgs;
	for (int argIdx = 0; argIdx < spanSize; argIdx++)
	{
		addr_ce argPtr = *(addr_ce*)(mMemory.mVals + spanPtr + argIdx * ptrSize);
		BfType* typeGenericArg = GetBfType(GetTypeIdFromType(argPtr));
		if (typeGenericArg == NULL)
			return 0;
		typeGenericArgs.Add(typeGenericArg);
	}

	SetAndRestoreValue<bool> prevIgnoreErrors(mCeMachine->mCeModule->mIgnoreErrors, true);
	auto specializedType = mCeMachine->mCeModule->ResolveTypeDef(unspecializedTypeInst->mTypeDef, typeGenericArgs, BfPopulateType_Identity);
	if (specializedType == NULL)
		return 0;

	return GetReflectType(specializedType->mTypeId);
}

addr_ce CeContext::GetString(int stringId)
{	
	addr_ce* ceAddrPtr = NULL;
	if (!mStringMap.TryAdd(stringId, NULL, &ceAddrPtr))
		return *ceAddrPtr;
	
	BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);

	String str;
	BfStringPoolEntry* entry = NULL;
	if (mCeMachine->mCeModule->mContext->mStringObjectIdMap.TryGetValue(stringId, &entry))
	{
		entry->mLastUsedRevision = mCeMachine->mCompiler->mRevision;
		str = entry->mString;
	}

	int allocSize = stringTypeInst->mInstSize + (int)str.length() + 1;
	int charsOffset = stringTypeInst->mInstSize;	
		
	uint8* mem = CeMalloc(allocSize);	

	memset(mem, 0, allocSize);

	auto lenByteCount = stringTypeInst->mFieldInstances[0].mResolvedType->mSize;
	auto lenOffset = stringTypeInst->mFieldInstances[0].mDataOffset;
	auto allocSizeOffset = stringTypeInst->mFieldInstances[1].mDataOffset;
	auto ptrOffset = stringTypeInst->mFieldInstances[2].mDataOffset;

	// Write TypeId into there
	*(int32*)(mem) = stringTypeInst->mTypeId;
	*(int32*)(mem + lenOffset) = (int)str.length();
	if (lenByteCount == 4)
		*(int32*)(mem + allocSizeOffset) = 0x40000000 + (int)str.length() + 1;
	else
		*(int64*)(mem + allocSizeOffset) = 0x4000000000000000LL + (int)str.length() + 1;
	*(int32*)(mem + ptrOffset) = (mem + charsOffset) - mMemory.mVals;
	memcpy(mem + charsOffset, str.c_str(), str.length());

	*ceAddrPtr = mem - mMemory.mVals;
	return *ceAddrPtr;
}

addr_ce CeContext::GetString(const StringImpl& str)
{
	int stringId = mCeMachine->mCeModule->mContext->GetStringLiteralId(str);
	return GetString(stringId);
}

BfType* CeContext::GetBfType(int typeId)
{
	if ((uintptr)typeId < (uintptr)mCeMachine->mCeModule->mContext->mTypes.size())
		return mCeMachine->mCeModule->mContext->mTypes[typeId];
	return NULL;
}

void CeContext::PrepareConstStructEntry(CeConstStructData& constEntry)
{
	if (constEntry.mHash.IsZero())
	{
		constEntry.mHash = Hash128(&constEntry.mData[0], constEntry.mData.mSize);
		if (!constEntry.mFixups.IsEmpty())		
			constEntry.mHash = Hash128(&constEntry.mFixups[0], constEntry.mFixups.mSize * sizeof(CeConstStructFixup), constEntry.mHash);
	}

	if (!constEntry.mFixups.IsEmpty())
	{
		if (constEntry.mFixedData.IsEmpty())
			constEntry.mFixedData = constEntry.mData;

		for (auto& fixup : constEntry.mFixups)
		{
			if (fixup.mKind == CeConstStructFixup::Kind_StringPtr)
			{
				BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);
				addr_ce addrPtr = GetString(fixup.mValue);
				*(addr_ce*)(constEntry.mFixedData.mVals + fixup.mOffset) = addrPtr;
			}
			else if (fixup.mKind == CeConstStructFixup::Kind_StringCharPtr)
			{
				BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);
				addr_ce addrPtr = GetString(fixup.mValue);
				*(addr_ce*)(constEntry.mFixedData.mVals + fixup.mOffset) = addrPtr + stringTypeInst->mInstSize;
			}
		}
	}	

	constEntry.mBindExecuteId = mExecuteId;
}

bool CeContext::CheckMemory(addr_ce addr, int32 size)
{
	if (((addr)-0x10000) + (size) > (mMemory.mSize - 0x10000))
		return false;
	return true;
}

bool CeContext::GetStringFromStringView(addr_ce addr, StringImpl& str)
{
	int ptrSize = mCeMachine->mCeModule->mSystem->mPtrSize;
	if (!CheckMemory(addr, ptrSize * 2))
		return false;

	addr_ce charsPtr = *(addr_ce*)(mMemory.mVals + addr);
	int32 len = *(int32*)(mMemory.mVals + addr + ptrSize);

	if (!CheckMemory(charsPtr, len))
		return false;

	str.Append((const char*)(mMemory.mVals + charsPtr), len);

	return true;
}

bool CeContext::GetCustomAttribute(BfCustomAttributes* customAttributes, int attributeTypeId, addr_ce resultAddr)
{
	BfType* attributeType = GetBfType(attributeTypeId);
	if (attributeType == NULL)
		return false;

	auto customAttr = customAttributes->Get(attributeType);
	if (customAttr == NULL)
		return false;

	if (resultAddr != 0)
	{
		
	}

	return true;
}



//#define CE_GETC(T) *((T*)(addr += sizeof(T)) - 1)
#define CE_GETC(T) *(T*)(mMemory.mVals + addr)

bool CeContext::WriteConstant(BfModule* module, addr_ce addr, BfConstant* constant, BfType* type, bool isParams)
{	
	switch (constant->mTypeCode)
	{
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Boolean:
	case BfTypeCode_Char8:
		CE_GETC(int8) = constant->mInt8;
		return true;
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		CE_GETC(int16) = constant->mInt16;
		return true;
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		CE_GETC(int32) = constant->mInt32;
		return true;
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:
		CE_GETC(int64) = constant->mInt64;
		return true;
	case BfTypeCode_NullPtr:
		if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
			CE_GETC(int32) = 0;
		else
			CE_GETC(int64) = 0;
		return true;
	case BfTypeCode_Float:
		CE_GETC(float) = (float)constant->mDouble;
		return true;
	case BfTypeCode_Double:
		CE_GETC(double) = constant->mDouble;
		return true;
	}

	if (constant->mConstType == BfConstType_Agg)
	{
		auto aggConstant = (BfConstantAgg*)constant;
		if (type->IsSizedArray())
		{
			return false;
		}
		else if (type->IsArray())
		{
			auto elemType = type->GetUnderlyingType();

			addr_ce elemsAddr = 0;
			addr_ce arrayAddr = CeAllocArray((BfArrayType*)type, aggConstant->mValues.size(), elemsAddr);			

			for (int i = 0; i < (int)aggConstant->mValues.size(); i++)
			{
				auto fieldConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[i]);
				if (fieldConstant == NULL)
					return false;
				if (!WriteConstant(module, elemsAddr + i * elemType->GetStride(), fieldConstant, elemType))
					return false;
			}
			
			if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
				CE_GETC(int32) = arrayAddr;
			else
				CE_GETC(int64) = arrayAddr;

			return true;
		}		
		else if ((type->IsInstanceOf(module->mCompiler->mSpanTypeDef)) && (isParams))
		{
			auto elemType = type->GetUnderlyingType();
			addr_ce elemsAddr = CeMalloc(elemType->GetStride() * aggConstant->mValues.size()) - mMemory.mVals;

			for (int i = 0; i < (int)aggConstant->mValues.size(); i++)
			{
				auto fieldConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[i]);
				if (fieldConstant == NULL)
					return false;
				if (!WriteConstant(module, elemsAddr + i * elemType->GetStride(), fieldConstant, elemType))
					return false;
			}

			if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
			{
				CE_GETC(int32) = elemsAddr;
				addr += 4;
				CE_GETC(int32) = (int32)aggConstant->mValues.size();
			}
			else
			{
				CE_GETC(int32) = elemsAddr;
				addr += 8;
				CE_GETC(int64) = (int32)aggConstant->mValues.size();
			}
		}
		else
		{			
			BF_ASSERT(type->IsStruct());

			module->PopulateType(type);
			auto typeInst = type->ToTypeInstance();
			int idx = 0;

			if (typeInst->mBaseType != NULL)
			{
				auto baseConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[0]);
				if (!WriteConstant(module, addr, baseConstant, typeInst->mBaseType))
					return false;
			}

			for (auto& fieldInstance : typeInst->mFieldInstances)
			{
				if (fieldInstance.mDataOffset < 0)
					continue;

				auto fieldConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[fieldInstance.mDataIdx]);
				if (fieldConstant == NULL)
					return false;
				if (!WriteConstant(module, addr + fieldInstance.mDataOffset, fieldConstant, fieldInstance.mResolvedType))
					return false;
			}
		}
		return true;
	}

	if (constant->mConstType == BfConstType_AggZero)
	{		
		BF_ASSERT(type->IsComposite());
		memset(mMemory.mVals + addr, 0, type->mSize);		
		return true;
	}

	if (constant->mConstType == BfConstType_AggCE)
	{
		auto constAggData = (BfConstantAggCE*)constant;

		if (type->IsPointer())
		{						
			if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
				CE_GETC(int32) = constAggData->mCEAddr;
			else
				CE_GETC(int64) = constAggData->mCEAddr;
		}
		else
		{
			BF_ASSERT(type->IsComposite());			
			memcpy(mMemory.mVals + addr, mMemory.mVals + constAggData->mCEAddr, type->mSize);
		}
		return true;
	}

	if (constant->mConstType == BfConstType_BitCast)
	{
		auto constBitCast = (BfConstantBitCast*)constant;

		auto constTarget = module->mBfIRBuilder->GetConstantById(constBitCast->mTarget);
		return WriteConstant(module, addr, constTarget, type);
	}
	
	if (constant->mConstType == BfConstType_GEP32_2)
	{
		auto gepConst = (BfConstantGEP32_2*)constant;
		auto constTarget = module->mBfIRBuilder->GetConstantById(gepConst->mTarget);
		if (constTarget->mConstType == BfConstType_GlobalVar)
		{
			auto globalVar = (BfGlobalVar*)constTarget;
			if (strncmp(globalVar->mName, "__bfStrData", 10) == 0)
			{
				BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);

				int stringId = atoi(globalVar->mName + 11);
				addr_ce strAddr = GetString(stringId) + stringTypeInst->mInstSize;
				if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
					CE_GETC(int32) = strAddr;
				else
					CE_GETC(int64) = strAddr;
				return true;
			}
		}
	}

	if (constant->mConstType == BfConstType_GlobalVar)
	{
		auto globalVar = (BfGlobalVar*)constant;
		if (strncmp(globalVar->mName, "__bfStrObj", 10) == 0)
		{
			int stringId = atoi(globalVar->mName  + 10);
			addr_ce strAddr = GetString(stringId);			
			if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
				CE_GETC(int32) = strAddr;
			else
				CE_GETC(int64) = strAddr;
			return true;
		}
	}

	if (constant->mTypeCode == BfTypeCode_StringId)
	{		
		addr_ce strAddr = GetString(constant->mInt32);

		if (type->IsPointer())
		{
			BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);
			strAddr += stringTypeInst->mInstSize;
		}

		if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
			CE_GETC(int32) = strAddr;
		else
			CE_GETC(int64) = strAddr;
		return true;
	}

	if ((constant->mConstType == BfConstType_TypeOf) || (constant->mConstType == BfConstType_TypeOf_WithData))
	{
		auto constTypeOf = (BfTypeOf_Const*)constant;
		addr_ce typeAddr = GetReflectType(constTypeOf->mType->mTypeId);
		if (mCeMachine->mCeModule->mSystem->mPtrSize == 4)
			CE_GETC(int32) = typeAddr;
		else
			CE_GETC(int64) = typeAddr;
		return true;
	}

	return false;
}


#define CE_CREATECONST_CHECKPTR(PTR, SIZE) \
	if ((((uint8*)(PTR) - memStart) - 0x10000) + (SIZE) > (memSize - 0x10000)) \
	{ \
		Fail("Access violation creating constant result"); \
		return BfIRValue(); \
	}

BfIRValue CeContext::CreateConstant(BfModule* module, uint8* ptr, BfType* bfType, BfType** outType)
{
	auto ceModule = mCeMachine->mCeModule;
	BfIRBuilder* irBuilder = module->mBfIRBuilder;
	int32 ptrSize = module->mSystem->mPtrSize;

	uint8* memStart = mMemory.mVals;
	int memSize = mMemory.mSize;

	if (bfType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)bfType;

		auto typeCode = primType->mTypeDef->mTypeCode;
		if (typeCode == BfTypeCode_IntPtr)
			typeCode = (ceModule->mCompiler->mSystem->mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
		else if (typeCode == BfTypeCode_UIntPtr)
			typeCode = (ceModule->mCompiler->mSystem->mPtrSize == 4) ? BfTypeCode_UInt32 : BfTypeCode_UInt64;

		switch (typeCode)
		{
		case BfTypeCode_Int8:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(int8));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(int8*)ptr);
		case BfTypeCode_UInt8:
		case BfTypeCode_Boolean:
		case BfTypeCode_Char8:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(uint8));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(uint8*)ptr);
		case BfTypeCode_Int16:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(int16));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(int16*)ptr);
		case BfTypeCode_UInt16:
		case BfTypeCode_Char16:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(uint16));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(uint16*)ptr);
		case BfTypeCode_Int32:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(int32));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(int32*)ptr);
		case BfTypeCode_UInt32:
		case BfTypeCode_Char32:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(uint32));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, (uint64) * (uint32*)ptr);
		case BfTypeCode_Int64:
		case BfTypeCode_UInt64:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(int64));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(uint64*)ptr);
		case BfTypeCode_Float:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(float));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(float*)ptr);
		case BfTypeCode_Double:
			CE_CREATECONST_CHECKPTR(ptr, sizeof(double));
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, *(double*)ptr);
		}

		return BfIRValue();
	}

	if (bfType->IsTypeInstance())
	{
		auto typeInst = bfType->ToTypeInstance();
		
		uint8* instData = ptr;
		// 		if ((typeInst->IsObject()) && (!isBaseType))
		// 		{
		// 			CE_CREATECONST_CHECKPTR(ptr, sizeof(addr_ce));
		// 			instData = mMemory.mVals + *(addr_ce*)ptr;
		// 			CE_CREATECONST_CHECKPTR(instData, typeInst->mInstSize);
		// 		}

		if (typeInst->IsInstanceOf(mCeMachine->mCompiler->mStringTypeDef))
		{
			BfTypeInstance* stringTypeInst = (BfTypeInstance*)ceModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);
			module->PopulateType(stringTypeInst);

			auto lenByteCount = stringTypeInst->mFieldInstances[0].mResolvedType->mSize;
			auto lenOffset = stringTypeInst->mFieldInstances[0].mDataOffset;
			auto allocSizeOffset = stringTypeInst->mFieldInstances[1].mDataOffset;
			auto ptrOffset = stringTypeInst->mFieldInstances[2].mDataOffset;

			int32 lenVal = *(int32*)(instData + lenOffset);

			char* charPtr = NULL;

			if (lenByteCount == 4)
			{
				int32 allocSizeVal = *(int32*)(instData + allocSizeOffset);
				if ((allocSizeVal & 0x40000000) != 0)
				{
					int32 ptrVal = *(int32*)(instData + ptrOffset);
					charPtr = (char*)(ptrVal + memStart);
				}
				else
				{
					charPtr = (char*)(instData + ptrOffset);
				}
			}

			CE_CREATECONST_CHECKPTR(charPtr, lenVal);
			String str(charPtr, lenVal);
			return module->GetStringObjectValue(str);
			
		}

		if (typeInst->IsInstanceOf(mCeMachine->mCompiler->mStringViewTypeDef))
		{
			char* charPtr = (char*)memStart + *(addr_ce*)(ptr);
			int32 lenVal = *(int32*)(ptr + ptrSize);

			CE_CREATECONST_CHECKPTR(charPtr, lenVal);
			String str(charPtr, lenVal);

			auto stringViewType = ceModule->ResolveTypeDef(mCeMachine->mCompiler->mStringViewTypeDef, BfPopulateType_Data)->ToTypeInstance();
			auto spanType = stringViewType->mBaseType;
			auto valueTypeType = spanType->mBaseType;

			SizedArray<BfIRValue, 1> valueTypeValues;
			BfIRValue valueTypeVal = irBuilder->CreateConstAgg(irBuilder->MapType(valueTypeType, BfIRPopulateType_Full), valueTypeValues);

			SizedArray<BfIRValue, 3> spanValues;
			spanValues.Add(valueTypeVal);
			spanValues.Add(module->GetStringCharPtr(str));
			spanValues.Add(irBuilder->CreateConst(BfTypeCode_IntPtr, lenVal));
			BfIRValue spanVal = irBuilder->CreateConstAgg(irBuilder->MapType(spanType, BfIRPopulateType_Full), spanValues);

			SizedArray<BfIRValue, 1> stringViewValues;
			stringViewValues.Add(spanVal);
			return irBuilder->CreateConstAgg(irBuilder->MapType(stringViewType, BfIRPopulateType_Full), stringViewValues);
		}

		SizedArray<BfIRValue, 8> fieldVals;

		if (typeInst->IsInstanceOf(ceModule->mCompiler->mSpanTypeDef))
		{
			if ((outType != NULL) && ((mCurExpectingType == NULL) || (mCurExpectingType->IsSizedArray())))
			{
				module->PopulateType(typeInst);

				auto ptrOffset = typeInst->mFieldInstances[0].mDataOffset;
				auto lenOffset = typeInst->mFieldInstances[1].mDataOffset;

				BfType* elemType = typeInst->GetUnderlyingType();

				CE_CREATECONST_CHECKPTR(instData, ceModule->mSystem->mPtrSize * 2);
				addr_ce addr = *(addr_ce*)(instData + ptrOffset);
				int32 lenVal = *(int32*)(instData + lenOffset);
				CE_CREATECONST_CHECKPTR(memStart + addr, lenVal);

				for (int i = 0; i < lenVal; i++)
				{
					auto result = CreateConstant(module, memStart + addr + i * elemType->GetStride(), elemType);
					if (!result)
						return BfIRValue();
					fieldVals.Add(result);
				}

				auto irArrayType = irBuilder->GetSizedArrayType(irBuilder->MapType(elemType, BfIRPopulateType_Full), lenVal);
				auto instResult = irBuilder->CreateConstAgg(irArrayType, fieldVals);
				*outType = module->CreateSizedArrayType(elemType, lenVal);
				return instResult;
			}

			Fail(StrFormat("Span return type '%s' must be received by a sized array", module->TypeToString(typeInst).c_str()));
			return BfIRValue();
		}

		if (typeInst->IsInstanceOf(ceModule->mCompiler->mTypeTypeDef))
		{
			addr_ce addr = *(addr_ce*)(instData);
			int typeId = GetTypeIdFromType(addr);
			if (typeId <= 0)
			{
				Fail("Unable to locate return type type");
				return BfIRValue();
			}

			return module->CreateTypeDataRef(module->mContext->mTypes[typeId]);
		}

		if (typeInst->IsObjectOrInterface())
		{
			Fail(StrFormat("Reference type '%s' return value not allowed", module->TypeToString(typeInst).c_str()));
			return BfIRValue();
		}
		
		if (typeInst->mBaseType != NULL)
		{
			auto result = CreateConstant(module, instData, typeInst->mBaseType);
			if (!result)
				return BfIRValue();
			fieldVals.Add(result);
		}
		
		if (typeInst->mIsUnion)
		{
			auto unionInnerType = typeInst->GetUnionInnerType();
			fieldVals.Add(CreateConstant(module, ptr, unionInnerType, outType));						
		}
		else
		{
			for (int fieldIdx = 0; fieldIdx < typeInst->mFieldInstances.size(); fieldIdx++)
			{
				auto& fieldInstance = typeInst->mFieldInstances[fieldIdx];
				if (fieldInstance.mDataOffset < 0)
					continue;

				if ((fieldInstance.mDataOffset == 0) && (typeInst == mCeMachine->mCompiler->mContext->mBfObjectType))
				{
					auto vdataPtr = module->GetClassVDataPtr(typeInst);
					if (fieldInstance.mResolvedType->IsInteger())
						fieldVals.Add(irBuilder->CreatePtrToInt(vdataPtr, ((BfPrimitiveType*)fieldInstance.mResolvedType)->mTypeDef->mTypeCode));
					else
						fieldVals.Add(vdataPtr);
					continue;
				}

				auto result = CreateConstant(module, instData + fieldInstance.mDataOffset, fieldInstance.mResolvedType);
				if (!result)
					return BfIRValue();

				if (fieldInstance.mDataIdx == fieldVals.mSize)
				{
					fieldVals.Add(result);
				}
				else
				{
					while (fieldInstance.mDataIdx >= fieldVals.mSize)
						fieldVals.Add(BfIRValue());
					fieldVals[fieldInstance.mDataIdx] = result;
				}
			}
		}
				
		for (auto& fieldVal : fieldVals)
		{
			if (!fieldVal)
				fieldVal = irBuilder->CreateConstArrayZero(0);
		}

		auto instResult = irBuilder->CreateConstAgg(irBuilder->MapTypeInst(typeInst, BfIRPopulateType_Full), fieldVals);
		return instResult;
	}

	if (bfType->IsPointer())
	{
		Fail(StrFormat("Pointer type '%s' return value not allowed", module->TypeToString(bfType).c_str()));
		return BfIRValue();
	}

	if ((bfType->IsSizedArray()) && (!bfType->IsUnknownSizedArrayType()))
	{
		SizedArray<BfIRValue, 8> values;
		auto sizedArrayType = (BfSizedArrayType*)bfType;
		for (int i = 0; i < sizedArrayType->mElementCount; i++)
		{
			auto elemValue = CreateConstant(module, ptr + i * sizedArrayType->mElementType->GetStride(), sizedArrayType->mElementType);
			if (!elemValue)
				return BfIRValue();
			values.Add(elemValue);
		}		

		return irBuilder->CreateConstAgg(irBuilder->MapType(sizedArrayType, BfIRPopulateType_Full), values);
	}

	return BfIRValue();
}

BfIRValue CeContext::CreateAttribute(BfAstNode* targetSrc, BfModule* module, BfIRConstHolder* constHolder, BfCustomAttribute* customAttribute)
{
	module->mContext->mUnreifiedModule->PopulateType(customAttribute->mType);
	auto ceAttrAddr = CeMalloc(customAttribute->mType->mSize) - mMemory.mVals;	
	BfIRValue ceAttrVal = module->mBfIRBuilder->CreateConstAggCE(module->mBfIRBuilder->MapType(customAttribute->mType, BfIRPopulateType_Identity), ceAttrAddr);
	BfTypedValue ceAttrTypedValue(ceAttrVal, customAttribute->mType);

	auto ctorMethodInstance = module->GetRawMethodInstance(customAttribute->mType, customAttribute->mCtor);
	if (ctorMethodInstance == NULL)
	{
		module->Fail("Attribute ctor failed", targetSrc);
		return ceAttrVal;
	}

	SizedArray<BfIRValue, 8> ctorArgs;
	if (!customAttribute->mType->IsValuelessType())
		ctorArgs.Add(ceAttrVal);
	int paramIdx = 0;
	for (auto& arg : customAttribute->mCtorArgs)
	{
		auto constant = constHolder->GetConstant(arg);
		if (!constant)
		{
			module->AssertErrorState();
			return ceAttrVal;
		}
		auto paramType = ctorMethodInstance->GetParamType(paramIdx);
		ctorArgs.Add(module->ConstantToCurrent(constant, constHolder, paramType, true));
		paramIdx++;
	}

	BfTypedValue retValue = Call(targetSrc, module, ctorMethodInstance, ctorArgs, CeEvalFlags_None, NULL);
	if (!retValue)
		return ceAttrVal;

	for (auto& setProperty : customAttribute->mSetProperties)
	{
		BfExprEvaluator exprEvaluator(module);
		BfMethodDef* setMethodDef = exprEvaluator.GetPropertyMethodDef(setProperty.mPropertyRef, BfMethodType_PropertySetter, BfCheckedKind_NotSet, ceAttrTypedValue);
		BfMethodInstance* setMethodInstance = NULL;
		if (setMethodDef != NULL)
			setMethodInstance = module->GetRawMethodInstance(customAttribute->mType, setMethodDef);
		if ((setMethodInstance == NULL) || (!setProperty.mParam))
		{
			module->Fail("Attribute prop failed", targetSrc);
			return ceAttrVal;
		}
		
		SizedArray<BfIRValue, 1> setArgs;
		if (!customAttribute->mType->IsValuelessType())
			setArgs.Add(ceAttrVal);		
		if (!setProperty.mParam.mType->IsValuelessType())
		{
			auto constant = constHolder->GetConstant(setProperty.mParam.mValue);
			if (!constant)
			{
				module->AssertErrorState();
				return ceAttrVal;
			}			
			setArgs.Add(module->ConstantToCurrent(constant, constHolder, setProperty.mParam.mType, true));
		}

		BfTypedValue retValue = Call(targetSrc, module, setMethodInstance, setArgs, CeEvalFlags_None, NULL);
		if (!retValue)
			return ceAttrVal;
	}

	for (auto& setField : customAttribute->mSetField)
	{
		BfFieldInstance* fieldInstance = setField.mFieldRef;
		if (fieldInstance->mDataOffset < 0)
			continue;		
		auto constant = constHolder->GetConstant(setField.mParam.mValue);
		WriteConstant(module, ceAttrAddr + fieldInstance->mDataOffset, constant, fieldInstance->mResolvedType);
	}

	return ceAttrVal;
}


BfTypedValue CeContext::Call(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags, BfType* expectingType)
{
	// DISABLED
	//return BfTypedValue();

	AutoTimer autoTimer(mCeMachine->mRevisionExecuteTime);

 	SetAndRestoreValue<CeContext*> prevContext(mCeMachine->mCurContext, this);
	SetAndRestoreValue<CeEvalFlags> prevEvalFlags(mCurEvalFlags, flags);
	SetAndRestoreValue<BfAstNode*> prevTargetSrc(mCurTargetSrc, targetSrc);
	SetAndRestoreValue<BfModule*> prevModule(mCurModule, module);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfType*> prevExpectingType(mCurExpectingType, expectingType);	

	// Reentrancy may occur as methods need defining
	//SetAndRestoreValue<BfMethodState*> prevMethodStateInConstEval(module->mCurMethodState, NULL);

	if (mCeMachine->mAppendAllocInfo != NULL)
	{		
		if (mCeMachine->mAppendAllocInfo->mAppendSizeValue)
		{
			bool isConst = mCeMachine->mAppendAllocInfo->mAppendSizeValue.IsConst();
			if (isConst)
			{
				auto constant = module->mBfIRBuilder->GetConstant(mCeMachine->mAppendAllocInfo->mAppendSizeValue);
				if (constant->mConstType == BfConstType_Undef)
					isConst = false;
			}

			if (!isConst)
			{
				Fail("Non-constant append alloc");
				return BfTypedValue();
			}
		}
	}

	int thisArgIdx = -1;
	int appendAllocIdx = -1;
	bool hasAggData = false;
	if (methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)
	{	
		if (!methodInstance->GetOwner()->IsValuelessType())
		{
			thisArgIdx = 0;
			auto constant = module->mBfIRBuilder->GetConstant(args[0]);
			if ((constant != NULL) && (constant->mConstType == BfConstType_AggCE))
				hasAggData = true;
		}

		if ((methodInstance->GetParamCount() >= 1) && (methodInstance->GetParamKind(0) == BfParamKind_AppendIdx))
			appendAllocIdx = 1;		
	}

	int paramCompositeSize = 0;
	int paramIdx = methodInstance->GetParamCount();
	for (int argIdx = (int)args.size() - 1; argIdx >= 0; argIdx--)
	{
		BfType* paramType = NULL;
		while (true)
		{
			paramIdx--;
			paramType = methodInstance->GetParamType(paramIdx);
			if (paramType->IsTypedPrimitive())
				paramType = paramType->GetUnderlyingType();
			if (!paramType->IsValuelessType())
				break;
		}
		if (paramType->IsComposite())
		{			
			paramCompositeSize += paramType->mSize;
		}

		auto arg = args[argIdx];
		bool isConst = arg.IsConst();
		if (isConst)
		{
			auto constant = module->mBfIRBuilder->GetConstant(arg);
			if (constant->mConstType == BfConstType_Undef)
				isConst = false;
		}

		if (!isConst)
		{
			if ((argIdx != thisArgIdx) && (argIdx != appendAllocIdx))
			{
				Fail(StrFormat("Non-constant argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
		}
	}

	BF_ASSERT(mCallStack.IsEmpty());

	auto methodDef = methodInstance->mMethodDef;

	if (mCeMachine->mCeModule == NULL)
		mCeMachine->Init();

	auto ceModule = mCeMachine->mCeModule;
	bool added = false;
	CeFunction* ceFunction = mCeMachine->GetFunction(methodInstance, BfIRValue(), added);

	if (ceFunction->mGenerating)
	{
		Fail("Recursive var-inference");
		return BfTypedValue();
	}

	if (!ceFunction->mInitialized)
		mCeMachine->PrepareFunction(ceFunction, NULL);	

	auto stackPtr = &mMemory[0] + BF_CE_STACK_SIZE;
	auto* memStart = &mMemory[0];

	BfTypeInstance* thisType = methodInstance->GetOwner();
	addr_ce allocThisInstAddr = 0;
	addr_ce allocThisAddr = 0;
	int allocThisSize = -1;

	if ((thisArgIdx != -1) && (!hasAggData))
	{
		allocThisSize = thisType->mInstSize;

		if ((mCeMachine->mAppendAllocInfo != NULL) && (mCeMachine->mAppendAllocInfo->mAppendSizeValue))
		{
			BF_ASSERT(mCeMachine->mAppendAllocInfo->mModule == module);
			BF_ASSERT(mCeMachine->mAppendAllocInfo->mAppendSizeValue.IsConst());

			auto appendSizeConstant = module->mBfIRBuilder->GetConstant(mCeMachine->mAppendAllocInfo->mAppendSizeValue);
			BF_ASSERT(module->mBfIRBuilder->IsInt(appendSizeConstant->mTypeCode));
			allocThisSize += appendSizeConstant->mInt32;
		}

		stackPtr -= allocThisSize;
		auto allocThisPtr = stackPtr;
		memset(allocThisPtr, 0, allocThisSize);

		if (thisType->IsObject())
			*(int32*)(allocThisPtr) = thisType->mTypeId;

		allocThisInstAddr = allocThisPtr - memStart;
		allocThisAddr = allocThisInstAddr;
	}

	addr_ce allocAppendIdxAddr = 0;
	if (appendAllocIdx != -1)
	{
		stackPtr -= ceModule->mSystem->mPtrSize;
		memset(stackPtr, 0, ceModule->mSystem->mPtrSize);
		allocAppendIdxAddr = stackPtr - memStart;
	}

	auto _FixVariables = [&]()
	{
		intptr memOffset = &mMemory[0] - memStart;
		if (memOffset == 0)
			return;
		memStart += memOffset;
		stackPtr += memOffset;
	};

	addr_ce compositeStartAddr = stackPtr - memStart;
	stackPtr -= paramCompositeSize;
	addr_ce useCompositeAddr = compositeStartAddr;
	paramIdx = methodInstance->GetParamCount();
	for (int argIdx = (int)args.size() - 1; argIdx >= 0; argIdx--)
	{
		BfType* paramType = NULL;
		while (true)
		{
			paramIdx--;
			paramType = methodInstance->GetParamType(paramIdx);
			if (paramType->IsTypedPrimitive())
				paramType = paramType->GetUnderlyingType();
			if (!paramType->IsValuelessType())
				break;
		}

		bool isParams = methodInstance->GetParamKind(paramIdx) == BfParamKind_Params;
		auto arg = args[argIdx];
		if (!arg.IsConst())
		{
			if (argIdx == thisArgIdx)
			{
				if (mCeMachine->mAppendAllocInfo != NULL)
					BF_ASSERT(mCeMachine->mAppendAllocInfo->mAllocValue == arg);

				stackPtr -= ceModule->mSystem->mPtrSize;
				int64 addr64 = allocThisAddr;
				memcpy(stackPtr, &addr64, ceModule->mSystem->mPtrSize);
				continue;
			}
			else if (argIdx == appendAllocIdx)
			{
				stackPtr -= ceModule->mSystem->mPtrSize;
				int64 addr64 = allocAppendIdxAddr;
				memcpy(stackPtr, &addr64, ceModule->mSystem->mPtrSize);
				continue;
			}
			else
				return BfTypedValue();
		}

		auto constant = module->mBfIRBuilder->GetConstant(arg);
		if (paramType->IsComposite())
		{
			auto paramTypeInst = paramType->ToTypeInstance();
			useCompositeAddr -= paramTypeInst->mInstSize;
			if (!WriteConstant(module, useCompositeAddr, constant, paramType, isParams))
			{
				Fail(StrFormat("Failed to process argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
			_FixVariables();

			stackPtr -= ceModule->mSystem->mPtrSize;
			int64 addr64 = useCompositeAddr;
			memcpy(stackPtr, &addr64, ceModule->mSystem->mPtrSize);
		}
		else
		{
			stackPtr -= paramType->mSize;
			if (!WriteConstant(module, stackPtr - memStart, constant, paramType, isParams))
			{
				Fail(StrFormat("Failed to process argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
			_FixVariables();
		}
	}

	addr_ce retAddr = 0;
	if (ceFunction->mMaxReturnSize > 0)
	{
		int retSize = ceFunction->mMaxReturnSize;
		stackPtr -= retSize;
		retAddr = stackPtr - memStart;
	}

	mCeMachine->mAppendAllocInfo = NULL;

	BfType* returnType = NULL;
	bool success = Execute(ceFunction, stackPtr - ceFunction->mFrameSize, stackPtr, returnType);
	memStart = &mMemory[0];

	addr_ce retInstAddr = retAddr;

	if (returnType->IsInstanceOf(mCeMachine->mCompiler->mTypeTypeDef))
	{
		// Allow
	}
	else if ((returnType->IsObject()) || (returnType->IsPointer()))
	{
		// Or pointer?
		retInstAddr = *(addr_ce*)(memStart + retAddr);
	}

	BfTypedValue returnValue;

	if (success)
	{
		BfTypedValue retValue;
		if ((retInstAddr != 0) || (allocThisInstAddr != 0))
		{
			auto* retPtr = memStart + retInstAddr;
			if (allocThisInstAddr != 0)
			{
				retPtr = memStart + allocThisAddr;
				returnType = thisType;
			}

			BfType* usedReturnType = returnType;
			BfIRValue constVal = CreateConstant(module, retPtr, returnType, &usedReturnType);
			if (constVal)
				returnValue = BfTypedValue(constVal, usedReturnType);
			else
			{
				Fail("Failed to encode return argument");
			}
		}
		else if (returnType->IsComposite())
		{
			returnValue = BfTypedValue(module->mBfIRBuilder->CreateConstArrayZero(module->mBfIRBuilder->MapType(returnType)), returnType);
		}
		else if (returnType->IsValuelessType())
		{
			returnValue = BfTypedValue(module->mBfIRBuilder->GetFakeVal(), returnType);			
		}
	}

	mCallStack.Clear();

	module->AddDependency(methodInstance->GetOwner(), module->mCurTypeInstance, BfDependencyMap::DependencyFlag_ConstEval);

	return returnValue;
}

#define CE_CHECKSTACK() \
	if (stackPtr < memStart) \
	{ \
		_Fail("Stack overflow"); \
		return false; \
	}

#define CE_CHECKALLOC(SIZE) \
	if ((SIZE < 0) || (uintptr)memSize + (uintptr)SIZE > BF_CE_MAX_MEMORY) \
	{ \
		_Fail("Maximum memory size exceeded"); \
	}

// This check will fail for addresses < 64K (null pointer), or out-of-bounds
#define CE_CHECKSIZE(SIZE) \
	if ((SIZE) < 0) \
	{ \
		_Fail("Invalid memory size"); \
		return false; \
	}
#define CE_CHECKADDR(ADDR, SIZE) \
	if (((ADDR) - 0x10000) + (SIZE) > (memSize - 0x10000)) \
	{ \
		_Fail("Access violation"); \
		return false; \
	}

#define CE_GETINST(T) *((T*)(instPtr += sizeof(T)) - 1)
#define CE_GETFRAME(T) *(T*)(framePtr + *((int32*)(instPtr += sizeof(int32)) - 1))
#define CEOP_BIN(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto lhs = CE_GETFRAME(T); \
		auto rhs = CE_GETFRAME(T); \
		result = lhs OP rhs; \
	}
#define CEOP_BIN_DIV(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto lhs = CE_GETFRAME(T); \
		auto rhs = CE_GETFRAME(T); \
		if (rhs == 0) \
		{ \
			_Fail("Division by zero"); \
			return false; \
		} \
		result = lhs OP rhs; \
	}
#define CEOP_BIN2(OP, TLHS, TRHS) \
	{ \
		auto& result = CE_GETFRAME(TLHS); \
		auto lhs = CE_GETFRAME(TLHS); \
		auto rhs = CE_GETFRAME(TRHS); \
		result = lhs OP rhs; \
	}
#define CEOP_BIN_CONST(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto lhs = CE_GETFRAME(T); \
		auto rhs = CE_GETINST(T); \
		result = lhs OP rhs; \
	}

#define CEOP_UNARY_FUNC(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto lhs = CE_GETFRAME(T); \
		result = OP(lhs); \
	}
#define CEOP_BIN_FUNC(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto lhs = CE_GETFRAME(T); \
		auto rhs = CE_GETFRAME(T); \
		result = OP(lhs, rhs); \
	}
#define CEOP_UNARY(OP, T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto val = CE_GETFRAME(T); \
		result = OP val; \
	}
#define CEOP_CMP(OP, T) \
	{ \
		auto& result = CE_GETFRAME(bool); \
		auto lhs = CE_GETFRAME(T); \
		auto rhs = CE_GETFRAME(T); \
		result = lhs OP rhs; \
	}
#define CE_CAST(TFROM, TTO) \
	{ \
		auto& result = CE_GETFRAME(TTO); \
		auto val = CE_GETFRAME(TFROM); \
		result = (TTO)val; \
	}
#define CE_LOAD(T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		auto ceAddr = CE_GETFRAME(addr_ce); \
		CE_CHECKADDR(ceAddr, sizeof(T)); \
		result = *(T*)(memStart + ceAddr); \
	}
#define CE_STORE(T) \
	{ \
		auto val = CE_GETFRAME(T); \
		auto ceAddr = CE_GETFRAME(addr_ce); \
		CE_CHECKADDR(ceAddr, sizeof(T)); \
		*(T*)(memStart + ceAddr) = val; \
	}
#define CEOP_MOVE(T) \
	{ \
		auto val = CE_GETFRAME(T); \
		auto& ptr = CE_GETFRAME(T); \
		ptr = val; \
	}
#define CEOP_PUSH(T) \
	{ \
		stackPtr -= sizeof(T); \
		auto val = CE_GETFRAME(T); \
		*((T*)stackPtr) = val; \
		CE_CHECKSTACK(); \
	}
#define CEOP_POP(T) \
	{ \
		auto& result = CE_GETFRAME(T); \
		result = *((T*)stackPtr); \
		stackPtr += sizeof(T); \
	}
#define CE_CALL(CEFUNC) \
	if (CEFUNC == NULL) \
	{ \
		_Fail("Unable to locate function entry"); \
		return false; \
	} \
	mCallStack.Add(_GetCurFrame()); \
	ceFunction = CEFUNC; \
	framePtr = stackPtr; \
	stackPtr -= ceFunction->mFrameSize; \
	instPtr = &ceFunction->mCode[0]; \
	CE_CHECKSTACK();

static void CeSetAddrVal(void* ptr, addr_ce val, int32 ptrSize)
{
	if (ptrSize == 4)
		*(int32*)(ptr) = (int32)val;
	else
		*(int64*)(ptr) = (int64)val;
}

bool CeContext::Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr, BfType*& returnType)
{
	auto ceModule = mCeMachine->mCeModule;
	CeFunction* ceFunction = startFunction;
	returnType = startFunction->mMethodInstance->mReturnType;
	uint8* memStart = &mMemory[0];
	int memSize = mMemory.mSize;
	uint8* instPtr = (ceFunction->mCode.IsEmpty()) ? NULL : &ceFunction->mCode[0];
	uint8* stackPtr = startStackPtr;
	uint8* framePtr = startFramePtr;
	bool needsFunctionIds = ceModule->mSystem->mPtrSize != 8;
	int32 ptrSize = ceModule->mSystem->mPtrSize;

	volatile bool* fastFinishPtr = &mCeMachine->mCompiler->mFastFinish;
	volatile bool* cancelingPtr = &mCeMachine->mCompiler->mCanceling;

	auto _GetCurFrame = [&]()
	{
		CeFrame ceFrame;
		ceFrame.mFunction = ceFunction;
		ceFrame.mReturnType = returnType;
		ceFrame.mFrameAddr = framePtr - memStart;
		ceFrame.mStackAddr = stackPtr - memStart;
		ceFrame.mInstPtr = instPtr;
		return ceFrame;
	};

	auto _FixVariables = [&]()
	{
		memSize = mMemory.mSize;
		intptr memOffset = &mMemory[0] - memStart;
		if (memOffset == 0)
			return;
		memStart += memOffset;
		stackPtr += memOffset;
		framePtr += memOffset;
	};

	auto _Fail = [&](const StringImpl& error)
	{
		Fail(_GetCurFrame(), error);
	};

	auto _CheckFunction = [&](CeFunction* checkFunction, bool& handled)
	{
		if (checkFunction == NULL)
		{
			Fail(_GetCurFrame(), "Const method not available");
			return false;
		}

		if (checkFunction->mFunctionKind != CeFunctionKind_Normal)
		{
			if (checkFunction->mFunctionKind == CeFunctionKind_OOB)
			{
				Fail(_GetCurFrame(), "Array out of bounds");
				return false;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Malloc)
			{
				int32 size = *(int32*)((uint8*)stackPtr + 4);
				CE_CHECKALLOC(size);
				uint8* ptr = CeMalloc(size);				
				CeSetAddrVal(stackPtr + 0, ptr - memStart, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Free)
			{
				addr_ce freeAddr = *(addr_ce*)((uint8*)stackPtr + 4);
				bool success = CeFree(freeAddr);
				if (!success)
					_Fail("Invalid heap address");
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_FatalError)
			{
				int32 strInstAddr = *(int32*)((uint8*)stackPtr + 0);
				CE_CHECKADDR(strInstAddr, 0);

				BfTypeInstance* stringTypeInst = (BfTypeInstance*)ceModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);

				auto lenByteCount = stringTypeInst->mFieldInstances[0].mResolvedType->mSize;
				auto lenOffset = stringTypeInst->mFieldInstances[0].mDataOffset;
				auto allocSizeOffset = stringTypeInst->mFieldInstances[1].mDataOffset;
				auto ptrOffset = stringTypeInst->mFieldInstances[2].mDataOffset;

				uint8* strInst = (uint8*)(strInstAddr + memStart);
				int32 lenVal = *(int32*)(strInst + lenOffset);

				char* charPtr = NULL;

				if (lenByteCount == 4)
				{
					int32 allocSizeVal = *(int32*)(strInst + allocSizeOffset);
					if ((allocSizeVal & 0x40000000) != 0)
					{
						int32 ptrVal = *(int32*)(strInst + ptrOffset);
						charPtr = (char*)(ptrVal + memStart);
					}
					else
					{
						charPtr = (char*)(strInst + ptrOffset);
					}
				}

				int32 ptrVal = *(int32*)(strInst + ptrOffset);

				String error = "Fatal Error: ";
				if (charPtr != NULL)
					error.Insert(error.length(), charPtr, lenVal);
				_Fail(error);

				return false;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_DynCheckFailed)
			{
				_Fail("Dynamic cast check failed");
				return false;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_DebugWrite)
			{
				int32 ptrVal = *(int32*)((uint8*)stackPtr + 0);
				auto size = *(int32*)(stackPtr + ceModule->mSystem->mPtrSize);
				CE_CHECKADDR(ptrVal, size);
				char* strPtr = (char*)(ptrVal + memStart);
				String str;
				str.Insert(0, strPtr, size);
				OutputDebugStr(str);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_DebugWrite_Int)
			{
				int32 intVal = *(int32*)((uint8*)stackPtr + 0);
				OutputDebugStrF("Debug Val: %d\n", intVal);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetReflectType)
			{
				addr_ce objAddr = *(addr_ce*)((uint8*)stackPtr + ceModule->mSystem->mPtrSize);
				CE_CHECKADDR(addr_ce, 4);
				int32 typeId = *(int32*)(objAddr + memStart);

				auto reflectType = GetReflectType(typeId);
				_FixVariables();
				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetReflectTypeById)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + ceModule->mSystem->mPtrSize);
				auto reflectType = GetReflectType(typeId);
				_FixVariables();
				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetReflectTypeByName)
			{
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				String typeName;
				if (!GetStringFromStringView(strViewPtr, typeName))
				{
					_Fail("Invalid StringView");
					return false;
				}
				auto reflectType = GetReflectType(typeName);
				_FixVariables();
				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetReflectSpecializedType)
			{
				addr_ce typeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce typeSpan = *(addr_ce*)((uint8*)stackPtr + ptrSize * 2);

				auto reflectType = GetReflectSpecializedType(typeAddr, typeSpan);
				_FixVariables();
				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Type_GetCustomAttribute)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + 1);
				int32 attributeTypeId = *(int32*)((uint8*)stackPtr + 1 + 4);
				addr_ce resultPtr = *(addr_ce*)((uint8*)stackPtr + 1 + 4 + 4);

				BfType* type = GetBfType(typeId);
				bool success = false;
				if (type != NULL)
				{
					auto typeInst = type->ToTypeInstance();
					if (typeInst != NULL)
						success = GetCustomAttribute(typeInst->mCustomAttributes, attributeTypeId, resultPtr);
				}

				*(addr_ce*)(stackPtr + 0) = success;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetMethodCount)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + 4);
				
				CeTypeInfo* typeInfo = mCeMachine->GetTypeInfo(GetBfType(typeId));				
				if (typeInfo == NULL)
				{
					_Fail("Invalid type");
					return false;
				}

				*(int32*)(stackPtr + 0) = (int)typeInfo->mMethodInstances.size();
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetMethod)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + 8);
				int32 methodIdx = *(int32*)((uint8*)stackPtr + 8+4);
				
				CeTypeInfo* typeInfo = mCeMachine->GetTypeInfo(GetBfType(typeId));				
				if (typeInfo == NULL)
				{
					_Fail("Invalid type");
					return false;
				}
				if ((methodIdx < 0) || (methodIdx >= typeInfo->mMethodInstances.mSize))
				{
					_Fail("Method out of bounds");
					return false;
				}

				*(int64*)(stackPtr + 0) = (int64)(intptr)typeInfo->mMethodInstances[methodIdx];
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_ToString)
			{				
				int64 methodHandle = *(int64*)((uint8*)stackPtr + ptrSize);
				
				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				
				CeSetAddrVal(stackPtr + 0, GetString(mCeMachine->mCeModule->MethodToString(methodInstance)), ptrSize);
				_FixVariables();
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetName)
			{				
				int64 methodHandle = *(int64*)((uint8*)stackPtr + ptrSize);
				
				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				
				CeSetAddrVal(stackPtr + 0, GetString(methodInstance->mMethodDef->mName), ptrSize);
				_FixVariables();
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetInfo)			
			{	
				// int32 mReturnType
				// int32 mParamCount
				// int16 mFlags

				int64 methodHandle = *(int64*)((uint8*)stackPtr + 4+4+2);
				
				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				
				*(int32*)(stackPtr + 0) = methodInstance->mReturnType->mTypeId;
				*(int32*)(stackPtr + 4) = methodInstance->GetParamCount();
				*(int16*)(stackPtr + 4+4) = methodInstance->GetMethodFlags();								
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetParamInfo)			
			{	
				// int32 mParamType				
				// int16 mFlags
				// str mName

				int64 methodHandle = *(int64*)((uint8*)stackPtr + 4+2+ptrSize);
				int32 paramIdx = *(int32*)((uint8*)stackPtr + 4+2+ptrSize+8);
				
				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				
				addr_ce stringAddr = GetString(methodInstance->GetParamName(paramIdx));
				_FixVariables();
				*(int32*)(stackPtr + 0) = methodInstance->GetParamType(paramIdx)->mTypeId;
				*(int16*)(stackPtr + 4) = 0; // Flags
				CeSetAddrVal(stackPtr + 4+2, stringAddr, ptrSize);								
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitTypeBody)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr);
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr + sizeof(int32));
				if ((mCurEmitContext == NULL) || (mCurEmitContext->mType->mTypeId != typeId))
				{
					_Fail("Code cannot be emitted for this type in this context");
					return false;
				}
				if (!GetStringFromStringView(strViewPtr, mCurEmitContext->mEmitData))
				{
					_Fail("Invalid StringView");
					return false;
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitMethodEntry)
			{
				int64 methodHandle = *(int64*)((uint8*)stackPtr);
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr + sizeof(int64));

				if ((mCurEmitContext == NULL) || (mCurEmitContext->mMethodInstance == NULL) ||
					(methodHandle != (int64)mCurEmitContext->mMethodInstance))
				{
					_Fail("Code cannot be emitted for this method in this context");
					return false;
				}
				if (!GetStringFromStringView(strViewPtr, mCurEmitContext->mEmitData))
				{
					_Fail("Invalid StringView");
					return false;
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitMethodExit)
			{
				int64 methodHandle = *(int64*)((uint8*)stackPtr);
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr + sizeof(int64));
				if ((mCurEmitContext == NULL) || (mCurEmitContext->mMethodInstance == NULL) ||
					(methodHandle != (int64)mCurEmitContext->mMethodInstance))
				{
					_Fail("Code cannot be emitted for this method in this context");
					return false;
				}
				if (!GetStringFromStringView(strViewPtr, mCurEmitContext->mExitEmitData))
				{
					_Fail("Invalid StringView");
					return false;
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitMixin)
			{				
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr);				
				String emitStr;
				if (!GetStringFromStringView(strViewPtr, emitStr))
				{
					_Fail("Invalid StringView");
					return false;
				}

				mCurModule->CEMixin(mCurTargetSrc, emitStr);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Sleep)
			{
				int32 sleepMS = *(int32*)((uint8*)stackPtr);
				while (sleepMS > 0)
				{
					if (*fastFinishPtr)
						break;

					if (sleepMS > 200)
					{
						BfpThread_Sleep(200);
						sleepMS -= 200;
						continue;
					}
					BfpThread_Sleep(sleepMS);
					break;
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSystem_GetTimeStamp)
			{
				int64& result = *(int64*)((uint8*)stackPtr + 0);
				result = BfpSystem_GetTimeStamp();				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_ToLower)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 4);
				result = utf8proc_tolower(val);				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_ToUpper)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 4);
				result = utf8proc_toupper(val);				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsLower)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				result = utf8proc_category(val) == UTF8PROC_CATEGORY_LL;				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsUpper)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				result = utf8proc_category(val) == UTF8PROC_CATEGORY_LU;				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsWhiteSpace_EX)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				auto cat = utf8proc_category(val);
				result = (cat == UTF8PROC_CATEGORY_ZS) || (cat == UTF8PROC_CATEGORY_ZL) || (cat == UTF8PROC_CATEGORY_ZP);				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsLetterOrDigit)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				auto cat = utf8proc_category(val);
				switch (cat)
				{
				case UTF8PROC_CATEGORY_LU:
				case UTF8PROC_CATEGORY_LL:
				case UTF8PROC_CATEGORY_LT:
				case UTF8PROC_CATEGORY_LM:
				case UTF8PROC_CATEGORY_LO:
				case UTF8PROC_CATEGORY_ND:
				case UTF8PROC_CATEGORY_NL:
				case UTF8PROC_CATEGORY_NO:
					result = true;
					break;
				default:
					result = false;
				}				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsLetter)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				auto cat = utf8proc_category(val);
				switch (cat)
				{
				case UTF8PROC_CATEGORY_LU:
				case UTF8PROC_CATEGORY_LL:
				case UTF8PROC_CATEGORY_LT:
				case UTF8PROC_CATEGORY_LM:
				case UTF8PROC_CATEGORY_LO:
					result = true;
					break;
				default:
					result = false;
				}				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Char32_IsNumber)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				int32 val = *(int32*)((uint8*)stackPtr + 1);
				auto cat = utf8proc_category(val);
				switch (cat)
				{
				case UTF8PROC_CATEGORY_ND:
				case UTF8PROC_CATEGORY_NL:
				case UTF8PROC_CATEGORY_NO:
					result = true;
					break;
				default:
					result = false;
				}				
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Double_Strtod)
			{
				double& result = *(double*)((uint8*)stackPtr + 0);
				addr_ce strAddr = *(addr_ce*)((uint8*)stackPtr + 8);
				addr_ce endAddr = *(addr_ce*)((uint8*)stackPtr + 8 + ptrSize);
								
				addr_ce checkAddr = strAddr;				
				while (true)
				{
					if ((uintptr)checkAddr >= (uintptr)memSize)
					{
						checkAddr++;
						break;
					}
					if (memStart[checkAddr] == 0)
						break;
				}
				CE_CHECKADDR(strAddr, checkAddr - strAddr + 1);

				char* strPtr = (char*)(memStart + strAddr);								
				char** endPtr = NULL;
				if (endAddr != NULL)
					endPtr = (char**)(memStart + endAddr);
				result = strtod(strPtr, endPtr);
				if (endAddr != 0)
				{
					CE_CHECKADDR(endAddr, ptrSize);
					CeSetAddrVal(endPtr, (uint8*)endPtr - memStart, ptrSize);
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Double_Ftoa)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				float val = *(float*)((uint8*)stackPtr + 4);
				addr_ce strAddr = *(addr_ce*)((uint8*)stackPtr + 4 + 4);

				char str[256];
				int count = sprintf(str, "%1.9f", val);
				CE_CHECKADDR(strAddr, count + 1);
				memcpy(memStart + strAddr, str, count + 1);
				result = count;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Double_ToString)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				double val = *(double*)((uint8*)stackPtr + 4);
				addr_ce strAddr = *(addr_ce*)((uint8*)stackPtr + 4 + 8);

				char str[256];
				int count = DoubleToString(val, str);				
				CE_CHECKADDR(strAddr, count + 1);
				memcpy(memStart + strAddr, str, count + 1);
				result = count;
			}
			else			
			{
				Fail(_GetCurFrame(), StrFormat("Unable to invoke extern method '%s'", ceModule->MethodToString(checkFunction->mMethodInstance).c_str()));
				return false;
			}
			handled = true;
			return true;
		}


		if (!checkFunction->mFailed)
			return true;
		auto error = Fail(_GetCurFrame(), StrFormat("Method call '%s' failed", ceModule->MethodToString(checkFunction->mMethodInstance).c_str()));
		if ((error != NULL) && (!checkFunction->mGenError.IsEmpty()))
			mCeMachine->mCompiler->mPassInstance->MoreInfo("Comptime method generation error: " + checkFunction->mGenError);
		return false;
	};

	//
	{
		bool handled = false;
		if (!_CheckFunction(ceFunction, handled))
			return false;
		if (handled)
			return true;
	}

	int callCount = 0;
	int instIdx = 0;

	while (true)
	{
		if (*fastFinishPtr)
		{
			if (*cancelingPtr)
				_Fail("Comptime evaluation canceled");
			return false;
		}

		++instIdx;

		CeOp op = CE_GETINST(CeOp);
		switch (op)
		{
		case CeOp_Ret:
		{
			if (mCallStack.mSize == 0)
				return true;

			auto& ceFrame = mCallStack.back();
			ceFunction = ceFrame.mFunction;
			instPtr = ceFrame.mInstPtr;
			stackPtr = memStart + ceFrame.mStackAddr;
			framePtr = memStart + ceFrame.mFrameAddr;
			returnType = ceFrame.mReturnType;

			mCallStack.pop_back();
		}
		break;
		case CeOp_SetRetType:
		{
			int typeId = CE_GETINST(int32);
			returnType = GetBfType(typeId);
			BF_ASSERT(returnType != NULL);
		}
		break;
		case CeOp_Jmp:
		{
			auto relOfs = CE_GETINST(int32);
			instPtr += relOfs;
		}
		break;
		case CeOp_JmpIf:
		{
			auto relOfs = CE_GETINST(int32);
			bool cond = CE_GETFRAME(bool);
			if (cond)
				instPtr += relOfs - 4;
		}
		break;
		case CeOp_JmpIfNot:
		{
			auto relOfs = CE_GETINST(int32);
			bool cond = CE_GETFRAME(bool);
			if (!cond)
				instPtr += relOfs - 4;
		}
		break;
		case CeOp_Error:
		{
			auto errorKind = (CeErrorKind)CE_GETINST(int32);
			switch (errorKind)
			{
			case CeErrorKind_GlobalVariable:
				_Fail("Global variable access not allowed");
				break;
			case CeErrorKind_FunctionPointer:
				_Fail("Function pointer calls not allowed");
				break;
			case CeErrorKind_Intrinsic:
				_Fail("Intrinsic not allowed");
				break;
			case CeErrorKind_ObjectDynCheckFailed:
				_Fail("Dynamic cast check failed");
				break;
			default:
				_Fail("Operation not allowed");
				break;
			}
		}
		break;
		case CeOp_DynamicCastCheck:
		{
			auto& result = CE_GETFRAME(uint32);
			auto valueAddr = CE_GETFRAME(addr_ce);
			int32 ifaceId = CE_GETINST(int32);

			if (valueAddr == 0)
			{
				CeSetAddrVal(&result, 0, ptrSize);
			}
			else
			{
				CE_CHECKADDR(valueAddr, sizeof(int32));

				auto ifaceType = GetBfType(ifaceId);
				int32 objTypeId = *(int32*)(memStart + valueAddr);
				auto valueType = GetBfType(objTypeId);
				if ((ifaceType == NULL) || (valueType == NULL))
				{
					_Fail("Invalid type");
					return false;
				}

				if (ceModule->TypeIsSubTypeOf(valueType->ToTypeInstance(), ifaceType->ToTypeInstance(), false))
					CeSetAddrVal(&result, valueAddr, ptrSize);
				else
					CeSetAddrVal(&result, 0, ptrSize);
					
			}			
		}
		break;
		case CeOp_GetReflectType:
		{
			auto frameOfs = CE_GETINST(int32);
			int32 typeId = CE_GETINST(int32);
			auto reflectType = GetReflectType(typeId);
			_FixVariables();
			CeSetAddrVal(framePtr + frameOfs, reflectType, ptrSize);			
		}
		break;
		case CeOp_GetString:
		{
			auto frameOfs = CE_GETINST(int32);
			auto stringTableIdx = CE_GETINST(int32);
			auto& ceStringEntry = ceFunction->mStringTable[stringTableIdx];
			if (ceStringEntry.mBindExecuteId != mExecuteId)
			{
				ceStringEntry.mStringAddr = GetString(ceStringEntry.mStringId);
				_FixVariables();
				ceStringEntry.mBindExecuteId = mExecuteId;
			}
			CeSetAddrVal(framePtr + frameOfs, ceStringEntry.mStringAddr, ptrSize);			
		}
		break;
		case CeOp_Malloc:
		{
			auto frameOfs = CE_GETINST(int32);
			int32 size = CE_GETFRAME(int32);
			CE_CHECKALLOC(size);
			uint8* mem = CeMalloc(size);
			_FixVariables();
			CeSetAddrVal(framePtr + frameOfs, mem - memStart, ptrSize);
		}
		break;
		case CeOp_Free:
		{
			auto freeAddr = CE_GETFRAME(addr_ce);
			bool success = CeFree(freeAddr);
			if (!success)
				_Fail("Invalid heap address");
		}
		break;
		case CeOp_MemSet:
		{
			auto destAddr = CE_GETFRAME(addr_ce);
			uint8 setValue = CE_GETFRAME(uint8);
			int32 setSize = CE_GETFRAME(int32);
			CE_CHECKSIZE(setSize);
			CE_CHECKADDR(destAddr, setSize);
			memset(memStart + destAddr, setValue, setSize);
		}
		break;
		case CeOp_MemSet_Const:
		{
			auto destAddr = CE_GETFRAME(addr_ce);
			uint8 setValue = CE_GETINST(uint8);
			int32 setSize = CE_GETINST(int32);
			CE_CHECKSIZE(setSize);
			CE_CHECKADDR(destAddr, setSize);
			memset(memStart + destAddr, setValue, setSize);
		}
		break;
		case CeOp_MemCpy:
		{
			auto destAddr = CE_GETFRAME(addr_ce);
			auto srcAddr = CE_GETFRAME(addr_ce);
			int32 size = CE_GETFRAME(int32);
			CE_CHECKSIZE(size);
			CE_CHECKADDR(srcAddr, size);
			CE_CHECKADDR(destAddr, size);
			memcpy(memStart + destAddr, memStart + srcAddr, size);
		}
		break;
		case CeOp_FrameAddr_32:
		{
			auto& result = CE_GETFRAME(int32);
			auto addr = &CE_GETFRAME(uint8);
			result = addr - memStart;
		}
		break;
		case CeOp_FrameAddr_64:
		{
			//if (instPtr - ceFunction->mCode.mVals == 0x9c1)
			auto& result = CE_GETFRAME(int64);
			auto addr = &CE_GETFRAME(uint8);
			result = addr - memStart;
		}
		break;
		case CeOp_FrameAddrOfs_32:
		{
			auto& result = CE_GETFRAME(int32);
			auto addr = &CE_GETFRAME(uint8);
			int32 ofs = CE_GETINST(int32);
			result = (int32)(addr - memStart + ofs);
		}
		break;
		case CeOp_ConstData:
		{
			auto frameOfs = CE_GETINST(int32);
			int32 constIdx = CE_GETINST(int32);
			auto& constEntry = ceFunction->mConstStructTable[constIdx];

			if (constEntry.mBindExecuteId != mExecuteId)
			{
				PrepareConstStructEntry(constEntry);
				_FixVariables();
			}
			auto& buff = (constEntry.mFixedData.mSize > 0) ? constEntry.mFixedData : constEntry.mData;
			memcpy(framePtr + frameOfs, buff.mVals, buff.mSize);
		}
		break;
		case CeOp_ConstDataRef:
		{
			auto frameOfs = CE_GETINST(int32);
			int32 constIdx = CE_GETINST(int32);
			auto& constEntry = ceFunction->mConstStructTable[constIdx];
			if (constEntry.mBindExecuteId != mExecuteId)
			{
				PrepareConstStructEntry(constEntry);
				_FixVariables();

				auto& buff = (constEntry.mFixedData.mSize > 0) ? constEntry.mFixedData : constEntry.mData;

				addr_ce* constAddrPtr = NULL;
				if (mConstDataMap.TryAdd(constEntry.mHash, NULL, &constAddrPtr))
				{
					uint8* data = CeMalloc(buff.mSize);
					_FixVariables();
					memcpy(data, &buff[0], buff.mSize);
					*constAddrPtr = (addr_ce)(data - memStart);
				}

				constEntry.mAddr = *constAddrPtr;
				constEntry.mBindExecuteId = mExecuteId;
			}
			*(addr_ce*)(framePtr + frameOfs) = constEntry.mAddr;
		}
		break;
		case CeOp_Zero:
		{
			auto resultPtr = &CE_GETFRAME(uint8);
			int32 constSize = CE_GETINST(int32);
			memset(resultPtr, 0, constSize);
		}
		break;
		case CeOp_Const_8:
		{
			auto& result = CE_GETFRAME(int8);
			result = CE_GETINST(int8);
		}
		break;
		case CeOp_Const_16:
		{
			auto& result = CE_GETFRAME(int16);
			result = CE_GETINST(int16);
		}
		break;
		case CeOp_Const_32:
		{
			auto& result = CE_GETFRAME(int32);
			result = CE_GETINST(int32);
		}
		break;
		case CeOp_Const_64:
		{
			auto& result = CE_GETFRAME(int64);
			result = CE_GETINST(int64);
		}
		break;
		case CeOp_Const_X:
		{
			int32 constSize = CE_GETINST(int32);
			auto resultPtr = &CE_GETFRAME(uint8);
			memcpy(resultPtr, instPtr, constSize);
			instPtr += constSize;
		}
		break;
		case CeOp_Load_8:
			CE_LOAD(uint8);
			break;
		case CeOp_Load_16:
			CE_LOAD(uint16);
			break;
		case CeOp_Load_32:
			CE_LOAD(uint32);
			break;
		case CeOp_Load_64:
			CE_LOAD(uint64);			
			break;
		case CeOp_Load_X:
		{
			int32 size = CE_GETINST(int32);
			auto resultPtr = &CE_GETFRAME(uint8);
			auto ceAddr = CE_GETFRAME(addr_ce);
			CE_CHECKADDR(ceAddr, size);
			memcpy(resultPtr, memStart + ceAddr, size);
		}
		break;
		case CeOp_Store_8:
			CE_STORE(uint8);
			break;
		case CeOp_Store_16:
			CE_STORE(uint16);
			break;
		case CeOp_Store_32:
			CE_STORE(uint32);
			break;
		case CeOp_Store_64:
			CE_STORE(uint64);
			break;
		case CeOp_Store_X:
		{
			auto size = CE_GETINST(int32);
			auto srcPtr = &CE_GETFRAME(uint8);
			auto ceAddr = CE_GETFRAME(addr_ce);
			CE_CHECKADDR(ceAddr, size);
			memcpy(memStart + ceAddr, srcPtr, size);
		}
		break;
		case CeOp_Move_8:
			CEOP_MOVE(int8);
			break;
		case CeOp_Move_16:
			CEOP_MOVE(int16);
			break;
		case CeOp_Move_32:
			CEOP_MOVE(int32);
			break;
		case CeOp_Move_64:
			CEOP_MOVE(int64);
			break;
		case CeOp_Move_X:
		{
			int32 size = CE_GETINST(int32);
			auto valPtr = &CE_GETFRAME(uint8);
			auto destPtr = &CE_GETFRAME(uint8);
			memcpy(destPtr, valPtr, size);
		}
		break;
		case CeOp_Push_8:
			CEOP_PUSH(int8);
			break;
		case CeOp_Push_16:
			CEOP_PUSH(int16);
			break;
		case CeOp_Push_32:
			CEOP_PUSH(int32);
			break;
		case CeOp_Push_64:
			CEOP_PUSH(int64);
			break;
		case CeOp_Pop_8:
			CEOP_POP(int8);
			break;
		case CeOp_Pop_16:
			CEOP_POP(int16);
			break;
		case CeOp_Pop_32:
			CEOP_POP(int32);
			break;
		case CeOp_Pop_64:
			CEOP_POP(int64);
			break;
		case CeOp_Pop_X:
		{
			int32 size = CE_GETINST(int32);
			auto resultPtr = &CE_GETFRAME(uint8);
			memcpy(resultPtr, stackPtr, size);
			stackPtr += size;
		}
		break;
		case CeOp_AdjustSP:
		{
			int32 adjust = CE_GETFRAME(int32);
			stackPtr += adjust;
		}
		break;
		case CeOp_AdjustSPNeg:
		{
			int32 adjust = CE_GETFRAME(int32);
			stackPtr -= adjust;
		}
		break;
		case CeOp_AdjustSPConst:
		{
			int32 adjust = CE_GETINST(int32);
			stackPtr += adjust;			
		}
		break;
		case CeOp_GetSP:
		{
			auto& result = CE_GETFRAME(int32);
			result = stackPtr - memStart;
		}
		break;
		case CeOp_SetSP:
		{
			auto addr = CE_GETFRAME(int32);
			stackPtr = memStart + addr;
		}
		break;
		case CeOp_GetStaticField:
		{
			auto frameOfs = CE_GETINST(int32);
			int32 tableIdx = CE_GETINST(int32);

			CeFunction* ctorCallFunction = NULL;

			auto& ceStaticFieldEntry = ceFunction->mStaticFieldTable[tableIdx];
			if (ceStaticFieldEntry.mBindExecuteId != mExecuteId)
			{
				if (mStaticCtorExecSet.TryAdd(ceStaticFieldEntry.mTypeId, NULL))
				{
					auto bfType = GetBfType(ceStaticFieldEntry.mTypeId);
					BfTypeInstance* bfTypeInstance = NULL;
					if (bfType != NULL)
						bfTypeInstance = bfType->ToTypeInstance();
					if (bfTypeInstance == NULL)
					{
						_Fail("Invalid type");
						return false;
					}

					auto methodDef = bfTypeInstance->mTypeDef->GetMethodByName("__BfStaticCtor");
					if (methodDef == NULL)
					{
						_Fail("No static ctor found");
						return false;
					}

					auto moduleMethodInstance = ceModule->GetMethodInstance(bfTypeInstance, methodDef, BfTypeVector());
					if (!moduleMethodInstance)
					{
						_Fail("No static ctor instance found");
						return false;
					}

					bool added = false;
					ctorCallFunction = mCeMachine->GetFunction(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, added);
					if (!ctorCallFunction->mInitialized)
						mCeMachine->PrepareFunction(ctorCallFunction, NULL);
				}

				CeStaticFieldInfo* staticFieldInfo = NULL;
				mStaticFieldMap.TryAdd(ceStaticFieldEntry.mName, NULL, &staticFieldInfo);

				if (staticFieldInfo->mAddr == 0)
				{
					if (ceStaticFieldEntry.mSize < 0)
						_Fail(StrFormat("Reference to unsized global variable '%s'", ceStaticFieldEntry.mName.c_str()));

					CE_CHECKALLOC(ceStaticFieldEntry.mSize);
					uint8* ptr = CeMalloc(ceStaticFieldEntry.mSize);
					_FixVariables();
					if (ceStaticFieldEntry.mSize > 0)
						memset(ptr, 0, ceStaticFieldEntry.mSize);
					staticFieldInfo->mAddr = (addr_ce)(ptr - memStart);
				}

				ceStaticFieldEntry.mAddr = staticFieldInfo->mAddr;
				ceStaticFieldEntry.mBindExecuteId = mExecuteId;
			}

			*(addr_ce*)(framePtr + frameOfs) = ceStaticFieldEntry.mAddr;

			if (ctorCallFunction != NULL)
			{
				bool handled = false;
				if (!_CheckFunction(ctorCallFunction, handled))
					return false;
				if (handled)
					break;
				CE_CALL(ctorCallFunction);
			}
		}
		break;
		case CeOp_GetMethod:
		{
			BF_ASSERT(memStart == mMemory.mVals);

			auto resultFrameIdx = CE_GETINST(int32);
			int32 callIdx = CE_GETINST(int32);
			auto& callEntry = ceFunction->mCallTable[callIdx];
			if (callEntry.mBindRevision != mCeMachine->mMethodBindRevision)
			{
				callEntry.mFunction = NULL;
				//mNamedFunctionMap.TryGetValue(callEntry.mFunctionName, &callEntry.mFunction);

				if (callEntry.mFunctionInfo == NULL)
				{
					_Fail("Unable to locate function entry");
					return false;
				}

				if ((callEntry.mFunctionInfo->mCeFunction == NULL) && (!callEntry.mFunctionInfo->mMethodRef.IsNull()))
				{
					auto methodRef = callEntry.mFunctionInfo->mMethodRef;
					auto methodDef = methodRef.mTypeInstance->mTypeDef->mMethods[methodRef.mMethodNum];
					auto moduleMethodInstance = ceModule->GetMethodInstance(methodRef.mTypeInstance, methodDef,
						methodRef.mMethodGenericArguments);

					if (moduleMethodInstance)
					{
						mCeMachine->QueueMethod(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc);
					}
				}

				if (callEntry.mFunctionInfo->mCeFunction == NULL)
				{
					_Fail("Method not generated");
					return false;
				}

				callEntry.mFunction = callEntry.mFunctionInfo->mCeFunction;
				if (!callEntry.mFunction->mInitialized)
				{
					auto curFrame = _GetCurFrame();
					SetAndRestoreValue<CeFrame*> prevFrame(mCurFrame, &curFrame);
					mCeMachine->PrepareFunction(callEntry.mFunction, NULL);
				}

				if (callEntry.mFunction->mMethodInstance != NULL)
				{
					if (callEntry.mFunction->mMethodInstance->GetOwner()->IsDeleting())
					{
						_Fail("Calling method on deleted type");
						return false;
					}
				}

				callEntry.mBindRevision = mCeMachine->mMethodBindRevision;
			}

			BF_ASSERT(memStart == mMemory.mVals);
			auto callFunction = callEntry.mFunction;

			if (needsFunctionIds)
				*(int32*)(framePtr + resultFrameIdx) = callFunction->mId;
			else
				*(CeFunction**)(framePtr + resultFrameIdx) = callFunction;
		}
		break;
		case CeOp_GetMethod_Inner:
		{
			auto resultFrameIdx = CE_GETINST(int32);
			int32 innerIdx = CE_GETINST(int32);

			auto outerFunction = ceFunction;
			if (outerFunction->mCeInnerFunctionInfo != NULL)
				outerFunction = outerFunction->mCeInnerFunctionInfo->mOwner;
			auto callFunction = outerFunction->mInnerFunctions[innerIdx];
			if (needsFunctionIds)
				*(int32*)(framePtr + resultFrameIdx) = callFunction->mId;
			else
				*(CeFunction**)(framePtr + resultFrameIdx) = callFunction;
		}
		break;
		case CeOp_GetMethod_Virt:
		{
			auto resultFrameIdx = CE_GETINST(int32);
			auto valueAddr = CE_GETFRAME(addr_ce);
			int32 virtualIdx = CE_GETINST(int32);

			CE_CHECKADDR(valueAddr, sizeof(int32));
			int32 objTypeId = *(int32*)(memStart + valueAddr);
			BfType* bfType = GetBfType(objTypeId);
			if ((bfType == NULL) || (!bfType->IsObject()))
			{
				_Fail("Invalid virtual method target");
				return false;
			}

			auto valueType = bfType->ToTypeInstance();
			if (valueType->mVirtualMethodTable.IsEmpty())
				ceModule->PopulateType(valueType, BfPopulateType_DataAndMethods);
			auto methodInstance = (BfMethodInstance*)valueType->mVirtualMethodTable[virtualIdx].mImplementingMethod;

			auto callFunction = mCeMachine->GetPreparedFunction(methodInstance);
			if (needsFunctionIds)
				*(int32*)(framePtr + resultFrameIdx) = callFunction->mId;
			else
				*(CeFunction**)(framePtr + resultFrameIdx) = callFunction;
		}
		break;
		case CeOp_GetMethod_IFace:
		{
			auto resultFrameIdx = CE_GETINST(int32);
			auto valueAddr = CE_GETFRAME(addr_ce);
			int32 ifaceId = CE_GETINST(int32);
			int32 methodIdx = CE_GETINST(int32);

			auto ifaceType = ceModule->mContext->mTypes[ifaceId]->ToTypeInstance();

			CE_CHECKADDR(valueAddr, sizeof(int32));
			int32 objTypeId = *(int32*)(memStart + valueAddr);

			auto bfObjectType = GetBfType(objTypeId);
			if ((bfObjectType == NULL) || (!bfObjectType->IsTypeInstance()))
			{
				_Fail("Invalid object");
				return false;
			}

			auto valueType = bfObjectType->ToTypeInstance();
			BfMethodInstance* methodInstance = NULL;

			if (valueType != NULL)
			{
				if (valueType->mVirtualMethodTable.IsEmpty())
					ceModule->PopulateType(valueType, BfPopulateType_DataAndMethods);

				auto checkType = valueType;
				while (checkType != NULL)
				{
					for (auto& iface : checkType->mInterfaces)
					{
						if (iface.mInterfaceType == ifaceType)
						{
							methodInstance = valueType->mInterfaceMethodTable[iface.mStartInterfaceTableIdx + methodIdx].mMethodRef;
							break;
						}
					}
					checkType = checkType->mBaseType;
				}
			}

			if (methodInstance == NULL)
			{
				_Fail("Failed to invoke interface method");
				return false;
			}

			auto callFunction = mCeMachine->GetPreparedFunction(methodInstance);
			if (needsFunctionIds)
				*(int32*)(framePtr + resultFrameIdx) = callFunction->mId;
			else
				*(CeFunction**)(framePtr + resultFrameIdx) = callFunction;
		}
		break;
		case CeOp_Call:
		{
			callCount++;
			CeFunction* callFunction;
			if (needsFunctionIds)
			{
				int32 functionId = CE_GETFRAME(int32);
				callFunction = mCeMachine->mFunctionIdMap[functionId];
			}
			else
				callFunction = CE_GETFRAME(CeFunction*);

			bool handled = false;
			if (!_CheckFunction(callFunction, handled))
				return false;
			if (callFunction->mIsVarReturn)
				_Fail("Illegal call to method with 'var' return.");
			if (handled)
				break;

			CE_CALL(callFunction);
		}
		break;
		case CeOp_Conv_I8_I16:
			CE_CAST(int8, int16);
			break;
		case CeOp_Conv_I8_I32:
			CE_CAST(int8, int32);
			break;
		case CeOp_Conv_I8_I64:
			CE_CAST(int8, int64);
			break;
		case CeOp_Conv_I8_F32:
			CE_CAST(int8, float);
			break;
		case CeOp_Conv_I8_F64:
			CE_CAST(int8, double);
			break;
		case CeOp_Conv_I16_I32:
			CE_CAST(int16, int32);
			break;
		case CeOp_Conv_I16_I64:
			CE_CAST(int16, int64);
			break;
		case CeOp_Conv_I16_F32:
			CE_CAST(int16, float);
			break;
		case CeOp_Conv_I16_F64:
			CE_CAST(int16, double);
			break;
		case CeOp_Conv_I32_I64:
			CE_CAST(int32, int64);
			break;
		case CeOp_Conv_I32_F32:
			CE_CAST(int32, float);
			break;
		case CeOp_Conv_I32_F64:
			CE_CAST(int32, double);
			break;
		case CeOp_Conv_I64_F32:
			CE_CAST(int64, float);
			break;
		case CeOp_Conv_I64_F64:
			CE_CAST(int64, double);
			break;
		case CeOp_Conv_U8_U16:
			CE_CAST(uint8, uint16);
			break;
		case CeOp_Conv_U8_U32:
			CE_CAST(uint8, uint32);
			break;
		case CeOp_Conv_U8_U64:
			CE_CAST(uint8, uint64);
			break;
		case CeOp_Conv_U8_F32:
			CE_CAST(uint8, float);
			break;
		case CeOp_Conv_U8_F64:
			CE_CAST(uint8, double);
			break;
		case CeOp_Conv_U16_U32:
			CE_CAST(uint16, uint32);
			break;
		case CeOp_Conv_U16_U64:
			CE_CAST(uint16, uint64);
			break;
		case CeOp_Conv_U16_F32:
			CE_CAST(uint16, float);
			break;
		case CeOp_Conv_U16_F64:
			CE_CAST(uint16, double);
			break;
		case CeOp_Conv_U32_U64:
			CE_CAST(uint32, uint64);
			break;
		case CeOp_Conv_U32_F32:
			CE_CAST(uint32, float);
			break;
		case CeOp_Conv_U32_F64:
			CE_CAST(uint32, double);
			break;
		case CeOp_Conv_U64_F32:
			CE_CAST(uint64, float);
			break;
		case CeOp_Conv_U64_F64:
			CE_CAST(uint64, double);
			break;
		case CeOp_Conv_F32_I8:
			CE_CAST(float, int8);
			break;
		case CeOp_Conv_F32_I16:
			CE_CAST(float, int16);
			break;
		case CeOp_Conv_F32_I32:
			CE_CAST(float, int32);
			break;
		case CeOp_Conv_F32_I64:
			CE_CAST(float, int64);
			break;
		case CeOp_Conv_F32_F64:
			CE_CAST(float, double);
			break;
		case CeOp_Conv_F64_I8:
			CE_CAST(double, int8);
			break;
		case CeOp_Conv_F64_I16:
			CE_CAST(double, int16);
			break;
		case CeOp_Conv_F64_I32:
			CE_CAST(double, int32);
			break;
		case CeOp_Conv_F64_I64:
			CE_CAST(double, int64);
			break;
		case CeOp_Conv_F64_F32:
			CE_CAST(double, float);
			break;

		case CeOp_Abs_I8:
			{
				auto& result = CE_GETFRAME(int8);
				auto val = CE_GETFRAME(int8);
				result = (val < 0) ? -val : val;
			}
			break;
		case CeOp_Abs_I16:
			{
				auto& result = CE_GETFRAME(int16);
				auto val = CE_GETFRAME(int16);
				result = (val < 0) ? -val : val;
			}
			break;
		case CeOp_Abs_I32:
			{
				auto& result = CE_GETFRAME(int32);
				auto val = CE_GETFRAME(int32);
				result = (val < 0) ? -val : val;
			}
			break;
		case CeOp_Abs_I64:
			{
				auto& result = CE_GETFRAME(int64);
				auto val = CE_GETFRAME(int64);
				result = (val < 0) ? -val : val;
			}
			break;
		case CeOp_Abs_F32:
			CEOP_UNARY_FUNC(fabs, float);
			break;
		case CeOp_Abs_F64:
			CEOP_UNARY_FUNC(fabs, double);
			break;
		case CeOp_AddConst_I8:
			CEOP_BIN_CONST(+, int8);
			break;
		case CeOp_AddConst_I16:
			CEOP_BIN_CONST(+, int16);
			break;
		case CeOp_AddConst_I32:
			CEOP_BIN_CONST(+, int32);
			break;
		case CeOp_AddConst_I64:
			CEOP_BIN_CONST(+, int64);
			break;
		case CeOp_Add_I8:
			CEOP_BIN(+, int8);
			break;
		case CeOp_Add_I16:
			CEOP_BIN(+, int16);
			break;
		case CeOp_Add_I32:
			CEOP_BIN(+, int32);
			break;
		case CeOp_Add_I64:
			CEOP_BIN(+, int64);
			break;
		case CeOp_Add_F32:
			CEOP_BIN(+, float);
			break;
		case CeOp_Add_F64:
			CEOP_BIN(+, double);
			break;
		case CeOp_Sub_I8:
			CEOP_BIN(-, int8);
			break;
		case CeOp_Sub_I16:
			CEOP_BIN(-, int16);
			break;
		case CeOp_Sub_I32:
			CEOP_BIN(-, int32);
			break;
		case CeOp_Sub_I64:
			CEOP_BIN(-, int64);
			break;
		case CeOp_Sub_F32:
			CEOP_BIN(-, float);
			break;
		case CeOp_Sub_F64:
			CEOP_BIN(-, double);
			break;
		case CeOp_Mul_I8:
			CEOP_BIN(*, int8);
			break;
		case CeOp_Mul_I16:
			CEOP_BIN(*, int16);
			break;
		case CeOp_Mul_I32:
			CEOP_BIN(*, int32);
			break;
		case CeOp_Mul_I64:
			CEOP_BIN(*, int64);
			break;
		case CeOp_Mul_F32:
			CEOP_BIN(*, float);
			break;
		case CeOp_Mul_F64:
			CEOP_BIN(*, double);
			break;
		case CeOp_Div_I8:
			CEOP_BIN_DIV(/ , int8);
			break;
		case CeOp_Div_I16:
			CEOP_BIN_DIV(/ , int16);
			break;
		case CeOp_Div_I32:
			CEOP_BIN_DIV(/ , int32);
			break;
		case CeOp_Div_I64:
			CEOP_BIN_DIV(/ , int64);
			break;
		case CeOp_Div_F32:
			CEOP_BIN_DIV(/ , float);
			break;
		case CeOp_Div_F64:
			CEOP_BIN_DIV(/ , double);
			break;
		case CeOp_Div_U8:
			CEOP_BIN_DIV(/ , uint8);
			break;
		case CeOp_Div_U16:
			CEOP_BIN_DIV(/ , uint16);
			break;
		case CeOp_Div_U32:
			CEOP_BIN_DIV(/ , uint32);
			break;
		case CeOp_Div_U64:
			CEOP_BIN_DIV(/ , uint64);
			break;
		case CeOp_Mod_I8:
			CEOP_BIN_DIV(%, int8);
			break;
		case CeOp_Mod_I16:
			CEOP_BIN_DIV(%, int16);
			break;
		case CeOp_Mod_I32:
			CEOP_BIN_DIV(%, int32);
			break;
		case CeOp_Mod_I64:
			CEOP_BIN_DIV(%, int64);
			break;
		case CeOp_Mod_F32:
		{
			auto& result = CE_GETFRAME(float);
			auto lhs = CE_GETFRAME(float);
			auto rhs = CE_GETFRAME(float);
			if (rhs == 0)
			{
				_Fail("Division by zero");
				return false;
			}
			result = fmodf(lhs, rhs);
		}
		break;
		case CeOp_Mod_F64:
		{
			auto& result = CE_GETFRAME(double);
			auto lhs = CE_GETFRAME(double);
			auto rhs = CE_GETFRAME(double);
			if (rhs == 0)
			{
				_Fail("Division by zero");
				return false;
			}
			result = fmod(lhs, rhs);
		}
		break;
		case CeOp_Mod_U8:
			CEOP_BIN_DIV(%, uint8);
			break;
		case CeOp_Mod_U16:
			CEOP_BIN_DIV(%, uint16);
			break;
		case CeOp_Mod_U32:
			CEOP_BIN_DIV(%, uint32);
			break;
		case CeOp_Mod_U64:
			CEOP_BIN_DIV(%, uint64);
			break;

		case CeOp_And_I8:
			CEOP_BIN(&, uint8);
			break;
		case CeOp_And_I16:
			CEOP_BIN(&, uint16);
			break;
		case CeOp_And_I32:
			CEOP_BIN(&, uint32);
			break;
		case CeOp_And_I64:
			CEOP_BIN(&, uint64);
			break;
		case CeOp_Or_I8:
			CEOP_BIN(| , uint8);
			break;
		case CeOp_Or_I16:
			CEOP_BIN(| , uint16);
			break;
		case CeOp_Or_I32:
			CEOP_BIN(| , uint32);
			break;
		case CeOp_Or_I64:
			CEOP_BIN(| , uint64);
			break;
		case CeOp_Xor_I8:
			CEOP_BIN(^, uint8);
			break;
		case CeOp_Xor_I16:
			CEOP_BIN(^, uint16);
			break;
		case CeOp_Xor_I32:
			CEOP_BIN(^, uint32);
			break;
		case CeOp_Xor_I64:
			CEOP_BIN(^, uint64);
			break;
		case CeOp_Shl_I8:
			CEOP_BIN2(<< , int8, uint8);
			break;
		case CeOp_Shl_I16:
			CEOP_BIN2(<< , int16, uint8);
			break;
		case CeOp_Shl_I32:
			CEOP_BIN2(<< , int32, uint8);
			break;
		case CeOp_Shl_I64:
			CEOP_BIN2(<< , int64, uint8);
			break;
		case CeOp_Shr_I8:
			CEOP_BIN2(>> , int8, uint8);
			break;
		case CeOp_Shr_I16:
			CEOP_BIN2(>> , int16, uint8);
			break;
		case CeOp_Shr_I32:
			CEOP_BIN2(>> , int32, uint8);
			break;
		case CeOp_Shr_I64:
			CEOP_BIN2(>> , int64, uint8);
			break;
		case CeOp_Shr_U8:
			CEOP_BIN2(>> , uint8, uint8);
			break;
		case CeOp_Shr_U16:
			CEOP_BIN2(>> , uint16, uint8);
			break;
		case CeOp_Shr_U32:
			CEOP_BIN2(>> , uint32, uint8);
			break;
		case CeOp_Shr_U64:
			CEOP_BIN2(>> , uint64, uint8);
			break;
		
		case CeOp_Acos_F32:
			CEOP_UNARY_FUNC(acosf, float);
			break;
		case CeOp_Acos_F64:
			CEOP_UNARY_FUNC(acos, double);
			break;
		case CeOp_Asin_F32:
			CEOP_UNARY_FUNC(asinf, float);
			break;
		case CeOp_Asin_F64:
			CEOP_UNARY_FUNC(asin, double);
			break;
		case CeOp_Atan_F32:
			CEOP_UNARY_FUNC(atanf, float);
			break;
		case CeOp_Atan_F64:
			CEOP_UNARY_FUNC(atan, double);
			break;
		case CeOp_Atan2_F32:
			CEOP_BIN_FUNC(atan2f, float);
			break;
		case CeOp_Atan2_F64:
			CEOP_BIN_FUNC(atan2, double);
			break;
		case CeOp_Ceiling_F32:
			CEOP_UNARY_FUNC(ceilf, float);
			break;
		case CeOp_Ceiling_F64:
			CEOP_UNARY_FUNC(ceil, double);
			break;
		case CeOp_Cos_F32:
			CEOP_UNARY_FUNC(cosf, float);
			break;
		case CeOp_Cos_F64:
			CEOP_UNARY_FUNC(cos, double);
			break;
		case CeOp_Cosh_F32:
			CEOP_UNARY_FUNC(coshf, float);
			break;
		case CeOp_Cosh_F64:
			CEOP_UNARY_FUNC(cosh, double);
			break;
		case CeOp_Exp_F32:
			CEOP_UNARY_FUNC(expf, float);
			break;
		case CeOp_Exp_F64:
			CEOP_UNARY_FUNC(exp, double);
			break;
		case CeOp_Floor_F32:
			CEOP_UNARY_FUNC(floorf, float);
			break;
		case CeOp_Floor_F64:
			CEOP_UNARY_FUNC(floor, double);
			break;
		case CeOp_Log_F32:
			CEOP_UNARY_FUNC(logf, float);
			break;
		case CeOp_Log_F64:
			CEOP_UNARY_FUNC(log, double);
			break;
		case CeOp_Log10_F32:
			CEOP_UNARY_FUNC(log10f, float);
			break;
		case CeOp_Log10_F64:
			CEOP_UNARY_FUNC(log10, double);
			break;
		case CeOp_Pow_F32:
			CEOP_BIN_FUNC(powf, float);
			break;
		case CeOp_Pow_F64:
			CEOP_BIN_FUNC(pow, double);
			break;
		case CeOp_Round_F32:
			CEOP_UNARY_FUNC(roundf, float);
			break;
		case CeOp_Round_F64:
			CEOP_UNARY_FUNC(round, double);
			break;
		case CeOp_Sin_F32:
			CEOP_UNARY_FUNC(sinf, float);
			break;
		case CeOp_Sin_F64:
			CEOP_UNARY_FUNC(sin, double);
			break;
		case CeOp_Sinh_F32:
			CEOP_UNARY_FUNC(sinhf, float);
			break;
		case CeOp_Sinh_F64:
			CEOP_UNARY_FUNC(sinh, double);
			break;
		case CeOp_Sqrt_F32:
			CEOP_UNARY_FUNC(sqrtf, float);
			break;
		case CeOp_Sqrt_F64:
			CEOP_UNARY_FUNC(sqrt, double);
			break;
		case CeOp_Tan_F32:
			CEOP_UNARY_FUNC(tanf, float);
			break;
		case CeOp_Tan_F64:
			CEOP_UNARY_FUNC(tan, double);
			break;
		case CeOp_Tanh_F32:
			CEOP_UNARY_FUNC(tanhf, float);
			break;
		case CeOp_Tanh_F64:
			CEOP_UNARY_FUNC(tanh, double);
			break;
		
		case CeOp_Cmp_NE_I8:
			CEOP_CMP(!= , int8);
			break;
		case CeOp_Cmp_NE_I16:
			CEOP_CMP(!= , int16);
			break;
		case CeOp_Cmp_NE_I32:
			CEOP_CMP(!= , int32);
			break;
		case CeOp_Cmp_NE_I64:
			CEOP_CMP(!= , int64);
			break;
		case CeOp_Cmp_NE_F32:
			CEOP_CMP(!= , float);
			break;
		case CeOp_Cmp_NE_F64:
			CEOP_CMP(!= , double);
			break;
		case CeOp_Cmp_EQ_I8:
			CEOP_CMP(== , int8);
			break;
		case CeOp_Cmp_EQ_I16:
			CEOP_CMP(== , int16);
			break;
		case CeOp_Cmp_EQ_I32:
			CEOP_CMP(== , int32);
			break;
		case CeOp_Cmp_EQ_I64:
			CEOP_CMP(== , int64);
			break;
		case CeOp_Cmp_EQ_F32:
			CEOP_CMP(== , float);
			break;
		case CeOp_Cmp_EQ_F64:
			CEOP_CMP(== , double);
			break;
		case CeOp_Cmp_SLT_I8:
			CEOP_CMP(< , int8);
			break;
		case CeOp_Cmp_SLT_I16:
			CEOP_CMP(< , int16);
			break;
		case CeOp_Cmp_SLT_I32:
			CEOP_CMP(< , int32);
			break;
		case CeOp_Cmp_SLT_I64:
			CEOP_CMP(< , int64);
			break;
		case CeOp_Cmp_SLT_F32:
			CEOP_CMP(< , float);
			break;
		case CeOp_Cmp_SLT_F64:
			CEOP_CMP(< , double);
			break;
		case CeOp_Cmp_ULT_I8:
			CEOP_CMP(< , uint8);
			break;
		case CeOp_Cmp_ULT_I16:
			CEOP_CMP(< , uint16);
			break;
		case CeOp_Cmp_ULT_I32:
			CEOP_CMP(< , uint32);
			break;
		case CeOp_Cmp_ULT_I64:
			CEOP_CMP(< , uint64);
			break;
		case CeOp_Cmp_SLE_I8:
			CEOP_CMP(<= , int8);
			break;
		case CeOp_Cmp_SLE_I16:
			CEOP_CMP(<= , int16);
			break;
		case CeOp_Cmp_SLE_I32:
			CEOP_CMP(<= , int32);
			break;
		case CeOp_Cmp_SLE_I64:
			CEOP_CMP(<= , int64);
			break;
		case CeOp_Cmp_SLE_F32:
			CEOP_CMP(<= , float);
			break;
		case CeOp_Cmp_SLE_F64:
			CEOP_CMP(<= , double);
			break;
		case CeOp_Cmp_ULE_I8:
			CEOP_CMP(<= , uint8);
			break;
		case CeOp_Cmp_ULE_I16:
			CEOP_CMP(<= , uint16);
			break;
		case CeOp_Cmp_ULE_I32:
			CEOP_CMP(<= , uint32);
			break;
		case CeOp_Cmp_ULE_I64:
			CEOP_CMP(<= , uint64);
			break;
		case CeOp_Cmp_SGT_I8:
			CEOP_CMP(> , int8);
			break;
		case CeOp_Cmp_SGT_I16:
			CEOP_CMP(> , int16);
			break;
		case CeOp_Cmp_SGT_I32:
			CEOP_CMP(> , int32);
			break;
		case CeOp_Cmp_SGT_I64:
			CEOP_CMP(> , int64);
			break;
		case CeOp_Cmp_SGT_F32:
			CEOP_CMP(> , float);
			break;
		case CeOp_Cmp_SGT_F64:
			CEOP_CMP(> , double);
			break;
		case CeOp_Cmp_UGT_I8:
			CEOP_CMP(> , uint8);
			break;
		case CeOp_Cmp_UGT_I16:
			CEOP_CMP(> , uint16);
			break;
		case CeOp_Cmp_UGT_I32:
			CEOP_CMP(> , uint32);
			break;
		case CeOp_Cmp_UGT_I64:
			CEOP_CMP(> , uint64);
			break;
		case CeOp_Cmp_SGE_I8:
			CEOP_CMP(>= , int8);
			break;
		case CeOp_Cmp_SGE_I16:
			CEOP_CMP(>= , int16);
			break;
		case CeOp_Cmp_SGE_I32:
			CEOP_CMP(>= , int32);
			break;
		case CeOp_Cmp_SGE_I64:
			CEOP_CMP(>= , int64);
			break;
		case CeOp_Cmp_SGE_F32:
			CEOP_CMP(>= , float);
			break;
		case CeOp_Cmp_SGE_F64:
			CEOP_CMP(>= , double);
			break;
		case CeOp_Cmp_UGE_I8:
			CEOP_CMP(>= , uint8);
			break;
		case CeOp_Cmp_UGE_I16:
			CEOP_CMP(>= , uint16);
			break;
		case CeOp_Cmp_UGE_I32:
			CEOP_CMP(>= , uint32);
			break;
		case CeOp_Cmp_UGE_I64:
			CEOP_CMP(>= , uint64);
			break;
		case CeOp_Neg_I8:
			CEOP_UNARY(-, int8);
			break;
		case CeOp_Neg_I16:
			CEOP_UNARY(-, int16);
			break;
		case CeOp_Neg_I32:
			CEOP_UNARY(-, int32);
			break;
		case CeOp_Neg_I64:
			CEOP_UNARY(-, int64);
			break;
		case CeOp_Neg_F32:
			CEOP_UNARY(-, float);
		case CeOp_Neg_F64:
			CEOP_UNARY(-, double);
			break;
		case CeOp_Not_I1:
			CEOP_UNARY(!, bool);
			break;
		case CeOp_Not_I8:
			CEOP_UNARY(~, int8);
			break;
		case CeOp_Not_I16:
			CEOP_UNARY(~, int16);
			break;
		case CeOp_Not_I32:
			CEOP_UNARY(~, int32);
			break;
		case CeOp_Not_I64:
			CEOP_UNARY(~, int64);
			break;
		default:
			_Fail("Unhandled op");
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

CeMachine::CeMachine(BfCompiler* compiler)
{
	mCompiler = compiler;
	mCeModule = NULL;
	mRevision = 0;
	mMethodBindRevision = 0;
	mCurContext = NULL;
	mExecuteId = -1;

	mCurFunctionId = 0;
	mRevisionExecuteTime = 0;
	mCurBuilder = NULL;
	mPreparingFunction = NULL;

	mCurEmitContext = NULL;

	mAppendAllocInfo = NULL;
	mTempParser = NULL;
	mTempReducer = NULL;
	mTempPassInstance = NULL;

	BfLogSys(mCompiler->mSystem, "CeMachine::CeMachine %p\n", this);
}


CeMachine::~CeMachine()
{
	for (auto context : mContextList)
		delete context;

	delete mTempPassInstance;
	delete mTempParser;
	delete mTempReducer;
	delete mAppendAllocInfo;
	delete mCeModule;

	auto _RemoveFunctionInfo = [&](CeFunctionInfo* functionInfo)
	{
		if (functionInfo->mMethodInstance != NULL)
			functionInfo->mMethodInstance->mInCEMachine = false;

		if (functionInfo->mCeFunction != NULL)
		{
			// We don't need to actually unmap it at this point
			functionInfo->mCeFunction->mId = -1;
			for (auto innerFunction : functionInfo->mCeFunction->mInnerFunctions)
				innerFunction->mId = -1;
		}

		delete functionInfo;
	};

	for (auto kv : mNamedFunctionMap)
	{
		if (kv.mValue->mMethodInstance == NULL)
			_RemoveFunctionInfo(kv.mValue);
	}

	for (auto kv : mFunctions)
	{
		BF_ASSERT(kv.mValue != NULL);
		_RemoveFunctionInfo(kv.mValue);
	}
}

void CeMachine::Init()
{
	mCeModule = new BfModule(mCompiler->mContext, "__constEval");
	mCeModule->mIsSpecialModule = true;
	//mCeModule->mIsScratchModule = true;
	mCeModule->mIsComptimeModule = true;
	//mCeModule->mIsReified = true;
	if (mCompiler->mIsResolveOnly)
		mCeModule->mIsReified = true;
	else
		mCeModule->mIsReified = false;
	mCeModule->Init();

	mCeModule->mBfIRBuilder = new BfIRBuilder(mCeModule);
	mCeModule->mBfIRBuilder->mDbgVerifyCodeGen = true;
	mCeModule->FinishInit();
	mCeModule->mBfIRBuilder->mHasDebugInfo = false; // Only line info
	mCeModule->mBfIRBuilder->mIgnoreWrites = false;
	mCeModule->mWantsIRIgnoreWrites = false;
}

BeContext* CeMachine::GetBeContext()
{
	if (mCeModule == NULL)
		return NULL;
	return mCeModule->mBfIRBuilder->mBeIRCodeGen->mBeContext;
}

BeModule* CeMachine::GetBeModule()
{
	if (mCeModule == NULL)
		return NULL;
	return mCeModule->mBfIRBuilder->mBeIRCodeGen->mBeModule;
}

void CeMachine::CompileStarted()
{
	mRevisionExecuteTime = 0;
	mRevision++;
	mMethodBindRevision++;
	if (mCeModule != NULL)
	{
		delete mCeModule;
		mCeModule = NULL;
	}
}

void CeMachine::CompileDone()
{
	// So things like deleted local methods get rechecked
	mRevision++;
	mMethodBindRevision++;
	mTypeInfoMap.Clear();
	mMethodInstanceSet.Clear();
}

void CeMachine::DerefMethodInfo(CeFunctionInfo* ceFunctionInfo)
{
	ceFunctionInfo->mRefCount--;
	if (ceFunctionInfo->mRefCount > 0)
		return;
	BF_ASSERT(ceFunctionInfo->mMethodInstance == NULL);

	if (!ceFunctionInfo->mName.IsEmpty())
	{
		auto itr = mNamedFunctionMap.Find(ceFunctionInfo->mName);
		if (itr->mValue == ceFunctionInfo)
			mNamedFunctionMap.Remove(itr);
	}
	delete ceFunctionInfo;
}

void CeMachine::RemoveMethod(BfMethodInstance* methodInstance)
{
	BfLogSys(methodInstance->GetOwner()->mModule->mSystem, "CeMachine::RemoveMethod %p\n", methodInstance);

	mMethodBindRevision++;

	auto itr = mFunctions.Find(methodInstance);
	auto ceFunctionInfo = itr->mValue;
	BF_ASSERT(itr != mFunctions.end());
	if (itr != mFunctions.end())
	{
		if (ceFunctionInfo->mMethodInstance == methodInstance)
		{
			auto ceFunction = ceFunctionInfo->mCeFunction;
			for (auto& callEntry : ceFunction->mCallTable)
			{
				if (callEntry.mFunctionInfo != NULL)
					DerefMethodInfo(callEntry.mFunctionInfo);
			}
			if (ceFunction->mId != -1)
			{
				mFunctionIdMap.Remove(ceFunction->mId);
				ceFunction->mId = -1;
				for (auto innerFunction : ceFunction->mInnerFunctions)
				{
					mFunctionIdMap.Remove(innerFunction->mId);
					innerFunction->mId = -1;
				}
			}

			delete ceFunction;
			ceFunctionInfo->mCeFunction = NULL;
			ceFunctionInfo->mMethodInstance = NULL;

			if (methodInstance->mMethodDef->mIsLocalMethod)
			{
				// We can't rebuild these anyway
			}
			else if (ceFunctionInfo->mRefCount > 1)
			{
				// Generate a methodref
				ceFunctionInfo->mMethodRef = methodInstance;
			}

			DerefMethodInfo(ceFunctionInfo);
		}

		mFunctions.Remove(itr);
	}
}

CeErrorKind CeMachine::WriteConstant(CeConstStructData& data, BeConstant* constVal, CeContext* ceContext)
{
	auto ceModule = mCeModule;
	auto beType = constVal->GetType();
	if (auto globalVar = BeValueDynCast<BeGlobalVariable>(constVal))
	{
		if (globalVar->mName.StartsWith("__bfStrObj"))
		{
			int stringId = atoi(globalVar->mName.c_str() + 10);

			addr_ce stringAddr;
			if (data.mQueueFixups)
			{
				stringAddr = 0;
				CeConstStructFixup fixup;
				fixup.mKind = CeConstStructFixup::Kind_StringPtr;
				fixup.mValue = stringId;
				fixup.mOffset = (int)data.mData.mSize;
				data.mFixups.Add(fixup);
			}
			else
			{
				stringAddr = ceContext->GetString(stringId);
			}
			auto ptr = data.mData.GrowUninitialized(ceModule->mSystem->mPtrSize);
			int64 addr64 = stringAddr;
			memcpy(ptr, &addr64, ceModule->mSystem->mPtrSize);
			return CeErrorKind_None;
		}

		if (globalVar->mInitializer == NULL)
		{
			auto ptr = data.mData.GrowUninitialized(ceModule->mSystem->mPtrSize);
			int64 addr64 = (addr_ce)0;
			memcpy(ptr, &addr64, ceModule->mSystem->mPtrSize);
			return CeErrorKind_None;

			//TODO: Add this global variable in there and fixup

// 			CeStaticFieldInfo* staticFieldInfoPtr = NULL;
// 			if (mCeMachine->mStaticFieldMap.TryGetValue(globalVar->mName, &staticFieldInfoPtr))
// 			{
// 				CeStaticFieldInfo* staticFieldInfo = staticFieldInfoPtr;
// 
// 				int* staticFieldTableIdxPtr = NULL;
// 				if (mStaticFieldMap.TryAdd(globalVar, NULL, &staticFieldTableIdxPtr))
// 				{
// 					CeStaticFieldEntry staticFieldEntry;
// 					staticFieldEntry.mTypeId = staticFieldInfo->mFieldInstance->mOwner->mTypeId;
// 					staticFieldEntry.mName = globalVar->mName;
// 					staticFieldEntry.mSize = globalVar->mType->mSize;
// 					*staticFieldTableIdxPtr = (int)mCeFunction->mStaticFieldTable.size();
// 					mCeFunction->mStaticFieldTable.Add(staticFieldEntry);
// 				}
// 
// 				auto result = FrameAlloc(mCeMachine->GetBeContext()->GetPointerTo(globalVar->mType));
// 
// 				Emit(CeOp_GetStaticField);
// 				EmitFrameOffset(result);
// 				Emit((int32)*staticFieldTableIdxPtr);
// 
// 				return result;
// 			}
//			return CeErrorKind_GlobalVariable;
		}

		
		BF_ASSERT(!data.mQueueFixups);
		CeConstStructData gvData;
		
		auto result = WriteConstant(gvData, globalVar->mInitializer, ceContext);
		if (result != CeErrorKind_None)
			return result;

		uint8* gvPtr = ceContext->CeMalloc(gvData.mData.mSize);
		memcpy(gvPtr, gvData.mData.mVals, gvData.mData.mSize);

		auto ptr = data.mData.GrowUninitialized(ceModule->mSystem->mPtrSize);
		int64 addr64 = (addr_ce)(gvPtr - ceContext->mMemory.mVals);
		memcpy(ptr, &addr64, mCeModule->mSystem->mPtrSize);
		return CeErrorKind_None;
	}
	else if (auto beFunc = BeValueDynCast<BeFunction>(constVal))
	{
		return CeErrorKind_FunctionPointer;
	}
	else if (auto constStruct = BeValueDynCast<BeStructConstant>(constVal))
	{
		int startOfs = data.mData.mSize;
		if (constStruct->mType->mTypeCode == BeTypeCode_Struct)
		{
			BeStructType* structType = (BeStructType*)constStruct->mType;
			BF_ASSERT(structType->mMembers.size() == constStruct->mMemberValues.size());
			for (int memberIdx = 0; memberIdx < (int)constStruct->mMemberValues.size(); memberIdx++)
			{
				auto& member = structType->mMembers[memberIdx];
				// Do any per-member alignment				
				int wantZeroes = member.mByteOffset - (data.mData.mSize - startOfs);
				if (wantZeroes > 0)				
					data.mData.Insert(data.mData.size(), (uint8)0, wantZeroes);				

				auto result = WriteConstant(data, constStruct->mMemberValues[memberIdx], ceContext);
				if (result != CeErrorKind_None)
					return result;
			}
			// Do end padding
			data.mData.Insert(data.mData.size(), (uint8)0, structType->mSize - (data.mData.mSize - startOfs));
		}
		else if (constStruct->mType->mTypeCode == BeTypeCode_SizedArray)
		{
			for (auto& memberVal : constStruct->mMemberValues)
			{
				auto result = WriteConstant(data, memberVal, ceContext);
				if (result != CeErrorKind_None)
					return result;
			}
		}
		else
			BF_FATAL("Invalid StructConst type");
	}
	else if (auto constStr = BeValueDynCast<BeStringConstant>(constVal))
	{
		data.mData.Insert(data.mData.mSize, (uint8*)constStr->mString.c_str(), (int)constStr->mString.length() + 1);
	}
	else if (auto constCast = BeValueDynCast<BeCastConstant>(constVal))
	{
		auto result = WriteConstant(data, constCast->mTarget, ceContext);
		if (result != CeErrorKind_None)
			return result;
	}
	else if (auto constGep = BeValueDynCast<BeGEPConstant>(constVal))
	{
		if (auto globalVar = BeValueDynCast<BeGlobalVariable>(constGep->mTarget))
		{
			BF_ASSERT(constGep->mIdx0 == 0);

			int64 dataOfs = 0;
			if (globalVar->mType->mTypeCode == BeTypeCode_Struct)
			{
				auto structType = (BeStructType*)globalVar->mType;
				dataOfs = structType->mMembers[constGep->mIdx1].mByteOffset;
			}
			else if (globalVar->mType->mTypeCode == BeTypeCode_SizedArray)
			{
				auto arrayType = (BeSizedArrayType*)globalVar->mType;
				dataOfs = arrayType->mElementType->GetStride() * constGep->mIdx1;
			}
			else
			{
				BF_FATAL("Invalid GEP");
			}
			
			addr_ce addr = -1;

			if (globalVar->mName.StartsWith("__bfStrData"))
			{
				int stringId = atoi(globalVar->mName.c_str() + 11);

				if (data.mQueueFixups)
				{
					addr = 0;
					CeConstStructFixup fixup;
					fixup.mKind = CeConstStructFixup::Kind_StringCharPtr;
					fixup.mValue = stringId;
					fixup.mOffset = (int)data.mData.mSize;
					data.mFixups.Add(fixup);
				}
				else
				{
					addr_ce stringAddr = ceContext->GetString(stringId);
					BfTypeInstance* stringTypeInst = (BfTypeInstance*)ceModule->ResolveTypeDef(ceModule->mCompiler->mStringTypeDef, BfPopulateType_Data);
					addr = stringAddr + stringTypeInst->mInstSize;
				}
			}

			if (addr != -1)
			{
				auto ptr = data.mData.GrowUninitialized(ceModule->mSystem->mPtrSize);
				int64 addr64 = addr + dataOfs;
				memcpy(ptr, &addr64, ceModule->mSystem->mPtrSize);
				return CeErrorKind_None;
			}

			return CeErrorKind_GlobalVariable;

			// 			auto sym = GetSymbol(globalVar);
			// 
			// 			BeMCRelocation reloc;
			// 			reloc.mKind = BeMCRelocationKind_ADDR64;
			// 			reloc.mOffset = sect.mData.GetPos();
			// 			reloc.mSymTableIdx = sym->mIdx;
			// 			sect.mRelocs.push_back(reloc);
			// 			sect.mData.Write((int64)dataOfs);
		}
		else
		{
			BF_FATAL("Invalid GEPConstant");
		}
	}
	
	/*else if ((beType->IsPointer()) && (constVal->mTarget != NULL))
	{
		auto result = WriteConstant(arr, constVal->mTarget);
		if (result != CeErrorKind_None)
			return result;
	}
	else if (beType->IsComposite())
	{
		BF_ASSERT(constVal->mInt64 == 0);

		int64 zero = 0;
		int sizeLeft = beType->mSize;
		while (sizeLeft > 0)
		{
			int writeSize = BF_MIN(sizeLeft, 8);
			auto ptr = arr.GrowUninitialized(writeSize);
			memset(ptr, 0, writeSize);
			sizeLeft -= writeSize;
		}
	}*/

	else if (BeValueDynCastExact<BeConstant>(constVal) != NULL)
	{
		if (constVal->mType->IsStruct())
		{
			if (constVal->mType->mSize > 0)
			{
				auto ptr = data.mData.GrowUninitialized(constVal->mType->mSize);
				memset(ptr, 0, constVal->mType->mSize);				
			}			
		}
		else
		{
			auto ptr = data.mData.GrowUninitialized(beType->mSize);
			memcpy(ptr, &constVal->mInt64, beType->mSize);			
		}
	}
	else
		return CeErrorKind_Error;

	return CeErrorKind_None;
}

void CeMachine::CheckFunctionKind(CeFunction* ceFunction)
{
	ceFunction->mFunctionKind = CeFunctionKind_Normal;

	if (ceFunction->mMethodInstance != NULL)
	{
		auto methodDef = ceFunction->mMethodInstance->mMethodDef;
		if (methodDef->mIsExtern)
		{
			ceFunction->mFunctionKind = CeFunctionKind_Extern;

			auto owner = ceFunction->mMethodInstance->GetOwner();
			if (owner == mCeModule->mContext->mBfObjectType)
			{
				if (methodDef->mName == "Comptime_GetType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetReflectType;
				}
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mTypeTypeDef))
			{
				if (methodDef->mName == "Comptime_GetTypeById")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetReflectTypeById;
				}
				else if (methodDef->mName == "Comptime_GetTypeByName")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetReflectTypeByName;
				}
				else if (methodDef->mName == "Comptime_GetSpecializedType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetReflectSpecializedType;
				}
				else if (methodDef->mName == "Comptime_Type_GetCustomAttribute")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Type_GetCustomAttribute;
				}
				else if (methodDef->mName == "Comptime_GetMethod")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetMethod;
				}
				else if (methodDef->mName == "Comptime_GetMethodCount")
				{
					ceFunction->mFunctionKind = CeFunctionKind_GetMethodCount;
				}
				else if (methodDef->mName == "Comptime_Method_ToString")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_ToString;
				}
				else if (methodDef->mName == "Comptime_Method_GetName")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetName;
				}
				else if (methodDef->mName == "Comptime_Method_GetInfo")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetInfo;
				}
				else if (methodDef->mName == "Comptime_Method_GetParamInfo")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetParamInfo;
				}
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mCompilerTypeDef))
			{
				if (methodDef->mName == "Comptime_EmitTypeBody")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitTypeBody;
				}
				else if (methodDef->mName == "Comptime_EmitMethodEntry")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitMethodEntry;
				}
				else if (methodDef->mName == "Comptime_EmitMethodExit")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitMethodExit;
				}
				else if (methodDef->mName == "Comptime_EmitMixin")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitMixin;
				}
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mDiagnosticsDebugTypeDef))
			{
				if (methodDef->mName == "Write")
				{
					if (ceFunction->mMethodInstance->GetParamCount() == 1)
						ceFunction->mFunctionKind = CeFunctionKind_DebugWrite_Int;
					else
						ceFunction->mFunctionKind = CeFunctionKind_DebugWrite;
				}
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mThreadTypeDef))
			{
				if (methodDef->mName == "SleepInternal")
					ceFunction->mFunctionKind = CeFunctionKind_Sleep;
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mInternalTypeDef))
			{
				if (methodDef->mName == "ThrowIndexOutOfRange")
					ceFunction->mFunctionKind = CeFunctionKind_OOB;
				else if (methodDef->mName == "FatalError")
					ceFunction->mFunctionKind = CeFunctionKind_FatalError;
				else if (methodDef->mName == "Dbg_RawAlloc")
					ceFunction->mFunctionKind = CeFunctionKind_Malloc;
				else if (methodDef->mName == "Dbg_RawFree")
					ceFunction->mFunctionKind = CeFunctionKind_Free;
				else if (methodDef->mName == "ObjectDynCheckFailed")
					ceFunction->mFunctionKind = CeFunctionKind_DynCheckFailed;
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mPlatformTypeDef))
			{
				if (methodDef->mName == "BfpSystem_GetTimeStamp")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSystem_GetTimeStamp;
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mChar32TypeDef))
			{
				if (methodDef->mName == "get__ToLower")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_ToLower;
				else if (methodDef->mName == "get__ToUpper")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_ToUpper;
				else if (methodDef->mName == "get__IsLower")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsLower;
				else if (methodDef->mName == "get__IsUpper")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsUpper;
				else if (methodDef->mName == "get__IsWhiteSpace_EX")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsWhiteSpace_EX;
				else if (methodDef->mName == "get__IsLetter")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsLetter;
				else if (methodDef->mName == "get__IsLetterOrDigit")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsLetterOrDigit;
				else if (methodDef->mName == "get__IsNumer")
					ceFunction->mFunctionKind = CeFunctionKind_Char32_IsNumber;
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mDoubleTypeDef))
			{
				if (methodDef->mName == "strtod")
					ceFunction->mFunctionKind = CeFunctionKind_Double_Strtod;
				else if (methodDef->mName == "ftoa")
					ceFunction->mFunctionKind = CeFunctionKind_Double_Ftoa;
				if (methodDef->mName == "ToString")
					ceFunction->mFunctionKind = CeFunctionKind_Double_ToString;				
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mMathTypeDef))
			{
				if (methodDef->mName == "Abs")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Abs;
				if (methodDef->mName == "Acos")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Acos;
				if (methodDef->mName == "Asin")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Asin;
				if (methodDef->mName == "Atan")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Atan;
				if (methodDef->mName == "Atan2")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Atan2;
				if (methodDef->mName == "Ceiling")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Ceiling;
				if (methodDef->mName == "Cos")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Cos;
				if (methodDef->mName == "Cosh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Cosh;
				if (methodDef->mName == "Exp")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Exp;
				if (methodDef->mName == "Floor")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Floor;
				if (methodDef->mName == "Log")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Log;
				if (methodDef->mName == "Log10")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Log10;
				if (methodDef->mName == "Mod")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Mod;
				if (methodDef->mName == "Pow")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Pow;
				if (methodDef->mName == "Round")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Round;
				if (methodDef->mName == "Sin")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sin;
				if (methodDef->mName == "Sinh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sinh;
				if (methodDef->mName == "Sqrt")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sqrt;
				if (methodDef->mName == "Tan")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Tan;
				if (methodDef->mName == "Tanh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Tanh;
			}

			ceFunction->mInitialized = true;
			return;
		}
	}
}

void CeMachine::PrepareFunction(CeFunction* ceFunction, CeBuilder* parentBuilder)
{
	AutoTimer autoTimer(mRevisionExecuteTime);
	SetAndRestoreValue<CeFunction*> prevCEFunction(mPreparingFunction, ceFunction);	

	if (ceFunction->mFunctionKind == CeFunctionKind_NotSet)
		CheckFunctionKind(ceFunction);		

	BF_ASSERT(!ceFunction->mInitialized);
	ceFunction->mInitialized = true;
	ceFunction->mGenerating = true;	

	CeBuilder ceBuilder;
	SetAndRestoreValue<CeBuilder*> prevBuilder(mCurBuilder, &ceBuilder);
	ceBuilder.mParentBuilder = parentBuilder;
	ceBuilder.mPtrSize = mCeModule->mCompiler->mSystem->mPtrSize;
	ceBuilder.mCeMachine = this;
	ceBuilder.mCeFunction = ceFunction;	
	ceBuilder.Build();

	ceFunction->mGenerating = false;

	/*if (!ceFunction->mCode.IsEmpty())
	{
		CeDumpContext dumpCtx;
		dumpCtx.mCeFunction = ceFunction;
		dumpCtx.mStart = &ceFunction->mCode[0];
		dumpCtx.mPtr = dumpCtx.mStart;
		dumpCtx.mEnd = dumpCtx.mPtr + ceFunction->mCode.mSize;
		dumpCtx.Dump();

		OutputDebugStrF("Code for %s:\n%s\n", ceBuilder.mBeFunction->mName.c_str(), dumpCtx.mStr.c_str());
	}*/
}

void CeMachine::MapFunctionId(CeFunction* ceFunction)
{	
	if (mCeModule->mSystem->mPtrSize == 8)
		return;
	ceFunction->mId = ++mCurFunctionId;
	mFunctionIdMap[ceFunction->mId] = ceFunction;
}

CeFunction* CeMachine::GetFunction(BfMethodInstance* methodInstance, BfIRValue func, bool& added)
{	
	if (func)
	{
		if ((func.IsConst()) || (func.IsFake()))
			return NULL;
	}

	CeFunctionInfo** functionInfoPtr = NULL;
	CeFunctionInfo* ceFunctionInfo = NULL;
	CeFunction* ceFunction = NULL;
	if (!mFunctions.TryAdd(methodInstance, NULL, &functionInfoPtr))	
	{
		ceFunctionInfo = *functionInfoPtr;
		BF_ASSERT(ceFunctionInfo->mCeFunction != NULL);
		return ceFunctionInfo->mCeFunction;
	}

	BF_ASSERT(!methodInstance->mInCEMachine);
	methodInstance->mInCEMachine = true;

	BfLogSys(mCeModule->mSystem, "CeMachine::GetFunction %p\n", methodInstance);
	
	if (!func)
	{
		ceFunctionInfo = new CeFunctionInfo();		
	}
	else
	{
		auto funcVal = mCeModule->mBfIRBuilder->mBeIRCodeGen->GetBeValue(func.mId);

		if (auto function = BeValueDynCast<BeFunction>(funcVal))
		{
			CeFunctionInfo** namedFunctionInfoPtr = NULL;
			if (mNamedFunctionMap.TryAdd(function->mName, NULL, &namedFunctionInfoPtr))
			{
				ceFunctionInfo = new CeFunctionInfo();
				ceFunctionInfo->mName = function->mName;
				*namedFunctionInfoPtr = ceFunctionInfo;
			}
			else
			{
				ceFunctionInfo = *namedFunctionInfoPtr;
			}
		}
		else
		{
			ceFunctionInfo = new CeFunctionInfo();
		}
	}

	ceFunctionInfo->mRefCount++;
	*functionInfoPtr = ceFunctionInfo;

	if (ceFunctionInfo->mMethodInstance == NULL)
	{
		added = true;
		auto module = methodInstance->GetOwner()->mModule;
		
		BF_ASSERT(ceFunctionInfo->mCeFunction == NULL);

		ceFunction = new CeFunction();		
		ceFunction->mCeMachine = this;
		ceFunction->mIsVarReturn = methodInstance->mReturnType->IsVar();
		ceFunction->mCeFunctionInfo = ceFunctionInfo;
		ceFunction->mMethodInstance = methodInstance;		
		ceFunctionInfo->mMethodInstance = methodInstance;
		ceFunctionInfo->mCeFunction = ceFunction;		
		MapFunctionId(ceFunction);
	}
	

	return ceFunction;
}

CeFunction* CeMachine::GetPreparedFunction(BfMethodInstance* methodInstance)
{
	bool added = false;
	auto ceFunction = GetFunction(methodInstance, BfIRValue(), added);
	if (ceFunction == NULL)
		return NULL;
	if (!ceFunction->mInitialized)
		PrepareFunction(ceFunction, NULL);
	return ceFunction;
}

CeTypeInfo* CeMachine::GetTypeInfo(BfType* type)
{
	if (type == NULL)
		return NULL;

	auto typeInstance = type->ToTypeInstance();
	if (typeInstance == NULL)
		return NULL;

	CeTypeInfo* ceTypeInfo = NULL;
	if (!mTypeInfoMap.TryAdd(type, NULL, &ceTypeInfo))
	{
		if (ceTypeInfo->mRevision == typeInstance->mRevision)
			return ceTypeInfo;
		ceTypeInfo->mMethodInstances.Clear();
	}
	
	mCeModule->PopulateType(typeInstance, BfPopulateType_DataAndMethods);
	ceTypeInfo->mRevision = typeInstance->mRevision;
	for (auto& methodGroup : typeInstance->mMethodInstanceGroups)
	{
		if (methodGroup.mDefault != NULL)
		{
			mMethodInstanceSet.Add(methodGroup.mDefault);
			ceTypeInfo->mMethodInstances.Add(methodGroup.mDefault);
		}
		if (methodGroup.mMethodSpecializationMap != NULL)
		{
			for (auto& kv : *methodGroup.mMethodSpecializationMap)
			{
				mMethodInstanceSet.Add(kv.mValue);
				ceTypeInfo->mMethodInstances.Add(kv.mValue);
			}
		}
	}	
	return ceTypeInfo;
}

BfMethodInstance* CeMachine::GetMethodInstance(int64 methodHandle)
{
	BfMethodInstance* methodInstance = (BfMethodInstance*)(intptr)methodHandle;
	if (!mMethodInstanceSet.Contains(methodInstance))
		return NULL;
	return methodInstance;
}

void CeMachine::QueueMethod(BfMethodInstance* methodInstance, BfIRValue func)
{	
	if (mPreparingFunction != NULL)
	{
		auto curOwner = mPreparingFunction->mMethodInstance->GetOwner();
		curOwner->mModule->AddDependency(methodInstance->GetOwner(), curOwner, BfDependencyMap::DependencyFlag_ConstEval);
	}

	bool added = false;
	auto ceFunction = GetFunction(methodInstance, func, added);
}

void CeMachine::QueueMethod(BfModuleMethodInstance moduleMethodInstance)
{
	QueueMethod(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc);
}

void CeMachine::QueueStaticField(BfFieldInstance* fieldInstance, const StringImpl& mangledFieldName)
{	
	if (mCurBuilder != NULL)
		mCurBuilder->mStaticFieldInstanceMap[mangledFieldName] = fieldInstance;
}

void CeMachine::SetAppendAllocInfo(BfModule* module, BfIRValue allocValue, BfIRValue appendSizeValue)
{
	delete mAppendAllocInfo;
	mAppendAllocInfo = new CeAppendAllocInfo();
	mAppendAllocInfo->mModule = module;
	mAppendAllocInfo->mAllocValue = allocValue;
	mAppendAllocInfo->mAppendSizeValue = appendSizeValue;
}

void CeMachine::ClearAppendAllocInfo()
{
	delete mAppendAllocInfo;
	mAppendAllocInfo = NULL;
}

CeContext* CeMachine::AllocContext()
{
	CeContext* ceContext = NULL;
	if (!mContextList.IsEmpty())
	{
		ceContext = mContextList.back();
		mContextList.pop_back();
	}
	else
	{
		ceContext = new CeContext();
		ceContext->mCeMachine = this;
		ceContext->mMemory.Reserve(BF_CE_INITIAL_MEMORY);
	}

	ceContext->mCurEmitContext = mCurEmitContext;
	mCurEmitContext = NULL;
	mExecuteId++;	
	ceContext->mMemory.Resize(BF_CE_STACK_SIZE);
	ceContext->mExecuteId = mExecuteId;
	return ceContext;
}

void CeMachine::ReleaseContext(CeContext* ceContext)
{
	ceContext->mStringMap.Clear();
	ceContext->mReflectMap.Clear();
	ceContext->mConstDataMap.Clear();
	ceContext->mMemory.Clear();
	if (ceContext->mMemory.mAllocSize > BF_CE_MAX_CARRYOVER_MEMORY)
		ceContext->mMemory.Dispose();	
	ceContext->mStaticCtorExecSet.Clear();
	ceContext->mStaticFieldMap.Clear();
	ceContext->mHeap->Clear(BF_CE_MAX_CARRYOVER_HEAP);
	ceContext->mReflectTypeIdOffset = -1;	
	mCurEmitContext = ceContext->mCurEmitContext;
	ceContext->mCurEmitContext = NULL;
	mContextList.Add(ceContext);
}

BfTypedValue CeMachine::Call(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags, BfType* expectingType)
{	
	auto ceContext = AllocContext();
	auto result = ceContext->Call(targetSrc, module, methodInstance, args, flags, expectingType);
	ReleaseContext(ceContext);	
	return result;
}
