#include "CeMachine.h"
#include "CeDebugger.h"
#include "BfModule.h"
#include "BfCompiler.h"
#include "BfIRBuilder.h"
#include "BfParser.h"
#include "BfReducer.h"
#include "BfExprEvaluator.h"
#include "BfResolvePass.h"
#include "../Backend/BeIRCodeGen.h"
#include "BeefySysLib/platform/PlatformHelper.h"
#include "../DebugManager.h"

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
	{OPNAME "_8", OPINFOA##8}, \
	{OPNAME "_16", OPINFOA##16}, \
	{OPNAME "_32", OPINFOA##32}, \
	{OPNAME "_64", OPINFOA##64}, \
	{OPNAME "_X", OPINFOA, CEOI_None, CEOI_None, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_8", OPINFOA##8, OPINFOB##8}, \
	{OPNAME "_16", OPINFOA##16, OPINFOB##16}, \
	{OPNAME "_32", OPINFOA##32, OPINFOB##32}, \
	{OPNAME "_64", OPINFOA##64, OPINFOB##64}, \
	{OPNAME "_X",  OPINFOA, OPINFOB, CEOI_None, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_8", OPINFOA##8, OPINFOB##8, OPINFOC##8}, \
	{OPNAME "_16", OPINFOA##16, OPINFOB##16, OPINFOC##16}, \
	{OPNAME "_32", OPINFOA##32, OPINFOB##32, OPINFOC##32}, \
	{OPNAME "_64", OPINFOA##64, OPINFOB##64, OPINFOC##64}, \
	{OPNAME "_X", OPINFOA, OPINFOB, OPINFOC, CEOI_None, CeOpInfoFlag_SizeX}

#define CEOPINFO_SIZED_NUMERIC_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_I8", OPINFOA##8, OPINFOB##8}, \
	{OPNAME "_I16", OPINFOA##16, OPINFOB##16}, \
	{OPNAME "_I32", OPINFOA##32, OPINFOB##32}, \
	{OPNAME "_I64", OPINFOA##64, OPINFOB##64}
#define CEOPINFO_SIZED_NUMERIC_PLUSF_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_I8", OPINFOA##8, OPINFOB##8}, \
	{OPNAME "_I16", OPINFOA##16, OPINFOB##16}, \
	{OPNAME "_I32", OPINFOA##32, OPINFOB##32}, \
	{OPNAME "_I64", OPINFOA##64, OPINFOB##64}, \
	{OPNAME "_F32", OPINFOA##F32, OPINFOB##F64}, \
	{OPNAME "_F64", OPINFOA##F64, OPINFOB##F64}
#define CEOPINFO_SIZED_NUMERIC_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_I8", OPINFOA##8, OPINFOB##8, OPINFOC##8}, \
	{OPNAME "_I16", OPINFOA##16, OPINFOB##16, OPINFOC##16}, \
	{OPNAME "_I32", OPINFOA##32, OPINFOB##32, OPINFOC##32}, \
	{OPNAME "_I64", OPINFOA##64, OPINFOB##64, OPINFOC##64}
#define CEOPINFO_SIZED_UNUMERIC_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_U8", OPINFOA##8, OPINFOB##8, OPINFOC##8}, \
	{OPNAME "_U16", OPINFOA##16, OPINFOB##16, OPINFOC##16}, \
	{OPNAME "_U32", OPINFOA##32, OPINFOB##32, OPINFOC##32}, \
	{OPNAME "_U64", OPINFOA##64, OPINFOB##64, OPINFOC##64}
#define CEOPINFO_SIZED_NUMERIC_PLUSF_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_I8", OPINFOA##8, OPINFOB##8, OPINFOC##8}, \
	{OPNAME "_I16", OPINFOA##16, OPINFOB##16, OPINFOC##16}, \
	{OPNAME "_I32", OPINFOA##32, OPINFOB##32, OPINFOC##32}, \
	{OPNAME "_I64", OPINFOA##64, OPINFOB##64, OPINFOC##64}, \
	{OPNAME "_F32", OPINFOA##F32, OPINFOB##F32, OPINFOC##F32}, \
	{OPNAME "_F64", OPINFOA##F64, OPINFOB##F64, OPINFOC##F64}
#define CEOPINFO_SIZED_FLOAT_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_F32", OPINFOA##F32, OPINFOB##F32}, \
	{OPNAME "_F64", OPINFOA##F64, OPINFOB##F64}
#define CEOPINFO_SIZED_FLOAT_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_F32", OPINFOA##F32, OPINFOB##F32, OPINFOC##F32}, \
	{OPNAME "_F64", OPINFOA##F64, OPINFOB##F64, OPINFOC##F64}

static CeOpInfo gOpInfo[] =
{
	{"InvalidOp"},
	{"Nop"},
	{"DbgBreak"},
	{"Ret"},
	{"SetRet", CEOI_None, CEOI_IMM32},
	{"Jmp", CEOI_None, CEOI_JMPREL},
	{"JmpIf", CEOI_None, CEOI_JMPREL, CEOI_FrameRef8},
	{"JmpIfNot", CEOI_None, CEOI_JMPREL, CEOI_FrameRef8},
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

	{"Const_8", CEOI_FrameRef8, CEOI_IMM8},
	{"Const_16", CEOI_FrameRef16, CEOI_IMM16},
	{"Const_32", CEOI_FrameRef32, CEOI_IMM32},
	{"Const_64", CEOI_FrameRef64, CEOI_IMM64},
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

	{"CeOp_Conv_I8_I16", CEOI_FrameRef16, CEOI_FrameRef8},
	{"CeOp_Conv_I8_I32", CEOI_FrameRef32, CEOI_FrameRef8},
	{"CeOp_Conv_I8_I64", CEOI_FrameRef64, CEOI_FrameRef8},
	{"CeOp_Conv_I8_F32", CEOI_FrameRefF32, CEOI_FrameRef8},
	{"CeOp_Conv_I8_F64", CEOI_FrameRefF64, CEOI_FrameRef8},
	{"CeOp_Conv_I16_I32", CEOI_FrameRef32, CEOI_FrameRef16},
	{"CeOp_Conv_I16_I64", CEOI_FrameRef64, CEOI_FrameRef16},
	{"CeOp_Conv_I16_F32", CEOI_FrameRefF32, CEOI_FrameRef16},
	{"CeOp_Conv_I16_F64", CEOI_FrameRefF64, CEOI_FrameRef16},
	{"CeOp_Conv_I32_I64", CEOI_FrameRef64, CEOI_FrameRef32},
	{"CeOp_Conv_I32_F32", CEOI_FrameRefF32, CEOI_FrameRef32},
	{"CeOp_Conv_I32_F64", CEOI_FrameRefF64, CEOI_FrameRef32},
	{"CeOp_Conv_I64_F32", CEOI_FrameRefF32, CEOI_FrameRef64},
	{"CeOp_Conv_I64_F64", CEOI_FrameRefF64, CEOI_FrameRef64},
	{"CeOp_Conv_U8_U16", CEOI_FrameRef16, CEOI_FrameRef8},
	{"CeOp_Conv_U8_U32", CEOI_FrameRef32, CEOI_FrameRef8},
	{"CeOp_Conv_U8_U64", CEOI_FrameRef64, CEOI_FrameRef8},
	{"CeOp_Conv_U8_F32", CEOI_FrameRefF32, CEOI_FrameRef8},
	{"CeOp_Conv_U8_F64", CEOI_FrameRefF64, CEOI_FrameRef8},
	{"CeOp_Conv_U16_U32", CEOI_FrameRef32, CEOI_FrameRef16},
	{"CeOp_Conv_U16_U64", CEOI_FrameRef64, CEOI_FrameRef16},
	{"CeOp_Conv_U16_F32", CEOI_FrameRefF32, CEOI_FrameRef16},
	{"CeOp_Conv_U16_F64", CEOI_FrameRefF64, CEOI_FrameRef16},
	{"CeOp_Conv_U32_U64", CEOI_FrameRef64, CEOI_FrameRef32},
	{"CeOp_Conv_U32_F32", CEOI_FrameRefF32, CEOI_FrameRef32},
	{"CeOp_Conv_U32_F64", CEOI_FrameRefF64, CEOI_FrameRef32},
	{"CeOp_Conv_U64_F32", CEOI_FrameRefF32, CEOI_FrameRef64},
	{"CeOp_Conv_U64_F64", CEOI_FrameRefF64, CEOI_FrameRef64},
	{"CeOp_Conv_F32_I8",  CEOI_FrameRef8, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_I16", CEOI_FrameRef16, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_I32", CEOI_FrameRef32, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_I64", CEOI_FrameRef64, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_U8", CEOI_FrameRef8, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_U16", CEOI_FrameRef16, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_U32", CEOI_FrameRef32, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_U64", CEOI_FrameRef64, CEOI_FrameRefF32},
	{"CeOp_Conv_F32_F64", CEOI_FrameRefF64, CEOI_FrameRefF32},
	{"CeOp_Conv_F64_I8", CEOI_FrameRef8, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_I16", CEOI_FrameRef16, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_I32", CEOI_FrameRef32, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_I64", CEOI_FrameRef64, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_U8", CEOI_FrameRef8, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_U16", CEOI_FrameRef16, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_U32", CEOI_FrameRef32, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_U64", CEOI_FrameRef64, CEOI_FrameRefF64},
	{"CeOp_Conv_F64_F32", CEOI_FrameRefF32, CEOI_FrameRefF64},

	CEOPINFO_SIZED_NUMERIC_PLUSF_2("Abs", CEOI_FrameRef, CEOI_FrameRef),
	{"AddConst_I8", CEOI_FrameRef8, CEOI_FrameRef8, CEOI_IMM8},
	{"AddConst_I16", CEOI_FrameRef16, CEOI_FrameRef16, CEOI_IMM16},
	{"AddConst_I32", CEOI_FrameRef32, CEOI_FrameRef32, CEOI_IMM32},
	{"AddConst_I64", CEOI_FrameRef64, CEOI_FrameRef64, CEOI_IMM64},
	{"AddConst_F32", CEOI_FrameRefF32, CEOI_FrameRefF32, CEOI_IMMF32},
	{"AddConst_F64", CEOI_FrameRefF64, CEOI_FrameRefF64, CEOI_IMMF64},
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
	{"Not_I1", CEOI_FrameRef8, CEOI_FrameRef8},
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

String CeDbgMethodRef::ToString()
{
	if (!mMethodRef)
		return mNameMod;

	BfMethodInstance* methodInstance = mMethodRef;
	auto module = methodInstance->GetOwner()->mModule;

	String name = module->MethodToString(methodInstance);
	if (!mNameMod.IsEmpty())
	{
		for (int i = 1; i < (int)name.length(); i++)
		{
			if (name[i] == '(')
			{
				char prevC = name[i - 1];
				if ((::isalnum((uint8)prevC)) || (prevC == '_'))
				{
					name.Insert(i, mNameMod);
					break;
				}
			}
		}
	}
	return name;
}

//////////////////////////////////////////////////////////////////////////

CeInternalData::~CeInternalData()
{
	switch (mKind)
	{
	case Kind_File:
		BfpFile_Release(mFile);
		break;
	case Kind_FindFileData:
		BfpFindFileData_Release(mFindFileData);
		break;
	case Kind_Spawn:
		BfpSpawn_Release(mSpawn);
		break;
	}
}

//////////////////////////////////////////////////////////////////////////

CeFunction::~CeFunction()
{
	BF_ASSERT(mId == -1);
	for (auto innerFunc : mInnerFunctions)
		delete innerFunc;
	delete mCeInnerFunctionInfo;
	delete mDbgInfo;

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

void CeFunction::UnbindBreakpoints()
{
	for (auto kv : mBreakpoints)
		mCode[kv.mKey] = kv.mValue.mPrevOpCode;
	mBreakpoints.Clear();
}

CeEmitEntry* CeFunction::FindEmitEntry(int instIdx, int* entryIdx)
{
	int i = 0;
	CeEmitEntry* emitEntry = NULL;

	if (!mCode.IsEmpty())
	{
		int lo = 0;
		int hi = mEmitTable.size() - 1;
		while (lo <= hi)
		{
			i = (lo + hi) / 2;
			emitEntry = &mEmitTable.mVals[i];
			//int c = midVal <=> value;
			if (emitEntry->mCodePos == instIdx) break;
			if (emitEntry->mCodePos < instIdx)
				lo = i + 1;
			else
				hi = i - 1;
		}
		if ((emitEntry != NULL) && (emitEntry->mCodePos > instIdx) && (i > 0))
		{
			emitEntry = &mEmitTable.mVals[i - 1];
			if (entryIdx != NULL)
				*entryIdx = i - 1;
		}
		else if (entryIdx != NULL)
			*entryIdx = i;
	}

	return emitEntry;
}

// This is for "safe" retrieval from within CeDebugger
int CeFunction::SafeGetId()
{
#ifdef BF_PLATFORM_WINDOWS
	__try
	{
		return mId;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return 0;
#else
	return mId;
#endif
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
	case CEOI_FrameRef8:
	case CEOI_FrameRef16:
	case CEOI_FrameRef32:
	case CEOI_FrameRef64:
	case CEOI_FrameRefF32:
	case CEOI_FrameRefF64:
		{
			int32 addr = CE_GET(int32);

			if (mCeFunction->mDbgInfo != NULL)
			{
				CeDbgVariable* dbgVar = NULL;
				if (mVarMap.TryGetValue(addr, &dbgVar))
				{
					mStr += dbgVar->mName;
					mStr += "@";
				}
			}

			switch (operandInfoKind)
			{
			case CEOI_FrameRef8:
				mStr += "int8";
				break;
			case CEOI_FrameRef16:
				mStr += "int16";
				break;
			case CEOI_FrameRef32:
				mStr += "int32";
				break;
			case CEOI_FrameRef64:
				mStr += "int64";
				break;
			case CEOI_FrameRefF32:
				mStr += "float";
				break;
			case CEOI_FrameRefF64:
				mStr += "double";
				break;
			}

			char str[64];
			if (addr >= 0)
				sprintf(str, "[FR+0x%X]", addr);
			else
				sprintf(str, "[FR-0x%X]", -addr);
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

			mJmp = (int32)(val + (mPtr - mStart));
		}
		break;
	}
}

void CeDumpContext::Next()
{
	CeOp op = CE_GET(CeOp);

	if (op == CeOp_DbgBreak)
	{
		int instIdx = mPtr - mCeFunction->mCode.mVals - 2;
		CeBreakpointBind* breakpointEntry = NULL;
		if (mCeFunction->mBreakpoints.TryGetValue(instIdx, &breakpointEntry))
		{
			op = breakpointEntry->mPrevOpCode;
		}
	}

	CeOpInfo& opInfo = gOpInfo[op];

	auto argPtr = mPtr;

	if (mCeFunction->mDbgInfo != NULL)
	{
		for (auto& dbgVar : mCeFunction->mDbgInfo->mVariables)
		{
			mVarMap[dbgVar.mValue.mFrameOfs] = &dbgVar;
		}
	}

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

	auto endPtr = mPtr;

	if (op == CeOp_GetMethod)
	{
		mPtr = argPtr;
		CE_GET(int32);
		int methodIdx = CE_GET(int32);
		auto& callEntry = mCeFunction->mCallTable[methodIdx];
		auto methodInstance = callEntry.mFunctionInfo->mMethodInstance;
		auto ceModule = mCeFunction->mCeMachine->mCeModule;
		if (!callEntry.mFunctionInfo->mMethodRef.IsNull())
		{
			auto methodRef = callEntry.mFunctionInfo->mMethodRef;
			auto methodDef = methodRef.mTypeInstance->mTypeDef->mMethods[methodRef.mMethodNum];
			methodInstance = ceModule->GetMethodInstance(methodRef.mTypeInstance, methodDef,
				methodRef.mMethodGenericArguments).mMethodInstance;
		}
		if (methodInstance != NULL)
			mStr += StrFormat(" ; %s", ceModule->MethodToString(methodInstance).c_str());

		mPtr = endPtr;
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

		mStr += StrFormat("%04X: ", ofs);
		Next();

		if ((curEmitEntry != NULL) && (curEmitEntry->mScope != -1))
		{
			mStr += StrFormat("  @%d[%s:%d]", curEmitIdx, GetFileName(mCeFunction->mDbgScopes[curEmitEntry->mScope].mFilePath).c_str(),
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

	String errStr = StrFormat("Failure during comptime generation of %s: %s", mBeFunction->mName.c_str(), str.c_str());
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

int CeBuilder::GetCallTableIdx(BeFunction* beFunction, CeOperand* outOperand)
{
	int* callIdxPtr = NULL;
	if (mFunctionMap.TryAdd(beFunction, NULL, &callIdxPtr))
	{
		CeFunctionInfo* ceFunctionInfo = NULL;
		mCeMachine->mNamedFunctionMap.TryGetValue(beFunction->mName, &ceFunctionInfo);
		if (ceFunctionInfo != NULL)
			ceFunctionInfo->mRefCount++;
		else
		{
			if (outOperand != NULL)
			{
				auto checkBuilder = this;
				if (checkBuilder->mParentBuilder != NULL)
					checkBuilder = checkBuilder->mParentBuilder;

				int innerFunctionIdx = 0;
				if (checkBuilder->mInnerFunctionMap.TryGetValue(beFunction, &innerFunctionIdx))
				{
					auto innerFunction = checkBuilder->mCeFunction->mInnerFunctions[innerFunctionIdx];
					if (innerFunction->mInitializeState < CeFunction::InitializeState_Initialized)
						mCeMachine->PrepareFunction(innerFunction, checkBuilder);

					CeOperand result = FrameAlloc(mCeMachine->GetBeContext()->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));
					Emit(CeOp_GetMethod_Inner);
					EmitFrameOffset(result);
					Emit((int32)innerFunctionIdx);

					*outOperand = result;
					return -1;
				}
			}

			Fail(StrFormat("Unable to locate method %s", beFunction->mName.c_str()));
		}

		CeCallEntry callEntry;
		callEntry.mFunctionInfo = ceFunctionInfo;
		*callIdxPtr = (int)mCeFunction->mCallTable.size();
		mCeFunction->mCallTable.Add(callEntry);

		if (ceFunctionInfo != NULL)
		{
			auto callerType = mCeFunction->mCeFunctionInfo->GetOwner();
			auto calleeType = ceFunctionInfo->GetOwner();

			if ((callerType != NULL) && (calleeType != NULL))
			{
				// This will generally already be set, but there are some error cases (such as duplicate type names)
				//  where this will not be set yet
				callerType->mModule->AddDependency(calleeType, callerType, BfDependencyMap::DependencyFlag_Calls);
			}
		}
	}
	return *callIdxPtr;
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
	case BeUndefConstant::TypeId:
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
	case BeGEP1Constant::TypeId:
		{
 			auto gepConstant = (BeGEP1Constant*)value;

 			auto mcVal = GetOperand(gepConstant->mTarget);

 			BePointerType* ptrType = (BePointerType*)mcVal.mType;
 			BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

 			auto result = mcVal;

 			// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
 			int64 byteOffset = 0;
 			BeType* elementType = NULL;
 			byteOffset += gepConstant->mIdx0 * ptrType->mElementType->mSize;

			result = FrameAlloc(ptrType);
			EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
			EmitFrameOffset(result);
			EmitFrameOffset(mcVal);
			Emit(&byteOffset, mPtrSize);

 			return result;
		}
		break;
	case BeGEP2Constant::TypeId:
		{
 			auto gepConstant = (BeGEP2Constant*)value;

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
			CeOperand operand;
			int callIdx = GetCallTableIdx(beFunction, &operand);
			if (operand)
				return operand;

			if (allowImmediate)
			{
				CeOperand result;
				result.mKind = CeOperandKind_CallTableIdx;
				result.mCallTableIdx = callIdx;
				return result;
			}

			BF_ASSERT(callIdx <= mCeFunction->mCallTable.mSize);

			CeOperand result = FrameAlloc(mCeMachine->GetBeContext()->GetPrimitiveType((sizeof(BfMethodInstance*) == 8) ? BeTypeCode_Int64 : BeTypeCode_Int32));
			Emit(CeOp_GetMethod);
			EmitFrameOffset(result);
			Emit((int32)callIdx);
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

int CeBuilder::DbgCreateMethodRef(BfMethodInstance* methodInstance, const StringImpl& nameMod)
{
	CeDbgMethodRef dbgMethodRef;
	dbgMethodRef.mNameMod = nameMod;
	dbgMethodRef.mMethodRef = methodInstance;
	int* valuePtr = NULL;
	if (mDbgMethodRefMap.TryAdd(dbgMethodRef, NULL, &valuePtr))
	{
		*valuePtr = mCeFunction->mDbgMethodRefTable.mSize;
		mCeFunction->mDbgMethodRefTable.Add(dbgMethodRef);
	}
	return *valuePtr;
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

void CeBuilder::ProcessMethod(BfMethodInstance* methodInstance, BfMethodInstance* dupMethodInstance, bool forceIRWrites)
{
	SetAndRestoreValue<BfMethodState*> prevMethodStateInConstEval(mCeMachine->mCeModule->mCurMethodState, NULL);
	BfAutoComplete* prevAutoComplete = NULL;
	if (mCeMachine->mCeModule->mCompiler->mResolvePassData != NULL)
	{
		prevAutoComplete = mCeMachine->mCeModule->mCompiler->mResolvePassData->mAutoComplete;
		mCeMachine->mCeModule->mCompiler->mResolvePassData->mAutoComplete = NULL;
	}

	auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
	auto irBuilder = mCeMachine->mCeModule->mBfIRBuilder;
	auto beModule = irCodeGen->mBeModule;
	beModule->mCeMachine = mCeMachine;

	dupMethodInstance->mIsReified = true;
	dupMethodInstance->mInCEMachine = false; // Only have the original one

	// We can't use methodInstance->mMethodInstanceGroup because we may add foreign method instances which
	//  would reallocate the methodInstanceGroup
	BfMethodInstanceGroup methodInstanceGroup;
	methodInstanceGroup.mOwner = methodInstance->mMethodInstanceGroup->mOwner;
	methodInstanceGroup.mDefault = methodInstance->mMethodInstanceGroup->mDefault;
	methodInstanceGroup.mMethodIdx = methodInstance->mMethodInstanceGroup->mMethodIdx;
	methodInstanceGroup.mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
	dupMethodInstance->mMethodInstanceGroup = &methodInstanceGroup;

	mCeMachine->mCeModule->mHadBuildError = false;
	auto irState = irBuilder->GetState();
	auto beState = irCodeGen->GetState();
	mCeMachine->mCeModule->ProcessMethod(dupMethodInstance, true, forceIRWrites);
	irCodeGen->SetState(beState);
	irBuilder->SetState(irState);

	if (mCeMachine->mCeModule->mCompiler->mResolvePassData != NULL)
		mCeMachine->mCeModule->mCompiler->mResolvePassData->mAutoComplete = prevAutoComplete;

	methodInstanceGroup.mDefault = NULL;
	dupMethodInstance->mMethodInstanceGroup = methodInstance->mMethodInstanceGroup;
}

void CeBuilder::Build()
{
	SetAndRestoreValue<CeDbgState*> prevDbgState;
	if (mCeMachine->mDebugger != NULL)
		prevDbgState.Init(mCeMachine->mDebugger->mCurDbgState, NULL);

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
			((methodInstance->GetOwner()->IsGenericTypeInstance()) && (!isGenericVariation) && (!methodInstance->mMethodDef->mIsLocalMethod) && (!methodInstance->mIsUnspecialized))))
		{
			auto unspecializedMethodInstance = mCeMachine->mCeModule->GetUnspecializedMethodInstance(methodInstance, !methodInstance->mMethodDef->mIsLocalMethod);
			if (!unspecializedMethodInstance->mHasBeenProcessed)
			{
				BfMethodInstance dupUnspecMethodInstance;
				dupUnspecMethodInstance.CopyFrom(unspecializedMethodInstance);
				ProcessMethod(unspecializedMethodInstance, &dupUnspecMethodInstance, false);
				if (dupUnspecMethodInstance.mMethodInfoEx != NULL)
					dupMethodInstance.GetMethodInfoEx()->mGenericTypeBindings = dupUnspecMethodInstance.mMethodInfoEx->mGenericTypeBindings;
			}
		}

		// Clear this so we can properly get QueueStaticField calls
		mCeMachine->mCeModule->mStaticFieldRefs.Clear();

		int startFunctionCount = (int)beModule->mFunctions.size();
		ProcessMethod(methodInstance, &dupMethodInstance, true);
		if (mCeFunction->mInitializeState == CeFunction::InitializeState_Initialized)
			return;

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
			if (mCeMachine->mDebugger != NULL)
				innerFunction->mDbgInfo = new CeDbgFunctionInfo();
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

	int scopeIdx = -1;

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

			if ((prevEmitDbgPos != mCurDbgLoc) && (mCurDbgLoc != NULL))
			{
				auto _GetScope = [&](BeMDNode* mdNode, int inlinedAt)
				{
					BeDbgFile* dbgFile;
					BeDbgFunction* dbgFunc = NULL;
					String nameAdd;

					while (auto dbgLexicalBlock = BeValueDynCast<BeDbgLexicalBlock>(mdNode))
						mdNode = dbgLexicalBlock->mScope;

					if (dbgFunc = BeValueDynCast<BeDbgFunction>(mdNode))
					{
						dbgFile = dbgFunc->mFile;
					}
					else if (auto dbgLoc = BeValueDynCast<BeDbgLoc>(mdNode))
						dbgFile = dbgLoc->GetDbgFile();
					else
						dbgFile = BeValueDynCast<BeDbgFile>(mdNode);

					int* valuePtr = NULL;
					CeDbgInlineLookup lookupPair(dbgFile, inlinedAt);
					if (mDbgScopeMap.TryAdd(lookupPair, NULL, &valuePtr))
					{
						int scopeIdx = (int)mCeFunction->mDbgScopes.size();
						String filePath = dbgFile->mDirectory;
						filePath.Append(DIR_SEP_CHAR);
						filePath += dbgFile->mFileName;
						CeDbgScope dbgScope;
						dbgScope.mFilePath = filePath;
						dbgScope.mInlinedAt = inlinedAt;
						dbgScope.mMethodVal = -1;

						if (dbgFunc != NULL)
						{
							if (dbgFunc->mValue == NULL)
							{
								if (!dbgFunc->mLinkageName.IsEmpty())
								{
									int methodRefIdx = atoi(dbgFunc->mLinkageName.c_str());
									dbgScope.mMethodVal = methodRefIdx | CeDbgScope::MethodValFlag_MethodRef;
								}
								else
								{
									CeDbgMethodRef dbgMethodRef;
									dbgMethodRef.mNameMod = dbgFunc->mName;

									int* valuePtr = NULL;
									if (mDbgMethodRefMap.TryAdd(dbgMethodRef, NULL, &valuePtr))
									{
										*valuePtr = mCeFunction->mDbgMethodRefTable.mSize;
										mCeFunction->mDbgMethodRefTable.Add(dbgMethodRef);
									}
									dbgScope.mMethodVal = *valuePtr | CeDbgScope::MethodValFlag_MethodRef;
								}
							}
							else if (dbgFunc->mValue != mBeFunction)
								dbgScope.mMethodVal = GetCallTableIdx(dbgFunc->mValue, NULL);
						}

						mCeFunction->mDbgScopes.Add(dbgScope);
						*valuePtr = scopeIdx;
						return scopeIdx;
					}
					else
						return *valuePtr;
				};

				std::function<int(BeDbgLoc*)> _GetInlinedScope = [&](BeDbgLoc* dbgLoc)
				{
					if (dbgLoc == NULL)
						return -1;
					int* valuePtr = NULL;
					if (mDbgInlineMap.TryAdd(dbgLoc, NULL, &valuePtr))
					{
						CeDbgInlineEntry inlineEntry;
						inlineEntry.mLine = dbgLoc->mLine;
						inlineEntry.mColumn = dbgLoc->mColumn;

						auto inlinedAt = _GetInlinedScope(dbgLoc->mDbgInlinedAt);
						inlineEntry.mScope = _GetScope(dbgLoc->mDbgScope, inlinedAt);

						*valuePtr = mCeFunction->mDbgInlineTable.mSize;
						mCeFunction->mDbgInlineTable.Add(inlineEntry);
					}
					return *valuePtr;
				};

				int inlinedAt = _GetInlinedScope(mCurDbgLoc->mDbgInlinedAt);
				scopeIdx = _GetScope(mCurDbgLoc->mDbgScope, inlinedAt);
			}

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
			case BeNopInst::TypeId:
			case BeLifetimeSoftEndInst::TypeId:
			case BeLifetimeStartInst::TypeId:
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
						EmitBinaryOp(CeOp_Shr_U8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_ARightShift:
						EmitBinaryOp(CeOp_Shr_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
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
								case BeTypeCode_Float:
									switch (toTypeCode)
									{
									case BeTypeCode_Int8:
										op = CeOp_Conv_F32_U8;
										break;
									case BeTypeCode_Int16:
										op = CeOp_Conv_F32_U16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_F32_U32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_F32_U64;
										break;
									}
									break;
								case BeTypeCode_Double:
									switch (toTypeCode)
									{
									case BeTypeCode_Int8:
										op = CeOp_Conv_F64_U8;
										break;
									case BeTypeCode_Int16:
										op = CeOp_Conv_F64_U16;
										break;
									case BeTypeCode_Int32:
										op = CeOp_Conv_F64_U32;
										break;
									case BeTypeCode_Int64:
										op = CeOp_Conv_F64_U64;
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
						case BfIRIntrinsic_MemSet:
							{
								CeOperand ceDestPtr = GetOperand(castedInst->mArgs[0].mValue);
								CeOperand ceValue = GetOperand(castedInst->mArgs[1].mValue);
								CeOperand ceSize = GetOperand(castedInst->mArgs[2].mValue);

								Emit(CeOp_MemSet);
								EmitFrameOffset(ceDestPtr);
								EmitFrameOffset(ceValue);
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
						case BfIRIntrinsic_DebugTrap:
							Emit(CeOp_DbgBreak);
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
							if (!ceArg)
								continue;
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
			case BeDbgDeclareInst::TypeId:
				{
					auto castedInst = (BeDbgDeclareInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue, true);

					if (mCeFunction->mDbgInfo != NULL)
					{
						bool isConst = false;

						auto beType = castedInst->mDbgVar->mType;
						if (auto dbgConstType = BeValueDynCast<BeDbgConstType>(beType))
						{
							isConst = true;
							beType = dbgConstType->mElement;
						}

						if (auto dbgTypeId = BeValueDynCast<BeDbgTypeId>(beType))
						{
							mDbgVariableMap[castedInst->mValue] = mCeFunction->mDbgInfo->mVariables.mSize;

							CeDbgVariable dbgVariable;
							dbgVariable.mName = castedInst->mDbgVar->mName;
							dbgVariable.mValue = mcValue;
							dbgVariable.mType = mCeMachine->mCeModule->mContext->mTypes[dbgTypeId->mTypeId];
							dbgVariable.mScope = scopeIdx;
							dbgVariable.mIsConst = isConst;
							dbgVariable.mStartCodePos = mCeFunction->mCode.mSize;
							dbgVariable.mEndCodePos = -1;
							mCeFunction->mDbgInfo->mVariables.Add(dbgVariable);
						}
					}
				}
				break;
			case BeLifetimeEndInst::TypeId:
				{
					auto castedInst = (BeLifetimeEndInst*)inst;
					int varIdx = 0;
					if (mDbgVariableMap.TryGetValue(castedInst->mPtr, &varIdx))
					{
						auto dbgVar = &mCeFunction->mDbgInfo->mVariables[varIdx];
						dbgVar->mEndCodePos = mCeFunction->mCode.mSize;
					}
				}
				break;
			case BeEnsureInstructionAtInst::TypeId:
				{
					if (mCeMachine->mDebugger != NULL)
					{
						Emit(CeOp_Nop);
					}
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
				CeEmitEntry emitEntry;
				emitEntry.mCodePos = startCodePos;
				emitEntry.mScope = scopeIdx;
				if ((mCurDbgLoc != NULL) && (mCurDbgLoc->mLine != -1))
				{
					emitEntry.mLine = mCurDbgLoc->mLine;
					emitEntry.mColumn = mCurDbgLoc->mColumn;
				}
				else if (!mCeFunction->mEmitTable.IsEmpty())
				{
					auto& prevEmitEntry = mCeFunction->mEmitTable.back();
					emitEntry.mScope = prevEmitEntry.mScope;
					emitEntry.mLine = prevEmitEntry.mLine;
					emitEntry.mColumn = -1;
				}
				else
				{
					emitEntry.mLine = -1;
					emitEntry.mColumn = -1;
				}
				mCeFunction->mEmitTable.Add(emitEntry);

				prevEmitDbgPos = mCurDbgLoc;
			}
		}
	}

	if (mCeFunction->mDbgInfo != NULL)
	{
		for (auto& dbgVar : mCeFunction->mDbgInfo->mVariables)
			if (dbgVar.mEndCodePos == -1)
				dbgVar.mEndCodePos = mCeFunction->mCode.mSize;
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
	mPrevContext = NULL;
	mCurEvalFlags = CeEvalFlags_None;
	mCeMachine = NULL;
	mReflectTypeIdOffset = -1;
	mExecuteId = -1;
	mStackSize = -1;

	mCurCallSource = NULL;
	mHeap = new	ContiguousHeap();
	mCurFrame = NULL;
	mCurModule = NULL;
	mCurMethodInstance = NULL;
	mCallerMethodInstance = NULL;
	mCallerTypeInstance = NULL;
	mCallerActiveTypeDef = NULL;
	mCurExpectingType = NULL;
	mCurEmitContext = NULL;
}

CeContext::~CeContext()
{
	delete mHeap;
	BF_ASSERT(mInternalDataMap.IsEmpty());
}

BfError* CeContext::Fail(const StringImpl& error)
{
	if (mCurModule == NULL)
		return NULL;
	if (mCurEmitContext != NULL)
		mCurEmitContext->mFailed = true;
	auto bfError = mCurModule->Fail(StrFormat("Unable to comptime %s", mCurModule->MethodToString(mCurMethodInstance).c_str()), mCurCallSource->mRefNode, (mCurEvalFlags & CeEvalFlags_PersistantError) != 0);
	if (bfError == NULL)
		return NULL;

	bool forceQueue = mCeMachine->mCompiler->GetAutoComplete() != NULL;
	if ((mCeMachine->mDebugger != NULL) && (mCeMachine->mDebugger->mCurDbgState != NULL))
		forceQueue = true;
	mCeMachine->mCompiler->mPassInstance->MoreInfo(error, forceQueue);
	return bfError;
}

BfError* CeContext::Fail(const CeFrame& curFrame, const StringImpl& str)
{
	if (mCurEmitContext != NULL)
		mCurEmitContext->mFailed = true;
	auto bfError = mCurModule->Fail(StrFormat("Unable to comptime %s", mCurModule->MethodToString(mCurMethodInstance).c_str()), mCurCallSource->mRefNode,
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

		CeEmitEntry* emitEntry = ceFunction->FindEmitEntry(ceFrame->mInstPtr - ceFunction->mCode.mVals - 1);
		StringT<256> err;
		if (isHeadEntry)
		{
			err = str;
			err += " ";
		}

		auto contextMethodInstance = mCallerMethodInstance;
		auto contextTypeInstance = mCallerTypeInstance;
		if (stackIdx > 1)
		{
			auto func = mCallStack[stackIdx - 1].mFunction;
			contextMethodInstance = func->mCeFunctionInfo->mMethodInstance;
			contextTypeInstance = contextMethodInstance->GetOwner();
		}

		auto _AddCeMethodInstance = [&](BfMethodInstance* methodInstance)
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCeMachine->mCeModule->mCurTypeInstance, contextTypeInstance);
			SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCeMachine->mCeModule->mCurMethodInstance, contextMethodInstance);
			err += mCeMachine->mCeModule->MethodToString(methodInstance, BfMethodNameFlag_OmitParams);
		};

		auto _AddError = [&](const StringImpl& filePath, int line, int column)
		{
			err += StrFormat(" at line% d:%d in %s", line + 1, column + 1, filePath.c_str());

			auto moreInfo = passInstance->MoreInfo(err, mCeMachine->mCeModule->mCompiler->GetAutoComplete() != NULL);
			if ((moreInfo != NULL))
			{
				BfErrorLocation* location = new BfErrorLocation();
				location->mFile = filePath;
				location->mLine = line;
				location->mColumn = column;
				moreInfo->mLocation = location;
			}
		};

		if (emitEntry != NULL)
		{
			int scopeIdx = emitEntry->mScope;
			int prevInlineIdx = -1;
			while (scopeIdx != -1)
			{
				err += StrFormat("in comptime ");

				int line = emitEntry->mLine;
				int column = emitEntry->mColumn;
				String fileName;

				if (prevInlineIdx != -1)
				{
					auto dbgInlineInfo = &ceFunction->mDbgInlineTable[prevInlineIdx];
					line = dbgInlineInfo->mLine;
					column = dbgInlineInfo->mColumn;
				}

				CeDbgScope* ceScope = &ceFunction->mDbgScopes[scopeIdx];
				if (ceScope->mMethodVal == -1)
				{
					if (ceFunction->mMethodInstance != NULL)
						_AddCeMethodInstance(ceFunction->mMethodInstance);
					else
						_AddCeMethodInstance(ceFunction->mCeInnerFunctionInfo->mOwner->mMethodInstance);
				}
				else
				{
					if ((ceScope->mMethodVal & CeDbgScope::MethodValFlag_MethodRef) != 0)
					{
						auto dbgMethodRef = &ceFunction->mDbgMethodRefTable[ceScope->mMethodVal & CeDbgScope::MethodValFlag_IdxMask];
						err += dbgMethodRef->ToString();
					}
					else
					{
						auto callTableEntry = &ceFunction->mCallTable[ceScope->mMethodVal];
						_AddCeMethodInstance(callTableEntry->mFunctionInfo->mMethodInstance);
					}
				}

				_AddError(ceFunction->mDbgScopes[emitEntry->mScope].mFilePath, line, column);

				if (ceScope->mInlinedAt == -1)
					break;
				auto inlineInfo = &ceFrame->mFunction->mDbgInlineTable[ceScope->mInlinedAt];
				scopeIdx = inlineInfo->mScope;
				prevInlineIdx = ceScope->mInlinedAt;

				err.Clear();
			}
		}
		else
		{
			err += StrFormat("in comptime ");

			if (ceFunction->mMethodInstance != NULL)
				_AddCeMethodInstance(ceFunction->mMethodInstance);
			else
				_AddCeMethodInstance(ceFunction->mCeInnerFunctionInfo->mOwner->mMethodInstance);

			if ((emitEntry != NULL) && (emitEntry->mScope != -1))
			{
				_AddError(ceFunction->mDbgScopes[emitEntry->mScope].mFilePath, emitEntry->mLine, emitEntry->mColumn);
			}
			else
			{
				auto moreInfo = passInstance->MoreInfo(err, mCeMachine->mCeModule->mCompiler->GetAutoComplete() != NULL);
			}
		}
	}

	return bfError;
}

//////////////////////////////////////////////////////////////////////////

void CeContext::CalcWorkingDir()
{
	if (mWorkingDir.IsEmpty())
	{
		BfProject* activeProject = NULL;
		auto activeTypeDef = mCallerActiveTypeDef;
		if (activeTypeDef != NULL)
			activeProject = activeTypeDef->mProject;
		if (activeProject != NULL)
			mWorkingDir = activeProject->mDirectory;
	}
}

void CeContext::FixRelativePath(StringImpl& path)
{
	CalcWorkingDir();
	if (!mWorkingDir.IsEmpty())
		path = GetAbsPath(path, mWorkingDir);
}

bool CeContext::AddRebuild(const CeRebuildKey& key, const CeRebuildValue& value)
{
	if (mCurModule == NULL)
		return false;
	if (mCallerTypeInstance == NULL)
		return false;
	if ((mCurEvalFlags & CeEvalFlags_NoRebuild) != 0)
		return false;
	if (mCallerTypeInstance->mCeTypeInfo == NULL)
		mCallerTypeInstance->mCeTypeInfo = new BfCeTypeInfo();
	mCallerTypeInstance->mCeTypeInfo->mRebuildMap[key] = value;
	mCurModule->mCompiler->mHasComptimeRebuilds = true;
	return true;
}

void CeContext::AddFileRebuild(const StringImpl& path)
{
	auto timeStamp = BfpFile_GetTime_LastWrite(path.c_str());
	if (timeStamp != 0)
	{
		String fixedPath = FixPathAndCase(path);

		CeRebuildKey rebuildKey;
		rebuildKey.mKind = CeRebuildKey::Kind_File;
		rebuildKey.mString = fixedPath;

		CeRebuildValue rebuildValue;
		rebuildValue.mInt = timeStamp;

		if (AddRebuild(rebuildKey, rebuildValue))
			mCurModule->mCompiler->mRebuildFileSet.Add(fixedPath);
	}
}

uint8* CeContext::CeMalloc(int size)
{
#ifdef CE_ENABLE_HEAP
	auto heapRef = mHeap->Alloc(size);
	auto ceAddr = mStackSize + heapRef;
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
	ContiguousHeap::AllocRef heapRef = addr - mStackSize;
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

BfType* CeContext::GetBfType(BfIRType irType)
{
	if (irType.mKind == BfIRTypeData::TypeKind_TypeId)
		return GetBfType(irType.mId);
	return NULL;
}

void CeContext::PrepareConstStructEntry(CeConstStructData& constEntry)
{
	if (constEntry.mHash.IsZero())
	{
		constEntry.mHash = Hash128(constEntry.mData.mVals, constEntry.mData.mSize);
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
	if ((addr < 0x10000) || (addr + size > mMemory.mSize))
		return false;
	return true;
}

uint8* CeContext::GetMemoryPtr(addr_ce addr, int32 size)
{
	if (CheckMemory(addr, size))
		return mMemory.mVals + addr;
	return NULL;
}

bool CeContext::GetStringFromAddr(addr_ce strInstAddr, StringImpl& str)
{
	if (!CheckMemory(strInstAddr, 0))
		return false;

	BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeMachine->mCeModule->ResolveTypeDef(mCeMachine->mCompiler->mStringTypeDef, BfPopulateType_Data);

	auto lenByteCount = stringTypeInst->mFieldInstances[0].mResolvedType->mSize;
	auto lenOffset = stringTypeInst->mFieldInstances[0].mDataOffset;
	auto allocSizeOffset = stringTypeInst->mFieldInstances[1].mDataOffset;
	auto ptrOffset = stringTypeInst->mFieldInstances[2].mDataOffset;

	uint8* strInst = (uint8*)(strInstAddr + mMemory.mVals);
	int32 lenVal = *(int32*)(strInst + lenOffset);

	char* charPtr = NULL;

	if (lenByteCount == 4)
	{
		int32 allocSizeVal = *(int32*)(strInst + allocSizeOffset);
		if ((allocSizeVal & 0x40000000) != 0)
		{
			int32 ptrVal = *(int32*)(strInst + ptrOffset);
			charPtr = (char*)(ptrVal + mMemory.mVals);
		}
		else
		{
			charPtr = (char*)(strInst + ptrOffset);
		}
	}
	else
	{
		int64 allocSizeVal = *(int64*)(strInst + allocSizeOffset);
		if ((allocSizeVal & 0x4000000000000000LL) != 0)
		{
			int32 ptrVal = *(int32*)(strInst + ptrOffset);
			charPtr = (char*)(ptrVal + mMemory.mVals);
		}
		else
		{
			charPtr = (char*)(strInst + ptrOffset);
		}
	}

	int32 ptrVal = *(int32*)(strInst + ptrOffset);

	if (charPtr != NULL)
		str.Insert(str.length(), charPtr, lenVal);
	return true;
}

bool CeContext::GetStringFromStringView(addr_ce addr, StringImpl& str)
{
	int ptrSize = mCeMachine->mCeModule->mSystem->mPtrSize;
	if (!CheckMemory(addr, ptrSize * 2))
		return false;

	addr_ce charsPtr = *(addr_ce*)(mMemory.mVals + addr);
	int32 len = *(int32*)(mMemory.mVals + addr + ptrSize);

	if (len > 0)
	{
		if (!CheckMemory(charsPtr, len))
			return false;
		str.Append((const char*)(mMemory.mVals + charsPtr), len);
	}

	return true;
}

bool CeContext::GetCustomAttribute(BfModule* module, BfIRConstHolder* constHolder, BfCustomAttributes* customAttributes, int attributeIdx, addr_ce resultAddr)
{
	if (customAttributes == NULL)
		return false;

	auto customAttr = customAttributes->Get(attributeIdx);
	if (customAttr == NULL)
		return false;

	auto ceContext = mCeMachine->AllocContext();
	BfIRValue foreignValue = ceContext->CreateAttribute(mCurCallSource->mRefNode, module, constHolder, customAttr);
	auto foreignConstant = module->mBfIRBuilder->GetConstant(foreignValue);
	if (foreignConstant->mConstType == BfConstType_AggCE)
	{
		auto constAggData = (BfConstantAggCE*)foreignConstant;
		auto value = ceContext->CreateConstant(module, ceContext->mMemory.mVals + constAggData->mCEAddr, customAttr->mType);
		if (!value)
			Fail("Failed to encoded attribute");
		auto attrConstant = module->mBfIRBuilder->GetConstant(value);
		if ((attrConstant == NULL) || (!WriteConstant(module, resultAddr, attrConstant, customAttr->mType)))
			Fail("Failed to decode attribute");
	}

	mCeMachine->ReleaseContext(ceContext);

	return true;
}

BfType* CeContext::GetCustomAttributeType(BfCustomAttributes* customAttributes, int attributeIdx)
{
	if (customAttributes == NULL)
		return NULL;

	auto customAttr = customAttributes->Get(attributeIdx);
	if (customAttr == NULL)
		return NULL;

	return customAttr->mType;
}

//#define CE_GETC(T) *((T*)(addr += sizeof(T)) - 1)
#define CE_GETC(T) *(T*)(mMemory.mVals + addr)

bool CeContext::WriteConstant(BfModule* module, addr_ce addr, BfConstant* constant, BfType* type, bool isParams)
{
	int ptrSize = mCeMachine->mCeModule->mSystem->mPtrSize;

	switch (constant->mTypeCode)
	{
	case BfTypeCode_None:
		return true;
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
		if (ptrSize == 4)
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
		if (type->IsPointer())
		{
			auto elementType = type->GetUnderlyingType();
			auto toPtr = CeMalloc(elementType->mSize);
			addr_ce toAddr = (addr_ce)(toPtr - mMemory.mVals);
			if (ptrSize == 4)
				CE_GETC(int32) = (int32)toAddr;
			else
				CE_GETC(int64) = (int64)toAddr;
			return WriteConstant(module, toAddr, constant, elementType, isParams);
		}

		auto aggConstant = (BfConstantAgg*)constant;
		if (type->IsSizedArray())
		{
			auto sizedArrayType = (BfSizedArrayType*)type;
			for (int i = 0; i < sizedArrayType->mElementCount; i++)
			{
				auto fieldConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[i]);
				if (fieldConstant == NULL)
					return false;
				if (!WriteConstant(module, addr + i * sizedArrayType->mElementType->mSize, fieldConstant, sizedArrayType->mElementType))
					return false;
			}

			return true;
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

			if (ptrSize == 4)
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

			if (ptrSize == 4)
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

			BfType* innerType = NULL;
			BfType* payloadType = NULL;
			if (typeInst->IsUnion())
				innerType = typeInst->GetUnionInnerType();

			if (typeInst->IsPayloadEnum())
			{
				auto& dscrFieldInstance = typeInst->mFieldInstances.back();

				auto fieldConstant = module->mBfIRBuilder->GetConstant(aggConstant->mValues[dscrFieldInstance.mDataIdx]);
				if (fieldConstant == NULL)
					return false;
				if (!WriteConstant(module, addr + dscrFieldInstance.mDataOffset, fieldConstant, dscrFieldInstance.mResolvedType))
					return false;

				for (auto& fieldInstance : typeInst->mFieldInstances)
				{
					auto fieldDef = fieldInstance.GetFieldDef();
					if (!fieldInstance.mIsEnumPayloadCase)
						continue;
					int tagIdx = -fieldInstance.mDataIdx - 1;
					if (fieldConstant->mInt32 == tagIdx)
						payloadType = fieldInstance.mResolvedType;
				}
			}

			if (typeInst->IsUnion())
			{
				if (!innerType->IsValuelessType())
				{
					BfIRValue dataVal = aggConstant->mValues[1];
					if ((payloadType != NULL) && (innerType != NULL))
					{
						Array<uint8> memArr;
						memArr.Resize(innerType->mSize);
						if (!module->mBfIRBuilder->WriteConstant(dataVal, memArr.mVals, innerType))
							return false;
						dataVal = module->mBfIRBuilder->ReadConstant(memArr.mVals, payloadType);
						if (!dataVal)
							return false;
						innerType = payloadType;
					}

					auto fieldConstant = module->mBfIRBuilder->GetConstant(dataVal);
					if (fieldConstant == NULL)
						return false;
					if (!WriteConstant(module, addr, fieldConstant, innerType))
						return false;
				}
			}

			if (!typeInst->IsUnion())
			{
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
		}
		return true;
	}

	if (constant->mConstType == BfConstType_AggZero)
	{
		BF_ASSERT(type->IsComposite());
		memset(mMemory.mVals + addr, 0, type->mSize);
		return true;
	}

	if (constant->mConstType == BfConstType_ArrayZero8)
	{
		memset(mMemory.mVals + addr, 0, constant->mInt32);
		return true;
	}

	if (constant->mConstType == BfConstType_Undef)
	{
		memset(mMemory.mVals + addr, 0, type->mSize);
		return true;
	}

	if (constant->mConstType == BfConstType_AggCE)
	{
		auto constAggData = (BfConstantAggCE*)constant;

		if (type->IsPointer())
		{
			if (ptrSize == 4)
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

	if (constant->mConstType == BfConstType_Box)
	{
		auto constBox = (BfConstantBox*)constant;
		auto boxedType = GetBfType(constBox->mToType);
		if (boxedType == NULL)
			return false;
		auto boxedTypeInst = boxedType->ToTypeInstance();
		if (boxedTypeInst == NULL)
			return false;
		module->PopulateType(boxedTypeInst);
		if (boxedTypeInst->mFieldInstances.IsEmpty())
			return false;
		auto& fieldInstance = boxedTypeInst->mFieldInstances.back();

		auto boxedMem = CeMalloc(boxedTypeInst->mInstSize);
		memset(boxedMem, 0, ptrSize*2);
		*(int32*)boxedMem = boxedTypeInst->mTypeId;

		auto constTarget = module->mBfIRBuilder->GetConstantById(constBox->mTarget);
		WriteConstant(module, boxedMem - mMemory.mVals + fieldInstance.mDataOffset, constTarget, fieldInstance.mResolvedType);

		if (ptrSize == 4)
			CE_GETC(int32) = (int32)(boxedMem - mMemory.mVals);
		else
			CE_GETC(int64) = (int64)(boxedMem - mMemory.mVals);
		return true;
	}

	if (constant->mConstType == BfConstType_PtrToInt)
	{
		auto ptrToIntConst = (BfConstantPtrToInt*)constant;

		auto constTarget = module->mBfIRBuilder->GetConstantById(ptrToIntConst->mTarget);
		return WriteConstant(module, addr, constTarget, type);
	}

	if (constant->mConstType == BfConstType_IntToPtr)
	{
		auto ptrToIntConst = (BfConstantIntToPtr*)constant;

		auto intType = mCeMachine->mCeModule->GetPrimitiveType(BfTypeCode_IntPtr);
		auto constTarget = module->mBfIRBuilder->GetConstantById(ptrToIntConst->mTarget);
		return WriteConstant(module, addr, constTarget, intType);
	}

	if (constant->mConstType == BfConstType_BitCastNull)
	{
		BF_ASSERT(type->IsPointer() || type->IsObjectOrInterface());
		memset(mMemory.mVals + addr, 0, type->mSize);
		return true;
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
				if (ptrSize == 4)
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
			if (ptrSize == 4)
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

		if (ptrSize == 4)
			CE_GETC(int32) = strAddr;
		else
			CE_GETC(int64) = strAddr;
		return true;
	}

	if ((constant->mConstType == BfConstType_TypeOf) || (constant->mConstType == BfConstType_TypeOf_WithData))
	{
		auto constTypeOf = (BfTypeOf_Const*)constant;
		addr_ce typeAddr = GetReflectType(constTypeOf->mType->mTypeId);
		if (ptrSize == 4)
			CE_GETC(int32) = typeAddr;
		else
			CE_GETC(int64) = typeAddr;
		return true;
	}

	if (constant->mConstType == BfConstType_ExtractValue)
	{
		Array<BfConstantExtractValue*> extractStack;
		auto checkConstant = constant;
		while (true)
		{
			if (checkConstant == NULL)
				break;

			if (checkConstant->mConstType == BfConstType_ExtractValue)
			{
				auto gepConst = (BfConstantExtractValue*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
				checkConstant = module->mBfIRBuilder->GetConstant(targetConst);
				extractStack.Add(gepConst);
				continue;
			}

			if (checkConstant->mConstType == BfConstType_AggCE)
				return WriteConstant(module, addr, checkConstant, type, isParams);
		}
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
		case BfTypeCode_None:
			return irBuilder->CreateConst(primType->mTypeDef->mTypeCode, 0);
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

	if (bfType->IsTypedPrimitive())
		return CreateConstant(module, ptr, bfType->GetUnderlyingType(), outType);

	if (bfType->IsGenericParam())
		return irBuilder->GetUndefConstValue(irBuilder->MapType(bfType));

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

		if (typeInst->IsObjectOrInterface())
		{
			addr_ce addr = *(addr_ce*)(ptr);
			if (addr == 0)
			{
				return irBuilder->CreateConstNull(irBuilder->MapType(typeInst));
			}
			instData = memStart + addr;

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
				else
				{
					int64 allocSizeVal = *(int64*)(instData + allocSizeOffset);
					if ((allocSizeVal & 0x4000000000000000LL) != 0)
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
			int typeId = GetTypeIdFromType(instData - mMemory.mVals);
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

		if (typeInst->IsUnion())
		{
			auto innerType = typeInst->GetUnionInnerType();
			if (!innerType->IsValuelessType())
			{
				auto result = CreateConstant(module, instData, innerType);
				if (!result)
					return BfIRValue();
				fieldVals.Add(result);
			}
		}

		if ((!typeInst->IsUnion()) || (typeInst->IsPayloadEnum()))
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

		auto instResult = irBuilder->CreateConstAgg(irBuilder->MapTypeInst(typeInst, BfIRPopulateType_Identity), fieldVals);
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

BfIRValue CeContext::CreateAttribute(BfAstNode* targetSrc, BfModule* module, BfIRConstHolder* constHolder, BfCustomAttribute* customAttribute, addr_ce ceAttrAddr)
{
	SetAndRestoreValue<bool> prevIgnoreWrites(module->mBfIRBuilder->mIgnoreWrites, true);

	module->mContext->mUnreifiedModule->PopulateType(customAttribute->mType);
	if (ceAttrAddr == 0)
		ceAttrAddr = CeMalloc(customAttribute->mType->mSize) - mMemory.mVals;
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

	BfTypedValue retValue = Call(CeCallSource(targetSrc), module, ctorMethodInstance, ctorArgs, CeEvalFlags_None, NULL);
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

BfTypedValue CeContext::Call(CeCallSource callSource, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags, BfType* expectingType)
{
	// DISABLED
	//return BfTypedValue();

	AutoTimer autoTimer(mCeMachine->mRevisionExecuteTime);

	SetAndRestoreValue<CeContext*> curPrevContext(mPrevContext, mCeMachine->mCurContext);
 	SetAndRestoreValue<CeContext*> prevContext(mCeMachine->mCurContext, this);
	SetAndRestoreValue<CeEvalFlags> prevEvalFlags(mCurEvalFlags, flags);
	SetAndRestoreValue<CeCallSource*> prevCallSource(mCurCallSource, &callSource);
	SetAndRestoreValue<BfModule*> prevModule(mCurModule, module);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfMethodInstance*> prevCallerMethodInstance(mCallerMethodInstance, module->mCurMethodInstance);
	SetAndRestoreValue<BfTypeInstance*> prevCallerTypeInstance(mCallerTypeInstance, module->mCurTypeInstance);
	SetAndRestoreValue<BfTypeDef*> prevCallerActiveTypeDef(mCallerActiveTypeDef, module->GetActiveTypeDef());
	SetAndRestoreValue<BfType*> prevExpectingType(mCurExpectingType, expectingType);

	SetAndRestoreValue<bool> prevCtxResolvingVar(module->mContext->mResolvingVarField, false);
	SetAndRestoreValue<BfMethodInstance*> moduleCurMethodInstance(module->mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfTypeInstance*> moduleCurTypeInstance(module->mCurTypeInstance, methodInstance->GetOwner());

	SetAndRestoreValue<int> prevCurExecuteId(mCurModule->mCompiler->mCurCEExecuteId, mCeMachine->mExecuteId);

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
	if (!methodInstance->mMethodDef->mIsStatic)
	{
		if (!methodInstance->GetOwner()->IsValuelessType())
		{
			thisArgIdx = 0;
			auto checkConstant = module->mBfIRBuilder->GetConstant(args[0]);
			while (checkConstant != NULL)
			{
				if ((checkConstant != NULL) && (checkConstant->mConstType == BfConstType_AggCE))
				{
					hasAggData = true;
					break;
				}

				if (checkConstant->mConstType == BfConstType_ExtractValue)
				{
					auto gepConst = (BfConstantExtractValue*)checkConstant;
					BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
					checkConstant = module->mBfIRBuilder->GetConstant(targetConst);
					continue;
				}

				break;
			}
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
			if ((!paramType->IsValuelessType()) && (!paramType->IsVar()))
				break;
		}

		BfType* compositeType = paramType->IsComposite() ? paramType : NULL;

		auto arg = args[argIdx];
		bool isConst = arg.IsConst();
		if (isConst)
		{
			auto constant = module->mBfIRBuilder->GetConstant(arg);
			if (constant->mConstType == BfConstType_Undef)
			{
				if (paramType->IsInstanceOf(module->mCompiler->mTypeTypeDef))
				{
					args[argIdx] = module->CreateTypeDataRef(module->GetPrimitiveType(BfTypeCode_None));
				}
// 				else
// 					isConst = false;
			}
			else if (((constant->mConstType == BfConstType_AggZero) || (constant->mConstType == BfConstType_Agg)) &&
				((paramType->IsPointer()) || (paramType->IsRef())))
				compositeType = paramType->GetUnderlyingType();
		}

		if (compositeType != NULL)
		{
			if ((paramType->IsPointer()) || (paramType->IsRef()))
				paramCompositeSize += paramType->GetUnderlyingType()->mSize;
			else
				paramCompositeSize += paramType->mSize;
		}

		if (!isConst)
		{
			if ((argIdx == thisArgIdx) && (methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor))
			{
				// Allow non-const 'this' for ctor
			}
			else if (argIdx != appendAllocIdx)
			{
				Fail(StrFormat("Non-constant argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
		}
	}

	auto methodDef = methodInstance->mMethodDef;

	if (mCeMachine->mCeModule == NULL)
		mCeMachine->Init();

	auto ceModule = mCeMachine->mCeModule;
	bool added = false;
	CeFunction* ceFunction = mCeMachine->GetFunction(methodInstance, BfIRValue(), added);

	if (ceFunction->mInitializeState == CeFunction::InitializeState_Initializing_ReEntry)
	{
		String error = "Comptime method preparation recursion";
		auto curContext = this;
		while (curContext != NULL)
		{
			if (curContext->mCurMethodInstance != NULL)
				error += StrFormat("\n  %s", module->MethodToString(curContext->mCurMethodInstance).c_str());

			curContext = curContext->mPrevContext;
			if ((curContext != NULL) && (curContext->mCurMethodInstance == mCurMethodInstance))
				break;
		}
		Fail(error);
		return BfTypedValue();
	}

	if (ceFunction->mInitializeState < CeFunction::InitializeState_Initialized)
		mCeMachine->PrepareFunction(ceFunction, NULL);

	Array<CeFrame> prevCallStack;

	auto stackPtr = &mMemory[0] + mStackSize;
	auto* memStart = &mMemory[0];

	if (!mCallStack.IsEmpty())
	{
		BF_ASSERT((flags & CeEvalFlags_DbgCall) != 0);
		prevCallStack = mCallStack;
		stackPtr = &mMemory[0] + mCallStack.back().mStackAddr;
		mCallStack.Clear();
	}

	BfTypeInstance* thisType = methodInstance->GetOwner();
	addr_ce allocThisInstAddr = 0;
	addr_ce allocThisAddr = 0;
	addr_ce thisAddr = 0;
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

		if (allocThisSize >= mStackSize / 4)
		{
			// Resize stack a reasonable size
			mStackSize = BF_ALIGN(allocThisSize, 0x100000) + BF_CE_DEFAULT_STACK_SIZE;
			int64 memSize = mStackSize + BF_CE_DEFAULT_HEAP_SIZE;
			if (memSize > BF_CE_MAX_MEMORY)
			{
				Fail("Return value too large (>2GB)");
				return BfTypedValue();
			}

			if (memSize > mMemory.mSize)
				mMemory.Resize(memSize);
			stackPtr = &mMemory[0] + mStackSize;
			memStart = &mMemory[0];
		}

		stackPtr -= allocThisSize;
		auto allocThisPtr = stackPtr;
		memset(allocThisPtr, 0, allocThisSize);

		if (thisType->IsObject())
			*(int32*)(allocThisPtr) = thisType->mTypeId;

		allocThisInstAddr = allocThisPtr - memStart;
		allocThisAddr = allocThisInstAddr;
		thisAddr = allocThisAddr;
	}

	addr_ce allocAppendIdxAddr = 0;
	if (appendAllocIdx != -1)
	{
		stackPtr -= ceModule->mSystem->mPtrSize;
		memset(stackPtr, 0, ceModule->mSystem->mPtrSize);
		*(addr_ce*)(stackPtr) = (addr_ce)(allocThisInstAddr + thisType->mInstSize);
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
			if ((!paramType->IsValuelessType()) && (!paramType->IsVar()))
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
		BfType* compositeType = paramType->IsComposite() ? paramType : NULL;
		if (((constant->mConstType == BfConstType_AggZero) || (constant->mConstType == BfConstType_Agg)) &&
			((paramType->IsPointer()) || (paramType->IsRef())))
			compositeType = paramType->GetUnderlyingType();
		if (compositeType != NULL)
		{
			useCompositeAddr -= compositeType->mSize;
			if (!WriteConstant(module, useCompositeAddr, constant, compositeType, isParams))
			{
				Fail(StrFormat("Failed to process argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
			_FixVariables();

			stackPtr -= ceModule->mSystem->mPtrSize;
			int64 addr64 = useCompositeAddr;
			if (argIdx == thisArgIdx)
				thisAddr = addr64;
			memcpy(stackPtr, &addr64, ceModule->mSystem->mPtrSize);
		}
		else
		{
			stackPtr -= paramType->mSize;
			auto useCompositeAddr = stackPtr - memStart;
			if (!WriteConstant(module, useCompositeAddr, constant, paramType, isParams))
			{
				Fail(StrFormat("Failed to process argument for param '%s'", methodInstance->GetParamName(paramIdx).c_str()));
				return BfTypedValue();
			}
			_FixVariables();

			if (argIdx == thisArgIdx)
			{
				auto checkConstant = constant;
				while (checkConstant != NULL)
				{
					if ((checkConstant != NULL) && (checkConstant->mConstType == BfConstType_AggCE))
					{
						auto constAggData = (BfConstantAggCE*)checkConstant;
						if (paramType->IsPointer())
							thisAddr = constAggData->mCEAddr;
						else
							thisAddr = useCompositeAddr;
						break;
					}

					if (checkConstant->mConstType == BfConstType_ExtractValue)
					{
						auto gepConst = (BfConstantExtractValue*)checkConstant;
						BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
						checkConstant = module->mBfIRBuilder->GetConstant(targetConst);
						continue;
					}

					break;
				}
			}
		}
	}

	addr_ce retAddr = 0;
	if (ceFunction->mMaxReturnSize > 0)
	{
		int retSize = ceFunction->mMaxReturnSize;
		stackPtr -= retSize;
		retAddr = stackPtr - memStart;
	}

	delete mCeMachine->mAppendAllocInfo;
	mCeMachine->mAppendAllocInfo = NULL;

	BfType* returnType = NULL;
	BfType* castReturnType = NULL;
	bool success = Execute(ceFunction, stackPtr - ceFunction->mFrameSize, stackPtr, returnType, castReturnType);
	memStart = &mMemory[0];

	addr_ce retInstAddr = retAddr;

	if ((returnType->IsObject()) || (returnType->IsPointer()))
	{
		// Or pointer?
		retInstAddr = *(addr_ce*)(memStart + retAddr);
	}

	if ((flags & CeEvalFlags_ForceReturnThis) != 0)
	{
		returnType = thisType;
		retInstAddr = thisAddr;
	}

	BfTypedValue returnValue;

	if (success)
	{
		BfTypedValue retValue;
		if (returnType->IsObject())
		{
			BfType* usedReturnType = returnType;
			BfIRValue constVal = CreateConstant(module, (uint8*)&retInstAddr, returnType, &usedReturnType);
			if (constVal)
				returnValue = BfTypedValue(constVal, usedReturnType);
			else
			{
				Fail("Failed to encode return argument");
			}
		}
		else if ((retInstAddr != 0) || (allocThisInstAddr != 0))
		{
			auto* retPtr = memStart + retInstAddr;
			if ((allocThisInstAddr != 0) && (methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor))
			{
				retPtr = memStart + allocThisAddr;
				returnType = thisType;
			}

			BfType* usedReturnType = returnType;
			BfIRValue constVal;
			if (returnType->IsObject())
			{
				addr_ce retAddr = retPtr - memStart;
				constVal = CreateConstant(module, (uint8*)&retAddr, returnType, &usedReturnType);
			}
			else
				constVal = CreateConstant(module, retPtr, returnType, &usedReturnType);
			if (constVal)
				returnValue = BfTypedValue(constVal, usedReturnType);
			else
			{
				Fail("Failed to encode return argument");
			}
		}
		else if ((methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor) && (thisType != NULL) && (thisType->IsValuelessType()))
		{
			returnValue = BfTypedValue(module->mBfIRBuilder->CreateConstAggZero(module->mBfIRBuilder->MapType(returnType, BfIRPopulateType_Identity)), thisType);
		}
		else if ((returnType->IsComposite()) || (returnType->IsValuelessType()))
		{
			returnValue = BfTypedValue(module->mBfIRBuilder->CreateConstAggZero(module->mBfIRBuilder->MapType(returnType, BfIRPopulateType_Identity)), returnType);
		}
	}

	mCallStack.Clear();

	moduleCurMethodInstance.Restore();
	moduleCurTypeInstance.Restore();
	module->AddDependency(methodInstance->GetOwner(), module->mCurTypeInstance, BfDependencyMap::DependencyFlag_ConstEval);

	if (!prevCallStack.IsEmpty())
	{
		BF_ASSERT((flags& CeEvalFlags_DbgCall) != 0);
		mCallStack = prevCallStack;
	}

	if ((castReturnType != NULL) && (returnValue))
	{
		auto castedReturnValue = module->Cast(callSource.mRefNode, returnValue, castReturnType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromComptimeReturn));
		if (castedReturnValue)
			return castedReturnValue;
	}

	return returnValue;
}

#define CE_CHECKSTACK() \
	if (stackPtr < memStart) \
	{ \
		_Fail("Stack overflow"); \
		return false; \
	}

#define CE_CHECKALLOC(SIZE) \
	if ((SIZE < 0) || (SIZE >= 0x80000000LL) || ((uintptr)memSize + (uintptr)SIZE > BF_CE_MAX_MEMORY)) \
	{ \
		_Fail("Maximum memory size exceeded (2GB)"); \
		return false; \
	}

// This check will fail for addresses < 64K (null pointer), or out-of-bounds
#define CE_CHECKSIZE(SIZE) \
	if ((SIZE) < 0) \
	{ \
		_Fail("Invalid memory size"); \
		return false; \
	}
#define CE_CHECKADDR(ADDR, SIZE) \
	if (((ADDR) < 0x10000) || ((ADDR) + (SIZE) > memSize)) \
	{ \
		_Fail("Access violation"); \
		return false; \
	}

#define CE_CHECKADDR_STR(STRNAME, ADDR) \
	{ \
		addr_ce checkAddr = ADDR; \
		while (true) \
		{ \
			if ((uintptr)checkAddr >= (uintptr)memSize) \
			{ \
				break; \
			} \
			if (memStart[checkAddr] == 0) \
			{ \
				CE_CHECKADDR(ADDR, checkAddr - ADDR + 1); \
				STRNAME = String::MakeRef((char*)memStart + ADDR, checkAddr - ADDR + 1); \
				break; \
			} \
			checkAddr++; \
		} \
	}

#define CE_GET_INTERNAL(VAR, ID, KIND) \
	if (!mInternalDataMap.TryGetValue((int)ID, &VAR)) \
	{ \
		_Fail("Invalid internal resource id"); \
		return false; \
	} \
	if (VAR->mKind != KIND) \
	{ \
		_Fail("Invalid internal resource kind"); \
		return false; \
	} \
	if (VAR->mReleased) \
	{ \
		_Fail("Resource already released"); \
		return false; \
	}

#define CE_REMOVE_INTERNAL(VAR, ID, KIND) \
	if (!mInternalDataMap.Remove((int)ID, &VAR)) \
	{ \
		_Fail("Invalid internal resource id"); \
		return false; \
	} \
	if (VAR->mKind != KIND) \
	{ \
		_Fail("Invalid internal resource kind"); \
		return false; \
	} \
	if (VAR->mReleased) \
	{ \
		_Fail("Resource already released"); \
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
	if (isDebugging) \
		memset(stackPtr, 0, ceFunction->mFrameSize); \
	instPtr = &ceFunction->mCode[0]; \
	CE_CHECKSTACK();

static void CeSetAddrVal(void* ptr, int64 val, int32 ptrSize)
{
	if (ptrSize == 4)
		*(int32*)(ptr) = (int32)val;
	else
		*(int64*)(ptr) = (int64)val;
}

class CeAsyncOperation
{
public:
	CeInternalData* mInternalData;
	int mRefCount;
	uint8* mData;
	int mDataSize;
	int mReadSize;
	BfpFileResult mResult;

public:
	CeAsyncOperation()
	{
		mInternalData = NULL;
		mRefCount = 1;
		mData = NULL;
		mDataSize = 0;
		mReadSize = 0;
		mResult = BfpFileResult_Ok;
	}

	~CeAsyncOperation()
	{
		mInternalData->Release();
		delete mData;
	}

	void AddRef()
	{
		BfpSystem_InterlockedExchangeAdd32((uint32*)&mRefCount, 1);
	}

	void Release()
	{
		if (BfpSystem_InterlockedExchangeAdd32((uint32*)&mRefCount, (uint32)-1) == 1)
			delete this;
	}

	void Run()
	{
		mReadSize = BfpFile_Read(mInternalData->mFile, mData, mDataSize, -1, &mResult);
		Release();
	}

	static void RunProc(void* ptr)
	{
		((CeAsyncOperation*)ptr)->Run();
	}
};

bool CeContext::Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr, BfType*& returnType, BfType*& castReturnType)
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
	bool isDebugging = mCeMachine->mDebugger != NULL;

	volatile bool* specialCheckPtr = &mCeMachine->mSpecialCheck;
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

	auto _DbgPause = [&]()
	{
		int itr = 0;
		while (mCeMachine->mDebugger != NULL)
		{
			if (mCeMachine->mDbgPaused)
			{
				// This indicates a missed breakpoint, we should try to avoid this
				// Re-entrancy can cause this, from populating a type during cedebugger autocomplete
				OutputDebugStrF("CeMachine DbgPause reentry\n");
				return;
			}

			CePendingExpr* prevPendingExpr = NULL;

			///
			{
				AutoCrit autoCrit(mCeMachine->mCritSect);

				if ((mCeMachine->mDebugger->mDebugPendingExpr != NULL) && (itr == 0))
				{
					// Abandon evaluating expression
					prevPendingExpr = mCeMachine->mDebugger->mDebugPendingExpr;
					mCeMachine->mDebugger->mDebugPendingExpr = NULL;
				}

				if (itr == 0)
					mCallStack.Add(_GetCurFrame());
				mCeMachine->mDbgPaused = true;
			}

			mCeMachine->mDebugEvent.WaitFor();

			CePendingExpr* pendingExpr = NULL;

			///
			{
				AutoCrit autoCrit(mCeMachine->mCritSect);
				mCeMachine->mDbgPaused = false;

				if (mCeMachine->mStepState.mKind != CeStepState::Kind_Evaluate)
				{
					mCallStack.pop_back();
					_FixVariables();
					break;
				}

				mCeMachine->mStepState.mKind = CeStepState::Kind_None;
				String result;
				if (mCeMachine->mDebugger->mDebugPendingExpr != NULL)
					pendingExpr = mCeMachine->mDebugger->mDebugPendingExpr;
			}

			if (pendingExpr == NULL)
				continue;;

			pendingExpr->mResult = mCeMachine->mDebugger->DoEvaluate(pendingExpr, true);

			///
			{
				AutoCrit autoCrit(mCeMachine->mCritSect);
				pendingExpr->mDone = true;
				if (pendingExpr != mCeMachine->mDebugger->mDebugPendingExpr)
					delete pendingExpr;
			}

			itr++;
		}
	};

	auto _Fail = [&](const StringImpl& error)
	{
		auto bfError = Fail(_GetCurFrame(), error);
		if ((bfError != NULL) && (mCeMachine->mDebugger != NULL))
		{
			mCeMachine->mDebugger->OutputRawMessage(StrFormat("error %s", error.c_str()));
			_DbgPause();
		}
	};

	auto _CheckFastFinish = [&]()
	{
		if (*fastFinishPtr)
			return true;
		if (mCeMachine->mDbgWantBreak)
		{
			mCeMachine->mDbgWantBreak = false;
			_DbgPause();
		}
		return false;
	};

	auto _CheckFunction = [&](CeFunction* checkFunction, bool& handled)
	{
		if (checkFunction == NULL)
		{
			Fail(_GetCurFrame(), "Const method not available");
			return false;
		}

		if (mCeMachine->mDebugger != NULL)
		{
			if (checkFunction->mBreakpointVersion != mCeMachine->mDebugger->mBreakpointVersion)
				mCeMachine->mDebugger->UpdateBreakpoints(checkFunction);
		}
		else if (checkFunction->mBreakpointVersion != 0)
		{
			checkFunction->UnbindBreakpoints();
			checkFunction->mBreakpointVersion = 0;
		}

		if (checkFunction->mFunctionKind != CeFunctionKind_Normal)
		{
			if (checkFunction->mFunctionKind == CeFunctionKind_OOB)
			{
				Fail(_GetCurFrame(), "Array out of bounds");
				return false;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_OOB)
			{
				Fail(_GetCurFrame(), "Object not initialized");
				return false;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Malloc)
			{
				int64 size;
				if (ptrSize == 4)
					size = *(int32*)((uint8*)stackPtr + 4);
				else
					size = *(int64*)((uint8*)stackPtr + 8);
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
				int32 stackOffset = *(int32*)(stackPtr + ceModule->mSystem->mPtrSize);

				if (mCeMachine->mDebugger != NULL)
					mCeMachine->mDebugger->mPendingActiveFrameOffset = stackOffset;

				String error = "Fatal Error: ";
				GetStringFromAddr(strInstAddr, error);
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

				if (mCeMachine->mDebugger != NULL)
					mCeMachine->mDebugger->OutputMessage(str);
				else
					OutputDebugStr(str);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_DebugWrite_Int)
			{
				if (ceModule->mSystem->mPtrSize == 4)
				{
					int32 intVal = *(int32*)((uint8*)stackPtr + 0);
					OutputDebugStrF("Debug Val: %d %X\n", intVal, intVal);
				}
				else
				{
					int64 intVal = *(int64*)((uint8*)stackPtr + 0);
					OutputDebugStrF("Debug Val: %lld %llX\n", intVal, intVal);
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_GetReflectType)
			{
				addr_ce objAddr = *(addr_ce*)((uint8*)stackPtr + ceModule->mSystem->mPtrSize);
				CE_CHECKADDR(objAddr, 4);
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
			else if (checkFunction->mFunctionKind == CeFunctionKind_Type_ToString)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + ptrSize);

				BfType* type = GetBfType(typeId);
				bool success = false;
				if (type == NULL)
				{
					_Fail("Invalid type");
					return false;
				}

				SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCeMachine->mCeModule->mCurMethodInstance, mCallerMethodInstance);
				SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCeMachine->mCeModule->mCurTypeInstance, mCallerTypeInstance);
				CeSetAddrVal(stackPtr + 0, GetString(mCeMachine->mCeModule->TypeToString(type)), ptrSize);
				_FixVariables();
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Type_GetCustomAttribute)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + 1);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + 1 + 4);
				addr_ce resultPtr = *(addr_ce*)((uint8*)stackPtr + 1 + 4 + 4);

				BfType* type = GetBfType(typeId);
				bool success = false;
				if (type != NULL)
				{
					auto typeInst = type->ToTypeInstance();
					if (typeInst != NULL)
						success = GetCustomAttribute(mCurModule, typeInst->mConstHolder, typeInst->mCustomAttributes, attributeIdx, resultPtr);
					_FixVariables();
				}

				*(addr_ce*)(stackPtr + 0) = success;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Field_GetCustomAttribute)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + 1);
				int32 fieldIdx = *(int32*)((uint8*)stackPtr + 1 + 4);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + 1 + 4 + 4);
				addr_ce resultPtr = *(addr_ce*)((uint8*)stackPtr + 1 + 4 + 4 + 4);

				BfType* type = GetBfType(typeId);
				bool success = false;
				if (type != NULL)
				{
					auto typeInst = type->ToTypeInstance();
					if (typeInst != NULL)
					{
						if (typeInst->mDefineState < BfTypeDefineState_CETypeInit)
							mCurModule->PopulateType(typeInst);
						if ((fieldIdx >= 0) && (fieldIdx < typeInst->mFieldInstances.mSize))
						{
							auto& fieldInstance = typeInst->mFieldInstances[fieldIdx];
							success = GetCustomAttribute(mCurModule, typeInst->mConstHolder, fieldInstance.mCustomAttributes, attributeIdx, resultPtr);
							_FixVariables();
						}
						else if (fieldIdx != -1)
						{
							_Fail("Invalid field");
							return false;
						}
					}
				}

				*(addr_ce*)(stackPtr + 0) = success;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetCustomAttribute)
			{
				int64 methodHandle = *(int64*)((uint8*)stackPtr + 1);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + 1 + 8);
				addr_ce resultPtr = *(addr_ce*)((uint8*)stackPtr + 1 + 8 + 4);

				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				bool success = GetCustomAttribute(mCurModule, methodInstance->GetOwner()->mConstHolder, methodInstance->GetCustomAttributes(), attributeIdx, resultPtr);
				_FixVariables();
				*(addr_ce*)(stackPtr + 0) = success;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Type_GetCustomAttributeType)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + ptrSize);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + ptrSize + 4);

				BfType* type = GetBfType(typeId);
				addr_ce reflectType = 0;
				if (type != NULL)
				{
					auto typeInst = type->ToTypeInstance();
					if (typeInst != NULL)
					{
						auto attrType = GetCustomAttributeType(typeInst->mCustomAttributes, attributeIdx);
						if (attrType != NULL)
							reflectType = GetReflectType(attrType->mTypeId);
					}
					_FixVariables();
				}

				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Field_GetCustomAttributeType)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr + ptrSize);
				int32 fieldIdx = *(int32*)((uint8*)stackPtr + ptrSize + 4);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + ptrSize + 4 + 4);

				BfType* type = GetBfType(typeId);
				addr_ce reflectType = 0;
				if (type != NULL)
				{
					auto typeInst = type->ToTypeInstance();
					if (typeInst != NULL)
					{
						if (typeInst->mDefineState < BfTypeDefineState_CETypeInit)
							mCurModule->PopulateType(typeInst);
						if ((fieldIdx >= 0) && (fieldIdx < typeInst->mFieldInstances.mSize))
						{
							auto& fieldInstance = typeInst->mFieldInstances[fieldIdx];
							auto attrType = GetCustomAttributeType(fieldInstance.mCustomAttributes, attributeIdx);
							if (attrType != NULL)
								reflectType = GetReflectType(attrType->mTypeId);
							_FixVariables();
						}
						else if (fieldIdx != -1)
						{
							_Fail("Invalid field");
							return false;
						}
					}
				}

				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetCustomAttributeType)
			{
				int64 methodHandle = *(int64*)((uint8*)stackPtr + ptrSize);
				int32 attributeIdx = *(int32*)((uint8*)stackPtr + ptrSize + 8);

				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}
				auto attrType = GetCustomAttributeType(methodInstance->GetCustomAttributes(), attributeIdx);
				if (attrType != NULL)
					CeSetAddrVal(stackPtr + 0, GetReflectType(attrType->mTypeId), ptrSize);
				else
					CeSetAddrVal(stackPtr + 0, 0, ptrSize);
				_FixVariables();
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
					*(int64*)(stackPtr + 0) = 0;
				}
				else
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
				// int32 mGenericArgCount
				// int16 mFlags
				// int32 mMethodIdx

				int64 methodHandle = *(int64*)((uint8*)stackPtr + 4+4+4+2+4);

				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}

				int genericArgCount = 0;
				if (methodInstance->mMethodInfoEx != NULL)
					genericArgCount = methodInstance->mMethodInfoEx->mMethodGenericArguments.mSize;

				*(int32*)(stackPtr + 0) = methodInstance->mReturnType->mTypeId;
				*(int32*)(stackPtr + 4) = methodInstance->GetParamCount();
				*(int32*)(stackPtr + 4+4) = genericArgCount;
				*(int16*)(stackPtr + 4+4+4) = methodInstance->GetMethodFlags();
				*(int32*)(stackPtr + 4+4+4+2) = methodInstance->mMethodDef->mIdx;
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

				if (paramIdx < 0 || paramIdx >= methodInstance->mParams.mSize)
				{
					_Fail("paramIdx is out of range");
					return false;
				}

				enum ParamFlags
				{
					ParamFlag_None = 0,
					ParamFlag_Splat = 1,
					ParamFlag_Implicit = 2,
					ParamFlag_AppendIdx = 4,
					ParamFlag_Params = 8
				};

				ParamFlags paramFlags = ParamFlag_None;
				if (methodInstance->GetParamIsSplat(paramIdx))
					paramFlags = (ParamFlags)(paramFlags | ParamFlag_Splat);
				if (methodInstance->GetParamKind(paramIdx) == BfParamKind_AppendIdx)
					paramFlags = (ParamFlags)(paramFlags | ParamFlag_Implicit | ParamFlag_AppendIdx);
				if (methodInstance->GetParamKind(paramIdx) == BfParamKind_Params)
					paramFlags = (ParamFlags)(paramFlags | ParamFlag_Params);

				addr_ce stringAddr = GetString(methodInstance->GetParamName(paramIdx));
				_FixVariables();
				*(int32*)(stackPtr + 0) = methodInstance->GetParamType(paramIdx)->mTypeId;
				*(int16*)(stackPtr + 4) = (int16)paramFlags;
				CeSetAddrVal(stackPtr + 4+2, stringAddr, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Method_GetGenericArg)
			{
				int64 methodHandle = *(int64*)((uint8*)stackPtr + ptrSize);
				int32 genericArgIdx = *(int32*)((uint8*)stackPtr + ptrSize + 8);

				auto methodInstance = mCeMachine->GetMethodInstance(methodHandle);
				if (methodInstance == NULL)
				{
					_Fail("Invalid method instance");
					return false;
				}

				if ((methodInstance->mMethodInfoEx == NULL) || (genericArgIdx < 0) || (genericArgIdx >= methodInstance->mMethodInfoEx->mMethodGenericArguments.mSize))
				{
					_Fail("genericArgIdx is out of range");
					return false;
				}

				auto reflectType = GetReflectType(methodInstance->mMethodInfoEx->mMethodGenericArguments[genericArgIdx]->mTypeId);
				_FixVariables();
				CeSetAddrVal(stackPtr + 0, reflectType, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_SetReturnType)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr);
				if (returnType->IsVar())
					castReturnType = GetBfType(typeId);
				else
					_Fail("Comptime return types can only be set on methods declared with a 'var' return type");
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Align)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr);
				int32 align = *(int32*)((uint8*)stackPtr + sizeof(int32));
				if ((mCurEmitContext == NULL) || (mCurEmitContext->mType == NULL) || (mCurEmitContext->mType->mTypeId != typeId))
				{
					_Fail("This type cannot be modified in this context");
					return false;
				}
				mCurEmitContext->mAlign = BF_MAX(mCurEmitContext->mAlign, align);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitTypeBody)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr);
				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr + sizeof(int32));
				if ((mCurEmitContext == NULL) || (mCurEmitContext->mType == NULL) || (mCurEmitContext->mType->mTypeId != typeId))
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
			else if (checkFunction->mFunctionKind == CeFunctionKind_EmitAddInterface)
			{
				int32 typeId = *(int32*)((uint8*)stackPtr);
				int32 ifaceTypeId = *(int32*)((uint8*)stackPtr + sizeof(int32));
				if ((mCurEmitContext == NULL) || (mCurEmitContext->mType->mTypeId != typeId))
				{
					_Fail("Code cannot be emitted for this type in this context");
					return false;
				}
				mCurEmitContext->mInterfaces.Add(ifaceTypeId);
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
				SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurModule->mCurMethodInstance, mCallerMethodInstance);
				SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurModule->mCurTypeInstance, mCallerTypeInstance);

// 				int32 strInstAddr = *(int32*)((uint8*)stackPtr + 0);
// 				String emitStr;
// 				if (!GetStringFromAddr(strInstAddr, emitStr))
// 				{
// 					_Fail("Invalid String");
// 					return false;
// 				}

				addr_ce strViewPtr = *(addr_ce*)((uint8*)stackPtr);
				String emitStr;
				if (!GetStringFromStringView(strViewPtr, emitStr))
				{
					_Fail("Invalid StringView");
					return false;
				}

				mCurModule->CEMixin(mCurCallSource->mRefNode, emitStr);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_Sleep)
			{
				int32 sleepMS = *(int32*)((uint8*)stackPtr);
				while (sleepMS > 0)
				{
					if (_CheckFastFinish())
						break;

					if (sleepMS > 20)
					{
						BfpThread_Sleep(20);
						sleepMS -= 20;
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
					checkAddr++;
				}
				CE_CHECKADDR(strAddr, checkAddr - strAddr + 1);

				char* strPtr = (char*)(memStart + strAddr);
				char** endPtr = NULL;
				if (endAddr != 0)
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
			else if (checkFunction->mFunctionKind == CeFunctionKind_Float_ToString)
			{
				int32& result = *(int32*)((uint8*)stackPtr + 0);
				float val = *(float*)((uint8*)stackPtr + 4);
				addr_ce strAddr = *(addr_ce*)((uint8*)stackPtr + 4 + 4);

				char str[256];
				int count = FloatToString(val, str);
				CE_CHECKADDR(strAddr, count + 1);
				memcpy(memStart + strAddr, str, count + 1);
				result = count;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_Create)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				BfpDirectory_Create(path.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_Rename)
			{
				addr_ce srcAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce destAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String srcPath;
				CE_CHECKADDR_STR(srcPath, srcAddr);
				String destPath;
				CE_CHECKADDR_STR(destPath, destAddr);
				FixRelativePath(srcPath);
				FixRelativePath(destPath);
				BfpDirectory_Rename(srcPath.c_str(), destPath.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_Delete)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				BfpDirectory_Delete(path.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_GetCurrent)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				CalcWorkingDir();
				TryStringOut(mWorkingDir, namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_SetCurrent)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);

				if (::BfpDirectory_Exists(path.c_str()))
				{
					mWorkingDir = path;
					if (outResultAddr != 0)
						*(BfpFileResult*)(memStart + outResultAddr) = BfpFileResult_Ok;
				}
				else
				{
					if (outResultAddr != 0)
						*(BfpFileResult*)(memStart + outResultAddr) = BfpFileResult_NotFound;
				}
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_Exists)
			{
				bool& result = *(bool*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 1);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				result = BfpDirectory_Exists(path.c_str());
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpDirectory_GetSysDirectory)
			{
				BfpSysDirectoryKind sysDirKind = *(BfpSysDirectoryKind*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 4);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + 4 + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + 4 + ptrSize + ptrSize);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				BfpDirectory_GetSysDirectory(sysDirKind, namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Close)
			{
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);

				CE_CHECKADDR(outResultAddr, 4);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				BfpFile_Close(internalData->mFile, (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Create)
			{
				void* resultPtr = ((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				int createKind = *(int*)((uint8*)stackPtr + ptrSize + ptrSize);
				int createFlags = *(int*)((uint8*)stackPtr + ptrSize + ptrSize + 4);
				int createFileAttrs = *(int*)((uint8*)stackPtr + ptrSize + ptrSize + 4 + 4);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + 4 + 4 + 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				CE_CHECKADDR(outResultAddr, 4);
				FixRelativePath(path);
				auto bfpFile = BfpFile_Create(path.c_str(), (BfpFileCreateKind)createKind, (BfpFileCreateFlags)createFlags, (BfpFileAttributes)createFileAttrs, (BfpFileResult*)(memStart + outResultAddr));
				if (bfpFile != NULL)
				{
					if ((createKind == BfpFileCreateKind_OpenExisting) || (createKind == BfpFileCreateKind_OpenAlways))
						AddFileRebuild(path);
					CeInternalData* internalData = new CeInternalData();
					internalData->mKind = CeInternalData::Kind_File;
					internalData->mFile = bfpFile;
					mInternalDataMap[++mCurHandleId] = internalData;
					CeSetAddrVal(resultPtr, mCurHandleId, ptrSize);
				}
				else
					CeSetAddrVal(resultPtr, 0, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Flush)
			{
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 0);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				BfpFile_Flush(internalData->mFile);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetFileSize)
			{
				int64& result = *(int64*)((uint8*)stackPtr + 0);
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				result = BfpFile_GetFileSize(internalData->mFile);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Read)
			{
				void* resultPtr = ((uint8*)stackPtr + 0);
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce bufferPtr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				intptr bufferSize = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);
				int timeoutMS = *(int32*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize + ptrSize);

				CE_CHECKADDR(bufferPtr, bufferSize);
				CE_CHECKADDR(outResultAddr, 4);

				BfpFileResult fileResult = BfpFileResult_UnknownError;

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);

				int timeoutLeft = timeoutMS;
				int64 result = 0;
				CeAsyncOperation* asyncOperation = NULL;
				BfpThread* asyncThread = NULL;
				while (true)
				{
					if (*cancelingPtr)
						break;

					int useTimeout = timeoutLeft;
					if (useTimeout < 0)
					{
						useTimeout = 20;
					}
					else if (useTimeout > 20)
					{
						useTimeout = 20;
						timeoutLeft -= useTimeout;
					}
					else
						timeoutLeft = 0;

					if (asyncOperation != NULL)
					{
						if (BfpThread_WaitFor(asyncThread, useTimeout))
						{
							if (asyncOperation->mReadSize > 0)
								memcpy(memStart + bufferPtr, asyncOperation->mData, asyncOperation->mReadSize);
							result = asyncOperation->mReadSize;
							fileResult = asyncOperation->mResult;
							break;
						}
						continue;
					}

					result = BfpFile_Read(internalData->mFile, memStart + bufferPtr, bufferSize, useTimeout, &fileResult);
					if (fileResult == BfpFileResult_Timeout)
					{
						if (timeoutLeft > 0)
							continue;
					}

					if (fileResult != BfpFileResult_InvalidParameter)
						break;

					BF_ASSERT(asyncOperation == NULL);

					asyncOperation = new CeAsyncOperation();
					asyncOperation->mInternalData = internalData;
					asyncOperation->mInternalData->AddRef();
					asyncOperation->mData = new uint8[bufferSize];
					asyncOperation->mDataSize = bufferSize;

					asyncOperation->AddRef();
					asyncThread = BfpThread_Create(CeAsyncOperation::RunProc, asyncOperation);
				}

				if (asyncOperation != NULL)
					asyncOperation->Release();
				if (asyncThread != NULL)
					BfpThread_Release(asyncThread);

				if (outResultAddr != 0)
					*(BfpFileResult*)(memStart + outResultAddr) = fileResult;

				CeSetAddrVal(resultPtr, result, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Release)
			{
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 0);

				CeInternalData* internalData = NULL;
				CE_REMOVE_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				internalData->Release();
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Seek)
			{
				int64& result = *(int64*)((uint8*)stackPtr + 0);
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 8);
				int64 offset = *(int64*)((uint8*)stackPtr + 8 + ptrSize);
				int seekKind = *(int*)((uint8*)stackPtr + 8 + ptrSize + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				result = BfpFile_Seek(internalData->mFile, offset, (BfpFileSeekKind)seekKind);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Truncate)
			{
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);

				CE_CHECKADDR(outResultAddr, 4);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				BfpFile_Truncate(internalData->mFile, (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Write)
			{
				void* resultPtr = ((uint8*)stackPtr + 0);
				addr_ce fileId = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce bufferPtr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				intptr bufferSize = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);
				int timeoutMS = *(int32*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize + ptrSize);

				CE_CHECKADDR(bufferPtr, bufferSize);
				CE_CHECKADDR(outResultAddr, 4);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)fileId, CeInternalData::Kind_File);
				int64 result = BfpFile_Write(internalData->mFile, memStart + bufferPtr, bufferSize, timeoutMS, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
				CeSetAddrVal(resultPtr, result, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetTime_LastWrite)
			{
				BfpTimeStamp& result = *(BfpTimeStamp*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 8);
				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				AddFileRebuild(path);
				result = BfpFile_GetTime_LastWrite(path.c_str());
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetAttributes)
			{
				BfpFileAttributes& result = *(BfpFileAttributes*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 4);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + 4);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				result = BfpFile_GetAttributes(path.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_SetAttributes)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				BfpFileAttributes attribs = *(BfpFileAttributes*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				BfpFile_SetAttributes(path.c_str(), attribs, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Copy)
			{
				addr_ce srcAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce destAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				BfpFileCopyKind fileCopyKind = *(BfpFileCopyKind*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + 4);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String srcPath;
				CE_CHECKADDR_STR(srcPath, srcAddr);
				String destPath;
				CE_CHECKADDR_STR(destPath, destAddr);
				FixRelativePath(srcPath);
				FixRelativePath(destPath);
				BfpFile_Copy(srcPath.c_str(), destPath.c_str(), fileCopyKind, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Rename)
			{
				addr_ce srcAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce destAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String srcPath;
				CE_CHECKADDR_STR(srcPath, srcAddr);
				String destPath;
				CE_CHECKADDR_STR(destPath, destAddr);
				FixRelativePath(srcPath);
				FixRelativePath(destPath);
				BfpFile_Rename(srcPath.c_str(), destPath.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Delete)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				BfpFile_Delete(path.c_str(), (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_Exists)
			{
				bool& result = *(bool*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 1);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				FixRelativePath(path);
				AddFileRebuild(path);
				result = BfpFile_Exists(path.c_str());
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetTempPath)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				BfpFile_GetTempPath(namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetTempFileName)
			{
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				BfpFile_GetTempFileName(namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetFullPath)
			{
				addr_ce srcAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);

				String srcPath;
				CE_CHECKADDR_STR(srcPath, srcAddr);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				FixRelativePath(srcPath);
				BfpFile_GetFullPath(srcPath.c_str(), namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFile_GetActualPath)
			{
				addr_ce srcAddr = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);

				String srcPath;
				CE_CHECKADDR_STR(srcPath, srcAddr);

				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				FixRelativePath(srcPath);
				BfpFile_GetActualPath(srcPath.c_str(), namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSpawn_Create)
			{
				void* resultPtr = ((uint8*)stackPtr + 0);
				addr_ce targetPathAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce argsAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce workingDirAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);
				addr_ce envAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize);
				int flags = *(int*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize + ptrSize + ptrSize + 4);

				String targetPath;
				CE_CHECKADDR_STR(targetPath, targetPathAddr);
				String args;
				if (argsAddr != 0)
					CE_CHECKADDR_STR(args, argsAddr);
				String workingDir;
				if (workingDirAddr != 0)
					CE_CHECKADDR_STR(workingDir, workingDirAddr);
				String env;
				if (envAddr != 0)
					CE_CHECKADDR_STR(env, envAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				if ((targetPath.Contains('/')) || (targetPath.Contains('\\')))
				{
					FixRelativePath(targetPath);
				}

				auto bfpSpawn = BfpSpawn_Create(targetPath.c_str(),
					(argsAddr == 0) ? NULL : args.c_str(),
					(workingDirAddr == 0) ? NULL : workingDir.c_str(),
					(envAddr == 0) ? NULL : env.c_str(), (BfpSpawnFlags)flags, (outResultAddr == 0) ? NULL : (BfpSpawnResult*)(memStart + outResultAddr));
				if (bfpSpawn != NULL)
				{
					CeInternalData* internalData = new CeInternalData();
					internalData->mKind = CeInternalData::Kind_Spawn;
					internalData->mSpawn = bfpSpawn;
					mInternalDataMap[++mCurHandleId] = internalData;
					CeSetAddrVal(resultPtr, mCurHandleId, ptrSize);
				}
				else
					CeSetAddrVal(resultPtr, 0, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSpawn_GetStdHandles)
			{
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce outStdInAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce outStdOutAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outStdErrAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);

				if (outStdInAddr != 0)
					CE_CHECKADDR(outStdInAddr, ptrSize);
				if (outStdOutAddr != 0)
					CE_CHECKADDR(outStdOutAddr, ptrSize);
				if (outStdErrAddr != 0)
					CE_CHECKADDR(outStdErrAddr, ptrSize);

				BfpFile* outStdIn = NULL;
				BfpFile* outStdOut = NULL;
				BfpFile* outStdErr = NULL;

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_Spawn);
				BfpSpawn_GetStdHandles(internalData->mSpawn,
					(outStdInAddr != 0) ? &outStdIn : NULL,
					(outStdOutAddr != 0) ? &outStdOut : NULL,
					(outStdErrAddr != 0) ? &outStdErr : NULL);

				auto _SetHandle = [&](addr_ce addr, BfpFile* file)
				{
					if (addr == 0)
						return;
					if (file != NULL)
					{
						CeInternalData* internalData = new CeInternalData();
						internalData->mKind = CeInternalData::Kind_File;
						internalData->mFile = file;
						mInternalDataMap[++mCurHandleId] = internalData;
						CeSetAddrVal(memStart + addr, mCurHandleId, ptrSize);
					}
				};

				if (outStdInAddr != 0)
					_SetHandle(outStdInAddr, outStdIn);
				if (outStdOutAddr != 0)
					_SetHandle(outStdOutAddr, outStdOut);
				if (outStdErrAddr != 0)
					_SetHandle(outStdErrAddr, outStdErr);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSpawn_Kill)
			{
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 0);
				int exitCode = *(int*)((uint8*)stackPtr + ptrSize);
				int killFlags = *(int*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);

				CE_CHECKADDR(outResultAddr, 4);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_Spawn);
				BfpSpawn_Kill(internalData->mSpawn, exitCode, (BfpKillFlags)killFlags, (BfpSpawnResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSpawn_Release)
			{
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 0);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_Spawn);
				internalData->mReleased = true;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpSpawn_WaitFor)
			{
				bool& result = *(bool*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 1);
				int waitMS = *(int*)((uint8*)stackPtr + 1 + ptrSize);
				addr_ce outExitCodeAddr = *(addr_ce*)((uint8*)stackPtr + 1 + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + 1 + ptrSize + ptrSize + ptrSize);

				CE_CHECKADDR(outExitCodeAddr, ptrSize);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_Spawn);

				int outExitCode = 0;
				int timeLeft = waitMS;
				do
				{
					if (_CheckFastFinish())
					{
						result = false;
						break;
					}

					int waitTime = 20;
					if (timeLeft >= 0)
					{
						waitTime = BF_MIN(timeLeft, 20);
						timeLeft -= waitTime;
					}

					result = BfpSpawn_WaitFor(internalData->mSpawn, waitTime, &outExitCode, (outResultAddr == 0) ? NULL : (BfpSpawnResult*)(memStart + outResultAddr));
					if (result)
						break;
					if (waitTime == 0)
						break;
				} while (true);
				*(int*)(memStart + outExitCodeAddr) = outExitCode;
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_FindFirstFile)
			{
				void* resultPtr = ((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				int flags = *(int*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + 4);

				String path;
				CE_CHECKADDR_STR(path, nameAddr);
				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				FixRelativePath(path);
				auto bfpFindFileData = BfpFindFileData_FindFirstFile(path.c_str(), (BfpFindFileFlags)flags, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
				if (bfpFindFileData != NULL)
				{
					String dir = GetFileDir(path);
					dir = FixPathAndCase(dir);
					dir.Append(DIR_SEP_CHAR);

					CeRebuildKey rebuildKey;
					rebuildKey.mKind = CeRebuildKey::Kind_Directory;
					rebuildKey.mString = dir;
					CeRebuildValue rebuildValue;
					if (AddRebuild(rebuildKey, rebuildValue))
						mCurModule->mCompiler->mRebuildFileSet.Add(dir);

					CeInternalData* internalData = new CeInternalData();
					internalData->mKind = CeInternalData::Kind_FindFileData;
					internalData->mFindFileData = bfpFindFileData;
					mInternalDataMap[++mCurHandleId] = internalData;
					CeSetAddrVal(resultPtr, mCurHandleId, ptrSize);
				}
				else
					CeSetAddrVal(resultPtr, 0, ptrSize);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_FindNextFile)
			{
				bool& result = *(bool*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 1);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_FindNextFile(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetFileName)
			{
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 0);
				addr_ce nameAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize);
				addr_ce sizeAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize);
				addr_ce outResultAddr = *(addr_ce*)((uint8*)stackPtr + ptrSize + ptrSize + ptrSize);

				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				CE_CHECKADDR(sizeAddr, 4);
				int& nameSize = *(int*)(memStart + sizeAddr);
				CE_CHECKADDR(nameAddr, nameSize);
				char* namePtr = (char*)(memStart + nameAddr);

				if (outResultAddr != 0)
					CE_CHECKADDR(outResultAddr, 4);

				BfpFindFileData_GetFileName(internalData->mFindFileData, namePtr, &nameSize, (outResultAddr == 0) ? NULL : (BfpFileResult*)(memStart + outResultAddr));
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetTime_LastWrite)
			{
				BfpTimeStamp& result = *(BfpTimeStamp*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_GetTime_LastWrite(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetTime_Created)
			{
				BfpTimeStamp& result = *(BfpTimeStamp*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_GetTime_Created(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetTime_Access)
			{
				BfpTimeStamp& result = *(BfpTimeStamp*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_GetTime_Access(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetFileAttributes)
			{
				BfpFileAttributes& result = *(BfpFileAttributes*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 4);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_GetFileAttributes(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_GetFileSize)
			{
				int64& result = *(int64*)((uint8*)stackPtr + 0);
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 8);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				result = BfpFindFileData_GetFileSize(internalData->mFindFileData);
			}
			else if (checkFunction->mFunctionKind == CeFunctionKind_BfpFindFileData_Release)
			{
				addr_ce spawnId = *(addr_ce*)((uint8*)stackPtr + 0);
				CeInternalData* internalData = NULL;
				CE_GET_INTERNAL(internalData, (int)spawnId, CeInternalData::Kind_FindFileData);
				internalData->mReleased = true;
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

		if ((mCeMachine->mDebugger != NULL) && (!mCallStack.IsEmpty()))
			_Fail(StrFormat("Attempting to call failed method '%s'", ceModule->MethodToString(checkFunction->mMethodInstance).c_str()));

		auto error = Fail(_GetCurFrame(), StrFormat("Method call preparation '%s' failed", ceModule->MethodToString(checkFunction->mMethodInstance).c_str()));
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
	int instCount = 0;

	CE_CHECKSTACK();

	while (true)
	{
		++instCount;
		CeOp op = CE_GETINST(CeOp);

		if (*specialCheckPtr)
		{
		SpecialCheck:
			if (*fastFinishPtr)
			{
				if ((mCurModule != NULL) && (mCurModule->mCurTypeInstance != NULL))
				{
					mCurModule->mCurTypeInstance->mRebuildFlags = (BfTypeRebuildFlags)(mCurModule->mCurTypeInstance->mRebuildFlags | BfTypeRebuildFlag_ConstEvalCancelled);
					mCurModule->DeferRebuildType(mCurModule->mCurTypeInstance);
				}
				if (*cancelingPtr)
				{
					if ((mCurModule == NULL) || (mCurModule->mCurTypeInstance == NULL))
						_Fail("Comptime evaluation canceled");
				}
				return false;
			}

			bool wantsStop = false;

			if (mCeMachine->mStepState.mKind != CeStepState::Kind_None)
			{
				int curDepth = mCallStack.mSize + 1;

				int instIdx = instPtr - ceFunction->mCode.mVals - 1;

				switch (mCeMachine->mStepState.mKind)
				{
				case CeStepState::Kind_StepInfo:
					if (curDepth != mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					else if (instIdx >= mCeMachine->mStepState.mNextInstIdx)
						wantsStop = true;
					break;
				case CeStepState::Kind_StepInfo_Asm:
					wantsStop = true;
					break;
				case CeStepState::Kind_StepOver:
					if (curDepth < mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					else if ((mCeMachine->mStepState.mStartDepth == curDepth) && (instIdx >= mCeMachine->mStepState.mNextInstIdx))
						wantsStop = true;
					break;
				case CeStepState::Kind_StepOver_Asm:
					if (curDepth < mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					else if (curDepth == mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					break;
				case CeStepState::Kind_StepOut:
					if (curDepth < mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					else if ((curDepth == mCeMachine->mStepState.mStartDepth) && (instIdx >= mCeMachine->mStepState.mNextInstIdx))
						wantsStop = true;
					break;
				case CeStepState::Kind_StepOut_Asm:
					if (curDepth <= mCeMachine->mStepState.mStartDepth)
						wantsStop = true;
					break;
				case CeStepState::Kind_Jmp:
					instPtr = &ceFunction->mCode[mCeMachine->mStepState.mNextInstIdx];
					op = CE_GETINST(CeOp);
					wantsStop = true;
					break;
				}
			}
			else if (mCeMachine->mDbgWantBreak)
			{
				wantsStop = true;
			}
			else if ((mCeMachine->mDebugger != NULL) && (mCeMachine->mDebugger->mBreakpointFramesDirty))
			{
				AutoCrit autoCrit(mCeMachine->mCritSect);
				mCallStack.Add(_GetCurFrame());
				mCeMachine->mDebugger->UpdateBreakpointFrames();
				mCallStack.pop_back();
			}
			else
				*specialCheckPtr = false;

			if (wantsStop)
			{
				mCeMachine->mDbgWantBreak = false;
				mCeMachine->mStepState.mKind = CeStepState::Kind_None;
				_DbgPause();
				if (mCeMachine->mStepState.mKind == CeStepState::Kind_Jmp)
					goto SpecialCheck;
				// We may have changed breakpoints so we need to re-read
				instPtr -= sizeof(CeOp);
				op = CE_GETINST(CeOp);
			}
		}

		OpSwitch:
		switch (op)
		{
		case CeOp_Nop:
			break;
		case CeOp_DbgBreak:
		{
			bool foundBreakpoint = false;
			bool skipInst = false;

			if (mCeMachine->mDebugger != NULL)
			{
				AutoCrit autoCrit(mCeMachine->mCritSect);
				int instIdx = instPtr - ceFunction->mCode.mVals - 2;
				CeBreakpointBind* breakpointEntry = NULL;
				if (ceFunction->mBreakpoints.TryGetValue(instIdx, &breakpointEntry))
				{
					bool doBreak = false;

					mCallStack.Add(_GetCurFrame());
					if (mCeMachine->mDebugger->CheckConditionalBreakpoint(breakpointEntry->mBreakpoint))
						doBreak = true;
					mCallStack.pop_back();

					op = breakpointEntry->mPrevOpCode;
					// Keep us from an infinite loop if we set a breakpoint on a manual Break
					skipInst = op == CeOp_DbgBreak;

					foundBreakpoint = true;

					if (!doBreak)
					{
						_FixVariables();
						if (skipInst)
							break;
						goto OpSwitch;
					}

					mCeMachine->mDebugger->mActiveBreakpoint = breakpointEntry->mBreakpoint;
				}
			}

			_DbgPause();
			if (mCeMachine->mStepState.mKind == CeStepState::Kind_Jmp)
				goto SpecialCheck;
			if (skipInst)
				break;
			if (foundBreakpoint)
				goto OpSwitch;
		}
		break;
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
			int64 size;
			if (ptrSize == 4)
				size = CE_GETFRAME(int32);
			else
				size = CE_GETFRAME(int64);
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
				if ((mStaticCtorExecSet.TryAdd(ceStaticFieldEntry.mTypeId, NULL)) && (!ceStaticFieldEntry.mName.StartsWith("#")))
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
					if (ctorCallFunction->mInitializeState < CeFunction::InitializeState_Initialized)
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

					if (ceStaticFieldEntry.mName.StartsWith("#"))
					{
						addr_ce resultAddr = 0;

						if (ceStaticFieldEntry.mName == "#CallerLineNum")
						{
							*(int*)ptr = mCurModule->mCurFilePosition.mCurLine;
						}
						else if (ceStaticFieldEntry.mName == "#CallerFilePath")
						{
							String filePath;
							if (mCurModule->mCurFilePosition.mFileInstance != NULL)
								filePath = mCurModule->mCurFilePosition.mFileInstance->mParser->mFileName;
							resultAddr = GetString(filePath);
						}
						else if (ceStaticFieldEntry.mName == "#CallerFileName")
						{
							String filePath;
							if (mCurModule->mCurFilePosition.mFileInstance != NULL)
								filePath = mCurModule->mCurFilePosition.mFileInstance->mParser->mFileName;
							resultAddr = GetString(GetFileName(filePath));
						}
						else if (ceStaticFieldEntry.mName == "#CallerFileDir")
						{
							String filePath;
							if (mCurModule->mCurFilePosition.mFileInstance != NULL)
								filePath = mCurModule->mCurFilePosition.mFileInstance->mParser->mFileName;
							resultAddr = GetString(GetFileDir(filePath));
						}
						else if (ceStaticFieldEntry.mName == "#CallerTypeName")
						{
							String typeName = "";
							typeName = mCeMachine->mCeModule->TypeToString(mCallerTypeInstance);
							resultAddr = GetString(typeName);
						}
						else if (ceStaticFieldEntry.mName == "#CallerType")
						{
							addr_ce typeAddr = GetReflectType(mCallerTypeInstance->mTypeId);
							resultAddr = typeAddr;
						}
						else if (ceStaticFieldEntry.mName == "#CallerMemberName")
						{
							String memberName = mCeMachine->mCeModule->MethodToString(mCallerMethodInstance);
							resultAddr = GetString(memberName);
						}
						else if (ceStaticFieldEntry.mName == "#CallerProject")
						{
							BfProject* project = NULL;
							project = mCallerTypeInstance->mTypeDef->mProject;
							if (project != NULL)
								resultAddr = GetString(project->mName);
						}
						else if (ceStaticFieldEntry.mName == "#OrigCalleeType")
						{
							if (mCurCallSource->mOrigCalleeType != NULL)
							{
								addr_ce typeAddr = GetReflectType(mCurCallSource->mOrigCalleeType->mTypeId);
								resultAddr = typeAddr;
							}
						}

						if (resultAddr != 0)
						{
							_FixVariables();
							CeSetAddrVal(memStart + staticFieldInfo->mAddr, resultAddr, ptrSize);
						}
					}
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
						auto ceFunction = mCeMachine->QueueMethod(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc);
						ceFunction->mCeFunctionInfo->mRefCount++;
						mCeMachine->DerefMethodInfo(callEntry.mFunctionInfo);
						callEntry.mFunctionInfo = ceFunction->mCeFunctionInfo;
					}
				}

				if (callEntry.mFunctionInfo->mCeFunction == NULL)
				{
					_Fail("Method not generated");
					return false;
				}

				callEntry.mFunction = callEntry.mFunctionInfo->mCeFunction;
				if (callEntry.mFunction->mInitializeState < CeFunction::InitializeState_Initialized)
				{
					auto curFrame = _GetCurFrame();
					SetAndRestoreValue<CeFrame*> prevFrame(mCurFrame, &curFrame);
					BF_ASSERT(callEntry.mFunction->mInitializeState < CeFunction::InitializeState_Initialized);
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
				ceModule->PopulateType(valueType, BfPopulateType_Full_Force);
			if (valueType->mVirtualMethodTable.IsEmpty())
			{
				_Fail("Empty virtual table");
				return false;
			}
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
							if (valueType->mInterfaceMethodTable.IsEmpty())
								ceModule->PopulateType(valueType, BfPopulateType_Full_Force);
							if (valueType->mInterfaceMethodTable.IsEmpty())
							{
								_Fail("Empty interface table");
								return false;
							}
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
		case CeOp_Conv_F32_U8:
			CE_CAST(float, uint8);
			break;
		case CeOp_Conv_F32_U16:
			CE_CAST(float, uint16);
			break;
		case CeOp_Conv_F32_U32:
			CE_CAST(float, uint32);
			break;
		case CeOp_Conv_F32_U64:
			CE_CAST(float, uint64);
			break;
		case CeOp_Conv_F64_U8:
			CE_CAST(float, uint8);
			break;
		case CeOp_Conv_F64_U16:
			CE_CAST(float, uint16);
			break;
		case CeOp_Conv_F64_U32:
			CE_CAST(float, uint32);
			break;
		case CeOp_Conv_F64_U64:
			CE_CAST(float, uint64);
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
			break;
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

		//BF_ASSERT(_CrtCheckMemory() != 0);
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
	mCurCallSource = NULL;
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

	mDebugger = NULL;
	mDbgPaused = false;
	mDbgWantBreak = false;
	mSpecialCheck = false;

	BfLogSys(mCompiler->mSystem, "CeMachine::CeMachine %p\n", this);
}

CeMachine::~CeMachine()
{
	BF_ASSERT(mDebugger == NULL);

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
	BF_ASSERT(mCeModule == NULL);

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

	BF_ASSERT(mCeModule->mBfIRBuilder == NULL);
	mCeModule->mBfIRBuilder = new BfIRBuilder(mCeModule);
	mCeModule->mBfIRBuilder->mDbgVerifyCodeGen = true;
	mCeModule->FinishInit();
	mCeModule->mBfIRBuilder->mHasDebugInfo = true;
	mCeModule->mHasFullDebugInfo = mDebugger != NULL;
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
	mSpecialCheck = false;
	mRevision++;
	mMethodBindRevision++;
	mDbgWantBreak = false;
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

#define CE_SIZE_GET(T) *((T*)(ptr += sizeof(T)) - 1)

int CeMachine::GetInstSize(CeFunction* ceFunction, int instIdx)
{
	auto ptr = &ceFunction->mCode[instIdx];
	auto startPtr = ptr;

	auto _HandleOperand = [&](CeOperandInfoKind kind)
	{
		switch (kind)
		{
		case CEOI_FrameRef:
		case CEOI_FrameRef8:
		case CEOI_FrameRef16:
		case CEOI_FrameRef32:
		case CEOI_FrameRef64:
		case CEOI_FrameRefF32:
		case CEOI_FrameRefF64:
			ptr += 4;
			break;
		case CEOI_IMM8:
			ptr += 1;
			break;
		case CEOI_IMM16:
			ptr += 2;
			break;
		case CEOI_IMM32:
			ptr += 4;
			break;
		case CEOI_IMM64:
			ptr += 8;
			break;
		case CEOI_IMM_VAR:
			{
				int32 size = CE_SIZE_GET(int32);
				ptr += size;
			}
			break;
		case CEOI_JMPREL:
			ptr += 4;
			break;
		default:
			BF_ASSERT("Unhandled");
		}
	};

	auto op = CE_SIZE_GET(CeOp);

	if (op == CeOp_DbgBreak)
	{
		CeBreakpointBind* breakpointEntry = NULL;
		if (ceFunction->mBreakpoints.TryGetValue(instIdx, &breakpointEntry))
			op = breakpointEntry->mPrevOpCode;
	}

	CeOpInfo& opInfo = gOpInfo[op];

	_HandleOperand(opInfo.mResultKind);
	if ((opInfo.mFlags & CeOpInfoFlag_SizeX) != 0)
		ptr += 4;
	_HandleOperand(opInfo.mOperandA);
	_HandleOperand(opInfo.mOperandB);
	_HandleOperand(opInfo.mOperandC);

	return (int)(ptr - startPtr);
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

void CeMachine::RemoveFunc(CeFunction* ceFunction)
{
	mFunctionIdMap.Remove(ceFunction->mId);
	ceFunction->mId = -1;
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
				RemoveFunc(ceFunction);
				for (auto innerFunction : ceFunction->mInnerFunctions)
					RemoveFunc(innerFunction);
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
	methodInstance->mInCEMachine = false;
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
	else if (auto constGep = BeValueDynCast<BeGEP2Constant>(constVal))
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
				else if (methodDef->mName == "Comptime_Type_ToString")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Type_ToString;
				}
				else if (methodDef->mName == "Comptime_Type_GetCustomAttribute")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Type_GetCustomAttribute;
				}
				else if (methodDef->mName == "Comptime_Field_GetCustomAttribute")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Field_GetCustomAttribute;
				}
				else if (methodDef->mName == "Comptime_Method_GetCustomAttribute")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetCustomAttribute;
				}
				else if (methodDef->mName == "Comptime_Type_GetCustomAttributeType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Type_GetCustomAttributeType;
				}
				else if (methodDef->mName == "Comptime_Field_GetCustomAttributeType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Field_GetCustomAttributeType;
				}
				else if (methodDef->mName == "Comptime_Method_GetCustomAttributeType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetCustomAttributeType;
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
				else if (methodDef->mName == "Comptime_Method_GetGenericArg")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Method_GetGenericArg;
				}
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mCompilerTypeDef))
			{
				if (methodDef->mName == "Comptime_SetReturnType")
				{
					ceFunction->mFunctionKind = CeFunctionKind_SetReturnType;
				}
				else if (methodDef->mName == "Comptime_Align")
				{
					ceFunction->mFunctionKind = CeFunctionKind_Align;
				}
				else if (methodDef->mName == "Comptime_EmitTypeBody")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitTypeBody;
				}
				else if (methodDef->mName == "Comptime_EmitAddInterface")
				{
					ceFunction->mFunctionKind = CeFunctionKind_EmitAddInterface;
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
				else if (methodDef->mName == "ThrowObjectNotInitialized")
					ceFunction->mFunctionKind = CeFunctionKind_ObjectNotInitialized;
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
				if (methodDef->mName == "BfpDirectory_Create")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_Create;
				else if (methodDef->mName == "BfpDirectory_Rename")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_Rename;
				else if (methodDef->mName == "BfpDirectory_Delete")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_Delete;
				else if (methodDef->mName == "BfpDirectory_GetCurrent")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_GetCurrent;
				else if (methodDef->mName == "BfpDirectory_SetCurrent")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_SetCurrent;
				else if (methodDef->mName == "BfpDirectory_Exists")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_Exists;
				else if (methodDef->mName == "BfpDirectory_GetSysDirectory")
					ceFunction->mFunctionKind = CeFunctionKind_BfpDirectory_GetSysDirectory;

				else if (methodDef->mName == "BfpFile_Close")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Close;
				else if (methodDef->mName == "BfpFile_Create")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Create;
				else if (methodDef->mName == "BfpFile_Flush")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Flush;
				else if (methodDef->mName == "BfpFile_GetFileSize")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetFileSize;
				else if (methodDef->mName == "BfpFile_Read")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Read;
				else if (methodDef->mName == "BfpFile_Release")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Release;
				else if (methodDef->mName == "BfpFile_Seek")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Seek;
				else if (methodDef->mName == "BfpFile_Truncate")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Truncate;
				else if (methodDef->mName == "BfpFile_Write")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Write;
				else if (methodDef->mName == "BfpFile_GetTime_LastWrite")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetTime_LastWrite;
				else if (methodDef->mName == "BfpFile_GetAttributes")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetAttributes;
				else if (methodDef->mName == "BfpFile_SetAttributes")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_SetAttributes;
				else if (methodDef->mName == "BfpFile_Copy")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Copy;
				else if (methodDef->mName == "BfpFile_Rename")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Rename;
				else if (methodDef->mName == "BfpFile_Delete")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Delete;
				else if (methodDef->mName == "BfpFile_Exists")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_Exists;
				else if (methodDef->mName == "BfpFile_GetTempPath")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetTempPath;
				else if (methodDef->mName == "BfpFile_GetTempFileName")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetTempFileName;
				else if (methodDef->mName == "BfpFile_GetFullPath")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetFullPath;
				else if (methodDef->mName == "BfpFile_GetActualPath")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFile_GetActualPath;

				else if (methodDef->mName == "BfpSpawn_Create")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSpawn_Create;
				else if (methodDef->mName == "BfpSpawn_GetStdHandles")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSpawn_GetStdHandles;
				else if (methodDef->mName == "BfpSpawn_Kill")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSpawn_Kill;
				else if (methodDef->mName == "BfpSpawn_Release")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSpawn_Release;
				else if (methodDef->mName == "BfpSpawn_WaitFor")
					ceFunction->mFunctionKind = CeFunctionKind_BfpSpawn_WaitFor;

				else if (methodDef->mName == "BfpFindFileData_FindFirstFile")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_FindFirstFile;
				else if (methodDef->mName == "BfpFindFileData_FindNextFile")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_FindNextFile;
				else if (methodDef->mName == "BfpFindFileData_GetFileName")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetFileName;
				else if (methodDef->mName == "BfpFindFileData_GetTime_LastWrite")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetTime_LastWrite;
				else if (methodDef->mName == "BfpFindFileData_GetTime_Created")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetTime_Created;
				else if (methodDef->mName == "BfpFindFileData_GetTime_Access")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetTime_Access;
				else if (methodDef->mName == "BfpFindFileData_GetFileAttributes")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetFileAttributes;
				else if (methodDef->mName == "BfpFindFileData_GetFileSize")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_GetFileSize;
				else if (methodDef->mName == "BfpFindFileData_Release")
					ceFunction->mFunctionKind = CeFunctionKind_BfpFindFileData_Release;

				else if (methodDef->mName == "BfpSystem_GetTimeStamp")
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
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mFloatTypeDef))
			{
				if (methodDef->mName == "ftoa")
					ceFunction->mFunctionKind = CeFunctionKind_Double_Ftoa;
				if (methodDef->mName == "ToString")
					ceFunction->mFunctionKind = CeFunctionKind_Float_ToString;
			}
			else if (owner->IsInstanceOf(mCeModule->mCompiler->mMathTypeDef))
			{
				if (methodDef->mName == "Abs")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Abs;
				else if (methodDef->mName == "Acos")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Acos;
				else if (methodDef->mName == "Asin")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Asin;
				else if (methodDef->mName == "Atan")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Atan;
				else if (methodDef->mName == "Atan2")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Atan2;
				else if (methodDef->mName == "Ceiling")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Ceiling;
				else if (methodDef->mName == "Cos")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Cos;
				else if (methodDef->mName == "Cosh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Cosh;
				else if (methodDef->mName == "Exp")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Exp;
				else if (methodDef->mName == "Floor")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Floor;
				else if (methodDef->mName == "Log")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Log;
				else if (methodDef->mName == "Log10")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Log10;
				else if (methodDef->mName == "Mod")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Mod;
				else if (methodDef->mName == "Pow")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Pow;
				else if (methodDef->mName == "Round")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Round;
				else if (methodDef->mName == "Sin")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sin;
				else if (methodDef->mName == "Sinh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sinh;
				else if (methodDef->mName == "Sqrt")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Sqrt;
				else if (methodDef->mName == "Tan")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Tan;
				else if (methodDef->mName == "Tanh")
					ceFunction->mFunctionKind = CeFunctionKind_Math_Tanh;
			}

			ceFunction->mInitializeState = CeFunction::InitializeState_Initialized;
			return;
		}
	}
}

void CeMachine::PrepareFunction(CeFunction* ceFunction, CeBuilder* parentBuilder)
{
	AutoTimer autoTimer(mRevisionExecuteTime);
	SetAndRestoreValue<CeFunction*> prevCEFunction(mPreparingFunction, ceFunction);

	BF_ASSERT(ceFunction->mInitializeState <= CeFunction::InitializeState_Initialized);

	if (ceFunction->mFunctionKind == CeFunctionKind_NotSet)
	{
		CheckFunctionKind(ceFunction);
		if (ceFunction->mInitializeState == CeFunction::InitializeState_Initialized)
			return;
	}

	BF_ASSERT(ceFunction->mInitializeState <= CeFunction::InitializeState_Initialized);
	if (ceFunction->mInitializeState == CeFunction::InitializeState_Initializing_ReEntry)
	{
		//Fail("Function generation re-entry");
		return;
	}

	if (ceFunction->mInitializeState == CeFunction::InitializeState_Initializing)
		ceFunction->mInitializeState = CeFunction::InitializeState_Initializing_ReEntry;
	else
		ceFunction->mInitializeState = CeFunction::InitializeState_Initializing;

	CeBuilder ceBuilder;
	SetAndRestoreValue<CeBuilder*> prevBuilder(mCurBuilder, &ceBuilder);
	ceBuilder.mParentBuilder = parentBuilder;
	ceBuilder.mPtrSize = mCeModule->mCompiler->mSystem->mPtrSize;
	ceBuilder.mCeMachine = this;
	ceBuilder.mCeFunction = ceFunction;
	ceBuilder.Build();

	ceFunction->mInitializeState = CeFunction::InitializeState_Initialized;

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
	if ((mCeModule->mSystem->mPtrSize == 8) && (mDebugger == NULL))
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
				if ((ceFunctionInfo->mMethodInstance != NULL) && (ceFunctionInfo->mMethodInstance != methodInstance))
				{
					// This ceFunctionInfo is already taken - probably from a name mangling conflict
					ceFunctionInfo = new CeFunctionInfo();
				}
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
		if (mDebugger != NULL)
			ceFunction->mDbgInfo = new CeDbgFunctionInfo();
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
	if (ceFunction->mInitializeState < CeFunction::InitializeState_Initialized)
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
		if (methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference)
		{
			auto methodDef = typeInstance->mTypeDef->mMethods[methodGroup.mMethodIdx];
			auto flags = ((methodDef->mGenericParams.size() != 0) || (typeInstance->IsUnspecializedType())) ? BfGetMethodInstanceFlag_UnspecializedPass : BfGetMethodInstanceFlag_None;
			flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_MethodInstanceOnly);
			mCeModule->GetMethodInstance(typeInstance, methodDef, BfTypeVector(), flags);
		}

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

BfFieldInstance* CeMachine::GetFieldInstance(int64 fieldHandle)
{
	BfFieldInstance* fieldInstance = (BfFieldInstance*)(intptr)fieldHandle;
	if (!mFieldInstanceSet.Contains(fieldInstance))
		return NULL;
	return fieldInstance;
}

CeFunction* CeMachine::QueueMethod(BfMethodInstance* methodInstance, BfIRValue func)
{
	if (mPreparingFunction != NULL)
	{
		auto curOwner = mPreparingFunction->mMethodInstance->GetOwner();
		curOwner->mModule->AddDependency(methodInstance->GetOwner(), curOwner, BfDependencyMap::DependencyFlag_ConstEval);
	}

	bool added = false;
	return GetFunction(methodInstance, func, added);
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

void CeMachine::ClearTypeData(BfTypeInstance* typeInstance)
{
	if (mTypeInfoMap.Remove(typeInstance))
	{
		for (auto& methodGroup : typeInstance->mMethodInstanceGroups)
		{
			if (methodGroup.mDefault != NULL)
				mMethodInstanceSet.Remove(methodGroup.mDefault);
			if (methodGroup.mMethodSpecializationMap != NULL)
			{
				for (auto& kv : *methodGroup.mMethodSpecializationMap)
					mMethodInstanceSet.Remove(kv.mValue);
			}
		}
	}
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
		memset(ceContext->mMemory.mVals, 0, BF_CE_INITIAL_MEMORY);
	}

	ceContext->mCurEmitContext = mCurEmitContext;
	mCurEmitContext = NULL;
	mExecuteId++;
	ceContext->mStackSize = BF_CE_DEFAULT_STACK_SIZE;
	ceContext->mMemory.ResizeRaw(ceContext->mStackSize);
	ceContext->mExecuteId = mExecuteId;
	ceContext->mCurHandleId = 0;
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
	for (auto kv : ceContext->mInternalDataMap)
		kv.mValue->Release();
	ceContext->mInternalDataMap.Clear();
	ceContext->mWorkingDir.Clear();
}

BfTypedValue CeMachine::Call(CeCallSource callSource, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags, BfType* expectingType)
{
	auto ceContext = AllocContext();
	auto result = ceContext->Call(callSource, module, methodInstance, args, flags, expectingType);
	ReleaseContext(ceContext);
	return result;
}