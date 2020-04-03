#include "BeIRCodeGen.h"
#include "../Compiler/BfIRCodeGen.h"
#include "BeDbgModule.h"
#include "BeefySysLib/util/BeefPerf.h"

#include "BeefySysLib/util/AllocDebug.h"
#include "BeefySysLib/util/Hash.h"

USING_NS_BF;

//#define CODEGEN_TRACK

#ifdef CODEGEN_TRACK
#include "../Compiler/MemReporter.h"
MemReporter gBEMemReporter;
CritSect gBEMemReporterCritSect;
int gBEMemReporterSize = 0;

#define BE_MEM_START \
	int startPos = mStream->GetReadPos();

#define BE_MEM_END(name) \
	gBEMemReporter.Add(name, mStream->GetReadPos() - startPos);

static const char* gIRCmdNames[] =
{
	"Module_Start",
	"Module_SetTargetTriple",
	"Module_AddModuleFlag",
	"WriteIR",
	"SetType",
	"SetInstType",
	"PrimitiveType",
	"CreateStruct",
	"StructSetBody",
	"Type",
	"TypeInst",
	"TypeInstPtr",
	"GetType",
	"GetPointerToFuncType",
	"GetPointerToType",
	"GetSizedArrayType",
	"CreateConstStruct",
	"CreateConstStructZero",
	"CreateConstArray",
	"CreateConstString",
	"SetName",
	"CreateUndefValue",
	"NumericCast",
	"CmpEQ",
	"CmpNE",
	"CmpSLT",
	"CmpULT",
	"CmpSLE",
	"CmpULE",
	"CmpSGT",
	"CmpUGT",
	"CmpSGE",
	"CmpUGE",
	"Add",
	"Sub",
	"Mul",
	"SDiv",
	"UDiv",
	"SRem",
	"URem",
	"And",
	"Or",
	"Xor",
	"Shl",
	"AShr",
	"LShr",
	"Neg",
	"Not",
	"BitCast",
	"PtrToInt",
	"IntToPtr",
	"InboundsGEP1_32",
	"InboundsGEP2_32",
	"InBoundsGEP1",
	"InBoundsGEP2",
	"IsNull",
	"IsNotNull",
	"ExtractValue",
	"InsertValue",
	"Alloca",
	"AllocaArray",
	"SetAllocaAlignment",
	"SetAllocaNoChkStkHint",
	"SetAllocaForceMem",
	"LifetimeStart",
	"LifetimeEnd",
	"LifetimeExtend",
	"ValueScopeStart",
	"ValueScopeRetain",
	"ValueScopeSoftEnd",
	"ValueScopeHardEnd",
	"Load",
	"AlignedLoad",
	"Store",
	"AlignedStore",
	"MemSet",
	"Fence",
	"StackSave",
	"StackRestore",
	"GlobalVariable",
	"GlobalVar_SetUnnamedAddr",
	"GlobalVar_SetInitializer",
	"GlobalVar_SetAlignment",
	"GlobalStringPtr",
	"CreateBlock",
	"MaybeChainNewBlock",
	"AddBlock",
	"DropBlocks",
	"MergeBlockDown",
	"SetInsertPoint",
	"SetInsertPointAtStart",
	"EraseFromParent",
	"DeleteBlock",
	"EraseInstFromParent",
	"CreateBr",
	"CreateBr_NoCollapse",
	"CreateCondBr",
	"MoveBlockToEnd",
	"CreateSwitch",
	"AddSwitchCase",
	"SetSwitchDefaultDest",
	"CreatePhi",
	"AddPhiIncoming",
	"GetIntrinsic",
	"CreateFunctionType",
	"CreateFunction",
	"EnsureFunctionPatchable",
	"RemapBindFunction",
	"SetActiveFunction",
	"CreateCall",
	"SetCallCallingConv",
	"SetFuncCallingConv",
	"SetTailCall",
	"SetCallAttribute",
	"CreateRet",
	"CreateRetVoid",
	"CreateUnreachable",
	"Call_AddAttribute",
	"Call_AddAttribute1",
	"Func_AddAttribute",
	"Func_AddAttribute1",
	"Func_SetParamName",
	"Func_DeleteBody",
	"Func_EraseFromParent",
	"Func_SetLinkage",
	"SaveDebugLocation",
	"RestoreDebugLocation",
	"ClearDebugLocation",
	"ClearDebugLocationInst",
	"UpdateDebugLocation",
	"SetCurrentDebugLocation",
	"Nop",
	"EnsureInstructionAt",
	"StatementStart",
	"ObjectAccessCheck",
	"DbgInit",
	"DbgFinalize",
	"DbgCreateCompileUnit",
	"DbgCreateFile",
	"ConstValueI64",
	"DbgGetCurrentLocation",
	"DbgSetType",
	"DbgSetInstType",
	"DbgGetType",
	"DbgGetTypeInst",
	"DbgTrackDITypes",
	"DbgCreateNamespace",
	"DbgCreateImportedModule",
	"DbgCreateBasicType",
	"DbgCreateStructType",
	"DbgCreateEnumerationType",
	"DbgCreatePointerType",
	"DbgCreateReferenceType",
	"DbgCreateConstType",
	"DbgCreateArtificialType",
	"DbgCreateArrayType",
	"DbgCreateReplaceableCompositeType",
	"DbgCreateForwardDecl",
	"DbgCreateSizedForwardDecl",
	"BeIRCmd_DbgSetTypeSize",
	"DbgReplaceAllUses",
	"DbgDeleteTemporary",
	"DbgMakePermanent",
	"CreateEnumerator",
	"DbgCreateMemberType",
	"DbgStaticCreateMemberType",
	"DbgCreateInheritance",
	"DbgCreateMethod",
	"DbgCreateFunction",
	"DbgCreateParameterVariable",
	"DbgCreateSubroutineType",
	"DbgCreateAutoVariable",
	"DbgInsertValueIntrinsic",
	"DbgInsertDeclare",
	"DbgLifetimeEnd",
	"DbgCreateGlobalVariable",
	"DbgCreateLexicalBlock",
	"DbgCreateLexicalBlockFile",
	"DbgCreateAnnotation"
};

BF_STATIC_ASSERT(BF_ARRAY_COUNT(gIRCmdNames) == BfIRCmd_COUNT);

#else
#define BE_MEM_START
#define BE_MEM_END(name)
#endif

#pragma warning(disable:4146)

#define CMD_PARAM(ty, name) ty name; Read(name);

template <typename T>
class CmdParamVec : public SizedArray<T, 8>
{};

BeIRCodeGen::BeIRCodeGen()
{
	mBfIRBuilder = NULL;
	mStream = NULL;
	mActiveFunction = NULL;
	mBeContext = NULL;
	mBeModule = NULL;
	mHasDebugLoc = false;
	mDebugging = false;
	mCmdCount = 0;
}

BeIRCodeGen::~BeIRCodeGen()
{
	BF_ASSERT(mSavedDebugLocs.size() == 0);
	delete mBeModule;
	delete mBeContext;	
	delete mStream;
}

void BeIRCodeGen::Hash(BeHashContext& hashCtx)
{
// 	if (mBeModule->mModuleName == "IDE_IDEApp")
// 	{
// 		hashCtx.mDbgViz = true;
// 		NOP;
// 	}

	hashCtx.Mixin(mPtrSize);
	hashCtx.Mixin(mIsOptimized);
	if (mBeModule != NULL)
		mBeModule->Hash(hashCtx);	

	Array<BeStructType*> structHashList;

	for (auto beType : mBeContext->mTypes)
	{
		if (!beType->IsStruct())
			continue;
		auto beStructType = (BeStructType*)beType;
		if (beStructType->mHashId != -1)
			continue;
		structHashList.Add(beStructType);
	}

	structHashList.Sort([](BeStructType* lhs, BeStructType* rhs)
	{
		return lhs->mName < rhs->mName;
	});

	for (auto beStructType : structHashList)
	{
		beStructType->HashReference(hashCtx);
	}
}

bool BeIRCodeGen::IsModuleEmpty()
{
	if (!mBeModule->mFunctions.IsEmpty())
		return false;
	if (!mBeModule->mGlobalVariables.IsEmpty())
		return false;
	return true;
}

void BeIRCodeGen::NotImpl()
{
	BF_FATAL("Not implemented");
}

BeType* BeIRCodeGen::GetBeType(BfTypeCode typeCode, bool& isSigned)
{
	isSigned = false;
	BeTypeCode beTypeCode = BeTypeCode_None;
	switch (typeCode)
	{
	case BfTypeCode_None:
		beTypeCode = BeTypeCode_None;
		break;
	case BfTypeCode_NullPtr:
		beTypeCode = BeTypeCode_NullPtr;
		break;
	case BfTypeCode_Boolean:
		beTypeCode = BeTypeCode_Boolean;
		break;
	case BfTypeCode_Int8:
		isSigned = true;
		beTypeCode = BeTypeCode_Int8;
		break;
	case BfTypeCode_UInt8:
	case BfTypeCode_Char8:
		beTypeCode = BeTypeCode_Int8;
		break;
	case BfTypeCode_Int16:
		isSigned = true;
		beTypeCode = BeTypeCode_Int16;
		break;
	case BfTypeCode_Char16:
	case BfTypeCode_UInt16:
		beTypeCode = BeTypeCode_Int16;
		break;
	case BfTypeCode_Int32:
		isSigned = true;
		beTypeCode = BeTypeCode_Int32;
		break;
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		beTypeCode = BeTypeCode_Int32;
		break;
	case BfTypeCode_Int64:
		isSigned = true;
		beTypeCode = BeTypeCode_Int64;
		break;
	case BfTypeCode_UInt64:
		beTypeCode = BeTypeCode_Int64;
		break;
	case BfTypeCode_IntPtr:
		BF_FATAL("Illegal");
		/*isSigned = true;
		if (mModule->mSystem->mPtrSize == 4)
		return llvm::Type::getInt32Ty(*mLLVMContext);
		else
		return llvm::Type::getInt64Ty(*mLLVMContext);*/
	case BfTypeCode_UIntPtr:
		BF_FATAL("Illegal");
		/*if (mModule->mSystem->mPtrSize == 4)
		return llvm::Type::getInt32Ty(*mLLVMContext);
		else
		return llvm::Type::getInt64Ty(*mLLVMContext);*/
	case BfTypeCode_Single:
		beTypeCode = BeTypeCode_Float;
		break;
	case BfTypeCode_Double:	
		beTypeCode = BeTypeCode_Double;
		break;
	}

	return mBeContext->GetPrimitiveType(beTypeCode);
}

BeIRTypeEntry& BeIRCodeGen::GetTypeEntry(int typeId)
{	
	BeIRTypeEntry& typeEntry = mTypes[typeId];
	if (typeEntry.mTypeId == -1)
		typeEntry.mTypeId = typeId;
	return typeEntry;
}

void BeIRCodeGen::Init(const BfSizedArray<uint8>& buffer)
{
	BP_ZONE("BeIRCodeGen::Init");

	BF_ASSERT(mStream == NULL);
	mStream = new ChunkedDataBuffer();
	mStream->InitFlatRef(buffer.mVals, buffer.mSize);

#ifdef CODEGEN_TRACK
	AutoCrit autoCrit(gBEMemReporterCritSect);
	AutoMemReporter autoMemReporter(&gBEMemReporter, "BeIRCodeGen");
#endif

	//
	{
		BP_ZONE("BeIRCodeGen::ProcessBfIRData.HandleNextCmds");
		while (mStream->GetReadPos() < buffer.mSize)
		{
			if (mFailed)
				break;
			HandleNextCmd();
		}
	}

	BF_ASSERT((mFailed) || (mStream->GetReadPos() == buffer.mSize));
}

void BeIRCodeGen::Process()
{
	BP_ZONE("BeIRCodeGen::process");

	//mDebugging |= ((mBeModule->mDbgModule != NULL) && (mBeModule->mDbgModule->mFileName == "ClassQ"));
	if (mDebugging)
	{
		String dbgStr;
		dbgStr = mBeModule->ToString();
		OutputDebugStr(dbgStr);
	}

	mBeModule->DoInlining();

	if (mDebugging)
	{
		String dbgStr = "-------------- AFTER INLINING --------------\n";
		dbgStr += mBeModule->ToString();
		OutputDebugStr(dbgStr);
	}
}

void BeIRCodeGen::ProcessBfIRData(const BfSizedArray<uint8>& buffer)
{
	BP_ZONE("BeIRCodeGen::ProcessBfIRData");

	Init(buffer);		
	Process();
}

BfTypeCode BeIRCodeGen::GetTypeCode(BeType * type, bool isSigned)
{
	switch (type->mTypeCode)
	{
	case BeTypeCode_Int8: 
		return (isSigned) ? BfTypeCode_Int8 : BfTypeCode_UInt8;
	case BeTypeCode_Int16:
		return (isSigned) ? BfTypeCode_Int16 : BfTypeCode_UInt16;
	case BeTypeCode_Int32:
		return (isSigned) ? BfTypeCode_Int32 : BfTypeCode_UInt32;
	case BeTypeCode_Int64:
		return (isSigned) ? BfTypeCode_Int64 : BfTypeCode_UInt64;
	case BeTypeCode_Float:
		return BfTypeCode_Single;
	case BeTypeCode_Double:
		return BfTypeCode_Double;
	default:
		return BfTypeCode_None;
	}
}

void BeIRCodeGen::SetResult(int id, BeValue* value)
{
	BeIRCodeGenEntry entry;
	entry.mKind = BeIRCodeGenEntryKind_Value;
	entry.mBeValue = value;
	mResults.TryAdd(id, entry);
}

void BeIRCodeGen::SetResult(int id, BeType* type)
{
	BeIRCodeGenEntry entry;
	entry.mKind = BeIRCodeGenEntryKind_Type;	
	entry.mBeType = type;
	mResults.TryAdd(id, entry);
}

void BeIRCodeGen::SetResult(int id, BeBlock* value)
{
	BeIRCodeGenEntry entry;
	entry.mKind = BeIRCodeGenEntryKind_Block;
	entry.mBeBlock = value;
	mResults.TryAdd(id, entry);
}

void BeIRCodeGen::SetResult(int id, BeMDNode* md)
{
	BeIRCodeGenEntry entry;
	entry.mKind = BeIRCodeGenEntryKind_Metadata;
	entry.mBeMetadata = md;
	mResults.TryAdd(id, entry);
}

int64 BeIRCodeGen::ReadSLEB128()
{
	int64 val = 0;
	int64 shift = 0;
	uint8 byteVal;
	do
	{
		byteVal = mStream->Read();
		val |= ((int64)(byteVal & 0x7f)) << shift;
		shift += 7;

	} while (byteVal >= 128);
	// Sign extend negative numbers.
	if ((byteVal & 0x40) && (shift < 64))
		val |= (-1ULL) << shift;
	return val;
}

void BeIRCodeGen::Read(StringImpl& str)
{
	BE_MEM_START;
	int len = (int)ReadSLEB128();
	str.Append('?', len);
	mStream->Read((void*)str.c_str(), len);
	BE_MEM_END("String");
}

void BeIRCodeGen::Read(int& i)
{
	BE_MEM_START;
	i = (int)ReadSLEB128();
	BE_MEM_END("int");
}

void BeIRCodeGen::Read(int64& i)
{
	BE_MEM_START;
	i = ReadSLEB128();
	BE_MEM_END("int64");
}

void BeIRCodeGen::Read(Val128& i)
{
	i.mLow = (uint64)ReadSLEB128();
	i.mHigh = (uint64)ReadSLEB128();
}

void BeIRCodeGen::Read(bool& val)
{
	BE_MEM_START;
	val = mStream->Read() != 0;
	BE_MEM_END("bool");
}

void BeIRCodeGen::Read(BeIRTypeEntry*& type)
{
	BE_MEM_START;
	int typeId = (int)ReadSLEB128();
	type = &GetTypeEntry(typeId);
	BE_MEM_END("BeIRTypeEntry");
}

void BeIRCodeGen::Read(BeType*& beType)
{
	BE_MEM_START;

	BfIRType::TypeKind typeKind = (BfIRType::TypeKind)mStream->Read();
	if (typeKind == BfIRType::TypeKind::TypeKind_None)
	{
		beType = NULL;
		BE_MEM_END("BeType");
		return;
	}

	if (typeKind == BfIRType::TypeKind::TypeKind_Stream)
	{
		int streamId = (int)ReadSLEB128();
		if (streamId == -1)
		{
			beType = NULL;
			BE_MEM_END("BeType");
			return;
		}
		auto& result = mResults[streamId];
		BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Type);
		beType = result.mBeType;
		BE_MEM_END("BeType");
		return;
	}

	if (typeKind == BfIRType::TypeKind::TypeKind_SizedArray)
	{
		CMD_PARAM(BeType*, elementType);
		CMD_PARAM(int, length);
		beType = mBeContext->CreateSizedArrayType(elementType, length);
		return;
	}

	int typeId = (int)ReadSLEB128();
	auto& typeEntry = GetTypeEntry(typeId);
	if (typeKind == BfIRType::TypeKind::TypeKind_TypeId)
		beType = typeEntry.mBeType;
	else if (typeKind == BfIRType::TypeKind::TypeKind_TypeInstId)
		beType = typeEntry.mInstBeType;
	else if (typeKind == BfIRType::TypeKind::TypeKind_TypeInstPtrId)
		beType = mBeContext->GetPointerTo(typeEntry.mInstBeType);	
	BE_MEM_END("BeType");
}

void BeIRCodeGen::Read(BeFunctionType*& beType)
{
	BE_MEM_START;
	int streamId = (int)ReadSLEB128();
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Type);
	beType = (BeFunctionType*)result.mBeType;
	BE_MEM_END("BeFunctionType");
}

void BeIRCodeGen::Read(BeValue*& beValue)
{
	BE_MEM_START;

	BfIRParamType paramType = (BfIRParamType)mStream->Read();
	if (paramType == BfIRParamType_None)
	{
		beValue = NULL;
		BE_MEM_END("ParamType_None");
	}
	else if (paramType == BfIRParamType_Const)
	{
		BfTypeCode typeCode = (BfTypeCode)mStream->Read();
		BfConstType constType = (BfConstType)typeCode;
		if (constType == BfConstType_GlobalVar)
		{
			CMD_PARAM(int, streamId);
			if (streamId == -1)
			{
				int streamId = mCmdCount++;

				CMD_PARAM(BeType*, varType);
				CMD_PARAM(bool, isConstant);
				BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
				CMD_PARAM(BeConstant*, initializer);
				CMD_PARAM(String, name);
				CMD_PARAM(bool, isTLS);
				
				auto globalVariable = mBeModule->mGlobalVariables.Alloc();
				globalVariable->mModule = mBeModule;
				globalVariable->mType = varType;
				globalVariable->mIsConstant = isConstant;
				globalVariable->mLinkageType = linkageType;
				globalVariable->mInitializer = initializer;
				globalVariable->mName = name;
				globalVariable->mIsTLS = isTLS;
				globalVariable->mAlign = varType->mAlign;
				globalVariable->mUnnamedAddr = false;				
				BF_ASSERT(varType->mAlign > 0);

				SetResult(streamId, globalVariable);
				beValue = globalVariable;
			}
			else
				beValue = GetBeValue(streamId);
			BE_MEM_END("ParamType_Const_GlobalVar");
			return;
		}		
		else if ((constType == BfConstType_BitCast) || (constType == BfConstType_BitCastNull))
		{		
			CMD_PARAM(BeConstant*, target);
			CMD_PARAM(BeType*, toType);

			auto castedVal = mBeModule->mAlloc.Alloc<BeCastConstant>();
			castedVal->mInt64 = target->mInt64;
			castedVal->mType = toType;
			castedVal->mTarget = target;			
			BF_ASSERT(target->GetType() != NULL);
			BF_ASSERT(!target->GetType()->IsComposite());
			beValue = castedVal;
			BE_MEM_END("ParamType_Const_BitCast");
			return;
		}
		else if (constType == BfConstType_GEP32_2)
		{
			CMD_PARAM(BeConstant*, target);
			CMD_PARAM(int, idx0);
			CMD_PARAM(int, idx1);
			
			BF_ASSERT(target->GetType()->IsPointer());
			auto gepConstant = mBeModule->mAlloc.Alloc<BeGEPConstant>();
			gepConstant->mTarget = target;
			gepConstant->mIdx0 = idx0;
			gepConstant->mIdx1 = idx1;

			beValue = gepConstant;
			BE_MEM_END("ParamType_Const_GEP32_2");
			return;
		}
		else if (constType == BfConstType_PtrToInt)
		{
			CMD_PARAM(BeConstant*, target);
			BfTypeCode toTypeCode = (BfTypeCode)mStream->Read();
			BF_ASSERT(target->GetType()->IsPointer());

			bool isSigned = false;
			BeType* toType = GetBeType(toTypeCode, isSigned);

			auto castedVal = mBeModule->mAlloc.Alloc<BeCastConstant>();
			castedVal->mInt64 = target->mInt64;
			castedVal->mType = toType;
			castedVal->mTarget = target;			
			BF_ASSERT(target->GetType() != NULL);
			beValue = castedVal;
			BE_MEM_END("ParamType_Const_PtrToInt");
			return;
		}
		else if (constType == BfConstType_AggZero)
		{
			CMD_PARAM(BeType*, type);
			auto beConst = mBeModule->mAlloc.Alloc<BeConstant>();
			beConst->mType = type;
			beValue = beConst;
			BE_MEM_END("ParamType_Const_AggZero");
			return;
		}
		else if (constType == BfConstType_Array)
		{
			CMD_PARAM(BeType*, type);
			CMD_PARAM(CmdParamVec<BeConstant*>, values);

			auto arrayType = (BeSizedArrayType*)type;
			int fillCount = (int)(arrayType->mLength - values.size());
			if (fillCount > 0)
			{
				auto lastValue = values.back();
				for (int i = 0; i < fillCount; i++)
					values.push_back(lastValue);
			}

			auto constStruct = mBeModule->mOwnedValues.Alloc<BeStructConstant>();
			constStruct->mType = type;
			for (auto val : values)
				constStruct->mMemberValues.push_back(BeValueDynCast<BeConstant>(val));
			beValue = constStruct;
			BE_MEM_END("ParamType_Const_Array");
			return;
		}

		bool isSigned = false;
		BeType* llvmConstType = GetBeType(typeCode, isSigned);

		if (typeCode == BfTypeCode_Single)
		{
			float f;
			mStream->Read(&f, sizeof(float));
			beValue = mBeModule->GetConstant(llvmConstType, f);
			BE_MEM_END("ParamType_Single");
		}
		else if (typeCode == BfTypeCode_Double)
		{
			double d;
			mStream->Read(&d, sizeof(double));
			beValue = mBeModule->GetConstant(llvmConstType, d);
			BE_MEM_END("ParamType_Const_Double");
		}
		else if (typeCode == BfTypeCode_Boolean)
		{
			CMD_PARAM(bool, boolVal);
			beValue = mBeModule->GetConstant(llvmConstType, boolVal);
			BE_MEM_END("ParamType_Const_Boolean");
		}
		else if (typeCode == BfTypeCode_None)
		{
			beValue = NULL;			
			BE_MEM_END("ParamType_Const_None");
		}
		else if (typeCode == BfTypeCode_NullPtr)
		{
			CMD_PARAM(BeType*, nullType);			
			beValue = mBeModule->GetConstantNull((BePointerType*)nullType);			
			BE_MEM_END("ParamType_Const_NullPtr");
		}
		else if (BfIRBuilder::IsInt(typeCode))
		{			
			int64 intVal = ReadSLEB128();
			auto constVal = mBeModule->GetConstant(llvmConstType, intVal);			
			beValue = constVal;
			BE_MEM_END("ParamType_Const_Int");
		}
		else
		{
			BF_FATAL("Unhandled");
		}		
	}
	else if (paramType == BfIRParamType_Arg)
	{
		int argIdx = mStream->Read();
		beValue = mBeModule->GetArgument(argIdx);
		BE_MEM_END("ParamType_Arg");
	}
	else if (paramType == BfIRParamType_StreamId_Abs8)
	{				
		int cmdId = mStream->Read();
		auto& result = mResults[cmdId];
		BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Value);
		beValue = result.mBeValue;
		BE_MEM_END("ParamType_StreamId");
	}
	else if (paramType == BfIRParamType_StreamId_Rel)
	{		
		int cmdId = mCmdCount - (int)ReadSLEB128();
		auto& result = mResults[cmdId];
		BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Value);
		beValue = result.mBeValue;
		BE_MEM_END("ParamType_StreamId");
	}	
	else
	{
		int cmdId = mCmdCount - (paramType - BfIRParamType_StreamId_Back1) - 1;
		auto& result = mResults[cmdId];
		BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Value);
		beValue = result.mBeValue;
		BE_MEM_END("ParamType_StreamId");
	}	
}

void BeIRCodeGen::Read(BeConstant*& llvmConstant)
{
	BE_MEM_START;
	BeValue* value;
	Read(value);
	if (value == NULL)
	{
		llvmConstant = NULL;
	}
	else
	{
		BF_ASSERT(BeValueDynCast<BeConstant>(value));
		llvmConstant = (BeConstant*)value;
	}
	BE_MEM_END("BeConstant");
}

void BeIRCodeGen::Read(BeFunction*& beFunc)
{
	BE_MEM_START;
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		beFunc = NULL;
		return;
	}
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Value);	
	BF_ASSERT(BeValueDynCast<BeFunction>(result.mBeValue));
	beFunc = (BeFunction*)result.mBeValue;
	BE_MEM_END("BeFunction");
}

void BeIRCodeGen::Read(BeBlock*& beBlock)
{
	BE_MEM_START;
	int streamId = (int)ReadSLEB128();
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Block);
	beBlock = (BeBlock*)result.mBeType;
	BE_MEM_END("BeBlock");
}

void BeIRCodeGen::Read(BeMDNode*& llvmMD)
{
	BE_MEM_START;
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		llvmMD = NULL;
		return;
	}
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Metadata);
	llvmMD = result.mBeMetadata;
	BE_MEM_END("BeMDNode");
}

void BeIRCodeGen::HandleNextCmd()
{
	int curId = mCmdCount;

	BfIRCmd cmd = (BfIRCmd)mStream->Read();
	mCmdCount++;

#ifdef CODEGEN_TRACK	
	gBEMemReporter.BeginSection(gIRCmdNames[cmd]);
	gBEMemReporter.Add(1);
#endif

	switch (cmd)
	{
	case BfIRCmd_Module_Start:
		{
			CMD_PARAM(String, moduleName);
			CMD_PARAM(int, ptrSize);
			CMD_PARAM(bool, isOptimized);

			BF_ASSERT(mBeModule == NULL);
			mPtrSize = ptrSize;
			mIsOptimized = isOptimized;
			mBeContext = new BeContext();
			mBeModule = new BeModule(moduleName, mBeContext);
			mBeModule->mBeIRCodeGen = this;

			for (auto constInt : mConfigConsts)
			{
				auto constVal = mBeModule->mAlloc.Alloc<BeConstant>();
				constVal->mType = mBeContext->GetPrimitiveType(BeTypeCode_Int32);
				constVal->mInt64 = constInt;
				mBeModule->mConfigConsts32.Add(constVal);

				constVal = mBeModule->mAlloc.Alloc<BeConstant>();
				constVal->mType = mBeContext->GetPrimitiveType(BeTypeCode_Int64);
				constVal->mInt64 = constInt;
				mBeModule->mConfigConsts64.Add(constVal);
			}
		}
		break;
	case BfIRCmd_Module_SetTargetTriple:
		{
			CMD_PARAM(String, targetTriple);
			mBeModule->mTargetTriple = targetTriple;			
		}
		break;
	case BfIRCmd_Module_AddModuleFlag:
		{
			CMD_PARAM(String, flag);
			CMD_PARAM(int, val);
			//mBeModule->addModuleFlag(BeModule::Warning, flag, val);
		}
		break;
	case BfIRCmd_WriteIR:
		{
			/*CMD_PARAM(String, fileName);
			std::error_code ec;
			Beraw_fd_ostream outStream(fileName.c_str(), ec, Besys::fs::OpenFlags::F_Text);
			if (ec)
			{
				Fail("Failed writing IR '" + fileName + "': " + ec.message());
			}
			else
				mBeModule->print(outStream, NULL);*/
		}
		break;
	case BfIRCmd_SetType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(BeType*, type);						
			GetTypeEntry(typeId).mBeType = type;
		}
		break;
	case BfIRCmd_SetInstType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(BeType*, type);
			GetTypeEntry(typeId).mInstBeType = type;
		}
		break;	
	case BfIRCmd_PrimitiveType:
		{
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			bool isSigned = false;
			SetResult(curId, GetBeType(typeCode, isSigned));
		}
		break;
	case BfIRCmd_CreateStruct:
		{
			CMD_PARAM(String, typeName);			
			SetResult(curId, mBeContext->CreateStruct(typeName));
		}
		break;
	case BfIRCmd_StructSetBody:
		{			
			CMD_PARAM(BeType*, type);
			CMD_PARAM(CmdParamVec<BeType*>, members);
			CMD_PARAM(bool, isPacked);
			BF_ASSERT(type->mTypeCode == BeTypeCode_Struct);			
			auto structType = (BeStructType*)type;
			mBeContext->SetStructBody(structType, members, isPacked);
		}
		break;
	case  BfIRCmd_Type:
		{
			CMD_PARAM(BeIRTypeEntry*, typeEntry);
			auto type = typeEntry->mBeType;
			SetResult(curId, type);
		}
		break;
	case  BfIRCmd_TypeInst:
		{
			CMD_PARAM(BeIRTypeEntry*, typeEntry);
			SetResult(curId, typeEntry->mInstBeType);
		}
		break;
	case BfIRCmd_TypeInstPtr:
		{
			CMD_PARAM(BeIRTypeEntry*, typeEntry);
			SetResult(curId, mBeContext->GetPointerTo(typeEntry->mInstBeType));
		}
		break;	
	case BfIRCmd_GetType:
		{
			CMD_PARAM(BeValue*, value);
			auto type = value->GetType();
			SetResult(curId, type);
		}
		break;	
	case BfIRCmd_GetPointerToFuncType:
		{
			CMD_PARAM(BeFunctionType*, funcType);
			SetResult(curId, mBeContext->GetPointerTo(funcType));
		}
		break;
	case BfIRCmd_GetPointerToType:
		{
			CMD_PARAM(BeType*, type);
			SetResult(curId, mBeContext->GetPointerTo(type));
		}
		break;
	case BfIRCmd_GetSizedArrayType:
		{
			CMD_PARAM(BeType*, elementType);
			CMD_PARAM(int, length);
			SetResult(curId, mBeContext->CreateSizedArrayType(elementType, length));
		}
		break;	
	case BfIRCmd_CreateConstStruct:
		{
			CMD_PARAM(BeType*, type);
			CMD_PARAM(CmdParamVec<BeValue*>, values)			
			auto constStruct = mBeModule->mOwnedValues.Alloc<BeStructConstant>();						
			constStruct->mType = type;
			BF_ASSERT(type->IsStruct());			
			BF_ASSERT(((BeStructType*)type)->mMembers.size() == values.size());
			for (int i = 0; i < (int)values.size(); i++)			
			{
				auto val = values[i];
				BF_ASSERT(((BeStructType*)type)->mMembers[i].mType == val->GetType());
				constStruct->mMemberValues.push_back(BeValueDynCast<BeConstant>(val));
			}
			SetResult(curId, constStruct);
		}
		break;
	case BfIRCmd_CreateConstStructZero:
		{
			CMD_PARAM(BeType*, type);
			auto beConst = mBeModule->mAlloc.Alloc<BeConstant>();
			beConst->mType = type;
			SetResult(curId, beConst);
		}
		break;
	case BfIRCmd_CreateConstArray:
		{
			CMD_PARAM(BeType*, type);
			CMD_PARAM(CmdParamVec<BeConstant*>, values);

			auto constStruct = mBeModule->mOwnedValues.Alloc<BeStructConstant>();
			constStruct->mType = type;
			for (auto val : values)
				constStruct->mMemberValues.push_back(BeValueDynCast<BeConstant>(val));
			SetResult(curId, constStruct);
		}
		break;
	case BfIRCmd_CreateConstString:
		{
			CMD_PARAM(String, str);			
			auto constStruct = mBeModule->mOwnedValues.Alloc<BeStringConstant>();
			constStruct->mString = str;
			auto charType = mBeContext->GetPrimitiveType(BeTypeCode_Int8);
			constStruct->mType = mBeContext->CreateSizedArrayType(charType, str.length() + 1);
			SetResult(curId, constStruct);
		}
		break;
	case BfIRCmd_ConfigConst:
		{
			CMD_PARAM(int, constIdx);
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			if (typeCode == BfTypeCode_IntPtr)
				typeCode = (mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
			BeConstant* constVal = (typeCode == BfTypeCode_Int32) ?
				mBeModule->mConfigConsts32[constIdx] :
				mBeModule->mConfigConsts64[constIdx];
			SetResult(curId, constVal);
		}
		break;
	case BfIRCmd_SetName:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(String, name);
			val->SetName(name);
		}
		break;
	case BfIRCmd_CreateUndefValue:
		{
			CMD_PARAM(BeType*, type);
			SetResult(curId, mBeModule->CreateUndefValue(type));
		}
		break;
	case BfIRCmd_NumericCast:
		{	
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(bool, valIsSigned);
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			BfTypeCode valTypeCode = GetTypeCode(val->GetType(), valIsSigned);		
			bool toSigned = false;
			auto toBeType = GetBeType(typeCode, toSigned);		
			BeValue* retVal = mBeModule->CreateNumericCast(val, toBeType, valIsSigned, toSigned);
			SetResult(curId, retVal);
		}
		break;
	case BfIRCmd_CmpEQ:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_EQ, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpNE:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_NE, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSLT:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_SLT, lhs, rhs));			
		}
		break;
	case BfIRCmd_CmpULT:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_ULT, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSLE:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_SLE, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpULE:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_ULE, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSGT:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_SGT, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpUGT:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_UGT, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSGE:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_SGE, lhs, rhs));
		}
		break;
	case BfIRCmd_CmpUGE:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_UGE, lhs, rhs));
		}
		break;
	case BfIRCmd_Add:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_Add, lhs, rhs));
		}
		break;
	case BfIRCmd_Sub:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_Subtract, lhs, rhs));
		}
		break;
	case BfIRCmd_Mul:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_Multiply, lhs, rhs));
		}
		break;
	case BfIRCmd_SDiv:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_SDivide, lhs, rhs));
		}
		break;
	case BfIRCmd_UDiv:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_UDivide, lhs, rhs));
		}
		break;
	case BfIRCmd_SRem:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_SModulus, lhs, rhs));
		}
		break;
	case BfIRCmd_URem:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_UModulus, lhs, rhs));
		}
		break;
	case BfIRCmd_And:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_BitwiseAnd, lhs, rhs));
		}
		break;
	case BfIRCmd_Or:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_BitwiseOr, lhs, rhs));
		}
		break;
	case BfIRCmd_Xor:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_ExclusiveOr, lhs, rhs));
		}
		break;
	case BfIRCmd_Shl:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_LeftShift, lhs, rhs));
		}
		break;
	case BfIRCmd_AShr:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_ARightShift, lhs, rhs));
		}
		break;
	case BfIRCmd_LShr:
		{
			CMD_PARAM(BeValue*, lhs);
			CMD_PARAM(BeValue*, rhs);			
			SetResult(curId, mBeModule->CreateBinaryOp(BeBinaryOpKind_RightShift, lhs, rhs));
		}
		break;
	case BfIRCmd_Neg:
		{
			CMD_PARAM(BeValue*, val);

			auto negInst = mBeModule->AllocInst<BeNegInst>();
			negInst->mValue = val;			
			SetResult(curId, negInst);
		}
		break;
	case BfIRCmd_Not:
		{
			CMD_PARAM(BeValue*, val);

			auto negInst = mBeModule->AllocInst<BeNotInst>();
			negInst->mValue = val;
			SetResult(curId, negInst);
		}
		break;
	case BfIRCmd_BitCast:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeType*, toType);
			if (auto funcVal = BeValueDynCast<BeFunction>(val))
			{
				auto beConst = mBeModule->mAlloc.Alloc<BeConstant>();
				beConst->mTarget = funcVal;
				BF_ASSERT(funcVal->mType != NULL);
				beConst->mType = toType;
				SetResult(curId, beConst);
				break;
			}

			SetResult(curId, mBeModule->CreateBitCast(val, toType));
		}
		break;
	case BfIRCmd_PtrToInt:
		{
			CMD_PARAM(BeValue*, val);
			auto typeCode = (BfTypeCode)mStream->Read();
			bool isSigned = false;
			auto beType = GetBeType(typeCode, isSigned);

			BF_ASSERT(beType != NULL);
			auto numericCastInst = mBeModule->AllocInst<BeNumericCastInst>();
			numericCastInst->mValue = val;
			numericCastInst->mValSigned = false;
			numericCastInst->mToType = beType;
			numericCastInst->mToSigned = isSigned;			
			SetResult(curId, numericCastInst);
		}
		break;
	case BfIRCmd_IntToPtr:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeType*, toType);
			
			auto bitcastInst = mBeModule->AllocInst<BeBitCastInst>();
			bitcastInst->mValue = val;
			bitcastInst->mToType = toType;
			SetResult(curId, bitcastInst);
		}
		break;
	case BfIRCmd_InboundsGEP1_32:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, idx0);
			BF_ASSERT(val->GetType()->IsPointer());
			BeType* int32Type = mBeContext->GetPrimitiveType(BeTypeCode_Int32);
			SetResult(curId, mBeModule->CreateGEP(val, mBeModule->GetConstant(int32Type, (int64)idx0), NULL));
		}
		break;
	case BfIRCmd_InboundsGEP2_32:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, idx0);
			CMD_PARAM(int, idx1);
			BF_ASSERT(val->GetType()->IsPointer());
			BeType* int32Type = mBeContext->GetPrimitiveType(BeTypeCode_Int32);
			SetResult(curId, mBeModule->CreateGEP(val, mBeModule->GetConstant(int32Type, (int64)idx0), mBeModule->GetConstant(int32Type, (int64)idx1)));
		}
		break;
	case BfIRCmd_InBoundsGEP1:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeValue*, idx0);
			BF_ASSERT(val->GetType()->IsPointer());
			SetResult(curId, mBeModule->CreateGEP(val, idx0, NULL));
		}
		break;
	case BfIRCmd_InBoundsGEP2:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeValue*, idx0);
			CMD_PARAM(BeValue*, idx1);
			BF_ASSERT(val->GetType()->IsPointer());
			SetResult(curId, mBeModule->CreateGEP(val, idx0, idx1));
		}
		break;
	case BfIRCmd_IsNull:
		{
			CMD_PARAM(BeValue*, val);
			BF_ASSERT(val->GetType()->IsPointer());
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_EQ, val, mBeModule->GetConstantNull((BePointerType*)val->GetType())));						
		}
		break;
	case BfIRCmd_IsNotNull:
		{
			CMD_PARAM(BeValue*, val);
			BF_ASSERT(val->GetType()->IsPointer());
			SetResult(curId, mBeModule->CreateCmp(BeCmpKind_NE, val, mBeModule->GetConstantNull((BePointerType*)val->GetType())));	
		}
		break;
	case BfIRCmd_ExtractValue:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, idx);
			
			BF_ASSERT(val->GetType()->IsComposite());

			auto extractValueInst = mBeModule->AllocInst<BeExtractValueInst>();
			extractValueInst->mAggVal = val;
			extractValueInst->mIdx = idx;			
			SetResult(curId, extractValueInst);
		}
		break;
	case BfIRCmd_InsertValue:
		{
			CMD_PARAM(BeValue*, agg);
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, idx);

			auto insertValueInst = mBeModule->AllocInst<BeInsertValueInst>();
			insertValueInst->mAggVal = agg;
			insertValueInst->mMemberVal = val;
			insertValueInst->mIdx = idx;			
			SetResult(curId, insertValueInst);
		}
		break;
	case BfIRCmd_Alloca:
		{
			CMD_PARAM(BeType*, type);
			if (type->IsStruct())
			{
				BF_ASSERT(!((BeStructType*)type)->mIsOpaque);
			}			

			auto allocaInst = mBeModule->CreateAlloca(type);
			allocaInst->mAlign = type->mAlign;
			SetResult(curId, allocaInst);		
		}
		break;
	case BfIRCmd_AllocaArray:
		{
			CMD_PARAM(BeType*, type);
			CMD_PARAM(BeValue*, arraySize);
			
			if (auto constant = BeValueDynCast<BeConstant>(arraySize))
			{
				//BF_ASSERT(constant->mInt64 >= 0);
			}

			auto allocaInst = mBeModule->AllocInst<BeAllocaInst>();
			allocaInst->mType = type;
			allocaInst->mAlign = type->mAlign;			
			allocaInst->mArraySize = arraySize;
			
			SetResult(curId, allocaInst);
		}
		break;
	case BfIRCmd_SetAllocaAlignment:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, alignment);
			auto inst = BeValueDynCast<BeAllocaInst>(val);

			inst->mAlign = alignment;			
			//TODO: Implement
			/*inst->setAlignment(alignment);*/
		}
		break;
	case BfIRCmd_AliasValue:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeAliasValueInst>();
			inst->mPtr = val;
			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_LifetimeStart:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeLifetimeStartInst>();
			inst->mPtr = val;
			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_LifetimeEnd:
		{
			CMD_PARAM(BeValue*, val);
#ifdef _DEBUG
			val->mLifetimeEnded = true;
#endif
			auto inst = mBeModule->AllocInst<BeLifetimeEndInst>();
			inst->mPtr = val;
			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_LifetimeExtend:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeLifetimeExtendInst>();
			inst->mPtr = val;
			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_ValueScopeStart:
		{
			auto inst = mBeModule->AllocInst<BeValueScopeStartInst>();
			SetResult(curId, inst);			
		}
		break;
	case BfIRCmd_ValueScopeRetain:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeValueScopeRetainInst>();
			inst->mValue = val;
		}
		break;
	case BfIRCmd_ValueScopeSoftEnd:
		{			
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeValueScopeEndInst>();
			inst->mScopeStart = (BeValueScopeStartInst*)val;
			inst->mIsSoft = true;
			//TODO: Is this always correct? This keeps there from being nops inserted on block opens ( { )
			inst->mDbgLoc = NULL;
		}
		break;
	case BfIRCmd_ValueScopeHardEnd:
		{		
			CMD_PARAM(BeValue*, val);
			auto inst = mBeModule->AllocInst<BeValueScopeEndInst>();
			inst->mScopeStart = (BeValueScopeStartInst*)val;
			inst->mIsSoft = false;
			//TODO: Is this always correct? This keeps there from being nops inserted on block opens ( { )
			inst->mDbgLoc = NULL;
		}
		break;
	case BfIRCmd_SetAllocaNoChkStkHint:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = BeValueDynCast<BeAllocaInst>(val);
			inst->mNoChkStk = true;
		}
		break;
	case BfIRCmd_SetAllocaForceMem:
		{
			CMD_PARAM(BeValue*, val);
			auto inst = BeValueDynCast<BeAllocaInst>(val);
			inst->mForceMem = true;
		}
		break;
	case BfIRCmd_Load:
		{
			CMD_PARAM(BeValue*, val);
#ifdef _DEBUG			
			auto ptrType = val->GetType();
			BF_ASSERT(ptrType->IsPointer());
			// We call via a function pointer so there's never a reason to allow loading of a funcPtr
			BF_ASSERT(((BePointerType*)ptrType)->mElementType->mTypeCode != BeTypeCode_Function);

			// Disallow loading from a NULL constant
			if (val->GetTypeId() == BeConstant::TypeId)
			{
				if (auto constant = BeValueDynCast<BeConstant>(val))
				{
					BF_ASSERT(constant->mTarget != NULL);
				}
			}

#endif
			CMD_PARAM(bool, isVolatile);			
			SetResult(curId, mBeModule->CreateLoad(val, isVolatile));
		}
		break;
	case BfIRCmd_AlignedLoad:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, alignment);
			CMD_PARAM(bool, isVolatile);
#ifdef _DEBUG
			// Disallow loading from a NULL constant
			if (val->GetTypeId() == BeConstant::TypeId)
			{
				if (auto constant = BeValueDynCast<BeConstant>(val))
				{
					BF_ASSERT(constant->mTarget != NULL);
				}
			}

			auto ptrType = val->GetType();
			BF_ASSERT(ptrType->IsPointer());
#endif
			SetResult(curId, mBeModule->CreateAlignedLoad(val, alignment, isVolatile));
		}
		break;
	case BfIRCmd_Store:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeValue*, ptr);

#ifdef _DEBUG
			auto ptrType = ptr->GetType();
			auto valType = val->GetType();
			BF_ASSERT(ptrType->IsPointer());
			BF_ASSERT(mBeContext->AreTypesEqual(((BePointerType*)ptrType)->mElementType, valType));
#endif

			CMD_PARAM(bool, isVolatile);
			SetResult(curId, mBeModule->CreateStore(val, ptr, isVolatile));
		}
		break;
	case BfIRCmd_AlignedStore:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeValue*, ptr);
			CMD_PARAM(int, alignment);
			CMD_PARAM(bool, isVolatile);

#ifdef _DEBUG
			auto ptrType = ptr->GetType();
			auto valType = val->GetType();
			BF_ASSERT(ptrType->IsPointer());
			BF_ASSERT(mBeContext->AreTypesEqual(((BePointerType*)ptrType)->mElementType, valType));
#endif

			SetResult(curId, mBeModule->CreateAlignedStore(val, ptr, alignment, isVolatile));
		}
		break;
	case BfIRCmd_MemSet:
		{
			auto inst = mBeModule->AllocInst<BeMemSetInst>();			
			Read(inst->mAddr);
			Read(inst->mVal);
			Read(inst->mSize);
			Read(inst->mAlignment);
			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_Fence:
		{
			BfIRFenceType fenceType = (BfIRFenceType)mStream->Read();
			mBeModule->AllocInst<BeFenceInst>();
		}
		break;
	case BfIRCmd_StackSave:
		{
			SetResult(curId, mBeModule->AllocInst<BeStackSaveInst>());
		}
		break;
	case BfIRCmd_StackRestore:
		{
			CMD_PARAM(BeValue*, stackVal);
			auto stackRestoreInst = mBeModule->AllocInst<BeStackRestoreInst>();
			stackRestoreInst->mStackVal = stackVal;
			SetResult(curId, stackRestoreInst);
		}
		break;
	case BfIRCmd_GlobalVariable:
		{
			CMD_PARAM(BeType*, varType);
			CMD_PARAM(bool, isConstant);
			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
			CMD_PARAM(BeConstant*, initializer);
			CMD_PARAM(String, name);
			CMD_PARAM(bool, isTLS);
						
			auto globalVariable = mBeModule->mGlobalVariables.Alloc();
			globalVariable->mModule = mBeModule;
			globalVariable->mType = varType;
			globalVariable->mIsConstant = isConstant;
			globalVariable->mLinkageType = linkageType;
			globalVariable->mInitializer = initializer;
			globalVariable->mName = name;			
			globalVariable->mIsTLS = isTLS;
			globalVariable->mUnnamedAddr = false;
			if (initializer != NULL)
			{
				globalVariable->mAlign = varType->mAlign;
				BF_ASSERT(varType->mAlign > 0);
				BF_ASSERT(mBeContext->AreTypesEqual(varType, initializer->GetType()));
			}
			else
				globalVariable->mAlign = -1;			
			
			SetResult(curId, globalVariable);
		}
		break;
	case BfIRCmd_GlobalVar_SetUnnamedAddr:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(bool, unnamedAddr);

			BF_ASSERT(BeValueDynCast<BeGlobalVariable>(val) != NULL);

			((BeGlobalVariable*)val)->mUnnamedAddr = true;			
		}
		break;
	case BfIRCmd_GlobalVar_SetInitializer:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeConstant*, initializer);
			
			BF_ASSERT(BeValueDynCast<BeGlobalVariable>(val) != NULL);

			auto globalVariable = (BeGlobalVariable*)val;
			globalVariable->mInitializer = initializer;			

			if (globalVariable->mInitializer != NULL)
			{
				globalVariable->mAlign = globalVariable->mType->mAlign;
				BF_ASSERT(globalVariable->mAlign != -1);
			}
		}
		break;
	case BfIRCmd_GlobalVar_SetAlignment:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(int, alignment);

			BF_ASSERT(BeValueDynCast<BeGlobalVariable>(val) != NULL);

			auto globalVariable = (BeGlobalVariable*)val;
			globalVariable->mAlign = alignment;
			BF_ASSERT(alignment > 0);

			if (globalVariable->mInitializer != NULL)
			{
				BF_ASSERT(globalVariable->mAlign != -1);
			}
		}
		break;
	case BfIRCmd_GlobalStringPtr:
		{
			CMD_PARAM(String, str);

			auto constStruct = mBeModule->mOwnedValues.Alloc<BeStringConstant>();
			constStruct->mString = str;
			auto charType = mBeContext->GetPrimitiveType(BeTypeCode_Int8);
			constStruct->mType = mBeContext->CreateSizedArrayType(charType, str.length() + 1);

			auto globalVariable = mBeModule->mGlobalVariables.Alloc();
			globalVariable->mModule = mBeModule;
			globalVariable->mType = constStruct->mType;
			globalVariable->mIsConstant = true;
			globalVariable->mLinkageType = BfIRLinkageType_Internal;
			globalVariable->mInitializer = constStruct;
			globalVariable->mName = StrFormat("__str%d", (int)mBeModule->mGlobalVariables.size() - 1);
			globalVariable->mIsTLS = false;
			globalVariable->mAlign = 1;
			globalVariable->mUnnamedAddr = false;

			auto castedVal = mBeModule->mAlloc.Alloc<BeCastConstant>();			
			castedVal->mType = mBeContext->GetPointerTo(charType);
			castedVal->mTarget = globalVariable;
			SetResult(curId, castedVal);
			
			//SetResult(curId, globalVariable);
		}
		break;
	case BfIRCmd_CreateBlock:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(bool, addNow);
			auto block = mBeModule->CreateBlock(name);			
			if (addNow)
				mBeModule->AddBlock(mActiveFunction, block);

			SetResult(curId, block);
		}
		break;
	case BfIRCmd_MaybeChainNewBlock:
		{
			CMD_PARAM(String, name);
			auto newBlock = mBeModule->GetInsertBlock();
			if (!newBlock->IsEmpty())
			{
				auto bb = mBeModule->CreateBlock(name);
				mBeModule->CreateBr(bb);
				mBeModule->AddBlock(mActiveFunction, bb);
				mBeModule->SetInsertPoint(bb);				
				newBlock = bb;
			}
			SetResult(curId, newBlock);
		}
		break;
	case BfIRCmd_AddBlock:
		{
			CMD_PARAM(BeBlock*, block);
			mBeModule->AddBlock(mActiveFunction, block);			
		}
		break;
	case BfIRCmd_DropBlocks:
		{
			CMD_PARAM(BeBlock*, startingBlock);
			auto& basicBlockList = mActiveFunction->mBlocks;
			int postExitBlockIdx = -1;

			/*auto itr = basicBlockList.begin();				
			while (itr != basicBlockList.end())
			{
				auto block = *itr;				
				if (block == startingBlock)
				{
					basicBlockList.erase(itr, basicBlockList.end());
					break;
				}
				++itr;
			}*/

			for (int i = 0; i < (int)basicBlockList.size(); i++)
			{
				if (basicBlockList[i] == startingBlock)
				{
					basicBlockList.RemoveRange(i, basicBlockList.size() - i);
					break;
				}
			}
		}
		break;
	case BfIRCmd_MergeBlockDown:
		{
			CMD_PARAM(BeBlock*, fromBlock);
			CMD_PARAM(BeBlock*, intoBlock);			
			for (auto inst : fromBlock->mInstructions)
				inst->mParentBlock = intoBlock;
			if (!fromBlock->mInstructions.IsEmpty())
				intoBlock->mInstructions.Insert(0, &fromBlock->mInstructions[0], fromBlock->mInstructions.size());
			mBeModule->RemoveBlock(mActiveFunction, fromBlock);
		}
		break;	
	case BfIRCmd_SetInsertPoint:
		{			
			CMD_PARAM(BeBlock*, block);
			mBeModule->SetInsertPoint(block);
		}
		break;
	case BfIRCmd_SetInsertPointAtStart:
		{
			CMD_PARAM(BeBlock*, block);
			mBeModule->SetInsertPointAtStart(block);
		}
		break;
	case BfIRCmd_EraseFromParent:
		{
			CMD_PARAM(BeBlock*, block);
			mBeModule->RemoveBlock(mActiveFunction, block);
		}
		break;
	case BfIRCmd_DeleteBlock:
		{
			CMD_PARAM(BeBlock*, block);			
		}
		break;	
	case BfIRCmd_EraseInstFromParent:
		{
			CMD_PARAM(BeValue*, instVal);

			BeInst* inst = (BeInst*)instVal;
			bool wasRemoved = inst->mParentBlock->mInstructions.Remove(inst);
			BF_ASSERT(wasRemoved);
#ifdef _DEBUG
			inst->mWasRemoved = true;			
#endif
		}
		break;
	case BfIRCmd_CreateBr:
		{
			CMD_PARAM(BeBlock*, block);
			mBeModule->CreateBr(block);
		}
		break;
	case BfIRCmd_CreateBr_Fake:
		{
			CMD_PARAM(BeBlock*, block);
			auto inst = mBeModule->CreateBr(block);
			inst->mIsFake = true;		
		}
		break;
	case BfIRCmd_CreateBr_NoCollapse:
		{
			CMD_PARAM(BeBlock*, block);
			auto inst = mBeModule->CreateBr(block);
			inst->mNoCollapse = true;
		}
		break;
	case BfIRCmd_CreateCondBr:
		{
			CMD_PARAM(BeValue*, condVal);
			CMD_PARAM(BeBlock*, trueBlock);
			CMD_PARAM(BeBlock*, falseBlock);
			mBeModule->CreateCondBr(condVal, trueBlock, falseBlock);
		}
		break;
	case BfIRCmd_MoveBlockToEnd:
		{
			CMD_PARAM(BeBlock*, block);
			mBeModule->RemoveBlock(mActiveFunction, block);
			mBeModule->AddBlock(mActiveFunction, block);
		}
		break;
	case BfIRCmd_CreateSwitch:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeBlock*, dest);
			CMD_PARAM(int, numCases);

			auto switchInst = mBeModule->AllocInstOwned<BeSwitchInst>();
			switchInst->mValue = val;
			switchInst->mDefaultBlock = dest;
			switchInst->mCases.Reserve(numCases);			
			SetResult(curId, switchInst);
		}
		break;
	case BfIRCmd_AddSwitchCase:
		{			
			CMD_PARAM(BeValue*, switchVal);
			CMD_PARAM(BeValue*, caseVal);
			CMD_PARAM(BeBlock*, caseBlock);						

			BeSwitchCase switchCase;
			switchCase.mValue = (BeConstant*)caseVal;
			switchCase.mBlock = caseBlock;
			((BeSwitchInst*)switchVal)->mCases.push_back(switchCase);
		}
		break;
	case BfIRCmd_SetSwitchDefaultDest:
		{
			CMD_PARAM(BeValue*, switchVal);
			CMD_PARAM(BeBlock*, caseBlock);
			((BeSwitchInst*)switchVal)->mDefaultBlock = caseBlock;
		}
		break;
	case BfIRCmd_CreatePhi:
		{
			CMD_PARAM(BeType*, type);
			CMD_PARAM(int, incomingCount);

			auto phiInst = mBeModule->AllocInstOwned<BePhiInst>();
			phiInst->mType = type;
			SetResult(curId, phiInst);
		}
		break;
	case BfIRCmd_AddPhiIncoming:
		{
			CMD_PARAM(BeValue*, phiValue);
			CMD_PARAM(BeValue*, value);
			CMD_PARAM(BeBlock*, comingFrom);

			auto phiIncoming = mBeModule->mAlloc.Alloc<BePhiIncoming>();
			phiIncoming->mBlock = comingFrom;
			phiIncoming->mValue = value;

			((BePhiInst*)phiValue)->mIncoming.push_back(phiIncoming);
		}
		break;
	case BfIRCmd_GetIntrinsic:
		{			
			CMD_PARAM(int, intrinId);
			CMD_PARAM(BeType*, returnType);
			CMD_PARAM(CmdParamVec<BeType*>, paramTypes);

			auto intrin = mBeModule->mAlloc.Alloc<BeIntrinsic>();
			intrin->mKind = (BfIRIntrinsic)intrinId;

			switch (intrin->mKind)
			{			
			case BfIRIntrinsic_Abs:
				intrin->mReturnType = paramTypes[0];
				break;
			case BfIRIntrinsic_AtomicAdd:
			case BfIRIntrinsic_AtomicAnd:
			case BfIRIntrinsic_AtomicCmpXChg:
			case BfIRIntrinsic_AtomicLoad:
			case BfIRIntrinsic_AtomicMax:
			case BfIRIntrinsic_AtomicMin:
			case BfIRIntrinsic_AtomicNAnd:
			case BfIRIntrinsic_AtomicOr:			
			case BfIRIntrinsic_AtomicSub:
			case BfIRIntrinsic_AtomicUMax:
			case BfIRIntrinsic_AtomicUMin:
			case BfIRIntrinsic_AtomicXChg:			
			case BfIRIntrinsic_AtomicXor:
				if (!paramTypes.IsEmpty())
				{
					BF_ASSERT(paramTypes[0]->IsPointer());
					if (paramTypes[0]->IsPointer())
						intrin->mReturnType = ((BePointerType*)paramTypes[0])->mElementType;
				}
				else
					intrin->mReturnType = mBeContext->GetPrimitiveType(BeTypeCode_None);
				break;
			case BfIRIntrinsic_AtomicCmpStore:
			case BfIRIntrinsic_AtomicCmpStore_Weak:			
				intrin->mReturnType = mBeContext->GetPrimitiveType(BeTypeCode_Boolean);
				break;			
			case BfIRIntrinsic_Cast:
				intrin->mReturnType = returnType;
				break;
			}

			SetResult(curId, intrin);
		}
		break;
	case BfIRCmd_CreateFunctionType:
		{			
			CMD_PARAM(BeType*, resultType);
			CMD_PARAM(CmdParamVec<BeType*>, paramTypes);
			CMD_PARAM(bool, isVarArg);
			auto functionType = mBeContext->CreateFunctionType(resultType, paramTypes, isVarArg);
			SetResult(curId, functionType);
		}
		break;
	case BfIRCmd_CreateFunction:
		{
			CMD_PARAM(BeFunctionType*, type);
			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
			CMD_PARAM(String, name);
			SetResult(curId, mBeModule->CreateFunction(type, linkageType, name));			
		}
		break;
	case BfIRCmd_EnsureFunctionPatchable:
		{

		}
		break;
	case BfIRCmd_RemapBindFunction:
		{
			CMD_PARAM(BeValue*, func);
			// We need to store this value to a data segment so we get a symbol we can remap during hot swap
			//  We actually do this to ensure that we don't bind to the NEW method but rather the old one- so
			//  delegate equality checks still work

			BeFunction* beFunc = BeValueDynCast<BeFunction>(func);
			if (beFunc != NULL)
			{
				if (beFunc->mRemapBindVar == NULL)
				{
					auto globalVariable = mBeModule->mGlobalVariables.Alloc();
					globalVariable->mModule = mBeModule;
					globalVariable->mType = beFunc->mType;
					globalVariable->mIsConstant = true;
					globalVariable->mLinkageType = BfIRLinkageType_External;
					globalVariable->mInitializer = beFunc;
					globalVariable->mName = StrFormat("bf_hs_preserve@%s_%s", beFunc->mName.c_str(), mBeModule->mModuleName.c_str());
					globalVariable->mIsTLS = false;
					globalVariable->mAlign = 8;
					globalVariable->mUnnamedAddr = false;
					beFunc->mRemapBindVar = globalVariable;

					/*if (mBeModule->mDbgModule != NULL)
					{
						auto dbgGlobalVariable = mBeModule->mDbgModule->mGlobalVariables.Alloc();
						dbgGlobalVariable->mContext = mBeContext;
						dbgGlobalVariable->mName = name;
						dbgGlobalVariable->mLinkageName = globalVariable->mName;
						dbgGlobalVariable->mFile = (BeDbgFile*)file;
						dbgGlobalVariable->mLineNum = lineNum;
						dbgGlobalVariable->mType = (BeDbgType*)type;
						dbgGlobalVariable->mIsLocalToUnit = isLocalToUnit;
						dbgGlobalVariable->mValue = val;
						dbgGlobalVariable->mDecl = decl;
					}*/
				}
				
				SetResult(curId, mBeModule->CreateLoad(beFunc->mRemapBindVar, false));
			}
			else
				SetResult(curId, func);			
		}
		break;
	case BfIRCmd_SetActiveFunction:
		{
			CMD_PARAM(BeFunction*, func);			
			mActiveFunction = func;
			mBeModule->mActiveFunction = func;
		}
		break;
	case BfIRCmd_CreateCall:
		{			
			CMD_PARAM(BeValue*, func);
			CMD_PARAM(CmdParamVec<BeValue*>, args);
			
#ifdef _DEBUG			
			auto funcPtrType = func->GetType();
			if (funcPtrType != NULL)
			{
				BF_ASSERT(funcPtrType->IsPointer());
				auto funcType = (BeFunctionType*)((BePointerType*)funcPtrType)->mElementType;
				BF_ASSERT(funcType->mTypeCode == BeTypeCode_Function);
				if (!funcType->mIsVarArg)
				{
					BF_ASSERT(funcType->mParams.size() == args.size());
					int argIdx = 0;
					for (int argIdx = 0; argIdx < (int)args.size(); argIdx++)
					{
						BF_ASSERT(funcType->mParams[argIdx].mType == args[argIdx]->GetType());
					}
				}
			}
			else
			{
				BF_ASSERT(func->GetTypeId() == BeIntrinsic::TypeId);
			}
#endif
			SetResult(curId, mBeModule->CreateCall(func, args));			
		}
		break;
	case BfIRCmd_SetCallCallingConv:
		{
			CMD_PARAM(BeValue*, callInst);
			BfIRCallingConv callingConv = (BfIRCallingConv)mStream->Read();
			((BeCallInst*)callInst)->mCallingConv = callingConv;
		}
		break;
	case BfIRCmd_SetFuncCallingConv:
		{
			CMD_PARAM(BeFunction*, func);
			BfIRCallingConv callingConv = (BfIRCallingConv)mStream->Read();						
			func->mCallingConv = callingConv;
		}
		break;
	case BfIRCmd_SetTailCall:
		{
			CMD_PARAM(BeValue*, callInstVal);
			BeCallInst* callInst = (BeCallInst*)callInstVal;
			callInst->mTailCall = true;
		}
		break;
	case BfIRCmd_SetCallAttribute:
		{
			CMD_PARAM(BeValue*, callInstVal);	
			CMD_PARAM(int, paramIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();			
			BeCallInst* callInst = (BeCallInst*)callInstVal;
			if (attribute == BfIRAttribute_NoReturn)
				callInst->mNoReturn = true;
		}
		break;
	case BfIRCmd_CreateRet:
		{
			CMD_PARAM(BeValue*, val);
			SetResult(curId, mBeModule->CreateRet(val));
		}
		break;
	case BfIRCmd_CreateRetVoid:
		{
			mBeModule->CreateRetVoid();
		}
		break;
	case BfIRCmd_CreateUnreachable:
		{
			mBeModule->AllocInst<BeUnreachableInst>();
		}
		break;
	case BfIRCmd_Call_AddAttribute:
		{
			CMD_PARAM(BeValue*, callInstVal);
			CMD_PARAM(int, argIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			BeCallInst* callInst = (BeCallInst*)callInstVal;
			if (argIdx > 0)
			{
				if (attribute == BfIRAttribute_StructRet)
				{
					BF_ASSERT(argIdx == 1);
					callInst->mArgs[argIdx - 1].mStructRet = true;
				}
				else if (attribute == BfIRAttribute_ZExt)
					callInst->mArgs[argIdx - 1].mZExt = true;
				else if (attribute == BfIRAttribute_NoAlias)
					callInst->mArgs[argIdx - 1].mNoAlias = true;
				else if (attribute == BfIRAttribute_NoCapture)
					callInst->mArgs[argIdx - 1].mNoCapture = true;
				else
					BF_FATAL("Unhandled");
			}
			else
			{
				if (attribute == BfIRAttribute_NoReturn)
					callInst->mNoReturn = true;				
				else
					BF_FATAL("Unhandled");
			}			
		}
		break;
	case BfIRCmd_Call_AddAttribute1:
		{
			CMD_PARAM(BeValue*, inst);
			CMD_PARAM(int, argIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			CMD_PARAM(int, arg);
						
			BeCallInst* callInst = BeValueDynCast<BeCallInst>(inst);
			if (callInst != NULL)
			{
				if (argIdx > 0)
				{
					if (attribute == BfIRAttribute_Dereferencable)
						callInst->mArgs[argIdx - 1].mDereferenceableSize = arg;
					else
						BF_FATAL("Unhandled");
				}
				else
				{
					BF_FATAL("Unhandled");
				}
			}
		}
		break;
	case BfIRCmd_Func_AddAttribute:
		{
			CMD_PARAM(BeFunction*, func);
			CMD_PARAM(int, argIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			if (argIdx > 0)
			{
				if (attribute == BfIRAttribute_StructRet)
					func->mParams[argIdx - 1].mStructRet = true;
				else if (attribute == BfIRAttribute_NoAlias)
					func->mParams[argIdx - 1].mNoAlias = true;
				else if (attribute == BfIRAttribute_NoCapture)
					func->mParams[argIdx - 1].mNoCapture = true;
				else if (attribute == BfIRAttribute_ZExt)
					func->mParams[argIdx - 1].mZExt = true;
				else
					BF_FATAL("Unhandled");
			}
			else
			{
				if (attribute == BFIRAttribute_AlwaysInline)
					func->mAlwaysInline = true;				
				else if (attribute == BFIRAttribute_NoUnwind)
					func->mNoUnwind = true;
				else if (attribute == BFIRAttribute_UWTable)
					func->mUWTable = true;
				else if (attribute == BfIRAttribute_NoReturn)
					func->mNoReturn = true;
				else if (attribute == BFIRAttribute_NoFramePointerElim)
					func->mNoFramePointerElim = true;
				else if (attribute == BFIRAttribute_DllExport)
					func->mIsDLLExport = true;
				else if (attribute == BFIRAttribute_DllImport)
					func->mIsDLLImport = true;
				else if (attribute == BFIRAttribute_NoRecurse)
				{
				}
				else
					BF_FATAL("Unhandled");
			}			
		}
		break;
	case BfIRCmd_Func_AddAttribute1:
		{
			CMD_PARAM(BeFunction*, func);
			CMD_PARAM(int, argIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			CMD_PARAM(int, arg);			
			// This is for adding things like Dereferencable, which we don't use

			if (argIdx > 0)
			{
				if (attribute == BfIRAttribute_Dereferencable)
					func->mParams[argIdx - 1].mDereferenceableSize = arg;
				else
					BF_FATAL("Unhandled");
			}
			else
				BF_FATAL("Unhandled");
		}
		break;
	case BfIRCmd_Func_SetParamName:
		{
			CMD_PARAM(BeFunction*, func);
			CMD_PARAM(int, argIdx);
			CMD_PARAM(String, name);
			if (argIdx > 0)
				func->mParams[argIdx - 1].mName = name;						
		}
		break;		
	case BfIRCmd_Func_DeleteBody:
		{
			CMD_PARAM(BeFunction*, func);
			func->mBlocks.Clear();
		}
		break;
	case BfIRCmd_Func_SetLinkage:
		{
			CMD_PARAM(BeFunction*, func);
			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();				
			func->mLinkageType = linkageType;
		}
		break;
	case BfIRCmd_SaveDebugLocation:
		{
			mSavedDebugLocs.push_back(mBeModule->GetCurrentDebugLocation());
		}
		break;
	case BfIRCmd_RestoreDebugLocation:
		{
			mBeModule->SetCurrentDebugLocation(mSavedDebugLocs[mSavedDebugLocs.size() - 1]);
			mSavedDebugLocs.pop_back();
		}
		break;
	case BfIRCmd_ClearDebugLocation:
		{
			mBeModule->SetCurrentDebugLocation(NULL);
		}
		break;	
	case BfIRCmd_ClearDebugLocationInst:
		{
			CMD_PARAM(BeValue*, instValue);
			auto inst = (BeInst*)instValue;
			inst->mDbgLoc = NULL;
		}
		break;
	case BfIRCmd_ClearDebugLocationInstLast:
		{
			if ((mBeModule->mActiveBlock != NULL) && (!mBeModule->mActiveBlock->mInstructions.IsEmpty()))
			{
				auto inst = mBeModule->mActiveBlock->mInstructions.back();
				inst->mDbgLoc = NULL;
			}			
		}
		break;
	case BfIRCmd_UpdateDebugLocation:
		{
			CMD_PARAM(BeValue*, instValue);
			auto inst = (BeInst*)instValue;
			inst->mDbgLoc = mBeModule->mCurDbgLoc;
		}
		break;
	case BfIRCmd_SetCurrentDebugLocation:
		{
			CMD_PARAM(int, line);
			CMD_PARAM(int, column);
			CMD_PARAM(BeMDNode*, diScope);
			CMD_PARAM(BeMDNode*, diInlinedAt);			
			mBeModule->SetCurrentDebugLocation(line - 1, column - 1, diScope, (BeDbgLoc*)diInlinedAt);
		}
		break;
	case BfIRCmd_Nop:
		{
			mBeModule->CreateNop();			
		}
		break;
	case BfIRCmd_EnsureInstructionAt:
		{
			mBeModule->AllocInst<BeEnsureInstructionAtInst>();
		}
		break;
	case BfIRCmd_StatementStart:
		{

		}
		break;
	case BfIRCmd_ObjectAccessCheck:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(bool, useAsm);

			auto inst = mBeModule->AllocInst<BeObjectAccessCheckInst>();
			inst->mValue = val;

			SetResult(curId, mBeModule->GetInsertBlock());			
		}
		break;
	case BfIRCmd_DbgInit:
		{
			/*mDIBuilder = new BeDIBuilder(*mBeModule);			*/
			mBeModule->mDbgModule = new BeDbgModule();
			mBeModule->mDbgModule->mBeModule = mBeModule;
		}
		break;
	case BfIRCmd_DbgFinalize:
		{
			/*for (auto typeEntryPair : mTypes)
			{
				auto& typeEntry = typeEntryPair.second;
				if (typeEntry.mInstDIType != NULL)
					typeEntry.mInstDIType->resolveCycles();
			}
			mDIBuilder->finalize();*/
			
		}
		break;
	case BfIRCmd_DbgCreateCompileUnit:
		{
			CMD_PARAM(int, lang);
			CMD_PARAM(String, fileName);
			CMD_PARAM(String, directory);
			CMD_PARAM(String, producer);
			CMD_PARAM(bool, isOptimized);
			CMD_PARAM(String, flags);
			CMD_PARAM(int, runtimeVer);
			CMD_PARAM(bool, linesOnly);
			
			mBeModule->mDbgModule->mFileName = fileName;
			mBeModule->mDbgModule->mDirectory = directory;
			mBeModule->mDbgModule->mProducer = producer;

			//mDebugging = fileName == "TestToots";

			SetResult(curId, mBeModule->mDbgModule);
		}
		break;
	case BfIRCmd_DbgCreateFile:
		{
			CMD_PARAM(String, fileName);
			CMD_PARAM(String, directory);
			CMD_PARAM(Val128, md5Hash);

			auto dbgFile = mBeModule->mDbgModule->mFiles.Alloc();
			dbgFile->mFileName = fileName;			
			dbgFile->mDirectory = directory;
			dbgFile->mMD5Hash = md5Hash;
			dbgFile->mIdx = (int)mBeModule->mDbgModule->mFiles.size() - 1;

			SetResult(curId, dbgFile);			
		}
		break;
	case BfIRCmd_DbgGetCurrentLocation:
		{
			SetResult(curId, mBeModule->mCurDbgLoc);			
		}
		break;
	case BfIRCmd_DbgSetType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(BeMDNode*, type);
			GetTypeEntry(typeId).mDIType = (BeDbgType*)type;			
		}
		break;
	case BfIRCmd_DbgSetInstType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(BeMDNode*, type);
			GetTypeEntry(typeId).mInstDIType = (BeDbgType*)type;
		}
		break;
	case BfIRCmd_DbgGetType:
		{
			CMD_PARAM(int, typeId);
			SetResult(curId, GetTypeEntry(typeId).mDIType);
		}
		break;
	case BfIRCmd_DbgGetTypeInst:
		{
			CMD_PARAM(int, typeId);
			SetResult(curId, GetTypeEntry(typeId).mInstDIType);
		}
		break;
	case BfIRCmd_DbgTrackDITypes:
		{
			CMD_PARAM(int, typeId);
			auto& typeEntry = GetTypeEntry(typeId);

			/*if (typeEntry.mDIType != NULL)
				BeMetadataTracking::track(*(BeMetadata**)&typeEntry.mDIType);
			if (typeEntry.mInstDIType != NULL)
				BeMetadataTracking::track(*(BeMetadata**)&typeEntry.mInstDIType);*/
			//NotImpl();
		}
		break;
	case BfIRCmd_DbgCreateNamespace:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);

			auto dbgNamespace = mBeModule->mOwnedValues.Alloc<BeDbgNamespace>();
			dbgNamespace->mScope = scope;
			dbgNamespace->mName = name;
			SetResult(curId, dbgNamespace);			
		}
		break;
	case BfIRCmd_DbgCreateImportedModule:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(BeMDNode*, namespaceNode);
			CMD_PARAM(int, lineNum);
			/*SetResult(curId, mDIBuilder->createImportedModule((BeDIScope*)context, (BeDINamespace*)namespaceNode, lineNum));*/
			//NotImpl();
		}
		break;
	case BfIRCmd_DbgCreateBasicType:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, encoding);

			auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgBasicType>();
			dbgType->mName = name;
			dbgType->mSize = (int)(sizeInBits / 8);
			dbgType->mAlign = (int)(alignInBits / 8);
			dbgType->mEncoding = encoding;
			
			SetResult(curId, dbgType);
		}
		break;
	case BfIRCmd_DbgCreateStructType:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, flags);
			CMD_PARAM(BeMDNode*, derivedFrom);
			CMD_PARAM(CmdParamVec<BeMDNode*>, members);
			
			auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgStructType>();
			dbgType->mScope = context;
			dbgType->mName = name;
			dbgType->mSize = (int)(sizeInBits / 8);
			dbgType->mAlign = (int)(alignInBits / 8);			
			dbgType->mDerivedFrom = (BeDbgType*)derivedFrom;
			dbgType->mDefFile = (BeDbgFile*)file;
			dbgType->mDefLine = lineNum - 1;
			dbgType->mIsFullyDefined = true;			
			dbgType->SetMembers(members);
			
			SetResult(curId, dbgType);
		}
		break;	
	case BfIRCmd_DbgCreateEnumerationType:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);						
			CMD_PARAM(CmdParamVec<BeMDNode*>, members);
			CMD_PARAM(BeMDNode*, underlyingType);
			
			auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgEnumType>();
			dbgType->mScope = context;
			dbgType->mName = name;
			dbgType->mSize = (int)(sizeInBits / 8);
			dbgType->mAlign = (int)(alignInBits / 8);
			dbgType->mIsFullyDefined = true;
			dbgType->mElementType = (BeDbgType*)underlyingType;
			for (auto member : members)
			{
				if (auto enumMember = BeValueDynCast<BeDbgEnumMember>(member))
				{
					dbgType->mMembers.push_back(enumMember);
				}				
				else
					NotImpl();
			}

			//dbgType->mDefFile = (BeDbgFile*)file;
			//dbgType->mDefLine = line - 1;
			SetResult(curId, dbgType);
		}
		break;
	case BfIRCmd_DbgCreatePointerType:
		{			
			CMD_PARAM(BeMDNode*, elementTypeNode);
			
			BeDbgType* elementType = (BeDbgType*)elementTypeNode;
			BeDbgType* useType = elementType->FindDerivedType(BeDbgPointerType::TypeId);
			if (useType == NULL)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgPointerType>();
				dbgType->mElement = elementType;
				dbgType->mSize = mPtrSize;
				dbgType->mAlign = mPtrSize;
				elementType->mDerivedTypes.PushFront(dbgType, &mBeModule->mAlloc);
				useType = dbgType;
			}			

			SetResult(curId, useType);
		}
		break;
	case BfIRCmd_DbgCreateReferenceType:
		{
			CMD_PARAM(BeMDNode*, elementTypeNode);
			auto useType = mBeModule->mDbgModule->CreateReferenceType((BeDbgType*)elementTypeNode);
			SetResult(curId, useType);
		}
		break;
	case BfIRCmd_DbgCreateConstType:
		{
			CMD_PARAM(BeMDNode*, elementTypeNode);

			BeDbgType* elementType = (BeDbgType*)elementTypeNode;
			BeDbgType* useType = elementType->FindDerivedType(BeDbgConstType::TypeId);
			if (useType == NULL)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgConstType>();
				dbgType->mElement = elementType;
				elementType->mDerivedTypes.PushFront(dbgType, &mBeModule->mAlloc);
				useType = dbgType;
			}

			SetResult(curId, useType);
		}
		break;
	case BfIRCmd_DbgCreateArtificialType:
		{
			CMD_PARAM(BeMDNode*, diType);

			//auto dbgType = mBeModule->mOwnedValues.Alloc<BeDbgArtificialType>();
			//dbgType->mElement = (BeDbgType*)diType;

			// Does the artificial thing do anything for us actually?
			auto dbgType = diType;

			SetResult(curId, dbgType);		
		}
		break;
	case BfIRCmd_DbgCreateArrayType:
		{
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(BeMDNode*, elementType);
			CMD_PARAM(int64, numElements);

			auto dbgArray = mBeModule->mDbgModule->mTypes.Alloc<BeDbgArrayType>();
			dbgArray->mSize = (int)(sizeInBits / 8);
			dbgArray->mAlign = (int)(alignInBits / 8);
			dbgArray->mElement = (BeDbgType*)elementType;
			dbgArray->mNumElements = numElements;

			SetResult(curId, dbgArray);
		}
		break;
	case BfIRCmd_DbgCreateReplaceableCompositeType:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, line);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, flags);

			if (tag == llvm::dwarf::DW_TAG_structure_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgStructType>();
				dbgType->mScope = scope;
				dbgType->mName = name;
				dbgType->mSize = (int)(sizeInBits / 8);
				dbgType->mAlign = (int)(alignInBits / 8);
				dbgType->mDefFile = (BeDbgFile*)file;
				dbgType->mDefLine = line - 1;
				SetResult(curId, dbgType);
			}
			else if (tag == llvm::dwarf::DW_TAG_enumeration_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgEnumType>();
				dbgType->mScope = scope;
				dbgType->mName = name;
				dbgType->mSize = (int)(sizeInBits / 8);
				dbgType->mAlign = (int)(alignInBits / 8);				
				//dbgType->mDefFile = (BeDbgFile*)file;
				//dbgType->mDefLine = line - 1;
				SetResult(curId, dbgType);
			}
			else
				NotImpl();			
		}
		break;
	case BfIRCmd_DbgCreateForwardDecl:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, line);

			if (tag == llvm::dwarf::DW_TAG_structure_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgStructType>();
				dbgType->mScope = scope;
				dbgType->mName = name;		
				dbgType->mDefFile = (BeDbgFile*)file;
				dbgType->mDefLine = line;
				SetResult(curId, dbgType);
			}
			else if (tag == llvm::dwarf::DW_TAG_enumeration_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgEnumType>();
				dbgType->mScope = scope;
				dbgType->mName = name;
				SetResult(curId, dbgType);
			}
			else
				NotImpl();			
		}
		break;
	case BfIRCmd_DbgCreateSizedForwardDecl:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, line);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			
			if (tag == llvm::dwarf::DW_TAG_structure_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgStructType>();
				dbgType->mDefFile = (BeDbgFile*)file;
				dbgType->mDefLine = line;
				dbgType->mScope = scope;
				dbgType->mName = name;
				dbgType->mSize = (int)(sizeInBits / 8);
				dbgType->mAlign = (int)(alignInBits / 8);
				SetResult(curId, dbgType);
			}
			else if (tag == llvm::dwarf::DW_TAG_enumeration_type)
			{
				auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgEnumType>();				
				dbgType->mScope = scope;
				dbgType->mName = name;
				dbgType->mSize = (int)(sizeInBits / 8);
				dbgType->mAlign = (int)(alignInBits / 8);
				SetResult(curId, dbgType);
			}
			else
				NotImpl();
		}
		break;
	case BeIRCmd_DbgSetTypeSize:
		{
			CMD_PARAM(BeMDNode*, mdType);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);

			auto dbgType = (BeDbgType*)mdType;
			dbgType->mSize = (int)(sizeInBits / 8);
			dbgType->mAlign = (int)(alignInBits / 8);
		}
		break;
	case BfIRCmd_DbgReplaceAllUses:
		{			
			CMD_PARAM(BeMDNode*, diPrevNode);
			CMD_PARAM(BeMDNode*, diNewNode);
			/*diPrevNode->replaceAllUsesWith(diNewNode); 	*/
			NotImpl();
		}
		break;
	case BfIRCmd_DbgDeleteTemporary:
		{
			CMD_PARAM(BeMDNode*, diNode);
			/*BeMDNode::deleteTemporary(diNode);*/
			NotImpl();
		}
		break;
	case BfIRCmd_DbgMakePermanent:
		{
			CMD_PARAM(BeMDNode*, diNode);
			CMD_PARAM(BeMDNode*, diBaseType);
			CMD_PARAM(CmdParamVec<BeMDNode*>, members);			
			
			if (auto dbgType = BeValueDynCast<BeDbgStructType>(diNode))
			{
				dbgType->SetMembers(members);
			}			
			else if (auto dbgType = BeValueDynCast<BeDbgEnumType>(diNode))
			{
				dbgType->mElementType = (BeDbgType*)diBaseType;
				dbgType->SetMembers(members);
			}
			else
				NotImpl();
			SetResult(curId, diNode);
			break;
		}
	case BfIRCmd_CreateEnumerator:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(int64, val);

			auto enumValue = mBeModule->mOwnedValues.Alloc<BeDbgEnumMember>();
			enumValue->mName = name;
			enumValue->mValue = val;
			SetResult(curId, enumValue);
		}
		break;
	case BfIRCmd_DbgCreateMemberType:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNumber);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int64, offsetInBits);
			CMD_PARAM(int, flags);
			CMD_PARAM(BeMDNode*, type);

			auto dbgMember = mBeModule->mOwnedValues.Alloc<BeDbgStructMember>();
			dbgMember->mName = name;
			dbgMember->mType = (BeDbgType*)type;
			dbgMember->mOffset = (int)(offsetInBits / 8);
			dbgMember->mFlags = flags;

			SetResult(curId, dbgMember);
		}
		break;
	case BfIRCmd_DbgStaticCreateMemberType:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNumber);
			CMD_PARAM(BeMDNode*, type);
			CMD_PARAM(int, flags);
			CMD_PARAM(BeConstant*, val);
			
			auto dbgMember = mBeModule->mOwnedValues.Alloc<BeDbgStructMember>();
			dbgMember->mName = name;
			dbgMember->mType = (BeDbgType*)type;
			dbgMember->mOffset = -1;
			dbgMember->mStaticValue = val;
			dbgMember->mFlags = flags;
			dbgMember->mIsStatic = true;

			SetResult(curId, dbgMember);
		}
		break;
	case BfIRCmd_DbgCreateInheritance:
		{
			CMD_PARAM(BeMDNode*, type);
			CMD_PARAM(BeMDNode*, baseType);
			CMD_PARAM(int64, baseOffset);
			CMD_PARAM(int, flags);

			auto dbgInheritance = mBeModule->mAlloc.Alloc<BeDbgInheritance>();
			dbgInheritance->mBaseType = (BeDbgType*)baseType;

			SetResult(curId, dbgInheritance);
		}
		break;
	case BfIRCmd_DbgCreateMethod:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum); 
			CMD_PARAM(BeMDNode*, type); 
			CMD_PARAM(bool, isLocalToUnit);
			CMD_PARAM(bool, isDefinition);
			CMD_PARAM(int, vk);
			CMD_PARAM(int, vIndex);
			CMD_PARAM(BeMDNode*, vTableHolder);
			CMD_PARAM(int, flags);
			CMD_PARAM(bool, isOptimized);
			CMD_PARAM(BeValue*, fn);
			CMD_PARAM(CmdParamVec<BeMDNode*>, genericArgs);
			CMD_PARAM(CmdParamVec<BeConstant*>, genericConstValueArgs);

			auto dbgFunc = mBeModule->mOwnedValues.Alloc<BeDbgFunction>();
			dbgFunc->mScope = context;
			dbgFunc->mFile = (BeDbgFile*)file;
			dbgFunc->mLine = lineNum - 1;
			dbgFunc->mType = (BeDbgFunctionType*)type;
			dbgFunc->mName = name;
			dbgFunc->mLinkageName = linkageName;
			dbgFunc->mValue = (BeFunction*)fn;
			dbgFunc->mIsLocalToUnit = isLocalToUnit;
			dbgFunc->mVK = vk;
			dbgFunc->mVIndex = vIndex;
			dbgFunc->mIsStaticMethod = (flags & llvm::DINode::FlagStaticMember) != 0;
			dbgFunc->mFlags = flags;

			for (auto arg : genericArgs)					
			{
				BF_ASSERT(arg != NULL);
				dbgFunc->mGenericArgs.Add((BeDbgType*)arg);
			}
			for (auto genericConstValue : genericConstValueArgs)
				dbgFunc->mGenericConstValueArgs.Add(genericConstValue);
			
			if (dbgFunc->mValue != NULL)
				dbgFunc->mValue->mDbgFunction = dbgFunc;
			dbgFunc->mIdx = (int)mBeModule->mDbgModule->mFuncs.size();
			mBeModule->mDbgModule->mFuncs.push_back(dbgFunc);

			SetResult(curId, dbgFunc);
		}
		break;
	case BfIRCmd_DbgCreateFunction:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum); 
			CMD_PARAM(BeMDNode*, type); 
			CMD_PARAM(bool, isLocalToUnit);
			CMD_PARAM(bool, isDefinition);		
			CMD_PARAM(int, scopeLine);
			CMD_PARAM(int, flags);
			CMD_PARAM(bool, isOptimized);
			CMD_PARAM(BeValue*, fn);
						
			auto dbgFunc = mBeModule->mOwnedValues.Alloc<BeDbgFunction>();
			dbgFunc->mScope = context;
			dbgFunc->mFile = (BeDbgFile*)file;
			dbgFunc->mLine = lineNum - 1;
			dbgFunc->mType = (BeDbgFunctionType*)type;
			dbgFunc->mName = name;
			dbgFunc->mLinkageName = linkageName;
			dbgFunc->mValue = (BeFunction*)fn;
			dbgFunc->mIsLocalToUnit = isLocalToUnit;
			dbgFunc->mFlags = flags;

			/*if (auto dbgStructType = BeValueDynCast<BeDbgStructType>(context))
			{
				// This will get added to the struct later
			}
			else
			{
				
			}*/
			if (dbgFunc->mValue != NULL)
				dbgFunc->mValue->mDbgFunction = dbgFunc;
			dbgFunc->mIdx = (int)mBeModule->mDbgModule->mFuncs.size();
			mBeModule->mDbgModule->mFuncs.push_back(dbgFunc);						

			SetResult(curId, dbgFunc);
		}
		break;
	case BfIRCmd_DbgCreateParameterVariable:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(int, argNo);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(BeMDNode*, type);
			CMD_PARAM(bool, alwaysPreserve);
			CMD_PARAM(int, flags);

			auto dbgFunc = (BeDbgFunction*)scope;

			auto dbgVar = mBeModule->mOwnedValues.Alloc<BeDbgVariable>();
			dbgVar->mName = name;
			dbgVar->mType = (BeDbgType*)type;
			dbgVar->mParamNum = argNo - 1;

			int argIdx = argNo - 1;

			//for (int i = ; i <= argNo - 1; i++)
			while (argIdx >= (int)dbgFunc->mVariables.size())
				dbgFunc->mVariables.push_back(NULL);
			if (dbgFunc->mVariables[argIdx] == NULL)
				dbgFunc->mVariables[argIdx] = dbgVar;
			else
			{
				BF_ASSERT(dbgFunc->mVariables[argIdx]->mParamNum == -1);
				dbgFunc->mVariables.Insert(argIdx, dbgVar);
			}
			//mActiveFunction->mDbgFunction->mVariables.push_back(dbgVar);
			
			//dbgVar->mValue = mBeModule->GetArgument(argNo - 1);

			SetResult(curId, dbgVar);
		}
		break;
	case BfIRCmd_DbgCreateSubroutineType:
		{
			CMD_PARAM(CmdParamVec<BeMDNode*>, elements);

			auto dbgFuncType = mBeModule->mOwnedValues.Alloc<BeDbgFunctionType>();
			if (!elements.empty())
			{
				dbgFuncType->mReturnType = (BeDbgType*)elements[0];
				for (int i = 1; i < (int)elements.size(); i++)
					dbgFuncType->mParams.push_back((BeDbgType*)elements[i]);
			}

			SetResult(curId, dbgFuncType);
		}
		break;
	case BfIRCmd_DbgCreateAutoVariable:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNo);
			CMD_PARAM(BeMDNode*, type);
			CMD_PARAM(int, initType);

			auto dbgVar = mBeModule->mOwnedValues.Alloc<BeDbgVariable>();
			dbgVar->mName = name;
			dbgVar->mType = (BeDbgType*)type;
			dbgVar->mScope = scope;
			dbgVar->mInitType = (BfIRInitType)initType;
			mActiveFunction->mDbgFunction->mVariables.push_back(dbgVar);

			BF_ASSERT(name != "__CRASH_AUTOVARIABLE__");

			SetResult(curId, dbgVar);
		}
		break;
	case BfIRCmd_DbgInsertValueIntrinsic:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeMDNode*, varInfo);

			auto dbgVar = BeValueDynCast<BeDbgVariable>(varInfo);
			//dbgVar->mValue = val;
			//dbgVar->mDeclDbgLoc = mBeModule->mCurDbgLoc;

			if (val == NULL)
			{
				val = mBeModule->GetConstant(mBeContext->GetPrimitiveType(BeTypeCode_Int32), (int64)0);
			}

			auto inst = mBeModule->AllocInst<BeDbgDeclareInst>();
			inst->mValue = val;
			inst->mDbgVar = dbgVar;
			inst->mIsValue = true;

			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_DbgInsertDeclare:
		{
			CMD_PARAM(BeValue*, val);
			CMD_PARAM(BeMDNode*, varInfo);
			CMD_PARAM(BeValue*, insertBefore);

			auto dbgVar = BeValueDynCast<BeDbgVariable>(varInfo);
			//dbgVar->mValue = val;
			//dbgVar->mDeclDbgLoc = mBeModule->mCurDbgLoc;

			auto inst = mBeModule->mAlloc.Alloc<BeDbgDeclareInst>();
			inst->mValue = val;
			inst->mDbgVar = dbgVar;
			inst->mIsValue = false;
			if (insertBefore == NULL)
				mBeModule->AddInst(inst);
			else
				NotImpl();

			SetResult(curId, inst);
		}
		break;
	case BfIRCmd_DbgLifetimeEnd:
		{
			CMD_PARAM(BeMDNode*, varInfo);
			auto inst = mBeModule->AllocInst<BeLifetimeEndInst>();
			inst->mPtr = varInfo;
		}
		break;
	case BfIRCmd_DbgCreateGlobalVariable:
		{
			CMD_PARAM(BeMDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(BeMDNode*, type);
			CMD_PARAM(bool, isLocalToUnit);			
			CMD_PARAM(BeConstant*, val);
			CMD_PARAM(BeMDNode*, decl);

			auto dbgGlobalVariable = mBeModule->mDbgModule->mGlobalVariables.Alloc();			
			dbgGlobalVariable->mContext = context;
			dbgGlobalVariable->mName = name;
			dbgGlobalVariable->mLinkageName = linkageName;
			dbgGlobalVariable->mFile = (BeDbgFile*)file;
			dbgGlobalVariable->mLineNum = lineNum;
			dbgGlobalVariable->mType = (BeDbgType*)type;
			dbgGlobalVariable->mIsLocalToUnit = isLocalToUnit;
			dbgGlobalVariable->mValue = val;
			dbgGlobalVariable->mDecl = decl;

			SetResult(curId, dbgGlobalVariable);
		}
		break;
	case BfIRCmd_DbgCreateLexicalBlock:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(BeMDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int, col);

			auto dbgLexicalBlock = mBeModule->mOwnedValues.Alloc<BeDbgLexicalBlock>();
			BF_ASSERT(BeValueDynCast<BeDbgFile>(file) != NULL);
			dbgLexicalBlock->mFile = (BeDbgFile*)file;
			dbgLexicalBlock->mScope = scope;
			dbgLexicalBlock->mId = mBeModule->mCurLexBlockId++;
			
			SetResult(curId, dbgLexicalBlock);
		}
		break;	
	case BfIRCmd_DbgCreateAnnotation:
		{
			CMD_PARAM(BeMDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(BeValue*, value);

			if (auto dbgFunc = BeValueDynCast<BeDbgFunction>(scope))
			{
				auto beType = value->GetType();
				BeDbgType** dbgTypePtr;
				if (mOnDemandTypeMap.TryAdd(beType, NULL, &dbgTypePtr))
				{
					auto dbgType = mBeModule->mDbgModule->mTypes.Alloc<BeDbgBasicType>();					
					dbgType->mSize = beType->mSize;
					dbgType->mAlign = beType->mAlign;
					dbgType->mEncoding = llvm::dwarf::DW_ATE_signed;
					*dbgTypePtr = dbgType;
				}
				
				auto dbgVar = mBeModule->mOwnedValues.Alloc<BeDbgVariable>();
				dbgVar->mName = "#" + name;
				dbgVar->mType = *dbgTypePtr;
				dbgVar->mValue = value;
				dbgVar->mScope = scope;
				dbgFunc->mVariables.Add(dbgVar);

				auto inst = mBeModule->AllocInst<BeDbgDeclareInst>();
				inst->mValue = value;
				inst->mDbgVar = dbgVar;
				inst->mIsValue = true;							
			}
			else
				NotImpl();
		}
		break;
	default:
		BF_FATAL("Unhandled");
		break;
	}	

#ifdef CODEGEN_TRACK	
	gBEMemReporter.EndSection();

	gBEMemReporterSize += mStream->GetReadPos() - curId;
#endif
}

void BeIRCodeGen::SetConfigConst(int idx, int value)
{
	BF_ASSERT(idx == (int)mConfigConsts.size());
	mConfigConsts.Add(value);		
}

BeValue* BeIRCodeGen::GetBeValue(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Value);
#ifdef _DEBUG
	BF_ASSERT(!result.mBeValue->mLifetimeEnded);
	BF_ASSERT(!result.mBeValue->mWasRemoved);
#endif
	return result.mBeValue;
}

BeType* BeIRCodeGen::GetBeType(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Type);
	return result.mBeType;
}

BeBlock* BeIRCodeGen::GetBeBlock(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Block);
	return result.mBeBlock;
}

BeMDNode* BeIRCodeGen::GetBeMetadata(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BeIRCodeGenEntryKind_Metadata);
	return result.mBeMetadata;
}

BeType* BeIRCodeGen::GetBeTypeById(int id)
{
	return GetTypeEntry(id).mBeType;
}
