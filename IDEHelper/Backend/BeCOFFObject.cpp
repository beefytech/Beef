#include "BeCOFFObject.h"
#include "BeMCContext.h"
#include "BeLibManger.h"
#include "../COFFData.h"
#include "BeefySysLib/MemStream.h"
#include "codeview/cvinfo.h"
#include "BeefySysLib/util/BeefPerf.h"

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(disable:4996)

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

BeInlineLineBuilder::BeInlineLineBuilder()
{
	mCurLine = 0;
	mCurCodePos = 0;
	mStartDbgLoc = NULL;
	mEnded = false;
}

void BeInlineLineBuilder::Compress(int val)
{
	if (val <= 0x7F)
	{
		mData.push_back((uint8)val);
		return;
	}

	if (val <= 0x3FFF)
	{
		mData.push_back((uint8)(val >> 8) | 0x80);
		mData.push_back((uint8)(val & 0xFF));
		return;
	}

	if (val <= 0x1FFFFFFF)
	{
		mData.push_back((uint8)(val >> 24) | 0xC0);
		mData.push_back((uint8)((val >> 16) & 0xFF));
		mData.push_back((uint8)((val >> 8) & 0xFF));
		mData.push_back((uint8)(val & 0xFF));
		return;
	}
}

void BeInlineLineBuilder::WriteSigned(int val)
{
	if (val < 0)
		Compress(-val * 2 + 1);
	else
		Compress(val * 2);
}

void BeInlineLineBuilder::Update(BeDbgCodeEmission* codeEmission)
{
	BF_ASSERT(!mEnded);
	int lineOfs = codeEmission->mDbgLoc->mLine - mCurLine;
	int codeOfs = codeEmission->mPos - mCurCodePos;

	if (codeOfs != 0)
	{
		mData.push_back(CodeViewInfo::BA_OP_ChangeLineOffset);
		WriteSigned(lineOfs);
		mData.push_back(CodeViewInfo::BA_OP_ChangeCodeOffset);
		Compress(codeOfs);

		mCurLine = codeEmission->mDbgLoc->mLine;
		mCurCodePos = codeEmission->mPos;
	}
}

void BeInlineLineBuilder::Start(BeDbgCodeEmission* codeEmission)
{	
	//mCurLine = codeEmission->mDbgLoc->mDbgInlinedAt->mLine;

	int lineOfs = codeEmission->mDbgLoc->mLine - mCurLine;
	int codeOfs = codeEmission->mPos - mCurCodePos;
	
	auto usingFile = codeEmission->mDbgLoc->mDbgInlinedAt->GetDbgFile();
	auto wantFile = codeEmission->mDbgLoc->GetDbgFile();
	
	mData.push_back(CodeViewInfo::BA_OP_ChangeLineOffset);
	WriteSigned(lineOfs);

	/*if (wantFile != usingFile)
	{
		mData.push_back(CodeViewInfo::BA_OP_ChangeFile);
		Compress(wantFile->mIdx * 8);
	}*/

	mData.push_back(CodeViewInfo::BA_OP_ChangeCodeOffset);
	Compress(codeOfs);	

	mCurLine = codeEmission->mDbgLoc->mLine;
	mCurCodePos = codeEmission->mPos;	

	/*mData.push_back(CodeViewInfo::BA_OP_ChangeLineOffset);
	WriteSigned(-mStartDbgLoc->mDbgInlinedAt->mLine - 1);
	mData.push_back(CodeViewInfo::BA_OP_ChangeCodeOffset);
	Compress(codeEmission->mPos);

	mCurLine = -1;
	mCurCodePos = codeEmission->mPos;*/
}

void BeInlineLineBuilder::End(BeDbgCodeEmission* codeEmission)
{
	BF_ASSERT(!mEnded);
	int codeOfs = codeEmission->mPos - mCurCodePos;
	mData.push_back(CodeViewInfo::BA_OP_ChangeCodeLength);
	Compress(codeOfs);
	mCurCodePos = codeEmission->mPos;
	mEnded = true;
}

//////////////////////////////////////////////////////////////////////////

BeCOFFObject::BeCOFFObject()
{
	mWriteToLib = false;
	mBSSPos = 0;	
	mCurTagId = 0x1000;
	mCurStringId = 0;
	mCurJumpTableIdx = 0;
	mBeModule = NULL;
	mTTagStartPos = -1;
	mSTagStartPos = -1;
	mSectionStartPos = -1;	
	mTypesLocked = false;
	mTimestamp = 0;
	mPerfManager = NULL;
	mStream = NULL;
}

void BeCOFFObject::ToString(BeMDNode* mdNode, String& str)
{
	if (auto dbgStruct = BeValueDynCast<BeDbgStructType>(mdNode))
	{
		if (auto dbgNamespace = BeValueDynCast<BeDbgNamespace>(dbgStruct->mScope))
		{
			ToString(dbgNamespace, str);
			str += "::";
		}
		else if (auto dbgOuterType = BeValueDynCast<BeDbgType>(dbgStruct->mScope))
		{
			ToString(dbgOuterType, str);
			str += "::";
		}
		str += dbgStruct->mName;
	}
	else if (auto dbgStruct = BeValueDynCast<BeDbgEnumType>(mdNode))
	{
		if (auto dbgNamespace = BeValueDynCast<BeDbgNamespace>(dbgStruct->mScope))
		{
			ToString(dbgNamespace, str);
			str += "::";
		}
		else if (auto dbgOuterType = BeValueDynCast<BeDbgType>(dbgStruct->mScope))
		{
			ToString(dbgOuterType, str);
			str += "::";
		}
		str += dbgStruct->mName;
	}
	else if (auto dbgNamespace = BeValueDynCast<BeDbgNamespace>(mdNode))
	{
		if (auto outerNamespace = BeValueDynCast<BeDbgNamespace>(dbgNamespace->mScope))
		{
			ToString(outerNamespace, str);
			str += "::";
		}
		str += dbgNamespace->mName;
	}
	else
	{
		BF_FATAL("err");
	}	
}

int BeCOFFObject::GetCVRegNum(X64CPURegister reg, int bits)
{		
	if (bits == 8)
	{
		switch (reg)
		{
		case X64Reg_RAX: return CV_AMD64_AL;
		case X64Reg_RDX: return CV_AMD64_DL;
		case X64Reg_RCX: return CV_AMD64_CL;
		case X64Reg_RBX: return CV_AMD64_BL;
		case X64Reg_RSI: return CV_AMD64_SIL;
		case X64Reg_RDI: return CV_AMD64_DIL;
		case X64Reg_R8: return  CV_AMD64_R8B;
		case X64Reg_R9: return  CV_AMD64_R9B;
		case X64Reg_R10: return CV_AMD64_R10B;
		case X64Reg_R11: return CV_AMD64_R11B;
		case X64Reg_R12: return CV_AMD64_R12B;
		case X64Reg_R13: return CV_AMD64_R13B;
		case X64Reg_R14: return CV_AMD64_R14B;
		case X64Reg_R15: return CV_AMD64_R15B;
		}
	}

	if (bits == 16)
	{
		switch (reg)
		{
		case X64Reg_RAX: return CV_AMD64_AX;
		case X64Reg_RDX: return CV_AMD64_DX;
		case X64Reg_RCX: return CV_AMD64_CX;
		case X64Reg_RBX: return CV_AMD64_BX;
		case X64Reg_RSI: return CV_AMD64_SI;
		case X64Reg_RDI: return CV_AMD64_DI;
		case X64Reg_R8: return  CV_AMD64_R8W;
		case X64Reg_R9: return  CV_AMD64_R9W;
		case X64Reg_R10: return CV_AMD64_R10W;
		case X64Reg_R11: return CV_AMD64_R11W;
		case X64Reg_R12: return CV_AMD64_R12W;
		case X64Reg_R13: return CV_AMD64_R13W;
		case X64Reg_R14: return CV_AMD64_R14W;
		case X64Reg_R15: return CV_AMD64_R15W;
		}
	}

	if (bits == 32)
	{
		switch (reg)
		{
		case X64Reg_RAX: return CV_AMD64_EAX;
		case X64Reg_RDX: return CV_AMD64_EDX;
		case X64Reg_RCX: return CV_AMD64_ECX;
		case X64Reg_RBX: return CV_AMD64_EBX;
		case X64Reg_RSI: return CV_AMD64_ESI;
		case X64Reg_RDI: return CV_AMD64_EDI;
		case X64Reg_R8: return  CV_AMD64_R8D;
		case X64Reg_R9: return  CV_AMD64_R9D;
		case X64Reg_R10: return CV_AMD64_R10D;
		case X64Reg_R11: return CV_AMD64_R11D;
		case X64Reg_R12: return CV_AMD64_R12D;
		case X64Reg_R13: return CV_AMD64_R13D;
		case X64Reg_R14: return CV_AMD64_R14D;
		case X64Reg_R15: return CV_AMD64_R15D;
		}
	}

	switch (reg)
	{
	case X64Reg_RAX: return CV_AMD64_RAX;
	case X64Reg_RDX: return CV_AMD64_RDX;
	case X64Reg_RCX: return CV_AMD64_RCX;
	case X64Reg_RBX: return CV_AMD64_RBX;
	case X64Reg_RSI: return CV_AMD64_RSI;
	case X64Reg_RDI: return CV_AMD64_RDI;
	case X64Reg_RBP: return CV_AMD64_RBP;
	case X64Reg_RSP: return CV_AMD64_RSP;
	case X64Reg_R8: return  CV_AMD64_R8;
	case X64Reg_R9: return  CV_AMD64_R9;
	case X64Reg_R10: return CV_AMD64_R10;
	case X64Reg_R11: return CV_AMD64_R11;
	case X64Reg_R12: return CV_AMD64_R12;
	case X64Reg_R13: return CV_AMD64_R13;
	case X64Reg_R14: return CV_AMD64_R14;
	case X64Reg_R15: return CV_AMD64_R15;
	case X64Reg_RIP: return CV_AMD64_RIP;

	case X64Reg_EAX: return CV_AMD64_EAX;
	case X64Reg_EDX: return CV_AMD64_EDX;
	case X64Reg_ECX: return CV_AMD64_ECX;
	case X64Reg_EBX: return CV_AMD64_EBX;
	case X64Reg_ESI: return CV_AMD64_ESI;
	case X64Reg_EDI: return CV_AMD64_EDI;
	case X64Reg_R8D: return  CV_AMD64_R8D;
	case X64Reg_R9D: return  CV_AMD64_R9D;
	case X64Reg_R10D: return CV_AMD64_R10D;
	case X64Reg_R11D: return CV_AMD64_R11D;
	case X64Reg_R12D: return CV_AMD64_R12D;
	case X64Reg_R13D: return CV_AMD64_R13D;
	case X64Reg_R14D: return CV_AMD64_R14D;
	case X64Reg_R15D: return CV_AMD64_R15D;

	case X64Reg_AX: return CV_AMD64_AX;
	case X64Reg_DX: return CV_AMD64_DX;
	case X64Reg_CX: return CV_AMD64_CX;
	case X64Reg_BX: return CV_AMD64_BX;
	case X64Reg_SI: return CV_AMD64_SI;
	case X64Reg_DI: return CV_AMD64_DI;
	case X64Reg_R8W: return  CV_AMD64_R8W;
	case X64Reg_R9W: return  CV_AMD64_R9W;
	case X64Reg_R10W: return CV_AMD64_R10W;
	case X64Reg_R11W: return CV_AMD64_R11W;
	case X64Reg_R12W: return CV_AMD64_R12W;
	case X64Reg_R13W: return CV_AMD64_R13W;
	case X64Reg_R14W: return CV_AMD64_R14W;
	case X64Reg_R15W: return CV_AMD64_R15W;

	case X64Reg_AL: return CV_AMD64_AL;
	case X64Reg_DL: return CV_AMD64_DL;
	case X64Reg_CL: return CV_AMD64_CL;
	case X64Reg_BL: return CV_AMD64_BL;
	case X64Reg_SIL: return CV_AMD64_SIL;
	case X64Reg_DIL: return CV_AMD64_DIL;
	case X64Reg_R8B: return  CV_AMD64_R8B;
	case X64Reg_R9B: return  CV_AMD64_R9B;
	case X64Reg_R10B: return CV_AMD64_R10B;
	case X64Reg_R11B: return CV_AMD64_R11B;
	case X64Reg_R12B: return CV_AMD64_R12B;
	case X64Reg_R13B: return CV_AMD64_R13B;
	case X64Reg_R14B: return CV_AMD64_R14B;
	case X64Reg_R15B: return CV_AMD64_R15B;

	case X64Reg_XMM0_f32: case X64Reg_XMM0_f64: return CV_AMD64_XMM0_0;
	case X64Reg_XMM1_f32: case X64Reg_XMM1_f64: return CV_AMD64_XMM1_0;
	case X64Reg_XMM2_f32: case X64Reg_XMM2_f64: return CV_AMD64_XMM2_0;
	case X64Reg_XMM3_f32: case X64Reg_XMM3_f64: return CV_AMD64_XMM3_0;
	case X64Reg_XMM4_f32: case X64Reg_XMM4_f64: return CV_AMD64_XMM4_0;
	case X64Reg_XMM5_f32: case X64Reg_XMM5_f64: return CV_AMD64_XMM5_0;
	case X64Reg_XMM6_f32: case X64Reg_XMM6_f64: return CV_AMD64_XMM6_0;
	case X64Reg_XMM7_f32: case X64Reg_XMM7_f64: return CV_AMD64_XMM7_0;
	case X64Reg_XMM8_f32: case X64Reg_XMM8_f64: return CV_AMD64_XMM8_0;
	case X64Reg_XMM9_f32: case X64Reg_XMM9_f64: return CV_AMD64_XMM9_0;
	case X64Reg_XMM10_f32: case X64Reg_XMM10_f64: return CV_AMD64_XMM10_0;
	case X64Reg_XMM11_f32: case X64Reg_XMM11_f64: return CV_AMD64_XMM11_0;
	case X64Reg_XMM12_f32: case X64Reg_XMM12_f64: return CV_AMD64_XMM12_0;
	case X64Reg_XMM13_f32: case X64Reg_XMM13_f64: return CV_AMD64_XMM13_0;
	case X64Reg_XMM14_f32: case X64Reg_XMM14_f64: return CV_AMD64_XMM14_0;
	case X64Reg_XMM15_f32: case X64Reg_XMM15_f64: return CV_AMD64_XMM15_0;

	case X64Reg_XMM00: return CV_AMD64_XMM0_0;
	case X64Reg_XMM01: return CV_AMD64_XMM0_1;
	case X64Reg_XMM02: return CV_AMD64_XMM0_2;
	case X64Reg_XMM03: return CV_AMD64_XMM0_3;
	case X64Reg_XMM10: return CV_AMD64_XMM1_0;
	case X64Reg_XMM11: return CV_AMD64_XMM1_1;
	case X64Reg_XMM12: return CV_AMD64_XMM1_2;
	case X64Reg_XMM13: return CV_AMD64_XMM1_3;
	case X64Reg_XMM20: return CV_AMD64_XMM2_0;
	case X64Reg_XMM21: return CV_AMD64_XMM2_1;
	case X64Reg_XMM22: return CV_AMD64_XMM2_2;
	case X64Reg_XMM23: return CV_AMD64_XMM2_3;
	case X64Reg_XMM30: return CV_AMD64_XMM3_0;
	case X64Reg_XMM31: return CV_AMD64_XMM3_1;
	case X64Reg_XMM32: return CV_AMD64_XMM3_2;
	case X64Reg_XMM33: return CV_AMD64_XMM3_3;
	case X64Reg_XMM40: return CV_AMD64_XMM4_0;
	case X64Reg_XMM41: return CV_AMD64_XMM4_1;
	case X64Reg_XMM42: return CV_AMD64_XMM4_2;
	case X64Reg_XMM43: return CV_AMD64_XMM4_3;
	case X64Reg_XMM50: return CV_AMD64_XMM5_0;
	case X64Reg_XMM51: return CV_AMD64_XMM5_1;
	case X64Reg_XMM52: return CV_AMD64_XMM5_2;
	case X64Reg_XMM53: return CV_AMD64_XMM5_3;
	case X64Reg_XMM60: return CV_AMD64_XMM6_0;
	case X64Reg_XMM61: return CV_AMD64_XMM6_1;
	case X64Reg_XMM62: return CV_AMD64_XMM6_2;
	case X64Reg_XMM63: return CV_AMD64_XMM6_3;
	case X64Reg_XMM70: return CV_AMD64_XMM7_0;
	case X64Reg_XMM71: return CV_AMD64_XMM7_1;
	case X64Reg_XMM72: return CV_AMD64_XMM7_2;
	case X64Reg_XMM73: return CV_AMD64_XMM7_3;
	}
		
	return 0;
}

void BeCOFFObject::DbgEncodeConstant(DynMemStream& memStream, int64 val)
{
	if ((val >= 0) && (val <= 0x7FFF))
	{
		memStream.Write((int16)val);
		return;
	}
	if ((val >= -0x80) && (val <= 0x7F))
	{
		memStream.Write((int16)LF_CHAR);
		memStream.Write((int8)val);
		return;
	}
	if ((val >= -0x8000) && (val <= 0x7FFF))
	{
		memStream.Write((int16)LF_SHORT);
		memStream.Write((int16)val);
		return;
	}
	if ((val >= -0x80000000LL) && (val <= 0x7FFFFFFF))
	{
		memStream.Write((int16)LF_LONG);
		memStream.Write((int32)val);
		return;
	}
	memStream.Write((int16)LF_QUADWORD);
	memStream.Write((int64)val);
}

void BeCOFFObject::DbgEncodeString(DynMemStream& memStream, const StringImpl& str)
{
	memStream.Write((char*)str.c_str(), str.length() + 1);
}

void BeCOFFObject::DbgMakeFuncType(BeDbgFunction* dbgFunc)
{	
	if (dbgFunc->mCvTypeId != -1)
		return;

	/*auto funcTypeResultPair = mFuncTypeSet.insert(dbgFunc);
	if (!funcTypeResultPair.second)
	{
		// This matchesInlineScope another signature so we don't need to redefine it
		dbgFunc->mCvTypeId = (*funcTypeResultPair.first)->mCvTypeId;
		return;
	}*/

	COFFFuncTypeRef* funcTypeRefPtr = NULL;
	if (!mFuncTypeSet.TryAdd(dbgFunc, &funcTypeRefPtr))
	{
		// This matchesInlineScope another signature so we don't need to redefine it
		dbgFunc->mCvTypeId = funcTypeRefPtr->mFunc->mCvTypeId;
		return;
	}

	auto dbgType = BeValueDynCast<BeDbgType>(dbgFunc->mScope);

	auto& outT = mDebugTSect.mData;

	for (auto genericArgType : dbgFunc->mGenericArgs)
		DbgGetTypeId(genericArgType);

	auto parentType = BeValueDynCast<BeDbgType>(dbgFunc->mScope);
	DbgGetTypeId(parentType);
	for (auto funcParam : dbgFunc->mType->mParams)
		DbgGetTypeId(funcParam);
	DbgGetTypeId(dbgFunc->mType->mReturnType);
	for (auto paramType : dbgFunc->mType->mParams)
		DbgGetTypeId(paramType);

	bool hasThis = dbgFunc->HasThis();		

	/*auto argListResultPair = mArgListSet.insert(dbgFunc);
	if (!argListResultPair.second)
	{		
		dbgFunc->mCvArgListId = (*argListResultPair.first)->mCvArgListId;		
	}
	else*/

	COFFArgListRef* argListRefPtr = NULL;
	if (!mArgListSet.TryAdd(dbgFunc, &argListRefPtr))
	{
		dbgFunc->mCvArgListId = argListRefPtr->mFunc->mCvArgListId;
	}
	else
	{
		dbgFunc->mCvArgListId = mCurTagId++;
		DbgTStartTag();
		outT.Write((int16)LF_ARGLIST);
		outT.Write((int32)(dbgFunc->mType->mParams.size() - (hasThis ? 1 : 0)));
		for (int paramIdx = hasThis ? 1 : 0; paramIdx < (int)dbgFunc->mType->mParams.size(); paramIdx++)
		{
			BeDbgType* dbgType = dbgFunc->mType->mParams[paramIdx];			
			outT.Write(DbgGetTypeId(dbgType));
		}
		DbgTEndTag();
	}
		
	dbgFunc->mCvTypeId = mCurTagId++;
	DbgTStartTag();

	if (dbgType != NULL)
	{
		outT.Write((int16)LF_MFUNCTION);
		outT.Write(DbgGetTypeId(dbgFunc->mType->mReturnType));
		outT.Write(DbgGetTypeId(parentType));

		if (hasThis)
		{
			if ((dbgFunc->mVariables.size() > 0) && (dbgFunc->mVariables[0] == NULL))
				outT.Write(DbgGetTypeId(dbgFunc->GetParamType(1))); // 0 is sret, 1 = this
			else
				outT.Write(DbgGetTypeId(dbgFunc->GetParamType(0))); // 0 = this
		}
		else
			outT.Write((int32)T_VOID);
		outT.Write((uint8)0); // calltype
		CV_funcattr_t attr = { 0 };
		outT.Write(*(uint8*)&attr);
		outT.Write((int16)(dbgFunc->mType->mParams.size() - (hasThis ? 1 : 0)));
		outT.Write((int32)dbgFunc->mCvArgListId);
		outT.Write((int32)0); // thisadjust
	}
	else
	{
		outT.Write((int16)LF_PROCEDURE);
		outT.Write(DbgGetTypeId(dbgFunc->mType->mReturnType));
		outT.Write((uint8)0); // calltype
		CV_funcattr_t attr = { 0 };
		outT.Write(*(uint8*)&attr);
		outT.Write((int16)(dbgFunc->mType->mParams.size() - (hasThis ? 1 : 0)));
		outT.Write((int32)dbgFunc->mCvArgListId);
	}
	DbgTEndTag();
}

void BeCOFFObject::DbgMakeFunc(BeDbgFunction* dbgFunc)
{
	auto& outT = mDebugTSect.mData;
	BF_ASSERT(dbgFunc->mCvTypeId == -1);	

	DbgMakeFuncType(dbgFunc);

	auto dbgType = BeValueDynCast<BeDbgType>(dbgFunc->mScope);
	if (dbgType != NULL)
	{
		dbgFunc->mCvFuncId = mCurTagId++;
		DbgTStartTag();
		outT.Write((int16)LF_MFUNC_ID);
		outT.Write(dbgType->mCvDeclTypeId);
		outT.Write(dbgFunc->mCvTypeId);
		DbgEncodeString(outT, dbgFunc->mName);
		DbgTEndTag();
	}
	else
	{
		dbgFunc->mCvFuncId = mCurTagId++;
		DbgTStartTag();
		outT.Write((int16)LF_FUNC_ID);
		outT.Write((int32)0); // ScopeID (global)
		outT.Write(dbgFunc->mCvTypeId);
		DbgEncodeString(outT, dbgFunc->mName);
		DbgTEndTag();
	}
}

void BeCOFFObject::DbgTAlign()
{
	int curPos = mDebugTSect.mData.GetPos();
	// Perform alignment	
	int addPadding = (4 - (curPos & 3)) % 4;
	while (addPadding > 0)
	{
		mDebugTSect.mData.Write((uint8)(0xF0 + addPadding));
		addPadding--;
	}
}

void BeCOFFObject::DbgTStartTag()
{	
	BF_ASSERT(mTTagStartPos == -1);
	mTTagStartPos = mDebugTSect.mData.GetPos();
	mDebugTSect.mData.Write((int16)0);
}

void BeCOFFObject::DbgTEndTag()
{	
	BF_ASSERT(mTTagStartPos != -1);
	DbgTAlign();
	int tagSize = mDebugTSect.mData.GetPos() - mTTagStartPos;	
	*((int16*)&mDebugTSect.mData.mData[mTTagStartPos]) = (int16)(tagSize - 2);
	mTTagStartPos = -1;
}

int BeCOFFObject::DbgGetTypeId(BeDbgType* dbgType, bool doDefine)
{
	if (dbgType == NULL)
		return 0;
	if ((!doDefine && (dbgType->mCvDeclTypeId != -1)))
	{
		if (dbgType->mCvDefTypeId != -1)
			return dbgType->mCvDefTypeId;
		return dbgType->mCvDeclTypeId;
	}
	if ((doDefine) && (dbgType->mCvDefTypeId != -1))
		return dbgType->mCvDefTypeId;

	auto& outT = mDebugTSect.mData;

	auto structType = BeValueDynCast<BeDbgStructType>(dbgType);
	
	if (structType != NULL)
	{
		//lfClass classInfo;			
		//BF_CLEAR_VALUE(classInfo);
		//classInfo.leaf = LF_STRUCTURE;			
		
		CV_prop_t structProp = { 0 };		

		if (structType->mAlign == 1)
			structProp.packed = true;

		int fieldListTag = 0;
		int memberCount = 0;		
		if (doDefine)
		{
			// Pass over all needed types first to make sure we generate any forward references we'll need
			//  because we're locked in inbetween DbgTStartTag and DbgTEndTag
			DbgGetTypeId(dbgType);
			DbgGetTypeId(structType->mDerivedFrom);
			for (auto member : structType->mMembers)
			{
				auto type = member->mType;
				//TODO:
				//if (member->mName == "VersionName")
					//continue;

				DbgGetTypeId(type);
			}
			for (auto func : structType->mMethods)
				DbgMakeFuncType(func);			

			fieldListTag = mCurTagId++;

			//int tagStartPos = -1;
			DbgTStartTag();
			outT.Write((int16)LF_FIELDLIST);
			
			if (structType->mDerivedFrom != NULL)
			{
				outT.Write((int16)LF_BCLASS);
				CV_fldattr_t attr = { 0 };
				attr.access = 3; // public
				outT.Write(*(int16*)&attr);
				outT.Write(DbgGetTypeId(structType->mDerivedFrom));
				DbgEncodeConstant(outT, 0); // Offset
				DbgTAlign();
			}

			auto _CheckFieldOverflow = [&]()
			{
				int tagSize = mDebugTSect.mData.GetPos() - mTTagStartPos;
				if (tagSize >= 2000)
				{
					int extFieldListTag = mCurTagId++;

					outT.Write((int16)LF_INDEX);
					outT.Write((int16)0); // Padding				
					outT.Write((int32)extFieldListTag); // Padding
					DbgTEndTag();

					DbgTStartTag();
					outT.Write((int16)LF_FIELDLIST);
				}
			};

			for (auto member : structType->mMembers)
			{	
				_CheckFieldOverflow();

				if (member->mIsStatic)
					outT.Write((int16)LF_STMEMBER);
				else
					outT.Write((int16)LF_MEMBER);

				CV_fldattr_t attr = { 0 };
				if ((member->mFlags & 3) == llvm::DINode::FlagPrivate)
					attr.access = 1;
				else if ((member->mFlags & 3) == llvm::DINode::FlagProtected)
					attr.access = 2;
				else
					attr.access = 3;
				outT.Write(*(int16*)&attr);
				outT.Write(DbgGetTypeId(member->mType));
				if (!member->mIsStatic)
					DbgEncodeConstant(outT, member->mOffset);				
				DbgEncodeString(outT, member->mName);
				memberCount++;
				DbgTAlign();
			}

			for (auto func : structType->mMethods)
			{
				_CheckFieldOverflow();

				//TODO: Handle static methods
				outT.Write((int16)LF_ONEMETHOD);
				CV_fldattr_t attr = { 0 };
				if ((func->mFlags & 3) == llvm::DINode::FlagPrivate)
					attr.access = 1;
				else if ((func->mFlags & 3) == llvm::DINode::FlagProtected)
					attr.access = 2;
				else
					attr.access = 3;
				if ((func->mFlags & llvm::DINode::FlagArtificial) != 0)
					attr.compgenx = 1;
				bool isVirt = func->mVK > 0;
				if (isVirt)
					attr.mprop = CV_MTintro;
				outT.Write(*(int16*)&attr);
				outT.Write(func->mCvTypeId);
				if (isVirt)
					outT.Write((int32)func->mVIndex * mBeModule->mContext->mPointerSize);
				DbgEncodeString(outT, func->mName);
				memberCount++;
				DbgTAlign();
			}

			// LF_FIELDLIST
			DbgTEndTag();
		}
		else
		{
			structProp.fwdref = 1;
		}

		DbgTStartTag();
		outT.Write((int16)LF_STRUCTURE);
		outT.Write((int16)memberCount);
		outT.Write(*(int16*)&structProp);
		outT.Write((int32)fieldListTag);
		outT.Write((int32)0); //derivedfrom - should we ever set this?
		outT.Write((int32)0); //vshape
		if (doDefine)
			DbgEncodeConstant(outT, structType->mSize);		
		else
			DbgEncodeConstant(outT, 0);
		String fullName;
		ToString(structType, fullName);		

		DbgEncodeString(outT, fullName);		
		DbgTEndTag(); // LF_STRUCTURE
		
		if (doDefine)
			dbgType->mCvDefTypeId = mCurTagId++;
		else
			dbgType->mCvDeclTypeId = mCurTagId++;

		if (doDefine)
		{
			int strId = mCurTagId++;
			DbgTStartTag();
			outT.Write((int16)LF_STRING_ID);
			outT.Write(0);
			String fullPath;
			structType->mDefFile->ToString(fullPath);
			DbgEncodeString(outT, fullPath);
			DbgTEndTag();

			mCurTagId++;
			DbgTStartTag();
			outT.Write((int16)LF_UDT_SRC_LINE);
			outT.Write(structType->mCvDefTypeId);
			outT.Write(strId);
			outT.Write(structType->mDefLine + 1);
			DbgTEndTag();

			/*for (auto func : structType->mMethods)
			{
				// Yes, using declTypeId is correct here
				DbgTStartTag();
				outT.Write((int16)LF_MFUNC_ID);
				outT.Write(declTypeId);
				outT.Write(func->mCvTypeId);
				DbgEncodeString(outT, func->mName);
				DbgTEndTag();
				func->mCvFuncId = mCurTagId++;
			}*/

			return dbgType->mCvDefTypeId;
		}
		
		return dbgType->mCvDeclTypeId;
	}	
	else if (auto enumType = BeValueDynCast<BeDbgEnumType>(dbgType))
	{
		int fieldListId = 0;

		if (enumType->mIsFullyDefined)
		{
			fieldListId = mCurTagId++;
			//int tagStartPos = -1;
			DbgTStartTag();
			outT.Write((int16)LF_FIELDLIST);
			
			for (auto member : enumType->mMembers)
			{				
				outT.Write((int16)LF_ENUMERATE);

				CV_fldattr_t attr = { 0 };
				attr.access = 3; // public
				outT.Write(*(int16*)&attr);				
				DbgEncodeConstant(outT, member->mValue);
				DbgEncodeString(outT, member->mName);
				DbgTAlign();
			}

			// LF_FIELDLIST
			DbgTEndTag();
		}

		int32 elementId = T_VOID;
		if (enumType->mElementType != NULL)
			elementId = DbgGetTypeId(enumType->mElementType);

		//int32 elementId = DbgGetTypeId(enumType->mElementType);

		CV_prop_t structProp = { 0 };			
		if (!enumType->mIsFullyDefined)
			structProp.fwdref = 1;

		DbgTStartTag();
		outT.Write((int16)LF_ENUM);
		outT.Write((int16)enumType->mMembers.size());			
		outT.Write(*(int16*)&structProp);
		outT.Write(elementId);
		outT.Write((int32)fieldListId);
		String fullName;
		ToString(dbgType, fullName);
		DbgEncodeString(outT, fullName);
		DbgTEndTag();
		dbgType->mCvDeclTypeId = dbgType->mCvDefTypeId = mCurTagId++;
		return dbgType->mCvDefTypeId;
	}
	else if (auto dbgBasicType = BeValueDynCast<BeDbgBasicType>(dbgType))
	{
		switch (dbgBasicType->mEncoding)
		{
		case llvm::dwarf::DW_ATE_address:
			dbgBasicType->mCvDefTypeId = 0;
			break;
		case llvm::dwarf::DW_ATE_signed:
			switch (dbgBasicType->mSize)
			{
			case 1: dbgBasicType->mCvDefTypeId = T_INT1; break;
			case 2: dbgBasicType->mCvDefTypeId = T_SHORT; break;
			case 4: dbgBasicType->mCvDefTypeId = T_INT4; break;
			case 8: dbgBasicType->mCvDefTypeId = T_QUAD; break;
			}
			break;
		case llvm::dwarf::DW_ATE_unsigned:
			switch (dbgBasicType->mSize)
			{
			case 1: dbgBasicType->mCvDefTypeId = T_UINT1; break;
			case 2: dbgBasicType->mCvDefTypeId = T_USHORT; break;
			case 4: dbgBasicType->mCvDefTypeId = T_UINT4; break;
			case 8: dbgBasicType->mCvDefTypeId = T_UQUAD; break;
			}
			break;
		case llvm::dwarf::DW_ATE_float:
			switch (dbgBasicType->mSize)
			{			
			case 4: dbgBasicType->mCvDefTypeId = T_REAL32; break;
			case 8: dbgBasicType->mCvDefTypeId = T_REAL64; break;
			}
			break;
		case llvm::dwarf::DW_ATE_unsigned_char:
			switch (dbgBasicType->mSize)
			{
			case 1: dbgBasicType->mCvDefTypeId = T_CHAR; break;
			case 2: dbgBasicType->mCvDefTypeId = T_CHAR16; break;
			case 4: dbgBasicType->mCvDefTypeId = T_CHAR32; break;
			}
			break;
		case llvm::dwarf::DW_ATE_boolean:
			dbgBasicType->mCvDefTypeId = T_BOOL08;
			break;
		}

		dbgBasicType->mCvDeclTypeId = dbgBasicType->mCvDefTypeId;
		return dbgBasicType->mCvDeclTypeId;
	}
	else if (auto ptrType = BeValueDynCast<BeDbgPointerType>(dbgType))
	{
		if (auto innerBasicType = BeValueDynCast<BeDbgBasicType>(ptrType->mElement))
		{
			switch (innerBasicType->mEncoding)
			{
			case llvm::dwarf::DW_ATE_address:
				ptrType->mCvDefTypeId = T_PVOID;
				break;
			case llvm::dwarf::DW_ATE_signed:
				switch (innerBasicType->mSize)
				{
				case 1: ptrType->mCvDefTypeId = T_64PINT1; break;
				case 2: ptrType->mCvDefTypeId = T_64PSHORT; break;
				case 4: ptrType->mCvDefTypeId = T_64PINT4; break;
				case 8: ptrType->mCvDefTypeId = T_64PINT8; break;
				}
				break;
			case llvm::dwarf::DW_ATE_unsigned:
				switch (innerBasicType->mSize)
				{
				case 1: ptrType->mCvDefTypeId = T_64PUINT1; break;
				case 2: ptrType->mCvDefTypeId = T_64PUSHORT; break;
				case 4: ptrType->mCvDefTypeId = T_64PUINT4; break;
				case 8: ptrType->mCvDefTypeId = T_64PUINT8; break;
				}
				break;
			case llvm::dwarf::DW_ATE_float:
				switch (innerBasicType->mSize)
				{
				case 4: ptrType->mCvDefTypeId = T_64PREAL32; break;
				case 8: ptrType->mCvDefTypeId = T_64PREAL64; break;
				}
				break;
			case llvm::dwarf::DW_ATE_unsigned_char:
				switch (innerBasicType->mSize)
				{
				case 1: ptrType->mCvDefTypeId = T_64PUCHAR; break;
				case 2: ptrType->mCvDefTypeId = T_64PCHAR16; break;
				case 4: ptrType->mCvDefTypeId = T_64PCHAR32; break;
				}
				break;
			case llvm::dwarf::DW_ATE_boolean:
				ptrType->mCvDefTypeId = T_64PBOOL08;
				break;
			}
			if (ptrType->mCvDefTypeId != -1)
			{
				ptrType->mCvDeclTypeId = ptrType->mCvDefTypeId;
				return ptrType->mCvDefTypeId;
			}
		}

		lfPointerBody::lfPointerAttr attr = { 0 };
		attr.ptrtype = CV_PTR_64;
		attr.ptrmode = CV_PTR_MODE_PTR;
		attr.size = 8;
		int32 elementId = DbgGetTypeId(ptrType->mElement);

		DbgTStartTag();
		outT.Write((int16)LF_POINTER);				
		outT.Write(elementId);
		outT.Write(*(int32*)&attr);
		DbgTEndTag();		
		dbgType->mCvDeclTypeId = dbgType->mCvDefTypeId = mCurTagId++;
		return dbgType->mCvDefTypeId;
	}
	else if (auto refType = BeValueDynCast<BeDbgReferenceType>(dbgType))
	{
		lfPointerBody::lfPointerAttr attr = { 0 };
		attr.ptrtype = CV_PTR_64;
		attr.ptrmode = CV_PTR_MODE_LVREF;
		attr.size = 8;
		int32 elementId = DbgGetTypeId(refType->mElement);

		DbgTStartTag();
		outT.Write((int16)LF_POINTER);		
		outT.Write(elementId);
		outT.Write(*(int32*)&attr);		
		DbgTEndTag();
		dbgType->mCvDeclTypeId = dbgType->mCvDefTypeId = mCurTagId++;
		return dbgType->mCvDefTypeId;
	}
	else if (auto constType = BeValueDynCast<BeDbgConstType>(dbgType))
	{	
		CV_modifier_t attr = { 0 };		
		attr.MOD_const = 1;
		int32 elementId = DbgGetTypeId(constType->mElement);

		DbgTStartTag();
		outT.Write((int16)LF_MODIFIER);		
		outT.Write(elementId);
		outT.Write(*(int16*)&attr);		
		DbgTEndTag();
		dbgType->mCvDeclTypeId = dbgType->mCvDefTypeId = mCurTagId++;
		return dbgType->mCvDefTypeId;
	}
	else if (auto artificialType = BeValueDynCast<BeDbgArtificialType>(dbgType))
	{
		return DbgGetTypeId(artificialType->mElement);
	}
	else if (auto arrayType = BeValueDynCast<BeDbgArrayType>(dbgType))
	{
		int32 elementId = DbgGetTypeId(arrayType->mElement);
		
		DbgTStartTag();
		outT.Write((int16)LF_ARRAY);
		outT.Write((int32)elementId);
		outT.Write((int32)0x23/*int64*/);
		DbgEncodeConstant(outT, BF_ALIGN(arrayType->mSize, arrayType->mAlign));
		DbgEncodeString(outT, "");
		DbgTEndTag();
		dbgType->mCvDeclTypeId = dbgType->mCvDefTypeId = mCurTagId++;
		return dbgType->mCvDefTypeId;
	}	
	else
		BF_FATAL("NotImpl");


	BF_FATAL("Invalid type");
	return -1;
}

void BeCOFFObject::DbgGenerateTypeInfo()
{
	BP_ZONE("BeCOFFObject::DbgGenerateTypeInfo");
	AutoPerf perf("BeCOFFObject::DbgGenerateTypeInfo", mPerfManager);

	auto& outT = mDebugTSect.mData;
	outT.Write((int)CV_SIGNATURE_C13);

	for (auto dbgType : mBeModule->mDbgModule->mTypes)
	{		
		bool defineType = true;
		if (auto dbgStructType = BeValueDynCast<BeDbgStructType>(dbgType))
		{
			if (!dbgStructType->mIsFullyDefined)
				defineType = false;			
		}

		if (defineType)
		{
			DbgGetTypeId(dbgType, true);
		}
	}

	for (auto dbgFunc : mBeModule->mDbgModule->mFuncs)
	{
		if (dbgFunc->mValue != NULL)
			DbgMakeFunc(dbgFunc);
	}
}

void BeCOFFObject::DbgStartSection(int sectionNum)
{
	auto& outS = mDebugSSect.mData;	
	BF_ASSERT(mSectionStartPos == -1);
	
	outS.Write((int32)sectionNum);
	outS.Write(0); // Temporary - size
	mSectionStartPos = outS.GetPos();
}

void BeCOFFObject::DbgEndSection()
{
	auto& outS = mDebugSSect.mData;	
	int totalLen = outS.GetPos() - mSectionStartPos;
	*((int32*)&outS.mData[mSectionStartPos - 4]) = totalLen;	
	mSectionStartPos = -1;
	while ((outS.GetPos() & 3) != 0)
		outS.Write((uint8)0);
}

void BeCOFFObject::DbgStartVarDefRange(BeDbgFunction* dbgFunc, BeDbgVariable* dbgVar, const BeDbgVariableLoc& varLoc, int offset, int range)
{
	BF_ASSERT(range >= 0);
	
	auto funcSym = GetSymbol(dbgFunc->mValue);

	auto& outS = mDebugSSect.mData;	
	if (varLoc.mKind == BeDbgVariableLoc::Kind_SymbolAddr)
	{
		BF_FATAL("Not supported");
	}
	else if (varLoc.mKind == BeDbgVariableLoc::Kind_Reg)
	{
		if (varLoc.mOfs == 0)
		{
			outS.Write((int16)S_DEFRANGE_REGISTER);
			outS.Write((int16)GetCVRegNum(varLoc.mReg, dbgVar->mType->mSize * 8));
			CV_RANGEATTR rangeAttr = { 0 };
			outS.Write(*(int16*)&rangeAttr); // offset to register
		}
		else
		{
			outS.Write((int16)S_DEFRANGE_REGISTER_REL);
			outS.Write((int16)GetCVRegNum(varLoc.mReg, dbgVar->mType->mSize * 8));
			outS.Write((int16)0);
			outS.Write((int32)varLoc.mOfs);
// 			CV_RANGEATTR rangeAttr = { 0 };
// 			outS.Write(*(int16*)&rangeAttr); // offset to register
		}
	}
	else //if (varLoc.mKind == BeDbgVariableLoc::Kind_Indexed)
	{
		outS.Write((int16)S_DEFRANGE_REGISTER_REL);
		outS.Write((int16)GetCVRegNum(varLoc.mReg, 64));
		outS.Write((int16)0); // offset in parent
		outS.Write((int32)varLoc.mOfs); // offset to register
	}

	BeMCRelocation reloc;
	reloc.mKind = BeMCRelocationKind_SECREL;
	reloc.mOffset = outS.GetPos();
	reloc.mSymTableIdx = funcSym->mIdx;
	mDebugSSect.mRelocs.push_back(reloc);
	outS.Write((int32)offset); // offset start

	reloc.mKind = BeMCRelocationKind_SECTION;
	reloc.mOffset = outS.GetPos();
	reloc.mSymTableIdx = funcSym->mIdx;
	mDebugSSect.mRelocs.push_back(reloc);
	outS.Write((int16)0); // section
	outS.Write((int16)range); // Range	
}


void BeCOFFObject::DbgEndLineBlock(BeDbgFunction* dbgFunc, const Array<BeDbgCodeEmission>& emissions, int blockStartPos, int emissionStartIdx, int lineCount)
{
	auto& outS = mDebugSSect.mData;

	int addLineCount = 0;
	if ((emissionStartIdx == 0) && (dbgFunc->mLine != -1))
	{
		outS.Write((int16)0);
		outS.Write((int16)0);
		addLineCount++;
	}

	for (int emissionIdx = emissionStartIdx; emissionIdx < emissionStartIdx + lineCount; emissionIdx++)
	{
		auto& codeEmission = emissions[emissionIdx];
		// Start column
		outS.Write((int16)(codeEmission.mDbgLoc->mColumn + 1));
		// End column
		outS.Write((int16)(codeEmission.mDbgLoc->mColumn + 1));
	}

	*(int*)(&outS.mData[blockStartPos] + 4) = lineCount + addLineCount;
	*(int*)(&outS.mData[blockStartPos] + 8) = outS.GetPos() - blockStartPos;
}

void BeCOFFObject::DbgSAlign()
{
	int curPos = mDebugSSect.mData.GetPos();
	// Perform alignment	
	int addPadding = (4 - (curPos & 3)) % 4;
	while (addPadding > 0)
	{
		mDebugSSect.mData.Write((uint8)0);
		addPadding--;
	}
}

void BeCOFFObject::DbgSStartTag()
{
	BF_ASSERT(mSTagStartPos == -1);
	mSTagStartPos = mDebugSSect.mData.GetPos();
	mDebugSSect.mData.Write((int16)0);
}

void BeCOFFObject::DbgSEndTag()
{
	BF_ASSERT(mSTagStartPos != -1);
	int tagSize = mDebugSSect.mData.GetPos() - mSTagStartPos;

	*((uint16*)&mDebugSSect.mData.mData[mSTagStartPos]) = (uint16)(tagSize - 2);
	mSTagStartPos = -1;
}

void BeCOFFObject::DbgOutputLocalVar(BeDbgFunction* dbgFunc, BeDbgVariable* dbgVar)
{	
	// CodeView only allows 16-bit lengths, so we need to split ranges for very long spans
	if (dbgVar->mDeclEnd - dbgVar->mDeclStart > 0xFFFF)
	{		
		int splitPos = dbgVar->mDeclStart + 0xFFFF;

		BeDbgVariable varStart = *dbgVar;
		varStart.mDeclEnd = splitPos;
		Array<BeDbgVariableRange>* startArrs[2] = { &varStart.mSavedRanges, &varStart.mGaps };
		for (auto& arr : startArrs)
		{			
			for (int arrIdx = 0; arrIdx < (int)arr->size(); arrIdx++)
			{
				auto& gap = (*arr)[arrIdx];
				
				int gapStart = gap.mOffset;
				int gapEnd = gap.mOffset + gap.mLength;

				if (gapStart > splitPos)
					gapStart = splitPos;
				if (gapEnd > splitPos)
					gapEnd = splitPos;

				if (gapStart == gapEnd)
				{
					arr->RemoveAt(arrIdx);
					arrIdx--;
				}
				else
				{
					gap.mOffset = gapStart;
					gap.mLength = gapEnd - gapStart;
					BF_ASSERT(gap.mLength > 0);
				}
			}
		}
		DbgOutputLocalVar(dbgFunc, &varStart);

		BeDbgVariable varEnd = *dbgVar;
		varEnd.mDeclStart = splitPos;
		Array<BeDbgVariableRange>* endArrs[2] = { &varEnd.mSavedRanges, &varEnd.mGaps };
		for (auto& arr : endArrs)
		{
			for (int arrIdx = 0; arrIdx < (int)arr->size(); arrIdx++)
			{
				auto& gap = (*arr)[arrIdx];

				int gapStart = gap.mOffset;				
				if (gapStart < splitPos)
					gapStart = splitPos;

				if (gap.mLength == -1)
				{					
					gap.mOffset = gapStart;
					continue;
				}

				int gapEnd = gap.mOffset + gap.mLength;				
				if (gapEnd < splitPos)
					gapEnd = splitPos;

				if (gapStart == gapEnd)
				{
					arr->RemoveAt(arrIdx);
					arrIdx--;
				}
				else
				{
					gap.mOffset = gapStart;
					gap.mLength = gapEnd - gapStart;
					BF_ASSERT(gap.mLength > 0);
				}
			}
		}
		DbgOutputLocalVar(dbgFunc, &varEnd);
	}

	auto& outS = mDebugSSect.mData;

	DbgSStartTag();
	outS.Write((int16)S_LOCAL);
	outS.Write(DbgGetTypeId(dbgVar->mType));
	CV_LVARFLAGS flags = { 0 };

	if (dbgVar->mParamNum != -1)
		flags.fIsParam = 1;

	outS.Write(*(int16*)&flags);

	bool isConst = false;
	String varName = dbgVar->mName;	

	bool isGlobal = false;
	//
	{
		auto checkVal = dbgVar->mValue;
		
		if (auto beCast = BeValueDynCast<BeCastConstant>(checkVal))
			checkVal = beCast->mTarget;
		if (auto beGlobal = BeValueDynCast<BeGlobalVariable>(checkVal))
		{
			isGlobal = true;
		}
	}

	if (!isGlobal)
	{
		if (auto beConst = BeValueDynCast<BeConstant>(dbgVar->mValue))
		{
			if ((beConst->mType != NULL) && (!beConst->mType->IsPointer()))
			{
				int64 writeVal = beConst->mInt64;
				if (beConst->mType->mTypeCode == BfTypeCode_Single)
				{
					// We need to do this because Singles are stored in mDouble, so we need to reduce here
					float floatVal = (float)beConst->mDouble;
					writeVal = *(uint32*)&floatVal;
				}
				if (writeVal < 0)
					varName += StrFormat("$_%llu", -writeVal);
				else
					varName += StrFormat("$%llu", writeVal);
				isConst = true;
			}
		}
	}

	DbgEncodeString(outS, varName);
	DbgSEndTag();

	// TODO: Split these up into multiple variables if we cover more than 0xF000 bytes...
	DbgSStartTag();
	/*if (dbgVar->mDeclEnd == -1)
		dbgVar->mDeclEnd = dbgFunc->mCodeLen;*/
	int declEnd = dbgVar->mDeclEnd;
	if (declEnd == -1)
	{
		declEnd = dbgFunc->mCodeLen;
		if ((dbgVar->mGaps.size() == 1) && (dbgVar->mGaps[0].mOffset == dbgVar->mDeclStart) && (dbgVar->mGaps[0].mLength == -1) && (!isConst))
		{			
			// Variable not used			
			declEnd = dbgVar->mDeclStart;
		}
	}
	else if (dbgVar->mDeclLifetimeExtend)
		declEnd++;
	DbgStartVarDefRange(dbgFunc, dbgVar, dbgVar->mPrimaryLoc, dbgVar->mDeclStart, declEnd - dbgVar->mDeclStart);
	if (!isConst)
	{
		for (auto gap : dbgVar->mGaps)
		{
			if (gap.mLength == 0)
				continue;

			if (gap.mLength == -1)
			{				
				// Not a real gap, and not an unused variable indicator
				if (gap.mOffset > dbgVar->mDeclStart)
					continue;
				if (gap.mOffset == dbgVar->mDeclEnd)
					continue;
			}

			outS.Write((int16)(gap.mOffset - dbgVar->mDeclStart));
			if (gap.mLength == -1)
			{
				// This means we never even used the variable
				BF_ASSERT(gap.mOffset == dbgVar->mDeclStart);
				outS.Write((int16)(declEnd - gap.mOffset));
			}
			else
			{
				outS.Write((int16)gap.mLength);
			}
		}
	}
	DbgSEndTag(); // S_DEFRANGE_X

	if (!dbgVar->mSavedRanges.empty())
	{
		auto& lastSavedRange = dbgVar->mSavedRanges.back();
		if (lastSavedRange.mLength == -1)
		{
			// This can happen if our variable dies before a saved register restore
			lastSavedRange.mLength = dbgFunc->mCodeLen - lastSavedRange.mOffset;
		}

		auto savedDeclStart = dbgVar->mSavedRanges[0].mOffset;

		// If there is only one value in mSavedRanges, then encode this as just that single span with no gaps
		//  Otherwise add gap information for the space between the mSavedRanges values
		DbgSStartTag();
		DbgStartVarDefRange(dbgFunc, dbgVar, dbgVar->mSavedLoc, savedDeclStart,
			(lastSavedRange.mOffset + lastSavedRange.mLength) - savedDeclStart);
		for (int saveGapIdx = 0; saveGapIdx < (int)dbgVar->mSavedRanges.size() - 1; saveGapIdx++)
		{
			auto& saveRange0 = dbgVar->mSavedRanges[saveGapIdx];
			auto& saveRange1 = dbgVar->mSavedRanges[saveGapIdx + 1];
			outS.Write((int16)(saveRange0.mOffset + saveRange0.mLength - savedDeclStart));
			outS.Write((int16)(saveRange1.mOffset - (saveRange0.mOffset + saveRange0.mLength)));
		}
		DbgSEndTag(); // S_DEFRANGE_X
	}
}

void BeCOFFObject::DbgOutputLocalVars(BeInlineLineBuilder* curInlineBuilder, BeDbgFunction* dbgFunc)
{
	auto& outS = mDebugSSect.mData;
	for (auto dbgVar : curInlineBuilder->mVariables)
	{	
		if (dbgVar == NULL)
			continue;
		DbgOutputLocalVar(dbgFunc, dbgVar);
	}
}

void BeCOFFObject::DbgGenerateModuleInfo()
{
	BP_ZONE("BeCOFFObject::DbgGenerateModuleInfo");
	AutoPerf perf("BeCOFFObject::DbgGenerateModuleInfo", mPerfManager);

	auto& outS = mDebugSSect.mData;
	outS.Write((int)CV_SIGNATURE_C13);
		
	Array<int> fileDataPositions;
	Array<BeDbgFunction*> inlinees;
		
	// Funcs
	for (auto dbgFunc : mBeModule->mDbgModule->mFuncs)
	{
		if (dbgFunc->mValue == NULL)
			continue;		

		if (dbgFunc->mCvFuncId == -1)
			continue;

		BF_ASSERT(dbgFunc->mCvFuncId != -1);

		auto funcSym = GetSymbol(dbgFunc->mValue, false);
		if (funcSym == NULL)
			continue;
				
		DbgStartSection(DEBUG_S_SYMBOLS);
		
		DbgSStartTag();
		if (dbgFunc->mValue->mLinkageType == BfIRLinkageType_Internal)
			outS.Write((int16)S_LPROC32_ID);
		else
			outS.Write((int16)S_GPROC32_ID);
		outS.Write((int32)0); // PtrParent
		outS.Write((int32)0); // PtrEnd
		outS.Write((int32)0); // PtrNext
		outS.Write((int32)BF_MAX(dbgFunc->mCodeLen, 0)); // CodeSize
		outS.Write((int32)0); // DbgStart
		outS.Write((int32)0); // DbgEnd
		outS.Write(dbgFunc->mCvFuncId);		

		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_SECREL;
		reloc.mOffset = outS.GetPos();
		reloc.mSymTableIdx = funcSym->mIdx;
		mDebugSSect.mRelocs.push_back(reloc);
		outS.Write((int32)0); // off
		
		reloc.mKind = BeMCRelocationKind_SECTION;
		reloc.mOffset = outS.GetPos();
		reloc.mSymTableIdx = funcSym->mIdx;
		mDebugSSect.mRelocs.push_back(reloc);
		outS.Write((int16)0); // seg

		CV_PROCFLAGS procFlags = { 0 };
		outS.Write(*(uint8*)&procFlags);

		String fullName;
		if ((BeValueDynCast<BeDbgNamespace>(dbgFunc->mScope) != NULL) ||
			(BeValueDynCast<BeDbgStructType>(dbgFunc->mScope) != NULL))
			ToString(dbgFunc->mScope, fullName);
		if (!fullName.empty())
			fullName += "::";
		fullName += dbgFunc->mName;
		DbgEncodeString(outS, fullName);
		DbgSEndTag();		

		BeInlineLineBuilder* curInlineBuilder = NULL;
		BeDbgLoc* curDbgLoc = NULL;

		OwnedVector<BeInlineLineBuilder> inlineBuilders;
		Array<BeInlineLineBuilder*> inlineStack;
		Dictionary<BeDbgLoc*, BeInlineLineBuilder*> inlineMap;

		// Build inline table
		for (int emissionIdx = 0; emissionIdx < (int)dbgFunc->mEmissions.size(); emissionIdx++)
		{
			auto& codeEmission = dbgFunc->mEmissions[emissionIdx];
			auto newDbgLoc = codeEmission.mDbgLoc;
			if (curDbgLoc != newDbgLoc)
			{
				curDbgLoc = newDbgLoc;
				int newInlineDepth = newDbgLoc->GetInlineDepth();				
				int curInlineDepth = 0;
				if (curInlineBuilder != NULL)
					curInlineDepth = curInlineBuilder->mStartDbgLoc->GetInlineDepth();
				
				int depthMatch = 0;
				if (curInlineBuilder != NULL)
	                depthMatch = curDbgLoc->GetInlineMatchDepth(curInlineBuilder->mStartDbgLoc);				
				while (curInlineDepth > depthMatch)
				{					
					curInlineBuilder->End(&codeEmission);
					inlineStack.pop_back();
					if (inlineStack.empty())
						curInlineBuilder = NULL;
					else
						curInlineBuilder = inlineStack.back();					
					curInlineDepth--;
				}

				// Check for new inlines
				while (newInlineDepth > curInlineDepth)
				{					
					auto inlineBuilder = inlineBuilders.Alloc<BeInlineLineBuilder>();
					// If we add more than one inline depth at a time then we need to set startDbgLoc appropriately
					int inlineIdx = newInlineDepth - curInlineDepth - 2;
					if (inlineIdx == -1)
						inlineBuilder->mStartDbgLoc = codeEmission.mDbgLoc;
					else
						inlineBuilder->mStartDbgLoc = codeEmission.mDbgLoc->GetInlinedAt(inlineIdx);

					auto dbgFunc = inlineBuilder->mStartDbgLoc->GetDbgFunc();
					if (!inlinees.Contains(dbgFunc))
						inlinees.Add(dbgFunc);

					inlineBuilder->mCurLine = dbgFunc->mLine;
					inlineBuilder->Start(&codeEmission);					
					curInlineBuilder = inlineBuilder;
					inlineStack.push_back(curInlineBuilder);
					curInlineDepth++;

					BF_ASSERT(inlineBuilder->mStartDbgLoc->mDbgInlinedAt != NULL);
					inlineMap[inlineBuilder->mStartDbgLoc->mDbgInlinedAt] = inlineBuilder;
				}

				if (curInlineBuilder != NULL)
					curInlineBuilder->Update(&codeEmission);				
			}			
		}
		BF_ASSERT(inlineStack.empty());

		for (auto dbgVar : dbgFunc->mVariables)
		{
			if ((dbgVar == NULL) || (dbgVar->mDeclDbgLoc == NULL))							
				continue;
			if (dbgVar->mDeclDbgLoc->mDbgInlinedAt == NULL)
				continue;
			
			BeInlineLineBuilder* inlineBuilder = NULL;
			if (inlineMap.TryGetValue(dbgVar->mDeclDbgLoc->mDbgInlinedAt, &inlineBuilder))
			{
				//auto inlineBuilder = itr->second;
				inlineBuilder->mVariables.push_back(dbgVar);
			}
		}

		// Emit inlines and variables
		int inlineBuilderIdx = 0;
				
		curInlineBuilder = NULL;
		
		for (auto dbgVar : dbgFunc->mVariables)
		{			
			if ((dbgVar == NULL) || (dbgVar->mDeclDbgLoc == NULL))							
				continue;
			if (dbgVar->mDeclDbgLoc->mDbgInlinedAt == NULL)
				DbgOutputLocalVar(dbgFunc, dbgVar);
		}

		while ((inlineBuilderIdx < (int)inlineBuilders.size()) || (curInlineBuilder != NULL) /*|| (varIdx < (int)dbgFunc->mVariables.size())*/)
		{	
			BeInlineLineBuilder* newInlineBuilder = NULL;

			int curInlineDepth = 0;
			if (curInlineBuilder != NULL)
				curInlineDepth = curInlineBuilder->mStartDbgLoc->GetInlineDepth();

			int newInlineDepth = 0;
			if (inlineBuilderIdx < (int)inlineBuilders.size())
			{
				newInlineBuilder = inlineBuilders[inlineBuilderIdx++];
				newInlineDepth = newInlineBuilder->mStartDbgLoc->GetInlineDepth();
			}

			int depthMatch = 0;
			if ((curInlineBuilder != NULL) && (newInlineBuilder != NULL))
				depthMatch = curInlineBuilder->mStartDbgLoc->GetInlineMatchDepth(newInlineBuilder->mStartDbgLoc);
			if ((curInlineDepth == newInlineDepth) && (depthMatch == newInlineDepth) && (depthMatch > 0))
			{
				// If we inline two of the same methods on the same line then its possible they share the same inlined position
				//  But we know there is a unique item here
				depthMatch--;
			}
			while (curInlineDepth > depthMatch)
			{
				DbgSStartTag();
				outS.Write((int16)S_INLINESITE_END);
				DbgSEndTag();
				curInlineDepth--;
				inlineStack.pop_back();
				if (inlineStack.empty())
					curInlineBuilder = NULL;
				else
					curInlineBuilder = inlineStack.back();				
			}

			if (newInlineDepth > curInlineDepth)
			{
				BF_ASSERT(newInlineDepth == curInlineDepth + 1);
				DbgSStartTag();
				outS.Write((int16)S_INLINESITE);
				outS.Write((int32)0); // pParent
				outS.Write((int32)0); // pEnd
				auto inlinedDbgFunc = newInlineBuilder->mStartDbgLoc->GetDbgFunc();
				if (inlinedDbgFunc->mCvFuncId == -1)
					DbgMakeFunc(inlinedDbgFunc);
				outS.Write(inlinedDbgFunc->mCvFuncId);				
				outS.Write(&newInlineBuilder->mData[0], (int)newInlineBuilder->mData.size());
				DbgSEndTag();
				newInlineDepth++;
				inlineStack.push_back(newInlineBuilder);

				curInlineBuilder = newInlineBuilder;
				DbgOutputLocalVars(curInlineBuilder, dbgFunc);
			}

			// This can fail if an inlined method is not emitted contiguously, or if multiple copies of the same method
			//  get inlined at exactly the same DbgLoc -- which isn't possible in Beef
			BF_ASSERT(curInlineBuilder == newInlineBuilder);
		}		
		
		DbgSStartTag();
		outS.Write((int16)S_PROC_ID_END);
		DbgSEndTag();

		DbgEndSection(); // DEBUG_S_SYMBOLS

		if (dbgFunc->mEmissions.empty())
			continue;

		DbgStartSection(DEBUG_S_LINES);

		reloc.mKind = BeMCRelocationKind_SECREL;
		reloc.mOffset = outS.GetPos();
		reloc.mSymTableIdx = funcSym->mIdx;
		mDebugSSect.mRelocs.push_back(reloc);
		outS.Write((int32)0x0); // offset contribution

		reloc.mKind = BeMCRelocationKind_SECTION;
		reloc.mOffset = outS.GetPos();
		reloc.mSymTableIdx = funcSym->mIdx;
		mDebugSSect.mRelocs.push_back(reloc);
		outS.Write((int16)0); // section contribution

		int16 flags = CV_LINES_HAVE_COLUMNS;
		outS.Write((int16)flags);
		outS.Write(dbgFunc->mCodeLen); // contribution size

		// Iterate over lines
		// int32 offFile (file num)
		// int32 nLines
		// int32 cbBlock

		curDbgLoc = NULL;
		BeDbgFile* curFile = NULL;
		int lastBlockStartPos = -1;		
		int lineCount = 0;		

		Array<BeDbgCodeEmission> emissions;
		emissions.Reserve(dbgFunc->mEmissions.size());		
		for (int emissionIdx = 0; emissionIdx < (int)dbgFunc->mEmissions.size(); emissionIdx++)
		{
			auto& codeEmission = dbgFunc->mEmissions[emissionIdx];
			auto rootDbgLoc = codeEmission.mDbgLoc->GetRoot();
			
			bool doEmission = true;
			if (!emissions.empty())
			{
				if (rootDbgLoc == emissions.back().mDbgLoc)
					doEmission = false;				
			}

			if (doEmission)
			{
				BeDbgCodeEmission newEmission;
				newEmission.mDbgLoc = rootDbgLoc;
				newEmission.mPos = codeEmission.mPos;
				emissions.push_back(newEmission);
			}
		}
		
		///
		{
			int fileDataPos = 0;
			for (auto dbgFile : mBeModule->mDbgModule->mFiles)
			{
				fileDataPositions.Add(fileDataPos);
				fileDataPos += 4;
				if (dbgFile->mMD5Hash.IsZero())
					fileDataPos += 4;
				else
					fileDataPos += 20;
			}
		}

		int emissionStartIdx = 0;
		BeDbgFile* curDbgFile = NULL;
		for (int emissionIdx = 0; emissionIdx < (int)emissions.size(); emissionIdx++)
		{
			auto& codeEmission = emissions[emissionIdx];
			
			auto dbgLoc = codeEmission.mDbgLoc;
			BeDbgFile* dbgFile = dbgLoc->GetDbgFile();

			if (dbgFile != curDbgFile)
			{
				if (curDbgLoc != NULL)
				{
					DbgEndLineBlock(dbgFunc, emissions, lastBlockStartPos, emissionStartIdx, lineCount);
					lineCount = 0;
					emissionStartIdx = emissionIdx;
				}

				curDbgLoc = dbgLoc;				
				curDbgFile = dbgFile;
				
				lastBlockStartPos = outS.GetPos();
				outS.Write((int32)fileDataPositions[dbgFile->mIdx]);
				outS.Write((int32)0); // placeholder nLines
				outS.Write((int32)0); // placeholder cbBlock

				if ((emissionIdx == 0) && (dbgFunc->mLine != -1))
				{
					outS.Write((int32)0);
					outS.Write((int32)dbgFunc->mLine + 1);
				}
			}

			outS.Write((int32)codeEmission.mPos);
			outS.Write((int32)codeEmission.mDbgLoc->mLine + 1);
			lineCount++;
		}
		if (curDbgLoc != NULL)
			DbgEndLineBlock(dbgFunc, emissions, lastBlockStartPos, emissionStartIdx, lineCount);
		DbgEndSection(); // DEBUG_S_LINES
	}	

	if (!inlinees.empty())
	{		
		DbgStartSection(DEBUG_S_INLINEELINES);
		outS.Write((int32)0); // Lines type
		for (auto inlinedDbgFunc : inlinees)
		{						
			BF_ASSERT(inlinedDbgFunc->mCvFuncId != -1);			
			outS.Write(inlinedDbgFunc->mCvFuncId);
			
			auto dbgFile = inlinedDbgFunc->mFile;
			outS.Write((int32)fileDataPositions[dbgFile->mIdx]);
			outS.Write((int32)inlinedDbgFunc->mLine + 1);
		}
		DbgEndSection();
	}	

	// Global variables	
	{
		bool startedSymbols = false;		
		for (auto dbgGlobalVar : mBeModule->mDbgModule->mGlobalVariables)
		{	
			auto gvSym = GetSymbol(dbgGlobalVar->mValue);
			if (gvSym == NULL)
			{
				//TODO: Is this an error?
				continue;
			}

			if (!startedSymbols)
			{				
				DbgStartSection(DEBUG_S_SYMBOLS);
				startedSymbols = true;
			}

			DbgSStartTag();
			bool isTLS = false;
			if (auto beGlobalVar = BeValueDynCast<BeGlobalVariable>(dbgGlobalVar->mValue))			
				isTLS = beGlobalVar->mIsTLS;
			
			if (isTLS)
				outS.Write(dbgGlobalVar->mIsLocalToUnit ? (int16)S_LTHREAD32 : (int16)S_GTHREAD32);
			else
				outS.Write(dbgGlobalVar->mIsLocalToUnit ? (int16)S_LDATA32 : (int16)S_GDATA32);
			
			outS.Write(DbgGetTypeId(dbgGlobalVar->mType));

			BF_ASSERT(dbgGlobalVar->mValue != NULL);			

			BeMCRelocation reloc;
			reloc.mKind = BeMCRelocationKind_SECREL;
			reloc.mOffset = outS.GetPos();
			reloc.mSymTableIdx = gvSym->mIdx;
			mDebugSSect.mRelocs.push_back(reloc);
			outS.Write((int32)0x0); // offset contribution

			reloc.mKind = BeMCRelocationKind_SECTION;
			reloc.mOffset = outS.GetPos();
			reloc.mSymTableIdx = gvSym->mIdx;
			mDebugSSect.mRelocs.push_back(reloc);
			outS.Write((int16)0); // section contribution

			DbgEncodeString(outS, dbgGlobalVar->mName);

			DbgSEndTag();
		}

		if (startedSymbols)
			DbgEndSection(); // DEBUG_S_SYMBOLS
	}		

	bool startedUDT = false;
	for (auto dbgType : mBeModule->mDbgModule->mTypes)
	{
		if (auto dbgStructType = BeValueDynCast<BeDbgStructType>(dbgType))
		{
			if (dbgStructType->mIsFullyDefined)
			{
				if (!startedUDT)
				{
					DbgStartSection(DEBUG_S_SYMBOLS);
					startedUDT = true;
				}

				DbgSStartTag();
				outS.Write((int16)S_UDT);
				outS.Write(DbgGetTypeId(dbgStructType));
				String fullName;
				ToString(dbgStructType, fullName);
				DbgEncodeString(outS, fullName);
				DbgSEndTag();
			}
		}
	}
	if (startedUDT)
		DbgEndSection();

	DbgStartSection(DEBUG_S_FILECHKSMS);
	Array<char> strTable;
	strTable.push_back(0);
	for (auto dbgFile : mBeModule->mDbgModule->mFiles)
	{
		outS.Write((int32)strTable.size());

		if (dbgFile->mMD5Hash.IsZero())
		{
			outS.Write((int32)0); // hashLen, hashType, padding	
		}
		else
		{
			outS.Write((uint8)16); // hashLen
			outS.Write((uint8)1); // hashType
			outS.Write(&dbgFile->mMD5Hash, 16);
			outS.Write((int8)0); // padding
			outS.Write((int8)0);
		}

		String fullPath;
		dbgFile->ToString(fullPath);
		strTable.Insert(strTable.size(), &fullPath[0], fullPath.length());
		strTable.push_back(0);
	}
	DbgEndSection();

	DbgStartSection(DEBUG_S_STRINGTABLE);
	outS.Write(&strTable[0], (int)strTable.size());
	DbgSAlign();
	DbgEndSection();
}

void BeCOFFObject::InitSect(BeCOFFSection& sect, const StringImpl& name, int characteristics, bool addNow, bool makeSectSymbol)
{
	sect.mSectName = name;
	sect.mCharacteristics = characteristics;
	if (addNow)
		MarkSectionUsed(sect, makeSectSymbol);
}

void BeCOFFObject::WriteConst(BeCOFFSection& sect, BeConstant* constVal)
{	
	auto beType = constVal->GetType();
	sect.mAlign = BF_MAX(sect.mAlign, beType->mAlign);
	sect.mData.Align(beType->mAlign);

	if (auto globalVar = BeValueDynCast<BeGlobalVariable>(constVal))
	{
		auto sym = GetSymbol(globalVar);

		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_ADDR64;
		reloc.mOffset = sect.mData.GetPos();
		reloc.mSymTableIdx = sym->mIdx;
		sect.mRelocs.push_back(reloc);
		sect.mData.Write((int64)0);
	}
	else if (auto beFunc = BeValueDynCast<BeFunction>(constVal))
	{
		auto sym = GetSymbol(beFunc);

		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_ADDR64;
		reloc.mOffset = sect.mData.GetPos();
		reloc.mSymTableIdx = sym->mIdx;
		sect.mRelocs.push_back(reloc);
		sect.mData.Write((int64)0);
	}
	else if (auto constStruct = BeValueDynCast<BeStructConstant>(constVal))
	{
		int startOfs = sect.mData.GetSize();
		if (constStruct->mType->mTypeCode == BeTypeCode_Struct)
		{
			BeStructType* structType = (BeStructType*)constStruct->mType;
			BF_ASSERT(structType->mMembers.size() == constStruct->mMemberValues.size());
			for (int memberIdx = 0; memberIdx < (int)constStruct->mMemberValues.size(); memberIdx++)
			{
				auto& member = structType->mMembers[memberIdx];
				// Do any per-member alignment
				sect.mData.WriteZeros(member.mByteOffset - (sect.mData.GetSize() - startOfs));
				WriteConst(sect, constStruct->mMemberValues[memberIdx]);
			}
			// Do end padding
			sect.mData.WriteZeros(structType->mSize - (sect.mData.GetSize() - startOfs));
		}
		else if (constStruct->mType->mTypeCode == BeTypeCode_SizedArray)
		{
			for (auto& memberVal : constStruct->mMemberValues)
				WriteConst(sect, memberVal);
		}
		else
			BF_FATAL("Invalid StructConst type");
	}
	else if (auto constArr = BeValueDynCast<BeStructConstant>(constVal))
	{
		for (auto member : constArr->mMemberValues)
		{
			WriteConst(sect, member);
		}
	}
	else if (auto constStr = BeValueDynCast<BeStringConstant>(constVal))
	{
		sect.mData.Write((void*)constStr->mString.c_str(), (int)constStr->mString.length() + 1);
	}
	else if (auto constCast = BeValueDynCast<BeCastConstant>(constVal))
	{
		WriteConst(sect, constCast->mTarget);
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
				dataOfs = arrayType->mElementType->mSize * constGep->mIdx1;
			}
			else
			{
				BF_FATAL("Invalid GEP");
			}

			auto sym = GetSymbol(globalVar);

			BeMCRelocation reloc;
			reloc.mKind = BeMCRelocationKind_ADDR64;
			reloc.mOffset = sect.mData.GetPos();
			reloc.mSymTableIdx = sym->mIdx;
			sect.mRelocs.push_back(reloc);
			sect.mData.Write((int64)dataOfs);
		}
		else
		{
			BF_FATAL("Invalid GEPConstant");
		}
	}
	else if ((beType->IsPointer()) && (constVal->mTarget != NULL))
	{
		WriteConst(sect, constVal->mTarget);
	}
	else if (beType->IsComposite())
	{
		BF_ASSERT(constVal->mInt64 == 0);
		
		int64 zero = 0;
		int sizeLeft = beType->mSize;
		while (sizeLeft > 0)
		{
			int writeSize = BF_MIN(sizeLeft, 8);
			sect.mData.Write(&zero, writeSize);
			sizeLeft -= writeSize;
		}
	}
	else
	{
		sect.mData.Write((void*)&constVal->mInt64, beType->mSize);
	}
}

namespace BeefyDbg64
{
	void TestCoff(void* tdata, int tdataSize, void* cuData, int cuDataSize);
}

void BeCOFFObject::Generate(BeModule* module)
{
	mBeModule = module;
	bool hasDebugInfo = module->mDbgModule != NULL;

	DynMemStream textSegData;

	InitSect(mTextSect, ".text", IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_16BYTES | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, true, true);
	InitSect(mDataSect, ".data", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, true, false);
	InitSect(mRDataSect, ".rdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, true, false);
	InitSect(mBSSSect, ".bss", IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, true, false);
	InitSect(mTLSSect, ".tls$", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, true, false);
	mBSSSect.mAlign = 4;
	InitSect(mXDataSect, ".xdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_READ, true, true);
	if (hasDebugInfo)
	{
		InitSect(mDebugSSect, ".debug$S", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ, true, false);
		InitSect(mDebugTSect, ".debug$T", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ, true, false);
	}
	InitSect(mPDataSect, ".pdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_READ, true, false);
	
	mTextSect.mData.mData.Reserve(4096);

	BfSizedVector<BeMCSymbol*, 32> globalVarSyms;
	for (int globalVarIdx = 0; globalVarIdx < (int)module->mGlobalVariables.size(); globalVarIdx++)
	{
		auto globalVar = module->mGlobalVariables[globalVarIdx];

		BeMCSymbol* sym = mSymbols.Alloc();
		sym->mType = globalVar->mType;
		sym->mName = globalVar->mName;
		sym->mIsStatic = globalVar->mLinkageType == BfIRLinkageType_Internal;
		sym->mSymKind = BeMCSymbolKind_External;
		sym->mIdx = (int)mSymbols.size() - 1;
		sym->mIsTLS = globalVar->mIsTLS;

		globalVarSyms.push_back(sym);
		mSymbolMap[globalVar] = sym;
	}

	for (int globalVarIdx = 0; globalVarIdx < (int)module->mGlobalVariables.size(); globalVarIdx++)
	{
		auto globalVar = module->mGlobalVariables[globalVarIdx];
		auto sym = globalVarSyms[globalVarIdx];

		if (globalVar->mInitializer != NULL)
		{
			if (globalVar->mAlign == -1)
				globalVar->mAlign = globalVar->mType->mAlign;

			BF_ASSERT(globalVar->mAlign != -1);

			if (globalVar->mIsConstant)
			{
				auto constVal = BeValueDynCast<BeConstant>(globalVar->mInitializer);

				MarkSectionUsed(mRDataSect);
				sym->mSectionNum = mRDataSect.mSectionIdx + 1;
				mRDataSect.mData.Align(globalVar->mAlign);
				mRDataSect.mAlign = BF_MAX(mRDataSect.mAlign, globalVar->mAlign);
				sym->mValue = mRDataSect.mData.GetSize();
				//mRDataSect.mSizeOverride += globalVar->mType->mSize;

				WriteConst(mRDataSect, constVal);
			}
			else if (globalVar->mIsTLS)
			{
				MarkSectionUsed(mTLSSect);
				sym->mSectionNum = mTLSSect.mSectionIdx + 1;
				mTLSSect.mSizeOverride = (mTLSSect.mSizeOverride + globalVar->mAlign - 1) & ~(globalVar->mAlign - 1);
				mTLSSect.mAlign = BF_MAX(mTLSSect.mAlign, globalVar->mAlign);
				sym->mValue = mTLSSect.mSizeOverride;
				mTLSSect.mSizeOverride += globalVar->mType->mSize;
			}
			else if (auto funcVal = BeValueDynCast<BeFunction>(globalVar->mInitializer))
			{
				auto& sect = mDataSect;
				MarkSectionUsed(sect);
				sym->mSectionNum = sect.mSectionIdx + 1;
				sect.mData.Align(globalVar->mAlign);
				sect.mAlign = BF_MAX(sect.mAlign, globalVar->mAlign);
				sym->mValue = sect.mData.GetSize();

				auto sym = GetSymbol(funcVal);

				BeMCRelocation reloc;
				reloc.mKind = BeMCRelocationKind_ADDR64;
				reloc.mOffset = sect.mData.GetPos();
				reloc.mSymTableIdx = sym->mIdx;
				sect.mRelocs.push_back(reloc);
				sect.mData.Write((int64)0);
			}
			else
			{
				MarkSectionUsed(mBSSSect);
				sym->mSectionNum = mBSSSect.mSectionIdx + 1;
				mBSSSect.mSizeOverride = (mBSSSect.mSizeOverride + globalVar->mAlign - 1) & ~(globalVar->mAlign - 1);
				mBSSSect.mAlign = BF_MAX(mBSSSect.mAlign, globalVar->mAlign);
				sym->mValue = mBSSSect.mSizeOverride;
				mBSSSect.mSizeOverride += globalVar->mType->mSize;
			}
		}
	}

	for (int i = 0; i < mTLSSect.mSizeOverride; i++)
		mTLSSect.mData.Write((uint8)0);

	for (int funcIdx = 0; funcIdx < (int)module->mFunctions.size(); funcIdx++)
	{
		auto func = module->mFunctions[funcIdx];
		if ((!func->IsDecl()) && (func->mLinkageType == BfIRLinkageType_External))
		{
			GetSymbol(func);
		}
	}

	while (!mFuncWorkList.IsEmpty())
	{
		auto func = mFuncWorkList[0];
		mFuncWorkList.RemoveAt(0);
		
		module->mActiveFunction = func;
		if (!func->IsDecl())
		{			
			BeMCSymbol* sym = GetSymbol(func);
			BF_ASSERT(sym != NULL);
			sym->mValue = mTextSect.mData.GetSize();

			BeMCContext context(this);
			context.Generate(func);

			if (func->mIsDLLExport)
			{
				mDirectives += " ";
				mDirectives.Append("/EXPORT:");
				mDirectives.Append(func->mName);
			}
		}
	}

	if (!mDirectives.IsEmpty())
	{
		InitSect(mDirectiveSect, ".drectve", IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_ALIGN_1BYTES, true, false);
		mDirectiveSect.mData.Write((void*)mDirectives.c_str(), (int)mDirectives.length());
	}
	
	if (hasDebugInfo)
	{
		DbgGenerateTypeInfo();
		DbgGenerateModuleInfo();
	}

	{
		//BeefyDbg64::TestCoff(&mDebugTSect.mData.mData[0], mDebugTSect.mData.GetSize(), &mDebugSSect.mData.mData[0], mDebugSSect.mData.GetSize());
	}
}

bool BeCOFFObject::Generate(BeModule* module, const StringImpl& fileName)
{
	BP_ZONE_F("BeCOFFObject::Generate %s", fileName.c_str());
	AutoPerf perf("BeCOFFObject::Generate", mPerfManager);	

	if (mWriteToLib)
	{
		DynMemStream memStream;
		
		Generate(module);

		mStream = &memStream;
		Finish();
		mStream = NULL;

		BeLibEntry* libEntry = BeLibManager::Get()->AddFile(fileName, memStream.GetPtr(), memStream.GetSize());

		if (libEntry == NULL)
			return false;

		for (auto sym : mSymbols)
		{			
			if (sym->mIsStatic)
				continue;

			if (((sym->mSymKind == BeMCSymbolKind_External) && (sym->mSectionNum != 0)) ||
				((sym->mSymKind == BeMCSymbolKind_Function)))
			{				
				libEntry->AddSymbol(sym->mName);
			}
		}
	}
	else
	{
		SysFileStream fileStream;
		bool success = fileStream.Open(fileName, BfpFileCreateKind_CreateAlways, BfpFileCreateFlag_Write);
		if (!success)
			return false;

		Generate(module);

		mStream = &fileStream;
		Finish();
		mStream = NULL;

		{
			BP_ZONE("BeCOFFObject::Generate.fclose");
			AutoPerf perf("BeCOFFObject::Generate - fclose", mPerfManager);
			fileStream.Close();
		}
	}

	return true;
}

void BeCOFFObject::Finish()
{
	if (mBeModule->mModuleName.Contains("vdata"))
	{
		NOP;
	}

	BP_ZONE("BeCOFFObject::Finish");
	//AutoPerf perf("BeCOFFObject::Finish", mPerfManager);

	PEFileHeader header;
	memset(&header, 0, sizeof(header));
	header.mMachine = PE_MACHINE_X64;
	header.mTimeDateStamp = mTimestamp;

	header.mNumberOfSections = (int)mUsedSections.size();
	SizedArray<PESectionHeader, 16> sectHdrs;
	sectHdrs.resize(header.mNumberOfSections);
	SizedArray<BeCOFFSection*, 16> sectData;
	sectData.resize(header.mNumberOfSections);
	int filePos = sizeof(PEFileHeader) + header.mNumberOfSections*sizeof(PESectionHeader);

	for (int sectNum = 0; sectNum < header.mNumberOfSections; sectNum++)
	{
		PESectionHeader& sectHdr = sectHdrs[sectNum];
		memset(&sectHdr, 0, sizeof(sectHdr));
		BeCOFFSection* sect = mUsedSections[sectNum];
		strcpy(sectHdr.mName, sect->mSectName.c_str());
		
		int characteristics = sect->mCharacteristics;
		if (sect->mAlign != 0)
		{
			if (sect->mAlign == 8192) characteristics |= IMAGE_SCN_ALIGN_8192BYTES;
			else if (sect->mAlign == 4096) characteristics |= IMAGE_SCN_ALIGN_4096BYTES;
			else if (sect->mAlign == 2048) characteristics |= IMAGE_SCN_ALIGN_2048BYTES;
			else if (sect->mAlign == 1024) characteristics |= IMAGE_SCN_ALIGN_1024BYTES;
			else if (sect->mAlign == 512) characteristics |= IMAGE_SCN_ALIGN_512BYTES;
			else if (sect->mAlign == 256) characteristics |= IMAGE_SCN_ALIGN_256BYTES;
			else if (sect->mAlign == 128) characteristics |= IMAGE_SCN_ALIGN_128BYTES;
			else if (sect->mAlign == 64) characteristics |= IMAGE_SCN_ALIGN_64BYTES;
			else if (sect->mAlign == 32) characteristics |= IMAGE_SCN_ALIGN_32BYTES;
			else if (sect->mAlign == 16) characteristics |= IMAGE_SCN_ALIGN_16BYTES;
			else if (sect->mAlign == 8) characteristics |= IMAGE_SCN_ALIGN_8BYTES;
			else if (sect->mAlign == 4) characteristics |= IMAGE_SCN_ALIGN_4BYTES;
			else if (sect->mAlign == 2) characteristics |= IMAGE_SCN_ALIGN_2BYTES;
		}
		
		sectData[sectNum] = sect;		
		int dataSize = sect->mData.GetSize();
		if (dataSize != 0)
		{
			sectHdr.mPointerToRawData = filePos;
			sectHdr.mSizeOfRawData = dataSize;
			filePos += dataSize;

			if (!sect->mRelocs.empty())
			{
				sectHdr.mPointerToRelocations = filePos;
				if (sect->mRelocs.size() > 0xFFFF)
				{
					characteristics |= IMAGE_SCN_LNK_NRELOC_OVFL;

					// Extended reloc count
					sectHdr.mNumberOfRelocations = 0xFFFF;
					filePos += sizeof(COFFRelocation);
				}
				else
				{ 
					sectHdr.mNumberOfRelocations = (int)sect->mRelocs.size();
				}								
				filePos += (int)sect->mRelocs.size() * sizeof(COFFRelocation);
			}
		}
		else
		{
			sectHdr.mSizeOfRawData = sect->mSizeOverride;
		}

		sectHdr.mCharacteristics = characteristics;
		BF_ASSERT(characteristics != 0);
	}
	
	header.mPointerToSymbolTable = filePos;	
	header.mNumberOfSymbols = (int)mSymbols.size();
	mStream->WriteT(header);

	for (int sectNum = 0; sectNum < header.mNumberOfSections; sectNum++)
	{
		PESectionHeader& sectHdr = sectHdrs[sectNum];
		mStream->WriteT(sectHdr);
	}

	for (int sectNum = 0; sectNum < header.mNumberOfSections; sectNum++)
	{
		BeCOFFSection* sect = sectData[sectNum];
		if (sect == NULL)
			continue;
		PESectionHeader& sectHdr = sectHdrs[sectNum];
		if (sectHdr.mPointerToRawData != 0)
			BF_ASSERT(mStream->GetPos() == sectHdr.mPointerToRawData);

		if (sect->mData.GetSize() != 0)
		{
			mStream->Write((uint8*)sect->mData.GetPtr(), (int)sect->mData.GetSize());
			BF_ASSERT(mStream->GetPos() == sectHdr.mPointerToRawData + (int)sect->mData.GetSize());
		}

		int relocIdx = 0;
		if (sect->mRelocs.size() > 0xFFFF)
		{
			// Extended reloc count
			COFFRelocation coffReloc = { 0 };
			coffReloc.mVirtualAddress = sect->mRelocs.size() + 1;
			mStream->WriteT(coffReloc);
			relocIdx++;
		}

		for (auto& reloc : sect->mRelocs)
		{
			COFFRelocation coffReloc;
			coffReloc.mVirtualAddress = reloc.mOffset;
			coffReloc.mSymbolTableIndex = reloc.mSymTableIdx;
			coffReloc.mType = IMAGE_REL_AMD64_ABSOLUTE;
			
			switch (reloc.mKind)
			{
			case BeMCRelocationKind_ADDR32NB:
				coffReloc.mType = IMAGE_REL_AMD64_ADDR32NB;
				break;
			case BeMCRelocationKind_ADDR64:
				coffReloc.mType = IMAGE_REL_AMD64_ADDR64;
				break;
			case BeMCRelocationKind_REL32:
				coffReloc.mType = IMAGE_REL_AMD64_REL32;
				break;
			case BeMCRelocationKind_SECREL:
				coffReloc.mType = IMAGE_REL_AMD64_SECREL;
				break;
			case BeMCRelocationKind_SECTION:
				coffReloc.mType = IMAGE_REL_AMD64_SECTION;
				break;
			}
			mStream->WriteT(coffReloc);

			relocIdx++;
			BF_ASSERT(mStream->GetPos() == sectHdr.mPointerToRawData + (int)sect->mData.GetSize() + relocIdx*10);
		}
	}

	BF_ASSERT(mStream->GetPos() == filePos);
	
	SizedArray<PE_SymInfo, 16> symInfoVec;
	symInfoVec.reserve(mSymbols.size() + 16);

	for (auto& sym : mSymbols)
	{
		//BP_ZONE("Finish - AddSym");

		if (sym->mSymKind == BeMCSymbolKind_AuxPlaceholder)		
			continue;		

		PE_SymInfo symInfo;
		memset(&symInfo, 0, sizeof(symInfo));
		if (sym->mName.length() > 7)
		{
			int strTablePos = mStrTable.mPos;
			mStrTable.Write((uint8*)sym->mName.c_str(), (int)sym->mName.length() + 1);
			symInfo.mNameOfs[1] = strTablePos + 4;
		}
		else
			strcpy(symInfo.mName, sym->mName.c_str());		
		if (sym->mSymKind == BeMCSymbolKind_SectionDef)
		{			
			symInfo.mSectionNum = sym->mSectionNum;
			symInfo.mStorageClass = IMAGE_SYM_CLASS_STATIC;
			symInfo.mNumOfAuxSymbols = 1;

			symInfoVec.push_back(symInfo);

			static_assert(sizeof(PE_SymInfoAux) == sizeof(PE_SymInfo), "PE_SymInfo size mismatch");
			BeCOFFSection& section = mXDataSect;
			PE_SymInfoAux auxSymInfo;
			auxSymInfo.mLength = section.mData.GetSize();
			auxSymInfo.mNumberOfRelocations = (int)section.mRelocs.size();
			auxSymInfo.mNumberOfLinenumbers = 0;
			auxSymInfo.mCheckSum = 0;
			auxSymInfo.mNumber = 0;
			auxSymInfo.mSelection = 2; // Pick any (only applicable for COMDAT but ignored elsewhere)
			auxSymInfo.mUnused = 0;
			auxSymInfo.mUnused2 = 0;
			auxSymInfo.mUnused3 = 0;			
			symInfoVec.push_back(*(PE_SymInfo*)&auxSymInfo);

			continue;
		}
		else if (sym->mSymKind == BeMCSymbolKind_SectionRef)
		{
			symInfo.mSectionNum = sym->mSectionNum;			
			symInfo.mStorageClass = IMAGE_SYM_CLASS_SECTION;			
		}
		else if (sym->mSymKind == BeMCSymbolKind_Function)
		{
			symInfo.mValue = sym->mValue;
			symInfo.mSectionNum = mTextSect.mSectionIdx + 1;			
			symInfo.mType = 0x20; //DT_FUNCTION
			if (sym->mIsStatic)
				symInfo.mStorageClass = IMAGE_SYM_CLASS_STATIC;
			else
				symInfo.mStorageClass = IMAGE_SYM_CLASS_EXTERNAL;
		}		
		else if (sym->mSymKind == BeMCSymbolKind_COMDAT)
		{
			symInfo.mValue = sym->mValue;
			symInfo.mSectionNum = sym->mSectionNum;			
			symInfo.mStorageClass = IMAGE_SYM_CLASS_EXTERNAL;
		}
		else
		{			
			if (sym->mIsStatic)
				symInfo.mStorageClass = IMAGE_SYM_CLASS_STATIC;
			else
				symInfo.mStorageClass = IMAGE_SYM_CLASS_EXTERNAL;
			symInfo.mValue = sym->mValue;
			symInfo.mSectionNum = sym->mSectionNum;
		}		
		symInfoVec.push_back(symInfo);
	}
	if (!symInfoVec.IsEmpty())
		mStream->Write(&symInfoVec[0], (int)(sizeof(PE_SymInfo)*symInfoVec.size()));

	int32 strTableSize = (int32)mStrTable.GetSize();
	mStream->Write(strTableSize + 4);
	if (strTableSize != 0)
		mStream->Write((uint8*)&mStrTable.mData[0], (int)mStrTable.mData.size());
}

BeMCSymbol* BeCOFFObject::GetSymbol(BeValue* value, bool allowCreate)
{	
	/*auto itr = mSymbolMap.find(value);
	if (itr != mSymbolMap.end())
		return itr->second;*/
	BeMCSymbol** symbolPtr = NULL;
	if (mSymbolMap.TryGetValue(value, &symbolPtr))
		return *symbolPtr;

	if (allowCreate)
	{		
		if (auto func = BeValueDynCast<BeFunction>(value))
		{
			mFuncWorkList.Add(func);

			BeMCSymbol* sym = mSymbols.Alloc();
			sym->mType = func->GetType();
			sym->mName = func->mName;
			sym->mIsStatic = func->mLinkageType == BfIRLinkageType_Internal;
			if (func->mBlocks.empty())
			{
				sym->mSymKind = BeMCSymbolKind_External;
			}
			else
			{
				sym->mSymKind = BeMCSymbolKind_Function;
			}
			sym->mIdx = (int)mSymbols.size() - 1;
			sym->mBeFunction = func;

			mSymbolMap[func] = sym;
			return sym;
		}
	}

	return NULL;
}

BeMCSymbol* BeCOFFObject::GetSymbolRef(const StringImpl& name)
{	
	/*auto itr = mNamedSymbolMap.find(name);
	if (itr != mNamedSymbolMap.end())
		return itr->second;*/
	BeMCSymbol** symbolPtr = NULL;
	if (mNamedSymbolMap.TryGetValue(name, &symbolPtr))
		return *symbolPtr;

	auto sym = mSymbols.Alloc();
	sym->mName = name;
	sym->mSymKind = BeMCSymbolKind_External;
	sym->mIdx = (int)mSymbols.size() - 1;
	mNamedSymbolMap[name] = sym;
	return sym;
}

void BeCOFFObject::MarkSectionUsed(BeCOFFSection& sect, bool getSectSymbol)
{
	if (sect.mSectionIdx == -1)
	{
		sect.mSectionIdx = (int)mUsedSections.size();
		mUsedSections.push_back(&sect);
	}

	if (getSectSymbol)
	{
		//TODO: We previously only did sectionDefs when we needed the SelectionNum value, but
		//  omitting this causes the MS linker to throw "multiple '<X>' sections found with different 
		//  attributes (0000000000) errors.  This change could potentially break LIB creation in the 
		//  linker.  Verify it still works.
		if (((sect.mCharacteristics & IMAGE_SCN_LNK_COMDAT) != 0) || (true))
		{
			if (sect.mSymbolIdx == -1)
			{
				BeMCSymbol* sym;
				sym = mSymbols.Alloc();
				sym->mSymKind = BeMCSymbolKind_SectionDef;
				sym->mName = sect.mSectName;
				sym->mIsStatic = false;
				sym->mSectionNum = sect.mSectionIdx + 1;
				sym->mIdx = (int)mSymbols.size() - 1;
				sect.mSymbolIdx = sym->mIdx;
				sym = mSymbols.Alloc();
				sym->mSymKind = BeMCSymbolKind_AuxPlaceholder;
			}
		}
		else
		{
			// It's important for the linker's import library output to include
			//  section refs and not section defs, even when they aren't an external 
			//  reference
			BeMCSymbol* sym;
			sym = mSymbols.Alloc();
			sym->mSymKind = BeMCSymbolKind_SectionRef;
			sym->mName = sect.mSectName;
			sym->mIsStatic = false;
			sym->mSectionNum = sect.mSectionIdx + 1;
			sym->mIdx = (int)mSymbols.size() - 1;
			sect.mSymbolIdx = sym->mIdx;
		}
	}
}

BeMCSymbol* BeCOFFObject::GetCOMDAT(const StringImpl& name, void* data, int size, int align)
{
	/*auto itr = mNamedSymbolMap.find(name);
	if (itr != mNamedSymbolMap.end())
		return itr->second;*/
	BeMCSymbol** symbolPtr = NULL;
	if (mNamedSymbolMap.TryGetValue(name, &symbolPtr))
		return *symbolPtr;

	BeCOFFSection mRData8Sect;
	auto* rdataSect = mDynSects.Alloc();
	int characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_LNK_COMDAT | IMAGE_SCN_MEM_READ;	
	InitSect(*rdataSect, ".rdata", characteristics, true, true);	
	rdataSect->mAlign = align;

	auto sym = mSymbols.Alloc();
	sym->mName = name;
	sym->mSymKind = BeMCSymbolKind_COMDAT;
	sym->mIdx = (int)mSymbols.size() - 1;
	sym->mSectionNum = rdataSect->mSectionIdx + 1;
	sym->mValue = rdataSect->mData.GetPos();
	mNamedSymbolMap[name] = sym;	
	rdataSect->mData.Write(data, size);	
	return sym;	
}

BeCOFFSection* BeCOFFObject::CreateSect(const StringImpl& name, int characteristics, bool makeSectSymbol)
{
	auto* sect = mDynSects.Alloc();
	sect->mCharacteristics = characteristics;
	InitSect(*sect, name, characteristics, true, makeSectSymbol);
	return sect;
}