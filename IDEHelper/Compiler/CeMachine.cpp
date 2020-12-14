#include "CeMachine.h"
#include "BfModule.h"
#include "BfCompiler.h"
#include "BfIRBuilder.h"
#include "..\Backend\BeIRCodeGen.h"

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
	{OPNAME##"_8", OPINFOA}, \
	{OPNAME##"_16", OPINFOA}, \
	{OPNAME##"_32", OPINFOA}, \
	{OPNAME##"_64", OPINFOA}, \
	{OPNAME##"_X", OPINFOA, CEOI_IMM32}

#define CEOPINFO_SIZED_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME##"_8", OPINFOA, OPINFOB}, \
	{OPNAME##"_16", OPINFOA, OPINFOB}, \
	{OPNAME##"_32", OPINFOA, OPINFOB}, \
	{OPNAME##"_64", OPINFOA, OPINFOB}, \
	{OPNAME##"_X", OPINFOA, CEOI_IMM32, OPINFOB}

#define CEOPINFO_SIZED_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME##"_8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_64", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_X", OPINFOA, CEOI_IMM32, OPINFOB, OPINFOC}

#define CEOPINFO_SIZED_NUMERIC(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME##"_I8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I64", OPINFOA, OPINFOB, OPINFOC}

#define CEOPINFO_SIZED_NUMERIC_PLUSF_2(OPNAME, OPINFOA, OPINFOB) \
	{OPNAME##"_I8", OPINFOA, OPINFOB}, \
	{OPNAME##"_I16", OPINFOA, OPINFOB}, \
	{OPNAME##"_I32", OPINFOA, OPINFOB}, \
	{OPNAME##"_I64", OPINFOA, OPINFOB}, \
	{OPNAME##"_F32", OPINFOA, OPINFOB}, \
	{OPNAME##"_F64", OPINFOA, OPINFOB}
#define CEOPINFO_SIZED_NUMERIC_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME##"_I8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I64", OPINFOA, OPINFOB, OPINFOC}
#define CEOPINFO_SIZED_NUMERIC_PLUSF_3(OPNAME, OPINFOA, OPINFOB, OPINFOC) \
	{OPNAME##"_I8", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I16", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_I64", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_F32", OPINFOA, OPINFOB, OPINFOC}, \
	{OPNAME##"_F64", OPINFOA, OPINFOB, OPINFOC}

static CeOpInfo gOpInfo[] =
{
	{"InvalidOp"},
	{"Ret"},
	{"Jmp", CEOI_None, CEOI_JMPREL},
	{"JmpIf", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"JmpIfNot", CEOI_None, CEOI_JMPREL, CEOI_FrameRef},
	{"FrameAddr32", CEOI_FrameRef, CEOI_FrameRef},
	{"FrameAddr64", CEOI_FrameRef, CEOI_FrameRef},
	{"Zero",  CEOI_FrameRef, CEOI_IMM32},
		
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
	{"Call", CEOI_None, CEOI_IMM32},

	{"Conv_I32_I64", CEOI_FrameRef, CEOI_FrameRef},

	{"AddConst_I8", CEOI_FrameRef, CEOI_IMM8},
	{"AddConst_I16", CEOI_FrameRef, CEOI_IMM16},
	{"AddConst_I32", CEOI_FrameRef, CEOI_IMM32},
	{"AddConst_I64", CEOI_FrameRef, CEOI_IMM64},
	{"AddConst_F32", CEOI_FrameRef, CEOI_IMMF32},
	{"AddConst_F64", CEOI_FrameRef, CEOI_IMMF64},

	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Add", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Sub", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Mul", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("SDiv", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("UDiv", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("SMod", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("UMod", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),

	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_EQ", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SLT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_ULT", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_PLUSF_3("Cmp_SLE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	CEOPINFO_SIZED_NUMERIC_3("Cmp_ULE", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef),
	//{"Cmp_SLT_I32", CEOI_FrameRef, CEOI_FrameRef, CEOI_FrameRef},

	CEOPINFO_SIZED_NUMERIC_PLUSF_2("Neg", CEOI_FrameRef, CEOI_FrameRef),
};

static_assert(BF_ARRAY_COUNT(gOpInfo) == (int)CeOp_COUNT, "gOpName incorrect size");


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
		
		while ((curEmitIdx < mCeFunction->mEmitTable.mSize) && (ofs > mCeFunction->mEmitTable[curEmitIdx].mCodePos))
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
		BeDumpContext dumpCtx;
		errStr += "\n DbgLoc : ";
		dumpCtx.ToString(errStr, mCurDbgLoc);
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

void CeBuilder::Emit(int32 val)
{
	*(int32*)mCeFunction->mCode.GrowUninitialized(4) = val;
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
	if (lhs.mType->IsIntegral())
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
	if (val.mType->IsIntegral())
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

CeOperand CeBuilder::GetOperand(BeValue* value, bool allowAlloca, bool allowImmediate)
{
	if (value == NULL)
		return CeOperand();

	switch (value->GetTypeId())
	{
	case BeGlobalVariable::TypeId:
		{
// 			auto globalVar = (BeGlobalVariable*)value;
// 			if ((globalVar->mIsTLS) && (mTLSVRegIdx == -1))
// 			{
// 				auto tlsVReg = AllocVirtualReg(mNativeIntType);
// 				auto vregInfo = GetVRegInfo(tlsVReg);
// 				vregInfo->mMustExist = true;
// 				vregInfo->mForceReg = true;
// 				mTLSVRegIdx = tlsVReg.mVRegIdx;
// 			}
// 
// 			auto sym = mCOFFObject->GetSymbol(globalVar);
// 			if (sym != NULL)
// 			{
// 				CeOperand mcOperand;
// 				mcOperand.mKind = CeOperandKind_SymbolAddr;
// 				mcOperand.mSymbolIdx = sym->mIdx;
// 				return mcOperand;
// 			}
		}
		break;
	case BeCastConstant::TypeId:
		{
// 			auto constant = (BeCastConstant*)value;
// 
// 			CeOperand mcOperand;
// 			auto relTo = GetOperand(constant->mTarget);
// 			if (relTo.mKind == CeOperandKind_Immediate_Null)
// 			{
// 				mcOperand.mKind = CeOperandKind_Immediate_Null;
// 				mcOperand.mType = constant->mType;
// 				return mcOperand;
// 			}
// 
// 			mcOperand = AllocVirtualReg(constant->mType);
// 			auto vregInfo = GetVRegInfo(mcOperand);
// 			vregInfo->mDefOnFirstUse = true;
// 			vregInfo->mRelTo = relTo;
// 			vregInfo->mIsExpr = true;
// 
// 			return mcOperand;
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
// 			auto gepConstant = (BeGEPConstant*)value;
// 
// 			auto mcVal = GetOperand(gepConstant->mTarget);
// 
// 			BePointerType* ptrType = (BePointerType*)GetType(mcVal);
// 			BEMC_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
// 
// 			auto result = mcVal;
// 
// 			// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.				
// 			int byteOffset = 0;
// 			BeType* elementType = NULL;
// 			byteOffset += gepConstant->mIdx0 * ptrType->mElementType->mSize;
// 
// 			if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
// 			{
// 				BeStructType* structType = (BeStructType*)ptrType->mElementType;
// 				auto& structMember = structType->mMembers[gepConstant->mIdx1];
// 				elementType = structMember.mType;
// 				byteOffset = structMember.mByteOffset;
// 			}
// 			else
// 			{
// 				BEMC_ASSERT(ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray);
// 				auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
// 				elementType = arrayType->mElementType;
// 				byteOffset = gepConstant->mIdx1 * elementType->mSize;
// 			}
// 
// 			auto elementPtrType = mModule->mContext->GetPointerTo(elementType);
// 			result = AllocRelativeVirtualReg(elementPtrType, result, GetImmediate(byteOffset), 1);
// 			// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use			
// 			auto vregInfo = GetVRegInfo(result);
// 			vregInfo->mDefOnFirstUse = true;
// 			result.mKind = CeOperandKind_VReg;
// 
// 			return result;
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
	
	//if (!allowFail)
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
		return FrameAlloc(mIntPtrType);
	}

	auto operand = *operandPtr;

	if ((operand.mKind == CeOperandKind_AllocaAddr) && (!allowAlloca))
	{
		auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
		auto result = FrameAlloc(mIntPtrType);
		Emit((mPtrSize == 4) ? CeOp_FrameAddr32 : CeOp_FrameAddr64);
		EmitFrameOffset(result);
		Emit((int32)operand.mFrameOfs);
		return result;
	}

// 	if ((operand.mKind == CeOperandKind_Phi) && (!allowMetaResult))
// 	{
// 		auto phi = operand.mPhi;
// 
// 		int phiInstIdx = 0;
// 
// 		auto mcBlock = phi->mBlock;
// 		for (auto instIdx = 0; instIdx < mcBlock->mInstructions.size(); instIdx++)
// 		{
// 			auto inst = mcBlock->mInstructions[instIdx];
// 			if (inst->mKind == BeMCInstKind_DefPhi)
// 			{
// 				BEMC_ASSERT(inst->mArg0.mPhi == phi);
// 				phiInstIdx = instIdx;
// 				RemoveInst(mcBlock, phiInstIdx);
// 				break;
// 			}
// 		}
// 
// 		SetAndRestoreValue<BeMCBlock*> prevBlock(mActiveBlock, mcBlock);
// 		SetAndRestoreValue<int*> prevInstIdxRef(mInsertInstIdxRef, &phiInstIdx);
// 
// 		auto resultType = value->GetType();
// 		auto result = AllocVirtualReg(resultType);
// 		auto vregInfo = GetVRegInfo(result);
// 		vregInfo->mHasDynLife = true; // No specific 'def' location
// 		mValueToOperand[value] = result;
// 
// 		if (resultType->mTypeCode == BeTypeCode_Boolean)
// 		{
// 			CreateDefineVReg(result);
// 
// 			CeOperand falseLabel = CeOperand::FromLabel(mCurLabelIdx++);
// 			CeOperand trueLabel = CeOperand::FromLabel(mCurLabelIdx++);
// 			CeOperand endLabel = CeOperand::FromLabel(mCurLabelIdx++);
// 			CreateCondBr(mActiveBlock, operand, trueLabel, falseLabel);
// 
// 			AllocInst(BeMCInstKind_Label, falseLabel);
// 			AllocInst(BeMCInstKind_Mov, result, CeOperand::FromImmediate(0));
// 			AllocInst(BeMCInstKind_Br, endLabel);
// 			AllocInst(BeMCInstKind_Label, trueLabel);
// 			AllocInst(BeMCInstKind_Mov, result, CeOperand::FromImmediate(1));
// 			AllocInst(BeMCInstKind_Label, endLabel);
// 		}
// 		else
// 		{
// 			// Attempt to find common ancestor to insert a 'def' at
// 			SizedArray<BeMCBlock*, 16> blockSearch;
// 			blockSearch.reserve(phi->mValues.size());
// 			BeMCBlock* lowestBlock = NULL;
// 			for (auto& phiValue : phi->mValues)
// 			{
// 				if ((lowestBlock == NULL) || (phiValue.mBlockFrom->mBlockIdx < lowestBlock->mBlockIdx))
// 					lowestBlock = phiValue.mBlockFrom;
// 				blockSearch.push_back(phiValue.mBlockFrom);
// 			}
// 			while (true)
// 			{
// 				bool allMatched = true;
// 				bool didWork = false;
// 				for (int searchIdx = 0; searchIdx < (int)blockSearch.size(); searchIdx++)
// 				{
// 					auto& blockRef = blockSearch[searchIdx];
// 					if (blockRef != lowestBlock)
// 					{
// 						allMatched = false;
// 
// 						for (auto& pred : blockRef->mPreds)
// 						{
// 							// Try find a block closer to start, but not below the current lowestBlock
// 							if ((pred->mBlockIdx >= lowestBlock->mBlockIdx) && (pred->mBlockIdx < blockRef->mBlockIdx))
// 							{
// 								blockRef = pred;
// 								didWork = true;
// 							}
// 						}
// 					}
// 				}
// 
// 				if (allMatched)
// 				{
// 					SetAndRestoreValue<BeMCBlock*> prevActiveBlock(mActiveBlock, lowestBlock);
// 					SetAndRestoreValue<int*> prevInstIdxRef(mInsertInstIdxRef, NULL);
// 					auto inst = CreateDefineVReg(result);
// 					inst->mVRegsInitialized = NULL;
// 					inst->mDbgLoc = NULL;
// 					break;
// 				}
// 
// 				if (!didWork)
// 				{
// 					BeMCBlock* nextLowestBlock = NULL;
// 
// 					// Find the next candidate block
// 					for (auto& blockRef : blockSearch)
// 					{
// 						for (auto& pred : blockRef->mPreds)
// 						{
// 							if (pred->mBlockIdx < lowestBlock->mBlockIdx)
// 							{
// 								if ((nextLowestBlock == NULL) || (pred->mBlockIdx > nextLowestBlock->mBlockIdx))
// 									nextLowestBlock = pred;
// 							}
// 						}
// 					}
// 
// 					if (nextLowestBlock == NULL)
// 						break;
// 					lowestBlock = nextLowestBlock;
// 				}
// 			}
// 
// 			CeOperand doneLabel = CeOperand::FromLabel(mCurLabelIdx++);
// 			CreatePhiAssign(mActiveBlock, operand, result, doneLabel);
// 
// 			// Don't use an explicit dbgLoc
// 			SetAndRestoreValue<BeDbgLoc*> prevDbgLoc(mCurDbgLoc, NULL);
// 			AllocInst(BeMCInstKind_Label, doneLabel);
// 		}
// 
// 		return result;
// 	}
// 
// 	if ((operand.mKind == CeOperandKind_CmpResult) && (!allowMetaResult))
// 	{
// 		auto& cmpResult = mCmpResults[operand.mCmpResultIdx];
// 		if (cmpResult.mResultVRegIdx == -1)
// 		{
// 			// Create the vreg now, and insert the CmpToBool during legalization
// 			BeType* boolType = mModule->mContext->GetPrimitiveType(BeTypeCode_Boolean);
// 			operand = AllocVirtualReg(boolType);
// 			cmpResult.mResultVRegIdx = operand.mVRegIdx;
// 
// 			auto vregInfo = GetVRegInfo(operand);
// 			vregInfo->mDefOnFirstUse = true;
// 		}
// 
// 		operand = CeOperand::FromVReg(cmpResult.mResultVRegIdx);
// 	}
// 
// 	if ((operand.mKind == CeOperandKind_NotResult) && (!allowMetaResult))
// 	{
// 		auto mcValue = GetOperand(operand.mNotResult->mValue, false, allowFail);
// 
// 		operand = AllocVirtualReg(GetType(mcValue));
// 		CreateDefineVReg(operand);
// 		AllocInst(BeMCInstKind_Mov, operand, mcValue);
// 
// 		CeOperand xorVal;
// 		xorVal.mKind = CeOperandKind_Immediate_i8;
// 		xorVal.mImmediate = 0x1;
// 		AllocInst(BeMCInstKind_Xor, operand, xorVal);
// 	}

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

	mCeMachine->mCeModule->ProcessMethod(&dupMethodInstance, true);

	if (!dupMethodInstance.mIRFunction)
	{
		mCeFunction->mFailed = true;
		return;
	}
	
	auto irCodeGen = mCeMachine->mCeModule->mBfIRBuilder->mBeIRCodeGen;
	mIntPtrType = irCodeGen->mBeContext->GetPrimitiveType((mPtrSize == 4) ? BeTypeCode_Int32 : BeTypeCode_Int64);
	mBeFunction = (BeFunction*)irCodeGen->GetBeValue(dupMethodInstance.mIRFunction.mId);		

	mCeFunction->mName = mBeFunction->mName;
	mCeMachine->mNamedFunctionMap[mCeFunction->mName] = mCeFunction;

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
				break;
			case BeUnreachableInst::TypeId:
				Emit(CeOp_InvalidOp);
				break;
			case BeAllocaInst::TypeId:
				{
					auto castedInst = (BeAllocaInst*)inst;
					int size = castedInst->mType->mSize;
					bool isAligned16 = false;
					int align = castedInst->mAlign;
					BeType* allocType = castedInst->mType;
					bool preservedVolatiles = false;
					bool doPtrCast = false;

					if (inHeadAlloca)
					{
						mFrameSize += size;

						result.mKind = CeOperandKind_AllocaAddr;
						result.mFrameOfs = -mFrameSize;
						result.mType = castedInst->mType;						
					}
					else
					{
						Fail("Non-head alloca");
						return;
					}
				}
				break;
			case BeLoadInst::TypeId:
				{
					auto castedInst = (BeLoadInst*)inst;
					auto ceTarget = GetOperand(castedInst->mTarget, true);

					if (ceTarget.mKind == CeOperandKind_AllocaAddr)
					{
						result = ceTarget;
						result.mKind = CeOperandKind_FrameOfs;
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

// 					if (castedInst->mOpKind == BeBinaryOpKind_Subtract)
// 					{
// 						if (((mcLHS.IsImmediateFloat()) && (mcLHS.GetImmediateDouble() == 0.0)) ||
// 							((mcLHS.IsImmediateInt()) && (mcLHS.mImmediate == 0)))
// 						{
// 							auto castedInst = (BeNumericCastInst*)inst;
// 
// 							result = AllocVirtualReg(GetType(mcRHS));
// 							CreateDefineVReg(result);
// 							AllocInst(BeMCInstKind_Mov, result, mcRHS);
// 							AllocInst(BeMCInstKind_Neg, result);
// 							break;
// 						}
// 					}
					
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
						EmitBinaryOp(CeOp_SDiv_I8, CeOp_SDiv_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_UDivide:
						EmitBinaryOp(CeOp_UDiv_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_SModulus:
						EmitBinaryOp(CeOp_SMod_I8, CeOp_SMod_F32, ceLHS, ceRHS, result);
						break;
					case BeBinaryOpKind_UModulus:
						EmitBinaryOp(CeOp_UMod_I8, CeOp_InvalidOp, ceLHS, ceRHS, result);
						break;
					default:
						Fail("Invalid binary op");
					}

// 					auto type = GetType(mcLHS);
// 
// 					switch (castedInst->mOpKind)
// 					{
// 					case BeBinaryOpKind_Add: result = AllocBinaryOp(BeMCInstKind_Add, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					case BeBinaryOpKind_Subtract: result = AllocBinaryOp(BeMCInstKind_Sub, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsZero); break;
// 					case BeBinaryOpKind_Multiply: result = AllocBinaryOp(BeMCInstKind_IMul, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsOne); break;
// 					case BeBinaryOpKind_SDivide: result = AllocBinaryOp(BeMCInstKind_IDiv, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsOne); break;
// 					case BeBinaryOpKind_UDivide: result = AllocBinaryOp(BeMCInstKind_Div, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsOne); break;
// 					case BeBinaryOpKind_SModulus: result = AllocBinaryOp(BeMCInstKind_IRem, mcLHS, mcRHS, type->IsFloat() ? BeMCBinIdentityKind_None : BeMCBinIdentityKind_Right_IsOne_Result_Zero); break;
// 					case BeBinaryOpKind_UModulus: result = AllocBinaryOp(BeMCInstKind_Rem, mcLHS, mcRHS, type->IsFloat() ? BeMCBinIdentityKind_None : BeMCBinIdentityKind_Right_IsOne_Result_Zero); break;
// 					case BeBinaryOpKind_BitwiseAnd: result = AllocBinaryOp(BeMCInstKind_And, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
// 					case BeBinaryOpKind_BitwiseOr: result = AllocBinaryOp(BeMCInstKind_Or, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					case BeBinaryOpKind_ExclusiveOr: result = AllocBinaryOp(BeMCInstKind_Xor, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					case BeBinaryOpKind_LeftShift: result = AllocBinaryOp(BeMCInstKind_Shl, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					case BeBinaryOpKind_RightShift: result = AllocBinaryOp(BeMCInstKind_Shr, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					case BeBinaryOpKind_ARightShift: result = AllocBinaryOp(BeMCInstKind_Sar, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
// 					}
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
						if ((toType->IsIntable()) && (fromType->IsIntable()) && (toType->mSize < fromType->mSize))
						{
							// For truncating values, no actual instructions are needed
							result = ceValue;
							result.mType = toType;
						}
						else
						{
							bool doSignExtension = (toType->IsIntable()) && (fromType->IsIntable()) && (toType->mSize > fromType->mSize) && (castedInst->mToSigned) && (castedInst->mValSigned);
// 							if ((toType->IsFloat()) && (fromType->IsIntable()) && (castedInst->mValSigned))
// 								doSignExtension = true;

							result = FrameAlloc(toType);

							CeOp op = CeOp_InvalidOp;

							if (doSignExtension)
							{								
								switch (fromType->mTypeCode)
								{
								case BeTypeCode_Int32:
									switch (toType->mTypeCode)
									{
									case BeTypeCode_Int64:
										op = CeOp_Conv_I32_I64;
									}
								}
							}
							else
							{

							}
							
							if (op == CeOp_InvalidOp)
								Fail("Invalid conversion op");

							Emit(op);
							EmitFrameOffset(result);
							EmitFrameOffset(ceValue);
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

					auto mcVal = GetOperand(castedInst->mPtr);
					auto mcIdx0 = GetOperand(castedInst->mIdx0, false, true);

					BePointerType* ptrType = (BePointerType*)mcVal.mType;
					BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

					result = mcVal;
					if (castedInst->mIdx1 != NULL)
					{
						// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
						BF_ASSERT(castedInst->mIdx0);

						auto mcIdx1 = GetOperand(castedInst->mIdx1);
						if (!mcIdx1.IsImmediate())
						{
							// This path is used when we have a const array that gets indexed by a non-const index value
// 							if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
// 							{
// 								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
// 
// 								auto elementPtrType = mModule->mContext->GetPointerTo(arrayType->mElementType);
// 
// 								auto ptrValue = AllocVirtualReg(elementPtrType);
// 								auto ptrInfo = GetVRegInfo(ptrValue);
// 								ptrInfo->mIsExpr = true;
// 								ptrInfo->mRelTo = result;
// 								CreateDefineVReg(ptrValue);
// 								result = ptrValue;
// 
// 								BeMCOperand mcRelOffset;
// 								int relScale = 1;
// 								if (mcIdx1.IsImmediate())
// 								{
// 									mcRelOffset = BeMCOperand::FromImmediate(mcIdx1.mImmediate * arrayType->mElementType->mSize);
// 								}
// 								else
// 								{
// 									mcRelOffset = mcIdx1;
// 									relScale = arrayType->mElementType->mSize;
// 								}
// 
// 								result = AllocRelativeVirtualReg(elementPtrType, result, mcRelOffset, relScale);
// 								// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
// 								CreateDefineVReg(result);
// 								//TODO: Always correct?
// 								result.mKind = BeMCOperandKind_VReg;
// 
// 							}
// 							else
// 								SoftFail("Invalid GEP", inst->mDbgLoc);
							Fail("Invalid GEP");
						}
						else
						{
							BF_ASSERT(mcIdx1.IsImmediate());
							int byteOffset = 0;
							BeType* elementType = NULL;
							if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
							{
								BeStructType* structType = (BeStructType*)ptrType->mElementType;
								auto& structMember = structType->mMembers[mcIdx1.mImmediate];
								elementType = structMember.mType;
								byteOffset = structMember.mByteOffset;
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
							{
								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = mcIdx1.mImmediate * elementType->mSize;
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_Vector)
							{
								auto arrayType = (BeVectorType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = mcIdx1.mImmediate * elementType->mSize;
							}
							else
							{
								Fail("Invalid gep target");
							}
							
							auto elementPtrType = beModule->mContext->GetPointerTo(elementType);
							
							

							//result = AllocRelativeVirtualReg(elementPtrType, result, GetImmediate(byteOffset), 1);
							// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
							//CreateDefineVReg(result);
							//result.mKind = BeMCOperandKind_VReg;
						}
					}
					else
					{
						// It's temping to do a (IsNonZero) precondition, but if we make a reference to a VReg that is NOT in Addr form,
						//  then this will encode that so we will know we need to do a Load on that value at the Def during legalization

						Fail("Unhandled gep");

// 						BeMCOperand mcRelOffset;
// 						int relScale = 1;
// 						if (mcIdx0.IsImmediate())
// 						{
// 							mcRelOffset = BeMCOperand::FromImmediate(mcIdx0.mImmediate * ptrType->mElementType->mSize);
// 						}
// 						else
// 						{
// 							mcRelOffset = mcIdx0;
// 							relScale = ptrType->mElementType->mSize;
// 						}
// 
// 						result = AllocRelativeVirtualReg(ptrType, result, mcRelOffset, relScale);
// 						// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
// 						CreateDefineVReg(result);
// 						//TODO: Always correct?
// 						result.mKind = BeMCOperandKind_VReg;
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
					auto castedInst = (BeNumericCastInst*)inst;
					auto ceValue = GetOperand(castedInst->mValue);
					EmitUnaryOp(CeOp_Neg_I8, CeOp_Neg_F32, ceValue, result);
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

					if (auto intrin = BeValueDynCast<BeIntrinsic>(castedInst->mFunc))
					{
						Fail("Intrinsics not allowed");
					}
					else if (auto beFunction = BeValueDynCast<BeFunction>(castedInst->mFunc))
					{
						int* callIdxPtr = NULL;
						if (mFunctionMap.TryAdd(beFunction, NULL, &callIdxPtr))
						{
							CeCallEntry callEntry;
							callEntry.mFunctionName = beFunction->mName;
							*callIdxPtr = (int)mCeFunction->mCallTable.size();
							mCeFunction->mCallTable.Add(callEntry);
						}

						callIdx = *callIdxPtr;

						for (int argIdx = (int)castedInst->mArgs.size() - 1; argIdx >= 0; argIdx--)
						{
							auto& arg = castedInst->mArgs[argIdx];
							auto ceArg = GetOperand(arg.mValue);
							EmitSizedOp(CeOp_Push_8, ceArg, NULL, true);
						}

						int stackAdjust = 0;

						auto beFuncType = beFunction->GetFuncType();
						if (beFuncType->mReturnType->mSize > 0)
						{
							Emit(CeOp_AdjustSP);
							Emit((int32)-beFuncType->mReturnType->mSize);
							stackAdjust += beFuncType->mReturnType->mSize;
						}
						
						Emit(CeOp_Call);
						Emit((int32)callIdx);

						if (beFuncType->mReturnType->mSize > 0)
						{
							result = FrameAlloc(beFuncType->mReturnType);
							EmitSizedOp(CeOp_Pop_8, result, NULL, true);
						}

						if (stackAdjust > 0)
						{
							Emit(CeOp_AdjustSP);
							Emit(stackAdjust);
						}
					}
					else
					{
						Fail("Cannot call through function pointers");
// 						auto funcPtrType = castedInst->mFunc->GetType();
// 						if (funcPtrType->IsPointer())
// 						{
// 							auto elementType = ((BePointerType*)funcPtrType)->mElementType;
// 							if (elementType->mTypeCode == BeTypeCode_Function)
// 							{
// 								isVarArg = ((BeFunctionType*)elementType)->mIsVarArg;
// 							}
// 						}
// 
// 						returnType = castedInst->GetType();
// 						ceFunc = GetOperand(castedInst->mFunc);
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
						mCeFunction->mFiles.Add(dbgFile->mFileName);
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

	mCeFunction->mFailed = false;
	mCeFunction->mFrameSize = mFrameSize;

	
}

//////////////////////////////////////////////////////////////////////////

CeMachine::CeMachine(BfCompiler* compiler)
{
	mCompiler = compiler;	
	mCeModule = NULL;	
	mStackMin = NULL;
	mRevision = 0;
	mCurTargetSrc = NULL;
	mCurModule = NULL;
}

CeMachine::~CeMachine()
{
	delete mCeModule;
}

void CeMachine::Fail(const CeFrame& curFrame, const StringImpl& str)
{
	mCurModule->Fail(str, mCurTargetSrc);
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

void CeMachine::CompileStarted()
{
	mRevision++;
	if (mCeModule != NULL)
	{
		delete mCeModule;
		mCeModule = NULL;
	}
}

void CeMachine::RemoveMethod(BfMethodInstance* methodInstance)
{
	auto itr = mFunctions.Find(methodInstance);
	auto ceFunction = itr->mValue;
	BF_ASSERT(itr != mFunctions.end());
	if (itr != mFunctions.end())
	{
		mNamedFunctionMap.Remove(ceFunction->mName);
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
	if (stackPtr < mStackMin) \
	{ \
		_Fail("Stack overflow"); \
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

bool CeMachine::Execute(CeFunction* startFunction, uint8* startStackPtr, uint8* startFramePtr)
{	
	CeFunction* ceFunction = startFunction;
	uint8* memStart = &mMemory[0];	
	uint8* instPtr = &ceFunction->mCode[0];
	uint8* stackPtr = startStackPtr;
	uint8* framePtr = startFramePtr;
	
	auto _GetCurFrame = [&]()
	{
		CeFrame ceFrame;
		ceFrame.mFunction = ceFunction;
		ceFrame.mFramePtr = framePtr;
		ceFrame.mStackPtr = stackPtr;
		ceFrame.mInstPtr = instPtr;
		return ceFrame;
	};

	auto _Fail = [&](const StringImpl& error)
	{
		Fail(_GetCurFrame(), error);
	};
	
	int callCount = 0;

	while (true)
	{
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
				stackPtr = ceFrame.mStackPtr;
				framePtr = ceFrame.mFramePtr;

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
		case CeOp_FrameAddr64:
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
		case CeOp_Load_32:
			{
				auto& result = CE_GETFRAME(uint32);
				auto ceAddr = CE_GETFRAME(uint32);
				// This check will fail for addresses < 256 (null pointer), or out-of-bounds the other direction
				if ((ceAddr - 256) + sizeof(uint32) > (BF_CE_STACK_SIZE - 256))
				{
					//TODO: Throw error
					return false;
				}
				result = *(uint32*)(memStart + ceAddr);
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
				int32 adjust = CE_GETINST(int32);
				stackPtr += adjust;
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
					mNamedFunctionMap.TryGetValue(callEntry.mFunctionName, &callEntry.mFunction);
					callEntry.mBindRevision = mRevision;
				}

				if (callEntry.mFunction == NULL)
				{
					_Fail("Unable to locate function entry");
					break;
				}

// 				if (callEntry.mFunction->mName.Contains("__static_dump"))
// 				{
// 					int32 val = *(int32*)(stackPtr);
// 					OutputDebugStrF("__static_dump: %d\n", val);
// 				}

				if (callEntry.mFunction != NULL)
				{
					mCallStack.Add(_GetCurFrame());

					ceFunction = callEntry.mFunction;
					framePtr = stackPtr;
					stackPtr -= ceFunction->mFrameSize;
					instPtr = &ceFunction->mCode[0];
					CE_CHECKSTACK();
				}
			}
			break;
		case CeOp_Conv_I32_I64:
			CE_CAST(int32, int64);
			break;
		case CeOp_Add_I32:
			CEOP_BIN(+, int32);
			break;
		case CeOp_Add_I64:
			CEOP_BIN(+, int64);
			break;
		case CeOp_Sub_I32:
			CEOP_BIN(-, int32);
			break;
		case CeOp_Sub_I64:
			CEOP_BIN(-, int64);
			break;
		case CeOp_Mul_I32:
			CEOP_BIN(*, int32);
			break;
		case CeOp_Mul_I64:
			CEOP_BIN(*, int64);
			break;
		case CeOp_Cmp_EQ_I32:
			CEOP_CMP(==, int32);
			break;
		case CeOp_Cmp_SLT_I32:
			CEOP_CMP(<, int32);
			break;
		case CeOp_Cmp_ULT_I32:
			CEOP_CMP(<, uint32);
			break;
		case CeOp_Cmp_SLE_I32:
			CEOP_CMP(<=, int32);
			break;
		case CeOp_Cmp_SLE_I64:
			CEOP_CMP(<= , int64);
			break;
		case CeOp_Cmp_ULE_I32:
			CEOP_CMP(<=, uint32);
			break;
		case CeOp_Neg_I32:
			CEOP_UNARY(-, int32);
			break;
		case CeOp_Neg_I64:
			CEOP_UNARY(-, int64);
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
	if (mCeModule == NULL)
		Init();

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

void CeMachine::ProcessWorkQueue()
{
	while (!mWorkQueue.IsEmpty())
	{
		auto ceFunction = mWorkQueue.back();
		mWorkQueue.pop_back();
		PrepareFunction(ceFunction);
	}
}

CeFunction* CeMachine::GetFunction(BfMethodInstance* methodInstance, bool& added)
{		
	CeFunction** functionValuePtr = NULL;
	CeFunction* ceFunction = NULL;
	if (mFunctions.TryAdd(methodInstance, NULL, &functionValuePtr))
	{
		added = true;
		auto module = methodInstance->GetOwner()->mModule;
		
		BF_ASSERT(!methodInstance->mInCEMachine);
		methodInstance->mInCEMachine = true;

		ceFunction = new CeFunction();
		ceFunction->mMethodInstance = methodInstance;		

		*functionValuePtr = ceFunction;
	}
	else
		ceFunction = *functionValuePtr;
	return ceFunction;
}

void CeMachine::QueueMethod(BfMethodInstance* methodInstance)
{
	bool added = false;
	auto ceFunction = GetFunction(methodInstance, added);
	if (added)
		mWorkQueue.Add(ceFunction);
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

	bool added = false;
	CeFunction* ceFunction = GetFunction(methodInstance, added);
	if (!ceFunction->mInitialized)
	{
		PrepareFunction(ceFunction);
		ProcessWorkQueue();
	}
	BF_ASSERT(mWorkQueue.IsEmpty());

	mMemory.Resize(BF_CE_STACK_SIZE);
	auto stackPtr = &mMemory[0] + mMemory.mSize;
	mStackMin = &mMemory[0];	

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

	uint8* retPtr = NULL;
	auto returnType = methodInstance->mReturnType;
	if (!returnType->IsValuelessType())
	{
		int retSize = methodInstance->mReturnType->mSize;
		stackPtr -= retSize;
		retPtr = stackPtr;
	}

	bool success = Execute(ceFunction, stackPtr - ceFunction->mFrameSize, stackPtr);

	mCallStack.Clear();
	
	auto constHolder = module->mBfIRBuilder;

	if (success)
	{
		BfTypedValue retValue;
		if (retPtr != NULL)
		{
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
				return BfTypedValue(constVal, returnType);
		}
		else
		{
			return BfTypedValue(module->mBfIRBuilder->GetFakeVal(), returnType);
		}
	}

	return BfTypedValue();
}
