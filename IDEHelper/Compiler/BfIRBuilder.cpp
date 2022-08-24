#pragma warning(disable:4146)

#include "BeefySysLib/util/BeefPerf.h"
#include "BfIRBuilder.h"
#include "BfUtil.h"
#include "BfModule.h"
#include "BfContext.h"
#include "BfResolvedTypeUtils.h"
#include "BfIRCodeGen.h"
#include "BfMangler.h"
#include "BfCompiler.h"
#include "BfSystem.h"
#include "../Backend/BeIRCodeGen.h"

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/FileSystem.h"

#pragma warning(pop)

static bool gDebugDbgLoc = false;

USING_NS_BF;

#define NEW_CMD_INSERTED NewCmdInserted()
#define NEW_CMD_INSERTED_IRVALUE NewCmdInserted();
#define NEW_CMD_INSERTED_IRTYPE NewCmdInserted();
#define NEW_CMD_INSERTED_IRFUNCTYPE NewCmdInserted();
#define NEW_CMD_INSERTED_IRBLOCK NewCmdInserted();
#define NEW_CMD_INSERTED_IRMD NewCmdInserted();

#define BINOPFUNC_APPLY(lhs, rhs, OP) \
	auto constLHS = GetConstantById(lhs.mId); \
	auto constRHS = GetConstantById(rhs.mId); \
	if (constLHS->mConstType == BfConstType_Undef) return lhs; \
	if (constRHS->mConstType == BfConstType_Undef) return rhs; \
	if ((constLHS->mTypeCode < BfTypeCode_Length) && (constRHS->mTypeCode < BfTypeCode_Length)) \
	{ \
		BF_ASSERT(constLHS->mTypeCode == constRHS->mTypeCode); \
		uint64 val = 0; \
		switch (constLHS->mTypeCode) \
		{ \
		case BfTypeCode_Boolean: val = OP(constLHS->mInt8, constRHS->mInt8); break; \
		case BfTypeCode_Int8: val = OP(constLHS->mInt8, constRHS->mInt8); break; \
		case BfTypeCode_Char8: val = OP(constLHS->mUInt8, constRHS->mUInt8); break; \
		case BfTypeCode_UInt8: val = OP(constLHS->mUInt8, constRHS->mUInt8); break; \
		case BfTypeCode_Int16: val = OP(constLHS->mInt16, constRHS->mInt16); break; \
		case BfTypeCode_UInt16: val = OP(constLHS->mUInt16, constRHS->mUInt16); break; \
		case BfTypeCode_Char16: val = OP(constLHS->mUInt16, constRHS->mUInt16); break; \
		case BfTypeCode_Int32: val = OP(constLHS->mInt32, constRHS->mInt32); break; \
		case BfTypeCode_UInt32: val = OP(constLHS->mUInt32, constRHS->mUInt32); break; \
		case BfTypeCode_Char32: val = OP(constLHS->mUInt32, constRHS->mUInt32); break; \
		case BfTypeCode_Int64: val = OP(constLHS->mInt64, constRHS->mInt64); break; \
		case BfTypeCode_UInt64: val = OP(constLHS->mUInt64, constRHS->mUInt64); break; \
		case BfTypeCode_Float: \
		case BfTypeCode_Double: \
			return CreateConst(constLHS->mTypeCode, OP(constLHS->mDouble, constRHS->mDouble)); break; \
		default: break; \
		} \
		return CreateConst(constLHS->mTypeCode, val); \
	}

#define INT_BINOPFUNC_APPLY(lhs, rhs, OP) \
	auto constLHS = GetConstantById(lhs.mId); \
	auto constRHS = GetConstantById(rhs.mId); \
	if (constLHS->mConstType == BfConstType_Undef) return lhs; \
	if (constRHS->mConstType == BfConstType_Undef) return rhs; \
	if ((constLHS->mTypeCode < BfTypeCode_Length) && (constRHS->mTypeCode < BfTypeCode_Length)) \
	{ \
		BF_ASSERT(constLHS->mTypeCode == constRHS->mTypeCode); \
		uint64 val = 0; \
		switch (constLHS->mTypeCode) \
		{ \
		case BfTypeCode_Boolean: val = OP(constLHS->mInt8, constRHS->mInt8); break; \
		case BfTypeCode_Int8: val = OP(constLHS->mInt8, constRHS->mInt8); break; \
		case BfTypeCode_Char8: val = OP(constLHS->mUInt8, constRHS->mUInt8); break; \
		case BfTypeCode_UInt8: val = OP(constLHS->mUInt8, constRHS->mUInt8); break; \
		case BfTypeCode_Int16: val = OP(constLHS->mInt16, constRHS->mInt16); break; \
		case BfTypeCode_UInt16: val = OP(constLHS->mUInt16, constRHS->mUInt16); break; \
		case BfTypeCode_Char16: val = OP(constLHS->mUInt16, constRHS->mUInt16); break; \
		case BfTypeCode_Int32: val = OP(constLHS->mInt32, constRHS->mInt32); break; \
		case BfTypeCode_UInt32: val = OP(constLHS->mUInt32, constRHS->mUInt32); break; \
		case BfTypeCode_Char32: val = OP(constLHS->mUInt32, constRHS->mUInt32); break; \
		case BfTypeCode_Int64: val = OP(constLHS->mInt64, constRHS->mInt64); break; \
		case BfTypeCode_UInt64: val = OP(constLHS->mUInt64, constRHS->mUInt64); break; \
		default: break; \
		} \
		return CreateConst(constLHS->mTypeCode, val); \
	}

#define BINOP_APPLY(lhs, rhs, OP) \
	auto constLHS = GetConstantById(lhs.mId); \
	auto constRHS = GetConstantById(rhs.mId); \
	if (constLHS->mConstType == BfConstType_Undef) return lhs; \
	if (constRHS->mConstType == BfConstType_Undef) return rhs; \
	if ((constLHS->mTypeCode < BfTypeCode_Length) && (constRHS->mTypeCode < BfTypeCode_Length)) \
	{ \
		BF_ASSERT(constLHS->mTypeCode == constRHS->mTypeCode); \
		uint64 val = 0; \
		switch (constLHS->mTypeCode) \
		{ \
		case BfTypeCode_Boolean: val = constLHS->mInt8 OP constRHS->mInt8; break; \
		case BfTypeCode_Int8: val = constLHS->mInt8 OP constRHS->mInt8; break; \
		case BfTypeCode_Char8: val = constLHS->mUInt8 OP constRHS->mUInt8; break; \
		case BfTypeCode_UInt8: val = constLHS->mUInt8 OP constRHS->mUInt8; break; \
		case BfTypeCode_Int16: val = constLHS->mInt16 OP constRHS->mInt16; break; \
		case BfTypeCode_UInt16: val = constLHS->mUInt16 OP constRHS->mUInt16; break; \
		case BfTypeCode_Char16: val = constLHS->mUInt16 OP constRHS->mUInt16; break; \
		case BfTypeCode_Int32: val = constLHS->mInt32 OP constRHS->mInt32; break; \
		case BfTypeCode_UInt32: val = constLHS->mUInt32 OP constRHS->mUInt32; break; \
		case BfTypeCode_Char32: val = constLHS->mUInt32 OP constRHS->mUInt32; break; \
		case BfTypeCode_Int64: val = constLHS->mInt64 OP constRHS->mInt64; break; \
		case BfTypeCode_UInt64: val = constLHS->mUInt64 OP constRHS->mUInt64; break; \
		case BfTypeCode_Float: \
		case BfTypeCode_Double: \
			return CreateConst(constLHS->mTypeCode, constLHS->mDouble OP constRHS->mDouble); break; \
		default: break; \
		} \
		return CreateConst(constLHS->mTypeCode, val); \
	}

#define INT_BINOP_APPLY(constLHS, constRHS, OP) \
	if (constLHS->mConstType == BfConstType_Undef) return lhs; \
	if (constRHS->mConstType == BfConstType_Undef) return rhs; \
	if ((constLHS->mTypeCode < BfTypeCode_Length) && (constRHS->mTypeCode < BfTypeCode_Length)) \
	{ \
		BF_ASSERT(constLHS->mTypeCode == constRHS->mTypeCode); \
		uint64 val = 0; \
		switch (constLHS->mTypeCode) \
		{ \
		case BfTypeCode_Int8: return CreateConst(constLHS->mTypeCode, constLHS->mInt8 OP constRHS->mInt8); \
		case BfTypeCode_Char8: return CreateConst(constLHS->mTypeCode, constLHS->mUInt8 OP constRHS->mUInt8); \
		case BfTypeCode_UInt8: return CreateConst(constLHS->mTypeCode, constLHS->mUInt8 OP constRHS->mUInt8); \
		case BfTypeCode_Int16: return CreateConst(constLHS->mTypeCode, constLHS->mInt16 OP constRHS->mInt16); \
		case BfTypeCode_UInt16: return CreateConst(constLHS->mTypeCode, constLHS->mUInt16 OP constRHS->mUInt16); \
		case BfTypeCode_Char16: return CreateConst(constLHS->mTypeCode, constLHS->mUInt16 OP constRHS->mUInt16); \
		case BfTypeCode_Int32: return CreateConst(constLHS->mTypeCode, constLHS->mInt32 OP constRHS->mInt32); \
		case BfTypeCode_UInt32: return CreateConst(constLHS->mTypeCode, (uint64)(constLHS->mUInt32 OP constRHS->mUInt32)); \
		case BfTypeCode_Char32: return CreateConst(constLHS->mTypeCode, (uint64)(constLHS->mUInt32 OP constRHS->mUInt32)); \
		case BfTypeCode_Int64: return CreateConst(constLHS->mTypeCode, (uint64)(constLHS->mInt64 OP constRHS->mInt64)); \
		case BfTypeCode_UInt64: return CreateConst(constLHS->mTypeCode, constLHS->mUInt64 OP constRHS->mUInt64); \
		default: break; \
		} \
	}

#define UNARYOP_APPLY(val, OP) \
	auto constVal = GetConstantById(val.mId); \
	if (constVal->mConstType == BfConstType_Undef) return val; \
	if (constVal->mTypeCode < BfTypeCode_Length) \
	{ \
		uint64 val = 0; \
		switch (constVal->mTypeCode) \
		{ \
		case BfTypeCode_Int8: val = OP constVal->mInt8; break; \
		case BfTypeCode_UInt8: val = OP constVal->mUInt8; break; \
		case BfTypeCode_Char8: val = OP constVal->mUInt8; break; \
		case BfTypeCode_Int16: val = OP constVal->mInt16; break; \
		case BfTypeCode_UInt16: val = OP constVal->mUInt16; break; \
		case BfTypeCode_Char16: val = OP constVal->mUInt16; break; \
		case BfTypeCode_Int32: val = OP constVal->mInt32; break; \
		case BfTypeCode_UInt32: val = OP constVal->mUInt32; break; \
		case BfTypeCode_Char32: val = OP constVal->mUInt32; break; \
		case BfTypeCode_Int64: val = OP constVal->mInt64; break; \
		case BfTypeCode_UInt64: val = OP constVal->mUInt64; break; \
		case BfTypeCode_Float: \
		case BfTypeCode_Double: \
			return CreateConst(constVal->mTypeCode, OP constVal->mDouble); break; \
		default: break; \
		} \
		return CreateConst(constVal->mTypeCode, val); \
	}

#define CMP_APPLY(lhs, rhs, OP) \
	auto constLHS = GetConstantById(lhs.mId); \
	auto constRHS = GetConstantById(rhs.mId); \
	if ((constLHS->mConstType == BfConstType_Undef) || (constRHS->mConstType == BfConstType_Undef)) \
	{ \
		return GetUndefConstValue(MapType(mModule->GetPrimitiveType(BfTypeCode_Boolean))); \
	} \
	if ((constLHS->mTypeCode == BfTypeCode_NullPtr) || (constRHS->mTypeCode == BfTypeCode_NullPtr)) \
	{ \
		bool val = constLHS->mTypeCode OP constRHS->mTypeCode; \
		return CreateConst(BfTypeCode_Boolean, val ? (uint64)1 : (uint64)0); \
	} \
	if ((constLHS->mTypeCode < BfTypeCode_Length) && (constRHS->mTypeCode < BfTypeCode_Length)) \
	{ \
		BF_ASSERT(constLHS->mTypeCode == constRHS->mTypeCode); \
		bool val = 0; \
		switch (constLHS->mTypeCode) \
		{ \
		case BfTypeCode_Boolean: val = constLHS->mInt8 OP constRHS->mInt8; break; \
		case BfTypeCode_Int8: val = constLHS->mInt8 OP constRHS->mInt8; break; \
		case BfTypeCode_UInt8: val = constLHS->mUInt8 OP constRHS->mUInt8; break; \
		case BfTypeCode_Int16: val = constLHS->mInt16 OP constRHS->mInt16; break; \
		case BfTypeCode_UInt16: val = constLHS->mUInt16 OP constRHS->mUInt16; break; \
		case BfTypeCode_Int32: val = constLHS->mInt32 OP constRHS->mInt32; break; \
		case BfTypeCode_UInt32: val = constLHS->mUInt32 OP constRHS->mUInt32; break; \
		case BfTypeCode_Int64: val = constLHS->mInt64 OP constRHS->mInt64; break; \
		case BfTypeCode_UInt64: val = constLHS->mUInt64 OP constRHS->mUInt64; break; \
		case BfTypeCode_Float: val = constLHS->mDouble OP constRHS->mDouble; break; \
		case BfTypeCode_Double: val = constLHS->mDouble OP constRHS->mDouble; break; \
		default: break; \
		} \
		return CreateConst(BfTypeCode_Boolean, val ? (uint64)1 : (uint64)0); \
	}

static llvm::GlobalValue::LinkageTypes LLVMMapLinkageType(BfIRLinkageType linkageType)
{
	llvm::GlobalValue::LinkageTypes llvmLinkageType;
	if (linkageType == BfIRLinkageType_Internal)
		llvmLinkageType = llvm::GlobalValue::InternalLinkage;
	else
		llvmLinkageType = llvm::GlobalValue::ExternalLinkage;
	return llvmLinkageType;
}

BfIRValue BfIRValue::sValueless(BfIRValueFlags_Value, -1);

bool BfIRValue::IsFake() const
{
	return mId < -1;
}

bool BfIRValue::IsConst() const
{
	return (mFlags & BfIRValueFlags_Const) != 0;
}

bool BfIRValue::IsArg() const
{
	return (mFlags & BfIRValueFlags_Arg) != 0;
}

bool BfIRValue::IsFromLLVM() const
{
	return (mFlags & BfIRValueFlags_FromLLVM) != 0;
}

//////////////////////////////////////////////////////////////////////////

BfIRFunction::BfIRFunction()
{
	//mFlags = BfIRValueFlags_None;
	mId = -1;
}

//////////////////////////////////////////////////////////////////////////

BfIRFunctionType::BfIRFunctionType()
{
	mId = -1;
}

//////////////////////////////////////////////////////////////////////////

BfIRBlock::BfIRBlock()
{
	mFlags = BfIRValueFlags_None;
	mId = -1;
}

//////////////////////////////////////////////////////////////////////////

BfIRConstHolder::BfIRConstHolder(BfModule* module)
{
	mModule = module;
}

BfIRConstHolder::~BfIRConstHolder()
{
}

String BfIRConstHolder::ToString(BfIRValue irValue)
{
	if ((irValue.mFlags & BfIRValueFlags_Const) != 0)
	{
		auto constant = GetConstantById(irValue.mId);

		if (constant->mTypeCode == BfTypeCode_None)
		{
			return "void";
		}
		else if (constant->mTypeCode == BfTypeCode_NullPtr)
		{
			String ret = "null";
			if (constant->mIRType)
			{
				ret += "\n";
				ret += ToString(constant->mIRType);
			}
			return ret;
		}
		else if (constant->mTypeCode == BfTypeCode_Boolean)
		{
			return constant->mBool ? "true" : "false";
		}
		else if (constant->mTypeCode == BfTypeCode_Float)
		{
			return StrFormat("Constant %ff", constant->mDouble);
		}
		else if (constant->mTypeCode == BfTypeCode_Double)
		{
			return StrFormat("Constant %f", constant->mDouble);
		}
		else if (IsInt(constant->mTypeCode))
		{
			return StrFormat("Constant %lld", constant->mInt64);
		}
		else if (constant->mTypeCode == BfTypeCode_StringId)
		{
			return StrFormat("StringId %d", constant->mInt64);
		}
		else if (constant->mConstType == BfConstType_GlobalVar)
		{
			auto gvConst = (BfGlobalVar*)constant;
			return String("GlobalVar ") + gvConst->mName;
		}
		else if (constant->mConstType == BfConstType_BitCast)
		{
			auto bitcast = (BfConstantBitCast*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, bitcast->mTarget);
			return ToString(targetConst) + " BitCast to " + ToString(bitcast->mToType);
		}
		else if (constant->mConstType == BfConstType_Box)
		{
			auto box = (BfConstantBox*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, box->mTarget);
			return ToString(targetConst) + " box to " + ToString(box->mToType);
		}
		else if (constant->mConstType == BfConstType_GEP32_1)
		{
			auto gepConst = (BfConstantGEP32_1*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
			return ToString(targetConst) + StrFormat(" Gep32 %d", gepConst->mIdx0);
		}
		else if (constant->mConstType == BfConstType_GEP32_2)
		{
			auto gepConst = (BfConstantGEP32_2*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
			return ToString(targetConst) + StrFormat(" Gep32 %d,%d", gepConst->mIdx0, gepConst->mIdx1);
		}
		else if (constant->mConstType == BfConstType_ExtractValue)
		{
			auto gepConst = (BfConstantExtractValue*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
			return ToString(targetConst) + StrFormat(" ExtractValue %d", gepConst->mIdx0);
		}
		else if (constant->mConstType == BfConstType_PtrToInt)
		{
			auto ptrToIntConst = (BfConstantPtrToInt*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, ptrToIntConst->mTarget);
			return ToString(targetConst) + StrFormat(" PtrToInt TypeCode:%d", ptrToIntConst->mToTypeCode);
		}
		else if (constant->mConstType == BfConstType_IntToPtr)
		{
			auto bitcast = (BfConstantIntToPtr*)constant;
			BfIRValue targetConst(BfIRValueFlags_Const, bitcast->mTarget);
			return ToString(targetConst) + " IntToPtr " + ToString(bitcast->mToType);
		}
		else if (constant->mConstType == BfConstType_Agg)
		{
			auto constAgg = (BfConstantAgg*)constant;
			String str = ToString(constAgg->mType);
			str += "(";

			for (int i = 0; i < (int)constAgg->mValues.size(); i++)
			{
				if (i > 0)
					str += ", ";
				str += ToString(constAgg->mValues[i]);
			}
			str += ")";
			return str;
		}
		else if (constant->mConstType == BfConstType_AggZero)
		{
			return ToString(constant->mIRType) + " zeroinitializer";
		}
		else if (constant->mConstType == BfConstType_AggCE)
		{
			auto constAgg = (BfConstantAggCE*)constant;
			return ToString(constAgg->mType) + StrFormat(" aggCe@%p", constAgg->mCEAddr);
		}
		else if (constant->mConstType == BfConstType_ArrayZero8)
		{
			return StrFormat("zero8[%d]", constant->mInt32);
		}
		else if (constant->mConstType == BfConstType_TypeOf)
		{
			auto typeofConst = (BfTypeOf_Const*)constant;
			return "typeof " + mModule->TypeToString(typeofConst->mType);
		}
		else if (constant->mConstType == BfConstType_TypeOf_WithData)
		{
			auto typeofConst = (BfTypeOf_WithData_Const*)constant;
			return "typeof_withData " + mModule->TypeToString(typeofConst->mType);
		}
		else if (constant->mConstType == BfConstType_Undef)
		{
			auto constUndef = (BfConstantUndef*)constant;
			return "undef " + ToString(constUndef->mType);
		}
		else
		{
			BF_FATAL("Unhandled");
		}
	}
	else if ((irValue.mFlags & BfIRValueFlags_Arg) != 0)
	{
		return StrFormat("Arg %d", irValue.mId);
	}
	else if (irValue.mFlags != 0)
	{
		return "Value???";
	}
	else
	{
		BF_ASSERT(irValue.mId == -1);
	}
	return "empty";
}

String BfIRConstHolder::ToString(BfIRType irType)
{
	if (irType.mKind == BfIRTypeData::TypeKind_TypeId)
	{
		return StrFormat("Type#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_TypeInstId)
	{
		return StrFormat("TypeInst#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_TypeInstPtrId)
	{
		return StrFormat("TypeInstPtr#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_SizedArray)
	{
		auto sizedArrayType = (BfConstantSizedArrayType*)GetConstantById(irType.mId);
		return StrFormat("%s[%d]", ToString(sizedArrayType->mType).c_str(), (int)sizedArrayType->mLength);
	}
	else
	{
		return "Type ???";
	}
}

void BfIRConstHolder::pv(const BfIRValue& irValue)
{
	OutputDebugStrF("%s\n", ToString(irValue).c_str());
}

void BfIRConstHolder::FixTypeCode(BfTypeCode& typeCode)
{
	if (typeCode == BfTypeCode_IntPtr)
	{
		if (mModule->mSystem->mPtrSize == 4)
			typeCode = BfTypeCode_Int32;
		else
			typeCode = BfTypeCode_Int64;
	}
	else if (typeCode == BfTypeCode_UIntPtr)
	{
		if (mModule->mSystem->mPtrSize == 4)
			typeCode = BfTypeCode_UInt32;
		else
			typeCode = BfTypeCode_UInt64;
	}
}

int BfIRConstHolder::GetSize(BfTypeCode typeCode, int ptrSize)
{
	switch (typeCode)
	{
	case BfTypeCode_None: return 0;
	case BfTypeCode_CharPtr: return ptrSize;
	case BfTypeCode_StringId: return 4;
	case BfTypeCode_Pointer: return ptrSize;
	case BfTypeCode_NullPtr: return ptrSize;
	case BfTypeCode_Self: return 0;
	case BfTypeCode_Dot: return 0;
	case BfTypeCode_Var: return 0;
	case BfTypeCode_Let: return 0;
	case BfTypeCode_Boolean: return 1;
	case BfTypeCode_Int8: return 1;
	case BfTypeCode_UInt8: return 1;
	case BfTypeCode_Int16: return 2;
	case BfTypeCode_UInt16: return 2;
	case BfTypeCode_Int24: return 3;
	case BfTypeCode_UInt24: return 3;
	case BfTypeCode_Int32: return 4;
	case BfTypeCode_UInt32: return 4;
	case BfTypeCode_Int40: return 5;
	case BfTypeCode_UInt40: return 5;
	case BfTypeCode_Int48: return 6;
	case BfTypeCode_UInt48: return 6;
	case BfTypeCode_Int56: return 7;
	case BfTypeCode_UInt56: return 7;
	case BfTypeCode_Int64: return 8;
	case BfTypeCode_UInt64: return 8;
	case BfTypeCode_Int128: return 16;
	case BfTypeCode_UInt128: return 16;
	case BfTypeCode_IntPtr: return ptrSize;
	case BfTypeCode_UIntPtr: return ptrSize;
	case BfTypeCode_IntUnknown: return 0;
	case BfTypeCode_UIntUnknown: return 0;
	case BfTypeCode_Char8: return 1;
	case BfTypeCode_Char16: return 2;
	case BfTypeCode_Char32: return 4;
	case BfTypeCode_Float: return 4;
	case BfTypeCode_Double: return 8;
	case BfTypeCode_Float2: return 8;
	case BfTypeCode_Object: return ptrSize;
	case BfTypeCode_Interface: return ptrSize;
	case BfTypeCode_Struct: return 0;
	case BfTypeCode_Enum: return 0;
	case BfTypeCode_TypeAlias: return 0;
	case BfTypeCode_Extension: return 0;
	case BfTypeCode_FloatX2: return 4 * 2;
	case BfTypeCode_FloatX3: return 4 * 3;
	case BfTypeCode_FloatX4: return 4 * 4;
	case BfTypeCode_DoubleX2: return 8 * 2;
	case BfTypeCode_DoubleX3: return 8 * 3;
	case BfTypeCode_DoubleX4: return 8 * 4;
	case BfTypeCode_Int64X2: return 8 * 2;
	case BfTypeCode_Int64X3: return 8 * 3;
	case BfTypeCode_Int64X4: return 8 * 4;
	default: return 0;
	}
}

int BfIRConstHolder::GetSize(BfTypeCode typeCode)
{
	return GetSize(typeCode, mModule->mSystem->mPtrSize);
}

bool BfIRConstHolder::IsInt(BfTypeCode typeCode)
{
	return (typeCode >= BfTypeCode_Int8) && (typeCode <= BfTypeCode_Char32);
}

bool BfIRConstHolder::IsChar(BfTypeCode typeCode)
{
	return (typeCode >= BfTypeCode_Char8) && (typeCode <= BfTypeCode_Char32);
}

bool BfIRConstHolder::IsIntable(BfTypeCode typeCode)
{
	return (typeCode >= BfTypeCode_Boolean) && (typeCode <= BfTypeCode_Char32);
}

bool BfIRConstHolder::IsSigned(BfTypeCode typeCode)
{
	return (typeCode == BfTypeCode_Int64) ||
		(typeCode == BfTypeCode_Int32) ||
		(typeCode == BfTypeCode_Int16) ||
		(typeCode == BfTypeCode_Int8) ||
		(typeCode == BfTypeCode_IntPtr);
}

bool BfIRConstHolder::IsFloat(BfTypeCode typeCode)
{
	return (typeCode == BfTypeCode_Float) ||
		(typeCode == BfTypeCode_Double);
}

const char* BfIRConstHolder::AllocStr(const StringImpl& str)
{
	char* strCopy = (char*)mTempAlloc.AllocBytes((int)str.length() + 1);
	memcpy(strCopy, str.c_str(), str.length());
	return strCopy;
}

BfConstant* BfIRConstHolder::GetConstantById(int id)
{
	return (BfConstant*)mTempAlloc.GetChunkedPtr(id);
}

BfConstant* BfIRConstHolder::GetConstant(BfIRValue id)
{
	if (!id.IsConst())
		return NULL;
#ifdef CHECK_CONSTHOLDER
	BF_ASSERT(id.mHolder == this);
#endif
	return GetConstantById(id.mId);
}

bool BfIRConstHolder::TryGetBool(BfIRValue id, bool& boolVal)
{
	auto constant = GetConstant(id);
	if ((constant != NULL) && (constant->mTypeCode == BfTypeCode_Boolean))
	{
		boolVal = constant->mBool;
		return true;
	}
	return false;
}

int BfIRConstHolder::IsZero(BfIRValue value)
{
	auto constant = GetConstant(value);
	if (constant == NULL)
		return -1;

	if ((constant->mTypeCode >= BfTypeCode_Boolean) && (constant->mTypeCode <= BfTypeCode_Double))
	{
		return (constant->mUInt64 == 0) ? 1 : 0;
	}

	if (constant->mConstType == BfConstType_AggZero)
		return 1;

	if (constant->mConstType == BfConstType_Agg)
	{
		auto constAgg = (BfConstantAgg*)constant;
		for (int i = 0; i < constAgg->mValues.mSize; i++)
		{
			int elemResult = IsZero(constAgg->mValues[i]);
			if (elemResult != 1)
				return elemResult;
		}
		return 1;
	}

	return -1;
}

bool BfIRConstHolder::IsConstValue(BfIRValue value)
{
	auto constant = GetConstant(value);
	if (constant == NULL)
		return false;

	if (constant->mConstType == BfConstType_GlobalVar)
		return false;

	return true;
}

int BfIRConstHolder::CheckConstEquality(BfIRValue lhs, BfIRValue rhs)
{
	auto constLHS = GetConstant(lhs);
	if (constLHS == NULL)
		return -1;
	auto constRHS = GetConstant(rhs);
	if (constRHS == NULL)
		return -1;

	if (constLHS == constRHS)
		return 1;

	if (constLHS->mConstType == BfConstType_BitCast)
		return CheckConstEquality(BfIRValue(BfIRValueFlags_Const, ((BfConstantBitCast*)constLHS)->mTarget), rhs);
	if (constRHS->mConstType == BfConstType_BitCast)
		return CheckConstEquality(lhs, BfIRValue(BfIRValueFlags_Const, ((BfConstantBitCast*)constRHS)->mTarget));

	int lhsZero = IsZero(lhs);
	if (lhsZero != -1)
	{
		int rhsZero = IsZero(rhs);
		if (rhsZero != -1)
		{
			if (lhsZero || rhsZero)
				return (lhsZero == rhsZero) ? 1 : 0;
		}
	}

	if (((constLHS->mConstType == BfConstType_TypeOf) || (constLHS->mConstType == BfConstType_TypeOf_WithData)) &&
		((constRHS->mConstType == BfConstType_TypeOf) || (constRHS->mConstType == BfConstType_TypeOf_WithData)))
	{
		auto typeOfLHS = (BfTypeOf_Const*)constLHS;
		auto typeOfRHS = (BfTypeOf_Const*)constRHS;
		return (typeOfLHS->mType == typeOfRHS->mType) ? 1 : 0;
	}

	if (constLHS->mTypeCode != constRHS->mTypeCode)
		return -1;

	if ((constLHS->mTypeCode >= BfTypeCode_Boolean) && (constLHS->mTypeCode <= BfTypeCode_Double))
	{
		return (constLHS->mUInt64 == constRHS->mUInt64) ? 1 : 0;
	}

	if (constLHS->mConstType == BfConstType_Agg)
	{
		auto aggLHS = (BfConstantAgg*)constLHS;
		auto aggRHS = (BfConstantAgg*)constRHS;

		if (aggLHS->mValues.mSize != aggRHS->mValues.mSize)
			return -1;

		for (int i = 0; i < aggLHS->mValues.mSize; i++)
		{
			int elemResult = CheckConstEquality(aggLHS->mValues[i], aggRHS->mValues[i]);
			if (elemResult != 1)
				return elemResult;
		}
		return 1;
	}

	//TODO: Why did we do this? This made global variable comparisons (ie: sA != sB) const-evaluate to false always
// 	if (constLHS->mConstType == BfConstType_GlobalVar)
// 	{
// 		// We would have already caught the (constLHS == constRHS) case further up
// 		return 0;
// 	}

	return -1;
}

BfIRType BfIRConstHolder::GetSizedArrayType(BfIRType elementType, int length)
{
	auto constSizedArrayType = mTempAlloc.Alloc<BfConstantSizedArrayType>();
	constSizedArrayType->mConstType = BfConstType_SizedArrayType;
	constSizedArrayType->mType = elementType;
	constSizedArrayType->mLength = length;

	int chunkId = mTempAlloc.GetChunkedId(constSizedArrayType);

	BfIRType retType;
	retType.mKind = BfIRTypeData::TypeKind_SizedArray;
	retType.mId = chunkId;
	return retType;
}

BfIRValue BfIRConstHolder::CreateConst(BfTypeCode typeCode, uint64 val)
{
	if (typeCode == BfTypeCode_IntUnknown)
		typeCode = BfTypeCode_Int64;
	else if (typeCode == BfTypeCode_UIntUnknown)
		typeCode = BfTypeCode_UInt64;

	FixTypeCode(typeCode);
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mTypeCode = typeCode;

	// Properly sign extend into int64
	switch (typeCode)
	{
	case BfTypeCode_Int8:
		constant->mInt64 = (int8)val;
		break;
	case BfTypeCode_Int16:
		constant->mInt64 = (int16)val;
		break;
	case BfTypeCode_Int32:
		constant->mInt64 = (int32)val;
		break;
	case BfTypeCode_UInt8:
		constant->mInt8 = (uint8)val;
		break;
	case BfTypeCode_UInt16:
		constant->mInt64 = (uint16)val;
		break;
	case BfTypeCode_UInt32:
		constant->mInt64 = (uint32)val;
		break;
	default:
		constant->mUInt64 = val;
		break;
	}

	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	BF_ASSERT(GetConstant(irValue) == constant);

	return irValue;
}

BfIRValue BfIRConstHolder::CreateConst(BfTypeCode typeCode, int val)
{
	FixTypeCode(typeCode);
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mTypeCode = typeCode;

	// Properly sign extend into int64
	switch (typeCode)
	{
	case BfTypeCode_Int8:
		constant->mInt64 = (int8)val;
		break;
	case BfTypeCode_Int16:
		constant->mInt64 = (int16)val;
		break;
	case BfTypeCode_UInt8:
		constant->mInt64 = (uint8)val;
		break;
	case BfTypeCode_UInt16:
		constant->mInt64 = (uint16)val;
		break;
	case BfTypeCode_UInt32:
		constant->mInt64 = (uint32)val;
		break;
	default:
		constant->mInt64 = val;
		break;
	}

	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif

	return irValue;
}

BfIRValue BfIRConstHolder::CreateConst(BfTypeCode typeCode, double val)
{
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mTypeCode = typeCode;
	constant->mDouble = val;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif

	return irValue;
}

BfIRValue BfIRConstHolder::CreateConst(BfConstant* fromConst, BfIRConstHolder* fromHolder)
{
	BfConstant* copiedConst = NULL;

	int chunkId = -1;

	if ((fromConst->mConstType == BfConstType_BitCast) || (fromConst->mConstType == BfConstType_BitCastNull))
	{
		//HMM- This should never happen?  Is that true?  We always just store string refs as ints
		//BF_FATAL("Bad");
		auto fromConstBitCast = (BfConstantBitCast*)fromConst;
		BfIRValue copiedTarget;
		if (fromConstBitCast->mTarget)
		{
			auto fromTarget = fromHolder->GetConstantById(fromConstBitCast->mTarget);
			copiedTarget = CreateConst(fromTarget, fromHolder);
		}
		auto ptrToInt = mTempAlloc.Alloc<BfConstantBitCast>();
		ptrToInt->mConstType = fromConst->mConstType;
		ptrToInt->mTarget = copiedTarget.mId;
		ptrToInt->mToType = fromConstBitCast->mToType;
		copiedConst = (BfConstant*)ptrToInt;
	}
	else if (fromConst->mConstType == BfConstType_GlobalVar)
	{
		auto fromGlobalVar = (BfGlobalVar*)fromConst;
		return CreateGlobalVariableConstant(fromGlobalVar->mType, fromGlobalVar->mIsConst, fromGlobalVar->mLinkageType, fromGlobalVar->mInitializer, fromGlobalVar->mName, fromGlobalVar->mIsTLS);
	}
	else if (fromConst->mConstType == BfConstType_GEP32_2)
	{
		auto fromConstGEP = (BfConstantGEP32_2*)fromConst;
		auto fromTarget = fromHolder->GetConstantById(fromConstGEP->mTarget);
		auto copiedTarget = CreateConst(fromTarget, fromHolder);
		auto constGEP = mTempAlloc.Alloc<BfConstantGEP32_2>();
		constGEP->mConstType = BfConstType_GEP32_2;
		constGEP->mTarget = copiedTarget.mId;
		constGEP->mIdx0 = fromConstGEP->mIdx0;
		constGEP->mIdx1 = fromConstGEP->mIdx1;
		copiedConst = (BfConstant*)constGEP;
	}
	else if (fromConst->mConstType == BfConstType_ExtractValue)
	{
		auto fromConstGEP = (BfConstantExtractValue*)fromConst;
		auto fromTarget = fromHolder->GetConstantById(fromConstGEP->mTarget);
		auto copiedTarget = CreateConst(fromTarget, fromHolder);
		auto constGEP = mTempAlloc.Alloc<BfConstantExtractValue>();
		constGEP->mConstType = BfConstType_ExtractValue;
		constGEP->mTarget = copiedTarget.mId;
		constGEP->mIdx0 = fromConstGEP->mIdx0;
		copiedConst = (BfConstant*)constGEP;
	}
	else if (fromConst->mConstType == BfConstType_TypeOf)
	{
		auto typeOf = (BfTypeOf_Const*)fromConst;
		return CreateTypeOf(typeOf->mType);
	}
	else if (fromConst->mConstType == BfConstType_TypeOf_WithData)
	{
		auto typeOf = (BfTypeOf_WithData_Const*)fromConst;
		auto dataConstant = fromHolder->GetConstant(typeOf->mTypeData);
		return CreateTypeOf(typeOf->mType, CreateConst(dataConstant, fromHolder));
	}
	else if (fromConst->mConstType == BfConstType_AggZero)
	{
		auto aggZero = (BfConstant*)fromConst;
		return CreateConstAggZero(fromConst->mIRType);
	}
	else if (fromConst->mConstType == BfConstType_Agg)
	{
		auto constAgg = (BfConstantAgg*)fromConst;

		BfSizedVector<BfIRValue, 8> copiedVals;
		copiedVals.reserve(constAgg->mValues.size());
		for (auto fromVal : constAgg->mValues)
		{
			auto elementConst = fromHolder->GetConstant(fromVal);
			copiedVals.push_back(CreateConst(elementConst, fromHolder));
		}
		return CreateConstAgg(constAgg->mType, copiedVals);
	}
	else if (fromConst->mConstType == BfConstType_Undef)
	{
		auto constUndef = (BfConstantUndef*)fromConst;
		BF_ASSERT(constUndef->mType.mKind != BfIRTypeData::TypeKind_Stream);
		if (constUndef->mType.mKind == BfIRTypeData::TypeKind_Stream)
			return GetUndefConstValue(BfIRValue());
		return GetUndefConstValue(constUndef->mType);
	}
	else if (fromConst->mConstType == BfConstType_ArrayZero8)
	{
		return CreateConstArrayZero(fromConst->mInt32);
	}
	else if ((IsInt(fromConst->mTypeCode)) || (fromConst->mTypeCode == BfTypeCode_Boolean) || (fromConst->mTypeCode == BfTypeCode_StringId))
	{
		return CreateConst(fromConst->mTypeCode, fromConst->mUInt64);
	}
	else if ((fromConst->mTypeCode == BfTypeCode_Float) || (fromConst->mTypeCode == BfTypeCode_Double))
	{
		return CreateConst(fromConst->mTypeCode, fromConst->mDouble);
	}
	else if (fromConst->mTypeCode == BfTypeCode_NullPtr)
	{
		if (fromConst->mIRType)
			return CreateConstNull(fromConst->mIRType);
		else
			return CreateConstNull();
	}
	else if (fromConst->mConstType == BfConstType_PtrToInt)
	{
		auto fromPtrToInt = (BfConstantPtrToInt*)fromConst;
		auto fromTarget = fromHolder->GetConstantById(fromPtrToInt->mTarget);
		auto copiedTarget = CreateConst(fromTarget, fromHolder);
		auto ptrToInt = mTempAlloc.Alloc<BfConstantPtrToInt>();
		ptrToInt->mConstType = BfConstType_PtrToInt;
		ptrToInt->mTarget = copiedTarget.mId;
		ptrToInt->mToTypeCode = fromPtrToInt->mToTypeCode;
		copiedConst = (BfConstant*)ptrToInt;
	}
	else if (fromConst->mConstType == BfConstType_IntToPtr)
	{
		auto fromPtrToInt = (BfConstantIntToPtr*)fromConst;
		auto fromTarget = fromHolder->GetConstantById(fromPtrToInt->mTarget);
		auto copiedTarget = CreateConst(fromTarget, fromHolder);
		auto ptrToInt = mTempAlloc.Alloc<BfConstantIntToPtr>();
		ptrToInt->mConstType = BfConstType_IntToPtr;
		ptrToInt->mTarget = copiedTarget.mId;
		ptrToInt->mToType = fromPtrToInt->mToType;
		copiedConst = (BfConstant*)ptrToInt;
	}

	else
	{
		BF_FATAL("not handled");
	}

	BfIRValue retVal;
	retVal.mFlags = BfIRValueFlags_Const;
	if (chunkId == -1)
		chunkId = mTempAlloc.GetChunkedId(copiedConst);
	retVal.mId = chunkId;
	BF_ASSERT(retVal.mId >= 0);
#ifdef CHECK_CONSTHOLDER
	retVal.mHolder = this;
#endif
	return retVal;
}

BfIRValue BfIRConstHolder::CreateConstNull()
{
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mTypeCode = BfTypeCode_NullPtr;
	constant->mIRType = BfIRType();
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstNull(BfIRType ptrType)
{
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mTypeCode = BfTypeCode_NullPtr;
	constant->mIRType = ptrType;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstAggZero(BfIRType aggType)
{
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mConstType = BfConstType_AggZero;
	constant->mIRType = aggType;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstAgg(BfIRType type, const BfSizedArray<BfIRValue>& values)
{
#ifdef _DEBUG
	for (auto& val : values)
	{
		BF_ASSERT(val);
	}
#endif

	BfConstantAgg* constant = mTempAlloc.Alloc<BfConstantAgg>();
	constant->mConstType = BfConstType_Agg;
	constant->mType = type = type;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));

	constant->mValues.mVals = (BfIRValue*)mTempAlloc.AllocBytes(sizeof(BfIRValue) * values.mSize, 8);
	memcpy(constant->mValues.mVals, values.mVals, sizeof(BfIRValue) * values.mSize);
	constant->mValues.mSize = values.mSize;

#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstAggCE(BfIRType type, addr_ce addr)
{
	BfConstantAggCE* constant = mTempAlloc.Alloc<BfConstantAggCE>();
	constant->mConstType = BfConstType_AggCE;
	constant->mType = type = type;
	constant->mCEAddr = addr;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));

#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstArrayZero(BfIRType type, int count)
{
	BfConstantArrayZero* constant = mTempAlloc.Alloc<BfConstantArrayZero>();
	constant->mConstType = BfConstType_ArrayZero;
	constant->mType = type = type;
	constant->mCount = count;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));

#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstArrayZero(int count)
{
	BfConstant* constant = mTempAlloc.Alloc<BfConstant>();
	constant->mConstType = BfConstType_ArrayZero8;
	constant->mInt64 = count;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constant));

#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateConstBitCast(BfIRValue val, BfIRType type)
{
	auto constVal = GetConstant(val);

	auto bitCast = mTempAlloc.Alloc<BfConstantBitCast>();
	if ((constVal == NULL) || (constVal->IsNull()))
		bitCast->mConstType = BfConstType_BitCastNull;
	else
		bitCast->mConstType = BfConstType_BitCast;
	BF_ASSERT(val.mId != -1);
	bitCast->mTarget = val.mId;
	bitCast->mToType = type;

	BfIRValue castedVal(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(bitCast));
#ifdef CHECK_CONSTHOLDER
	castedVal.mHolder = this;
#endif
	BF_ASSERT((void*)GetConstant(castedVal) == (void*)bitCast);
	return castedVal;
}

BfIRValue BfIRConstHolder::CreateConstBox(BfIRValue val, BfIRType type)
{
	auto constVal = GetConstant(val);

	auto box = mTempAlloc.Alloc<BfConstantBox>();
	box->mConstType = BfConstType_Box;
	BF_ASSERT(val.mId != -1);
	box->mTarget = val.mId;
	box->mToType = type;

	BfIRValue castedVal(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(box));
#ifdef CHECK_CONSTHOLDER
	castedVal.mHolder = this;
#endif
	BF_ASSERT((void*)GetConstant(castedVal) == (void*)box);
	return castedVal;
}

BfIRValue BfIRConstHolder::CreateTypeOf(BfType* type)
{
	BfTypeOf_Const* typeOf = mTempAlloc.Alloc<BfTypeOf_Const>();
	typeOf->mConstType = BfConstType_TypeOf;
	typeOf->mType = type;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(typeOf));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::CreateTypeOf(BfType* type, BfIRValue typeData)
{
	BfTypeOf_WithData_Const* typeOf = mTempAlloc.Alloc<BfTypeOf_WithData_Const>();
	typeOf->mConstType = BfConstType_TypeOf_WithData;
	typeOf->mType = type;
	typeOf->mTypeData = typeData;
	auto irValue = BfIRValue(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(typeOf));
#ifdef CHECK_CONSTHOLDER
	irValue.mHolder = this;
#endif
	return irValue;
}

BfIRValue BfIRConstHolder::GetUndefConstValue(BfIRType irType)
{
	auto constUndef = mTempAlloc.Alloc<BfConstantUndef>();
	constUndef->mConstType = BfConstType_Undef;
	constUndef->mType = irType;

	BfIRValue undefVal(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(constUndef));
#ifdef CHECK_CONSTHOLDER
	castedVal.mHolder = this;
#endif
	BF_ASSERT((void*)GetConstant(undefVal) == (void*)constUndef);
	return undefVal;
}

bool BfIRConstHolder::WriteConstant(BfIRValue val, void* ptr, BfType* type)
{
	auto constant = GetConstant(val);
	if (constant == NULL)
		return false;

	switch (constant->mTypeCode)
	{
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Boolean:
	case BfTypeCode_Char8:
		*(int8*)ptr = constant->mInt8;
		return true;
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		*(int16*)ptr = constant->mInt16;
		return true;
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
	case BfTypeCode_StringId:
		*(int32*)ptr = constant->mInt32;
		return true;
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:
		*(int64*)ptr = constant->mInt64;
		return true;
	case BfTypeCode_NullPtr:
		if (mModule->mSystem->mPtrSize == 4)
			*(int32*)ptr = 0;
		else
			*(int64*)ptr = 0;
		return true;
	case BfTypeCode_Float:
		*(float*)ptr = (float)constant->mDouble;
		return true;
	case BfTypeCode_Double:
		*(double*)ptr = constant->mDouble;
		return true;
	}

	if (constant->mConstType == BfConstType_Agg)
	{
		auto aggConstant = (BfConstantAgg*)constant;
		if (type->IsSizedArray())
		{
			auto sizedArrayType = (BfSizedArrayType*)type;
			for (int i = 0; i < sizedArrayType->mElementCount; i++)
			{
				if (!WriteConstant(aggConstant->mValues[i], (uint8*)ptr + (i * sizedArrayType->mElementType->GetStride()), sizedArrayType->mElementType))
					return false;
			}

			return true;
		}
		else
		{
			BF_ASSERT(type->IsStruct());

			mModule->PopulateType(type);
			auto typeInst = type->ToTypeInstance();
			int idx = 0;

			if (typeInst->mBaseType != NULL)
			{
				if (!WriteConstant(aggConstant->mValues[0], ptr, typeInst->mBaseType))
					return false;
			}

			if (typeInst->IsUnion())
			{
				auto innerType = typeInst->GetUnionInnerType();
				if (!WriteConstant(aggConstant->mValues[1], (uint8*)ptr, innerType))
					return false;
			}

			if ((!typeInst->IsUnion()) || (typeInst->IsPayloadEnum()))
			{
				for (auto& fieldInstance : typeInst->mFieldInstances)
				{
					if (fieldInstance.mDataOffset < 0)
						continue;
					if (!WriteConstant(aggConstant->mValues[fieldInstance.mDataIdx], (uint8*)ptr + fieldInstance.mDataOffset, fieldInstance.mResolvedType))
						return false;
				}
			}
		}
		return true;
	}

	if (constant->mConstType == BfConstType_AggZero)
	{
		BF_ASSERT(type->IsComposite());
		memset(ptr, 0, type->mSize);
		return true;
	}

	if (constant->mConstType == BfConstType_BitCast)
	{
		auto constBitCast = (BfConstantBitCast*)constant;

		auto constTarget = mModule->mBfIRBuilder->GetConstantById(constBitCast->mTarget);
		return WriteConstant(BfIRValue(BfIRValueFlags_Const, constBitCast->mTarget), ptr, type);
	}

	if (constant->mConstType == BfConstType_GlobalVar)
	{
		auto constGV = (BfGlobalVar*)constant;
		const char* strDataPrefix = "__bfStrData";
		if (strncmp(constGV->mName, strDataPrefix, strlen(strDataPrefix)) == 0)
		{
			*(int32*)ptr = atoi(constGV->mName + strlen(strDataPrefix));
			return true;
		}

		const char* strObjPrefix = "__bfStrObj";
		if (strncmp(constGV->mName, strObjPrefix, strlen(strObjPrefix)) == 0)
		{
			*(int32*)ptr = atoi(constGV->mName + strlen(strObjPrefix));
			return true;
		}
	}

	return false;
}

BfIRValue BfIRConstHolder::ReadConstant(void* ptr, BfType* type)
{
	if (type->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)type;
		switch (primType->mTypeDef->mTypeCode)
		{
		case BfTypeCode_Int8:
		case BfTypeCode_UInt8:
		case BfTypeCode_Boolean:
		case BfTypeCode_Char8:
			return CreateConst(primType->mTypeDef->mTypeCode, *(int8*)ptr);
		case BfTypeCode_Int16:
		case BfTypeCode_UInt16:
		case BfTypeCode_Char16:
			return CreateConst(primType->mTypeDef->mTypeCode, *(int16*)ptr);
		case BfTypeCode_Int32:
		case BfTypeCode_UInt32:
		case BfTypeCode_Char32:
		case BfTypeCode_StringId:
			return CreateConst(primType->mTypeDef->mTypeCode, *(int32*)ptr);
		case BfTypeCode_Int64:
		case BfTypeCode_UInt64:
			return CreateConst(primType->mTypeDef->mTypeCode, *(uint64*)ptr);
		case BfTypeCode_NullPtr:
			return CreateConstNull();
		case BfTypeCode_Float:
			return CreateConst(primType->mTypeDef->mTypeCode, *(float*)ptr);
		case BfTypeCode_Double:
			return CreateConst(primType->mTypeDef->mTypeCode, *(double*)ptr);
		case BfTypeCode_IntPtr:
		case BfTypeCode_UIntPtr:
			if (mModule->mSystem->mPtrSize == 4)
				return CreateConst(primType->mTypeDef->mTypeCode, *(int32*)ptr);
			else
				return CreateConst(primType->mTypeDef->mTypeCode, *(uint64*)ptr);
		default:
			return BfIRValue();
		}
	}

	if (type->IsTypedPrimitive())
	{
		return ReadConstant(ptr, type->GetUnderlyingType());
	}

	if (type->IsSizedArray())
	{
		SizedArray<BfIRValue, 8> irValues;

		auto sizedArrayType = (BfSizedArrayType*)type;
		for (int i = 0; i < sizedArrayType->mElementCount; i++)
		{
			auto val = ReadConstant((uint8*)ptr + (i * sizedArrayType->mElementType->GetStride()), sizedArrayType->mElementType);
			if (!val)
				return BfIRValue();
			irValues.Add(val);
		}

		BfIRType irType;
		irType.mKind = BfIRTypeData::TypeKind_TypeId;
		irType.mId = type->mTypeId;
		return CreateConstAgg(irType, irValues);
	}

	if (type->IsStruct())
	{
		mModule->PopulateType(type);
		auto typeInst = type->ToTypeInstance();
		int idx = 0;

		SizedArray<BfIRValue, 8> irValues;

		if (typeInst->mBaseType != NULL)
		{
			auto val = ReadConstant(ptr, typeInst->mBaseType);
			if (!val)
				return BfIRValue();
			irValues.Add(val);
		}

		if (typeInst->IsUnion())
		{
			auto innerType = typeInst->GetUnionInnerType();
			auto val = ReadConstant(ptr, innerType);
			if (!val)
				return BfIRValue();
			irValues.Add(val);
		}

		if ((!typeInst->IsUnion()) || (typeInst->IsPayloadEnum()))
		{
			for (auto& fieldInstance : typeInst->mFieldInstances)
			{
				if (fieldInstance.mDataOffset < 0)
					continue;
				auto val = ReadConstant((uint8*)ptr + fieldInstance.mDataOffset, fieldInstance.mResolvedType);
				if (!val)
					return BfIRValue();
				irValues.Add(val);
			}
		}
		BfIRType irType;
		irType.mKind = BfIRTypeData::TypeKind_TypeId;
		irType.mId = type->mTypeId;
		return CreateConstAgg(irType, irValues);
	}

	if (type->IsInstanceOf(mModule->mCompiler->mStringTypeDef))
	{
		return CreateConst(BfTypeCode_StringId, *(int32*)ptr);
	}

	return BfIRValue();
}

//////////////////////////////////////////////////////////////////////////

void BfIRBuilder::OpFailed()
{
	mOpFailed = true;
}

uint8 BfIRBuilder::CheckedAdd(uint8 a, uint8 b)
{
	uint32 result = (uint32)a + b;
	if (result & 0xFFFFFF00)
		OpFailed();
	return (uint8)result;
}

uint16 BfIRBuilder::CheckedAdd(uint16 a, uint16 b)
{
	uint32 result = (uint32)a + b;
	if (result & 0xFFFF0000)
		OpFailed();
	return (uint16)result;
}

uint32 BfIRBuilder::CheckedAdd(uint32 a, uint32 b)
{
	uint32 result = a + b;
	if (result < a)
		OpFailed();
	return (uint32)result;
}

uint64 BfIRBuilder::CheckedAdd(uint64 a, uint64 b)
{
	uint64 result = a + b;
	if (result < a)
		OpFailed();
	return (uint64)result;
}

int8 BfIRBuilder::CheckedAdd(int8 a, int8 b)
{
	int32 result = (int32)a + b;
	if ((result > 0x7F) || (result < -0x80))
		OpFailed();
	return (int8)result;
}

int16 BfIRBuilder::CheckedAdd(int16 a, int16 b)
{
	int32 result = (int32)a + b;
	if ((result > 0x7FFF) || (result < -0x8000))
		OpFailed();
	return (int16)result;
}

int32 BfIRBuilder::CheckedAdd(int32 a, int32 b)
{
	int32 result = a + b;
	if (b >= 0)
	{
		if (result < a)
			OpFailed();
	}
	else if (result > a)
		OpFailed();
	return (uint32)result;
}

int64 BfIRBuilder::CheckedAdd(int64 a, int64 b)
{
	int64 result = a + b;
	if (b >= 0)
	{
		if (result < a)
			OpFailed();
	}
	else if (result > a)
		OpFailed();
	return (uint64)result;
}

///

uint8 BfIRBuilder::CheckedSub(uint8 a, uint8 b)
{
	uint32 result = (uint32)a - b;
	if (result & 0xFFFFFF00)
		OpFailed();
	return (uint8)result;
}

uint16 BfIRBuilder::CheckedSub(uint16 a, uint16 b)
{
	uint32 result = (uint32)a - b;
	if (result & 0xFFFF0000)
		OpFailed();
	return (uint16)result;
}

uint32 BfIRBuilder::CheckedSub(uint32 a, uint32 b)
{
	uint32 result = a - b;
	if (result > a)
		OpFailed();
	return (uint32)result;
}

uint64 BfIRBuilder::CheckedSub(uint64 a, uint64 b)
{
	uint64 result = a - b;
	if (result > a)
		OpFailed();
	return (uint64)result;
}

int8 BfIRBuilder::CheckedSub(int8 a, int8 b)
{
	int32 result = (int32)a - b;
	if ((result > 0x7F) || (result < -0x80))
		OpFailed();
	return (int8)result;
}

int16 BfIRBuilder::CheckedSub(int16 a, int16 b)
{
	int32 result = (int32)a - b;
	if ((result > 0x7FFF) || (result < -0x8000))
		OpFailed();
	return (int16)result;
}

int32 BfIRBuilder::CheckedSub(int32 a, int32 b)
{
	int32 result = a - b;
	if (b >= 0)
	{
		if (result > a)
			OpFailed();
	}
	else if (result < a)
		OpFailed();
	return (uint32)result;
}

int64 BfIRBuilder::CheckedSub(int64 a, int64 b)
{
	int64 result = a - b;
	if (b >= 0)
	{
		if (result > a)
			OpFailed();
	}
	else if (result < a)
		OpFailed();
	return (uint64)result;
}

///

uint8 BfIRBuilder::CheckedMul(uint8 a, uint8 b)
{
	int result = (uint32)a * b;
	if (result & 0xFFFFFF00)
		OpFailed();
	return (uint8)result;
}

uint16 BfIRBuilder::CheckedMul(uint16 a, uint16 b)
{
	int result = (uint32)a * b;
	if (result & 0xFFFF0000)
		OpFailed();
	return (uint16)result;
}

uint32 BfIRBuilder::CheckedMul(uint32 a, uint32 b)
{
	uint64 result = (uint64)a * b;
	uint32 upper = (uint32)(result >> 32);
	if ((upper != 0) && (upper != 0xFFFFFFFF))
		OpFailed();
	return (uint32)result;
}

uint64 BfIRBuilder::CheckedMul(uint64 a, uint64 b)
{
	uint32 aHigh;
	uint32 aLow;
	uint32 bHigh;
	uint32 bLow;

	// a*b can be decomposed to
	//	(aHigh * bHigh * 2^64) + (aLow * bHigh * 2^32) + (aHigh * bLow * 2^32) + (aLow * bLow)

	aHigh = (uint32)(a >> 32);
	aLow  = (uint32)(a);
	bHigh = (uint32)(b >> 32);
	bLow  = (uint32)(b);

	uint64 ret = 0;

	if (aHigh == 0)
	{
		if (bHigh != 0)
			ret = (uint64)aLow * (uint64)bHigh;
	}
	else if (bHigh == 0)
	{
		if (aHigh != 0)
			ret = (uint64)aHigh * (uint64)bLow;
	}
	else
		OpFailed();

	if (ret != 0)
	{
		uint64 tmp;

		if((uint32)(ret >> 32) != 0)
			OpFailed();

		ret <<= 32;
		tmp = (uint64)aLow * (uint64)bLow;
		ret += tmp;

		if (ret < tmp)
			OpFailed();

		return ret;
	}

	return (uint64)aLow * (uint64)bLow;
}

int8 BfIRBuilder::CheckedMul(int8 a, int8 b)
{
	int32 result = (int32)a * b;
	if ((result > 0x7F) || (result < -0x80))
		OpFailed();
	return (int8)result;
}

int16 BfIRBuilder::CheckedMul(int16 a, int16 b)
{
	int result = a + b;
	if ((result > 0x7FFF) || (result < -0x8000))
		OpFailed();
	return (int16)result;
}

int32 BfIRBuilder::CheckedMul(int32 a, int32 b)
{
	int64 result = (int64)a * b;
	int32 upper = (int32)(result >> 32);
	if ((upper != 0) && (upper != 0xFFFFFFFF))
		OpFailed();
	return (int32)result;
}

int64 BfIRBuilder::CheckedMul(int64 a, int64 b)
{
	bool aNegative = false;
	int64 aAbs = a;
	if (aAbs < 0)
	{
		aNegative = true;
		aAbs = -aAbs;
	}

	int64 bAbs = b;
	bool bNegative = false;
	if (bAbs < 0)
	{
		bNegative = true;
		bAbs = -bAbs;
	}

	uint64 tmp = CheckedMul((uint64)aAbs, (uint64)bAbs);

	// Don't allow overflow into sign flag
	if (tmp & 0x8000000000000000LL)
		OpFailed();

	if (aNegative ^ bNegative)
		return -(int64)tmp;

	return (int64)tmp;
}

///

uint8 BfIRBuilder::CheckedShl(uint8 a, uint8 b)
{
	if ((a != 0) && (b >= 8))
		OpFailed();
	uint32 result = (uint32)a << b;
	if (result & 0xFFFFFF00)
		OpFailed();
	return (uint8)result;
}

uint16 BfIRBuilder::CheckedShl(uint16 a, uint16 b)
{
	if ((a != 0) && (b >= 16))
		OpFailed();
	uint32 result = (uint32)a << b;
	if (result & 0xFFFF0000)
		OpFailed();
	return (uint16)result;
}

uint32 BfIRBuilder::CheckedShl(uint32 a, uint32 b)
{
	if ((a != 0) && (b >= 32))
		OpFailed();
	uint32 result = a << b;
	if (result < a)
		OpFailed();
	return (uint32)result;
}

uint64 BfIRBuilder::CheckedShl(uint64 a, uint64 b)
{
	if ((a != 0) && (b >= 64))
		OpFailed();
	uint64 result = a << b;
	if (result < a)
		OpFailed();
	return (uint64)result;
}

int8 BfIRBuilder::CheckedShl(int8 a, int8 b)
{
	if ((a != 0) && (b >= 8))
		OpFailed();
	int32 result = (int32)a << b;
	if ((result > 0x7F) || (result < -0x80))
		OpFailed();
	return (int8)result;
}

int16 BfIRBuilder::CheckedShl(int16 a, int16 b)
{
	if ((a != 0) && (b >= 16))
		OpFailed();
	int32 result = (int32)a << b;
	if ((result > 0x7FFF) || (result < -0x8000))
		OpFailed();
	return (int16)result;
}

int32 BfIRBuilder::CheckedShl(int32 a, int32 b)
{
	if ((a != 0) && (b >= 32))
		OpFailed();
	int64 result = (int64)a << b;
	if ((result > 0x7FFFFFFFLL) || (result < -0x80000000LL))
		OpFailed();
	return (int32)result;
}

int64 BfIRBuilder::CheckedShl(int64 a, int64 b)
{
	if ((a != 0) && (b >= 64))
		OpFailed();
	int64 result = (int64)a << b;
	if (((a > 0) && (result < a)) ||
		((a < 0) && (result > a)))
		OpFailed();
	return result;
}

//////////////////////////////////////////////////////////////////////////

BfIRBuilder::BfIRBuilder(BfModule* module) : BfIRConstHolder(module)
{
	mBlockCount = 0;
	mModule = module;
	mHasDebugLoc = false;
	mHasDebugInfo = false;
	mHasDebugLineInfo = false;
	mIRCodeGen = NULL;
	mBeIRCodeGen = NULL;
	mBfIRCodeGen = NULL;
	mDbgVerifyCodeGen = false;

	mIgnoreWrites = false;
	mCurFakeId = -32;
	mOpFailed = false;

	mHasGlobalDefs = false;
	mNumFunctionsWithBodies = 0;
	mActiveFunctionHasBody = false;
	mHasStarted = false;
	mCmdCount = 0;
	mIsBeefBackend = false;
}

bool BfIRBuilder::HasExports()
{
	return mHasGlobalDefs || (mNumFunctionsWithBodies > 0);
}

BfIRBuilder::~BfIRBuilder()
{
	if (mIRCodeGen != NULL)
	{
		mIRCodeGen->mStream = NULL;
		delete mIRCodeGen;
	}
	BF_ASSERT(mSavedDebugLocs.size() == 0);
}

String BfIRBuilder::ToString(BfIRValue irValue)
{
	if ((irValue.mFlags & BfIRValueFlags_Const) != 0)
	{
		return BfIRConstHolder::ToString(irValue);
	}
	else if ((irValue.mFlags & BfIRValueFlags_Arg) != 0)
	{
		return StrFormat("Arg %d in %s", irValue.mId, ActiveFuncToString().c_str());
	}
	else if (irValue.mFlags != 0)
	{
		if (mBfIRCodeGen != NULL)
		{
			auto val = mBfIRCodeGen->GetLLVMValue(irValue.mId);
			std::string outStr;
			llvm::raw_string_ostream strStream(outStr);
			val->print(strStream);
			strStream << "\n Type: ";
			val->getType()->print(strStream);
			strStream.flush();
			return outStr;
		}
		else if (mBeIRCodeGen != NULL)
		{
			auto val = mBeIRCodeGen->GetBeValue(irValue.mId);
			String str;
			BeDumpContext dc;
			dc.ToString(str, val);

			auto type = val->GetType();
			if (type != NULL)
			{
				str += "\n";
				dc.ToString(str, type);
			}

			return str;
		}
		else
			return "Value???";
	}
	else
	{
		BF_ASSERT(irValue.mId == -1);
	}
	return "empty";
}

String BfIRBuilder::ToString(BfIRType irType)
{
	if (mBfIRCodeGen != NULL)
	{
		llvm::Type* llvmType = NULL;
		if (irType.mKind == BfIRTypeData::TypeKind_Stream)
		{
			llvmType = mBfIRCodeGen->GetLLVMType(irType.mId);
		}
		else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeCode)
		{
			bool isSigned = false;
			llvmType = mBfIRCodeGen->GetLLVMType((BfTypeCode)irType.mId, isSigned);
		}
		else
		{
			auto& typeEntry = mBfIRCodeGen->GetTypeEntry(irType.mId);
			if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeId)
				llvmType = typeEntry.mLLVMType;
			else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeInstId)
				llvmType = typeEntry.mInstLLVMType;
			else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeInstPtrId)
				llvmType = typeEntry.mInstLLVMType->getPointerTo();
		}

		if (llvmType == NULL)
			return "null";
		std::string outStr;
		llvm::raw_string_ostream strStream(outStr);
		llvmType->print(strStream);

		if (auto pointerType = llvm::dyn_cast<llvm::PointerType>(llvmType))
		{
			strStream << "\n ElementType: ";
			pointerType->getElementType()->print(strStream);
		}
		strStream.flush();
		return outStr;
	}
	else if (mBeIRCodeGen != NULL)
	{
		BeType* beType;
		if (irType.mKind == BfIRTypeData::TypeKind_Stream)
		{
			beType = mBeIRCodeGen->GetBeType(irType.mId);
		}
		else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeCode)
		{
			bool isSigned = false;
			beType = mBeIRCodeGen->GetBeType((BfTypeCode)irType.mId, isSigned);
		}
		else
		{
			auto& typeEntry = mBeIRCodeGen->GetTypeEntry(irType.mId);
			if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeId)
				beType = typeEntry.mBeType;
			else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeInstId)
				beType = typeEntry.mBeType;
			else if (irType.mKind == BfIRType::TypeKind::TypeKind_TypeInstPtrId)
				beType = mBeIRCodeGen->mBeContext->GetPointerTo(typeEntry.mInstBeType);
		}

		String str;
		BeDumpContext dc;
		dc.ToString(str, beType);
		return str;
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_TypeId)
	{
		return StrFormat("Type#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_TypeInstId)
	{
		return StrFormat("TypeInst#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_TypeInstPtrId)
	{
		return StrFormat("TypeInstPtr#%d:%s", irType.mId, mModule->TypeToString(mModule->mContext->mTypes[irType.mId]).c_str());
	}
	else if (irType.mKind == BfIRTypeData::TypeKind_SizedArray)
	{
		auto sizedArrayType = (BfConstantSizedArrayType*)GetConstantById(irType.mId);
		return StrFormat("%s[%d]", ToString(sizedArrayType->mType).c_str(), (int)sizedArrayType->mLength);
	}
	else
	{
		return "Type ???";
	}
}

String BfIRBuilder::ToString(BfIRFunction irFunc)
{
	if (mBfIRCodeGen != NULL)
	{
		auto val = mBfIRCodeGen->GetLLVMValue(irFunc.mId);
		if (val == NULL)
			return "null";
		std::string outStr;
		llvm::raw_string_ostream strStream(outStr);
		val->print(strStream);
		strStream.flush();
		return outStr;
	}
	else if (mBeIRCodeGen != NULL)
	{
		auto val = mBeIRCodeGen->GetBeValue(irFunc.mId);
		if (val == NULL)
			return "null";

		String str;
		BeDumpContext dc;
		dc.ToString(str, val);
		return str;
	}
	else
		return "???";
}

String BfIRBuilder::ToString(BfIRFunctionType irType)
{
	if (mBfIRCodeGen != NULL)
	{
		auto llvmType = mBfIRCodeGen->GetLLVMType(irType.mId);
		if (llvmType == NULL)
			return "null";
		std::string outStr;
		llvm::raw_string_ostream strStream(outStr);
		llvmType->print(strStream);
		strStream.flush();
		return outStr;
	}
	else
		return "???";
}

String BfIRBuilder::ToString(BfIRMDNode irMDNode)
{
	if (mBfIRCodeGen != NULL)
	{
		auto md = mBfIRCodeGen->GetLLVMMetadata(irMDNode.mId);
		if (md == NULL)
			return "null";
		std::string outStr;
		llvm::raw_string_ostream strStream(outStr);
		md->print(strStream);
		strStream.flush();
		return outStr;
	}
	else if (mBeIRCodeGen != NULL)
	{
		auto md = mBeIRCodeGen->GetBeMetadata(irMDNode.mId);
		if (md == NULL)
			return "null";
		String str;
		BeDumpContext dc;
		dc.ToString(str, md);
		return str;
	}
	else
		return "???";
}

String BfIRBuilder::ActiveFuncToString()
{
	if (mBfIRCodeGen != NULL)
	{
		std::string outStr;
		llvm::raw_string_ostream strStream(outStr);
		mBfIRCodeGen->mActiveFunction->print(strStream);
		return outStr;
	}
	else
		return "???";
}

void BfIRBuilder::PrintActiveFunc()
{
	pv(mActiveFunction);
}

void BfIRBuilder::pv(const BfIRValue& irValue)
{
	OutputDebugStrF("%s\n", ToString(irValue).c_str());
}

void BfIRBuilder::pt(const BfIRType& irType)
{
	OutputDebugStrF("%s\n", ToString(irType).c_str());
}

void pt(llvm::Type* t);

void BfIRBuilder::pbft(BfType* type)
{
	OutputDebugStrF("%s\n", mModule->TypeToString(type).c_str());
	//auto itr = mTypeMap.find(type);
	//if (itr == mTypeMap.end())
	BfIRPopulateType* populateType = NULL;
	if (!mTypeMap.TryGetValue(type, &populateType))
	{
		OutputDebugStrF("Not in mTypeMap\n");
		return;
	}
	OutputDebugStrF("mTypeMap DefState: %d\n", *populateType);

	if (mBfIRCodeGen != NULL)
	{
		auto llvmType = mBfIRCodeGen->GetLLVMTypeById(type->mTypeId);
		::pt(llvmType);
	}
}

void BfIRBuilder::pt(const BfIRFunction& irFunc)
{
	OutputDebugStrF("%s\n", ToString(irFunc).c_str());
}

void BfIRBuilder::pft(const BfIRFunctionType& irType)
{
	OutputDebugStrF("%s\n", ToString(irType).c_str());
}

void BfIRBuilder::pmd(const BfIRMDNode& irMDNode)
{
	OutputDebugStrF("%s\n", ToString(irMDNode).c_str());
}

void BfIRBuilder::GetBufferData(Array<uint8>& outBuffer)
{
	if (mStream.GetSize() == 0)
		return;

	outBuffer.ResizeRaw(mStream.GetSize());
	mStream.SetReadPos(0);
	mStream.Read(&outBuffer[0], mStream.GetSize());
}

void BfIRBuilder::ClearConstData()
{
	mTempAlloc.Clear();
	mConstMemMap.Clear();
	mFunctionMap.Clear();
	mGlobalVarMap.Clear();
	BF_ASSERT(mMethodTypeMap.GetCount() == 0);
	BF_ASSERT(mTypeMap.GetCount() == 0);
	BF_ASSERT(mDITemporaryTypes.size() == 0);
#ifdef BFIR_RENTRY_CHECK
	BF_ASSERT(mDeclReentrySet.size() == 0);
	BF_ASSERT(mDefReentrySet.size() == 0);
#endif
	BF_ASSERT(mStream.GetSize() == 0);
}

void BfIRBuilder::ClearNonConstData()
{
	mMethodTypeMap.Clear();
	mFunctionMap.Clear();
	mGlobalVarMap.Clear();
	mTypeMap.Clear();
	mConstMemMap.Clear();
	mDITemporaryTypes.Clear();
	mSavedDebugLocs.Clear();
	mDeferredDbgTypeDefs.Clear();
	mActiveFunction = BfIRFunction();
	mInsertBlock = BfIRValue();
}

void BfIRBuilder::Start(const StringImpl& moduleName, int ptrSize, bool isOptimized)
{
	mHasStarted = true;
	WriteCmd(BfIRCmd_Module_Start, moduleName, ptrSize, isOptimized);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetBackend(bool isBeefBackend)
{
	mIsBeefBackend = isBeefBackend;

	BF_ASSERT(mIRCodeGen == NULL);
	if (mDbgVerifyCodeGen)
	{
		if (isBeefBackend)
		{
			mBeIRCodeGen = new BeIRCodeGen();
			mBeIRCodeGen->mStream = &mStream;
			mBeIRCodeGen->mBfIRBuilder = this;
			mIRCodeGen = mBeIRCodeGen;
		}
		else
		{
			mBfIRCodeGen = new BfIRCodeGen();
			mBfIRCodeGen->mStream = &mStream;
			mBfIRCodeGen->mBfIRBuilder = this;
			mIRCodeGen = mBfIRCodeGen;
		}
		mIRCodeGen->SetConfigConst(BfIRConfigConst_VirtualMethodOfs, 0);
		mIRCodeGen->SetConfigConst(BfIRConfigConst_DynSlotOfs, 0);

		while (mStream.GetReadPos() < mStream.GetSize())
			mIRCodeGen->HandleNextCmd();
	}
}

void BfIRBuilder::RemoveIRCodeGen()
{
	if (mIRCodeGen != NULL)
	{
		mIRCodeGen->mStream = NULL;
		delete mIRCodeGen;
		mIRCodeGen = NULL;
	}
}

void BfIRBuilder::WriteIR(const StringImpl& fileName)
{
	WriteCmd(BfIRCmd_WriteIR, fileName);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Module_SetTargetTriple(const StringImpl& targetTriple, const StringImpl& targetCPU)
{
	WriteCmd(BfIRCmd_Module_SetTargetTriple, targetTriple, targetCPU);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Module_AddModuleFlag(const StringImpl& flag, int val)
{
	WriteCmd(BfIRCmd_Module_AddModuleFlag, flag, val);
	NEW_CMD_INSERTED;
}

#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

void BfIRBuilder::WriteSLEB128(int64 value)
{
	if ((value >= -0x40) && (value <= 0x3F))
	{
		mStream.Write(((uint8)value) & 0x7F);
		return;
	}

	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		mStream.Write(curByte);
	}
	while (hasMore);
}

void BfIRBuilder::WriteSLEB128(int32 value)
{
	if (value < 0)
	{
// 		if (value >= -0x40)
// 		{
// 			mStream.Write((uint8)value);
// 			return;
// 		}
//
// 		if (value >= -0x2000)
// 		{
// 			uint16 val =
// 				(((uint16)(value << 1)) & 0x7F00) |
// 				(((uint16)value) & 0x7F) | 0x80;
// 			mStream.Write_2(val);
// 			return;
// 		}
//
// 		if (value >= -0x100000)
// 		{
// 			uint32 val =
// 				(((uint32)(value << 2)) & 0x7F0000) |
// 				(((uint32)(value << 1)) & 0x7F00) |
// 				(((uint32)value) & 0x7F) | 0x8080;
// 			mStream.Write_3(val);
// 			return;
// 		}
//
// 		if (value >= -0x8000000)
// 		{
// 			uint32 val =
// 				(((uint32)(value << 3)) & 0x7F000000) |
// 				(((uint32)(value << 2)) & 0x7F0000) |
// 				(((uint32)(value << 1)) & 0x7F00) |
// 				(((uint32)value) & 0x7F) | 0x808080;
// 			mStream.Write_4(val);
// 			return;
// 		}
	}
	else
	{
		if (value <= 0x3F)
		{
			mStream.Write((uint8)value);
			return;
		}

		if (value <= 0x1FFF)
		{
			uint16 val =
				(((uint16)(value << 1)) & 0x7F00) |
				(((uint16)value) & 0x7F) | 0x80;
			mStream.Write_2(val);
			return;
		}
//
// 		if (value <= 0x0FFFFF)
// 		{
// 			uint32 val =
// 				(((uint32)(value << 2)) & 0x7F0000) |
// 				(((uint32)(value << 1)) & 0x7F00) |
// 				(((uint32)value) & 0x7F) | 0x8080;
// 			mStream.Write_3(val);
// 			return;
// 		}
//
// 		if (value <= 0x7FFFFF)
// 		{
// 			uint32 val =
// 				(((uint32)(value << 3)) & 0x7F000000) |
// 				(((uint32)(value << 2)) & 0x7F0000) |
// 				(((uint32)(value << 1)) & 0x7F00) |
// 				(((uint32)value) & 0x7F) | 0x808080;
// 			mStream.Write_4(val);
// 			return;
// 		}
	}

	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		mStream.Write(curByte);
	} while (hasMore);
}

void BfIRBuilder::Write(uint8 val)
{
	mStream.Write(val);
}

void BfIRBuilder::Write(bool val)
{
	mStream.Write(val ? 1 : 0);
}

void BfIRBuilder::Write(int intVal)
{
	WriteSLEB128(intVal);
}

void BfIRBuilder::Write(int64 intVal)
{
	WriteSLEB128(intVal);
}

void BfIRBuilder::Write(Val128 val)
{
	WriteSLEB128((int64)val.mLow);
	WriteSLEB128((int64)val.mHigh);
}

void BfIRBuilder::Write(const StringImpl&str)
{
	WriteSLEB128((int)str.length());
	mStream.Write(str.c_str(), (int)str.length());
}

void BfIRBuilder::Write(const BfIRValue& irValue)
{
	if ((irValue.mFlags & BfIRValueFlags_Const) != 0)
	{
		auto constant = GetConstantById(irValue.mId);

		mStream.Write(BfIRParamType_Const);
		mStream.Write((uint8)constant->mTypeCode);

		switch ((int)constant->mTypeCode)
		{
		case (int)BfTypeCode_Float:
			{
				float f = (float)constant->mDouble;
				mStream.Write(&f, sizeof(float));
			}
			break;
		case (int)BfTypeCode_Double:
			{
				mStream.Write(&constant->mDouble, sizeof(double));
			}
			break;
		case (int)BfTypeCode_Int8:
		case (int)BfTypeCode_UInt8:
		case (int)BfTypeCode_Int16:
		case (int)BfTypeCode_UInt16:
		case (int)BfTypeCode_Int32:
		case (int)BfTypeCode_UInt32:
		case (int)BfTypeCode_Int64:
		case (int)BfTypeCode_UInt64:
		case (int)BfTypeCode_IntPtr:
		case (int)BfTypeCode_UIntPtr:
		case (int)BfTypeCode_IntUnknown:
		case (int)BfTypeCode_UIntUnknown:
		case (int)BfTypeCode_Char8:
		case (int)BfTypeCode_Char16:
		case (int)BfTypeCode_Char32:
			{
				WriteSLEB128(constant->mInt64);
			}
			break;
		case (int)BfTypeCode_Boolean:
			{
				Write(constant->mBool);
			}
			break;
		case (int)BfTypeCode_NullPtr:
			{
				Write(constant->mIRType);
			}
			break;
		case (int)BfTypeCode_None:
			{
				// No param needed
			}
			break;
		case (int)BfConstType_GlobalVar:
			{
				auto gvConst = (BfGlobalVar*)constant;
				WriteSLEB128(gvConst->mStreamId);
				if (gvConst->mStreamId == -1)
				{
					int streamId = mCmdCount++;
					gvConst->mStreamId = streamId;

					Write(gvConst->mType);
					Write(gvConst->mIsConst);
					Write((uint8)gvConst->mLinkageType);
					Write(gvConst->mInitializer);
					Write(String(gvConst->mName));
					Write(gvConst->mIsTLS);
				}
			}
			break;
		case (int)BfConstType_BitCast:
		case (int)BfConstType_BitCastNull:
			{
				auto bitcast = (BfConstantBitCast*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, bitcast->mTarget);
				Write(targetConst);
				Write(bitcast->mToType);
			}
			break;
		case (int)BfConstType_GEP32_1:
			{
				auto gepConst = (BfConstantGEP32_1*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
				Write(targetConst);
				Write(gepConst->mIdx0);
			}
			break;
		case (int)BfConstType_GEP32_2:
			{
				auto gepConst = (BfConstantGEP32_2*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
				Write(targetConst);
				Write(gepConst->mIdx0);
				Write(gepConst->mIdx1);
			}
			break;
		case (int)BfConstType_ExtractValue:
			{
				auto gepConst = (BfConstantExtractValue*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, gepConst->mTarget);
				Write(targetConst);
				Write(gepConst->mIdx0);
			}
			break;
		case (int)BfConstType_PtrToInt:
			{
				auto ptrToIntConst = (BfConstantPtrToInt*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, ptrToIntConst->mTarget);
				Write(targetConst);
				Write(ptrToIntConst->mToTypeCode);
			}
			break;
		case (int)BfConstType_IntToPtr:
			{
				auto intToPtrConst = (BfConstantIntToPtr*)constant;
				BfIRValue targetConst(BfIRValueFlags_Const, intToPtrConst->mTarget);
				Write(targetConst);
				Write(intToPtrConst->mToType);
			}
			break;
		case (int)BfConstType_AggZero:
			{
				Write(constant->mIRType);
			}
			break;
		case (int)BfConstType_Agg:
			{
				auto arrayConst = (BfConstantAgg*)constant;
				Write(arrayConst->mType);
				Write(arrayConst->mValues);
			}
			break;
		case (int)BfConstType_ArrayZero8:
			{
				Write(constant->mInt64);
			}
			break;
		case (int)BfConstType_TypeOf:
			{
				auto typeofConst = (BfTypeOf_Const*)constant;
				Write(MapType(typeofConst->mType, BfIRPopulateType_Identity));
			}
			break;
		case (int)BfConstType_Undef:
			{
				auto undefConst = (BfConstantUndef*)constant;
				Write(undefConst->mType);
			}
			break;
		case (int)BfConstType_TypeOf_WithData:
			{
				auto typeofConst = (BfTypeOf_WithData_Const*)constant;
				Write(MapType(typeofConst->mType, BfIRPopulateType_Identity));
				Write(typeofConst->mTypeData);
			}
			break;
		default:
			{
				BF_FATAL("Unhandled");
			}
			break;
		}
	}
	else if ((irValue.mFlags & BfIRValueFlags_Arg) != 0)
	{
		mStream.Write(BfIRParamType_Arg);
		WriteSLEB128(irValue.mId);
	}
	else if (irValue.mFlags != 0)
	{
		BF_ASSERT(irValue.mId >= 0);

		if (irValue.mId < 0x100)
		{
			mStream.Write(BfIRParamType_StreamId_Abs8);
			mStream.Write(irValue.mId);
		}
		else
		{
			int offset = mCmdCount - irValue.mId;
			BF_ASSERT(offset > 0);

			if (offset <= BfIRParamType_StreamId_Back_LAST - BfIRParamType_StreamId_Back1)
			{
				mStream.Write(BfIRParamType_StreamId_Back1 + (offset - 1));
			}
			else
			{
				mStream.Write(BfIRParamType_StreamId_Rel);
				WriteSLEB128(offset);
			}
		}
	}
	else
	{
		BF_ASSERT(irValue.mId == -1);
		mStream.Write(BfIRParamType_None);
	}
}

void BfIRBuilder::Write(BfTypeCode typeCode)
{
	mStream.Write((uint8)typeCode);
}

void BfIRBuilder::Write(const BfIRTypeData& type)
{
	mStream.Write((uint8)type.mKind);
	if (type.mKind == BfIRTypeData::TypeKind_SizedArray)
	{
		auto sizedArrayType = (BfConstantSizedArrayType*)GetConstantById(type.mId);
		Write(sizedArrayType->mType);
		WriteSLEB128((int64)sizedArrayType->mLength);
	}
	else if (type.mKind != BfIRTypeData::TypeKind_None)
		WriteSLEB128(type.mId);
}

void BfIRBuilder::Write(BfIRFunctionType funcType)
{
	WriteSLEB128(funcType.mId);
}

void BfIRBuilder::Write(BfIRFunction func)
{
	//auto funcData = mFunctionVec[func.mId];
	//WriteSLEB128(bufPtr, funcData->mStreamId);
	WriteSLEB128(func.mId);
}

void BfIRBuilder::Write(BfIRBlock block)
{
	WriteSLEB128(block.mId);
}

void BfIRBuilder::Write(BfIRMDNode node)
{
	BF_ASSERT(node.mId >= -1);
	WriteSLEB128(node.mId);
}

BfIRValue BfIRBuilder::WriteCmd(BfIRCmd cmd)
{
	if (mIgnoreWrites)
		return GetFakeVal();
	mStream.Write((uint8)cmd);
	return BfIRValue(BfIRValueFlags_Value, mCmdCount++);
}

void BfIRBuilder::NewCmdInserted()
{
	BF_ASSERT(mIgnoreWrites || mHasStarted);
	if (mIgnoreWrites)
		return;
	if (mIRCodeGen == NULL)
		return;

	// This is for debug only - for checking the stream after each command. Useful when debugging IR generation errors
	BF_ASSERT(mStream.GetReadPos() < mStream.GetSize());
	mIRCodeGen->HandleNextCmd();
	BF_ASSERT(mStream.GetReadPos() == mStream.GetSize());
}

BfIRMDNode BfIRBuilder::CreateNamespaceScope(BfType* type, BfIRMDNode fileDIScope)
{
	BfIRMDNode curDIScope = fileDIScope;
	auto typeInstance = type->ToTypeInstance();

	if (mModule->mCompiler->mOptions.IsCodeView())
	{
		curDIScope = DbgCreateNameSpace(curDIScope, "_bf",
			fileDIScope, 0);
	}

	if (typeInstance != NULL)
	{
		auto typeDef = typeInstance->mTypeDef;

		if (!typeInstance->IsBoxed())
		{
			BfAtomCompositeT<16> curNamespace;
			if (typeInstance->IsArray())
			{
				auto arrayType = (BfArrayType*)typeInstance;
				auto innerTypeInst = arrayType->GetUnderlyingType()->ToTypeInstance();
				if (innerTypeInst != NULL)
					curNamespace = innerTypeInst->mTypeDef->mNamespace;
			}
			else
				curNamespace = typeDef->mNamespace;

			for (int partCount = 0; partCount < curNamespace.GetPartsCount(); partCount++)
			{
				curDIScope = DbgCreateNameSpace(curDIScope, curNamespace.mParts[partCount]->ToString(),
					fileDIScope, 0);
			}
		}
	}
	return curDIScope;
}

String BfIRBuilder::GetDebugTypeName(BfTypeInstance* typeInstance, bool includeOuterTypeName)
{
	BfTypeNameFlags typeNameFlags = BfTypeNameFlag_AddGlobalContainerName;
	if (!typeInstance->IsUnspecializedTypeVariation())
		typeNameFlags = (BfTypeNameFlags)(typeNameFlags | BfTypeNameFlag_ResolveGenericParamNames);

	String typeName;
	if (typeInstance->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)typeInstance;
		typeName = mModule->TypeToString(boxedType->mElementType, (BfTypeNameFlags)(typeNameFlags));
		if (boxedType->IsBoxedStructPtr())
			typeName += "*";
		typeName = "Box<" + typeName + ">";
	}
	else if (includeOuterTypeName)
	{
		typeName = mModule->TypeToString(typeInstance, (BfTypeNameFlags)(typeNameFlags | BfTypeNameFlag_OmitNamespace));
	}
	else
	{
		typeName = mModule->TypeToString(typeInstance, (BfTypeNameFlags)(typeNameFlags | BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType));
	}

	for (int i = 0; i < (int)typeName.length(); i++)
	{
		if (typeName[i] == '.')
		{
			typeName[i] = ':';
			typeName.Insert(i, ":");
		}
	}

	//DbgAddPrefix(typeName);
	return typeName;
}

#ifdef BFIR_RENTRY_CHECK
struct ReEntryCheck
{
public:
	std::set<BfType*>* mSet;
	BfType* mType;
public:
	ReEntryCheck(std::set<BfType*>* set, BfType* type)
	{
		mSet = set;
		mType = type;
		auto pair = mSet->insert(type);
		BF_ASSERT(pair.second);
	}

	~ReEntryCheck()
	{
		mSet->erase(mType);
	}
};
#endif

void BfIRBuilder::CreateTypeDeclaration(BfType* type, bool forceDbgDefine)
{
	auto populateModule = mModule->mContext->mUnreifiedModule;
	auto typeInstance = type->ToTypeInstance();
	if ((typeInstance != NULL) && (typeInstance->mModule != NULL))
		populateModule = typeInstance->mModule;

	bool wantDIData = DbgHasInfo() && (!type->IsUnspecializedType());

	// Types that don't have a proper 'defining module' need to be defined in every module they are used
	bool wantsDIForwardDecl = (type->GetModule() != mModule) && (!type->IsFunction());
	// Forward declarations of valuetypes don't work in LLVM backend for Win32.....
	//TODO: Why was this commented out?

	bool wantsDIPartialDef = false;
	if (wantsDIForwardDecl)
	{
		if ((!mIsBeefBackend) && (type->IsValueType()))
		{
			wantsDIPartialDef = true;
			wantsDIForwardDecl = false;
		}
	}
	if (mModule->mExtensionCount != 0)
		wantsDIForwardDecl = true;
	if (forceDbgDefine)
		wantsDIForwardDecl = false;

	bool isPrimEnum = (type->IsEnum()) && (type->IsTypedPrimitive());

	if (!type->IsDeclared())
		populateModule->PopulateType(type, BfPopulateType_Declaration);

	BF_ASSERT(type->IsDeclared());

#ifdef BFIR_RENTRY_CHECK
	ReEntryCheck reEntryCheck(&mDeclReentrySet, type);
#endif

	BfIRType irType;
	BfIRMDNode diType;
	bool trackDIType = false;

	BfType* underlyingArrayType = NULL;
	int underlyingArraySize = -1;
	bool underlyingArrayIsVector = false;
	if (typeInstance != NULL)
		typeInstance->GetUnderlyingArray(underlyingArrayType, underlyingArraySize, underlyingArrayIsVector);

	if (type->IsPointer())
	{
		BfPointerType* pointerType = (BfPointerType*)type;
		populateModule->PopulateType(pointerType->mElementType, BfPopulateType_Data);
		if (pointerType->mElementType->IsValuelessType())
		{
			irType = GetPrimitiveType(BfTypeCode_NullPtr);
		}
		else
		{
			irType = GetPointerTo(MapType(pointerType->mElementType));
		}

		if (wantDIData)
		{
			diType = DbgCreatePointerType(DbgGetType(pointerType->mElementType, BfIRPopulateType_Declaration));
			trackDIType = true;
		}
	}
	else if (type->IsRef())
	{
		BfRefType* refType = (BfRefType*)type;

		if (refType->mElementType->IsValuelessType())
			irType = GetPrimitiveType(BfTypeCode_NullPtr);
		else
		{
			//mModule->PopulateType(refType->mElementType, BfPopulateType_Declaration);
			irType = GetPointerTo(MapType(refType->mElementType));
		}

		if ((wantDIData) && (!type->IsUnspecializedType()))
		{
			diType = DbgCreateReferenceType(DbgGetType(refType->mElementType));
			trackDIType = true;
		}
	}
	else if ((type->IsGenericParam()) || (type->IsModifiedTypeType()))
	{
		//mModule->PopulateType(mModule->mContext->mBfObjectType, BfPopulateType_Declaration);
		irType = MapType(mModule->mContext->mBfObjectType);
		if (wantDIData)
			diType = DbgGetType(mModule->mContext->mBfObjectType);
	}
	else if (type->IsConcreteInterfaceType())
	{
		BfConcreteInterfaceType* concreteInterfaceType = (BfConcreteInterfaceType*)type;
		irType = MapType(concreteInterfaceType->mInterface);
		if (wantDIData)
		{
			diType = DbgGetType(concreteInterfaceType->mInterface);
		}
	}
	else if (type->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)type;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;

		String name = "_BF_MethodRef_";

		if (methodInstance != NULL)
			name += BfTypeUtils::HashEncode64(methodInstance->mIdHash).c_str();

		BF_ASSERT(methodInstance != NULL);

		if ((wantDIData) && (methodInstance != NULL))
		{
			auto typeDeclaration = methodInstance->GetOwner()->mTypeDef->mTypeDeclaration;

			BfFileInstance* bfFileInstance;
			if (typeDeclaration != NULL)
				bfFileInstance = mModule->GetFileFromNode(typeDeclaration);
			else
				bfFileInstance = mModule->GetFileFromNode(mModule->mContext->mBfObjectType->mTypeDef->mTypeDeclaration);

			auto namespaceScope = DbgCreateNameSpace(bfFileInstance->mDIFile, "_bf", bfFileInstance->mDIFile, 0);

			StringT<128> mangledName;
			BfMangler::Mangle(mangledName, mModule->mCompiler->GetMangleKind(), methodInstance);

			int captureSize = 0;
			int captureAlign = 0;
			BfIRMDNode derivedFrom;
			Array<BfIRMDNode> elements;

			diType = DbgCreateReplaceableCompositeType(llvm::dwarf::DW_TAG_structure_type, name, namespaceScope, bfFileInstance->mDIFile, 0, captureSize * 8, captureAlign * 8, 0);

			auto int64Type = mModule->GetPrimitiveType(BfTypeCode_Int64);
			auto memberType = DbgCreateMemberType(diType, mangledName, bfFileInstance->mDIFile, 0, 0, 0, -1, 0, DbgGetType(int64Type));
			elements.Add(memberType);

			int offset = 0;
			int implicitParamCount = methodInstance->GetImplicitParamCount();
			for (int paramIdx = methodInstance->HasThis() ? -1 : 0; paramIdx < implicitParamCount; paramIdx++)
			{
				BfType* paramType = methodInstance->GetParamType(paramIdx);
				offset = BF_ALIGN(offset, paramType->mAlign);
				String memberName = methodInstance->GetParamName(paramIdx);
				if (memberName == "this")
					memberName = "__this";
				auto diMember = DbgCreateMemberType(diType, memberName, bfFileInstance->mDIFile, 0, paramType->mSize * 8, paramType->mAlign * 8, offset * 8, 0, DbgGetType(paramType));
				elements.Add(diMember);
				offset += paramType->mSize;
			}
			offset = BF_ALIGN(offset, methodRefType->mAlign);

			BF_ASSERT(offset == methodRefType->mSize);

			DbgMakePermanent(diType, derivedFrom, elements);
		}

		Array<BfIRType> members;
		for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
		{
			BfType* paramType = methodRefType->GetCaptureType(dataIdx);
			if (paramType->IsValueType())
				PopulateType(paramType, BfIRPopulateType_Eventually_Full);
			members.Add(MapType(paramType));
		}

		irType = CreateStructType(name);
		StructSetBody(irType, members, type->mSize, type->mAlign, false);
	}
	else if (type->IsSizedArray())
	{
		BfSizedArrayType* arrayType = (BfSizedArrayType*)type;
		auto elementType = arrayType->mElementType;
		BfIRType elementIrType;

		if (elementType->IsValueType())
		{
			//mModule->PopulateType(arrayType->mElementType, BfPopulateType_Data);
			elementIrType = MapType(arrayType->mElementType, BfIRPopulateType_Eventually_Full);
		}
		else
		{
			//mModule->PopulateType(arrayType->mElementType, BfPopulateType_Declaration);
			elementIrType = MapType(arrayType->mElementType);
		}

		if (arrayType->mElementType->IsValuelessType())
			irType = elementIrType;
		else
			irType = GetSizedArrayType(MapType(arrayType->mElementType), BF_MAX(arrayType->mElementCount, 0));
// 		else if (arrayType->mElementType->IsSizeAligned())
// 			irType = GetSizedArrayType(MapType(arrayType->mElementType), BF_MAX(arrayType->mElementCount, 0));
// 		else
// 			irType = GetSizedArrayType(MapType(mModule->GetPrimitiveType(BfTypeCode_Int8)), BF_MAX(arrayType->mSize, 0));

		if (wantDIData)
			diType = DbgCreateArrayType((int64)arrayType->mSize * 8, arrayType->mAlign * 8, DbgGetType(arrayType->mElementType), arrayType->mElementCount);
	}
	else if (type->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)type;
		auto typeCode = primType->mTypeDef->mTypeCode;
		if ((typeCode == BfTypeCode_Var) || (typeCode == BfTypeCode_Let) || (typeCode == BfTypeCode_Self))
		{
			//mModule->PopulateType(mModule->mContext->mBfObjectType, BfPopulateType_Declaration);
			irType = MapType(mModule->mContext->mBfObjectType);
			if (wantDIData)
				diType = DbgGetType(mModule->mContext->mBfObjectType);
		}
		else if (typeCode == BfTypeCode_NullPtr)
		{
			irType = GetPrimitiveType(typeCode);

			if (wantDIData)
			{
				auto voidType = DbgGetType(mModule->GetPrimitiveType(BfTypeCode_None));
				diType = DbgCreatePointerType(voidType);
			}
		}
		else
		{
			irType = GetPrimitiveType(typeCode);
			if (wantDIData)
			{
				int dwarfType = 0;
				switch (typeCode)
				{
				case BfTypeCode_None:
					dwarfType = llvm::dwarf::DW_ATE_address;
					break;
				case BfTypeCode_Boolean:
					dwarfType = llvm::dwarf::DW_ATE_boolean;
					break;
				case BfTypeCode_Int8:
				case BfTypeCode_Int16:
				case BfTypeCode_Int32:
				case BfTypeCode_Int64:
				case BfTypeCode_IntPtr:
					dwarfType = llvm::dwarf::DW_ATE_signed;
					break;
				case BfTypeCode_UInt8:
				case BfTypeCode_UInt16:
				case BfTypeCode_UInt32:
				case BfTypeCode_UInt64:
				case BfTypeCode_UIntPtr:
					dwarfType = llvm::dwarf::DW_ATE_unsigned;
					break;
				case BfTypeCode_Char8:
				case BfTypeCode_Char16:
				case BfTypeCode_Char32:
					dwarfType = llvm::dwarf::DW_ATE_unsigned_char;
					break;
				case BfTypeCode_Float:
				case BfTypeCode_Double:
					dwarfType = llvm::dwarf::DW_ATE_float;
					break;
				default:
					BF_FATAL("Unhandled");
				}

				diType = DbgCreateBasicType(primType->mTypeDef->mName->ToString(), primType->mSize * 8, primType->mAlign * 8, dwarfType);
			}
		}
	}
	else if (type->IsTypeInstance())
	{
		auto typeDef = typeInstance->mTypeDef;

		BfIRMDNode diForwardDecl;
		if (wantDIData)
		{
			BfFileInstance* bfFileInstance;

			// Why did we bother setting the actual type declaration location?
			bfFileInstance = mModule->GetFileFromNode(mModule->mContext->mBfObjectType->mTypeDef->mTypeDeclaration);

			BfIRMDNode fileDIScope;
			if (bfFileInstance != NULL)
				fileDIScope = bfFileInstance->mDIFile;

			BfIRMDNode curDIScope;
			auto checkType = type;
			if (checkType->IsArray())
			{
				BfArrayType* arrayType = (BfArrayType*)checkType;
				checkType = arrayType->mGenericTypeInfo->mTypeGenericArguments[0];
			}
			BfTypeInstance* outerType = NULL;
			if (!checkType->IsBoxed())
				outerType = mModule->GetOuterType(checkType);
			if (outerType != NULL)
				curDIScope = DbgGetTypeInst(outerType);
			else if (checkType->IsNullable())
				curDIScope = CreateNamespaceScope(checkType->GetUnderlyingType(), fileDIScope);
			else
				curDIScope = CreateNamespaceScope(checkType, fileDIScope);
			String typeName = GetDebugTypeName(typeInstance, false);
			if (wantsDIForwardDecl)
			{
				if (type->IsInterface())
				{
					int flags = 0;
					diForwardDecl = DbgCreateReplaceableCompositeType(llvm::dwarf::DW_TAG_structure_type,
						typeName, curDIScope, fileDIScope, 0, (int64)0 * 8, (int64)0 * 8, flags);
					auto derivedFrom = DbgGetTypeInst(mModule->mContext->mBfObjectType);
					SizedArray<BfIRMDNode, 8> diFieldTypes;
					auto inheritanceType = DbgCreateInheritance(diForwardDecl, derivedFrom, 0, llvm::DINode::FlagPublic);
					diFieldTypes.push_back(inheritanceType);
					DbgMakePermanent(diForwardDecl, derivedFrom, diFieldTypes);
				}
				else
				{
					diForwardDecl = DbgCreateSizedForwardDecl(llvm::dwarf::DW_TAG_structure_type,
						typeName, curDIScope, fileDIScope, 0, (int64)BF_ALIGN(type->mSize, type->mAlign) * 8, (int64)type->mAlign * 8);
				}
			}
			else
			{
				if (wantsDIPartialDef)
					typeName += "$part$";

				// Will fill in later (during definition phase)
				int flags = 0;
				diForwardDecl = DbgCreateReplaceableCompositeType(llvm::dwarf::DW_TAG_structure_type,
					typeName, curDIScope, fileDIScope, 0, (int64)BF_ALIGN(typeInstance->mInstSize, typeInstance->mInstAlign) * 8, (int64)typeInstance->mInstAlign * 8, flags);

				mDITemporaryTypes.push_back(typeInstance);

				if (!type->IsUnspecializedType())
				{
					BF_ASSERT(!mDeferredDbgTypeDefs.Contains(type));
					mDeferredDbgTypeDefs.Add(type);
				}
			}

			DbgSetInstType(type, diForwardDecl);

			BfIRMDNode diType;
			if (type->IsValueType())
				diType = diForwardDecl;
			else
				diType = DbgCreatePointerType(diForwardDecl);
			DbgSetType(type, diType);
		}

		if (underlyingArraySize != -1)
		{
			if (underlyingArrayIsVector)
			{
				if (underlyingArrayType == mModule->GetPrimitiveType(BfTypeCode_Boolean))
					underlyingArrayType = mModule->GetPrimitiveType(BfTypeCode_UInt8);
				irType = GetVectorType(MapType(underlyingArrayType), underlyingArraySize);
			}
			else
				irType = GetSizedArrayType(MapType(underlyingArrayType), underlyingArraySize);
			SetType(type, irType);
		}
		else if (type->IsTypedPrimitive())
		{
			populateModule->PopulateType(type);
			auto underlyingType = type->GetUnderlyingType();
			irType = MapType(underlyingType);
			SetType(type, irType);
			SetInstType(type, irType);
		}
		else
		{
			String prefix = typeDef->mProject->mName + ".";
			StringT<128> mangledName;
			mangledName += prefix;
			BfMangler::Mangle(mangledName, mModule->mCompiler->GetMangleKind(), typeInstance, typeInstance->mModule);
			BfIRType irStructType = CreateStructType(mangledName);
			if (type->IsObjectOrInterface())
			{
				BfIRType ptrToStructType = GetPointerTo(irStructType);
				SetType(type, ptrToStructType);
				SetInstType(type, irStructType);
			}
			else
			{
				SetType(type, irStructType);
				SetInstType(type, irStructType);
			}
		}
		return;
	}

	if (irType)
		SetType(type, irType);
	if (diType)
	{
		DbgSetType(type, diType);
        //if (trackDIType)
            //DbgTrackDITypes(type);
	}
}

void BfIRBuilder::CreateDbgTypeDefinition(BfType* type)
{
	BP_ZONE("BfIRBuilder::CreateDbgTypeDefinition");

	auto typeInstance = type->ToTypeInstance();

	bool isPrimEnum = (type->IsEnum()) && (type->IsTypedPrimitive());
	auto typeDef = typeInstance->mTypeDef;
	bool wantDIData = true;

	BfModuleOptions moduleOptions = mModule->GetModuleOptions();
	bool isOptimized = (moduleOptions.mOptLevel != BfOptLevel_O0) && (moduleOptions.mOptLevel != BfOptLevel_OgPlus);

	BfIRMDNode fileDIScope;
	if (wantDIData)
	{
		BfFileInstance* bfFileInstance;
		if (typeDef->mTypeDeclaration != NULL)
			bfFileInstance = mModule->GetFileFromNode(typeDef->mTypeDeclaration);
		else
			bfFileInstance = mModule->GetFileFromNode(mModule->mCompiler->mBfObjectTypeDef->mTypeDeclaration);
		if (bfFileInstance != NULL)
			fileDIScope = bfFileInstance->mDIFile;
	}

#ifdef BFIR_RENTRY_CHECK
	ReEntryCheck reEntryCheck(&mDefReentrySet, type);
#endif

	//BF_ASSERT(WantsDbgDefinition(type));

	SizedArray<BfIRMDNode, 256> diFieldTypes;

	int packing = 0;
	bool isUnion = false;
	bool isCRepr = false;
	BfType* underlyingArrayType = NULL;
	int underlyingArraySize = -1;
	bool underlyingArrayIsVector = false;

	if (typeInstance->IsBoxed())
	{
	}
	else
	{
		isCRepr = typeInstance->mIsCRepr;
		packing = typeInstance->mPacking;
		isUnion = typeInstance->mIsUnion;
		typeInstance->GetUnderlyingArray(underlyingArrayType, underlyingArraySize, underlyingArrayIsVector);
// 		if (underlyingArrayType != NULL)
// 			return; // Done
	}

	String typeName = GetDebugTypeName(typeInstance, false);

	bool isGlobalContainer = typeDef->IsGlobalsContainer();

	bool isDefiningModule = ((type->GetModule() == mModule) || (type->IsFunction()));
	auto diForwardDecl = DbgGetTypeInst(typeInstance);

	BfSizedVector<BfFieldInstance*, 8> orderedFields;

	if ((type->IsUnion()) && (!type->IsEnum()))
	{
		int lineNum = 0;
		int flags = llvm::DINode::FlagPublic;
		auto fieldType = typeInstance->GetUnionInnerType();
		auto resolvedFieldDIType = DbgGetType(fieldType);
		String fieldName = "$bfunion";
		auto memberType = DbgCreateMemberType(diForwardDecl, fieldName, fileDIScope, lineNum,
			fieldType->mSize * 8, fieldType->mAlign * 8, typeInstance->mBaseType->mInstSize * 8,
			flags, resolvedFieldDIType);
		diFieldTypes.push_back(memberType);
	}

	bool isPayloadEnum = (typeInstance->IsEnum()) && (!typeInstance->IsTypedPrimitive());
	for (int fieldIdx = 0; fieldIdx < typeInstance->mFieldInstances.mSize; fieldIdx++)
	{
		auto fieldInstance = &typeInstance->mFieldInstances[fieldIdx];
		if (!fieldInstance->mFieldIncluded)
			continue;
		auto fieldDef = fieldInstance->GetFieldDef();

		if ((fieldInstance->mResolvedType == NULL) || (typeInstance->IsBoxed()))
			continue;

		auto resolvedFieldType = fieldInstance->GetResolvedType();
		mModule->PopulateType(resolvedFieldType, BfPopulateType_Declaration);
		BfIRType resolvedFieldIRType = MapType(resolvedFieldType);
		BfIRMDNode resolvedFieldDIType;

		if ((fieldDef != NULL) && (!fieldDef->mIsStatic) && (resolvedFieldType->IsStruct()))
			PopulateType(resolvedFieldType, BfIRPopulateType_Eventually_Full);
		resolvedFieldDIType = DbgGetType(resolvedFieldType);

		if (fieldInstance->IsAppendedObject())
			resolvedFieldDIType = DbgGetTypeInst(resolvedFieldType->ToTypeInstance());

		if ((fieldDef == NULL) && (typeInstance->IsPayloadEnum()))
		{
			orderedFields.push_back(fieldInstance);

			int lineNum = 0;
			int flags = llvm::DINode::FlagPublic;
			auto fieldType = fieldInstance->mResolvedType;
			String fieldName = "__bftag";
			auto memberType = DbgCreateMemberType(diForwardDecl, fieldName, fileDIScope, lineNum,
				resolvedFieldType->mSize * 8, resolvedFieldType->mAlign * 8, fieldInstance->mDataOffset * 8,
				flags, resolvedFieldDIType);
			diFieldTypes.push_back(memberType);
		}

		if ((!typeInstance->IsBoxed()) && (fieldDef != NULL))
		{
			if (fieldDef->mIsConst)
			{
				if (isDefiningModule)
				{
					if ((isPayloadEnum) && (fieldDef->IsEnumCaseEntry()))
					{
						auto payloadType = fieldInstance->mResolvedType;
						if (payloadType == NULL)
							payloadType = mModule->CreateTupleType(BfTypeVector(), Array<String>());

						String fieldName = StrFormat("_%d_%s", -fieldInstance->mDataIdx - 1, fieldDef->mName.c_str());

						int flags = 0;
						auto memberType = DbgCreateMemberType(diForwardDecl, fieldName, fileDIScope, 0,
							BF_ALIGN(payloadType->mSize, payloadType->mAlign) * 8, payloadType->mAlign * 8, 0,
							flags, DbgGetType(payloadType));
						diFieldTypes.push_back(memberType);
					}
					else
					{
						BfConstant* constant = NULL;
						BfIRValue staticValue;

						if (fieldInstance->mConstIdx != -1)
						{
							constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
							staticValue = mModule->ConstantToCurrent(constant, typeInstance->mConstHolder, resolvedFieldType);
						}

						if (fieldInstance->mResolvedType->IsComposite())
							PopulateType(fieldInstance->mResolvedType);

						BfIRMDNode constDIType = DbgCreateConstType(resolvedFieldDIType);
						if ((fieldDef->mIsExtern) && (resolvedFieldType->IsPointer()))
						{
							auto underlyingType = resolvedFieldType->GetUnderlyingType();
							auto staticTypedValue = mModule->ReferenceStaticField(fieldInstance);
							staticValue = staticTypedValue.mValue;

							int flags = 0;
							auto memberType = DbgCreateStaticMemberType(diForwardDecl, fieldDef->mName, fileDIScope, 0,
								constDIType, flags, CreateConst(BfTypeCode_Int32, 0));
							diFieldTypes.push_back(memberType);

							StringT<128> staticVarName;
							BfMangler::Mangle(staticVarName, mModule->mCompiler->GetMangleKind(), fieldInstance);
							if (!staticVarName.StartsWith("#"))
							{
								String fieldName = DbgGetStaticFieldName(fieldInstance);
								DbgCreateGlobalVariable(diForwardDecl, fieldName, staticVarName, fileDIScope, 0,
									constDIType, false, staticValue, memberType);
							}
						}
						else if (resolvedFieldType->IsValuelessType())
						{
							// Do nothing
						}
						else if ((resolvedFieldType->IsObjectOrInterface()) || (resolvedFieldType->IsPointer()) || (resolvedFieldType->IsSizedArray()))
						{
							bool useIntConstant = false;

							bool wasMadeAddr = false;

							StringT<128> staticVarName;
							BfMangler::Mangle(staticVarName, mModule->mCompiler->GetMangleKind(), fieldInstance);

							String fieldName = fieldDef->mName;
							BfIRValue intConstant;
							if (constant != NULL)
							{
								// This constant debug info causes linking errors on Linux
								if (isOptimized)
									continue;

								if ((constant->mConstType == BfConstType_Agg) ||
									(constant->mConstType == BfConstType_AggZero) ||
									(constant->mTypeCode == BfTypeCode_NullPtr))
								{
									staticValue = ConstToMemory(staticValue);
									wasMadeAddr = true;
								}
								else if (constant->mTypeCode == BfTypeCode_StringId)
								{
									int stringId = constant->mInt32;
									const StringImpl& str = mModule->mContext->mStringObjectIdMap[stringId].mString;
									if (resolvedFieldType->IsPointer())
										staticValue = mModule->GetStringCharPtr(str);
									else
										staticValue = mModule->GetStringObjectValue(str);
								}
								else
								{
									// Ignore other types (for now)
									continue;
								}
							}

							if (!useIntConstant)
							{
								auto useType = resolvedFieldType;
								if (wasMadeAddr)
									useType = mModule->CreatePointerType(useType);
								staticValue = CreateGlobalVariable(mModule->mBfIRBuilder->MapType(useType), true, BfIRLinkageType_Internal, staticValue, staticVarName);
								GlobalVar_SetAlignment(staticValue, useType->mAlign);
							}

							int flags = 0;
							auto memberType = DbgCreateStaticMemberType(diForwardDecl, fieldName, fileDIScope, 0,
								constDIType, flags, useIntConstant ? intConstant : BfIRValue());
							diFieldTypes.push_back(memberType);

							if (fieldDef->mUsingProtection != BfProtection_Hidden)
							{
								auto memberType = DbgCreateStaticMemberType(diForwardDecl, "$using$" + fieldName, fileDIScope, 0,
									constDIType, flags, useIntConstant ? intConstant : BfIRValue());
								diFieldTypes.push_back(memberType);
							}

							if (staticValue)
							{
								String qualifiedName = DbgGetStaticFieldName(fieldInstance);
								DbgCreateGlobalVariable(diForwardDecl, qualifiedName, staticVarName, fileDIScope, 0,
									constDIType, false, staticValue, memberType);
							}
						}
						else if (mModule->mCompiler->mOptions.IsCodeView())
						{
							int flags = 0;
							String fieldName = fieldDef->mName;
							if ((constant != NULL) &&
								((IsIntable(constant->mTypeCode)) || (IsFloat(constant->mTypeCode))))
							{
								int64 writeVal = constant->mInt64;
								if (constant->mTypeCode == BfTypeCode_Float)
								{
									// We need to do this because Singles are stored in mDouble, so we need to reduce here
									float floatVal = (float)constant->mDouble;
									writeVal = *(uint32*)&floatVal;
								}
								if (writeVal < 0)
									fieldName += StrFormat("$_%llu", -writeVal);
								else
									fieldName += StrFormat("$%llu", writeVal);
							}
							auto memberType = DbgCreateStaticMemberType(diForwardDecl, fieldName, fileDIScope, 0,
								constDIType, flags, staticValue);
							diFieldTypes.push_back(memberType);
						}
						else
						{
							int flags = 0;
							String fieldName = DbgGetStaticFieldName(fieldInstance);
							auto memberType = DbgCreateStaticMemberType(diForwardDecl, fieldName, fileDIScope, 0,
								constDIType, flags, staticValue);
							diFieldTypes.push_back(memberType);
						}
					}
				}
			}
			else if (fieldDef->mIsStatic)
			{
				if (isDefiningModule)
				{
					int flags = 0;
					auto memberType = DbgCreateStaticMemberType(diForwardDecl, fieldDef->mName, fileDIScope, 0,
						resolvedFieldDIType, flags, BfIRValue());
					diFieldTypes.push_back(memberType);

					if (fieldDef->mUsingProtection != BfProtection_Hidden)
					{
						auto memberType = DbgCreateStaticMemberType(diForwardDecl, "$using$" + fieldDef->mName, fileDIScope, 0,
							resolvedFieldDIType, flags, BfIRValue());
						diFieldTypes.push_back(memberType);
					}

					StringT<128> staticVarName;
					BfMangler::Mangle(staticVarName, mModule->mCompiler->GetMangleKind(), fieldInstance);
					if (!staticVarName.StartsWith('#'))
					{
						auto staticValue = mModule->ReferenceStaticField(fieldInstance);
						if (staticValue.mValue)
						{
							String fieldName = DbgGetStaticFieldName(fieldInstance);
							DbgCreateGlobalVariable(diForwardDecl, fieldName, staticVarName, fileDIScope, 0,
								resolvedFieldDIType, false, staticValue.mValue, memberType);
						}
					}
				}
			}
			else
			{
				bool useForUnion = false;

				BF_ASSERT(!fieldInstance->mIsEnumPayloadCase);

				if (wantDIData)
				{
					int lineNum = 0;

					int flags = 0;
					String fieldName = fieldDef->mName;
					if (fieldDef->mHasMultiDefs)
						fieldName += "$" + fieldDef->mDeclaringType->mProject->mName;
					auto memberType = DbgCreateMemberType(diForwardDecl, fieldName, fileDIScope, lineNum,
						fieldInstance->mDataSize * 8, resolvedFieldType->mAlign * 8, fieldInstance->mDataOffset * 8,
						flags, resolvedFieldDIType);
					diFieldTypes.push_back(memberType);

					if (fieldDef->mUsingProtection != BfProtection_Hidden)
					{
						auto memberType = DbgCreateMemberType(diForwardDecl, "$using$" + fieldName, fileDIScope, lineNum,
							fieldInstance->mDataSize * 8, resolvedFieldType->mAlign * 8, fieldInstance->mDataOffset * 8,
							flags, resolvedFieldDIType);
						diFieldTypes.push_back(memberType);
					}
				}
			}
		}
	}

	int dataPos = 0;
	if (typeInstance->mBaseType != NULL)
		dataPos = typeInstance->mBaseType->mInstSize;

	int unionSize = 0;
	if (typeInstance->mIsUnion)
	{
		auto unionInnerType = typeInstance->GetUnionInnerType();
		unionSize = unionInnerType->mSize;
		dataPos += unionSize;
	}

	bool wantsMethods = true;

	// We can't directly call boxed methods from the debugger
	if (type->IsBoxed())
		wantsMethods = false;

	if (!isDefiningModule)
		wantsMethods = false;

	if (wantsMethods)
	{
		for (int methodIdx = 0; methodIdx < (int)typeInstance->mMethodInstanceGroups.size(); methodIdx++)
		{
			auto& methodGroup = typeInstance->mMethodInstanceGroups[methodIdx];
			auto methodInstance = methodGroup.mDefault;

			// We're only adding non-generic methods at the moment
			if ((methodInstance == NULL) || (methodInstance->mIsUnspecialized))
				continue;

			if (type->IsGenericTypeInstance())
			{
				// We don't force method declarations for generic type instances, so don't add any unreified methods here
				//  for the purpose of keeping debug type info the same between rebuilds
				if (!methodInstance->mIsReified)
					continue;
			}

			auto methodDef = methodInstance->mMethodDef;
			if ((methodDef->mMethodType == BfMethodType_Operator) || (methodDef->mMethodType == BfMethodType_Mixin) || (methodDef->mMethodType == BfMethodType_Ignore))
				continue;

			if (methodDef->mIsOverride)
				continue;

			BfIRMDNode diFuncType = DbgCreateSubroutineType(methodInstance);

			int defLine = 0;

			StringT<128> mangledName;
			int flags = 0;
			BfMangler::Mangle(mangledName, mModule->mCompiler->GetMangleKind(), methodInstance);

			BfIRMDNode funcScope = diForwardDecl;

			if (methodDef->mProtection == BfProtection_Public)
				flags = llvm::DINode::FlagPublic;
			else if (methodDef->mProtection == BfProtection_Protected)
				flags = llvm::DINode::FlagProtected;
			else
				flags = llvm::DINode::FlagPrivate;
			if (methodDef->mIsOverride)
			{
				//flags |= llvm::DINode::FlagVirtual;
			}
			else if (methodDef->mIsVirtual)
				flags |= llvm::DINode::FlagIntroducedVirtual;
			if ((methodDef->mIsStatic) || (methodDef->mMethodType == BfMethodType_Mixin))
				flags |= llvm::DINode::FlagStaticMember;
			else
			{
				if (typeInstance->IsValuelessType())
					flags |= llvm::DINode::FlagStaticMember;
			}
			flags |= llvm::DINode::FlagPrototyped;
			if (methodDef->mMethodType == BfMethodType_Ctor)
			{
				flags |= llvm::DINode::FlagArtificial;
			}

			String methodName = methodDef->mName;
			SizedArray<BfIRMDNode, 1> genericArgs;
			SizedArray<BfIRValue, 1> genericConstValueArgs;
			auto diFunction = DbgCreateMethod(funcScope, methodName, mangledName, fileDIScope,
				defLine + 1, diFuncType, false, false,
				(methodInstance->mVirtualTableIdx != -1) ? 1 : 0,
				(methodInstance->mVirtualTableIdx != -1) ? methodInstance->DbgGetVirtualMethodNum() : 0,
				funcScope, flags, /*IsOptimized()*/false, BfIRValue(), genericArgs, genericConstValueArgs);
			diFieldTypes.push_back(diFunction);

			if (methodInstance->mVirtualTableIdx != -1)
			{
				BF_ASSERT(mModule->mCompiler->mMaxInterfaceSlots != -1);
			}
		}
	}

	BfType* declaredBaseType = typeInstance->mBaseType;
	if (typeInstance->IsTypedPrimitive())
		declaredBaseType = typeInstance->GetUnderlyingType();

	/*if (isPrimEnum)
	{
		// Handled below
	}
	else*/ if (type->IsBoxed())
	{
		auto underlyingType = ((BfBoxedType*)type)->GetModifiedElementType();
		if (!underlyingType->IsValuelessType())
		{
			auto fieldInstance = &typeInstance->mFieldInstances.back();
			auto resolvedFieldType = fieldInstance->GetResolvedType();

			auto inheritanceType = DbgCreateInheritance(diForwardDecl, DbgGetTypeInst(mModule->mContext->mBfObjectType), 0, llvm::DINode::FlagPublic);
			diFieldTypes.push_back(inheritanceType);

			int lineNum = 0;
			int flags = llvm::DINode::FlagPublic;
			auto memberType = DbgCreateMemberType(fileDIScope, "val", fileDIScope, lineNum,
				resolvedFieldType->mSize * 8, resolvedFieldType->mAlign * 8, fieldInstance->mDataOffset * 8,
				flags, DbgGetType(underlyingType));
			diFieldTypes.push_back(memberType);
		}
	}
	else
	{
		auto baseType = typeInstance->mBaseType;

		if (baseType != NULL)
		{
			auto baseTypeInst = baseType->ToTypeInstance();
			BfIRMDNode inheritanceType;
			if (baseTypeInst != NULL)
				inheritanceType = DbgCreateInheritance(diForwardDecl, DbgGetTypeInst(baseTypeInst), 0, llvm::DINode::FlagPublic);
			else
				inheritanceType = DbgCreateInheritance(diForwardDecl, DbgGetType(baseType), 0, llvm::DINode::FlagPublic);
			diFieldTypes.push_back(inheritanceType);
		}

		// Typed primitives are expressed as multiple inheritance
		if ((typeInstance->IsTypedPrimitive()) && (typeInstance->mBaseType != NULL) && (!typeInstance->mBaseType->IsTypedPrimitive()))
		{
 			auto underlyingType = typeInstance->GetUnderlyingType();
// 			auto inheritanceType = DbgCreateInheritance(diForwardDecl, DbgGetType(underlyingType), 0, llvm::DINode::FlagPublic);
// 			diFieldTypes.push_back(inheritanceType);

			int lineNum = 0;
			int flags = llvm::DINode::FlagPublic;
			auto memberType = DbgCreateMemberType(fileDIScope, "$prim", fileDIScope, lineNum,
				underlyingType->mSize * 8, underlyingType->mAlign * 8, 0,
				flags, DbgGetType(underlyingType));
			diFieldTypes.push_back(memberType);
		}
	}

	BfIRMDNode derivedFrom;

	if (typeInstance->IsInterface())
	{
		derivedFrom = DbgGetTypeInst(mModule->mContext->mBfObjectType);
		auto inheritanceType = DbgCreateInheritance(diForwardDecl, derivedFrom, 0, llvm::DINode::FlagPublic);
		diFieldTypes.push_back(inheritanceType);
	}
	else if (typeInstance->mBaseType != NULL)
	{
		derivedFrom = DbgGetTypeInst(typeInstance->mBaseType);
	}

	BfIRMDNode curDIScope;
	BfTypeInstance* outerType = NULL;
	if (!type->IsBoxed())
		outerType = mModule->GetOuterType(type);
	if (outerType != NULL)
		curDIScope = DbgGetTypeInst(outerType);
	else if (type->IsNullable())
		curDIScope = CreateNamespaceScope(type->GetUnderlyingType(), fileDIScope);
	else
		curDIScope = CreateNamespaceScope(type, fileDIScope);

	//TODO: Set DI Flags
	int diFlags = 0;

	BfIRMDNode diType;
	/*if ((typeInstance->IsEnum()) && (typeInstance->IsTypedPrimitive()))
	{
		llvm::SmallVector<BfIRMDNode, 8> diEnumValues;

		for (auto& fieldInst : typeInstance->mFieldInstances)
		{
			if (!fieldInst.mFieldIncluded)
				continue;
			auto fieldDef = fieldInst.GetFieldDef();
			if ((fieldInst.mConstIdx != -1) && (fieldDef->IsEnumCaseEntry()))
			{
				auto constant = typeInstance->mConstHolder->GetConstantById(fieldInst.mConstIdx);
				auto enumValue = DbgCreateEnumerator(fieldInst.GetFieldDef()->mName, constant->mInt64);
				diEnumValues.push_back(enumValue);
			}
		}

		diType = DbgMakePermanent(diForwardDecl, DbgGetType(declaredBaseType), diEnumValues);

		DbgSetType(type, diType);
		DbgSetInstType(type, diType);
	}
	else*/
	{
		BfIRMDNode diCompositeType = DbgMakePermanent(diForwardDecl, BfIRMDNode(), diFieldTypes);
		diType = diCompositeType;
	}
}

bool BfIRBuilder::WantsDbgDefinition(BfType* type)
{
	if ((type->GetModule() == mModule) || (type->IsFunction()))
		return true;

	// Forward declarations of valuetypes doesn't work in LLVM backend
// 	if ((!mIsBeefBackend) && (type->IsValueType()))
// 		return true;

	return false;
}

void BfIRBuilder::CreateTypeDefinition_Data(BfModule* populateModule, BfTypeInstance* typeInstance, bool forceDbgDefine)
{
	auto typeDef = typeInstance->mTypeDef;

#ifdef BFIR_RENTRY_CHECK
	ReEntryCheck reEntryCheck(&mDefReentrySet, type);
#endif

	bool isGlobalContainer = typeDef->IsGlobalsContainer();

	auto diForwardDecl = DbgGetTypeInst(typeInstance);
	SizedArray<BfIRType, 256> irFieldTypes;
	if ((!typeInstance->IsTypedPrimitive()) && (typeInstance->mBaseType != NULL))
	{
		irFieldTypes.push_back(MapTypeInst(typeInstance->mBaseType, BfIRPopulateType_Eventually_Full));
	}

	SizedArray<BfIRMDNode, 256> diFieldTypes;

	int packing = 0;
	bool isUnion = false;
	bool isCRepr = false;

	if (typeInstance->IsBoxed())
	{
		auto boxedType = (BfBoxedType*)typeInstance;
		BF_ASSERT(!boxedType->mFieldInstances.IsEmpty());

		auto& fieldInst = boxedType->mFieldInstances.back();
		auto elementType = fieldInst.mResolvedType;
		populateModule->PopulateType(elementType, BfPopulateType_Data);

		if (!elementType->IsValuelessType())
		{
			irFieldTypes.Add(MapType(elementType, elementType->IsValueType() ? BfIRPopulateType_Eventually_Full : BfIRPopulateType_Declaration));
		}
	}
	else
	{
		isCRepr = typeInstance->mIsCRepr;
		packing = typeInstance->mPacking;
		isUnion = typeInstance->mIsUnion;
	}

	BfSizedVector<BfFieldInstance*, 8> orderedFields;

	bool isPayloadEnum = (typeInstance->IsEnum()) && (!typeInstance->IsTypedPrimitive());
	for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
	{
		auto fieldInstance = &fieldInstanceRef;
		if (!fieldInstance->mFieldIncluded)
			continue;
		auto fieldDef = fieldInstance->GetFieldDef();

		if ((fieldInstance->mResolvedType == NULL) || (typeInstance->IsBoxed()))
			continue;

		if ((fieldDef != NULL) && (fieldDef->mIsStatic))
			continue; // Just create type first - we may contain a static reference to ourselves, creating an dependency loop

		auto resolvedFieldType = fieldInstance->GetResolvedType();

		mModule->PopulateType(resolvedFieldType, BfPopulateType_Declaration);
		BfIRType resolvedFieldIRType = MapType(resolvedFieldType);
		BfIRMDNode resolvedFieldDIType;

		if ((fieldDef != NULL) && (resolvedFieldType->IsStruct()))
			PopulateType(resolvedFieldType, BfIRPopulateType_Eventually_Full);

		if (fieldInstance->IsAppendedObject())
			PopulateType(resolvedFieldType, BfIRPopulateType_Eventually_Full);

		if ((fieldDef == NULL) && (typeInstance->IsPayloadEnum()))
		{
			orderedFields.push_back(fieldInstance);
		}

		if ((!typeInstance->IsBoxed()) && (fieldDef != NULL))
		{
			bool useForUnion = false;

			BF_ASSERT(!fieldInstance->mIsEnumPayloadCase);

			if ((!fieldDef->mIsStatic) && (!resolvedFieldType->IsValuelessType()))
			{
				if (!isUnion)
				{
					BF_ASSERT(resolvedFieldIRType.mId != -1);
					//irFieldTypes.push_back(resolvedFieldIRType);

					if (fieldInstance->mDataIdx != -1)
					{
						while (fieldInstance->mDataIdx >= (int)orderedFields.size())
							orderedFields.push_back(NULL);
						orderedFields[fieldInstance->mDataIdx] = fieldInstance;
					}
				}
			}
		}
	}

	int dataPos = 0;
	if (typeInstance->mBaseType != NULL)
		dataPos = typeInstance->mBaseType->mInstSize;

	int unionSize = 0;
	if (typeInstance->mIsUnion)
	{
		auto unionInnerType = typeInstance->GetUnionInnerType();
		irFieldTypes.push_back(MapType(unionInnerType, unionInnerType->IsValueType() ? BfIRPopulateType_Eventually_Full : BfIRPopulateType_Declaration));
		unionSize = unionInnerType->mSize;
		dataPos += unionSize;
	}

	for (int fieldIdx = 0; fieldIdx < (int)orderedFields.size(); fieldIdx++)
	{
		auto fieldInstance = orderedFields[fieldIdx];
		if (fieldInstance == NULL)
			continue;

		auto fieldDef = fieldInstance->GetFieldDef();

		auto resolvedFieldType = fieldInstance->GetResolvedType();

		BfIRType resolvedFieldIRType = MapType(resolvedFieldType);

		if (fieldInstance->IsAppendedObject())
		{
			auto fieldTypeInst = fieldInstance->mResolvedType->ToTypeInstance();

			if (fieldInstance->mDataSize > fieldTypeInst->mInstSize)
			{
				SizedArray<BfIRType, 2> types;
				types.push_back(MapTypeInst(fieldTypeInst));
				types.push_back(GetSizedArrayType(GetPrimitiveType(BfTypeCode_Int8), fieldInstance->mDataSize - fieldTypeInst->mInstSize));
				resolvedFieldIRType = CreateStructType(types);
			}
			else
				resolvedFieldIRType = MapTypeInst(fieldTypeInst);
		}

		if (fieldInstance->mDataOffset > dataPos)
		{
			int fillSize = fieldInstance->mDataOffset - dataPos;
			auto byteType = mModule->GetPrimitiveType(BfTypeCode_Int8);
			auto arrType = GetSizedArrayType(MapType(byteType), fillSize);
			irFieldTypes.push_back(arrType);
			dataPos = fieldInstance->mDataOffset;
		}
		dataPos += fieldInstance->mDataSize;

		BF_ASSERT((int)irFieldTypes.size() == fieldInstance->mDataIdx);
		irFieldTypes.push_back(resolvedFieldIRType);
	}

	if (isCRepr)
	{
		// Add explicit padding at end to enforce the "aligned size"
		int endPad = typeInstance->mInstSize - dataPos;
		if (endPad > 0)
		{
			auto byteType = mModule->GetPrimitiveType(BfTypeCode_Int8);
			auto arrType = GetSizedArrayType(MapType(byteType), endPad);
			irFieldTypes.push_back(arrType);
			dataPos += endPad;
		}
	}

	if (typeInstance->mIsUnion)
	{
	}
	else if ((typeInstance->IsEnum()) && (typeInstance->IsStruct()))
	{
		BF_FATAL("Shouldn't happen");

		// Just an empty placeholder
		auto byteType = GetPrimitiveType(BfTypeCode_UInt8);
		auto rawDataType = GetSizedArrayType(byteType, 0);
		irFieldTypes.push_back(rawDataType);
	}

	if (!typeInstance->IsTypedPrimitive())
		StructSetBody(MapTypeInst(typeInstance), irFieldTypes, typeInstance->mInstSize, typeInstance->mInstAlign, true);

	if (typeInstance->IsNullable())
	{
		BF_ASSERT(irFieldTypes.size() <= 3);
	}
}

void BfIRBuilder::CreateTypeDefinition(BfType* type, bool forceDbgDefine)
{
	auto populateModule = mModule->mContext->mUnreifiedModule;
	auto typeInstance = type->ToTypeInstance();
	if (typeInstance != NULL)
		populateModule = typeInstance->mModule;

	// This PopulateType is generally NOT needed, but here is a scenario in which it is:
	//  ClassB derives from ClassA.  ClassC uses ClassB.  A method inside ClassA gets modified,
	//  marking ClassA as incomplete, and then ClassC rebuilds and calls MapType on ClassB.
	//  "ClassB" itself is still populated, but its base class (ClassA) is not -- until we call
	//  this PopulateType below.
	if (type->IsDataIncomplete())
		populateModule->PopulateType(type, BfPopulateType_Data);

	bool isDefiningModule = ((type->GetModule() == mModule) || (type->IsFunction()));
	if (mModule->mExtensionCount != 0)
		isDefiningModule = false;

// 	if (mModule->mModuleName == "vdata")
// 		isDefiningModule = true;

	if ((isDefiningModule) || (type->IsValueType()))
	{
		if ((typeInstance != NULL) && (typeInstance->mDefineState == BfTypeDefineState_DefinedAndMethodsSlotting))
		{
			// Don't re-enter
			BfLogSys(mModule->mSystem, "BfIRBuilder::CreateTypeDefinition avoided PopulateType BfPopulateType_DataAndMethods re-entry typeInst: %p\n", typeInstance);
		}
		else
			populateModule->PopulateType(type, BfPopulateType_DataAndMethods);
	}

	if ((!isDefiningModule) && (!type->IsUnspecializedType()) && (type->IsValueType()) && (mHasDebugInfo))
	{
		DbgSetTypeSize(DbgGetType(type), BF_ALIGN(type->mSize, type->mAlign) * 8, type->mAlign * 8);
	}

	bool isPrimEnum = (type->IsEnum()) && (type->IsTypedPrimitive());

	if (typeInstance == NULL)
		return;

	BfType* underlyingArrayType = NULL;
	int underlyingArraySize = -1;
	bool underlyingIsVector = false;
	typeInstance->GetUnderlyingArray(underlyingArrayType, underlyingArraySize, underlyingIsVector);
	if (underlyingArraySize > 0)
	{
		// Don't populate data
	}
	else
	{
		CreateTypeDefinition_Data(populateModule, typeInstance, forceDbgDefine);
	}

	for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
	{
		auto fieldInstance = &fieldInstanceRef;
		if (!fieldInstance->mFieldIncluded)
			continue;
		auto fieldDef = fieldInstance->GetFieldDef();

		if ((fieldInstance->mResolvedType == NULL) || (typeInstance->IsBoxed()))
			continue;

		if ((fieldDef == NULL) || (!fieldDef->mIsStatic))
			continue; // Already handled non-statics

		auto resolvedFieldType = fieldInstance->GetResolvedType();

		mModule->PopulateType(resolvedFieldType, BfPopulateType_Declaration);
		BfIRType resolvedFieldIRType = MapType(resolvedFieldType);
		BfIRMDNode resolvedFieldDIType;

		//if ((fieldDef != NULL) && (!fieldDef->mIsStatic) && (resolvedFieldType->IsStruct()))
			//PopulateType(resolvedFieldType, BfIRPopulateType_Eventually_Full);

		if ((!typeInstance->IsBoxed()) && (fieldDef != NULL))
		{
			if (fieldDef->mIsConst)
			{
				if ((isDefiningModule) && (fieldDef->mIsExtern) && (resolvedFieldType->IsPointer()))
				{
					if (!resolvedFieldType->IsVoid())
						mModule->CreateStaticField(fieldInstance);
				}
			}
			else if (fieldDef->mIsStatic)
			{
				if (isDefiningModule)
				{
					fieldInstance->mIsThreadLocal = mModule->IsThreadLocal(fieldInstance);
					if (!resolvedFieldType->IsVoid())
						mModule->CreateStaticField(fieldInstance, fieldInstance->mIsThreadLocal);
				}
			}
		}
	}
}

void BfIRBuilder::ReplaceDITemporaryTypes()
{
	//for (auto typeInstance : mDITemporaryTypes)
	for (int i = 0; i < (int)mDITemporaryTypes.size(); i++)
	{
		auto typeInstance = mDITemporaryTypes[i];
		auto populateType = mTypeMap[typeInstance];
		if (populateType == BfIRPopulateType_Full)
			continue;

		mTypeMap[typeInstance] = BfIRPopulateType_Eventually_Full;
		CreateTypeDefinition(typeInstance, false);
		mTypeMap[typeInstance] = BfIRPopulateType_Full;
	}
	mDITemporaryTypes.Clear();
}

void BfIRBuilder::PushDbgLoc(BfTypeInstance* typeInst)
{
}

BfIRPopulateType BfIRBuilder::GetPopulateTypeState(BfType* type)
{
	BfIRPopulateType* populateTypePtr = NULL;
	if (mTypeMap.TryGetValue(type, &populateTypePtr))
		return *populateTypePtr;
	return BfIRPopulateType_Identity;
}

void BfIRBuilder::PopulateType(BfType* type, BfIRPopulateType populateType)
{
	if (mIgnoreWrites)
		return;

	BF_ASSERT(!mModule->mIsScratchModule);

	if (populateType == BfIRPopulateType_Identity)
		return;

	auto curPopulateType = BfIRPopulateType_Identity;
	BfIRPopulateType* populateTypePtr = NULL;
	if (mTypeMap.TryGetValue(type, &populateTypePtr))
		curPopulateType = *populateTypePtr;

	if (curPopulateType >= populateType)
		return;
	if (curPopulateType == BfIRPopulateType_Full)
		return;

	auto typeInst = type->ToTypeInstance();

	if ((curPopulateType < BfIRPopulateType_Declaration) && (populateType >= BfIRPopulateType_Declaration))
	{
		CreateTypeDeclaration(type, populateType == BfIRPopulateType_Full_ForceDefinition);

		mTypeMap[type] = BfIRPopulateType_Declaration;
	}

	if ((curPopulateType < populateType) && (populateType >= BfIRPopulateType_Eventually_Full))
	{
		mTypeMap[type] = BfIRPopulateType_Eventually_Full;
		CreateTypeDefinition(type, populateType == BfIRPopulateType_Full_ForceDefinition);
		mTypeMap[type] = BfIRPopulateType_Full;
	}
}

void BfIRBuilder::SetType(BfType* type, BfIRType irType)
{
	WriteCmd(BfIRCmd_SetType, type->mTypeId, irType);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetInstType(BfType* type, BfIRType irType)
{
	WriteCmd(BfIRCmd_SetInstType, type->mTypeId, irType);
	NEW_CMD_INSERTED;
}

int BfIRBuilder::GetFakeId()
{
	int fakeId = mCurFakeId;
	mCurFakeId--;
	if (mCurFakeId >= 0)
		mCurFakeId = -2;
	return fakeId;
}

BfIRValue BfIRBuilder::GetFakeVal()
{
	BfIRValue irValue;
	irValue.mFlags = BfIRValueFlags_Value;
	irValue.mId = GetFakeId();
	return irValue;
}

BfIRValue BfIRBuilder::GetFakeConst()
{
	BfIRValue irValue;
	irValue.mFlags = BfIRValueFlags_Const;
	irValue.mId = GetFakeId();
	return irValue;
}

BfIRType BfIRBuilder::GetFakeType()
{
	BfIRType type;
	type.mId = GetFakeId();
	return type;
}

BfIRType BfIRBuilder::GetFakeBlock()
{
	BfIRBlock block;
	block.mFlags = BfIRValueFlags_Block;
	block.mId = GetFakeId();
	return block;
}

BfIRFunctionType BfIRBuilder::GetFakeFunctionType()
{
	BfIRFunctionType funcType;
	funcType.mId = GetFakeId();
	return funcType;
}

BfIRFunction BfIRBuilder::GetFakeFunction()
{
	BfIRFunction func;
	//func.mFlags = BfIRValueFlags_Func;
	func.mId = GetFakeId();
	return func;
}

BfIRType BfIRBuilder::GetPrimitiveType(BfTypeCode typeCode)
{
	FixTypeCode(typeCode);

	BfIRType irType;
	irType.mKind = BfIRTypeData::TypeKind_TypeCode;
	irType.mId = (int)typeCode;
	return irType;
}

BfIRType BfIRBuilder::CreateStructType(const StringImpl& name)
{
	BfIRType retType = WriteCmd(BfIRCmd_CreateStruct, name);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

BfIRType BfIRBuilder::CreateStructType(const BfSizedArray<BfIRType>& memberTypes)
{
	BfIRType retType = WriteCmd(BfIRCmd_CreateAnonymousStruct, memberTypes);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

void BfIRBuilder::StructSetBody(BfIRType type, const BfSizedArray<BfIRType>& memberTypes, int size, int align, bool isPacked)
{
	WriteCmd(BfIRCmd_StructSetBody, type, memberTypes, size, align, isPacked);
	NEW_CMD_INSERTED;
}

BfIRType BfIRBuilder::MapType(BfType* type, BfIRPopulateType populateType)
{
	if (!mIgnoreWrites)
	{
		PopulateType(type, populateType);
	}
	BF_ASSERT(type->mTypeId > 0);
	BfIRType retType;
	retType.mKind = BfIRType::TypeKind_TypeId;
	retType.mId = type->mTypeId;
	return retType;
}

BfIRType BfIRBuilder::MapTypeInst(BfTypeInstance* typeInst, BfIRPopulateType populateType)
{
	if (!mIgnoreWrites)
	{
		PopulateType(typeInst, populateType);
	}

	if ((!mIgnoreWrites) && (populateType != BfIRPopulateType_Identity))
		BF_ASSERT(mTypeMap.ContainsKey(typeInst));
	BfIRType retType;
	retType.mKind = BfIRType::TypeKind_TypeInstId;
	retType.mId = typeInst->mTypeId;
	return retType;
}

BfIRType BfIRBuilder::MapTypeInstPtr(BfTypeInstance* typeInst)
{
	if (!mIgnoreWrites)
	{
		PopulateType(typeInst, BfIRPopulateType_Declaration);
	}

	BfIRType retType;
	retType.mKind = BfIRType::TypeKind_TypeInstPtrId;
	retType.mId = typeInst->mTypeId;
	return retType;
}

BfIRType BfIRBuilder::GetType(BfIRValue val)
{
	if (mIgnoreWrites)
		return GetFakeType();

	BfIRType retType = WriteCmd(BfIRCmd_GetType, val);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

BfIRType BfIRBuilder::GetPointerTo(BfIRFunctionType funcType)
{
	BfIRType retType = WriteCmd(BfIRCmd_GetPointerToFuncType, funcType);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

BfIRType BfIRBuilder::GetPointerTo(BfIRType type)
{
	BfIRType retType = WriteCmd(BfIRCmd_GetPointerToType, type);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

BfIRType BfIRBuilder::GetSizedArrayType(BfIRType elementType, int length)
{
	BF_ASSERT(length >= 0);
	if (mIgnoreWrites)
	{
		auto constSizedArrayType = mTempAlloc.Alloc<BfConstantSizedArrayType>();
		constSizedArrayType->mConstType = BfConstType_SizedArrayType;
		constSizedArrayType->mType = elementType;
		constSizedArrayType->mLength = length;

		int chunkId = mTempAlloc.GetChunkedId(constSizedArrayType);

		BfIRType retType;
		retType.mKind = BfIRTypeData::TypeKind_SizedArray;
		retType.mId = chunkId;
		return retType;
	}
	else
	{
		BfIRType retType = WriteCmd(BfIRCmd_GetSizedArrayType, elementType, length);
		NEW_CMD_INSERTED_IRTYPE;
		return retType;
	}
}

BfIRType BfIRBuilder::GetVectorType(BfIRType elementType, int length)
{
	BfIRType retType = WriteCmd(BfIRCmd_GetVectorType, elementType, length);
	NEW_CMD_INSERTED_IRTYPE;
	return retType;
}

BfIRValue BfIRBuilder::CreateConstAgg_Value(BfIRType type, const BfSizedArray<BfIRValue>& values)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateConstAgg, type, values);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateConstString(const StringImpl& str)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateConstString, str);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::ConstToMemory(BfIRValue constVal)
{
	auto constant = GetConstant(constVal);
	BfIRValue* value = NULL;
	if (mConstMemMap.TryGetValue(constVal.mId, &value))
		return *value;

	BfIRType constType;
	if (constant->mConstType == BfConstType_Agg)
		constType = ((BfConstantAgg*)constant)->mType;
	else if (constant->mConstType == BfConstType_AggZero)
		constType = constant->mIRType;
	else if (constant->mTypeCode == BfTypeCode_NullPtr)
		constType = constant->mIRType;
	else
		BF_FATAL("Invalid const type for ConstToMemory");
	auto memVal = CreateGlobalVariable(constType, true, BfIRLinkageType_Internal, constVal, StrFormat("__constMem%d", constVal.mId));
	mConstMemMap[constVal.mId] = memVal;
	return memVal;
}

BfIRValue BfIRBuilder::GetConfigConst(BfIRConfigConst constType, BfTypeCode typeCode)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_ConfigConst, (int)constType, typeCode);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::GetArgument(int argIdx)
{
	BfIRValue retVal(BfIRValueFlags_Arg, argIdx);
	return retVal;
}

void BfIRBuilder::SetName(BfIRValue val, const StringImpl& name)
{
	WriteCmd(BfIRCmd_SetName, val, name);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateUndefValue(BfIRType type)
{
 	BfIRValue retVal = WriteCmd(BfIRCmd_CreateUndefValue, type);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateNumericCast(BfIRValue val, bool valIsSigned, BfTypeCode typeCode)
{
	FixTypeCode(typeCode);
	if (val.IsConst())
	{
		auto constVal = GetConstantById(val.mId);
		if (constVal->mConstType == BfConstType_Undef)
			return GetUndefConstValue(GetPrimitiveType(typeCode));

		if (constVal->mTypeCode < BfTypeCode_Length)
		{
			// ? -> Int
			if (IsInt(typeCode))
			{
				uint64 val = 0;

				if ((typeCode == BfTypeCode_IntPtr) || (typeCode == BfTypeCode_UIntPtr))
				{
					if (mModule->mSystem->mPtrSize == 4)
						typeCode = (typeCode == BfTypeCode_IntPtr) ? BfTypeCode_Int32 : BfTypeCode_UInt32;
					else
						typeCode = (typeCode == BfTypeCode_IntPtr) ? BfTypeCode_Int64 : BfTypeCode_UInt64;
				}

				// Int -> Int
				if (IsInt(constVal->mTypeCode))
				{
					switch (typeCode)
					{
					case BfTypeCode_Int8: val = (int8)constVal->mInt64; break;
					case BfTypeCode_Char8:
					case BfTypeCode_UInt8: val = (uint8)constVal->mInt64; break;
					case BfTypeCode_Int16: val = (int16)constVal->mInt64; break;
					case BfTypeCode_Char16:
					case BfTypeCode_UInt16: val = (uint16)constVal->mInt64; break;
					case BfTypeCode_Int32: val = (int32)constVal->mInt64; break;
					case BfTypeCode_Char32:
					case BfTypeCode_UInt32: val = (uint32)constVal->mInt64; break;
					case BfTypeCode_Int64: val = (uint64)(int64)constVal->mInt64; break;
					case BfTypeCode_UInt64: val = (uint64)constVal->mUInt64; break;
					default: break;
					}
				}
				else // Float -> Int
				{
					switch (typeCode)
					{
					case BfTypeCode_Int8: val = (int8)constVal->mDouble; break;
					case BfTypeCode_Char8:
					case BfTypeCode_UInt8: val = (uint8)constVal->mDouble; break;
					case BfTypeCode_Int16: val = (int16)constVal->mDouble; break;
					case BfTypeCode_Char16:
					case BfTypeCode_UInt16: val = (uint16)constVal->mDouble; break;
					case BfTypeCode_Int32: val = (int32)constVal->mDouble; break;
					case BfTypeCode_Char32:
					case BfTypeCode_UInt32: val = (uint32)constVal->mDouble; break;
					case BfTypeCode_Int64: val = (uint64)(int64)constVal->mDouble; break;
					case BfTypeCode_UInt64: val = (uint64)constVal->mDouble; break;
					default: break;
					}
				}

				return CreateConst(typeCode, val);
			}

			// Int -> Float
			if (IsInt(constVal->mTypeCode))
			{
				double val = 0;
				if (IsSigned(constVal->mTypeCode))
					val = (double)constVal->mInt64;
				else
					val = (double)constVal->mUInt64;
				return CreateConst(typeCode, val);
			}

			// Float -> Float
			return CreateConst(typeCode, constVal->mDouble);
		}
	}

	auto retVal = WriteCmd(BfIRCmd_NumericCast, val, valIsSigned, typeCode);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpEQ(BfIRValue lhs, BfIRValue rhs)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, ==);
		int eqVal = CheckConstEquality(lhs, rhs);
		if (eqVal != -1)
			return CreateConst(BfTypeCode_Boolean, (eqVal == 1) ? (uint64)1 : (uint64)0);
	}

	auto retVal = WriteCmd(BfIRCmd_CmpEQ, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpNE(BfIRValue lhs, BfIRValue rhs)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, !=);
		int eqVal = CheckConstEquality(lhs, rhs);
		if (eqVal != -1)
			return CreateConst(BfTypeCode_Boolean, (eqVal == 0) ? (uint64)1 : (uint64)0);
	}

	auto retVal = WriteCmd(BfIRCmd_CmpNE, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpLT(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, <);
	}
	else if ((!isSigned) && (rhs.IsConst()))
	{
		// "unsigned < 0" is always false
		auto constant = GetConstant(rhs);
		if ((IsInt(constant->mTypeCode)) && (constant->mUInt64 == 0))
			return CreateConst(BfTypeCode_Boolean, 0);
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_CmpSLT : BfIRCmd_CmpULT, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpLTE(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, <=);
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_CmpSLE : BfIRCmd_CmpULE, lhs, rhs);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpGT(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, >);
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_CmpSGT : BfIRCmd_CmpUGT, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateCmpGTE(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		CMP_APPLY(lhs, rhs, >=);
	}
	else if ((!isSigned) && (lhs.IsConst()))
	{
		// "0 >= unsigned" is always true
		auto constant = GetConstant(lhs);
		if ((IsInt(constant->mTypeCode)) && (constant->mUInt64 == 0))
			return CreateConst(BfTypeCode_Boolean, 1);
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_CmpSGE : BfIRCmd_CmpUGE, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAdd(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind)
{
	mOpFailed = false;
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		if (constLHS->mConstType != BfConstType_PtrToInt)
		{
			BINOPFUNC_APPLY(lhs, rhs, CheckedAdd);
		}
	}

	auto retVal = WriteCmd(BfIRCmd_Add, lhs, rhs, overflowCheckKind);
	NEW_CMD_INSERTED_IRVALUE;

	if ((overflowCheckKind != BfOverflowCheckKind_None) && (!mIgnoreWrites))
	{
		mInsertBlock = mActualInsertBlock = WriteCmd(BfIRCmd_GetInsertBlock);
		NEW_CMD_INSERTED_IRVALUE;
	}

	return retVal;
}

BfIRValue BfIRBuilder::CreateSub(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind)
{
	mOpFailed = false;
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		BINOPFUNC_APPLY(lhs, rhs, CheckedSub);
	}

	auto retVal = WriteCmd(BfIRCmd_Sub, lhs, rhs, overflowCheckKind);
	NEW_CMD_INSERTED;

	if ((overflowCheckKind != BfOverflowCheckKind_None) && (!mIgnoreWrites))
	{
		mInsertBlock = mActualInsertBlock = WriteCmd(BfIRCmd_GetInsertBlock);
		NEW_CMD_INSERTED_IRVALUE;
	}

	return retVal;
}

BfIRValue BfIRBuilder::CreateMul(BfIRValue lhs, BfIRValue rhs, BfOverflowCheckKind overflowCheckKind)
{
	mOpFailed = false;
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		BINOPFUNC_APPLY(lhs, rhs, CheckedMul);
	}

	auto retVal = WriteCmd(BfIRCmd_Mul, lhs, rhs, overflowCheckKind);
	NEW_CMD_INSERTED_IRVALUE;

	if ((overflowCheckKind != BfOverflowCheckKind_None) && (!mIgnoreWrites))
	{
		mInsertBlock = mActualInsertBlock = WriteCmd(BfIRCmd_GetInsertBlock);
		NEW_CMD_INSERTED_IRVALUE;
	}

	return retVal;
}

BfIRValue BfIRBuilder::CreateDiv(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);

		if ((constLHS->mTypeCode == BfTypeCode_Float) || (constLHS->mTypeCode == BfTypeCode_Double))
		{
			double fVal = constLHS->mDouble / constRHS->mDouble;
			return CreateConst(constLHS->mTypeCode, fVal);
		}

		if (constRHS->mInt64 != 0)
		{
			INT_BINOP_APPLY(constLHS, constRHS, /);
		}
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_SDiv : BfIRCmd_UDiv, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateRem(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);

		if ((constLHS->mTypeCode == BfTypeCode_Float) || (constLHS->mTypeCode == BfTypeCode_Double))
		{
			double fVal = fmod(constLHS->mDouble, constRHS->mDouble);
			return CreateConst(constLHS->mTypeCode, fVal);
		}

		if (constRHS->mInt64 != 0)
		{
			INT_BINOP_APPLY(constLHS, constRHS, %);
		}
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_SRem : BfIRCmd_URem, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAnd(BfIRValue lhs, BfIRValue rhs)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);
		INT_BINOP_APPLY(constLHS, constRHS, &);
	}

	auto retVal = WriteCmd(BfIRCmd_And, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateOr(BfIRValue lhs, BfIRValue rhs)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);
		INT_BINOP_APPLY(constLHS, constRHS, |);
	}

	auto retVal = WriteCmd(BfIRCmd_Or, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateXor(BfIRValue lhs, BfIRValue rhs)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);
		INT_BINOP_APPLY(constLHS, constRHS, ^);
	}

	auto retVal = WriteCmd(BfIRCmd_Xor, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateShl(BfIRValue lhs, BfIRValue rhs)
{
	mOpFailed = false;
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		INT_BINOPFUNC_APPLY(lhs, rhs, CheckedShl);
	}

	auto retVal = WriteCmd(BfIRCmd_Shl, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateShr(BfIRValue lhs, BfIRValue rhs, bool isSigned)
{
	if ((lhs.IsConst()) && (rhs.IsConst()))
	{
		auto constLHS = GetConstantById(lhs.mId);
		auto constRHS = GetConstantById(rhs.mId);

		uint64 val;
		if (isSigned)
			val = (uint64)(constLHS->mInt64 >> constRHS->mInt32);
		else
			val = constLHS->mUInt64 >> constRHS->mInt32;
		return CreateConst(constLHS->mTypeCode, val);
	}

	auto retVal = WriteCmd(isSigned ? BfIRCmd_AShr : BfIRCmd_LShr, lhs, rhs);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateNeg(BfIRValue val)
{
	if (val.IsConst())
	{
		UNARYOP_APPLY(val, -);
	}

	auto retVal = WriteCmd(BfIRCmd_Neg, val);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateNot(BfIRValue val)
{
	if (val.IsConst())
	{
		auto constVal = GetConstantById(val.mId);
		uint64 newVal = 0;
		if (constVal->mTypeCode == BfTypeCode_Boolean)
			newVal = constVal->mBool ? 0 : 1;
		else
			newVal = ~constVal->mUInt64;
		return CreateConst(constVal->mTypeCode, newVal);
	}

	auto retVal = WriteCmd(BfIRCmd_Not, val);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateBitCast(BfIRValue val, BfIRType type)
{
	if (val.IsConst())
		return CreateConstBitCast(val, type);
	auto retVal = WriteCmd(BfIRCmd_BitCast, val, type);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreatePtrToInt(BfIRValue val, BfTypeCode typeCode)
{
	FixTypeCode(typeCode);
	if (val.IsConst())
	{
		auto ptrToInt = mTempAlloc.Alloc<BfConstantPtrToInt>();
		ptrToInt->mConstType = BfConstType_PtrToInt;
		ptrToInt->mTarget = val.mId;
		ptrToInt->mToTypeCode = typeCode;

		BfIRValue castedVal(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(ptrToInt));
#ifdef CHECK_CONSTHOLDER
		castedVal.mHolder = this;
#endif
		return castedVal;
	}

	auto retVal = WriteCmd(BfIRCmd_PtrToInt, val, typeCode);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateIntToPtr(BfIRValue val, BfIRType type)
{
	if (val.IsConst())
	{
		auto ptrToInt = mTempAlloc.Alloc<BfConstantIntToPtr>();
		ptrToInt->mConstType = BfConstType_IntToPtr;
		ptrToInt->mTarget = val.mId;
		ptrToInt->mToType = type;

		BfIRValue castedVal(BfIRValueFlags_Const, mTempAlloc.GetChunkedId(ptrToInt));
#ifdef CHECK_CONSTHOLDER
		castedVal.mHolder = this;
#endif
		return castedVal;
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_IntToPtr, val, type);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateIntToPtr(uint64 val, BfIRType type)
{
	return CreateIntToPtr(CreateConst(BfTypeCode_IntPtr, val), type);
}

BfIRValue BfIRBuilder::CreateInBoundsGEP(BfIRValue val, int idx0)
{
	if (val.IsConst())
	{
		auto constGEP = mTempAlloc.Alloc<BfConstantGEP32_1>();
		constGEP->mConstType = BfConstType_GEP32_1;
		constGEP->mTarget = val.mId;
		constGEP->mIdx0 = idx0;

		BfIRValue retVal;
		retVal.mFlags = BfIRValueFlags_Const;
		retVal.mId = mTempAlloc.GetChunkedId(constGEP);

#ifdef CHECK_CONSTHOLDER
		retVal.mHolder = this;
#endif
		return retVal;
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_InboundsGEP1_32, val, idx0);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateInBoundsGEP(BfIRValue val, int idx0, int idx1)
{
	if (val.IsConst())
	{
		auto constGEP = mTempAlloc.Alloc<BfConstantGEP32_2>();
		constGEP->mConstType = BfConstType_GEP32_2;
		constGEP->mTarget = val.mId;
		constGEP->mIdx0 = idx0;
		constGEP->mIdx1 = idx1;

		BfIRValue retVal;
		retVal.mFlags = BfIRValueFlags_Const;
		retVal.mId = mTempAlloc.GetChunkedId(constGEP);

#ifdef CHECK_CONSTHOLDER
		retVal.mHolder = this;
#endif
		return retVal;
	}

	auto retVal = WriteCmd(BfIRCmd_InboundsGEP2_32, val, idx0, idx1);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateInBoundsGEP(BfIRValue val, BfIRValue idx0)
{
	auto constant = GetConstant(val);
	if (constant != NULL)
	{
		if (constant->mConstType == BfConstType_IntToPtr)
		{
			auto fromPtrToInt = (BfConstantIntToPtr*)constant;
			auto fromTarget = GetConstantById(fromPtrToInt->mTarget);
			if (IsInt(fromTarget->mTypeCode))
			{
				if (fromPtrToInt->mToType.mKind == BfIRTypeData::TypeKind_TypeId)
				{
					auto type = mModule->mContext->mTypes[fromPtrToInt->mToType.mId];
					if (type->IsPointer())
					{
						auto elementType = type->GetUnderlyingType();
						auto addConstant = GetConstant(idx0);
						if ((addConstant != NULL) && (IsInt(addConstant->mTypeCode)))
						{
							return CreateIntToPtr(CreateConst(fromTarget->mTypeCode, (uint64)(fromTarget->mInt64 + addConstant->mInt64 * elementType->GetStride())),
								fromPtrToInt->mToType);
						}
					}
				}
			}
		}

		if (auto idxConstant = GetConstant(idx0))
		{
			if (IsInt(idxConstant->mTypeCode))
				return CreateInBoundsGEP(val, idxConstant->mInt32);
		}
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_InBoundsGEP1, val, idx0);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateInBoundsGEP(BfIRValue val, BfIRValue idx0, BfIRValue idx1)
{
	if ((val.IsConst()) && (idx0.IsConst()) && (idx1.IsConst()))
	{
		auto idx0Constant = GetConstant(idx0);
		auto idx1Constant = GetConstant(idx1);

		if ((IsInt(idx0Constant->mTypeCode)) && (IsInt(idx1Constant->mTypeCode)))
			return CreateInBoundsGEP(val, idx0Constant->mInt32, idx1Constant->mInt32);
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_InBoundsGEP2, val, idx0, idx1);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateIsNull(BfIRValue val)
{
	auto constant = GetConstant(val);
	if (constant != NULL)
	{
		if (constant->mTypeCode == BfTypeCode_NullPtr)
			return CreateConst(BfTypeCode_Boolean, 1);
		if (constant->mConstType == BfConstType_BitCastNull)
			return CreateConst(BfTypeCode_Boolean, 1);
		if (constant->mConstType == BfConstType_GlobalVar)
			return CreateConst(BfTypeCode_Boolean, 0);
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_IsNull, val);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateIsNotNull(BfIRValue val)
{
	auto constant = GetConstant(val);
	if (constant != NULL)
	{
		if (constant->mTypeCode == BfTypeCode_NullPtr)
			return CreateConst(BfTypeCode_Boolean, 0);
		if (constant->mConstType == BfConstType_BitCastNull)
			return CreateConst(BfTypeCode_Boolean, 0);
		if (constant->mConstType == BfConstType_GlobalVar)
			return CreateConst(BfTypeCode_Boolean, 1);
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_IsNotNull, val);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateExtractValue(BfIRValue val, int idx)
{
	auto aggConstant = GetConstant(val);
	if (aggConstant != NULL)
	{
		if (aggConstant->mConstType == BfConstType_Agg)
		{
			auto arrayConstant = (BfConstantAgg*)aggConstant;
			return arrayConstant->mValues[idx];
		}

		auto constGEP = mTempAlloc.Alloc<BfConstantExtractValue>();
		constGEP->mConstType = BfConstType_ExtractValue;
		constGEP->mTarget = val.mId;
		constGEP->mIdx0 = idx;

		BfIRValue retVal;
		retVal.mFlags = BfIRValueFlags_Const;
		retVal.mId = mTempAlloc.GetChunkedId(constGEP);

#ifdef CHECK_CONSTHOLDER
		retVal.mHolder = this;
#endif
		return retVal;
	}

	BfIRValue retVal = WriteCmd(BfIRCmd_ExtractValue, val, idx);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateExtractValue(BfIRValue val, BfIRValue idx)
{
	auto idxConst = GetConstant(idx);
	if (idxConst != NULL)
	{
		BF_ASSERT(IsInt(idxConst->mTypeCode));
		return CreateExtractValue(val, idxConst->mInt32);
	}

	if (mIgnoreWrites)
		return GetFakeVal();

	// We allow looking up into a const array with a non-const index by creating memory backing for the array
	auto arrConst = GetConstant(val);
	if (arrConst != NULL)
	{
		if ((arrConst->mConstType == BfConstType_Agg) || (arrConst->mConstType == BfConstType_AggZero))
		{
			BfIRValue arrMemVal = ConstToMemory(val);
			auto valAddr = CreateInBoundsGEP(arrMemVal, CreateConst(BfTypeCode_IntPtr, 0), idx);
			return CreateLoad(valAddr);
		}
	}

	BF_FATAL("Invalid extract value");
	return BfIRValue();
}

BfIRValue BfIRBuilder::CreateInsertValue(BfIRValue agg, BfIRValue val, int idx)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_InsertValue, agg, val, idx);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAlloca(BfIRType type)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Alloca, type);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAlloca(BfIRType type, BfIRValue arraySize)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AllocaArray, type, arraySize);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::SetAllocaAlignment(BfIRValue val, int alignment)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetAllocaAlignment, val, alignment);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetAllocaNoChkStkHint(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetAllocaNoChkStkHint, val);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetAllocaForceMem(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetAllocaForceMem, val);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateAliasValue(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AliasValue, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateLifetimeStart(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_LifetimeStart, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateLifetimeEnd(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_LifetimeEnd, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateLifetimeSoftEnd(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_LifetimeSoftEnd, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateLifetimeExtend(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_LifetimeExtend, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateValueScopeStart()
{
	BfIRValue retVal = WriteCmd(BfIRCmd_ValueScopeStart);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::CreateValueScopeRetain(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_ValueScopeRetain, val);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateValueScopeSoftEnd(BfIRValue scopeStart)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_ValueScopeSoftEnd, scopeStart);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateValueScopeHardEnd(BfIRValue scopeStart)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_ValueScopeHardEnd, scopeStart);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateLoad(BfIRValue val, bool isVolatile)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Load, val, isVolatile);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAlignedLoad(BfIRValue val, int align, bool isVolatile)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AlignedLoad, val, align, isVolatile);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateStore(BfIRValue val, BfIRValue ptr, bool isVolatile)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Store, val, ptr, isVolatile);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateAlignedStore(BfIRValue val, BfIRValue ptr, int align, bool isVolatile)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AlignedStore, val, ptr, align, isVolatile);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::CreateMemSet(BfIRValue addr, BfIRValue val, BfIRValue size, int align)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_MemSet, addr, val, size, align);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::CreateFence(BfIRFenceType fenceType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Fence, (uint8)fenceType);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateStackSave()
{
	BfIRValue retVal = WriteCmd(BfIRCmd_StackSave);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateStackRestore(BfIRValue stackVal)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_StackRestore, stackVal);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::CreateGlobalVariable(BfIRValue irValue)
{
	auto globalVar = (BfGlobalVar*)GetConstant(irValue);

	if ((!mIgnoreWrites) && (globalVar->mStreamId == -1))
	{
		if (globalVar->mInitializer)
			mHasGlobalDefs = true;

		BfIRValue retVal = WriteCmd(BfIRCmd_GlobalVariable, globalVar->mType, globalVar->mIsConst, (uint8)globalVar->mLinkageType, String(globalVar->mName), globalVar->mIsTLS, globalVar->mInitializer);
		globalVar->mStreamId = retVal.mId;

		NEW_CMD_INSERTED_IRVALUE;
	}
}

BfIRValue BfIRConstHolder::CreateGlobalVariableConstant(BfIRType varType, bool isConstant, BfIRLinkageType linkageType, BfIRValue initializer, const StringImpl& name, bool isTLS)
{
	BfIRValue* valuePtr = NULL;
	if ((!mGlobalVarMap.TryAdd(name, NULL, &valuePtr)) && (!initializer))
	{
		return *valuePtr;
	}

	BF_ASSERT(varType);

	auto constGV = mTempAlloc.Alloc<BfGlobalVar>();
	int chunkId = mTempAlloc.GetChunkedId(constGV);
	constGV->mStreamId = -1;
	constGV->mConstType = BfConstType_GlobalVar;
	constGV->mType = varType;
	constGV->mIsConst = isConstant;
	constGV->mLinkageType = linkageType;
	constGV->mInitializer = initializer;
	constGV->mName = AllocStr(name);
	constGV->mIsTLS = isTLS;

	auto irValue = BfIRValue(BfIRValueFlags_Const, chunkId);;
	*valuePtr = irValue;
	return irValue;
}

BfIRValue BfIRBuilder::CreateGlobalVariable(BfIRType varType, bool isConstant, BfIRLinkageType linkageType, BfIRValue initializer, const StringImpl& name, bool isTLS)
{
	auto irValue = CreateGlobalVariableConstant(varType, isConstant, linkageType, initializer, name, isTLS);
	CreateGlobalVariable(irValue);
	return irValue;
}

void BfIRBuilder::GlobalVar_SetUnnamedAddr(BfIRValue val, bool unnamedAddr)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_GlobalVar_SetUnnamedAddr, val, unnamedAddr);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::GlobalVar_SetInitializer(BfIRValue globalVar, BfIRValue initVal)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_GlobalVar_SetInitializer, globalVar, initVal);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::GlobalVar_SetAlignment(BfIRValue globalVar, int alignment)
{
	BF_ASSERT(alignment != -1);
	BfIRValue retVal = WriteCmd(BfIRCmd_GlobalVar_SetAlignment, globalVar, alignment);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::GlobalVar_SetStorageKind(BfIRValue globalVar, BfIRStorageKind storageKind)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_GlobalVar_SetStorageKind, globalVar, (int)storageKind);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateGlobalStringPtr(const StringImpl& str)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_GlobalStringPtr, str);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::SetReflectTypeData(BfIRType type, BfIRValue globalVar)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetReflectTypeData, type, globalVar);
	NEW_CMD_INSERTED_IRVALUE;
}

BfIRBlock BfIRBuilder::CreateBlock(const StringImpl& name, bool addNow)
{
	if (addNow)
		mActiveFunctionHasBody = true;
	mBlockCount++;
	BfIRBlock retBlock = WriteCmd(BfIRCmd_CreateBlock, name, addNow);
	NEW_CMD_INSERTED_IRBLOCK;
	return retBlock;
}

BfIRBlock BfIRBuilder::MaybeChainNewBlock(const StringImpl& name)
{
	BfIRBlock retBlock = WriteCmd(BfIRCmd_MaybeChainNewBlock, name);
	NEW_CMD_INSERTED_IRBLOCK;
	if (!mIgnoreWrites)
	{
		BF_ASSERT(!retBlock.IsFake());
		mActualInsertBlock = retBlock;
	}
	mInsertBlock = retBlock;
	return retBlock;
}

void BfIRBuilder::AddBlock(BfIRBlock block)
{
	mActiveFunctionHasBody = true;
	BfIRValue retVal = WriteCmd(BfIRCmd_AddBlock, block);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::DropBlocks(BfIRBlock startingBlock)
{
	WriteCmd(BfIRCmd_DropBlocks, startingBlock);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::MergeBlockDown(BfIRBlock fromBlock, BfIRBlock intoBlock)
{
	WriteCmd(BfIRCmd_MergeBlockDown, fromBlock, intoBlock);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetInsertPoint(BfIRValue value)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetInsertPoint, value);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetInsertPoint(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetInsertPoint, block);
	if (!mIgnoreWrites)
	{
		BF_ASSERT(!block.IsFake());
		mActualInsertBlock = block;
	}
	mInsertBlock = block;
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetInsertPointAtStart(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_SetInsertPointAtStart, block);
	if (!mIgnoreWrites)
	{
		BF_ASSERT(!block.IsFake());
		mActualInsertBlock = block;
	}
	mInsertBlock = block;
	NEW_CMD_INSERTED;
}

void BfIRBuilder::EraseFromParent(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_EraseFromParent, block);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::DeleteBlock(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_DeleteBlock, block);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::EraseInstFromParent(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_EraseInstFromParent, val);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateBr(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateBr, block);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateBr_Fake(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateBr_Fake, block);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateBr_NoCollapse(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateBr_NoCollapse, block);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::CreateCondBr(BfIRValue val, BfIRBlock trueBlock, BfIRBlock falseBlock)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateCondBr, val, trueBlock, falseBlock);
	NEW_CMD_INSERTED;
}

BfIRBlock BfIRBuilder::GetInsertBlock()
{
	if (!mIgnoreWrites)
	{
		BF_ASSERT(!mActualInsertBlock.IsFake());
		return mActualInsertBlock;
	}
	return mInsertBlock;
}

void BfIRBuilder::MoveBlockToEnd(BfIRBlock block)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_MoveBlockToEnd, block);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateSwitch(BfIRValue value, BfIRBlock dest, int numCases)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateSwitch, value, dest, numCases);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::AddSwitchCase(BfIRValue switchVal, BfIRValue caseVal, BfIRBlock caseBlock)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AddSwitchCase, switchVal, caseVal, caseBlock);

	BF_ASSERT(caseVal.IsConst());
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::SetSwitchDefaultDest(BfIRValue switchVal, BfIRBlock caseBlock)
{
	WriteCmd(BfIRCmd_SetSwitchDefaultDest, switchVal, caseBlock);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreatePhi(BfIRType type, int incomingCount)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreatePhi, type, incomingCount);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::AddPhiIncoming(BfIRValue phi, BfIRValue value, BfIRBlock comingFrom)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_AddPhiIncoming, phi, value, comingFrom);
	NEW_CMD_INSERTED;
}

BfIRFunction BfIRBuilder::GetIntrinsic(String intrinName, int intrinId, BfIRType returnType, const BfSizedArray<BfIRType>& paramTypes)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_GetIntrinsic, intrinName, intrinId, returnType, paramTypes);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRFunctionType BfIRBuilder::MapMethod(BfMethodInstance* methodInstance)
{
	if (mIgnoreWrites)
		return GetFakeFunctionType();

	bool useCache = (!mModule->mIsSpecialModule) && (methodInstance->mMethodDef->mIdx >= 0);

	if (useCache)
	{
		BfIRFunctionType* funcType = NULL;
		if (mMethodTypeMap.TryGetValue(methodInstance, &funcType))
			return *funcType;
	}

	BfIRType retType;
	SizedArray<BfIRType, 8> paramTypes;
	methodInstance->GetIRFunctionInfo(mModule, retType, paramTypes);

	auto funcType = CreateFunctionType(retType, paramTypes, methodInstance->IsVarArgs());
	if (useCache)
		mMethodTypeMap[methodInstance] = funcType;

	return funcType;
}

BfIRFunctionType BfIRBuilder::CreateFunctionType(BfIRType resultType, const BfSizedArray<BfIRType>& paramTypes, bool isVarArg)
{
	BfIRFunctionType retType = WriteCmd(BfIRCmd_CreateFunctionType, resultType, paramTypes, isVarArg);
	NEW_CMD_INSERTED_IRFUNCTYPE;
	return retType;
}

BfIRFunction BfIRBuilder::CreateFunction(BfIRFunctionType funcType, BfIRLinkageType linkageType, const StringImpl& name)
{
	if (mIgnoreWrites)
	{
		auto fakeVal = GetFakeVal();
		return fakeVal;
	}

	BF_ASSERT(mModule->mIsModuleMutable);

	BfIRFunction retVal = WriteCmd(BfIRCmd_CreateFunction, funcType, (uint8)linkageType, name);
	NEW_CMD_INSERTED_IRVALUE;

	StringView nameSV = StringView(AllocStr(name), name.mLength);
	mFunctionMap[nameSV] = retVal;

	//BfLogSys(mModule->mSystem, "BfIRBuilder::CreateFunction: %d %s Module:%p\n", retVal.mId, name.c_str(), mModule);

	return retVal;
}

void BfIRBuilder::SetFunctionName(BfIRValue func, const StringImpl& name)
{
	WriteCmd(BfIRCmd_SetFunctionName, func, name);
	NEW_CMD_INSERTED_IRVALUE;
}

void BfIRBuilder::EnsureFunctionPatchable()
{
	BfIRValue retVal = WriteCmd(BfIRCmd_EnsureFunctionPatchable);
	NEW_CMD_INSERTED_IRVALUE;
}

BfIRValue BfIRBuilder::RemapBindFunction(BfIRValue func)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_RemapBindFunction, func);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::SetActiveFunction(BfIRFunction func)
{
	//BfLogSys(mModule->mSystem, "BfIRBuilder::SetActiveFunction: %d\n", func.mId);

	if (mActiveFunctionHasBody)
		mNumFunctionsWithBodies++;

	WriteCmd(BfIRCmd_SetActiveFunction, func);
	mActiveFunction = func;
	mActiveFunctionHasBody = false;
	NEW_CMD_INSERTED;
}

BfIRFunction BfIRBuilder::GetActiveFunction()
{
	return mActiveFunction;
}

BfIRFunction BfIRBuilder::GetFunction(const StringImpl& name)
{
	BfIRFunction* funcPtr = NULL;
	if (mFunctionMap.TryGetValue(name, &funcPtr))
		return *funcPtr;
	return BfIRFunction();
}

BfIRValue BfIRBuilder::CreateCall(BfIRValue func, const BfSizedArray<BfIRValue>& args)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateCall, func, args);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::SetCallCallingConv(BfIRValue callInst, BfIRCallingConv callingConv)
{
	if (callingConv == BfIRCallingConv_CDecl)
		return;
	WriteCmd(BfIRCmd_SetCallCallingConv, callInst, (uint8)callingConv);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetFuncCallingConv(BfIRFunction func, BfIRCallingConv callingConv)
{
	if (callingConv == BfIRCallingConv_CDecl)
		return;
	WriteCmd(BfIRCmd_SetFuncCallingConv, func, (uint8)callingConv);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetTailCall(BfIRValue callInst)
{
	WriteCmd(BfIRCmd_SetTailCall, callInst);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetCallAttribute(BfIRValue callInst, int paramIdx, BfIRAttribute attribute)
{
	WriteCmd(BfIRCmd_SetCallAttribute, callInst, paramIdx, attribute);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::CreateRet(BfIRValue val)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateRet, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::CreateSetRet(BfIRValue val, int returnTypeId)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_CreateSetRet, val, returnTypeId);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::CreateRetVoid()
{
	WriteCmd(BfIRCmd_CreateRetVoid);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateUnreachable()
{
	WriteCmd(BfIRCmd_CreateUnreachable);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Call_AddAttribute(BfIRValue callInst, int argIdx, BfIRAttribute attr)
{
	WriteCmd(BfIRCmd_Call_AddAttribute, callInst, argIdx, attr);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Call_AddAttribute(BfIRValue callInst, int argIdx, BfIRAttribute attr, int arg)
{
	WriteCmd(BfIRCmd_Call_AddAttribute1, callInst, argIdx, attr, arg);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_AddAttribute(BfIRFunction func, int argIdx, BfIRAttribute attr)
{
	WriteCmd(BfIRCmd_Func_AddAttribute, func, argIdx, attr);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_AddAttribute(BfIRFunction func, int argIdx, BfIRAttribute attr, int arg)
{
	WriteCmd(BfIRCmd_Func_AddAttribute1, func, argIdx, attr, arg);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_SetParamName(BfIRFunction func, int argIdx, const StringImpl& name)
{
	WriteCmd(BfIRCmd_Func_SetParamName, func, argIdx, name);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_DeleteBody(BfIRFunction func)
{
	if (mActiveFunction == func)
		mActiveFunctionHasBody = false;

	WriteCmd(BfIRCmd_Func_DeleteBody, func);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_SafeRename(BfIRFunction func)
{
	WriteCmd(BfIRCmd_Func_SafeRename, func);

	// We don't actually remove it from the named map.  It doesn't matter for us.

// 	{
// 		auto llvmFunc = llvm::dyn_cast<llvm::Function>(func.mLLVMValue);
// 		llvmFunc->eraseFromParent();
// 	}

	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_SafeRenameFrom(BfIRFunction func, const StringImpl& prevName)
{
	WriteCmd(BfIRCmd_Func_SafeRenameFrom, func, prevName);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Func_SetLinkage(BfIRFunction func, BfIRLinkageType linkage)
{
	WriteCmd(BfIRCmd_Func_SetLinkage, func, (uint8)linkage);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::Comptime_Error(int errorKind)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_Error, errorKind);
	NEW_CMD_INSERTED;
}

BfIRValue BfIRBuilder::Comptime_GetBfType(int typeId, BfIRType resultType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_GetBfType, typeId, resultType);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::Comptime_GetReflectType(int typeId, BfIRType resultType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_GetReflectType, typeId, resultType);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::Comptime_DynamicCastCheck(BfIRValue value, int typeId, BfIRType resultType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_DynamicCastCheck, value, typeId, resultType);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::Comptime_GetVirtualFunc(BfIRValue value, int virtualTableId, BfIRType resultType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_GetVirtualFunc, value, virtualTableId, resultType);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::Comptime_GetInterfaceFunc(BfIRValue value, int typeId, int methodIdx, BfIRType resultType)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_Comptime_GetInterfaceFunc, value, typeId, methodIdx, resultType);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::SaveDebugLocation()
{
	if (!mIgnoreWrites)
	{
		mSavedDebugLocs.push_back(mModule->mCurFilePosition);
		WriteCmd(BfIRCmd_SaveDebugLocation);
		NEW_CMD_INSERTED;
	}
}

void BfIRBuilder::RestoreDebugLocation()
{
	if (!mIgnoreWrites)
	{
		mModule->mCurFilePosition = mSavedDebugLocs.back();
		mSavedDebugLocs.pop_back();
		WriteCmd(BfIRCmd_RestoreDebugLocation);
		mHasDebugLoc = true;
		NEW_CMD_INSERTED;
	}
}

void BfIRBuilder::DupDebugLocation()
{
	WriteCmd(BfIRCmd_DupDebugLocation);
	NEW_CMD_INSERTED;
}

bool BfIRBuilder::HasDebugLocation()
{
	return mHasDebugLoc;
}

void BfIRBuilder::ClearDebugLocation()
{
	WriteCmd(BfIRCmd_ClearDebugLocation);
	mHasDebugLoc = false;
	NEW_CMD_INSERTED;
}

void BfIRBuilder::ClearDebugLocation(BfIRValue inst)
{
	WriteCmd(BfIRCmd_ClearDebugLocationInst, inst);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::ClearDebugLocation_Last()
{
	WriteCmd(BfIRCmd_ClearDebugLocationInstLast);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::UpdateDebugLocation(BfIRValue inst)
{
	WriteCmd(BfIRCmd_UpdateDebugLocation, inst);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::SetCurrentDebugLocation(int line, int column, BfIRMDNode diScope, BfIRMDNode diInlinedAt)
{
	BF_ASSERT(diScope);
	if (mDbgVerifyCodeGen && gDebugDbgLoc)
	{
		OutputDebugStrF("SetCurrentDebugLocation %d %d:%d\n", diScope.mId, line, column);
	}
	WriteCmd(BfIRCmd_SetCurrentDebugLocation, line, column, diScope, diInlinedAt);
	mHasDebugLoc = true;
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateNop()
{
	WriteCmd(BfIRCmd_Nop);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateEnsureInstructionAt()
{
	WriteCmd(BfIRCmd_EnsureInstructionAt);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateStatementStart()
{
	WriteCmd(BfIRCmd_StatementStart);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::CreateObjectAccessCheck(BfIRValue value, bool useAsm)
{
	auto retBlock = WriteCmd(BfIRCmd_ObjectAccessCheck, value, useAsm);
	NEW_CMD_INSERTED_IRBLOCK;
	if (!mIgnoreWrites)
	{
		BF_ASSERT(!retBlock.IsFake());
		mActualInsertBlock = retBlock;
	}
	mInsertBlock = retBlock;
}

void BfIRBuilder::DbgInit()
{
	mHasDebugInfo = true;
	mHasDebugLineInfo = true;
	WriteCmd(BfIRCmd_DbgInit);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::DbgFinalize()
{
	while ((!mDeferredDbgTypeDefs.IsEmpty()) || (!mDITemporaryTypes.IsEmpty()))
	{
		//for (auto deferredType : mDeferredDbgTypeDefs)
		for (int i = 0; i < (int)mDeferredDbgTypeDefs.size(); i++)
			CreateDbgTypeDefinition(mDeferredDbgTypeDefs[i]);
		mDeferredDbgTypeDefs.Clear();

		ReplaceDITemporaryTypes();
	}

	WriteCmd(BfIRCmd_DbgFinalize);
	NEW_CMD_INSERTED;
}

bool BfIRBuilder::DbgHasInfo()
{
	return mHasDebugInfo;
}

bool BfIRBuilder::DbgHasLineInfo()
{
	return mHasDebugLineInfo;
}

String BfIRBuilder::DbgGetStaticFieldName(BfFieldInstance* fieldInstance)
{
	String fieldName;
	auto fieldDef = fieldInstance->GetFieldDef();
	auto typeInstance = fieldInstance->mOwner;
	auto typeDef = typeInstance->mTypeDef;
	if (mModule->mCompiler->mOptions.IsCodeView())
	{
		fieldName += "_bf";
		for (int partIdx = 0; partIdx < typeInstance->mTypeDef->mNamespace.GetPartsCount(); partIdx++)
		{
			auto atom = typeInstance->mTypeDef->mNamespace.mParts[partIdx];
			if (!fieldName.empty())
				fieldName += "::";
			fieldName += atom->ToString();
		}
		if (!fieldName.empty())
			fieldName += "::";
		fieldName += GetDebugTypeName(typeInstance, true);
		fieldName += "::";
		fieldName += fieldDef->mName;
	}
	else
	{
		fieldName += fieldDef->mName;
	}
	return fieldName;
}

void BfIRBuilder::DbgAddPrefix(String & name)
{
	if (mModule->mCompiler->mOptions.IsCodeView())
		name.Insert(0, "bf__");
}

BfIRMDNode BfIRBuilder::DbgCreateCompileUnit(int lang, const StringImpl& fileName, const StringImpl& directory, const StringImpl& producer, bool isOptimized, const StringImpl& flags, int runtimeVer, bool linesOnly)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateCompileUnit, lang, fileName, directory, producer, isOptimized, flags, runtimeVer, linesOnly);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateFile(const StringImpl& fileName, const StringImpl& directory, const Val128& md5Hash)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateFile, fileName, directory, md5Hash);
	NEW_CMD_INSERTED_IRMD;

	if (mDbgVerifyCodeGen && gDebugDbgLoc)
	{
		OutputDebugStrF("DbgCreateFile %s %d\n", fileName.c_str(), retVal.mId);
	}

	return retVal;
}

BfIRMDNode BfIRBuilder::DbgGetCurrentLocation()
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgGetCurrentLocation);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

void BfIRBuilder::DbgSetType(BfType* type, BfIRMDNode diType)
{
	WriteCmd(BfIRCmd_DbgSetType, type->mTypeId, diType);
	NEW_CMD_INSERTED;
}

void BfIRBuilder::DbgSetInstType(BfType* type, BfIRMDNode diType)
{
	WriteCmd(BfIRCmd_DbgSetInstType, type->mTypeId, diType);
	NEW_CMD_INSERTED;
}

BfIRMDNode BfIRBuilder::DbgCreateConstValue(int64 val)
{
	auto retVal = WriteCmd(BfIRCmd_ConstValueI64, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgGetType(BfType* type, BfIRPopulateType populateType)
{
	if (mIgnoreWrites)
		return BfIRMDNode();
	PopulateType(type, populateType);
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgGetType, type->mTypeId);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgGetTypeInst(BfTypeInstance* typeInst, BfIRPopulateType populateType)
{
	if (mIgnoreWrites)
		return BfIRMDNode();
	PopulateType(typeInst, populateType);
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgGetTypeInst, typeInst->mTypeId);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

void BfIRBuilder::DbgTrackDITypes(BfType* type)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgTrackDITypes, type->mTypeId);
	NEW_CMD_INSERTED;
}

BfIRMDNode BfIRBuilder::DbgCreateNameSpace(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNum)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateNamespace, scope, name, file, lineNum);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateImportedModule(BfIRMDNode context, BfIRMDNode namespaceNode, int line)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateImportedModule, context, namespaceNode, line);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateBasicType(const StringImpl& name, int64 sizeInBits, int64 alignInBits, int encoding)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateBasicType, name, (int32)sizeInBits, (int32)alignInBits, encoding);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateStructType(BfIRMDNode context, const StringImpl& name, BfIRMDNode file, int lineNum, int64 sizeInBits, int64 alignInBits, int flags, BfIRMDNode derivedFrom, const BfSizedArray<BfIRMDNode>& elements)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateStructType, context, name, file, lineNum, (int32)sizeInBits, (int32)alignInBits, flags, derivedFrom, elements);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateEnumerationType(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNumber, int64 sizeInBits, int64 alignInBits, const BfSizedArray<BfIRMDNode>& elements, BfIRMDNode underlyingType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateEnumerationType, scope, name, file, lineNumber, (int32)sizeInBits, (int32)alignInBits, elements, underlyingType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreatePointerType(BfIRMDNode diType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreatePointerType, diType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateReferenceType(BfIRMDNode diType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateReferenceType, diType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateConstType(BfIRMDNode diType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateConstType, diType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateArtificialType(BfIRMDNode diType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateArtificialType, diType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateArrayType(int64 sizeInBits, int64 alignInBits, BfIRMDNode elementType, int64 numElements)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateArrayType, sizeInBits, alignInBits, elementType, numElements);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateReplaceableCompositeType(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode file, int line, int64 sizeInBits, int64 alignInBits, int flags)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateReplaceableCompositeType, tag, name, scope, file, line, (int32)sizeInBits, (int32)alignInBits, flags);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

void BfIRBuilder::DbgSetTypeSize(BfIRMDNode diType, int64 sizeInBits, int alignInBits)
{
	BF_ASSERT(diType);
	BfIRMDNode retVal = WriteCmd(BeIRCmd_DbgSetTypeSize, diType, sizeInBits, alignInBits);
	NEW_CMD_INSERTED_IRMD;
}

BfIRMDNode BfIRBuilder::DbgCreateForwardDecl(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode file, int line)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateForwardDecl, tag, name, scope, file, line);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateSizedForwardDecl(int tag, const StringImpl& name, BfIRMDNode scope, BfIRMDNode file, int line, int64 sizeInBits, int64 alignInBits)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateSizedForwardDecl, tag, name, scope, file, line, (int32)sizeInBits, (int32)alignInBits);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgReplaceAllUses(BfIRMDNode diPrevNode, BfIRMDNode diNewNode)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgReplaceAllUses, diPrevNode, diNewNode);
	NEW_CMD_INSERTED;
	return retVal;
}

void BfIRBuilder::DbgDeleteTemporary(BfIRMDNode diNode)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgDeleteTemporary, diNode);
	NEW_CMD_INSERTED;
}

BfIRMDNode BfIRBuilder::DbgMakePermanent(BfIRMDNode diNode, BfIRMDNode diBaseType, const BfSizedArray<BfIRMDNode>& elements)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgMakePermanent, diNode, diBaseType, elements);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateEnumerator(const StringImpl& name, int64 val)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_CreateEnumerator, name, val);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateMemberType(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNumber, int64 sizeInBits, int64 alignInBits, int64 offsetInBits, int flags, BfIRMDNode type)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateMemberType, scope, name, file, lineNumber, (int32)sizeInBits, (int32)alignInBits, (int32)offsetInBits, flags, type);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateStaticMemberType(BfIRMDNode scope, const StringImpl&name, BfIRMDNode file, int lineNumber, BfIRMDNode type, int flags, BfIRValue val)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgStaticCreateMemberType, scope, name, file, lineNumber, type, flags, val);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateInheritance(BfIRMDNode type, BfIRMDNode baseType, int64 baseOffset, int flags)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateInheritance, type, baseType, (int32)baseOffset, flags);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateMethod(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNum, BfIRMDNode type, bool isLocalToUnit, bool isDefinition, int vk, int vIndex, BfIRMDNode vTableHolder, int flags,
	bool isOptimized, BfIRValue fn, const BfSizedArray<BfIRMDNode>& genericArgs, const BfSizedArray<BfIRValue>& genericConstValueArgs)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateMethod, context, name, linkageName, file, lineNum, type, isLocalToUnit, isDefinition, vk, vIndex, vTableHolder, flags, isOptimized, fn, genericArgs, genericConstValueArgs);
	NEW_CMD_INSERTED_IRMD;

// 	if (mDbgVerifyCodeGen && gDebugDbgLoc)
// 	{
// 		OutputDebugStrF("DbgCreateFunction Context:%d name:%s = %d\n", context.mId, name.c_str(), retVal.mId);
// 	}

	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateFunction(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNum, BfIRMDNode type, bool isLocalToUnit, bool isDefinition, int scopeLine, int flags, bool isOptimized, BfIRValue fn)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateFunction, context, name, linkageName, file, lineNum, type, isLocalToUnit, isDefinition, scopeLine, flags, isOptimized, fn);
	NEW_CMD_INSERTED_IRMD;

// 	if (mDbgVerifyCodeGen && gDebugDbgLoc)
// 	{
// 		OutputDebugStrF("DbgCreateFunction Context:%d name:%s = %d\n", context.mId, name.c_str(), retVal.mId);
// 	}

	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateParameterVariable(BfIRMDNode scope, const StringImpl& name, int argNo, BfIRMDNode file, int lineNum, BfIRMDNode type, bool alwaysPreserve, int flags)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateParameterVariable, scope, name, argNo, file, lineNum, type, alwaysPreserve, flags);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateSubroutineType(BfMethodInstance* methodInstance)
{
	auto methodDef = methodInstance->mMethodDef;
	auto typeInstance = methodInstance->GetOwner();

	SizedArray<BfIRMDNode, 32> diParams;
	diParams.push_back(DbgGetType(methodInstance->mReturnType));

	BfType* thisType = NULL;
	if (!methodDef->mIsStatic)
	{
		BfType* thisType;
		thisType = typeInstance;
		if (!thisType->IsValuelessType())
		{
			BfType* thisPtrType = thisType;
			auto diType = DbgGetType(thisPtrType);
			if ((thisType->IsComposite()) && (!methodInstance->GetParamIsSplat(-1)))
			{
				diType = DbgCreatePointerType(diType);
				diType = DbgCreateArtificialType(diType);
			}
			diParams.push_back(diType);
		}
	}

	for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
	{
		bool isParamSkipped = methodInstance->IsParamSkipped(paramIdx);
		if (!isParamSkipped)
		{
			auto resolvedType = methodInstance->GetParamType(paramIdx);
			diParams.push_back(DbgGetType(resolvedType));
		}
	}

	return DbgCreateSubroutineType(diParams);
}

BfIRMDNode BfIRBuilder::DbgCreateSubroutineType(const BfSizedArray<BfIRMDNode>& elements)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateSubroutineType, elements);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRMDNode BfIRBuilder::DbgCreateAutoVariable(BfIRMDNode scope, const StringImpl& name, BfIRMDNode file, int lineNo, BfIRMDNode type, BfIRInitType initType)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateAutoVariable, scope, name, file, lineNo, type, (int)initType);
	NEW_CMD_INSERTED_IRMD;
	return retVal;
}

BfIRValue BfIRBuilder::DbgInsertValueIntrinsic(BfIRValue val, BfIRMDNode varInfo)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_DbgInsertValueIntrinsic, val, varInfo);
	NEW_CMD_INSERTED;
	return retVal;
}

BfIRValue BfIRBuilder::DbgInsertDeclare(BfIRValue val, BfIRMDNode varInfo, BfIRValue declareBefore)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_DbgInsertDeclare, val, varInfo, declareBefore);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

BfIRValue BfIRBuilder::DbgLifetimeEnd(BfIRMDNode varInfo)
{
	BfIRValue retVal = WriteCmd(BfIRCmd_DbgLifetimeEnd, varInfo);
	NEW_CMD_INSERTED_IRVALUE;
	return retVal;
}

void BfIRBuilder::DbgCreateGlobalVariable(BfIRMDNode context, const StringImpl& name, const StringImpl& linkageName, BfIRMDNode file, int lineNumber, BfIRMDNode type, bool isLocalToUnit, BfIRValue val, BfIRMDNode decl)
{
	WriteCmd(BfIRCmd_DbgCreateGlobalVariable, context, name, linkageName, file, lineNumber, type, isLocalToUnit, val, decl);
	NEW_CMD_INSERTED;
}

BfIRMDNode BfIRBuilder::DbgCreateLexicalBlock(BfIRMDNode scope, BfIRMDNode file, int line, int col)
{
	BfIRMDNode retVal = WriteCmd(BfIRCmd_DbgCreateLexicalBlock, scope, file, line, col);
	NEW_CMD_INSERTED;

	if (mDbgVerifyCodeGen && gDebugDbgLoc)
	{
		OutputDebugStrF("DbgCreateLexicalBlock Scope:%d File:%d = %d\n", scope.mId, file.mId, retVal.mId);
	}

	return retVal;
}

void BfIRBuilder::DbgCreateAnnotation(BfIRMDNode scope, const StringImpl& name, BfIRValue value)
{
	WriteCmd(BfIRCmd_DbgCreateAnnotation, scope, name, value);
	NEW_CMD_INSERTED;
}

BfIRState BfIRBuilder::GetState()
{
	BfIRState state;
	state.mActualInsertBlock = mActualInsertBlock;
	state.mInsertBlock = mInsertBlock;
	state.mActiveFunction = mActiveFunction;
	state.mActiveFunctionHasBody = mActiveFunctionHasBody;
	state.mSavedDebugLocs = mSavedDebugLocs;
	return state;
}

void BfIRBuilder::SetState(const BfIRState& state)
{
	mActualInsertBlock = state.mActualInsertBlock;
	mInsertBlock = state.mInsertBlock;
	mActiveFunction = state.mActiveFunction;
	mActiveFunctionHasBody = state.mActiveFunctionHasBody;
	mSavedDebugLocs = state.mSavedDebugLocs;
}