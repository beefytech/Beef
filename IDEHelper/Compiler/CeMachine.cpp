#include "CeMachine.h"
#include "BfModule.h"
#include "BfCompiler.h"
#include "BfIRBuilder.h"
#include "../Backend/BeIRCodeGen.h"

#define CE_ENABLE_HEAP

USING_NS_BF;

struct CeOpInfo
{
	const char* mName;
	CeOperandInfoKind mResultKind;
	CeOperandInfoKind mOperandA;
	CeOperandInfoKind mOperandB;
	CeOperandInfoKind mOperandC;
};

#define CEOPINFO_SIZED_1(OPNAME, OPINFOA) \
	{OPNAME "_8", OPINFOA}, \
	{OPNAME "_16", OPINFOA}, \
	{OPNAME "_32", OPINFOA}, \
	{OPNAME "_64", OPINFOA}, \
	{OPNAME "_X", OPINFOA, CEOI_IMM32}

#define CEOPINFO_SIZED_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME "_8", OPINFOA, OPINFOB}, \
	{OPNAME "_16", OPINFOA, OPINFOB}, \
	{OPNAME "_32", OPINFOA, OPINFOB}, \
	{OPNAME "_64", OPINFOA, OPINFOB}, \
	{OPNAME "_X", OPINFOA, CEOI_IMM32, OPINFOB}

#define CEOPINFO_SIZED_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME "_8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_64", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME "_X", OPINFOA, CEOI_IMM32, OPINFOB, OPINFOC}

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

static CeOpInfo gOpInfo[] =
{
	{"InvalidOp"},
	{"Ret"},
	{"Jmp", CEOI_None, CEOI_JMPREL},
	{"JmpIf", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"JmpIfNot", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"Error", CEOI_None, CEOI_IMM32},	
	{"DynamicCastCheck", CEOI_FrameRef, CEOI_FrameRef, CEOI_IMM32},
	{"GetString", CEOI_FrameRef, CEOI_IMM32},
	{"Malloc", CEOI_FrameRef, CEOI_FrameRef},
	{"Free", CEOI_None, CEOI_FrameRef},

	{"MemSet", CEOI_None, CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef},
	{"MemSet_Const", CEOI_None, CEOI_FrameRef, CEOI_IMM8, CEOI_IMM32},
	{"MemCpy", CEOI_None, CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddr_32", CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddr_64", CEOI_FrameRef, CEOI_FrameRef},
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
	{"Call", CEOI_None, CEOI_IMM32},
	{"Call_Virt", CEOI_None, CEOI_FrameRef, CEOI_IMM32},
	{"CeOp_Call_IFace", CEOI_None, CEOI_FrameRef, CEOI_IMM32, CEOI_IMM32},

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

CeFunction::~CeFunction()
{
	
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
			sprintf(str, "%lld", val);
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

		if (opInfo.mResultKind != CEOI_None)
		{
			DumpOperandInfo(opInfo.mResultKind);
			mStr += " = ";
		}

		mStr += opInfo.mName;
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
	Emit((CeOp)(val + GetSizeClass(size)));
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
// 					auto relTo = GetOperand(constant->mTarget);
// 
// 					if (relTo.mKind == CeOperandKind_Immediate_Null)
// 					{
// 						mcOperand.mKind = CeOperandKind_Immediate_Null;
// 						mcOperand.mType = constant->mType;
// 						return mcOperand;
// 					}
// 
// 					mcOperand = AllocVirtualReg(constant->mType);
// 					auto vregInfo = GetVRegInfo(mcOperand);
// 					vregInfo->mDefOnFirstUse = true;
// 					vregInfo->mRelTo = relTo;
// 					vregInfo->mIsExpr = true;
// 
// 					return mcOperand;
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
// 			auto structConstant = (BeStructConstant*)value;
// 
// 			CeOperand mcOperand;
// 			mcOperand.mKind = CeOperandKind_ConstAgg;
// 			mcOperand.mConstant = structConstant;
// 
// 			return mcOperand;
		}
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
 				byteOffset = gepConstant->mIdx1 * elementType->mSize;
 			}
 
 			auto elementPtrType = mCeMachine->GetBeContext()->GetPointerTo(elementType);
			result = FrameAlloc(elementPtrType);
			EmitSizedOp(CeOp_AddConst_I8, mPtrSize);
			EmitFrameOffset(result);
			EmitFrameOffset(mcVal);
			Emit(&byteOffset, mPtrSize);
 			
// 			result = AllocRelativeVirtualReg(elementPtrType, result, GetImmediate(byteOffset), 1);
//  			// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use			
//  			auto vregInfo = GetVRegInfo(result);
//  			vregInfo->mDefOnFirstUse = true;
//  			result.mKind = CeOperandKind_VReg;
 
 			return result;
		}
		break;
	case BeExtractValueConstant::TypeId:
		{
			// Note: this only handles zero-aggregates
// 			auto extractConstant = (BeExtractValueConstant*)value;
// 			auto elementType = extractConstant->GetType();
// 
// 			auto mcVal = GetOperand(extractConstant->mTarget);
// 			auto valType = GetType(mcVal);
// 
// 			BeConstant beConstant;
// 			beConstant.mType = elementType;
// 			beConstant.mUInt64 = 0;
// 			return GetOperand(&beConstant);
		}
		break;
	case BeFunction::TypeId:
		{		
// 			auto sym = mCOFFObject->GetSymbol(value);
// 			BEMC_ASSERT(sym != NULL);
// 			if (sym != NULL)
// 			{
// 				CeOperand mcOperand;
// 				mcOperand.mKind = CeOperandKind_SymbolAddr;
// 				mcOperand.mSymbolIdx = sym->mIdx;
// 				return mcOperand;
// 			}
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

	if (retType->mSize > 0)
	{				
		mReturnVal.mKind = CeOperandKind_AllocaAddr;
		mReturnVal.mFrameOfs = frameOffset;
		mReturnVal.mType = retType;

		frameOffset += retType->mSize;
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

void CeBuilder::Build()
{
	mCeFunction->mFailed = true;

	auto methodInstance = mCeFunction->mMethodInstance;
	auto methodDef = methodInstance->mMethodDef;

	BfMethodInstance dupMethodInstance = *methodInstance;
	if (dupMethodInstance.mMethodInfoEx != NULL)
	{
		dupMethodInstance.mMethodInfoEx = new BfMethodInfoEx();
		*dupMethodInstance.mMethodInfoEx = *(methodInstance->mMethodInfoEx);
		for (auto genericParam : dupMethodInstance.mMethodInfoEx->mGenericParams)
			genericParam->AddRef();
		dupMethodInstance.mMethodInfoEx->mMethodCustomAttributes = NULL;
	}
	dupMethodInstance.mHasBeenProcessed = false;
	dupMethodInstance.mIRFunction = BfIRValue();
	//dupMethodInstance.mIRFunction = workItem.mFunc;
	dupMethodInstance.mMethodProcessRequest = NULL;
	dupMethodInstance.mIsReified = true;
	dupMethodInstance.mHotMethod = NULL;
	dupMethodInstance.mInCEMachine = false; // Only have the original one
	
	mCeMachine->mCeModule->mIgnoreWarnings = true;
	mCeMachine->mCeModule->mHadBuildError = false;
	mCeMachine->mCeModule->ProcessMethod(&dupMethodInstance, true);
	
	if (!dupMethodInstance.mIRFunction)
	{
		mCeFunction->mFailed = true;
		return;
	}
	
	auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
	mIntPtrType = irCodeGen->mBeContext->GetPrimitiveType((mPtrSize == 4) ? BeTypeCode_Int32 : BeTypeCode_Int64);
	mBeFunction = (BeFunction*)irCodeGen->GetBeValue(dupMethodInstance.mIRFunction.mId);		

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


	auto beModule = irCodeGen->mBeModule;
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
			case BeConstEvalGetVirtualFunc::TypeId:
			case BeConstEvalGetInterfaceFunc::TypeId:
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
														
							if (castedInst->mToSigned)
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
				{
					auto castedInst = (BeRetInst*)inst;
					if (castedInst->mRetValue != NULL)
					{
						auto mcVal = GetOperand(castedInst->mRetValue);

						BF_ASSERT(mReturnVal.mKind == CeOperandKind_AllocaAddr);
						EmitSizedOp(CeOp_Move_8, mcVal, NULL, true);
						Emit((int32)mReturnVal.mFrameOfs);
					}
					Emit(CeOp_Ret);
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
										Emit((CeOp)(CeOp_AddConst_I32));
										EmitFrameOffset(result);
										EmitFrameOffset(ceVal);
										Emit((int32)(ceIdx1.mImmediate * arrayType->mElementType->mSize));
									}
								}
								else
								{
									auto ptrValue = FrameAlloc(elementPtrType);
									result = ptrValue;

									result = FrameAlloc(elementPtrType);

									if (mPtrSize == 4)
									{
										auto mcElementSize = FrameAlloc(mIntPtrType);
										Emit(CeOp_Const_32);
										EmitFrameOffset(mcElementSize);
										Emit((int32)arrayType->mElementType->mSize);

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
										Emit((int64)arrayType->mElementType->mSize);

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
								byteOffset = ceIdx1.mImmediate * elementType->mSize;
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_Vector)
							{
								auto arrayType = (BeVectorType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = ceIdx1.mImmediate * elementType->mSize;
							}
							else
							{
								Fail("Invalid gep target");
							}
							
							auto elementPtrType = beModule->mContext->GetPointerTo(elementType);

							if (byteOffset != 0)
							{
								result = FrameAlloc(elementPtrType);
								Emit((CeOp)(CeOp_AddConst_I32));
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								Emit((int32)byteOffset);
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
							int byteOffset = ceIdx0.mImmediate * ptrType->mElementType->mSize;
							if (byteOffset != 0)
							{
								result = FrameAlloc(ptrType);
								Emit((CeOp)(CeOp_AddConst_I32));
								EmitFrameOffset(result);
								EmitFrameOffset(ceVal);
								Emit((int32)byteOffset);
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
								Emit((int32)ptrType->mElementType->mSize);

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
								Emit((int64)ptrType->mElementType->mSize);

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
					CeOperand ceFunc;
					BeType* returnType = NULL;
					bool isVarArg = false;

					bool useAltArgs = false;
					//SizedArray<BeValue*, 6> args;

					int callIdx = -1;
					BeFunctionType* beFuncType = NULL;
					CeOperand virtTarget;
					int ifaceTypeId = -1;
					int virtualTableIdx = -1;

					if (auto intrin = BeValueDynCast<BeIntrinsic>(castedInst->mFunc))
					{
						switch (intrin->mKind)
						{
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
						default:
							Emit(CeOp_Error);
							Emit((int32)CeErrorKind_Intrinsic);
							break;
						}
					}
					else if (auto beFunction = BeValueDynCast<BeFunction>(castedInst->mFunc))
					{
						beFuncType = beFunction->GetFuncType();

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

						int* callIdxPtr = NULL;
						if (mFunctionMap.TryAdd(beFunction, NULL, &callIdxPtr))
						{
							CeFunctionInfo* ceFunctionInfo = NULL;
							mCeMachine->mNamedFunctionMap.TryGetValue(beFunction->mName, &ceFunctionInfo);
							if (ceFunctionInfo != NULL)
								ceFunctionInfo->mRefCount++;
							else
							{								
								Fail(StrFormat("Unable to locate method %s", beFunction->mName.c_str()));
							}

							CeCallEntry callEntry;
							callEntry.mFunctionInfo = ceFunctionInfo;
							*callIdxPtr = (int)mCeFunction->mCallTable.size();
							mCeFunction->mCallTable.Add(callEntry);
						}
						
						callIdx = *callIdxPtr;						
					}
					else if (auto beGetVirtualFunc = BeValueDynCast<BeConstEvalGetVirtualFunc>(castedInst->mFunc))
					{
						virtTarget = GetOperand(beGetVirtualFunc->mValue);
						virtualTableIdx = beGetVirtualFunc->mVirtualTableIdx;
						
						auto resultType = beGetVirtualFunc->GetType();
						BF_ASSERT(resultType->IsPointer());
						beFuncType = (BeFunctionType*)((BePointerType*)resultType)->mElementType;
					}
					else if (auto beGetInterfaceFunc = BeValueDynCast<BeConstEvalGetInterfaceFunc>(castedInst->mFunc))
					{
						virtTarget = GetOperand(beGetInterfaceFunc->mValue);
						ifaceTypeId = beGetInterfaceFunc->mIFaceTypeId;
						virtualTableIdx = beGetInterfaceFunc->mVirtualTableIdx;

						auto resultType = beGetInterfaceFunc->GetType();
						BF_ASSERT(resultType->IsPointer());
						beFuncType = (BeFunctionType*)((BePointerType*)resultType)->mElementType;
					}
					else
					{
						Emit(CeOp_Error);
						Emit((int32)CeErrorKind_FunctionPointer);

						auto funcType = castedInst->mFunc->GetType();
						if (funcType->IsPointer())
						{
							auto ptrType = (BePointerType*)funcType;
							if (ptrType->mElementType->mTypeCode == BeTypeCode_Function)
							{
								auto beFuncType = (BeFunctionType*)ptrType->mElementType;
								if (beFuncType->mReturnType->mSize > 0)
									result = FrameAlloc(beFuncType->mReturnType);
							}
						}
					}					

					if ((callIdx != -1) || (virtualTableIdx != -1))
					{
						CeOperand thisOperand;

						for (int argIdx = (int)castedInst->mArgs.size() - 1; argIdx >= 0; argIdx--)
						{
							auto& arg = castedInst->mArgs[argIdx];
							auto ceArg = GetOperand(arg.mValue);
							if (argIdx == 0)
								thisOperand = ceArg;
							EmitSizedOp(CeOp_Push_8, ceArg, NULL, true);
						}

						int stackAdjust = 0;
						
						if (beFuncType->mReturnType->mSize > 0)
						{
							Emit(CeOp_AdjustSPConst);
							Emit((int32)-beFuncType->mReturnType->mSize);
							stackAdjust += beFuncType->mReturnType->mSize;
						}

						if (ifaceTypeId != -1)
						{
							Emit(CeOp_Call_IFace);
							EmitFrameOffset(thisOperand);
							Emit((int32)ifaceTypeId);
							Emit((int32)virtualTableIdx);
						}
						else if (virtualTableIdx != -1)
						{
							Emit(CeOp_Call_Virt);
							EmitFrameOffset(thisOperand);
							Emit((int32)virtualTableIdx);
						}
						else
						{
							Emit(CeOp_Call);							
							Emit((int32)callIdx);
						}

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
			case BeConstEvalGetType::TypeId:
				{
					auto castedInst = (BeConstEvalGetType*)inst;
					result.mKind = CeOperandKind_Immediate;
					result.mImmediate = castedInst->mTypeId;
					result.mType = beModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
				}
				break;
			case BeConstEvalDynamicCastCheck::TypeId:
				{
					auto castedInst = (BeConstEvalDynamicCastCheck*)inst;
					auto mcValue = GetOperand(castedInst->mValue);

					auto ptrType = beModule->mContext->GetPrimitiveType(BeTypeCode_NullPtr);
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

CeMachine::CeMachine(BfCompiler* compiler)
{
	mCompiler = compiler;	
	mCeModule = NULL;		
	mRevision = 0;
	mExecuteId = 0;
	mCurTargetSrc = NULL;
	mCurModule = NULL;
	mHeap = NULL;
}

CeMachine::~CeMachine()
{
	delete mCeModule;
	delete mHeap;

	for (auto kv : mFunctions)
	{
		auto functionInfo = kv.mValue;
		delete functionInfo;
	}
}

BfError* CeMachine::Fail(const CeFrame& curFrame, const StringImpl& str)
{
	auto bfError = mCurModule->Fail("Unable to const-evaluate function", mCurTargetSrc);
	if (bfError == NULL)
		return NULL;

	auto passInstance = mCompiler->mPassInstance;
	
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
			err = str;
		else
		{
			StrFormat("in const evaluation of");
			err += mCeModule->MethodToString(ceFunction->mMethodInstance, BfMethodNameFlag_OmitParams);
			//err += "(";
			//err += ")";
		}
		 
		if (emitEntry != NULL)
			err += StrFormat(" at line% d:%d in %s", emitEntry->mLine + 1, emitEntry->mColumn + 1, ceFunction->mFiles[emitEntry->mFile].c_str());

		auto moreInfo = passInstance->MoreInfo(err);
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

void CeMachine::Init()
{
	mCeModule = new BfModule(mCompiler->mContext, "__constEval");
	mCeModule->mIsSpecialModule = true;
	//mCeModule->mIsScratchModule = true;
	mCeModule->mIsConstModule = true;
	mCeModule->mIsReified = true;
	mCeModule->Init();

	mCeModule->mBfIRBuilder = new BfIRBuilder(mCeModule);
	mCeModule->mBfIRBuilder->mDbgVerifyCodeGen = true;		
	mCeModule->FinishInit();
	mCeModule->mBfIRBuilder->mHasDebugInfo = false; // Only line info
	mCeModule->mBfIRBuilder->mIgnoreWrites = false;
	mCeModule->mWantsIRIgnoreWrites = false;
}

uint8* CeMachine::CeMalloc(int size)
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

bool CeMachine::CeFree(addr_ce addr)
{
#ifdef CE_ENABLE_HEAP
	ContiguousHeap::AllocRef heapRef = addr - BF_CE_STACK_SIZE;
	return mHeap->Free(heapRef);
#else
	return true;
#endif
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
	mRevision++;
	if (mCeModule != NULL)
	{
		delete mCeModule;
		mCeModule = NULL;
	}
}

void CeMachine::DerefMethodInfo(CeFunctionInfo* ceFunctionInfo)
{
	ceFunctionInfo->mRefCount--;
	if (ceFunctionInfo->mRefCount > 0)
		return;
	BF_ASSERT(ceFunctionInfo->mMethodInstance == NULL);	

	auto itr = mNamedFunctionMap.Find(ceFunctionInfo->mName);
	if (itr->mValue == ceFunctionInfo)
		mNamedFunctionMap.Remove(itr);
	delete ceFunctionInfo;
}

void CeMachine::RemoveMethod(BfMethodInstance* methodInstance)
{
	auto itr = mFunctions.Find(methodInstance);
	auto ceFunctionInfo = itr->mValue;
	BF_ASSERT(itr != mFunctions.end());
	if (itr != mFunctions.end())
	{		
		auto ceFunction = ceFunctionInfo->mCeFunction;
		for (auto& callEntry : ceFunction->mCallTable)
		{
			if (callEntry.mFunctionInfo != NULL)
				DerefMethodInfo(callEntry.mFunctionInfo);			
		}
		delete ceFunction;
		ceFunctionInfo->mCeFunction = NULL;
		ceFunctionInfo->mMethodInstance = NULL;

		if (ceFunctionInfo->mRefCount > 1)
		{
			// Generate a methodref
			ceFunctionInfo->mMethodRef = methodInstance;
		}

		DerefMethodInfo(ceFunctionInfo);

		mFunctions.Remove(itr);
	}
}

int CeMachine::GetConstantSize(BfConstant* constant)
{
	switch (constant->mTypeCode)
	{
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Boolean:
	case BfTypeCode_Char8:
		return 1;
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		return 2;	
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		return 4;
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:	
		return 8;
	case BfTypeCode_NullPtr:
		return 4;
	case BfTypeCode_Float:
		return 4;
	case BfTypeCode_Double:
		return 8;
	}

	return -1;
}

#define CE_GETC(T) *((T*)(ptr += sizeof(T)) - 1)

void CeMachine::WriteConstant(uint8* ptr, BfConstant* constant)
{
	switch (constant->mTypeCode)
	{
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Boolean:
	case BfTypeCode_Char8:
		CE_GETC(int8) = constant->mInt8;
		return;
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		CE_GETC(int16) = constant->mInt16;
		return;
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		CE_GETC(int32) = constant->mInt32;
		return;
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:	
		CE_GETC(int64) = constant->mInt64;
		return;
	case BfTypeCode_NullPtr:
		CE_GETC(int32) = 0;
		return;
	case BfTypeCode_Float:
		CE_GETC(float) = (float)constant->mDouble;
		return;
	case BfTypeCode_Double:
		CE_GETC(double) = constant->mDouble;
		return;
	}
}
#define CE_CHECKSTACK() \
	if (stackPtr < memStart) \
	{ \
		_Fail("Stack overflow"); \
		return false; \
	}

#define CE_CHECKALLOC(SIZE) \
	if ((uintptr)memSize + (uintptr)SIZE > BF_CE_MAX_MEMORY) \
	{ \
		_Fail("Maximum memory size exceeded"); \
	}

// This check will fail for addresses < 64K (null pointer), or out-of-bounds
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

bool CeMachine::Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr)
{
	mExecuteId++;	

	CeFunction* ceFunction = startFunction;
	uint8* memStart = &mMemory[0];	
	int memSize = mMemory.mSize;
	uint8* instPtr = (ceFunction->mCode.IsEmpty()) ? NULL : &ceFunction->mCode[0];
	uint8* stackPtr = startStackPtr;
	uint8* framePtr = startFramePtr;
		
	auto _GetCurFrame = [&]()
	{
		CeFrame ceFrame;
		ceFrame.mFunction = ceFunction;
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
		if (checkFunction->mFunctionKind == CeFunctionKind_OOB)
		{
			Fail(_GetCurFrame(), "Array out of bounds");
			return false;
		}
		else if (checkFunction->mFunctionKind == CeFunctionKind_DebugWrite)
		{
			int32 ptrVal = *(int32*)((uint8*)stackPtr + 0);
			auto size = *(int32*)(stackPtr + mCeModule->mSystem->mPtrSize);
			CE_CHECKADDR(ptrVal, size);
			char* strPtr = (char*)(ptrVal + memStart);
			String str;
			str.Insert(0, strPtr, size);
			OutputDebugStr(str);
			handled = true;
			return true;
		}
		else if (checkFunction->mFunctionKind == CeFunctionKind_DebugWrite_Int)
		{
			int32 intVal = *(int32*)((uint8*)stackPtr + 0);
			OutputDebugStrF("Debug Val: %d\n", intVal);
			handled = true;
			return true;
		}
		else if (checkFunction->mFunctionKind == CeFunctionKind_FatalError)
		{
			int32 strInstAddr = *(int32*)((uint8*)stackPtr + 0);
			CE_CHECKADDR(strInstAddr, 0);
			
			BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeModule->ResolveTypeDef(mCompiler->mStringTypeDef, BfPopulateType_Data);

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
		else if (checkFunction->mFunctionKind != CeFunctionKind_Normal)
		{
			Fail(_GetCurFrame(), StrFormat("Unable to invoke extern method '%s'", mCeModule->MethodToString(checkFunction->mMethodInstance).c_str()));
			return false;
		}

		if (!checkFunction->mFailed)
			return true;
		auto error = Fail(_GetCurFrame(), "Method call failed");
		if ((error != NULL) && (!checkFunction->mGenError.IsEmpty()))
			mCompiler->mPassInstance->MoreInfo("Const Method Generation Error: " + checkFunction->mGenError);
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

	volatile bool* cancelPtr = &mCompiler->mCanceling;

	int callCount = 0;

	while (true)
	{
		if (*cancelPtr)
		{
			_Fail("Cancelled");
			return false;
		}

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

				mCallStack.pop_back();				
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

				CE_CHECKADDR(valueAddr, sizeof(int32));

				auto ifaceType = mCeModule->mContext->mTypes[ifaceId];
				int32 objTypeId = *(int32*)(memStart + valueAddr);
				auto valueType = mCeModule->mContext->mTypes[objTypeId];
				if (mCeModule->TypeIsSubTypeOf(valueType->ToTypeInstance(), ifaceType->ToTypeInstance(), false))
					result = valueAddr;
				else
					result = 0;
			}
			break;
		case CeOp_GetString:
			{
				auto frameOfs = CE_GETINST(int32);
				auto stringTableIdx = CE_GETINST(int32);
				auto& ceStringEntry = ceFunction->mStringTable[stringTableIdx];
				if (ceStringEntry.mBindExecuteId != mExecuteId)
				{
					addr_ce* ceAddrPtr = NULL;
					if (mStringMap.TryAdd(ceStringEntry.mStringId, NULL, &ceAddrPtr))
					{
						String str;
						BfStringPoolEntry* entry = NULL;
						if (mCeModule->mContext->mStringObjectIdMap.TryGetValue(ceStringEntry.mStringId, &entry))
						{
							str = entry->mString;
						}

						BfTypeInstance* stringTypeInst = (BfTypeInstance*)mCeModule->ResolveTypeDef(mCompiler->mStringTypeDef, BfPopulateType_Data);

						int allocSize = stringTypeInst->mInstSize + (int)str.length() + 1;
						int charsOffset = stringTypeInst->mInstSize;

						CE_CHECKALLOC(allocSize);
						uint8* mem = CeMalloc(allocSize);
						_FixVariables();

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
						*(int32*)(mem + ptrOffset) = (mem + charsOffset) - memStart;
						memcpy(mem + charsOffset, str.c_str(), str.length());

						*ceAddrPtr = mem - memStart;
					}

					ceStringEntry.mStringAddr = *ceAddrPtr;
					ceStringEntry.mBindExecuteId = mExecuteId;
				}
				
				*(addr_ce*)(framePtr + frameOfs) = ceStringEntry.mStringAddr;
			}
			break;
		case CeOp_Malloc:
			{			
				auto frameOfs = CE_GETINST(int32);
				int32 size = CE_GETFRAME(int32);
				CE_CHECKALLOC(size);
				uint8* mem = CeMalloc(size);
				_FixVariables();
				*(addr_ce*)(framePtr + frameOfs) = mem - memStart;
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
				memset(memStart + destAddr, setValue, setSize);
			}
			break;
		case CeOp_MemSet_Const:
			{
				auto destAddr = CE_GETFRAME(addr_ce);
				uint8 setValue = CE_GETINST(uint8);
				int32 setSize = CE_GETINST(int32);
				memset(memStart + destAddr, setValue, setSize);
			}
			break;
		case CeOp_MemCpy:
			{
				auto destAddr = CE_GETFRAME(addr_ce);
				auto srcAddr = CE_GETFRAME(addr_ce);				
				int32 size = CE_GETFRAME(int32);
				memcpy(memStart + destAddr, memStart + srcAddr, size);
			}
			break;
		case CeOp_FrameAddr_64:
			{
				auto& result = CE_GETFRAME(int64);
				auto addr = &CE_GETFRAME(uint8);
				result = addr - memStart;
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
				auto resultPtr = &CE_GETFRAME(uint8);
				int32 constSize = CE_GETINST(int32);
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
		case CeOp_Call:
			{
				callCount++;

				int32 callIdx = CE_GETINST(int32);
				auto& callEntry = ceFunction->mCallTable[callIdx];
				if (callEntry.mBindRevision != mRevision)
				{
					callEntry.mFunction = NULL;
					//mNamedFunctionMap.TryGetValue(callEntry.mFunctionName, &callEntry.mFunction);

					if (callEntry.mFunctionInfo == NULL)
					{
						_Fail("Unable to locate function entry");
						break;
					}
					
					if ((callEntry.mFunctionInfo->mCeFunction == NULL) && (!callEntry.mFunctionInfo->mMethodRef.IsNull()))
					{
						auto methodRef = callEntry.mFunctionInfo->mMethodRef;
						auto methodDef = methodRef.mTypeInstance->mTypeDef->mMethods[methodRef.mMethodNum];
						auto moduleMethodInstance = mCeModule->GetMethodInstance(methodRef.mTypeInstance, methodDef, 
							methodRef.mMethodGenericArguments);

						if (moduleMethodInstance)												
						{							
							QueueMethod(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc);							
						}						
					}

					if (callEntry.mFunctionInfo->mCeFunction == NULL)
					{
						_Fail("Method not generated");
						break;
					}

					callEntry.mFunction = callEntry.mFunctionInfo->mCeFunction;
					if (!callEntry.mFunction->mInitialized)
					{
						PrepareFunction(callEntry.mFunction);
					}

					bool handled = false;
					if (!_CheckFunction(callEntry.mFunction, handled))
						return false;
					if (handled)
						break;

					callEntry.mBindRevision = mRevision;
				}

				CE_CALL(callEntry.mFunction);
				

// 				if (callEntry.mFunction->mName.Contains("__static_dump"))
// 				{
// 					int32 val = *(int32*)(stackPtr);
// 					OutputDebugStrF("__static_dump: %d\n", val);
// 				}				
			}
			break;
		case CeOp_Call_Virt:
			{
				auto valueAddr = CE_GETFRAME(addr_ce);
				int32 virtualIdx = CE_GETINST(int32);

				CE_CHECKADDR(valueAddr, sizeof(int32));
				int32 objTypeId = *(int32*)(memStart + valueAddr);
				auto valueType = mCeModule->mContext->mTypes[objTypeId]->ToTypeInstance();
				if (valueType->mVirtualMethodTable.IsEmpty())
					mCeModule->PopulateType(valueType, BfPopulateType_DataAndMethods);
				auto methodInstance = (BfMethodInstance*)valueType->mVirtualMethodTable[virtualIdx].mImplementingMethod;
								
				auto callFunction = GetPreparedFunction(methodInstance);				
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
			CEOP_BIN_DIV(/, int8);
			break;
		case CeOp_Div_I16:
			CEOP_BIN_DIV(/, int16);
			break;
		case CeOp_Div_I32:
			CEOP_BIN_DIV(/, int32);
			break;
		case CeOp_Div_I64:
			CEOP_BIN_DIV(/, int64);
			break;
		case CeOp_Div_F32:
			CEOP_BIN_DIV(/, float);
			break;
		case CeOp_Div_F64:
			CEOP_BIN_DIV(/, double);
			break;
		case CeOp_Div_U8:
			CEOP_BIN_DIV(/, uint8);
			break;
		case CeOp_Div_U16:
			CEOP_BIN_DIV(/, uint16);
			break;
		case CeOp_Div_U32:
			CEOP_BIN_DIV(/, uint32);
			break;
		case CeOp_Div_U64:
			CEOP_BIN_DIV(/, uint64);
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
			CEOP_BIN(|, uint8);
			break;
		case CeOp_Or_I16:
			CEOP_BIN(|, uint16);
			break;
		case CeOp_Or_I32:
			CEOP_BIN(|, uint32);
			break;
		case CeOp_Or_I64:
			CEOP_BIN(|, uint64);
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
			CEOP_BIN2(<<, int8, uint8);
			break;
		case CeOp_Shl_I16:
			CEOP_BIN2(<<, int16, uint8);
			break;
		case CeOp_Shl_I32:
			CEOP_BIN2(<<, int32, uint8);
			break;
		case CeOp_Shl_I64:
			CEOP_BIN2(<<, int64, uint8);
			break;
		case CeOp_Shr_I8:
			CEOP_BIN2(>>, int8, uint8);
			break;
		case CeOp_Shr_I16:
			CEOP_BIN2(>>, int16, uint8);
			break;
		case CeOp_Shr_I32:
			CEOP_BIN2(>>, int32, uint8);
			break;
		case CeOp_Shr_I64:
			CEOP_BIN2(>>, int64, uint8);
			break;
		case CeOp_Shr_U8:
			CEOP_BIN2(>>, uint8, uint8);
			break;
		case CeOp_Shr_U16:
			CEOP_BIN2(>>, uint16, uint8);
			break;
		case CeOp_Shr_U32:
			CEOP_BIN2(>>, uint32, uint8);
			break;
		case CeOp_Shr_U64:
			CEOP_BIN2(>>, uint64, uint8);
			break;
		case CeOp_Cmp_NE_I8:
			CEOP_CMP(!= , int8);
			break;
		case CeOp_Cmp_NE_I16:
			CEOP_CMP(!= , int16);
			break;
		case CeOp_Cmp_NE_I32:
			CEOP_CMP(!=, int32);
			break;
		case CeOp_Cmp_NE_I64:
			CEOP_CMP(!=, int64);
			break;
		case CeOp_Cmp_NE_F32:
			CEOP_CMP(!= , float);
			break;
		case CeOp_Cmp_NE_F64:
			CEOP_CMP(!= , double);
			break;
		case CeOp_Cmp_EQ_I8:
			CEOP_CMP(==, int8);
			break;
		case CeOp_Cmp_EQ_I16:
			CEOP_CMP(==, int16);
			break;
		case CeOp_Cmp_EQ_I32:
			CEOP_CMP(==, int32);
			break;
		case CeOp_Cmp_EQ_I64:
			CEOP_CMP(==, int64);
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
			CEOP_CMP(<, int32);
			break;
		case CeOp_Cmp_SLT_I64:
			CEOP_CMP(<, int64);
			break;
		case CeOp_Cmp_SLT_F32:
			CEOP_CMP(<, float);
			break;
		case CeOp_Cmp_SLT_F64:
			CEOP_CMP(< , double);
			break;
		case CeOp_Cmp_ULT_I8:
			CEOP_CMP(<, uint8);
			break;
		case CeOp_Cmp_ULT_I16:
			CEOP_CMP(<, uint16);
			break;
		case CeOp_Cmp_ULT_I32:
			CEOP_CMP(<, uint32);
			break;
		case CeOp_Cmp_ULT_I64:
			CEOP_CMP(<, uint64);
			break;
		case CeOp_Cmp_SLE_I8:
			CEOP_CMP(<=, int8);
			break;
		case CeOp_Cmp_SLE_I16:
			CEOP_CMP(<=, int16);
			break;
		case CeOp_Cmp_SLE_I32:
			CEOP_CMP(<=, int32);
			break;
		case CeOp_Cmp_SLE_I64:
			CEOP_CMP(<=, int64);
			break;
		case CeOp_Cmp_SLE_F32:
			CEOP_CMP(<= , float);
			break;
		case CeOp_Cmp_SLE_F64:
			CEOP_CMP(<= , double);
			break;
		case CeOp_Cmp_ULE_I8:
			CEOP_CMP(<=, uint8);
			break;
		case CeOp_Cmp_ULE_I16:
			CEOP_CMP(<=, uint16);
			break;
		case CeOp_Cmp_ULE_I32:
			CEOP_CMP(<=, uint32);
			break;
		case CeOp_Cmp_ULE_I64:
			CEOP_CMP(<=, uint64);
			break;
		case CeOp_Cmp_SGT_I8:
			CEOP_CMP(>, int8);
			break;
		case CeOp_Cmp_SGT_I16:
			CEOP_CMP(>, int16);
			break;
		case CeOp_Cmp_SGT_I32:
			CEOP_CMP(>, int32);
			break;
		case CeOp_Cmp_SGT_I64:
			CEOP_CMP(>, int64);
			break;
		case CeOp_Cmp_SGT_F32:
			CEOP_CMP(>, float);
			break;
		case CeOp_Cmp_SGT_F64:
			CEOP_CMP(>, double);
			break;
		case CeOp_Cmp_UGT_I8:
			CEOP_CMP(>, uint8);
			break;
		case CeOp_Cmp_UGT_I16:
			CEOP_CMP(>, uint16);
			break;
		case CeOp_Cmp_UGT_I32:
			CEOP_CMP(>, uint32);
			break;
		case CeOp_Cmp_UGT_I64:
			CEOP_CMP(>, uint64);
			break;
		case CeOp_Cmp_SGE_I8:
			CEOP_CMP(>=, int8);
			break;
		case CeOp_Cmp_SGE_I16:
			CEOP_CMP(>=, int16);
			break;
		case CeOp_Cmp_SGE_I32:
			CEOP_CMP(>=, int32);
			break;
		case CeOp_Cmp_SGE_I64:
			CEOP_CMP(>=, int64);
			break;
		case CeOp_Cmp_SGE_F32:
			CEOP_CMP(>=, float);
			break;
		case CeOp_Cmp_SGE_F64:
			CEOP_CMP(>=, double);
			break;
		case CeOp_Cmp_UGE_I8:
			CEOP_CMP(>=, uint8);
			break;
		case CeOp_Cmp_UGE_I16:
			CEOP_CMP(>=, uint16);
			break;
		case CeOp_Cmp_UGE_I32:
			CEOP_CMP(>=, uint32);
			break;
		case CeOp_Cmp_UGE_I64:
			CEOP_CMP(>=, uint64);
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

void CeMachine::PrepareFunction(CeFunction* ceFunction)
{	
	auto methodDef = ceFunction->mMethodInstance->mMethodDef;
	if (methodDef->mIsExtern)
	{
		ceFunction->mFunctionKind = CeFunctionKind_Extern;

		auto owner = ceFunction->mMethodInstance->GetOwner();
		if (owner->IsInstanceOf(mCeModule->mCompiler->mDiagnosticsDebugTypeDef))
		{
			if (methodDef->mName == "Write")
			{
				if (ceFunction->mMethodInstance->GetParamCount() == 1)
					ceFunction->mFunctionKind = CeFunctionKind_DebugWrite_Int;
				else
					ceFunction->mFunctionKind = CeFunctionKind_DebugWrite;
			}

			//MAKE CeFunctionKind_DebugWrite_Int
		}
		else if (owner->IsInstanceOf(mCeModule->mCompiler->mInternalTypeDef))
		{
			if (methodDef->mName == "ThrowIndexOutOfRange")
			{
				ceFunction->mFunctionKind = CeFunctionKind_OOB;
			}
			else if (methodDef->mName == "FatalError")
			{
				ceFunction->mFunctionKind = CeFunctionKind_FatalError;
			}
		}

		return;
	}

	BF_ASSERT(!ceFunction->mInitialized);
	ceFunction->mInitialized = true;

	CeBuilder ceBuilder;
	ceBuilder.mPtrSize = mCeModule->mCompiler->mSystem->mPtrSize;
	ceBuilder.mCeMachine = this;
	ceBuilder.mCeFunction = ceFunction;
	ceBuilder.Build();

	if (!ceFunction->mCode.IsEmpty())
	{
		CeDumpContext dumpCtx;
		dumpCtx.mCeFunction = ceFunction;
		dumpCtx.mStart = &ceFunction->mCode[0];
		dumpCtx.mPtr = dumpCtx.mStart;
		dumpCtx.mEnd = dumpCtx.mPtr + ceFunction->mCode.mSize;
		dumpCtx.Dump();

		OutputDebugStrF("Code for %s:\n%s\n", ceBuilder.mBeFunction->mName.c_str(), dumpCtx.mStr.c_str());
	}
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
		
		BF_ASSERT(!methodInstance->mInCEMachine);
		methodInstance->mInCEMachine = true;			

		ceFunction = new CeFunction();
		ceFunction->mCeFunctionInfo = ceFunctionInfo;
		ceFunction->mMethodInstance = methodInstance;

		ceFunctionInfo->mMethodInstance = methodInstance;
		ceFunctionInfo->mCeFunction = ceFunction;
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
		PrepareFunction(ceFunction);
	return ceFunction;
}

void CeMachine::QueueMethod(BfMethodInstance* methodInstance, BfIRValue func)
{
	bool added = false;
	auto ceFunction = GetFunction(methodInstance, func, added);
}

void CeMachine::QueueMethod(BfModuleMethodInstance moduleMethodInstance)
{
	QueueMethod(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc);
}

BfTypedValue CeMachine::Call(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* methodInstance, const BfSizedArray<BfIRValue>& args, CeEvalFlags flags)
{	
// 	for (int argIdx = 0; argIdx < (int)args.size(); argIdx++)
// 	{
// 		auto arg = args[argIdx];
// 		if (!arg.IsConst())
// 			return BfTypedValue();
// 	}

	// DISABLED
	return BfTypedValue();

	SetAndRestoreValue<BfAstNode*> prevTargetSrc(mCurTargetSrc, targetSrc);
	SetAndRestoreValue<BfModule*> prevModule(mCurModule, module);

	BF_ASSERT(mCallStack.IsEmpty());
	
	auto methodDef = methodInstance->mMethodDef;
	if (!methodDef->mIsStatic)
	{
		if (!methodInstance->GetOwner()->IsValueType())
			return BfTypedValue();
	}

	if (mCeModule == NULL)
		Init();

	bool added = false;
	CeFunction* ceFunction = GetFunction(methodInstance, BfIRValue(), added);
	if (!ceFunction->mInitialized)	
		PrepareFunction(ceFunction);
	
	if (mHeap == NULL)
		mHeap = new	ContiguousHeap();
	mMemory.Resize(BF_CE_STACK_SIZE);

	auto stackPtr = &mMemory[0] + mMemory.mSize;	
	auto* memStart = &mMemory[0];

	for (int argIdx = (int)args.size() - 1; argIdx >= 0; argIdx--)
 	{
 		auto arg = args[argIdx];
 		if (!arg.IsConst())
 			return BfTypedValue();
				
		auto constant = module->mBfIRBuilder->GetConstant(arg);
		int constSize = GetConstantSize(constant);
		if (constSize == -1)
			return BfTypedValue();

		stackPtr -= constSize;
		WriteConstant(stackPtr, constant);
 	}

	addr_ce retAddr = 0;
	auto returnType = methodInstance->mReturnType;
	if (!returnType->IsValuelessType())
	{
		int retSize = methodInstance->mReturnType->mSize;
		stackPtr -= retSize;
		retAddr = stackPtr - memStart;
	}

	bool success = Execute(ceFunction, stackPtr - ceFunction->mFrameSize, stackPtr);
	memStart = &mMemory[0];
		
	auto constHolder = module->mBfIRBuilder;

	BfTypedValue returnValue;

	if (success)
	{
		BfTypedValue retValue;
		if (retAddr != 0)
		{
			auto* retPtr = memStart + retAddr;
			BfIRValue constVal;

			if (returnType->IsPrimitiveType())
			{				
				auto primType = (BfPrimitiveType*)returnType;

				auto typeCode = primType->mTypeDef->mTypeCode;
				if (typeCode == BfTypeCode_IntPtr)
					typeCode = (module->mCompiler->mSystem->mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
				else if (typeCode == BfTypeCode_UIntPtr)
					typeCode = (module->mCompiler->mSystem->mPtrSize == 4) ? BfTypeCode_UInt32 : BfTypeCode_UInt64;

				switch (typeCode)
				{
				case BfTypeCode_Int8:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(int8*)retPtr);
					break;
				case BfTypeCode_UInt8:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(uint8*)retPtr);
					break;
				case BfTypeCode_Int16:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(int16*)retPtr);
					break;
				case BfTypeCode_UInt16:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(uint16*)retPtr);
					break;
				case BfTypeCode_Int32:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(int32*)retPtr);
					break;
				case BfTypeCode_UInt32:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, (uint64)*(uint32*)retPtr);
					break;
				case BfTypeCode_Int64:
				case BfTypeCode_UInt64:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(uint64*)retPtr);
					break;
				case BfTypeCode_Float:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(float*)retPtr);
					break;
				case BfTypeCode_Double:
					constVal = constHolder->CreateConst(primType->mTypeDef->mTypeCode, *(double*)retPtr);
					break;				
				}
			}

			if (constVal)
				returnValue = BfTypedValue(constVal, returnType);
		}
		else
		{
			returnValue = BfTypedValue(module->mBfIRBuilder->GetFakeVal(), returnType);
		}
	}
	
	mStringMap.Clear();
	mMemory.Clear();
	mCallStack.Clear();
	mHeap->Clear();

	return returnValue;
}
