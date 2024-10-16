#include "BfIRCodeGen.h"
#include "BfModule.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/Hash.h"

#ifdef BF_PLATFORM_WINDOWS
#include <io.h>
#endif

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
#include "llvm/IR/Attributes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Host.h"
//#include "llvm/Support/Dwarf.h"
#include "llvm/IR/DIBuilder.h"

//#include "llvm/ADT/Triple.h"
//#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
//#include "llvm/MC/SubtargetFeature.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Pass.h"
//#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
//#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
//#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
//#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
//#include "llvm/Target/TargetSubtargetInfo.h"
//#include "llvm/Transforms/IPO/PassManagerBuilder.h"
//#include "llvm-c/Transforms/PassManagerBuilder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
//#include "llvm/Analysis/CFLAliasAnalysis.h"
//#include "llvm/Analysis/CFLAndersAliasAnalysis.h"
//#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
//#include "llvm/Transforms/Vectorize.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Passes/PassBuilder.h"

//#include "llvm/Bitcode/ReaderWriter.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
//#include "llvm/Transforms/Vectorize.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/TargetRegistry.h"

#include "llvm/LTO/LTOBackend.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO.h"

#include "../LLVMUtils.h"

#pragma warning(pop)

void pm(llvm::Module* module);

USING_NS_BF;

#pragma warning(disable:4146)
#pragma warning(disable:4996)

struct BuiltinEntry
{
	const char* mName;
	bool operator<(const StringImpl& rhs) const
	{
		return strcmp(mName, rhs.c_str()) < 0;
	}
};

static const BuiltinEntry gIntrinEntries[] =
{
	{":PLATFORM"},
	{"abs"},
	{"add"},
	{"and"},
	{"atomic_add"},
	{"atomic_and"},
	{"atomic_cmpstore"},
	{"atomic_cmpstore_weak"},
	{"atomic_cmpxchg"},
	{"atomic_fence"},
	{"atomic_load"},
	{"atomic_max"},
	{"atomic_min"},
	{"atomic_nand"},
	{"atomic_or"},
	{"atomic_store"},
	{"atomic_sub"},
	{"atomic_umax"},
	{"atomic_umin"},
	{"atomic_xchg"},
	{"atomic_xor"},
	{"bswap"},
	{"cast"},
	{"cos"},
	{"cpuid"},
	{"debugtrap"},
	{"div"},
	{"eq"},
	{"floor"},
	{"free"},
	{"gt"},
	{"gte"},
	("index"),
	{"log"},
	{"log10"},
	{"log2"},
	{"lt"},
	{"lte"},
	{"malloc"},
	{"max"},
	{"memcpy"},
	{"memmove"},
	{"memset"},
	{"min"},
	{"mod"},
	{"mul"},
	{"neq"},
	{"not"},
	{"or"},
	{"pow"},
	{"powi"},
	{"returnaddress"},
	{"round"},
	{"sar"},
	{"shl"},
	{"shr"},
	{"shuffle"},
	{"sin"},
	{"sqrt"},
	{"sub"},
	{"va_arg"},
	{"va_end"},
	{"va_start"},
	{"xgetbv"},
	{"xor"},
};

#define CMD_PARAM(ty, name) ty name; Read(name);
#define CMD_PARAM_NOTRANS(ty, name) ty name; Read(name, NULL, BfIRSizeAlignKind_NoTransform);
BF_STATIC_ASSERT(BF_ARRAY_COUNT(gIntrinEntries) == BfIRIntrinsic_COUNT);

template <typename T>
class CmdParamVec : public llvm::SmallVector<T, 8>
{};

static int GetLLVMCallingConv(BfIRCallingConv callingConv, BfTargetTriple& targetTriple)
{
	int llvmCallingConv = llvm::CallingConv::C;

	if (targetTriple.GetMachineType() == BfMachineType_AArch64)
	{
		if (callingConv == BfIRCallingConv_CDecl)
			llvmCallingConv = llvm::CallingConv::C;
		else
			llvmCallingConv = llvm::CallingConv::PreserveMost;
	}
	else
	{
		if (callingConv == BfIRCallingConv_ThisCall)
			llvmCallingConv = llvm::CallingConv::X86_ThisCall;
		else if (callingConv == BfIRCallingConv_StdCall)
			llvmCallingConv = llvm::CallingConv::X86_StdCall;
		else if (callingConv == BfIRCallingConv_FastCall)
			llvmCallingConv = llvm::CallingConv::X86_FastCall;
		else if (callingConv == BfIRCallingConv_CDecl)
			llvmCallingConv = llvm::CallingConv::C;
	}

	return llvmCallingConv;
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

static llvm::Attribute::AttrKind LLVMMapAttribute(BfIRAttribute attr)
{
	switch (attr)
	{
	case BfIRAttribute_NoReturn: return llvm::Attribute::NoReturn;
	case BfIRAttribute_NoAlias: return llvm::Attribute::NoAlias;
	case BfIRAttribute_NoCapture: return llvm::Attribute::NoCapture;
	case BfIRAttribute_StructRet: return llvm::Attribute::StructRet;
	case BfIRAttribute_ZExt: return llvm::Attribute::ZExt;
	case BFIRAttribute_NoUnwind: return llvm::Attribute::NoUnwind;
	case BFIRAttribute_UWTable: return llvm::Attribute::UWTable;
	case BFIRAttribute_AlwaysInline: return llvm::Attribute::AlwaysInline;
	case BFIRAttribute_NoRecurse: return llvm::Attribute::NoRecurse;
	default: break;
	}
	return llvm::Attribute::None;
}

#ifdef BF_PLATFORM_WINDOWS
struct BfTempFile
{
	String mContents;
	String mFilePath;
	CritSect mCritSect;
	FILE* mFP;

	BfTempFile()
	{
		mFP = NULL;
	}

	~BfTempFile()
	{
		if (mFP != NULL)
			fclose(mFP);
		if (!mFilePath.IsEmpty())
			::DeleteFileW(UTF8Decode(mFilePath).c_str());
	}

	bool Create()
	{
		AutoCrit autoCrit(mCritSect);
		if (mFP != NULL)
			return false;

		WCHAR wPath[4096];
		wPath[0] = 0;
		::GetTempPathW(4096, wPath);

		WCHAR wFilePath[4096];
		wFilePath[0] = 0;
		GetTempFileNameW(wPath, L"bftmp", 0, wFilePath);

		mFilePath = UTF8Encode(wFilePath);
		mFP = _wfopen(wFilePath, L"w+D");
		return mFP != NULL;
	}

	String GetContents()
	{
		AutoCrit autoCrit(mCritSect);

		if (mFP != NULL)
		{
			fseek(mFP, 0, SEEK_END);
			int size = (int)ftell(mFP);
			fseek(mFP, 0, SEEK_SET);

			char* str = new char[size];
			int readSize = (int)fread(str, 1, size, mFP);
			mContents.Append(str, readSize);
			delete [] str;
			fclose(mFP);
			mFP = NULL;

			::DeleteFileW(UTF8Decode(mFilePath).c_str());
		}
		return mContents;
	}
};

static BfTempFile gTempFile;

static void AddStdErrCrashInfo()
{
	String tempContents = gTempFile.GetContents();
	if (!tempContents.IsEmpty())
		BfpSystem_AddCrashInfo(tempContents.c_str());
}

#endif

///

BfIRCodeGen::BfIRCodeGen()
{	
	mStream = NULL;
	mBfIRBuilder = NULL;
	mLLVMTargetMachine = NULL;

	mNopInlineAsm = NULL;
	mObjectCheckAsm = NULL;
	mOverflowCheckAsm = NULL;
	mHasDebugLoc = false;
	mAttrSet = NULL;
	mIRBuilder = NULL;
	mDIBuilder = NULL;
	mDICompileUnit = NULL;
	mActiveFunction = NULL;
	mActiveFunctionType = NULL;

	mLLVMContext = new llvm::LLVMContext();
	mLLVMModule = NULL;
	mIsCodeView = false;
	mHadDLLExport = false;
	mConstValIdx = 0;
	mCmdCount = 0;
	mCurLine = -1;

#ifdef BF_PLATFORM_WINDOWS
	if (::GetStdHandle(STD_ERROR_HANDLE) == 0)
	{
		if (gTempFile.Create())
		{
			_dup2(fileno(gTempFile.mFP), 2);
			BfpSystem_AddCrashInfoFunc(AddStdErrCrashInfo);
		}
	}
#endif
}

BfIRCodeGen::~BfIRCodeGen()
{
	mDebugLoc = llvm::DebugLoc();
	mSavedDebugLocs.Clear();
	
	for (auto typeEx : mIRTypeExs)
		delete typeEx;

	delete mStream;
	delete mIRBuilder;
	delete mDIBuilder;
	delete mLLVMTargetMachine;
	delete mLLVMModule;
	delete mLLVMContext;
}

void BfIRCodeGen::FatalError(const StringImpl &err)
{
	String failStr = "Fatal Error in Module: ";
	failStr += mModuleName;
	failStr += "\n";
	if (mLLVMModule != NULL)
	{
		if (mActiveFunction != NULL)
		{
			failStr += "Function: ";
			failStr += mActiveFunction->getName().str();
			failStr += "\n";
		}

		auto loc = mIRBuilder->getCurrentDebugLocation();
		auto dbgLoc = loc.getAsMDNode();
		if (dbgLoc != NULL)
		{
			std::string str;
			llvm::raw_string_ostream os(str);
			dbgLoc->print(os);
			failStr += "DbgLoc: ";
			failStr += str;
			failStr += "\n";

			llvm::MDNode* scope = loc.getScope();
			if (scope != NULL)
			{
				std::string str;
				llvm::raw_string_ostream os(str);
				scope->print(os);
				failStr += "Scope: ";
				failStr += str;
				failStr += "\n";
			}
		}
	}

	failStr += err;
	BF_FATAL(failStr);
}

void BfIRCodeGen::Fail(const StringImpl& error)
{
	if (mFailed)
		return;

	if (mHasDebugLoc)
	{
		auto dbgLoc = mIRBuilder->getCurrentDebugLocation();
		if (dbgLoc)
		{
			llvm::DIFile* file = NULL;
			if (llvm::DIScope* scope = llvm::dyn_cast<llvm::DIScope>(dbgLoc.getScope()))
			{
				BfIRCodeGenBase::Fail(StrFormat("%s at line %d:%d in %s/%s", error.c_str(), dbgLoc.getLine(), dbgLoc.getCol(), scope->getDirectory().data(), scope->getFilename().data()));
				return;
			}
		}
	}

	BfIRCodeGenBase::Fail(error);
}

void BfIRCodeGen::PrintModule()
{
	Beefy::debug_ostream os;
	mLLVMModule->print(os, NULL, false, true);
	os << "\n";
	os.flush();
}

void BfIRCodeGen::PrintFunction()
{
	Beefy::debug_ostream os;
	mActiveFunction->print(os);
	os << "\n";
	os.flush();
}

void pte(BfIRTypeEx* typeEx, int indent)
{
	Beefy::debug_ostream os;	
	typeEx->mLLVMType->print(os);
	os << "\n";
	os.flush();

	for (int i = 0; i < typeEx->mMembers.mSize; i++)
	{
		for (int i = 0; i < indent; i++)
			os << " ";
		os << i << ". ";
		os.flush();
		pte(typeEx->mMembers[i], indent + 1);
	}
}

void pte(BfIRTypeEx* typeEx)
{
	if (typeEx == NULL)
		return;
	pte(typeEx, 0);
}

void pve(const BfIRTypedValue& typedValue)
{	
	Beefy::debug_ostream os;	
	os << "Value: ";
	typedValue.mValue->print(os);
	os << "\nType: ";
	os.flush();
	pte(typedValue.mTypeEx);
}

void pirb(llvm::IRBuilder<>* irBuilder)
{
	Beefy::debug_ostream os;
	os << "Debug loc: ";
	auto debugLoc = irBuilder->getCurrentDebugLocation();
	if (debugLoc.get() == NULL)
		os << "NULL";
	else
		debugLoc->print(os);	
	os << "\n";
	os.flush();
}

void BfIRCodeGen::FixValues(llvm::StructType* structType, llvm::SmallVector<llvm::Value*, 8>& values)
{
	if (values.size() >= structType->getNumElements())
		return;

	int readIdx = (int)values.size() - 1;
	values.resize(structType->getNumElements());
	for (int i = (int)values.size() - 1; i >= 0; i--)
	{
		if (values[readIdx]->getType() == structType->getElementType(i))
		{
			values[i] = values[readIdx];
			readIdx--;
		}
		else if (structType->getElementType(i)->isArrayTy())
		{
			values[i] = llvm::ConstantAggregateZero::get(structType->getElementType(i));
		}
		else
		{
			BF_FATAL("Malformed structure values");
		}
	}
}

void BfIRCodeGen::FixIndexer(llvm::Value*& val)
{
	if ((int)val->getType()->getScalarSizeInBits() > mPtrSize * 8)
		val = mIRBuilder->CreateIntCast(val, llvm::Type::getInt32Ty(*mLLVMContext), false);
}

BfTypeCode BfIRCodeGen::GetTypeCode(llvm::Type* type, bool isSigned)
{
	if (type->isIntegerTy())
	{
		switch (type->getIntegerBitWidth())
		{
		case 1:
			return BfTypeCode_Boolean;
		case 8:
			return isSigned ? BfTypeCode_Int8 : BfTypeCode_UInt8;
		case 16:
			return isSigned ? BfTypeCode_Int16 : BfTypeCode_UInt16;
		case 32:
			return isSigned ? BfTypeCode_Int32 : BfTypeCode_UInt32;
		case 64:
			return isSigned ? BfTypeCode_Int64 : BfTypeCode_UInt64;
		}
	}

	if (type->isFloatingPointTy())
		return BfTypeCode_Float;
	if (type->isDoubleTy())
		return BfTypeCode_Double;

	return BfTypeCode_None;
}

llvm::Type* BfIRCodeGen::GetLLVMType(BfTypeCode typeCode, bool& isSigned)
{
	if ((typeCode == BfTypeCode_IntPtr) || (typeCode == BfTypeCode_UIntPtr))
	{
		/*isSigned = typeCode == BfTypeCode_IntPtr;
		if (mModule->mSystem->mPtrSize == 4)
			return llvm::Type::getInt32Ty(*mLLVMContext);
		else
			return llvm::Type::getInt64Ty(*mLLVMContext);*/
		BF_FATAL("Unsupported");
	}

	isSigned = false;
	switch (typeCode)
	{
	case BfTypeCode_None:
		return llvm::Type::getVoidTy(*mLLVMContext);
	case BfTypeCode_NullPtr:
		return llvm::PointerType::get(*mLLVMContext, 0);
	case BfTypeCode_Boolean:
		return llvm::Type::getInt1Ty(*mLLVMContext);
	case BfTypeCode_Int8:
		isSigned = true;
		return llvm::Type::getInt8Ty(*mLLVMContext);
	case BfTypeCode_UInt8:
	case BfTypeCode_Char8:
		return llvm::Type::getInt8Ty(*mLLVMContext);
	case BfTypeCode_Int16:
		isSigned = true;
		return llvm::Type::getInt16Ty(*mLLVMContext);
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		return llvm::Type::getInt16Ty(*mLLVMContext);
	case BfTypeCode_Int24:
		isSigned = true;
		return llvm::Type::getIntNTy(*mLLVMContext, 24);
	case BfTypeCode_UInt24:
		return llvm::Type::getIntNTy(*mLLVMContext, 24);
	case BfTypeCode_Int32:
		isSigned = true;
		return llvm::Type::getInt32Ty(*mLLVMContext);
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		return llvm::Type::getInt32Ty(*mLLVMContext);
	case BfTypeCode_Int40:
		isSigned = true;
		return llvm::Type::getIntNTy(*mLLVMContext, 40);
	case BfTypeCode_UInt40:
		return llvm::Type::getIntNTy(*mLLVMContext, 40);
	case BfTypeCode_Int48:
		isSigned = true;
		return llvm::Type::getIntNTy(*mLLVMContext, 48);
	case BfTypeCode_UInt48:
		return llvm::Type::getIntNTy(*mLLVMContext, 48);
	case BfTypeCode_Int56:
		isSigned = true;
		return llvm::Type::getIntNTy(*mLLVMContext, 56);
	case BfTypeCode_UInt56:
		return llvm::Type::getIntNTy(*mLLVMContext, 56);
	case BfTypeCode_Int64:
		isSigned = true;
		return llvm::Type::getInt64Ty(*mLLVMContext);
	case BfTypeCode_UInt64:
		return llvm::Type::getInt64Ty(*mLLVMContext);
	case BfTypeCode_Int128:
		isSigned = true;
		return llvm::Type::getInt128Ty(*mLLVMContext);
	case BfTypeCode_UInt128:
		return llvm::Type::getInt128Ty(*mLLVMContext);
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
	case BfTypeCode_Float:
		return llvm::Type::getFloatTy(*mLLVMContext);
	case BfTypeCode_Double:
		return llvm::Type::getDoubleTy(*mLLVMContext);
	case BfTypeCode_Float2:
		return llvm::FixedVectorType::get(llvm::Type::getFloatTy(*mLLVMContext), 2);
	case BfTypeCode_FloatX2:
		return llvm::ArrayType::get(llvm::Type::getFloatTy(*mLLVMContext), 2);
	case BfTypeCode_FloatX3:
		return llvm::ArrayType::get(llvm::Type::getFloatTy(*mLLVMContext), 3);
	case BfTypeCode_FloatX4:
		return llvm::ArrayType::get(llvm::Type::getFloatTy(*mLLVMContext), 4);
	case BfTypeCode_DoubleX2:
		return llvm::ArrayType::get(llvm::Type::getDoubleTy(*mLLVMContext), 2);
	case BfTypeCode_DoubleX3:
		return llvm::ArrayType::get(llvm::Type::getDoubleTy(*mLLVMContext), 3);
	case BfTypeCode_DoubleX4:
		return llvm::ArrayType::get(llvm::Type::getDoubleTy(*mLLVMContext), 4);
	case BfTypeCode_Int64X2:
		return llvm::ArrayType::get(llvm::Type::getInt64Ty(*mLLVMContext), 2);
	case BfTypeCode_Int64X3:
		return llvm::ArrayType::get(llvm::Type::getInt64Ty(*mLLVMContext), 3);
	case BfTypeCode_Int64X4:
		return llvm::ArrayType::get(llvm::Type::getInt64Ty(*mLLVMContext), 4);
	default: break;
	}
	return NULL;
}

BfIRTypeEx* BfIRCodeGen::GetTypeEx(BfTypeCode typeCode, bool& isSigned)
{
	BfIRTypeEx** valuePtr = NULL;
	if (mTypeCodeTypeExMap.TryAdd(typeCode, NULL, &valuePtr))
	{
		BfIRTypeEx* typeEx = new BfIRTypeEx();
		typeEx->mLLVMType = GetLLVMType(typeCode, isSigned);

		if (typeEx->mLLVMType->isPointerTy())
		{
			// Make void* actually be an i8*
			typeEx->mMembers.Add(GetTypeEx(llvm::Type::getInt8Ty(*mLLVMContext)));
		}

		if (auto arrType = llvm::dyn_cast<llvm::ArrayType>(typeEx->mLLVMType))
		{
			typeEx->mMembers.Add(GetTypeEx(arrType->getElementType()));
		}

		if (auto vectorType = llvm::dyn_cast<llvm::VectorType>(typeEx->mLLVMType))
		{
			typeEx->mMembers.Add(GetTypeEx(vectorType->getElementType()));
		}

		*valuePtr = typeEx;
	}
	else
	{
		isSigned = false;
		switch (typeCode)
		{		
		case BfTypeCode_Int8:			
		case BfTypeCode_Int16:			
		case BfTypeCode_Int24:						
		case BfTypeCode_Int32:			
		case BfTypeCode_Int40:			
		case BfTypeCode_Int48:			
		case BfTypeCode_Int56:			
		case BfTypeCode_Int64:			
		case BfTypeCode_Int128:
			isSigned = true;		
		}		
	}
	return *valuePtr;
}

BfIRTypeEx* BfIRCodeGen::GetTypeEx(llvm::Type* llvmType)
{	
	BfIRTypeEx** valuePtr = NULL;
	if (mLLVMTypeExMap.TryAdd(llvmType, NULL, &valuePtr))
	{
		BfIRTypeEx* typeEx = new BfIRTypeEx();
		mIRTypeExs.Add(typeEx);
		typeEx->mLLVMType = llvmType;		
		*valuePtr = typeEx;
	}	
	return *valuePtr;
}

BfIRTypeEx* BfIRCodeGen::CreateTypeEx(llvm::Type* llvmType)
{
	BfIRTypeEx* typeEx = new BfIRTypeEx();
	mIRTypeExs.Add(typeEx);
	typeEx->mLLVMType = llvmType;
	return typeEx;
}

BfIRTypeEx* BfIRCodeGen::GetPointerTypeEx(BfIRTypeEx* elementType)
{	
	BF_ASSERT(elementType != NULL);
	BfIRTypeEx** valuePtr = NULL;
	if (mPointerTypeExMap.TryAdd(elementType, NULL, &valuePtr))
	{
		BfIRTypeEx* typeEx = new BfIRTypeEx();
		mIRTypeExs.Add(typeEx);
		typeEx->mLLVMType = llvm::PointerType::get(*mLLVMContext, 0);		
		typeEx->mMembers.Add(elementType);
		*valuePtr = typeEx;
	}
	return *valuePtr;
}

BfIRTypeEx* BfIRCodeGen::GetTypeMember(BfIRTypeEx* typeEx, int idx)
{
	if ((idx < 0) || (idx >= typeEx->mMembers.mSize))
	{
		Fail("BfIRTypeEx GetTypeMember OOB");

		bool isSigned;
		return GetTypeEx(BfTypeCode_Int8, isSigned);
	}
	return typeEx->mMembers[idx];
}

BfIRTypeEntry& BfIRCodeGen::GetTypeEntry(int typeId)
{
	BfIRTypeEntry& typeEntry = mTypes[typeId];
	if (typeEntry.mTypeId == -1)
		typeEntry.mTypeId = typeId;
	return typeEntry;
}

BfIRTypeEntry* BfIRCodeGen::GetTypeEntry(BfIRTypeEx* type)
{
	int typeId = 0;
	if (!mTypeToTypeIdMap.TryGetValue(type, &typeId))
		return NULL;
	return &GetTypeEntry(typeId);
}

void BfIRCodeGen::SetResult(int id, llvm::Value* value)
{	
	BF_ASSERT(!value->getType()->isAggregateType());
	BF_ASSERT(!value->getType()->isPointerTy());

	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_LLVMValue;
	entry.mLLVMValue = value;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResult(int id, const BfIRTypedValue& value)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_TypedValue;
	entry.mTypedValue = value;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResultAligned(int id, llvm::Value* value)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_LLVMValue_Aligned;
	entry.mLLVMValue = value;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResultAligned(int id, const BfIRTypedValue& value)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_TypedValue_Aligned;
	entry.mTypedValue = value;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResult(int id, llvm::Type* type)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_LLVMType;
	entry.mLLVMType = type;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResult(int id, BfIRTypeEx* typeEx)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_TypeEx;
	entry.mTypeEx = typeEx;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResult(int id, llvm::BasicBlock* value)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_LLVMBasicBlock;
	entry.mLLVMBlock = value;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::SetResult(int id, llvm::MDNode* md)
{
	BfIRCodeGenEntry entry;
	entry.mKind = BfIRCodeGenEntryKind_LLVMMetadata;
	entry.mLLVMMetadata = md;
	mResults.TryAdd(id, entry);
}

void BfIRCodeGen::ProcessBfIRData(const BfSizedArray<uint8>& buffer)
{
    // Diagnostic handlers were unified in LLVM change 5de2d189e6ad, so starting
    // with LLVM 13 this function is gone.
	/*struct InlineAsmErrorHook
	{
		static void StaticHandler(const llvm::SMDiagnostic& diag, void *context, unsigned locCookie)
		{
			if (diag.getKind() == llvm::SourceMgr::DK_Error)
			{
				BfIRCodeGen* irCodeGen = (BfIRCodeGen*)context;

				if (!irCodeGen->mErrorMsg.empty())
					irCodeGen->mErrorMsg += "\n";
				irCodeGen->mErrorMsg += StrFormat("Inline assembly error: \"%s\" : %s", diag.getMessage().data(), diag.getLineContents().data());
			}
		}
	};
	mLLVMContext->setInlineAsmDiagnosticHandler(InlineAsmErrorHook::StaticHandler, this);*/

	BF_ASSERT(mStream == NULL);
	mStream = new ChunkedDataBuffer();
	mStream->InitFlatRef(buffer.mVals, buffer.mSize);

	while (mStream->GetReadPos() < buffer.mSize)
	{
		if (mFailed)
			break;
		HandleNextCmd();
	}

	BF_ASSERT((mFailed) || (mStream->GetReadPos() == buffer.mSize));
}

int64 BfIRCodeGen::ReadSLEB128()
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

void BfIRCodeGen::Read(StringImpl& str)
{
	int len = (int)ReadSLEB128();
	str.Append('?', len);
	mStream->Read((void*)str.c_str(), len);
}

void BfIRCodeGen::Read(int& i)
{
	i = (int)ReadSLEB128();
}

void BfIRCodeGen::Read(int64& i)
{
	i = ReadSLEB128();
}

void BfIRCodeGen::Read(Val128& i)
{
	i.mLow = (uint64)ReadSLEB128();
	i.mHigh = (uint64)ReadSLEB128();
}

void BfIRCodeGen::Read(bool& val)
{
	val = mStream->Read() != 0;
}

void BfIRCodeGen::Read(int8& val)
{
	val = mStream->Read();
}

void BfIRCodeGen::Read(BfIRTypeEntry*& type)
{
	int typeId = (int)ReadSLEB128();
	type = &GetTypeEntry(typeId);
}

void BfIRCodeGen::Read(BfIRTypeEx*& typeEx, BfIRTypeEntry** outTypeEntry)
{
	typeEx = NULL;

	BfIRType::TypeKind typeKind = (BfIRType::TypeKind)mStream->Read();
	if (typeKind == BfIRType::TypeKind::TypeKind_None)
		return;	

	if (typeKind == BfIRType::TypeKind::TypeKind_Stream)
	{
		int streamId = (int)ReadSLEB128();
		if (streamId == -1)
		{	
			typeEx = NULL;
			return;
		}
		auto& result = mResults[streamId];		
		BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_TypeEx);
		typeEx = result.mTypeEx;
		return;
	}

	if (typeKind == BfIRType::TypeKind::TypeKind_SizedArray)
	{
		CMD_PARAM(BfIRTypeEx*, elementType);
		CMD_PARAM(int, length);
		typeEx = new BfIRTypeEx();
		typeEx->mLLVMType = llvm::ArrayType::get(elementType->mLLVMType, length);
		BF_ASSERT(elementType != NULL);
		typeEx->mMembers.Add(elementType);
		mIRTypeExs.Add(typeEx);
		return;
	}

	int typeId = (int)ReadSLEB128();

	if (typeKind == BfIRType::TypeKind::TypeKind_TypeCode)
	{		
		bool isSigned = false;
		typeEx = GetTypeEx((BfTypeCode)typeId, isSigned);
		return;
	}

	auto& typeEntry = GetTypeEntry(typeId);
	if (typeKind == BfIRType::TypeKind::TypeKind_TypeId)
		typeEx = typeEntry.mType;
	else if (typeKind == BfIRType::TypeKind::TypeKind_TypeInstId)
		typeEx = typeEntry.mInstType;
	else if (typeKind == BfIRType::TypeKind::TypeKind_TypeInstPtrId)
		typeEx = GetPointerTypeEx(typeEntry.mInstType);
	if (outTypeEntry != NULL)
		*outTypeEntry = &typeEntry;
}

void BfIRCodeGen::Read(llvm::Type*& llvmType, BfIRTypeEntry** outTypeEntry)
{
	BfIRTypeEx* typeEx = NULL;
	Read(typeEx, outTypeEntry);
	if (typeEx != NULL)
	{
		llvmType = typeEx->mLLVMType;
	}
	else
		llvmType = NULL;
}

void BfIRCodeGen::Read(llvm::FunctionType*& llvmType)
{
	int streamId = (int)ReadSLEB128();
	auto& result = mResults[streamId];

	if (result.mKind == BfIRCodeGenEntryKind_TypeEx)
	{		
		llvmType = (llvm::FunctionType*)result.mTypeEx->mLLVMType;
		return;
	}

	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMType);
	llvmType = (llvm::FunctionType*)result.mLLVMType;
}

void BfIRCodeGen::ReadFunctionType(BfIRTypeEx*& typeEx)
{
	int streamId = (int)ReadSLEB128();
	auto& result = mResults[streamId];

	if (result.mKind == BfIRCodeGenEntryKind_TypeEx)
	{
		typeEx = result.mTypeEx;
		return;
	}

	BF_FATAL("Invalid path in ReadFunctionType");

	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMType);
	typeEx = GetTypeEx(result.mLLVMType);
}

void BfIRCodeGen::FixTypedValue(BfIRTypedValue& typedValue)
{
	if ((typedValue.mValue != NULL) && (typedValue.mTypeEx == NULL))
	{
		typedValue.mTypeEx = GetTypeEx(typedValue.mValue->getType());
		BF_ASSERT(!typedValue.mValue->getType()->isStructTy());
		BF_ASSERT(!typedValue.mValue->getType()->isFunctionTy());
	}
}

void BfIRCodeGen::Read(BfIRTypedValue& typedValue, BfIRCodeGenEntry** codeGenEntry, BfIRSizeAlignKind sizeAlignKind)
{
	typedValue.mValue = NULL;
	typedValue.mTypeEx = NULL;

	BfIRParamType paramType = (BfIRParamType)mStream->Read();
	if (paramType == BfIRParamType_None)
	{
		//
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

				CMD_PARAM(BfIRTypeEx*, varType);
				CMD_PARAM(bool, isConstant);
				BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
				CMD_PARAM(llvm::Constant*, initializer);
				CMD_PARAM(String, name);
				CMD_PARAM(bool, isTLS);

				llvm::GlobalVariable* globalVariable = mLLVMModule->getGlobalVariable(name.c_str(), true);
				if (globalVariable == NULL)
				{
					globalVariable = mLLVMModule->getGlobalVariable(name.c_str());
					if (globalVariable == NULL)
					{
						globalVariable = new llvm::GlobalVariable(
							*mLLVMModule,
							varType->mLLVMType,
							isConstant,
							LLVMMapLinkageType(linkageType),
							initializer,
							name.c_str(), NULL, isTLS ? llvm::GlobalValue::GeneralDynamicTLSModel : llvm::GlobalValue::NotThreadLocal);
					}
				}
				typedValue.mTypeEx = GetPointerTypeEx(varType);
				typedValue.mValue = globalVariable;
				SetResult(streamId, typedValue);
			}
			else
				typedValue = GetTypedValue(streamId);
			FixTypedValue(typedValue);
			return;
		}
		/*else if (constType == BfConstType_GlobalVar_TypeInst)
		{
			CMD_PARAM(int, streamId);
			if (streamId == -1)
			{
				int streamId = mStream->GetReadPos();

				CMD_PARAM(int, varTypeId);
				auto& typeEntry = GetTypeEntry(varTypeId);
				auto varType = typeEntry.mInstLLVMType;

				CMD_PARAM(bool, isConstant);
				BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
				CMD_PARAM(llvm::Constant*, initializer);
				CMD_PARAM(String, name);
				CMD_PARAM(bool, isTLS);

				auto globalVariable = new llvm::GlobalVariable(
					*mLLVMModule,
					varType,
					isConstant,
					LLVMMapLinkageType(linkageType),
					initializer,
					name, NULL, isTLS ? llvm::GlobalValue::GeneralDynamicTLSModel : llvm::GlobalValue::NotThreadLocal);
				llvmValue = globalVariable;

				SetResult(streamId, globalVariable);
			}
			else
				llvmValue = GetLLVMValue(streamId);
			return;
		}*/
		else if ((constType == BfConstType_BitCast) || (constType == BfConstType_BitCastNull))
		{
			CMD_PARAM(llvm::Constant*, target);
			CMD_PARAM(BfIRTypeEx*, toType);

			typedValue.mTypeEx = toType;
			if ((constType == BfConstType_BitCastNull) && (toType->mLLVMType->isIntegerTy()))
			{
				typedValue.mValue = llvm::ConstantInt::getNullValue(toType->mLLVMType);
			}
			else if (target->getType()->isIntegerTy())
				typedValue.mValue = llvm::ConstantExpr::getIntToPtr(target, toType->mLLVMType);
			else
				typedValue.mValue = llvm::ConstantExpr::getBitCast(target, toType->mLLVMType);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_GEP32_1)
		{
			CMD_PARAM(BfIRTypedValue, target);
			CMD_PARAM(int, idx0);
			llvm::Value* gepArgs[] = {
				llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), idx0)};
			
			auto compositeType = GetTypeMember(target.mTypeEx, 0);
			auto constant = llvm::dyn_cast<llvm::Constant>(target.mValue);

			typedValue.mTypeEx = target.mTypeEx;
			typedValue.mValue = llvm::ConstantExpr::getInBoundsGetElementPtr(compositeType->mLLVMType, constant, gepArgs);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_GEP32_2)
		{
			CMD_PARAM(BfIRTypedValue, target);
			CMD_PARAM(int, idx0);
			CMD_PARAM(int, idx1);
			llvm::Value* gepArgs[] = {
				llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), idx0),
				llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), idx1)};

			auto compositeType = GetTypeMember(target.mTypeEx, 0);
			int elemIdx = BF_MAX(BF_MIN(idx1, (int)compositeType->mMembers.mSize - 1), 0);
			auto elemType = GetTypeMember(compositeType, elemIdx);

			auto constant = llvm::dyn_cast<llvm::Constant>(target.mValue);
			typedValue.mValue = llvm::ConstantExpr::getInBoundsGetElementPtr(compositeType->mLLVMType, constant, gepArgs);
			typedValue.mTypeEx = GetPointerTypeEx(elemType);						
			return;
		}
		else if (constType == BfConstType_ExtractValue)
		{
			CMD_PARAM(BfIRTypedValue, target);
			CMD_PARAM(int, idx0);			
			
			auto compositeType = target.mTypeEx;
			int elemIdx = BF_MIN(idx0, (int)compositeType->mMembers.mSize - 1);
			auto elemType = GetTypeMember(compositeType, elemIdx);

			typedValue.mTypeEx = elemType;
			if (auto constant = llvm::dyn_cast<llvm::Constant>(target.mValue))			
				typedValue.mValue = constant->getAggregateElement(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), idx0));							
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_PtrToInt)
		{
			CMD_PARAM(llvm::Constant*, target);
			BfTypeCode toTypeCode = (BfTypeCode)mStream->Read();

			bool isSigned;
			BfIRTypeEx* toType = GetTypeEx(toTypeCode, isSigned);
			typedValue.mTypeEx = toType;
			typedValue.mValue = llvm::ConstantExpr::getPtrToInt(target, toType->mLLVMType);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_IntToPtr)
		{
			CMD_PARAM(llvm::Constant*, target);
			CMD_PARAM(BfIRTypeEx*, toType);
			typedValue.mTypeEx = toType;
			typedValue.mValue = llvm::ConstantExpr::getIntToPtr(target, toType->mLLVMType);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_AggZero)
		{
			BfIRTypeEntry* typeEntry = NULL;
			BfIRTypeEx* type = NULL;
			Read(type, &typeEntry);
			typedValue.mTypeEx = type;
			if ((sizeAlignKind == BfIRSizeAlignKind_Aligned) && (typeEntry != NULL))
				typedValue.mValue = llvm::ConstantAggregateZero::get(GetSizeAlignedType(typeEntry)->mLLVMType);
			else
				typedValue.mValue = llvm::ConstantAggregateZero::get(type->mLLVMType);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_ArrayZero8)
		{
			CMD_PARAM(int, count);
			auto arrType = llvm::ArrayType::get(llvm::Type::getInt8Ty(*mLLVMContext), count);
			typedValue.mValue = llvm::ConstantAggregateZero::get(arrType);
			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_Agg)
		{
			BfIRTypeEntry* typeEntry = NULL;
			BfIRTypeEx* type = NULL;
			Read(type, &typeEntry);
			CmdParamVec<llvm::Constant*> values;
			Read(values, type->mLLVMType->isArrayTy() ? BfIRSizeAlignKind_Aligned : BfIRSizeAlignKind_Original);

			typedValue.mTypeEx = type;

			if (auto arrayType = llvm::dyn_cast<llvm::ArrayType>(type->mLLVMType))
			{
				int fillCount = (int)(arrayType->getNumElements() - values.size());
				if (fillCount > 0)
				{
					auto lastValue = values.back();
					for (int i = 0; i < fillCount; i++)
						values.push_back(lastValue);
				}
				typedValue.mValue = llvm::ConstantArray::get(arrayType, values);
			}
			else if (auto structType = llvm::dyn_cast<llvm::StructType>(type->mLLVMType))
			{
				for (int i = 0; i < (int)values.size(); i++)
				{
					if (values[i]->getType() != structType->getElementType(i))
					{
						auto valArrayType = llvm::dyn_cast<llvm::ArrayType>(values[i]->getType());
						if (valArrayType != NULL)
						{
							if (valArrayType->getNumElements() == 0)
							{
								values[i] = llvm::ConstantAggregateZero::get(structType->getElementType(i));
							}
						}
					}
				}

				if ((sizeAlignKind == BfIRSizeAlignKind_Aligned) && (typeEntry != NULL))
				{
					auto alignedTypeEx = GetSizeAlignedType(typeEntry);
					auto alignedType = llvm::dyn_cast<llvm::StructType>(alignedTypeEx->mLLVMType);
					if (type != alignedTypeEx)
						values.push_back(llvm::ConstantAggregateZero::get(alignedType->getElementType(alignedType->getNumElements() - 1)));
					typedValue.mTypeEx = alignedTypeEx;
					typedValue.mValue = llvm::ConstantStruct::get(alignedType, values);
				}
				else
				{					
					typedValue.mValue = llvm::ConstantStruct::get(structType, values);
				}
			}
			else if (auto vecType = llvm::dyn_cast<llvm::VectorType>(type->mLLVMType))
			{
				typedValue.mValue = llvm::ConstantVector::get(values);
			}
			else
			{
				typedValue.mValue = NULL;
				Fail("Bad type");
			}

			FixTypedValue(typedValue);
			return;
		}
		else if (constType == BfConstType_Undef)
		{
			CMD_PARAM(BfIRTypeEx*, type);
			typedValue.mTypeEx = type;
			typedValue.mValue = llvm::UndefValue::get(type->mLLVMType);			
			return;
		}
		else if (constType == BfConstType_TypeOf)
		{
			CMD_PARAM(BfIRTypeEx*, type);
			typedValue = mReflectDataMap[type];
			BF_ASSERT(typedValue.mValue != NULL);			
			return;
		}
		else if (constType == BfConstType_TypeOf_WithData)
		{
			CMD_PARAM(BfIRTypeEx*, type);
			CMD_PARAM(BfIRTypedValue, value);
			mReflectDataMap[type] = value;
			typedValue = value;			
			return;
		}

		bool isSigned;
		llvm::Type* llvmConstType = GetLLVMType(typeCode, isSigned);

		if (typeCode == BfTypeCode_Float)
		{
			float f;
			mStream->Read(&f, sizeof(float));
			typedValue.mValue = llvm::ConstantFP::get(llvmConstType, f);
		}
		else if (typeCode == BfTypeCode_Double)
		{
			double d;
			mStream->Read(&d, sizeof(double));
			typedValue.mValue = llvm::ConstantFP::get(llvmConstType, d);
		}
		else if (typeCode == BfTypeCode_Boolean)
		{
			CMD_PARAM(bool, boolVal);
			typedValue.mValue = llvm::ConstantInt::get(llvmConstType, boolVal ? 1 : 0);
		}
		else if (typeCode == BfTypeCode_None)
		{
			typedValue.mValue = NULL;
		}
		else if (typeCode == BfTypeCode_NullPtr)
		{
			CMD_PARAM(llvm::Type*, nullType);
			if (nullType != NULL)
				typedValue.mValue = llvm::ConstantPointerNull::get((llvm::PointerType*)nullType);
			else
				typedValue.mValue = llvm::ConstantPointerNull::get((llvm::PointerType*)llvmConstType);
		}
		else if (BfIRBuilder::IsInt(typeCode))
		{
			int64 intVal = ReadSLEB128();
			auto constVal = llvm::ConstantInt::get(llvmConstType, intVal);
			auto constInt = (llvm::ConstantInt*)constVal;
			typedValue.mValue = constInt;
		}
		else
		{
			BF_FATAL("Unhandled");
		}
	}
	else if (paramType == BfIRParamType_Arg)
	{
		int argIdx = mStream->Read();
		if (argIdx >= mActiveFunction->arg_size())
		{
			FatalError(StrFormat("ARG out of bounds %d", argIdx));
		}

		auto typeEx = mActiveFunctionType;

		BF_ASSERT(argIdx < mActiveFunction->arg_size());
		auto argItr = mActiveFunction->arg_begin();
		for (int i = 0; i < argIdx; i++)
			argItr++;
		typedValue.mValue = &(*argItr);
		typedValue.mTypeEx = GetTypeMember(typeEx, argIdx + 1);
	}
	else
	{
		int cmdId = -1;
		if (paramType == BfIRParamType_StreamId_Abs8)
		{
			cmdId = mStream->Read();
		}
		else if (paramType == BfIRParamType_StreamId_Rel)
		{
			cmdId = mCmdCount - (int)ReadSLEB128();
		}
		else
		{
			cmdId = mCmdCount - (paramType - BfIRParamType_StreamId_Back1) - 1;
		}		

		auto& result = mResults[cmdId];

		if ((codeGenEntry != NULL) && (result.mKind != BfIRCodeGenEntryKind_None))		
			*codeGenEntry = &result;					

		if (result.mKind == BfIRCodeGenEntryKind_TypedValue_Aligned)
		{
			typedValue = result.mTypedValue;

			BfIRTypeEx* normalType = NULL;
			if (mAlignedTypeToNormalType.TryGetValue(typedValue.mTypeEx, &normalType)) 			
				typedValue.mTypeEx = normalType; 			

			return;
		}

		if (result.mKind == BfIRCodeGenEntryKind_TypedValue)
		{			
			typedValue = result.mTypedValue;
			return;
		}

		if (result.mKind != BfIRCodeGenEntryKind_LLVMValue)
		{
			if ((codeGenEntry != NULL) && (result.mKind != BfIRCodeGenEntryKind_None))
			{
				*codeGenEntry = &result;
				return;
			}
		}

		if (result.mKind == BfIRCodeGenEntryKind_LLVMValue_Aligned)
		{
			typedValue.mValue = result.mLLVMValue;
			if (sizeAlignKind != BfIRSizeAlignKind_Original)
				return;

			llvm::Type* normalType = NULL;
			//TODO: if (auto ptrType = llvm::dyn_cast<llvm::PointerType>(llvmValue->getType()))
			{
				
// 				if (mAlignedTypeToNormalType.TryGetValue(ptrType->getElementType(), &normalType))
// 				{
// 					llvmValue = mIRBuilder->CreateBitCast(llvmValue, normalType->getPointerTo());
// 					return;
// 				}
			}
		}

		BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMValue);
		typedValue.mValue = result.mLLVMValue;		
	}
	FixTypedValue(typedValue);
}

void BfIRCodeGen::Read(llvm::Value*& llvmValue, BfIRCodeGenEntry** codeGenEntry, BfIRSizeAlignKind sizeAlignKind)
{
	BfIRTypedValue typedValue;
	Read(typedValue, codeGenEntry, sizeAlignKind);
	llvmValue = typedValue.mValue;
}

void BfIRCodeGen::Read(llvm::Constant*& llvmConstant, BfIRSizeAlignKind sizeAlignKind)
{
	llvm::Value* value;
	Read(value, NULL, sizeAlignKind);
	if (value == NULL)
	{
		llvmConstant = NULL;
	}
	else
	{
		BF_ASSERT(llvm::isa<llvm::Constant>(value));
		llvmConstant = (llvm::Constant*)value;
	}
}

void BfIRCodeGen::Read(llvm::Function*& llvmFunc)
{
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		llvmFunc = NULL;
		return;
	}
	auto& result = mResults[streamId];	
	if (result.mKind == BfIRCodeGenEntryKind_TypedValue)
	{
		llvmFunc = (llvm::Function*)result.mTypedValue.mValue;
		return;
	}

	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMValue);
	BF_ASSERT(llvm::isa<llvm::Function>(result.mLLVMValue));
	llvmFunc = (llvm::Function*)result.mLLVMValue;
}

void BfIRCodeGen::ReadFunction(BfIRTypedValue& typedValue)
{
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		typedValue.mValue = NULL;
		typedValue.mTypeEx = NULL;
		return;
	}
	auto& result = mResults[streamId];
	if (result.mKind == BfIRCodeGenEntryKind_TypedValue)
	{
		typedValue = result.mTypedValue;
		return;
	}

	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMValue);
	BF_ASSERT(llvm::isa<llvm::Function>(result.mLLVMValue));
	typedValue.mValue = result.mLLVMValue;
	FixTypedValue(typedValue);	
}

void BfIRCodeGen::Read(llvm::BasicBlock*& llvmBlock)
{
	int streamId = (int)ReadSLEB128();
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMBasicBlock);
	llvmBlock = (llvm::BasicBlock*)result.mLLVMType;
}

void BfIRCodeGen::Read(llvm::MDNode*& llvmMD)
{
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		llvmMD = NULL;
		return;
	}
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMMetadata);
	llvmMD = result.mLLVMMetadata;
}

void BfIRCodeGen::Read(llvm::Metadata*& llvmMD)
{
	int streamId = (int)ReadSLEB128();
	if (streamId == -1)
	{
		llvmMD = NULL;
		return;
	}
	auto& result = mResults[streamId];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMMetadata);
	llvmMD = result.mLLVMMetadata;
}

void BfIRCodeGen::AddNop()
{
	if ((mTargetTriple.GetMachineType() != BfMachineType_x86) && (mTargetTriple.GetMachineType() != BfMachineType_x64))
		return;

	if (mNopInlineAsm == NULL)
	{
		llvm::SmallVector<llvm::Type*, 8> paramTypes;
		llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getVoidTy(*mLLVMContext), paramTypes, false);
		mNopInlineAsm = llvm::InlineAsm::get(funcType,
			"nop", "", true, false, llvm::InlineAsm::AD_ATT);
	}

	llvm::CallInst* callInst = mIRBuilder->CreateCall(mNopInlineAsm);
	
	callInst->addFnAttr(llvm::Attribute::NoUnwind);
}

llvm::Value* BfIRCodeGen::TryToVector(const BfIRTypedValue& value)
{
	auto valueType = value.mTypeEx->mLLVMType;
	if (llvm::isa<llvm::VectorType>(valueType))
		return value.mValue;
	
 	if (auto ptrType = llvm::dyn_cast<llvm::PointerType>(valueType))
 	{
 		auto ptrElemType = GetTypeMember(value.mTypeEx, 0);
 		if (auto arrType = llvm::dyn_cast<llvm::ArrayType>(ptrElemType->mLLVMType))
 		{
 			auto vecType = llvm::FixedVectorType::get(arrType->getArrayElementType(), (uint)arrType->getArrayNumElements());
 			auto vecPtrType = vecType->getPointerTo();
 			
 			auto ptrVal0 = mIRBuilder->CreateBitCast(value.mValue, vecPtrType);
 			return mIRBuilder->CreateAlignedLoad(vecType, ptrVal0, llvm::MaybeAlign(1));
 		}
 
 		if (auto vecType = llvm::dyn_cast<llvm::VectorType>(ptrElemType->mLLVMType))
 		{
 			return mIRBuilder->CreateAlignedLoad(vecType, value.mValue, llvm::MaybeAlign(1));
 		}
 	}

	return NULL;
}

bool BfIRCodeGen::TryMemCpy(const BfIRTypedValue& ptr, llvm::Value* val)
{
	auto valType = val->getType();

	auto dataLayout = llvm::DataLayout(mLLVMModule);
	int arrayBytes = (int)dataLayout.getTypeSizeInBits(valType) / 8;

	// LLVM has perf issues with large aggregates - it treats each element as a unique value,
	//  which is great for optimizing small data but is a perf killer for large data.
	if (arrayBytes < 256)
		return false;

	auto int8Ty = llvm::Type::getInt8Ty(*mLLVMContext);
	auto int32Ty = llvm::Type::getInt32Ty(*mLLVMContext);
	auto int8PtrTy = int8Ty->getPointerTo();

	if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(val))
	{
		mIRBuilder->CreateMemCpy(
			mIRBuilder->CreateBitCast(ptr.mValue, int8PtrTy),
			llvm::MaybeAlign(1),
			mIRBuilder->CreateBitCast(loadInst->getPointerOperand(), int8PtrTy),
			llvm::MaybeAlign(1),
			llvm::ConstantInt::get(int32Ty, arrayBytes));
		return true;
	}

	auto constVal = llvm::dyn_cast<llvm::Constant>(val);
	if (constVal == NULL)
		return false;

	if (llvm::isa<llvm::ConstantAggregateZero>(constVal))
	{
		mIRBuilder->CreateMemSet(
			mIRBuilder->CreateBitCast(ptr.mValue, int8PtrTy),
			llvm::ConstantInt::get(int8Ty, 0),
			llvm::ConstantInt::get(int32Ty, arrayBytes),
			llvm::MaybeAlign(1));
		return true;
	}

	auto globalVariable = new llvm::GlobalVariable(
		*mLLVMModule,
		valType,
		true,
		llvm::GlobalValue::InternalLinkage,
		constVal,
		StrFormat("__ConstVal__%d", mConstValIdx++).c_str(),
		NULL,
		llvm::GlobalValue::NotThreadLocal);

	mIRBuilder->CreateMemCpy(
		mIRBuilder->CreateBitCast(ptr.mValue, int8PtrTy),
		llvm::MaybeAlign(1),
		mIRBuilder->CreateBitCast(globalVariable, int8PtrTy),
		llvm::MaybeAlign(1),
		llvm::ConstantInt::get(int32Ty, arrayBytes));

	return true;
}

bool BfIRCodeGen::TryVectorCpy(const BfIRTypedValue& ptr, llvm::Value* val)
{	
 	if (GetTypeMember(ptr.mTypeEx, 0)->mLLVMType == val->getType())
 		return false;

	if (!llvm::isa<llvm::VectorType>(val->getType()))
	{
		return false;
	}

	auto usePtr = mIRBuilder->CreateBitCast(ptr.mValue, val->getType()->getPointerTo());
	mIRBuilder->CreateAlignedStore(val, usePtr, llvm::MaybeAlign(1));

	return true;
}

llvm::Type* BfIRCodeGen::GetLLVMPointerElementType(BfIRTypeEx* typeEx)
{
	BF_ASSERT(typeEx != NULL);
	BF_ASSERT(typeEx->mLLVMType->isPointerTy());
	return GetTypeMember(typeEx, 0)->mLLVMType;
}

BfIRTypeEx* BfIRCodeGen::GetSizeAlignedType(BfIRTypeEntry* typeEntry)
{
	if ((typeEntry->mAlignType == NULL) && ((typeEntry->mSize & (typeEntry->mAlign - 1)) != 0))
	{
		auto structType = llvm::dyn_cast<llvm::StructType>(typeEntry->mType->mLLVMType);
		if (structType != NULL)
		{
			//TODO: Fill out properly

			BF_ASSERT(structType->isPacked());

			auto alignTypeEx = new BfIRTypeEx();
			mIRTypeExs.Add(alignTypeEx);

			auto alignType = llvm::StructType::create(*mLLVMContext, (structType->getName().str() + "_ALIGNED").c_str());
			llvm::SmallVector<llvm::Type*, 8> members;
			for (int elemIdx = 0; elemIdx < (int)structType->getNumElements(); elemIdx++)
			{
				members.push_back(structType->getElementType(elemIdx));
			}
			int alignSize = BF_ALIGN(typeEntry->mSize, typeEntry->mAlign);
			int fillSize = alignSize - typeEntry->mSize;
			members.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(*mLLVMContext), fillSize));
			alignType->setBody(members, structType->isPacked());
					
			alignTypeEx->mLLVMType = alignType;
			typeEntry->mAlignType = alignTypeEx;
			mAlignedTypeToNormalType[alignTypeEx] = typeEntry->mType;
		}
	}

	if (typeEntry->mAlignType != NULL)
		return typeEntry->mAlignType;

	return typeEntry->mType;
}

BfIRTypedValue BfIRCodeGen::GetAlignedPtr(const BfIRTypedValue& val)
{	
	BfIRTypedValue result = val;

	if (auto ptrType = llvm::dyn_cast<llvm::PointerType>(val.mTypeEx->mLLVMType))
	{
		auto elemType = GetTypeMember(val.mTypeEx, 0);
		auto typeEntry = GetTypeEntry(elemType);
		if (typeEntry != NULL)
		{
			auto alignedType = GetSizeAlignedType(typeEntry);
			if (alignedType != elemType)
				result.mTypeEx = GetPointerTypeEx(alignedType);
		}
	}

	return result;
}


llvm::Value* BfIRCodeGen::DoCheckedIntrinsic(llvm::Intrinsic::ID intrin, llvm::Value* lhs, llvm::Value* rhs, bool useAsm)
{
	if ((mTargetTriple.GetMachineType() != BfMachineType_x86) && (mTargetTriple.GetMachineType() != BfMachineType_x64))
		useAsm = false;

	CmdParamVec<llvm::Type*> useParams;
	useParams.push_back(lhs->getType());
	auto func = llvm::Intrinsic::getDeclaration(mLLVMModule, intrin, useParams);

	CmdParamVec<llvm::Value*> args;
	args.push_back(lhs);
	args.push_back(rhs);
	llvm::FunctionType* funcType = func->getFunctionType();

	auto aggResult = mIRBuilder->CreateCall(funcType, func, args);
	auto valResult = mIRBuilder->CreateExtractValue(aggResult, 0);
	auto failResult = mIRBuilder->CreateExtractValue(aggResult, 1);

	if (!useAsm)
	{
		mLockedBlocks.Add(mIRBuilder->GetInsertBlock());

		auto failBB = llvm::BasicBlock::Create(*mLLVMContext, "access.fail");
		auto passBB = llvm::BasicBlock::Create(*mLLVMContext, "access.pass");

		mIRBuilder->CreateCondBr(failResult, failBB, passBB);

		mActiveFunction->insert(mActiveFunction->end(), failBB);
		mIRBuilder->SetInsertPoint(failBB);

		auto trapDecl = llvm::Intrinsic::getDeclaration(mLLVMModule, llvm::Intrinsic::trap);
		auto callInst = mIRBuilder->CreateCall(trapDecl);
		callInst->addFnAttr(llvm::Attribute::NoReturn);
		mIRBuilder->CreateBr(passBB);
		
		mActiveFunction->insert(mActiveFunction->end(), passBB);
		mIRBuilder->SetInsertPoint(passBB);
	}
	else
	{
		if (mOverflowCheckAsm == NULL)
		{
			std::vector<llvm::Type*> paramTypes;
			paramTypes.push_back(llvm::Type::getInt8Ty(*mLLVMContext));
			auto funcType = llvm::FunctionType::get(llvm::Type::getVoidTy(*mLLVMContext), paramTypes, false);

			String asmStr =
				"testb $$1, $0\n"
				"jz 1f\n"
				"int $$3\n"
				"1:";

			mOverflowCheckAsm = llvm::InlineAsm::get(funcType,
				asmStr.c_str(), "r,~{dirflag},~{fpsr},~{flags}", true,
				false, llvm::InlineAsm::AD_ATT);
		}

		llvm::SmallVector<llvm::Value*, 1> llvmArgs;
		llvmArgs.push_back(mIRBuilder->CreateIntCast(failResult, llvm::Type::getInt8Ty(*mLLVMContext), false));
		llvm::CallInst* callInst = mIRBuilder->CreateCall(mOverflowCheckAsm, llvmArgs);
		callInst->addFnAttr(llvm::Attribute::NoUnwind);
	}
	return valResult;
}

void BfIRCodeGen::CreateMemSet(llvm::Value* addr, llvm::Value* val, llvm::Value* size, int alignment, bool isVolatile)
{
	auto sizeConst = llvm::dyn_cast<llvm::ConstantInt>(size);
	auto valConst = llvm::dyn_cast<llvm::ConstantInt>(val);
	if ((!mIsOptimized) && (sizeConst != NULL) && (valConst != NULL))
	{
		int64 sizeVal = sizeConst->getSExtValue();
		uint8 setVal = (uint8)valConst->getSExtValue();
		if (sizeVal <= 128)
		{
			//llvm::Value* intVal = mIRBuilder->CreatePtrToInt(addr, llvm::Type::getInt32Ty(*mLLVMContext))

			int curOffset = 0;
			int sizeLeft = (int)sizeVal;
			llvm::Value* headVal;

			if (mPtrSize >= 8)
			{
				headVal = NULL;
				auto intTy = llvm::Type::getInt64Ty(*mLLVMContext);
				auto constVal = llvm::ConstantInt::get(intTy,
					((int64)setVal << 56) | ((int64)setVal << 48) | ((int64)setVal << 40) | ((int64)setVal << 32) |
					((int64)setVal << 24) | ((int64)setVal << 16) | ((int64)setVal << 8) | ((int64)setVal));
				while (sizeLeft >= 8)
				{
					if (headVal == NULL)
						headVal = mIRBuilder->CreateBitCast(addr, intTy->getPointerTo());
					llvm::Value* ptrVal = headVal;
					if (curOffset != 0)
						ptrVal = mIRBuilder->CreateConstInBoundsGEP1_32(intTy, headVal, curOffset / 8);
					mIRBuilder->CreateStore(constVal, ptrVal, isVolatile);

					curOffset += 8;
					sizeLeft -= 8;
				}
			}

			if (sizeLeft >= 4)
			{
				headVal = NULL;
				auto intTy = llvm::Type::getInt32Ty(*mLLVMContext);
				auto constVal = llvm::ConstantInt::get(intTy, ((int)setVal << 24) | ((int)setVal << 16) | ((int)setVal << 8) | ((int)setVal));
				while (sizeLeft >= 4)
				{
					if (headVal == NULL)
						headVal = mIRBuilder->CreateBitCast(addr, intTy->getPointerTo());
					llvm::Value* ptrVal = headVal;
					if (curOffset != 0)
						ptrVal = mIRBuilder->CreateConstInBoundsGEP1_32(intTy, headVal, curOffset / 4);
					mIRBuilder->CreateStore(constVal, ptrVal, isVolatile);

					curOffset += 4;
					sizeLeft -= 4;
				}
			}

			if (sizeLeft >= 2)
			{
				headVal = NULL;
				auto intTy = llvm::Type::getInt16Ty(*mLLVMContext);
				auto constVal = llvm::ConstantInt::get(intTy, ((int)setVal << 8) | ((int)setVal));
				while (sizeLeft >= 2)
				{
					if (headVal == NULL)
						headVal = mIRBuilder->CreateBitCast(addr, intTy->getPointerTo());
					llvm::Value* ptrVal = headVal;
					if (curOffset != 0)
						ptrVal = mIRBuilder->CreateConstInBoundsGEP1_32(intTy, headVal, curOffset / 2);
					mIRBuilder->CreateStore(constVal, ptrVal, isVolatile);

					curOffset += 2;
					sizeLeft -= 2;
				}
			}

			if (sizeLeft >= 1)
			{
				headVal = NULL;
				auto intTy = llvm::Type::getInt8Ty(*mLLVMContext);
				auto constVal = llvm::ConstantInt::get(intTy, ((int)setVal));
				while (sizeLeft >= 1)
				{
					if (headVal == NULL)
						headVal = mIRBuilder->CreateBitCast(addr, intTy->getPointerTo());
					llvm::Value* ptrVal = headVal;
					if (curOffset != 0)
						ptrVal = mIRBuilder->CreateConstInBoundsGEP1_32(intTy, headVal, curOffset / 1);
					mIRBuilder->CreateStore(constVal, ptrVal, isVolatile);

					curOffset += 1;
					sizeLeft -= 1;
				}
			}

			return;
		}
	}

	mIRBuilder->CreateMemSet(addr, val, size, llvm::MaybeAlign(alignment), isVolatile);
}

void BfIRCodeGen::InitTarget()
{
	llvm::SMDiagnostic Err;
	llvm::Triple theTriple = llvm::Triple(mLLVMModule->getTargetTriple());
	llvm::CodeGenOptLevel optLvl = llvm::CodeGenOptLevel::None;

	String cpuName = mTargetCPU;
	String arch = "";

	// Get the target specific parser.
	std::string Error;
	const llvm::Target *theTarget = llvm::TargetRegistry::lookupTarget(arch.c_str(), theTriple, Error);
	if (!theTarget)
	{
		Fail(StrFormat("Failed to create LLVM Target: %s", Error.c_str()));
		return;
	}

	llvm::TargetOptions Options = llvm::TargetOptions(); // InitTargetOptionsFromCodeGenFlags();

	String featuresStr;

	if (mCodeGenOptions.mOptLevel == BfOptLevel_O1)
	{
		//optLvl = CodeGenOpt::Less;
	}
	else if (mCodeGenOptions.mOptLevel == BfOptLevel_O2)
		optLvl = llvm::CodeGenOptLevel::Default;
	else if (mCodeGenOptions.mOptLevel == BfOptLevel_O3)
		optLvl = llvm::CodeGenOptLevel::Aggressive;

	if (theTriple.isWasm())
		featuresStr = "+atomics,+bulk-memory,+mutable-globals,+sign-ext";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_SSE)
		featuresStr = "+sse";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_SSE2)
		featuresStr = "+sse2";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_SSE3)
		featuresStr = "+sse3";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_SSE4)
		featuresStr = "+sse4";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_SSE41)
		featuresStr = "+sse4.1";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_AVX)
		featuresStr = "+avx";
	else if (mCodeGenOptions.mSIMDSetting == BfSIMDSetting_AVX2)
		featuresStr = "+avx2";

	std::optional<llvm::Reloc::Model> relocModel;
	llvm::CodeModel::Model cmModel = llvm::CodeModel::Small;

	switch (mCodeGenOptions.mRelocType)
	{
	case BfRelocType_Static:
		relocModel = llvm::Reloc::Model::DynamicNoPIC;
		break;
	case BfRelocType_PIC:
		relocModel = llvm::Reloc::Model::PIC_;
		break;
	case BfRelocType_DynamicNoPIC:
		relocModel = llvm::Reloc::Model::DynamicNoPIC;
		break;
	case BfRelocType_ROPI:
		relocModel = llvm::Reloc::Model::ROPI;
		break;
	case BfRelocType_RWPI:
		relocModel = llvm::Reloc::Model::RWPI;
		break;
	case BfRelocType_ROPI_RWPI:
		relocModel = llvm::Reloc::Model::ROPI_RWPI;
		break;
	default: break;
	}

	switch (mCodeGenOptions.mPICLevel)
	{
	case BfPICLevel_Not:
		mLLVMModule->setPICLevel(llvm::PICLevel::Level::NotPIC);
		break;
	case BfPICLevel_Small:
		mLLVMModule->setPICLevel(llvm::PICLevel::Level::SmallPIC);
		break;
	case BfPICLevel_Big:
		mLLVMModule->setPICLevel(llvm::PICLevel::Level::BigPIC);
		break;
	default: break;
	}

	mLLVMTargetMachine =
		theTarget->createTargetMachine(theTriple.getTriple(), cpuName.c_str(), featuresStr.c_str(),
			Options, relocModel, cmModel, optLvl);

	mLLVMModule->setDataLayout(mLLVMTargetMachine->createDataLayout());
}

void BfIRCodeGen::HandleNextCmd()
{
	int curId = mCmdCount;

	BfIRCmd cmd = (BfIRCmd)mStream->Read();
	mCmdCount++;

	switch (cmd)
	{
	case BfIRCmd_Module_Start:
		{
			CMD_PARAM(String, moduleName);
			CMD_PARAM(int, ptrSize);
			CMD_PARAM(bool, isOptimized);

			BF_ASSERT(mLLVMModule == NULL);
            mModuleName = moduleName;
			mPtrSize = ptrSize;
			mIsOptimized = isOptimized;
			mLLVMModule = new llvm::Module(moduleName.c_str(), *mLLVMContext);
			mIRBuilder = new llvm::IRBuilder<>(*mLLVMContext);

            //OutputDebugStrF("-------- Starting Module %s --------\n", moduleName.c_str());
		}
		break;
	case BfIRCmd_Module_SetTargetTriple:
		{
			CMD_PARAM(String, targetTriple);
			CMD_PARAM(String, targetCPU);

			mTargetTriple.Set(targetTriple);
			mTargetCPU = targetCPU;						

            if (targetTriple.IsEmpty())
                mLLVMModule->setTargetTriple(llvm::sys::getDefaultTargetTriple());
            else
                mLLVMModule->setTargetTriple(targetTriple.c_str());

			InitTarget();
		}
		break;
	case BfIRCmd_Module_AddModuleFlag:
		{
			CMD_PARAM(String, flag);
			CMD_PARAM(int, val);
			mLLVMModule->addModuleFlag(llvm::Module::Warning, flag.c_str(), val);

			if (flag == "CodeView")
				mIsCodeView = true;
		}
		break;
	case BfIRCmd_WriteIR:
		{
			CMD_PARAM(String, fileName);
			std::error_code ec;
			llvm::raw_fd_ostream outStream(fileName.c_str(), ec, llvm::sys::fs::OpenFlags::OF_Text);
			if (ec)
			{
				Fail("Failed writing IR '" + fileName + "': " + ec.message());
			}
			else
				mLLVMModule->print(outStream, NULL);
		}
		break;
	case BfIRCmd_SetType:
		{
			CMD_PARAM(int, typeId);
			
			CMD_PARAM(BfIRTypeEx*, type);
			//llvm::Type* type;
			//llvm::Type* elementType;

			auto& typeEntry = GetTypeEntry(typeId);
			typeEntry.mType = type;
			if (typeEntry.mInstType == NULL)
				typeEntry.mInstType = type;
			mTypeToTypeIdMap[type] = typeId;
		}
		break;
	case BfIRCmd_SetInstType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(BfIRTypeEx*, type);
			GetTypeEntry(typeId).mInstType = type;
		}
		break;
	case BfIRCmd_PrimitiveType:
		{
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			bool isSigned;
			SetResult(curId, GetLLVMType(typeCode, isSigned));
		}
		break;
	case BfIRCmd_CreateAnonymousStruct:
		{
			CMD_PARAM(CmdParamVec<BfIRTypeEx*>, members);
			
			CmdParamVec<llvm::Type*> llvmMembers;
			for (auto& memberType : members)			
				llvmMembers.push_back(memberType->mLLVMType);

			auto structType = llvm::StructType::get(*mLLVMContext, llvmMembers);
			auto typeEx = CreateTypeEx(structType);
			for (auto& memberType : members)
			{
				BF_ASSERT(memberType != NULL);
				typeEx->mMembers.Add(memberType);
			}

			SetResult(curId, typeEx);
		}
		break;
	case BfIRCmd_CreateStruct:
		{
			CMD_PARAM(String, typeName);

			auto structType = llvm::StructType::create(*mLLVMContext, typeName.c_str());
			auto typeEx = CreateTypeEx(structType);			
			SetResult(curId, typeEx);
		}
		break;
	case BfIRCmd_StructSetBody:
		{
			BfIRTypeEx* typeEx = NULL;
			BfIRTypeEntry* typeEntry = NULL;

			Read(typeEx, &typeEntry);
			CMD_PARAM(CmdParamVec<BfIRTypeEx*>, members);
			CMD_PARAM(int, instSize);
			CMD_PARAM(int, instAlign);
			CMD_PARAM(bool, isPacked);

			typeEx->mMembers.clear();
			auto type = typeEx->mLLVMType;

			CmdParamVec<llvm::Type*> llvmMembers;
			for (auto& memberType : members)
			{
				BF_ASSERT(memberType != NULL);
				typeEx->mMembers.Add(memberType);
				llvmMembers.push_back(memberType->mLLVMType);
			}

			BF_ASSERT(llvm::isa<llvm::StructType>(type));
			auto structType = (llvm::StructType*)type;
			if (structType->isOpaque())
				structType->setBody(llvmMembers, isPacked);
			if (typeEntry != NULL)
			{
				typeEntry->mSize = instSize;
				typeEntry->mAlign = instAlign;
			}
		}
		break;
	case  BfIRCmd_Type:
		{
			CMD_PARAM(BfIRTypeEntry*, typeEntry);
			auto type = typeEntry->mType;
			SetResult(curId, type);
		}
		break;
	case  BfIRCmd_TypeInst:
		{
			CMD_PARAM(BfIRTypeEntry*, typeEntry);
			SetResult(curId, typeEntry->mInstType);
		}
		break;
	case BfIRCmd_TypeInstPtr:
		{
			CMD_PARAM(BfIRTypeEntry*, typeEntry);
			SetResult(curId, GetPointerTypeEx(typeEntry->mInstType));
		}
		break;
	case BfIRCmd_GetType:
		{
			CMD_PARAM(BfIRTypedValue, typedValue);
			BF_ASSERT(typedValue.mTypeEx != NULL);
			SetResult(curId, typedValue.mTypeEx);
		}
		break;
	case BfIRCmd_GetPointerToFuncType:
		{
			BfIRTypeEx* funcType = NULL;
			ReadFunctionType(funcType);			
			SetResult(curId, GetPointerTypeEx(funcType));
		}
		break;
	case BfIRCmd_GetPointerToType:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			SetResult(curId, GetPointerTypeEx(type));
		}
		break;
	case BfIRCmd_GetSizedArrayType:
		{
			BfIRTypeEx* elementType = NULL;
			BfIRTypeEntry* elementTypeEntry = NULL;
			Read(elementType, &elementTypeEntry);

			auto typeEx = new BfIRTypeEx();			
			typeEx->mMembers.Add(elementType);
			mIRTypeExs.Add(typeEx);
			
			CMD_PARAM(int, length);
			if (elementTypeEntry != NULL)
				typeEx->mLLVMType = llvm::ArrayType::get(GetSizeAlignedType(elementTypeEntry)->mLLVMType, length);			
			else
				typeEx->mLLVMType = llvm::ArrayType::get(elementType->mLLVMType, length);

			SetResult(curId, typeEx);
		}
		break;
	case BfIRCmd_GetVectorType:
		{
			CMD_PARAM(BfIRTypeEx*, elementType);
			CMD_PARAM(int, length);

			auto typeEx = new BfIRTypeEx();
			mIRTypeExs.Add(typeEx);
			typeEx->mLLVMType = llvm::FixedVectorType::get(elementType->mLLVMType, length);
			typeEx->mMembers.Add(elementType);
			SetResult(curId, typeEx);
		}
		break;
	case BfIRCmd_CreateConstAgg:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			CMD_PARAM(CmdParamVec<llvm::Value*>, values)
			llvm::SmallVector<llvm::Constant*, 8> copyValues;

			if (auto arrayType = llvm::dyn_cast<llvm::ArrayType>(type->mLLVMType))
			{
				for (auto val : values)
				{
					auto constValue = llvm::dyn_cast<llvm::Constant>(val);
					BF_ASSERT(constValue != NULL);
					copyValues.push_back(constValue);
				}

				int fillCount = (int)(arrayType->getNumElements() - copyValues.size());
				if (fillCount > 0)
				{
					auto lastValue = copyValues.back();
					for (int i = 0; i < fillCount; i++)
						copyValues.push_back(lastValue);
				}

				BfIRTypedValue result;
				result.mTypeEx = type;
				result.mValue = llvm::ConstantArray::get(arrayType, copyValues);
				SetResult(curId, result);
			}
			else if (auto structType = llvm::dyn_cast<llvm::StructType>(type->mLLVMType))
			{
				FixValues(structType, values);
				for (auto val : values)
				{
					auto constValue = llvm::dyn_cast<llvm::Constant>(val);
					BF_ASSERT(constValue != NULL);
					copyValues.push_back(constValue);
				}
				BfIRTypedValue result;
				result.mTypeEx = type;
				result.mValue = llvm::ConstantStruct::get(structType, copyValues);
				SetResult(curId, result);
			}
			else
				Fail("Bad type");
		}
		break;
	case BfIRCmd_CreateConstStructZero:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			BfIRTypedValue result;
			result.mTypeEx = type;
			result.mValue = llvm::ConstantAggregateZero::get(type->mLLVMType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_CreateConstString:
		{
			CMD_PARAM(String, str);

			BfIRTypedValue result;
			result.mValue = llvm::ConstantDataArray::getString(*mLLVMContext, llvm::StringRef(str.c_str(), str.length()));
			result.mTypeEx = GetTypeEx(result.mValue->getType());
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_ConfigConst:
		{
			CMD_PARAM(int, constIdx);
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			if (typeCode == BfTypeCode_IntPtr)
				typeCode = (mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
			llvm::Constant* constVal = (typeCode == BfTypeCode_Int32) ?
				mConfigConsts32[constIdx] :
				mConfigConsts64[constIdx];
			SetResult(curId, constVal);
		}
		break;
	case BfIRCmd_SetName:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
			CMD_PARAM(String, name);
			val->setName(name.c_str());
		}
		break;
	case BfIRCmd_CreateUndefValue:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			BfIRTypedValue result;
			result.mTypeEx = type;
			result.mValue = llvm::UndefValue::get(type->mLLVMType);
			SetResult(curId, result);			
		}
		break;
	case BfIRCmd_NumericCast:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(bool, valIsSigned);
			BfTypeCode typeCode = (BfTypeCode)mStream->Read();
			BfTypeCode valTypeCode = GetTypeCode(val->getType(), valIsSigned);
			bool toSigned;
			auto toLLVMType = GetLLVMType(typeCode, toSigned);

			llvm::Value* retVal = NULL;

			if (BfIRBuilder::IsInt(typeCode))
			{
				// Int -> Int
				if ((BfIRBuilder::IsInt(valTypeCode)) || (valTypeCode == BfTypeCode_Boolean))
				{
					retVal = mIRBuilder->CreateIntCast(val, toLLVMType, toSigned && valIsSigned);
				}
				else // Float -> Int
				{
					if (BfIRBuilder::IsSigned(typeCode))
						retVal = mIRBuilder->CreateFPToSI(val, toLLVMType);
					else
						retVal = mIRBuilder->CreateFPToUI(val, toLLVMType);
				}
			}
			else
			{
				// Int -> Float
				if ((BfIRBuilder::IsInt(valTypeCode)) || (valTypeCode == BfTypeCode_Boolean))
				{
					if (BfIRBuilder::IsSigned(valTypeCode))
						retVal = mIRBuilder->CreateSIToFP(val, toLLVMType);
					else
						retVal = mIRBuilder->CreateUIToFP(val, toLLVMType);
				}
				else // Float -> Float
				{
					retVal = mIRBuilder->CreateFPCast(val, toLLVMType);
				}
			}
			SetResult(curId, retVal);
		}
		break;
	case BfIRCmd_CmpEQ:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOEQ(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpEQ(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpNE:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpUNE(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpNE(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSLT:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOLT(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpSLT(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpULT:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOLT(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpULT(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSLE:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOLE(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpSLE(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpULE:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOLE(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpULE(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSGT:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOGT(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpSGT(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpUGT:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOGT(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpUGT(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpSGE:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOGE(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpSGE(lhs, rhs));
		}
		break;
	case BfIRCmd_CmpUGE:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFCmpOGE(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateICmpUGE(lhs, rhs));
		}
		break;

	case BfIRCmd_Add:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			CMD_PARAM(int8, overflowCheckKind);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFAdd(lhs, rhs));
			else if ((overflowCheckKind & (BfOverflowCheckKind_Signed | BfOverflowCheckKind_Unsigned)) != 0)
				SetResult(curId, DoCheckedIntrinsic(((overflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? llvm::Intrinsic::sadd_with_overflow : llvm::Intrinsic::uadd_with_overflow,
					lhs, rhs, (overflowCheckKind & BfOverflowCheckKind_Flag_UseAsm) != 0));
			else
				SetResult(curId, mIRBuilder->CreateAdd(lhs, rhs));
		}
		break;
	case BfIRCmd_Sub:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			CMD_PARAM(int8, overflowCheckKind);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFSub(lhs, rhs));
			else if ((overflowCheckKind & (BfOverflowCheckKind_Signed | BfOverflowCheckKind_Unsigned)) != 0)
				SetResult(curId, DoCheckedIntrinsic(((overflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? llvm::Intrinsic::ssub_with_overflow : llvm::Intrinsic::usub_with_overflow,
					lhs, rhs, (overflowCheckKind & BfOverflowCheckKind_Flag_UseAsm) != 0));
			else
				SetResult(curId, mIRBuilder->CreateSub(lhs, rhs));
		}
		break;
	case BfIRCmd_Mul:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			CMD_PARAM(int8, overflowCheckKind);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFMul(lhs, rhs));
			else if ((overflowCheckKind & (BfOverflowCheckKind_Signed | BfOverflowCheckKind_Unsigned)) != 0)
				SetResult(curId, DoCheckedIntrinsic(((overflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? llvm::Intrinsic::smul_with_overflow : llvm::Intrinsic::umul_with_overflow,
					lhs, rhs, (overflowCheckKind & BfOverflowCheckKind_Flag_UseAsm) != 0));
			else
				SetResult(curId, mIRBuilder->CreateMul(lhs, rhs));
		}
		break;
	case BfIRCmd_SDiv:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFDiv(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateSDiv(lhs, rhs));
		}
		break;
	case BfIRCmd_UDiv:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateUDiv(lhs, rhs));
		}
		break;
	case BfIRCmd_SRem:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			if (lhs->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFRem(lhs, rhs));
			else
				SetResult(curId, mIRBuilder->CreateSRem(lhs, rhs));
		}
		break;
	case BfIRCmd_URem:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateURem(lhs, rhs));
		}
		break;
	case BfIRCmd_And:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateAnd(lhs, rhs));
		}
		break;
	case BfIRCmd_Or:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateOr(lhs, rhs));
		}
		break;
	case BfIRCmd_Xor:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateXor(lhs, rhs));
		}
		break;
	case BfIRCmd_Shl:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateShl(lhs, rhs));
		}
		break;
	case BfIRCmd_AShr:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateAShr(lhs, rhs));
		}
		break;
	case BfIRCmd_LShr:
		{
			CMD_PARAM(llvm::Value*, lhs);
			CMD_PARAM(llvm::Value*, rhs);
			SetResult(curId, mIRBuilder->CreateLShr(lhs, rhs));
		}
		break;
	case BfIRCmd_Neg:
		{
			CMD_PARAM(llvm::Value*, val);
			if (val->getType()->isFloatingPointTy())
				SetResult(curId, mIRBuilder->CreateFNeg(val));
			else
				SetResult(curId, mIRBuilder->CreateNeg(val));
		}
		break;
	case BfIRCmd_Not:
		{
			CMD_PARAM(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateNot(val));
		}
		break;
	case BfIRCmd_BitCast:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(BfIRTypeEx*, toType);

			BfIRTypedValue result;		
			result.mTypeEx = toType;

			auto fromType = val.mValue->getType();
			if ((!fromType->isPointerTy()) || (!toType->mLLVMType->isPointerTy()))
			{
				if (fromType->isIntegerTy())				
					result.mValue = mIRBuilder->CreateIntToPtr(val.mValue, toType->mLLVMType);									
				else
					result.mValue = mIRBuilder->CreatePtrToInt(val.mValue, toType->mLLVMType);				
			}
			else
				result.mValue = mIRBuilder->CreateBitCast(val.mValue, toType->mLLVMType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_PtrToInt:
		{
			CMD_PARAM(llvm::Value*, val);
			auto typeCode = (BfTypeCode)mStream->Read();
			bool isSigned;			

			BfIRTypedValue result;
			result.mTypeEx = GetTypeEx(typeCode, isSigned);
			result.mValue = mIRBuilder->CreatePtrToInt(val, result.mTypeEx->mLLVMType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_IntToPtr:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(BfIRTypeEx*, toType);

			BfIRTypedValue result;
			result.mTypeEx = toType;
			result.mValue = mIRBuilder->CreateIntToPtr(val, toType->mLLVMType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_InboundsGEP1_32:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(int, idx0);
			
			BfIRTypedValue result;
			result.mTypeEx = val.mTypeEx;
			auto alignedPtr = GetAlignedPtr(val);

			auto compositeType = GetTypeMember(alignedPtr.mTypeEx, 0);
			result.mValue = mIRBuilder->CreateConstInBoundsGEP1_32(compositeType->mLLVMType, alignedPtr.mValue, idx0);			
			SetResult(curId, result);			
		}
		break;
	case BfIRCmd_InboundsGEP2_32:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(int, idx0);
			CMD_PARAM(int, idx1);
			
			auto compositeType = GetTypeMember(val.mTypeEx, 0);
			int elemIdx = BF_MIN(idx1, (int)compositeType->mMembers.mSize - 1);
			BfIRTypeEx* elemType = GetTypeMember(compositeType, elemIdx);

			BfIRTypedValue result;
			result.mValue = mIRBuilder->CreateConstInBoundsGEP2_32(compositeType->mLLVMType, val.mValue, idx0, idx1);
			result.mTypeEx = GetPointerTypeEx(elemType);

			SetResult(curId, result);
		}
		break;
	case BfIRCmd_InBoundsGEP1:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(llvm::Value*, idx0);

			BfIRTypedValue result;			
			auto alignedPtr = GetAlignedPtr(val);			
			auto compositeType = GetTypeMember(alignedPtr.mTypeEx, 0);

			FixIndexer(idx0);
			result.mValue = mIRBuilder->CreateInBoundsGEP(compositeType->mLLVMType, alignedPtr.mValue, idx0);
			result.mTypeEx = val.mTypeEx;
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_InBoundsGEP2:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(llvm::Value*, idx0);
			CMD_PARAM(llvm::Value*, idx1);
			FixIndexer(idx0);
			FixIndexer(idx1);
			llvm::Value* indices[2] = { idx0, idx1 };

			int elemIdx = 0;
			if (auto constInt = llvm::dyn_cast<llvm::ConstantInt>(idx1))							
				elemIdx = BF_MIN((int)constInt->getSExtValue(), (int)val.mTypeEx->mMembers.mSize - 1);

			auto compositeType = GetTypeMember(val.mTypeEx, 0);
			BfIRTypeEx* elemType = GetTypeMember(compositeType, elemIdx);

			BfIRTypedValue result;			
			result.mValue = mIRBuilder->CreateInBoundsGEP(compositeType->mLLVMType, val.mValue, llvm::makeArrayRef(indices));
			result.mTypeEx = GetPointerTypeEx(elemType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_IsNull:
		{
			CMD_PARAM(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateIsNull(val));
		}
		break;
	case BfIRCmd_IsNotNull:
		{
			CMD_PARAM(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateIsNotNull(val));
		}
		break;
	case BfIRCmd_ExtractValue:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(int, idx);
			
			auto compositeType = val.mTypeEx;
			int elemIdx = BF_MIN(idx, (int)compositeType->mMembers.mSize - 1);
			auto elemType = GetTypeMember(compositeType, elemIdx);

			BfIRTypedValue result;
			result.mTypeEx = elemType;
			result.mValue = mIRBuilder->CreateExtractValue(val.mValue, llvm::makeArrayRef((unsigned)idx));
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_InsertValue:
		{
			CMD_PARAM(BfIRTypedValue, agg);
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(int, idx);

			BfIRTypedValue result;
			result.mTypeEx = agg.mTypeEx;
			result.mValue = mIRBuilder->CreateInsertValue(agg.mValue, val.mValue, llvm::makeArrayRef((unsigned)idx));
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_Alloca:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			if (type->mLLVMType->isStructTy())
			{
				BF_ASSERT(!((llvm::StructType*)type->mLLVMType)->isOpaque());
			}

			BfIRTypedValue result;
			result.mTypeEx = GetPointerTypeEx(type);
			result.mValue = mIRBuilder->CreateAlloca(type->mLLVMType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_AllocaArray:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			CMD_PARAM(llvm::Value*, arraySize);

			auto origType = type;
			auto typeEntry = GetTypeEntry(type);
			if (typeEntry != NULL)
				type = GetSizeAlignedType(typeEntry);

			BfIRTypedValue typedValue;
			typedValue.mTypeEx = GetPointerTypeEx(type);
			
			if (origType != type)
			{
				typedValue.mValue = mIRBuilder->CreateAlloca(type->mLLVMType, arraySize);
				SetResultAligned(curId, typedValue);
			}
			else
			{
				typedValue.mValue = mIRBuilder->CreateAlloca(type->mLLVMType, arraySize);
				SetResult(curId, typedValue);
			}
		}
		break;
	case BfIRCmd_SetAllocaAlignment:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
			CMD_PARAM(int, alignment);
			auto inst = llvm::dyn_cast<llvm::AllocaInst>(val);
			inst->setAlignment(llvm::Align(alignment));
		}
		break;
	case BfIRCmd_SetAllocaNoChkStkHint:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
			// LLVM does not support this
		}
		break;
	case BfIRCmd_LifetimeStart:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateLifetimeStart(val));
		}
		break;
	case BfIRCmd_LifetimeEnd:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateLifetimeEnd(val));
		}
		break;
	case BfIRCmd_LifetimeSoftEnd:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
		}
		break;
	case BfIRCmd_LifetimeExtend:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, val);
		}
		break;
	case BfIRCmd_Load:
		{
			CMD_PARAM(BfIRTypedValue, typedValue);
			BF_ASSERT(typedValue.mTypeEx != NULL);
			CMD_PARAM(bool, isVolatile);

			BfIRTypedValue result;
			result.mTypeEx = GetTypeMember(typedValue.mTypeEx, 0);
			result.mValue = mIRBuilder->CreateLoad(result.mTypeEx->mLLVMType, typedValue.mValue, isVolatile);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_AlignedLoad:
		{
			CMD_PARAM(BfIRTypedValue, typedValue);
			BF_ASSERT(typedValue.mTypeEx != NULL);
			CMD_PARAM(int, alignment);
			CMD_PARAM(bool, isVolatile);
			
			BfIRTypedValue result;
			result.mTypeEx = GetTypeMember(typedValue.mTypeEx, 0);
			result.mValue = mIRBuilder->CreateAlignedLoad(result.mTypeEx->mLLVMType, typedValue.mValue, llvm::MaybeAlign(alignment), isVolatile);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_Store:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(BfIRTypedValue, ptr);
			CMD_PARAM(bool, isVolatile);

			if ((!TryMemCpy(ptr, val.mValue)) &&
				(!TryVectorCpy(ptr, val.mValue)))
				SetResult(curId, mIRBuilder->CreateStore(val.mValue, ptr.mValue, isVolatile));
		}
		break;
	case BfIRCmd_AlignedStore:
		{
			CMD_PARAM(BfIRTypedValue, val);
			CMD_PARAM(BfIRTypedValue, ptr);
			CMD_PARAM(int, alignment);
			CMD_PARAM(bool, isVolatile);
			if ((!TryMemCpy(ptr, val.mValue)) &&
				(!TryVectorCpy(ptr, val.mValue)))
				SetResult(curId, mIRBuilder->CreateAlignedStore(val.mValue, ptr.mValue, llvm::MaybeAlign(alignment), isVolatile));
		}
		break;
	case BfIRCmd_MemSet:
		{
			CMD_PARAM(llvm::Value*, addr);
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(llvm::Value*, size);
			CMD_PARAM(int, alignment);

			CreateMemSet(addr, val, size, alignment);
		}
		break;
	case BfIRCmd_Fence:
		{
			BfIRFenceType fenceType = (BfIRFenceType)mStream->Read();
			if (fenceType == BfIRFenceType_AcquireRelease)
				mIRBuilder->CreateFence(llvm::AtomicOrdering::AcquireRelease);
		}
		break;
	case BfIRCmd_StackSave:
		{
			//auto intrin = llvm::Intrinsic::getDeclaration(mLLVMModule, llvm::Intrinsic::stacksave);
			//CreateStackSave

			//auto callInst = mIRBuilder->CreateCall(intrin);

			BfIRTypedValue result;
			result.mValue = mIRBuilder->CreateStackSave();
			result.mTypeEx = GetTypeEx(result.mValue->getType());
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_StackRestore:
		{
			CMD_PARAM(llvm::Value*, stackVal);
			//auto intrin = llvm::Intrinsic::getDeclaration(mLLVMModule, llvm::Intrinsic::stackrestore);
			//auto callInst = mIRBuilder->CreateCall(intrin, llvm::SmallVector<llvm::Value*, 1> {stackVal });
			auto callInst = mIRBuilder->CreateStackRestore(stackVal);
			SetResult(curId, callInst);
		}
		break;
	case BfIRCmd_GlobalVariable:
		{
			CMD_PARAM(BfIRTypeEx*, varType);
			CMD_PARAM(bool, isConstant);
			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
			CMD_PARAM(String, name);
			CMD_PARAM(bool, isTLS);
			CMD_PARAM(llvm::Constant*, initializer);

			auto globalVariable = mLLVMModule->getGlobalVariable(name.c_str());
			if (globalVariable == NULL)
			{
				globalVariable = new llvm::GlobalVariable(
					*mLLVMModule,
					varType->mLLVMType,
					isConstant,
					LLVMMapLinkageType(linkageType),
					initializer,
					name.c_str(), NULL, isTLS ? llvm::GlobalValue::GeneralDynamicTLSModel : llvm::GlobalValue::NotThreadLocal);
			}

			BfIRTypedValue result;
			result.mValue = globalVariable;
			result.mTypeEx = GetPointerTypeEx(varType);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_GlobalVar_SetUnnamedAddr:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(bool, unnamedAddr);
			((llvm::GlobalVariable*)val)->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		}
		break;
	case BfIRCmd_GlobalVar_SetInitializer:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(llvm::Constant*, initializer);
			((llvm::GlobalVariable*)val)->setInitializer(initializer);
		}
		break;
	case BfIRCmd_GlobalVar_SetAlignment:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(int, alignment);
			((llvm::GlobalVariable*)val)->setAlignment(llvm::Align(alignment));
		}
		break;
	case BfIRCmd_GlobalVar_SetStorageKind:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(int, storageKind);
			((llvm::GlobalVariable*)val)->setDLLStorageClass((llvm::GlobalValue::DLLStorageClassTypes)storageKind);
		}
		break;
	case BfIRCmd_GlobalStringPtr:
		{
			CMD_PARAM(String, str);

			BfIRTypedValue result;
			result.mValue = mIRBuilder->CreateGlobalStringPtr(llvm::StringRef(str.c_str(), str.length()));
			result.mTypeEx = GetTypeEx(result.mValue->getType());
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_SetReflectTypeData:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			CMD_PARAM(BfIRTypedValue, value);
			mReflectDataMap[type] = value;
		}
		break;
	case BfIRCmd_CreateBlock:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(bool, addNow);
			auto block = llvm::BasicBlock::Create(*mLLVMContext, name.c_str());
			if (addNow)
				mActiveFunction->insert(mActiveFunction->end(), block);
			SetResult(curId, block);
		}
		break;
	case BfIRCmd_MaybeChainNewBlock:
		{
			CMD_PARAM(String, name);
			auto newBlock = mIRBuilder->GetInsertBlock();
			if (!newBlock->empty())
			{
				auto bb = llvm::BasicBlock::Create(*mLLVMContext, name.c_str());
				mIRBuilder->CreateBr(bb);
				mActiveFunction->insert(mActiveFunction->end(), bb);
				mIRBuilder->SetInsertPoint(bb);
				newBlock = bb;
			}
			SetResult(curId, newBlock);
		}
		break;
	case BfIRCmd_AddBlock:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			mActiveFunction->insert(mActiveFunction->end(), block);
		}
		break;
	case BfIRCmd_DropBlocks:
		{
			//TODO: Not even needed
// 			CMD_PARAM(llvm::BasicBlock*, startingBlock);
// 			auto& basicBlockList = mActiveFunction->getBasicBlockList();
// 			int postExitBlockIdx = -1;
// 
// 			auto itr = basicBlockList.rbegin();
// 			int blockIdx = (int)basicBlockList.size() - 1;
// 			while (itr != basicBlockList.rend())
// 			{
// 				auto& block = *itr++;
// 				block.dropAllReferences();
// 				if (&block == startingBlock)
// 				{
// 					postExitBlockIdx = blockIdx;
// 					break;
// 				}
// 				blockIdx--;
// 			}
// 
// 			while ((int)basicBlockList.size() > postExitBlockIdx)
// 			{
// 				auto& block = basicBlockList.back();
// 				block.eraseFromParent();
// 			}
		}
		break;
	case BfIRCmd_MergeBlockDown:
		{
			CMD_PARAM(llvm::BasicBlock*, fromBlock);
			CMD_PARAM(llvm::BasicBlock*, intoBlock);
			//llvm::BasicBlock::InstListType& fromInstList = fromBlock->getInstList();
			//llvm::BasicBlock::InstListType& intoInstList = intoBlock->getInstList();
			//intoInstList.splice(intoInstList.begin(), fromInstList, fromInstList.begin(), fromInstList.end());
			intoBlock->splice(intoBlock->begin(), fromBlock);
			fromBlock->eraseFromParent();
		}
		break;
	case BfIRCmd_GetInsertBlock:
		{
			SetResult(curId, mIRBuilder->GetInsertBlock());
		}
		break;
	case BfIRCmd_SetInsertPoint:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			if (mLockedBlocks.Contains(block))
				Fail("Attempt to modify locked block");
			mIRBuilder->SetInsertPoint(block);
		}
		break;
	case BfIRCmd_SetInsertPointAtStart:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			mIRBuilder->SetInsertPoint(block, block->begin());			
			// SetInsertPoint can clear the debug loc so reset it here
			mIRBuilder->SetCurrentDebugLocation(mDebugLoc);			
		}
		break;
	case BfIRCmd_EraseFromParent:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			block->eraseFromParent();
		}
		break;
	case BfIRCmd_DeleteBlock:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			delete block;
		}
		break;
	case BfIRCmd_EraseInstFromParent:
		{
			CMD_PARAM(llvm::Value*, instVal);
			BF_ASSERT(llvm::isa<llvm::Instruction>(instVal));
			((llvm::Instruction*)instVal)->eraseFromParent();
		}
		break;
	case BfIRCmd_CreateBr:
	case BfIRCmd_CreateBr_NoCollapse:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			mIRBuilder->CreateBr(block);
		}
		break;
	case BfIRCmd_CreateBr_Fake:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			// Do nothing
		}
		break;
	case BfIRCmd_CreateCondBr:
		{
			CMD_PARAM(llvm::Value*, condVal);
			CMD_PARAM(llvm::BasicBlock*, trueBlock);
			CMD_PARAM(llvm::BasicBlock*, falseBlock);
			mIRBuilder->CreateCondBr(condVal, trueBlock, falseBlock);
		}
		break;
	case BfIRCmd_MoveBlockToEnd:
		{
			CMD_PARAM(llvm::BasicBlock*, block);
			block->moveAfter(&block->getParent()->back());
		}
		break;
	case BfIRCmd_CreateSwitch:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(llvm::BasicBlock*, dest);
			CMD_PARAM(int, numCases);
			SetResult(curId, mIRBuilder->CreateSwitch(val, dest, numCases));
		}
		break;
	case BfIRCmd_AddSwitchCase:
		{
			CMD_PARAM(llvm::Value*, switchVal);
			CMD_PARAM(llvm::Value*, caseVal);
			CMD_PARAM(llvm::BasicBlock*, caseBlock);
			BF_ASSERT(llvm::isa<llvm::SwitchInst>(switchVal));
			BF_ASSERT(llvm::isa<llvm::ConstantInt>(caseVal));
			((llvm::SwitchInst*)switchVal)->addCase((llvm::ConstantInt*)caseVal, caseBlock);
		}
		break;
	case BfIRCmd_SetSwitchDefaultDest:
		{
			CMD_PARAM(llvm::Value*, switchVal);
			CMD_PARAM(llvm::BasicBlock*, caseBlock);
			((llvm::SwitchInst*)switchVal)->setDefaultDest(caseBlock);
		}
		break;
	case BfIRCmd_CreatePhi:
		{
			CMD_PARAM(BfIRTypeEx*, type);
			CMD_PARAM(int, incomingCount);			
			BfIRTypedValue result;			
			result.mTypeEx = type;
			result.mValue = mIRBuilder->CreatePHI(type->mLLVMType, incomingCount);
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_AddPhiIncoming:
		{
			CMD_PARAM(llvm::Value*, phiValue);
			CMD_PARAM(llvm::Value*, value);
			CMD_PARAM(llvm::BasicBlock*, comingFrom);
			BF_ASSERT(llvm::isa<llvm::PHINode>(phiValue));
			((llvm::PHINode*)phiValue)->addIncoming(value, comingFrom);
		}
		break;
	case BfIRCmd_GetIntrinsic:
		{
			CMD_PARAM(String, intrinName);
			CMD_PARAM(int, intrinId);
			CMD_PARAM(BfIRTypeEx*, returnType);
			CMD_PARAM(CmdParamVec<BfIRTypeEx*>, paramTypes);

			llvm::Function* func = NULL;

			struct _Intrinsics
			{
				llvm::Intrinsic::ID mID;
				int mArg0;
				int mArg1;
				int mArg2;
			};

			static _Intrinsics intrinsics[] =
			{
				{ (llvm::Intrinsic::ID)-1, -1}, // PLATFORM,
				{ llvm::Intrinsic::fabs, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // add,
				{ (llvm::Intrinsic::ID)-2, -1}, // and,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicAdd,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicAnd,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicCmpStore,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicCmpStore_Weak,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicCmpXChg,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicFence,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicLoad,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicMax,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicMin,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicNAnd,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicOr,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicStore,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicSub,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicUMax,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicUMin,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicXChg,
				{ (llvm::Intrinsic::ID)-2, -1}, // AtomicXor,
				{ llvm::Intrinsic::bswap, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // cast,
				{ llvm::Intrinsic::cos, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // cpuid
				{ llvm::Intrinsic::debugtrap, -1}, // debugtrap,
				{ (llvm::Intrinsic::ID)-2, -1}, // div
				{ (llvm::Intrinsic::ID)-2, -1}, // eq
				{ llvm::Intrinsic::floor, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // free
				{ (llvm::Intrinsic::ID)-2, -1}, // gt
				{ (llvm::Intrinsic::ID)-2, -1}, // gte
				{ (llvm::Intrinsic::ID)-2, -1}, // index
				{ llvm::Intrinsic::log, 0, -1},
				{ llvm::Intrinsic::log10, 0, -1},
				{ llvm::Intrinsic::log2, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // lt
				{ (llvm::Intrinsic::ID)-2, -1}, // lte
				{ (llvm::Intrinsic::ID)-2}, // malloc
				{ (llvm::Intrinsic::ID)-2, -1}, // max
				{ llvm::Intrinsic::memcpy, 0, 1, 2},
				{ llvm::Intrinsic::memmove, 0, 2},
				{ llvm::Intrinsic::memset, 0, 2},
				{ (llvm::Intrinsic::ID)-2, -1}, // min
				{ (llvm::Intrinsic::ID)-2, -1}, // mod
				{ (llvm::Intrinsic::ID)-2, -1}, // mul
				{ (llvm::Intrinsic::ID)-2, -1}, // neq
				{ (llvm::Intrinsic::ID)-2, -1}, // not
				{ (llvm::Intrinsic::ID)-2, -1}, // or
				{ llvm::Intrinsic::pow, 0, -1},
				{ llvm::Intrinsic::powi, 0, -1},
				{ llvm::Intrinsic::returnaddress, -1},
				{ llvm::Intrinsic::round, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // sar
				{ (llvm::Intrinsic::ID)-2, -1}, // shl
				{ (llvm::Intrinsic::ID)-2, -1}, // shr
				{ (llvm::Intrinsic::ID)-2, -1}, // shuffle
				{ llvm::Intrinsic::sin, 0, -1},
				{ llvm::Intrinsic::sqrt, 0, -1},
				{ (llvm::Intrinsic::ID)-2, -1}, // sub,
				{ (llvm::Intrinsic::ID)-2, -1}, // va_arg,
				{ llvm::Intrinsic::vaend, -1}, // va_end,
				{ llvm::Intrinsic::vastart, -1}, // va_start,
				{ (llvm::Intrinsic::ID)-2, -1}, // xgetbv
				{ (llvm::Intrinsic::ID)-2, -1}, // xor
			};
			BF_STATIC_ASSERT(BF_ARRAY_COUNT(intrinsics) == BfIRIntrinsic_COUNT);

			CmdParamVec<llvm::Type*> useParams;
			if (intrinsics[intrinId].mArg0 != -1)
			{
				useParams.push_back(paramTypes[0]->mLLVMType);
				if (intrinsics[intrinId].mArg1 != -1)
				{
					useParams.push_back(paramTypes[1]->mLLVMType);
					if (intrinsics[intrinId].mArg2 != -1)
					{
						useParams.push_back(paramTypes[2]->mLLVMType);
					}
				}
			}

			bool isFakeIntrinsic = (int)intrinsics[intrinId].mID == -2;
			if (isFakeIntrinsic)
			{
				auto intrinsicData = mIntrinsicData.Alloc();
				intrinsicData->mName = intrinName;
				intrinsicData->mIntrinsic = (BfIRIntrinsic)intrinId;
				intrinsicData->mReturnType = returnType;

				BfIRCodeGenEntry entry;
				entry.mKind = BfIRCodeGenEntryKind_IntrinsicData;
				entry.mIntrinsicData = intrinsicData;
				mResults.TryAdd(curId, entry);
				break;
			}

			if (intrinId == BfIRIntrinsic__PLATFORM)
			{
				int colonPos = (int)intrinName.IndexOf(':');
				String platName = intrinName.Substring(0, colonPos);
				String platIntrinName = intrinName.Substring(colonPos + 1);

				if (platName.IsEmpty())
				{
					auto intrinsicData = mIntrinsicData.Alloc();
					intrinsicData->mName = platIntrinName;
					intrinsicData->mIntrinsic = BfIRIntrinsic__PLATFORM;
					intrinsicData->mReturnType = returnType;

					BfIRCodeGenEntry entry;
					entry.mKind = BfIRCodeGenEntryKind_IntrinsicData;
					entry.mIntrinsicData = intrinsicData;
					mResults.TryAdd(curId, entry);
					break;
				}

				llvm::Intrinsic::ID intrin = llvm::Intrinsic::getIntrinsicForClangBuiltin(platName.c_str(), platIntrinName.c_str());
				if ((int)intrin <= 0)
					FatalError(StrFormat("Unable to find intrinsic '%s'", intrinName.c_str()));
				else
					func = llvm::Intrinsic::getDeclaration(mLLVMModule, intrinsics[intrinId].mID, useParams);
			}
			else
			{
				BF_ASSERT(intrinsics[intrinId].mID != (llvm::Intrinsic::ID)-1);
				func = llvm::Intrinsic::getDeclaration(mLLVMModule, intrinsics[intrinId].mID, useParams);
			}
			mIntrinsicReverseMap[func] = intrinId;

			auto funcTypeEx = CreateTypeEx(func->getFunctionType());
			funcTypeEx->mMembers.Add(returnType);
			for (auto typeEx : paramTypes)
				funcTypeEx->mMembers.Add(typeEx);

			BfIRTypedValue result;
			result.mTypeEx = GetPointerTypeEx(funcTypeEx);
			result.mValue = func;
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_CreateFunctionType:
		{
			CMD_PARAM(BfIRTypeEx*, resultType);
			CMD_PARAM(CmdParamVec<BfIRTypeEx*>, paramTypes);
			CMD_PARAM(bool, isVarArg);
			
			CmdParamVec<llvm::Type*> llvmTypes;
			for (auto typeEx : paramTypes)
			{
				if (typeEx->mLLVMType->isPointerTy())
				{
					BF_ASSERT(!typeEx->mMembers.IsEmpty());
				}

				llvmTypes.push_back(typeEx->mLLVMType);
			}

			auto funcType = llvm::FunctionType::get(resultType->mLLVMType, llvmTypes, isVarArg);

			auto typeEx = CreateTypeEx(funcType);
			if (typeEx->mMembers.IsEmpty())
			{
				typeEx->mMembers.Add(resultType);
				for (auto paramType : paramTypes)			
					typeEx->mMembers.Add(paramType);
			}

			SetResult(curId, typeEx);
		}
		break;
	case BfIRCmd_CreateFunction:
		{
			BfIRTypeEx* type = NULL;
			ReadFunctionType(type);

			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
			CMD_PARAM(String, name);

			BfIRTypedValue result;
			result.mTypeEx = GetPointerTypeEx(type);

			auto func = mLLVMModule->getFunction(name.c_str());
			if ((func == NULL) || (func->getFunctionType() != type->mLLVMType))
				func = llvm::Function::Create((llvm::FunctionType*)type->mLLVMType, LLVMMapLinkageType(linkageType), name.c_str(), mLLVMModule);
			result.mValue = func;
			SetResult(curId, result);
		}
		break;
	case BfIRCmd_SetFunctionName:
		{
			CMD_PARAM(llvm::Value*, func);
			CMD_PARAM(String, name);
			llvm::Function* llvmFunc = llvm::dyn_cast<llvm::Function>(func);
			llvmFunc->setName(name.c_str());
		}
		break;
	case BfIRCmd_EnsureFunctionPatchable:
		{
			int minPatchSize = 5;

			int guessInstBytes = 1; // ret
			guessInstBytes += mActiveFunction->getFunctionType()->getNumParams() * 4;

			if (guessInstBytes < 5)
			{
				for (auto& block : *mActiveFunction)
				{
					for (auto& inst : block)
					{
						if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(&inst))
							guessInstBytes += 2;
						else if (auto storeInst = llvm::dyn_cast<llvm::StoreInst>(&inst))
							guessInstBytes += 2;
						else if (auto callInst = llvm::dyn_cast<llvm::CallInst>(&inst))
						{
							auto calledValue = callInst->getCalledOperand();

							if (calledValue == mNopInlineAsm)
								guessInstBytes += 1;
							else if (auto func = llvm::dyn_cast<llvm::Function>(calledValue))
							{
								if (!func->isIntrinsic())
									guessInstBytes += 4;
							}
							else
								guessInstBytes += 4;
						}

						if (guessInstBytes >= minPatchSize)
							break;
					}
				}
			}

			for (int i = guessInstBytes; i < minPatchSize; i++)
				AddNop();
		}
		break;
	case BfIRCmd_RemapBindFunction:
		{
			CMD_PARAM(BfIRTypedValue, func);
			// We need to store this value to a data segment so we get a symbol we can remap during hot swap
			//  We actually do this to ensure that we don't bind to the NEW method but rather the old one- so
			//  delegate equality checks still work

			llvm::Function* llvmFunc = llvm::dyn_cast<llvm::Function>(func.mValue);

			if (llvmFunc != NULL)
			{
				// I don't know why we mixed in HSPreserveIdx - that causes bound address to change after reloading, basically totally breaking
				//  the whole point of this.
				//String funcName = StrFormat("bf_hs_preserve@%d@%s", mModule->mCompiler->mHSPreserveIdx++, func->getName());
				String funcName = StrFormat("bf_hs_preserve@%s_%s", llvmFunc->getName().data(), mLLVMModule->getName().data());
				llvm::GlobalVariable* globalVariable = mLLVMModule->getGlobalVariable(funcName.c_str());
				if (globalVariable == NULL)
				{
					globalVariable = new llvm::GlobalVariable(*mLLVMModule, llvmFunc->getType(), true, llvm::GlobalValue::ExternalLinkage, (llvm::Constant*)llvmFunc, funcName.c_str());
				}

				BfIRTypedValue result;
				result.mTypeEx = func.mTypeEx;
				result.mValue = mIRBuilder->CreateLoad(result.mTypeEx->mLLVMType, globalVariable);
				SetResult(curId, result);
			}
			else
				SetResult(curId, func);
		}
		break;
	case BfIRCmd_SetActiveFunction:
		{
			BfIRTypedValue func;
			ReadFunction(func);
			mActiveFunction = (llvm::Function*)func.mValue;
			if (mActiveFunction == NULL)
				mActiveFunctionType = NULL;
			else
				mActiveFunctionType = GetTypeMember(func.mTypeEx, 0);
		}
		break;
	case BfIRCmd_CreateCall:
		{
			BfIRTypedValue func;
			BfIRCodeGenEntry* codeGenEntry = NULL;
			Read(func, &codeGenEntry);
			CMD_PARAM(CmdParamVec<BfIRTypedValue>, args);			

			if ((func.mValue == NULL) && (codeGenEntry != NULL) && (codeGenEntry->mKind == BfIRCodeGenEntryKind_IntrinsicData))
			{
				auto intrinsicData = codeGenEntry->mIntrinsicData;

				switch (intrinsicData->mIntrinsic)
				{
				case BfIRIntrinsic__PLATFORM:
				{
					if (intrinsicData->mName == "add_string_to_section")
					{
						llvm::StringRef strContent[2];
						llvm::ConstantDataArray* dataArray;

						for (int i = 0; i < 2; i++)
						{
							if (const llvm::ConstantExpr* ce = llvm::dyn_cast<llvm::ConstantExpr>(args[i].mValue))
							{
								llvm::Value* firstOperand = ce->getOperand(0);
								if (llvm::GlobalVariable* gv = llvm::dyn_cast<llvm::GlobalVariable>(firstOperand))
								{
									if (gv->getType()->isPointerTy())
									{
										if (dataArray = llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer()))
											strContent[i] = dataArray->getAsString();
									}
								}
							}
							else
								FatalError("Value is not ConstantExpr");
						}

						static int symbolCount = 0;
						symbolCount++;

						auto charType = llvm::IntegerType::get(*mLLVMContext, 8);
						std::vector<llvm::Constant*> chars(strContent[0].size());
						for (unsigned int i = 0; i < strContent[0].size(); i++)
						{
							chars[i] = llvm::ConstantInt::get(charType, strContent[0][i]);;
						}

						chars.push_back(llvm::ConstantInt::get(charType, 0));
						auto stringType = llvm::ArrayType::get(charType, chars.size());

						std::string symbolName = strContent[1].str() + "_" + std::to_string(symbolCount);
						llvm::StringRef resultStringRef(symbolName);

						auto globalVar = (llvm::GlobalVariable*)mLLVMModule->getOrInsertGlobal(symbolName, stringType);
						globalVar->setSection(strContent[1]);
						globalVar->setInitializer(llvm::ConstantArray::get(stringType, chars));
						globalVar->setConstant(true);
						globalVar->setLinkage(llvm::GlobalValue::LinkageTypes::ExternalLinkage);
						globalVar->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

						SetResult(curId, llvm::ConstantExpr::getBitCast(globalVar, charType->getPointerTo()));
						break;
					}

					FatalError(StrFormat("Unable to find intrinsic '%s'", intrinsicData->mName.c_str()));
					break;
				}
				case BfIRIntrinsic_Add:
				case BfIRIntrinsic_And:
				case BfIRIntrinsic_Div:
				case BfIRIntrinsic_Eq:
				case BfIRIntrinsic_Gt:
				case BfIRIntrinsic_GtE:
				case BfIRIntrinsic_Lt:
				case BfIRIntrinsic_LtE:
				case BfIRIntrinsic_Mod:
				case BfIRIntrinsic_Mul:
				case BfIRIntrinsic_Neq:
				case BfIRIntrinsic_Or:
				case BfIRIntrinsic_Sub:
				case BfIRIntrinsic_Xor:
					{
						auto val0 = TryToVector(args[0]);
						if (val0 != NULL)
						{
							auto vecType = llvm::dyn_cast<llvm::VectorType>(val0->getType());
							auto elemType = vecType->getElementType();
							bool isFP = elemType->isFloatingPointTy();

							llvm::Value* val1;
							if (args.size() < 2)
							{
								llvm::Value* val;
								if (isFP)
									val = llvm::ConstantFP::get(elemType, 1);
								else
									val = llvm::ConstantInt::get(elemType, 1);
								val1 = mIRBuilder->CreateInsertElement(llvm::UndefValue::get(vecType), val, (uint64)0);
								val1 = mIRBuilder->CreateInsertElement(val1, val, (uint64)1);
								val1 = mIRBuilder->CreateInsertElement(val1, val, (uint64)2);
								val1 = mIRBuilder->CreateInsertElement(val1, val, (uint64)3);
							}
							else if (args[1].mValue->getType()->isPointerTy())
							{
								auto ptrVal1 = mIRBuilder->CreateBitCast(args[1].mValue, vecType->getPointerTo());
								val1 = mIRBuilder->CreateAlignedLoad(vecType, ptrVal1, llvm::MaybeAlign(1));
							}
							else if (args[1].mValue->getType()->isVectorTy())
							{
								val1 = args[1].mValue;
							}
							else
							{
								val1 = mIRBuilder->CreateInsertElement(llvm::UndefValue::get(vecType), args[1].mValue, (uint64)0);
								val1 = mIRBuilder->CreateInsertElement(val1, args[1].mValue, (uint64)1);
								val1 = mIRBuilder->CreateInsertElement(val1, args[1].mValue, (uint64)2);
								val1 = mIRBuilder->CreateInsertElement(val1, args[1].mValue, (uint64)3);
							}

							if (isFP)
							{
								llvm::Value* result = NULL;
								switch (intrinsicData->mIntrinsic)
								{
								case BfIRIntrinsic_Add:
									result = mIRBuilder->CreateFAdd(val0, val1);
									break;
								case BfIRIntrinsic_Div:
									result = mIRBuilder->CreateFDiv(val0, val1);
									break;
								case BfIRIntrinsic_Eq:
									result = mIRBuilder->CreateFCmpOEQ(val0, val1);
									break;
								case BfIRIntrinsic_Gt:
									result = mIRBuilder->CreateFCmpOGT(val0, val1);
									break;
								case BfIRIntrinsic_GtE:
									result = mIRBuilder->CreateFCmpOGE(val0, val1);
									break;
								case BfIRIntrinsic_Lt:
									result = mIRBuilder->CreateFCmpOLT(val0, val1);
									break;
								case BfIRIntrinsic_LtE:
									result = mIRBuilder->CreateFCmpOLE(val0, val1);
									break;
								case BfIRIntrinsic_Mod:
									result = mIRBuilder->CreateFRem(val0, val1);
									break;
								case BfIRIntrinsic_Mul:
									result = mIRBuilder->CreateFMul(val0, val1);
									break;
								case BfIRIntrinsic_Neq:
									result = mIRBuilder->CreateFCmpONE(val0, val1);
									break;
								case BfIRIntrinsic_Sub:
									result = mIRBuilder->CreateFSub(val0, val1);
									break;
								default:
									FatalError("Intrinsic argument error");
								}

								if (result != NULL)
								{
									if (auto vecType = llvm::dyn_cast<llvm::FixedVectorType>(result->getType()))
									{
										if (auto intType = llvm::dyn_cast<llvm::IntegerType>(vecType->getElementType()))
										{
											if (intType->getBitWidth() == 1)
											{
												auto toType = llvm::FixedVectorType::get(llvm::IntegerType::get(*mLLVMContext, 8), vecType->getNumElements());
												result = mIRBuilder->CreateZExt(result, toType);
											}
										}
									}

									SetResult(curId, result);
								}
							}
							else
							{
								llvm::Value* result = NULL;
								switch (intrinsicData->mIntrinsic)
								{
								case BfIRIntrinsic_And:
									result = mIRBuilder->CreateAnd(val0, val1);
									break;
								case BfIRIntrinsic_Add:
									result = mIRBuilder->CreateAdd(val0, val1);
									break;
								case BfIRIntrinsic_Div:
									result = mIRBuilder->CreateSDiv(val0, val1);
									break;
								case BfIRIntrinsic_Eq:
									result = mIRBuilder->CreateICmpEQ(val0, val1);
									break;
								case BfIRIntrinsic_Gt:
									result = mIRBuilder->CreateICmpSGT(val0, val1);
									break;
								case BfIRIntrinsic_GtE:
									result = mIRBuilder->CreateICmpSGE(val0, val1);
									break;
								case BfIRIntrinsic_Lt:
									result = mIRBuilder->CreateICmpSLT(val0, val1);
									break;
								case BfIRIntrinsic_LtE:
									result = mIRBuilder->CreateICmpSLE(val0, val1);
									break;
								case BfIRIntrinsic_Mod:
									result = mIRBuilder->CreateSRem(val0, val1);
									break;
								case BfIRIntrinsic_Mul:
									result = mIRBuilder->CreateMul(val0, val1);
									break;
								case BfIRIntrinsic_Neq:
									result = mIRBuilder->CreateICmpNE(val0, val1);
									break;
								case BfIRIntrinsic_Or:
									result = mIRBuilder->CreateOr(val0, val1);
									break;
								case BfIRIntrinsic_Sub:
									result = mIRBuilder->CreateSub(val0, val1);
									break;
								case BfIRIntrinsic_Xor:
									result = mIRBuilder->CreateXor(val0, val1);
									break;
								default:
									FatalError("Intrinsic argument error");
								}

								if (result != NULL)
								{
									if (auto vecType = llvm::dyn_cast<llvm::FixedVectorType>(result->getType()))
									{
										if (auto intType = llvm::dyn_cast<llvm::IntegerType>(vecType->getElementType()))
										{
											if (intType->getBitWidth() == 1)
											{
												auto toType = llvm::FixedVectorType::get(llvm::IntegerType::get(*mLLVMContext, 8), vecType->getNumElements());
												result = mIRBuilder->CreateZExt(result, toType);
											}
										}
									}

									SetResult(curId, result);
								}
							}
						}
						else if (auto ptrType = llvm::dyn_cast<llvm::PointerType>(args[1].mTypeEx->mLLVMType))
						{
							//auto ptrElemType = ptrType->getElementType();
							auto ptrElemType = GetLLVMPointerElementType(args[1].mTypeEx);
							if (auto arrType = llvm::dyn_cast<llvm::ArrayType>(ptrElemType))
							{
								auto vecType = llvm::FixedVectorType::get(arrType->getArrayElementType(), (uint)arrType->getArrayNumElements());
								auto vecPtrType = vecType->getPointerTo();

								llvm::Value* val0;
								val0 = mIRBuilder->CreateInsertElement(llvm::UndefValue::get(vecType), args[0].mValue, (uint64)0);
								val0 = mIRBuilder->CreateInsertElement(val0, args[0].mValue, (uint64)1);
								val0 = mIRBuilder->CreateInsertElement(val0, args[0].mValue, (uint64)2);
								val0 = mIRBuilder->CreateInsertElement(val0, args[0].mValue, (uint64)3);

								auto ptrVal1 = mIRBuilder->CreateBitCast(args[1].mValue, vecPtrType);
								auto val1 = mIRBuilder->CreateAlignedLoad(vecType, ptrVal1, llvm::MaybeAlign(1));

								switch (intrinsicData->mIntrinsic)
								{
								case BfIRIntrinsic_Div:
									SetResult(curId, mIRBuilder->CreateFDiv(val0, val1));
									break;
								case BfIRIntrinsic_Mod:
									SetResult(curId, mIRBuilder->CreateFRem(val0, val1));
									break;
								default:
									FatalError("Intrinsic argument error");
								}
							}
						}
						else
						{
							FatalError("Intrinsic argument error");
						}
					}
					break;
				case BfIRIntrinsic_Min:
				case BfIRIntrinsic_Max:
					{
						// Get arguments as vectors
						auto val0 = TryToVector(args[0]);
						if (val0 == NULL)
							FatalError("Intrinsic argument error");

						auto val1 = TryToVector(args[1]);
						if (val1 == NULL)
							FatalError("Intrinsic argument error");

						// Make sure both argument types are the same
						auto vecType = llvm::dyn_cast<llvm::VectorType>(val0->getType());
						if (vecType != llvm::dyn_cast<llvm::VectorType>(val1->getType()))
							FatalError("Intrinsic argument error");

						// Make sure the type is not scalable
						if (vecType->getElementCount().isScalable())
							FatalError("Intrinsic argument error");

						// Make sure the element type is either float or double
						auto elemType = vecType->getElementType();
						if (!elemType->isFloatTy() && !elemType->isDoubleTy())
							FatalError("Intrinsic argument error");

						// Get some properties for easier access
						bool isFloat = elemType->isFloatTy();
						bool isMin = intrinsicData->mIntrinsic == BfIRIntrinsic_Min;
						auto elemCount = vecType->getElementCount().getFixedValue();

						// Get the intrinsic function
						const char* funcName;

						if (isFloat)
						{
							if (elemCount == 4)
							{
								funcName = isMin ? "llvm.x86.sse.min.ps" : "llvm.x86.sse.max.ps";
								SetActiveFunctionSimdType(BfIRSimdType_SSE);
							}
							else if (elemCount == 8)
							{
								funcName = isMin ? "llvm.x86.avx.min.ps.256" : "llvm.x86.avx.max.ps.256";
								SetActiveFunctionSimdType(BfIRSimdType_AVX2);
							}
							else if (elemCount == 16)
							{
								funcName = isMin ? "llvm.x86.avx512.min.ps.512" : "llvm.x86.avx512.max.ps.512";
								SetActiveFunctionSimdType(BfIRSimdType_AVX512);
							}
							else
								FatalError("Intrinsic argument error");
						}
						else
						{
							if (elemCount == 2)
							{
								funcName = isMin ? "llvm.x86.sse.min.pd" : "llvm.x86.sse.max.pd";
								SetActiveFunctionSimdType(BfIRSimdType_SSE);
							}
							else if (elemCount == 4)
							{
								funcName = isMin ? "llvm.x86.avx.min.pd.256" : "llvm.x86.avx.max.pd.256";
								SetActiveFunctionSimdType(BfIRSimdType_AVX2);
							}
							else if (elemCount == 8)
							{
								funcName = isMin ? "llvm.x86.avx512.min.pd.512" : "llvm.x86.avx512.max.pd.512";
								SetActiveFunctionSimdType(BfIRSimdType_AVX512);
							}
							else
								FatalError("Intrinsic argument error");
						}

						auto func = mLLVMModule->getOrInsertFunction(funcName, vecType, vecType, vecType);

						// Call intrinsic
						llvm::SmallVector<llvm::Value*, 2> args;
						args.push_back(val0);
						args.push_back(val1);

						SetResult(curId, mIRBuilder->CreateCall(func, args));
					}
					break;
				case BfIRIntrinsic_Cpuid:
					{
						llvm::Type* elemType = llvm::Type::getInt32Ty(*mLLVMContext);

						// Check argument errors
						if (args.size() != 6 || !args[0].mValue->getType()->isIntegerTy(32) || !args[1].mValue->getType()->isIntegerTy(32))
							FatalError("Intrinsic argument error");

// 						for (int i = 2; i < 6; i++)
// 						{
// 							llvm::Type* type = args[i]->getType();
// 
// 							if (!type->isPointerTy() || !GetPointerElementType(args[1])->isIntegerTy(32))
// 								FatalError("Intrinsic argument error");
// 						}

						// Get asm return type
						llvm::SmallVector<llvm::Type*, 4> asmReturnTypes;
						asmReturnTypes.push_back(elemType);
						asmReturnTypes.push_back(elemType);
						asmReturnTypes.push_back(elemType);
						asmReturnTypes.push_back(elemType);

						llvm::Type* returnType = llvm::StructType::get(*mLLVMContext, asmReturnTypes);

						// Get asm function
						llvm::SmallVector<llvm::Type*, 2> funcParams;
						funcParams.push_back(elemType);
						funcParams.push_back(elemType);

						llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, funcParams, false);
						llvm::InlineAsm* func = llvm::InlineAsm::get(funcType, "xchgq %rbx,${1:q}\ncpuid\nxchgq %rbx,${1:q}", "={ax},=r,={cx},={dx},0,2,~{dirflag},~{fpsr},~{flags}", false);

						// Call asm function
						llvm::SmallVector<llvm::Value*, 2> funcArgs;
						funcArgs.push_back(args[0].mValue);
						funcArgs.push_back(args[1].mValue);

						llvm::Value* asmResult = mIRBuilder->CreateCall(func, funcArgs);

						// Store results
						mIRBuilder->CreateStore(mIRBuilder->CreateExtractValue(asmResult, 0), args[2].mValue);
						mIRBuilder->CreateStore(mIRBuilder->CreateExtractValue(asmResult, 1), args[3].mValue);
						mIRBuilder->CreateStore(mIRBuilder->CreateExtractValue(asmResult, 2), args[4].mValue);
						mIRBuilder->CreateStore(mIRBuilder->CreateExtractValue(asmResult, 3), args[5].mValue);
					}
					break;
				case BfIRIntrinsic_Xgetbv:
					{
						if (args.size() != 1 || !args[0].mValue->getType()->isIntegerTy(32))
							FatalError("Intrinsic argument error");

						auto func = mLLVMModule->getOrInsertFunction("llvm.x86.xgetbv", llvm::Type::getInt64Ty(*mLLVMContext), llvm::Type::getInt32Ty(*mLLVMContext));
						SetResult(curId, mIRBuilder->CreateCall(func, args[0].mValue));
					}
					break;
				case BfIRIntrinsic_Not:
					{
						auto val0 = TryToVector(args[0]);
						SetResult(curId, mIRBuilder->CreateNot(val0));
					}
					break;
				case BfIRIntrinsic_Shuffle:
					{
						llvm::SmallVector<int, 8> intMask;
						for (int i = 7; i < (int)intrinsicData->mName.length(); i++)
							intMask.push_back((int)(intrinsicData->mName[i] - '0'));

						auto val0 = TryToVector(args[0]);

						if (val0 != NULL)
						{
							SetResult(curId, mIRBuilder->CreateShuffleVector(val0, val0, intMask));
						}
						else
						{
							FatalError("Intrinsic argument error");
						}
					}
					break;
				case BfIRIntrinsic_Index:
					{
						llvm::Value* gepArgs[] = {
							llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), 0),
							args[1].mValue };
						auto gep = mIRBuilder->CreateInBoundsGEP(GetLLVMPointerElementType(args[0].mTypeEx), args[0].mValue, llvm::makeArrayRef(gepArgs));
						if (args.size() >= 3)
							mIRBuilder->CreateStore(args[2].mValue, gep);
						else
						{
							BfIRTypedValue result;
							result.mTypeEx = GetTypeMember(args[0].mTypeEx, 0);
							result.mValue = mIRBuilder->CreateLoad(result.mTypeEx->mLLVMType, gep);
							SetResult(curId, result);
						}
					}
					break;

				case BfIRIntrinsic_AtomicCmpStore:
				case BfIRIntrinsic_AtomicCmpStore_Weak:
				case BfIRIntrinsic_AtomicCmpXChg:
					{
						auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[3].mValue);
						if (memoryKindConst == NULL)
						{
							FatalError("Non-constant success ordering on Atomic_CmpXChg");
							break;
						}
						auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();

						auto successOrdering = llvm::AtomicOrdering::Unordered;
						auto failOrdering = llvm::AtomicOrdering::Unordered;
						switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
						{
						case BfIRAtomicOrdering_Acquire:
							successOrdering = llvm::AtomicOrdering::Acquire;
							failOrdering = llvm::AtomicOrdering::Acquire;
							break;
						case BfIRAtomicOrdering_AcqRel:
							successOrdering = llvm::AtomicOrdering::AcquireRelease;
							failOrdering = llvm::AtomicOrdering::Acquire;
							break;
						case BfIRAtomicOrdering_Relaxed:
							successOrdering = llvm::AtomicOrdering::Monotonic;
							failOrdering = llvm::AtomicOrdering::Monotonic;
							break;
						case BfIRAtomicOrdering_Release:
							successOrdering = llvm::AtomicOrdering::Release;
							failOrdering = llvm::AtomicOrdering::Monotonic;
							break;
						case BfIRAtomicOrdering_SeqCst:
							successOrdering = llvm::AtomicOrdering::SequentiallyConsistent;
							failOrdering = llvm::AtomicOrdering::SequentiallyConsistent;
							break;
						default:
							Fail("Invalid success ordering on Atomic_CmpXChg");
							break;
						}

						if (args.size() >= 5)
						{
							auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[4].mValue);
							if (memoryKindConst == NULL)
							{
								FatalError("Non-constant fail ordering on Atomic_CmpXChg");
								break;
							}
							auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();
							switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
							{
							case BfIRAtomicOrdering_Acquire:
								failOrdering = llvm::AtomicOrdering::Acquire;
								break;
							case BfIRAtomicOrdering_Relaxed:
								failOrdering = llvm::AtomicOrdering::Monotonic;
								break;
							case BfIRAtomicOrdering_SeqCst:
								failOrdering = llvm::AtomicOrdering::SequentiallyConsistent;
								break;
							default:
								FatalError("Invalid fail ordering on Atomic_CmpXChg");
								break;
							}
						}

						auto inst = mIRBuilder->CreateAtomicCmpXchg(args[0].mValue, args[1].mValue, args[2].mValue, llvm::MaybeAlign(), successOrdering, failOrdering);
						if (intrinsicData->mIntrinsic == BfIRIntrinsic_AtomicCmpStore_Weak)
							inst->setWeak(true);
						if ((memoryKind & BfIRAtomicOrdering_Volatile) != 0)
							inst->setVolatile(true);
						if (intrinsicData->mIntrinsic == BfIRIntrinsic_AtomicCmpXChg)
						{
							auto prevVal = mIRBuilder->CreateExtractValue(inst, 0);
							SetResult(curId, prevVal);
						}
						else
						{
							auto successVal = mIRBuilder->CreateExtractValue(inst, 1);
							SetResult(curId, successVal);
						}
					}
					break;
				case BfIRIntrinsic_AtomicFence:
					{
						if (args.size() == 0)
						{
							if ((mTargetTriple.GetMachineType() != BfMachineType_x86) && (mTargetTriple.GetMachineType() != BfMachineType_x64))
							{
								Fail("Unable to create compiler barrier on this platform");
							}
							else
							{
								// Compiler barrier

								llvm::SmallVector<llvm::Type*, 8> paramTypes;
								llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getVoidTy(*mLLVMContext), paramTypes, false);
								auto fenceFunc = llvm::InlineAsm::get(funcType,
									"", "~{memory},~{dirflag},~{fpsr},~{flags}", true, false, llvm::InlineAsm::AD_ATT);
								mIRBuilder->CreateCall(fenceFunc);
							}
							break;
						}

						auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[0].mValue);
						if (memoryKindConst == NULL)
						{
							FatalError("Non-constant success ordering on AtomicFence");
							break;
						}

						auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();
						auto ordering = llvm::AtomicOrdering::SequentiallyConsistent;
						switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
						{
						case BfIRAtomicOrdering_Acquire:
							ordering = llvm::AtomicOrdering::Acquire;
							break;
						case BfIRAtomicOrdering_AcqRel:
							ordering = llvm::AtomicOrdering::AcquireRelease;
							break;
						case BfIRAtomicOrdering_Release:
							ordering = llvm::AtomicOrdering::Release;
							break;
						case BfIRAtomicOrdering_SeqCst:
							ordering = llvm::AtomicOrdering::SequentiallyConsistent;
							break;
						default:
							Fail("Invalid ordering on atomic operation");
							break;
						}

						mIRBuilder->CreateFence(ordering);
					}
					break;
				case BfIRIntrinsic_AtomicLoad:
					{
						auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[1].mValue);
						if (memoryKindConst == NULL)
						{
							FatalError("Non-constant success ordering on AtomicLoad");
							break;
						}

						BfIRTypedValue result;
						result.mTypeEx = GetTypeMember(args[0].mTypeEx, 0);

						auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();
						auto ptrType = llvm::dyn_cast<llvm::PointerType>(args[0].mValue->getType());
						auto loadInst = mIRBuilder->CreateAlignedLoad(result.mTypeEx->mLLVMType, args[0].mValue, llvm::MaybeAlign((uint)GetLLVMPointerElementType(args[0].mTypeEx)->getPrimitiveSizeInBits() / 8));
						switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
						{
						case BfIRAtomicOrdering_Acquire:
							loadInst->setAtomic(llvm::AtomicOrdering::Acquire);
							break;
						case BfIRAtomicOrdering_Relaxed:
							loadInst->setAtomic(llvm::AtomicOrdering::Monotonic);
							break;
						case BfIRAtomicOrdering_SeqCst:
							loadInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
							break;
						default:
							BF_FATAL("BadAtomic");
						}
						if ((memoryKind & BfIRAtomicOrdering_Volatile) != 0)
							loadInst->setVolatile(true);
						result.mValue = loadInst;

						SetResult(curId, result);
					}
					break;
				case BfIRIntrinsic_AtomicStore:
					{
						auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[2].mValue);
						if (memoryKindConst == NULL)
						{
							FatalError("Non-constant success ordering on AtomicLoad");
							break;
						}
						auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();

						auto storeInst = mIRBuilder->CreateAlignedStore(args[1].mValue, args[0].mValue, llvm::MaybeAlign((uint)args[1].mValue->getType()->getPrimitiveSizeInBits() / 8));
						switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
						{
						case BfIRAtomicOrdering_Relaxed:
							storeInst->setAtomic(llvm::AtomicOrdering::Monotonic);
							break;
						case BfIRAtomicOrdering_Release:
							storeInst->setAtomic(llvm::AtomicOrdering::Release);
							break;
						case BfIRAtomicOrdering_SeqCst:
							storeInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
							break;
						}
						if ((memoryKind & BfIRAtomicOrdering_Volatile) != 0)
							storeInst->setVolatile(true);
						SetResult(curId, storeInst);
					}
					break;
				case BfIRIntrinsic_AtomicAdd:
				case BfIRIntrinsic_AtomicAnd:
				case BfIRIntrinsic_AtomicMax:
				case BfIRIntrinsic_AtomicMin:
				case BfIRIntrinsic_AtomicNAnd:
				case BfIRIntrinsic_AtomicOr:
				case BfIRIntrinsic_AtomicSub:
				case BfIRIntrinsic_AtomicUMax:
				case BfIRIntrinsic_AtomicUMin:
				case BfIRIntrinsic_AtomicXChg:
				case BfIRIntrinsic_AtomicXor:
					{
						bool isFloat = args[1].mValue->getType()->isFloatingPointTy();

						auto op = llvm::AtomicRMWInst::BinOp::Add;
						switch (intrinsicData->mIntrinsic)
						{
						case BfIRIntrinsic_AtomicAdd:
							op = llvm::AtomicRMWInst::BinOp::Add;
							break;
						case BfIRIntrinsic_AtomicAnd:
							op = llvm::AtomicRMWInst::BinOp::And;
							break;
						case BfIRIntrinsic_AtomicMax:
							op = llvm::AtomicRMWInst::BinOp::Max;
							break;
						case BfIRIntrinsic_AtomicMin:
							op = llvm::AtomicRMWInst::BinOp::Min;
							break;
						case BfIRIntrinsic_AtomicNAnd:
							op = llvm::AtomicRMWInst::BinOp::Nand;
							break;
						case BfIRIntrinsic_AtomicOr:
							op = llvm::AtomicRMWInst::BinOp::Or;
							break;
						case BfIRIntrinsic_AtomicSub:
							op = llvm::AtomicRMWInst::BinOp::Sub;
							break;
						case BfIRIntrinsic_AtomicUMax:
							op = llvm::AtomicRMWInst::BinOp::UMax;
							break;
						case BfIRIntrinsic_AtomicUMin:
							op = llvm::AtomicRMWInst::BinOp::UMin;
							break;
						case BfIRIntrinsic_AtomicXChg:
							op = llvm::AtomicRMWInst::BinOp::Xchg;
							break;
						case BfIRIntrinsic_AtomicXor:
							op = llvm::AtomicRMWInst::BinOp::Xor;
							break;
						default: break;
						}

						auto memoryKindConst = llvm::dyn_cast<llvm::ConstantInt>(args[2].mValue);
						if (memoryKindConst == NULL)
						{
							FatalError("Non-constant ordering on atomic operation");
							break;
						}

						auto memoryKind = (BfIRAtomicOrdering)memoryKindConst->getSExtValue();
						auto ordering = llvm::AtomicOrdering::Unordered;
						switch (memoryKind & BfIRAtomicOrdering_ORDERMASK)
						{
						case BfIRAtomicOrdering_Acquire:
							ordering = llvm::AtomicOrdering::Acquire;
							break;
						case BfIRAtomicOrdering_AcqRel:
							ordering = llvm::AtomicOrdering::AcquireRelease;
							break;
						case BfIRAtomicOrdering_Relaxed:
							ordering = llvm::AtomicOrdering::Monotonic;
							break;
						case BfIRAtomicOrdering_Release:
							ordering = llvm::AtomicOrdering::Release;
							break;
						case BfIRAtomicOrdering_SeqCst:
							ordering = llvm::AtomicOrdering::SequentiallyConsistent;
							break;
						default:
							Fail("Invalid ordering on atomic operation");
							break;
						}

						auto atomicRMW = mIRBuilder->CreateAtomicRMW(op, args[0].mValue, args[1].mValue, llvm::MaybeAlign(), ordering);
						if ((memoryKind & BfIRAtomicOrdering_Volatile) != 0)
							atomicRMW->setVolatile(true);
						llvm::Value* result = atomicRMW;
						if ((memoryKind & BfIRAtomicOrdering_ReturnModified) != 0)
						{
							switch (intrinsicData->mIntrinsic)
							{
							case BfIRIntrinsic_AtomicAdd:
								if (isFloat)
									result = mIRBuilder->CreateFAdd(atomicRMW, args[1].mValue);
								else
									result = mIRBuilder->CreateAdd(atomicRMW, args[1].mValue);
								break;
							case BfIRIntrinsic_AtomicAnd:
								result = mIRBuilder->CreateAnd(atomicRMW, args[1].mValue);
								break;
							case BfIRIntrinsic_AtomicMax:
							case BfIRIntrinsic_AtomicMin:
							case BfIRIntrinsic_AtomicUMax:
							case BfIRIntrinsic_AtomicUMin:
								{
									llvm::Value* cmpVal = NULL;
									switch (intrinsicData->mIntrinsic)
									{
									case BfIRIntrinsic_AtomicMax:
										if (isFloat)
											cmpVal = mIRBuilder->CreateFCmpOGE(atomicRMW, args[1].mValue);
										else
											cmpVal = mIRBuilder->CreateICmpSGE(atomicRMW, args[1].mValue);
										break;
									case BfIRIntrinsic_AtomicMin:
										if (isFloat)
											cmpVal = mIRBuilder->CreateFCmpOLE(atomicRMW, args[1].mValue);
										else
											cmpVal = mIRBuilder->CreateICmpSLE(atomicRMW, args[1].mValue);
										break;
									case BfIRIntrinsic_AtomicUMax:
										cmpVal = mIRBuilder->CreateICmpUGE(atomicRMW, args[1].mValue);
										break;
									case BfIRIntrinsic_AtomicUMin:
										cmpVal = mIRBuilder->CreateICmpULE(atomicRMW, args[1].mValue);
										break;
									default: break;
									}
									result = mIRBuilder->CreateSelect(cmpVal, atomicRMW, args[1].mValue);
								}
								break;
							case BfIRIntrinsic_AtomicNAnd:
								result = mIRBuilder->CreateAnd(atomicRMW, args[1].mValue);
								result = mIRBuilder->CreateNot(result);
								break;
							case BfIRIntrinsic_AtomicOr:
								result = mIRBuilder->CreateOr(atomicRMW, args[1].mValue);
								break;
							case BfIRIntrinsic_AtomicSub:
								if (isFloat)
									result = mIRBuilder->CreateFSub(atomicRMW, args[1].mValue);
								else
									result = mIRBuilder->CreateSub(atomicRMW, args[1].mValue);
								break;
							case BfIRIntrinsic_AtomicXor:
								result = mIRBuilder->CreateXor(atomicRMW, args[1].mValue);
								break;
							case BfIRIntrinsic_AtomicXChg:
								result = args[1].mValue;
								break;
							default: break;
							}
						}
						SetResult(curId, result);
					}
					break;
				case BfIRIntrinsic_Cast:
					{
						BfIRTypedValue result;
						result.mTypeEx = intrinsicData->mReturnType;

						auto arg0Type = args[0].mValue->getType();
						if (arg0Type->isPointerTy())
						{
							if (intrinsicData->mReturnType->mLLVMType->isPointerTy())
							{
								result.mValue = mIRBuilder->CreateBitCast(args[0].mValue, intrinsicData->mReturnType->mLLVMType);
							}
							else
							{
								auto castedRes = mIRBuilder->CreateBitCast(args[0].mValue, intrinsicData->mReturnType->mLLVMType->getPointerTo());
								result.mValue = mIRBuilder->CreateAlignedLoad(intrinsicData->mReturnType->mLLVMType, castedRes, llvm::MaybeAlign(1));
							}
						}
						else if ((arg0Type->isVectorTy()) && (intrinsicData->mReturnType->mLLVMType->isVectorTy()))
						{
							result.mValue = mIRBuilder->CreateBitCast(args[0].mValue, intrinsicData->mReturnType->mLLVMType);
						}
						else
							FatalError("Invalid cast intrinsic values");

						SetResult(curId, result);
					}
					break;
				case BfIRIntrinsic_VAArg:
					{
						auto constInt = llvm::dyn_cast<llvm::ConstantInt>(args[2].mValue);
						auto argType = GetLLVMTypeById((int)constInt->getSExtValue());
						auto vaArgVal = mIRBuilder->CreateVAArg(args[0].mValue, argType);

						auto resultPtr = mIRBuilder->CreateBitCast(args[1].mValue, argType->getPointerTo());
						mIRBuilder->CreateStore(vaArgVal, resultPtr);
					}
					break;
				default:
					FatalError("Unhandled intrinsic");
				}
				break;
			}

			if (auto funcPtr = llvm::dyn_cast<llvm::Function>(func.mValue))
			{
// 				if (funcPtr->getName() == "__FAILCALL")
// 				{
// 					FatalError("__FAILCALL");
// 				}

				int intrinId = -1;
				if (mIntrinsicReverseMap.TryGetValue(funcPtr, &intrinId))
				{
					if (intrinId == BfIRIntrinsic_MemSet)
					{
						int align = 1;

						BF_ASSERT(args.size() == 5);
						auto alignConst = llvm::dyn_cast<llvm::ConstantInt>(args[3].mValue);
						if (alignConst != NULL)
							align = (int)alignConst->getSExtValue();
						bool isVolatile = false;
						auto volatileConst = llvm::dyn_cast<llvm::ConstantInt>(args[4].mValue);
						if ((volatileConst != NULL) && (volatileConst->getSExtValue() != 0))
							isVolatile = true;
						CreateMemSet(args[0].mValue, args[1].mValue, args[2].mValue, align, isVolatile);
						break;
					}
					else if ((intrinId == BfIRIntrinsic_MemCpy) || (intrinId == BfIRIntrinsic_MemMove))
					{
						int align = 1;

						BF_ASSERT(args.size() == 5);
						auto alignConst = llvm::dyn_cast<llvm::ConstantInt>(args[3].mValue);
						if (alignConst != NULL)
							align = (int)alignConst->getSExtValue();
						bool isVolatile = false;
						auto volatileConst = llvm::dyn_cast<llvm::ConstantInt>(args[4].mValue);
						if ((volatileConst != NULL) && (volatileConst->getSExtValue() != 0))
							isVolatile = true;
						if (intrinId == BfIRIntrinsic_MemCpy)
							mIRBuilder->CreateMemCpy(args[0].mValue, llvm::MaybeAlign(align), args[1].mValue, llvm::MaybeAlign(align), args[2].mValue, isVolatile);
						else
							mIRBuilder->CreateMemMove(args[0].mValue, llvm::MaybeAlign(align), args[1].mValue, llvm::MaybeAlign(align), args[2].mValue, isVolatile);
						break;
					}
				}
			}

            llvm::Value* val0 = NULL;
            llvm::Value* val1 = NULL;
            if (args.size() > 0)
            {
                val0 = args[0].mValue;
            }
            if (args.size() > 1)
            {
                val1 = args[1].mValue;
            }

			llvm::FunctionType* funcType = NULL;
			if (auto ptrType = llvm::dyn_cast<llvm::PointerType>(func.mValue->getType()))
				funcType = llvm::dyn_cast<llvm::FunctionType>(GetLLVMPointerElementType(func.mTypeEx));

			CmdParamVec<llvm::Value*> llvmArgs;
			for (auto& arg : args)
				llvmArgs.push_back(arg.mValue);

			auto funcTypeEx = GetTypeMember(func.mTypeEx, 0);
			auto returnTypeEx = GetTypeMember(funcTypeEx, 0);
			
			BfIRTypedValue result;
			result.mTypeEx = returnTypeEx;
			result.mValue = mIRBuilder->CreateCall(funcType, func.mValue, llvmArgs);
			SetResult(curId, result);

			mLastFuncCalled.mValue = result.mValue;
			mLastFuncCalled.mTypeEx = funcTypeEx;
		}
		break;
	case BfIRCmd_SetCallCallingConv:
		{
			CMD_PARAM(llvm::Value*, callInst);
			BfIRCallingConv callingConv = (BfIRCallingConv)mStream->Read();
			BF_ASSERT(llvm::isa<llvm::CallInst>(callInst));
			((llvm::CallInst*)callInst)->setCallingConv(GetLLVMCallingConv(callingConv, mTargetTriple));
		}
		break;
	case BfIRCmd_SetFuncCallingConv:
		{
			CMD_PARAM(llvm::Function*, func);
			BfIRCallingConv callingConv = (BfIRCallingConv)mStream->Read();
			((llvm::Function*)func)->setCallingConv(GetLLVMCallingConv(callingConv, mTargetTriple));
		}
		break;
	case BfIRCmd_SetTailCall:
		{
			CMD_PARAM(llvm::Value*, callInst);
			BF_ASSERT(llvm::isa<llvm::CallInst>(callInst));
			((llvm::CallInst*)callInst)->setTailCall();
		}
		break;
	case BfIRCmd_SetCallAttribute:
		{
			CMD_PARAM(llvm::Value*, callInst);
			CMD_PARAM(int, paramIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			BF_ASSERT(llvm::isa<llvm::CallInst>(callInst));
			llvm::Attribute::AttrKind attr = llvm::Attribute::None;
			if (attribute == BfIRAttribute_NoReturn)
				attr = llvm::Attribute::NoReturn;
			((llvm::CallInst*)callInst)->addParamAttr(paramIdx, attr);
		}
		break;
	case BfIRCmd_CreateRet:
		{
			CMD_PARAM(llvm::Value*, val);
			SetResult(curId, mIRBuilder->CreateRet(val));
		}
		break;
	case BfIRCmd_CreateRetVoid:
		{
			mIRBuilder->CreateRetVoid();
		}
		break;
	case BfIRCmd_CreateUnreachable:
		{
			mIRBuilder->CreateUnreachable();
		}
		break;
	case BfIRCmd_Call_AddAttribute:
		{
			CMD_PARAM(BfIRTypedValue, inst);
			CMD_PARAM(int, argIdx);

			BF_ASSERT(inst.mValue == mLastFuncCalled.mValue);

			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();			
			auto attr = LLVMMapAttribute(attribute);
			auto callInst = llvm::dyn_cast<llvm::CallInst>(inst.mValue);
			BfIRTypeEx* funcType = mLastFuncCalled.mTypeEx;

			if (attr == llvm::Attribute::StructRet)
			{
				auto elemPtrType = GetTypeMember(funcType, argIdx);
				auto elemType = GetTypeMember(elemPtrType, 0);
				llvm::Attribute sret = llvm::Attribute::getWithStructRetType(*mLLVMContext, elemType->mLLVMType);
 				((llvm::CallInst*)callInst)->addParamAttr(argIdx - 1, sret);
			}
			else
			{
				if (argIdx == -1)
					((llvm::CallInst*)callInst)->addFnAttr(attr);
				else if (argIdx == 0)
					((llvm::CallInst*)callInst)->addRetAttr(attr);
				else
					((llvm::CallInst*)callInst)->addParamAttr(argIdx - 1, attr);
			}
		}
		break;
	case BfIRCmd_Call_AddAttribute1:
		{
			CMD_PARAM(BfIRTypedValue, inst);
			CMD_PARAM(int, argIdx);
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			CMD_PARAM(int, arg);

			BF_ASSERT(inst.mValue == mLastFuncCalled.mValue);

			auto callInst = llvm::dyn_cast<llvm::CallInst>(inst.mValue);
			if (callInst != NULL)
			{
				BfIRTypeEx* funcType = mLastFuncCalled.mTypeEx;

				if (attribute == BfIRAttribute_Dereferencable)
				{					
					((llvm::CallInst*)callInst)->addDereferenceableParamAttr(argIdx - 1, arg);
				}
				else if (attribute == BfIRAttribute_ByVal)
				{
					auto elemPtrType = GetTypeMember(funcType, argIdx);
					auto elemType = GetTypeMember(elemPtrType, 0);
					llvm::Attribute byValAttr = llvm::Attribute::getWithByValType(*mLLVMContext, elemType->mLLVMType);
					llvm::Attribute alignAttr = llvm::Attribute::getWithAlignment(*mLLVMContext, llvm::Align(arg));
					((llvm::CallInst*)callInst)->addParamAttr(argIdx - 1, byValAttr);
					((llvm::CallInst*)callInst)->addParamAttr(argIdx - 1, alignAttr);
				}
			}
		}
		break;
	case BfIRCmd_Func_AddAttribute:
		{
			BfIRTypedValue typedValue;
			ReadFunction(typedValue);
			CMD_PARAM(int, argIdx);			

			auto func = llvm::dyn_cast<llvm::Function>(typedValue.mValue);
			auto funcType = GetTypeMember(typedValue.mTypeEx, 0);

			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			if (attribute == BFIRAttribute_DllImport)
			{
				func->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
			}
			else if (attribute == BFIRAttribute_DllExport)
			{
				func->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
				mHadDLLExport = true;
			}
			else if (attribute == BFIRAttribute_NoFramePointerElim)
			{
				func->addFnAttr("no-frame-pointer-elim", "true");
			}
			else if ((attribute == BFIRAttribute_Constructor) || (attribute == BFIRAttribute_Destructor))
			{
				CmdParamVec<llvm::Type*> members;
				members.push_back(llvm::Type::getInt32Ty(*mLLVMContext));
				members.push_back(func->getType());
				members.push_back(llvm::PointerType::get(*mLLVMContext, 0));
				llvm::StructType* structType = llvm::StructType::get(*mLLVMContext, members);

				CmdParamVec<llvm::Constant*> structVals;
				structVals.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), 0x7FFFFF00));
				structVals.push_back(func);
				structVals.push_back(llvm::ConstantPointerNull::get(llvm::PointerType::get(*mLLVMContext, 0)));
				auto constStruct = llvm::ConstantStruct::get(structType, structVals);

				CmdParamVec<llvm::Constant*> structArrVals;
				structArrVals.push_back(constStruct);

				auto arrTy = llvm::ArrayType::get(structType, 1);
				auto constArr = llvm::ConstantArray::get(arrTy, structArrVals);

				auto globalVariable = new llvm::GlobalVariable(
					*mLLVMModule,
					arrTy,
					false,
					llvm::GlobalValue::AppendingLinkage,
					constArr,
					(attribute == BFIRAttribute_Constructor) ? "llvm.global_ctors" : "llvm.global_dtors",
					NULL, llvm::GlobalValue::NotThreadLocal);
			}
			else
			{
				auto attr = LLVMMapAttribute(attribute);
				if (attr == llvm::Attribute::StructRet)
				{					
					auto elemPtrType = GetTypeMember(funcType, argIdx);
					auto elemType = GetTypeMember(elemPtrType, 0);
					llvm::Attribute sret = llvm::Attribute::getWithStructRetType(*mLLVMContext, elemType->mLLVMType);
					func->addParamAttr(argIdx - 1, sret);
				}
				else if (attr != llvm::Attribute::None)
				{
					if (argIdx < 0)
					{
						switch (attr)
						{
						case llvm::Attribute::UWTable:
							{
								llvm::AttrBuilder attrBuilder(*mLLVMContext);
								attrBuilder.addUWTableAttr(llvm::UWTableKind::Default);
								func->addFnAttrs(attrBuilder);
							}
							break;
						default:
							func->addFnAttr(attr);
						}						
					}
					else if (argIdx == 0)
						func->addRetAttr(attr);
					else
						func->addParamAttr(argIdx - 1, attr);
				}
			}
		}
		break;
	case BfIRCmd_Func_AddAttribute1:
		{
			BfIRTypedValue typedValue;
			ReadFunction(typedValue);
			CMD_PARAM(int, argIdx);

			auto func = llvm::dyn_cast<llvm::Function>(typedValue.mValue);
			auto funcType = GetTypeMember(typedValue.mTypeEx, 0);
		
			BfIRAttribute attribute = (BfIRAttribute)mStream->Read();
			CMD_PARAM(int, arg);
			if (attribute == BfIRAttribute_Dereferencable)
			{
				((llvm::Function*)func)->addDereferenceableParamAttr(argIdx - 1, arg);
			}
			else if (attribute == BfIRAttribute_ByVal)
			{
				auto elemPtrType = GetTypeMember(funcType, argIdx);
				auto elemType = GetTypeMember(elemPtrType, 0);

				auto funcType = func->getFunctionType();
				llvm::Attribute byValAttr = llvm::Attribute::getWithByValType(*mLLVMContext, elemType->mLLVMType);
				llvm::Attribute alignAttr = llvm::Attribute::getWithAlignment(*mLLVMContext, llvm::Align(arg));
				func->addParamAttr(argIdx - 1, byValAttr);
				func->addParamAttr(argIdx - 1, alignAttr);
			}
		}
		break;
	case BfIRCmd_Func_SetParamName:
		{
			CMD_PARAM(llvm::Function*, func);
			CMD_PARAM(int, argIdx);
			CMD_PARAM(String, name);

			if (argIdx > func->arg_size())
			{
				Fail("BfIRCmd_Func_SetParamName argIdx error");
				break;
			}

			auto argItr = func->arg_begin();
			for (int i = 1; i < argIdx; i++)
				++argItr;
			argItr->setName(name.c_str());
		}
		break;
	case BfIRCmd_Func_DeleteBody:
		{
			CMD_PARAM(llvm::Function*, func);
			BF_ASSERT(llvm::isa<llvm::Function>(func));
			((llvm::Function*)func)->deleteBody();
		}
		break;
	case BfIRCmd_Func_SafeRename:
		{
			CMD_PARAM(llvm::Function*, func);
			func->setName(llvm::Twine((Beefy::String(func->getName().data()) + StrFormat("__RENAME%d", curId)).c_str()));
		}
		break;
	case BfIRCmd_Func_SafeRenameFrom:
		{
			CMD_PARAM(llvm::Function*, func);
			CMD_PARAM(String, prevName);
			if (String(func->getName().data()) == prevName)
				func->setName(llvm::Twine((Beefy::String(func->getName().data()) + StrFormat("__RENAME%d", curId)).c_str()));
		}
		break;
	case BfIRCmd_Func_SetLinkage:
		{
			CMD_PARAM(llvm::Function*, func);
			BfIRLinkageType linkageType = (BfIRLinkageType)mStream->Read();
			((llvm::Function*)func)->setLinkage(LLVMMapLinkageType(linkageType));
		}
		break;
	case BfIRCmd_SaveDebugLocation:
		{
			mSavedDebugLocs.push_back(mIRBuilder->getCurrentDebugLocation());
		}
		break;
	case BfIRCmd_RestoreDebugLocation:
		{
			mDebugLoc = mSavedDebugLocs[mSavedDebugLocs.size() - 1];
			mIRBuilder->SetCurrentDebugLocation(mDebugLoc);
			mSavedDebugLocs.pop_back();
		}
		break;
	case BfIRCmd_DupDebugLocation:
		break;
	case BfIRCmd_ClearDebugLocation:
		{
			mDebugLoc = llvm::DebugLoc();
			mIRBuilder->SetCurrentDebugLocation(llvm::DebugLoc());
		}
		break;
	case BfIRCmd_ClearDebugLocationInst:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, instValue);
			BF_ASSERT(llvm::isa<llvm::Instruction>(instValue));

			if (llvm::dyn_cast<llvm::DbgDeclareInst>(instValue))
			{
				printf("BfIRCmd_ClearDebugLocationInst on DbgDeclareInst in %s\n", mModuleName.c_str());
			}
			else
			{
				((llvm::Instruction*)instValue)->setDebugLoc(llvm::DebugLoc());
			}
		}
		break;
	case BfIRCmd_ClearDebugLocationInstLast:
		{
			llvm::BasicBlock* bb = mIRBuilder->GetInsertBlock();
			if (bb != NULL)
			{
				if (!bb->empty())
				{
					auto& inst = bb->back();
					if (llvm::dyn_cast<llvm::DbgDeclareInst>(&inst))
					{
						printf("BfIRCmd_ClearDebugLocationInstLast on DbgDeclareInst\n");
					}
					else
					{
						inst.setDebugLoc(llvm::DebugLoc());
					}					
				}
			}
		}
		break;
	case BfIRCmd_UpdateDebugLocation:
		{
			CMD_PARAM_NOTRANS(llvm::Value*, instValue);
			BF_ASSERT(llvm::isa<llvm::Instruction>(instValue));
			if ((llvm::dyn_cast<llvm::DbgDeclareInst>(instValue)) && (!mIRBuilder->getCurrentDebugLocation()))
			{
				printf("BfIRCmd_UpdateDebugLocation NULL on DbgDeclareInst\n");
			}
			else
			{
				((llvm::Instruction*)instValue)->setDebugLoc(mIRBuilder->getCurrentDebugLocation());
			}
		}
		break;
	case BfIRCmd_SetCurrentDebugLocation:
		{
			CMD_PARAM(int, line);
			CMD_PARAM(int, column);
			CMD_PARAM(llvm::MDNode*, diScope);
			CMD_PARAM(llvm::MDNode*, diInlinedAt);
			if (line == 0)
				column = 0;
			mCurLine = line;
			mDebugLoc = llvm::DILocation::get(*mLLVMContext, line, column, diScope, diInlinedAt);

#ifdef _DEBUG
			llvm::DILocation* DL = mDebugLoc;
			if (DL != NULL)
			{
				llvm::Metadata* Parent = DL->getRawScope();
				llvm::DILocalScope* Scope = DL->getInlinedAtScope();
				llvm::DISubprogram* SP = Scope->getSubprogram();
				if (SP != NULL)
				{
					BF_ASSERT(SP->describes(mActiveFunction));
				}
			}
#endif
		}
		break;
	case BfIRCmd_Nop:
	case BfIRCmd_EnsureInstructionAt:
		AddNop();
		break;
	case BfIRCmd_StatementStart:
		// We only commit the debug loc for statement starts
		mIRBuilder->SetCurrentDebugLocation(mDebugLoc);
		mHasDebugLoc = true;
		break;
	case BfIRCmd_ObjectAccessCheck:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(bool, useAsm);
			auto curLLVMFunc = mActiveFunction;
			auto irBuilder = mIRBuilder;

			if ((mTargetTriple.GetMachineType() != BfMachineType_x86) && (mTargetTriple.GetMachineType() != BfMachineType_x64))
				useAsm = false;

			if (!useAsm)
			{
				mLockedBlocks.Add(irBuilder->GetInsertBlock());

				// This is generates slower code than the inline asm in debug mode, but can optimize well in release
				auto int8Ty = llvm::Type::getInt8Ty(*mLLVMContext);
				auto int8Ptr = irBuilder->CreateBitCast(val, int8Ty->getPointerTo());
				auto int8Val = irBuilder->CreateLoad(int8Ty, int8Ptr);
				auto cmpResult = irBuilder->CreateICmpUGE(int8Val, llvm::ConstantInt::get(int8Ty, 0x80));

				auto failBB = llvm::BasicBlock::Create(*mLLVMContext, "access.fail");
				auto passBB = llvm::BasicBlock::Create(*mLLVMContext, "access.pass");

				irBuilder->CreateCondBr(cmpResult, failBB, passBB);

				curLLVMFunc->insert(curLLVMFunc->end(), failBB);
				irBuilder->SetInsertPoint(failBB);

				auto trapDecl = llvm::Intrinsic::getDeclaration(mLLVMModule, llvm::Intrinsic::trap);
				auto callInst = irBuilder->CreateCall(trapDecl);
				callInst->addFnAttr(llvm::Attribute::NoReturn);
				irBuilder->CreateBr(passBB);

				curLLVMFunc->insert(curLLVMFunc->end(), passBB);
				irBuilder->SetInsertPoint(passBB);

				SetResult(curId, passBB);
			}
			else
			{
				llvm::Type* voidPtrType = llvm::PointerType::get(*mLLVMContext, 0);
				if (mObjectCheckAsm == NULL)
				{
					std::vector<llvm::Type*> paramTypes;
					paramTypes.push_back(voidPtrType);
					auto funcType = llvm::FunctionType::get(llvm::Type::getVoidTy(*mLLVMContext), paramTypes, false);

					String asmStr =
						"cmpb $$128, ($0)\n"
						"jb 1f\n"
						"int $$3\n"
						"1:";

					mObjectCheckAsm = llvm::InlineAsm::get(funcType,
						asmStr.c_str(), "r,~{dirflag},~{fpsr},~{flags}", true,
						false, llvm::InlineAsm::AD_ATT);
				}

				llvm::SmallVector<llvm::Value*, 1> llvmArgs;
				llvmArgs.push_back(mIRBuilder->CreateBitCast(val, voidPtrType));

				llvm::CallInst* callInst = irBuilder->CreateCall(mObjectCheckAsm, llvmArgs);
				callInst->addFnAttr(llvm::Attribute::NoUnwind);

				SetResult(curId, mIRBuilder->GetInsertBlock());
			}
		}
		break;
	case BfIRCmd_DbgInit:
		{
			mDIBuilder = new llvm::DIBuilder(*mLLVMModule);
		}
		break;
	case BfIRCmd_DbgFinalize:
		{
			for (auto& typeEntryPair : mTypes)
			{
				auto& typeEntry = typeEntryPair.mValue;
				if (typeEntry.mInstDIType != NULL)
					typeEntry.mInstDIType->resolveCycles();
			}
			mDIBuilder->finalize();
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

			auto diFile = mDIBuilder->createFile(fileName.c_str(), directory.c_str());
			mDICompileUnit = mDIBuilder->createCompileUnit(lang, diFile, producer.c_str(), isOptimized, flags.c_str(), runtimeVer, "", linesOnly ? llvm::DICompileUnit::LineTablesOnly : llvm::DICompileUnit::FullDebug);
			SetResult(curId, mDICompileUnit);
		}
		break;
	case BfIRCmd_DbgCreateFile:
		{
			CMD_PARAM(String, fileName);
			CMD_PARAM(String, directory);
			CMD_PARAM(Val128, md5Hash);

			char hashStr[64];
			for (int i = 0; i < 16; i++)
				sprintf(&hashStr[i * 2], "%.2x", ((uint8*)&md5Hash)[i]);

			SetResult(curId, mDIBuilder->createFile(fileName.c_str(), directory.c_str(),
				llvm::DIFile::ChecksumInfo<llvm::StringRef>(llvm::DIFile::CSK_MD5, hashStr)));
		}
		break;
	case BfIRCmd_ConstValueI64:
		{
			CMD_PARAM(int64, val);
			SetResult(curId, mDIBuilder->createConstantValueExpression((uint64)val));
		}
		break;
	case BfIRCmd_DbgGetCurrentLocation:
		{
			auto debugLoc = mIRBuilder->getCurrentDebugLocation();
			if (!debugLoc)
				debugLoc = mDebugLoc;
			SetResult(curId, debugLoc);
		}
		break;
	case BfIRCmd_DbgSetType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(llvm::MDNode*, type);
			auto& typeEntry = GetTypeEntry(typeId);
			typeEntry.mDIType = (llvm::DIType*)type;
			if (typeEntry.mInstDIType == NULL)
				typeEntry.mInstDIType = (llvm::DIType*)type;
		}
		break;
	case BfIRCmd_DbgSetInstType:
		{
			CMD_PARAM(int, typeId);
			CMD_PARAM(llvm::MDNode*, type);
			GetTypeEntry(typeId).mInstDIType = (llvm::DIType*)type;
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

            if (typeEntry.mDIType != NULL)
				llvm::MetadataTracking::track(*(llvm::Metadata**)&typeEntry.mDIType);
			if (typeEntry.mInstDIType != NULL)
                llvm::MetadataTracking::track(*(llvm::Metadata**)&typeEntry.mInstDIType);
		}
		break;
	case BfIRCmd_DbgCreateNamespace:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			BF_ASSERT(file != NULL);
			SetResult(curId, mDIBuilder->createNameSpace((llvm::DIScope*)scope, name.c_str(), true));
		}
		break;
	case BfIRCmd_DbgCreateImportedModule:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(llvm::MDNode*, namespaceNode);
			CMD_PARAM(int, lineNum);
			//SetResult(curId, mDIBuilder->createImportedModule((llvm::DIScope*)context, (llvm::DINamespace*)namespaceNode, lineNum));
		}
		break;
	case BfIRCmd_DbgCreateBasicType:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, encoding);
			SetResult(curId, mDIBuilder->createBasicType(name.c_str(), sizeInBits, encoding));
		}
		break;
	case BfIRCmd_DbgCreateStructType:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, flags);
			CMD_PARAM(llvm::MDNode*, derivedFrom);
			CMD_PARAM(CmdParamVec<llvm::Metadata*>, members);
			auto diMembersArray = mDIBuilder->getOrCreateArray(members);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;
            auto mdStruct = mDIBuilder->createStructType((llvm::DIScope*)context, name.c_str(), (llvm::DIFile*)file, lineNum, sizeInBits, (uint32)alignInBits, diFlags, (llvm::DIType*)derivedFrom, diMembersArray);
            SetResult(curId, mdStruct);

            //OutputDebugStrF("BfIRCmd_DbgCreateStructType %p\n", mdStruct);
		}
		break;
	case BfIRCmd_DbgCreateEnumerationType:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(CmdParamVec<llvm::Metadata*>, members);
			CMD_PARAM(llvm::MDNode*, underlyingType);
			auto diMembersArray = mDIBuilder->getOrCreateArray(members);

			/*static int typeIdx = 0;
			if (name == "TypeCode")
				name += StrFormat("_%d", typeIdx);
			typeIdx++;*/

			BF_ASSERT(file != NULL);
			auto enumType = mDIBuilder->createEnumerationType((llvm::DIScope*)context, name.c_str(), (llvm::DIFile*)file, lineNum, sizeInBits, (uint32)alignInBits, diMembersArray, (llvm::DIType*)underlyingType);
			SetResult(curId, enumType);

            //OutputDebugStrF("BfIRCmd_DbgCreateEnumerationType %p\n", enumType);
		}
		break;
	case BfIRCmd_DbgCreatePointerType:
		{
			CMD_PARAM(llvm::MDNode*, diType);
			SetResult(curId, mDIBuilder->createPointerType((llvm::DIType*)diType, mPtrSize*8, (uint32)mPtrSize * 8));
		}
		break;
	case BfIRCmd_DbgCreateReferenceType:
		{
			CMD_PARAM(llvm::MDNode*, diType);
			SetResult(curId, mDIBuilder->createReferenceType(llvm::dwarf::DW_TAG_reference_type, (llvm::DIType*)diType));
		}
		break;
	case BfIRCmd_DbgCreateConstType:
		{
			CMD_PARAM(llvm::MDNode*, diType);
			SetResult(curId, mDIBuilder->createQualifiedType(llvm::dwarf::DW_TAG_const_type, (llvm::DIType*)diType));
		}
		break;
	case BfIRCmd_DbgCreateArtificialType:
		{
			CMD_PARAM(llvm::MDNode*, diType);
			SetResult(curId, mDIBuilder->createArtificialType((llvm::DIType*)diType));
		}
		break;
	case BfIRCmd_DbgCreateArrayType:
		{
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(llvm::MDNode*, elementType);
			CMD_PARAM(int64, numElements);

			llvm::SmallVector<llvm::Metadata*, 1> diSizeVec;
			diSizeVec.push_back(mDIBuilder->getOrCreateSubrange(0, numElements));
			auto diSizeArray = mDIBuilder->getOrCreateArray(diSizeVec);
			SetResult(curId, mDIBuilder->createArrayType(sizeInBits, (uint32)alignInBits, (llvm::DIType*)elementType, diSizeArray));
		}
		break;
	case BfIRCmd_DbgCreateReplaceableCompositeType:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, line);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int, flags);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;
			SetResult(curId, mDIBuilder->createReplaceableCompositeType(tag, name.c_str(), (llvm::DIScope*)scope, (llvm::DIFile*)file, line, 0, sizeInBits, (uint32)alignInBits, diFlags));
		}
		break;
	case BfIRCmd_DbgCreateForwardDecl:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, line);
			BF_ASSERT(file != NULL);

			auto diType = mDIBuilder->createForwardDecl(tag, name.c_str(), (llvm::DIScope*)scope, (llvm::DIFile*)file, line);
			SetResult(curId, diType);
		}
		break;
	case BfIRCmd_DbgCreateSizedForwardDecl:
		{
			CMD_PARAM(int, tag);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, line);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			BF_ASSERT(file != NULL);
			SetResult(curId, mDIBuilder->createForwardDecl(tag, name.c_str(), (llvm::DIScope*)scope, (llvm::DIFile*)file, line, 0, sizeInBits, (uint32)alignInBits));
		}
		break;
	case BeIRCmd_DbgSetTypeSize:
		{
			CMD_PARAM(llvm::MDNode*, mdType);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);

			class DIMutType : public llvm::DIType
			{
			public:
				void Resize(int64 newSize, int32 newAlign)
				{
					init(getLine(), newSize, newAlign, getOffsetInBits(), getFlags());
				}
			};

			auto diType = (DIMutType*)mdType;
			diType->Resize(sizeInBits, (int32)alignInBits);
		}
		break;
	case BfIRCmd_DbgReplaceAllUses:
		{
			CMD_PARAM(llvm::MDNode*, diPrevNode);
			CMD_PARAM(llvm::MDNode*, diNewNode);
			diPrevNode->replaceAllUsesWith(diNewNode);
		}
		break;
	case BfIRCmd_DbgDeleteTemporary:
		{
			CMD_PARAM(llvm::MDNode*, diNode);
			llvm::MDNode::deleteTemporary(diNode);
		}
		break;
	case BfIRCmd_DbgMakePermanent:
		{
			CMD_PARAM(llvm::MDNode*, diNode);
			CMD_PARAM(llvm::MDNode*, diBaseType);
			CMD_PARAM(CmdParamVec<llvm::Metadata*>, members);

			llvm::MDNode* newNode = diNode;
			if (auto diComposite = llvm::dyn_cast<llvm::DICompositeType>(diNode))
			{
				//diComposite->getBaseType()

				if (diBaseType != NULL)
				{
					// It's unfortunate we have to hard-code the '3' here
					diComposite->replaceOperandWith(3, diBaseType);
					BF_ASSERT(diComposite->getBaseType() == diBaseType);
				}

				if (members.size() != 0)
				{
					llvm::DINodeArray elements = mDIBuilder->getOrCreateArray(members);
					mDIBuilder->replaceArrays(diComposite, elements);
				}
				newNode = llvm::MDNode::replaceWithPermanent(llvm::TempDICompositeType(diComposite));
			}
			/*else if (auto diEnumerator = llvm::dyn_cast<llvm::DIEnumerator>(diNode))
			{
				if (members.size() != 0)
				{
					llvm::DINodeArray elements = mDIBuilder->getOrCreateArray(diNode);
					mDIBuilder->set(diComposite, elements);
				}
				newNode = llvm::MDNode::replaceWithPermanent(llvm::TempDIEnumerator(diEnumerator));
			}*/

			SetResult(curId, newNode);
			break;
		}
	case BfIRCmd_CreateEnumerator:
		{
			CMD_PARAM(String, name);
			CMD_PARAM(int64, val);
			SetResult(curId, mDIBuilder->createEnumerator(name.c_str(), val));
		}
		break;
	case BfIRCmd_DbgCreateMemberType:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNumber);
			CMD_PARAM(int64, sizeInBits);
			CMD_PARAM(int64, alignInBits);
			CMD_PARAM(int64, offsetInBits);
			CMD_PARAM(int, flags);
			CMD_PARAM(llvm::MDNode*, type);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;

            /*Beefy::debug_ostream os;
            os << "BfIRCmd_DbgCreateMemberType " << name.c_str() << "\n";
            scope->print(os);
            os << "\n";
            type->print(os);
            os << "\n";
            os.flush();*/

			const char* namePtr = name.c_str();
			if (name.IsEmpty())
				namePtr = NULL;

            auto member = mDIBuilder->createMemberType((llvm::DIScope*)scope, namePtr, (llvm::DIFile*)file, lineNumber, sizeInBits, (uint32)alignInBits, offsetInBits, diFlags, (llvm::DIType*)type);
            SetResult(curId, member);
            //OutputDebugStrF("BfIRCmd_DbgCreateMemberType = %p\n", member);
		}
		break;
	case BfIRCmd_DbgStaticCreateMemberType:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNumber);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(int, flags);
			CMD_PARAM(llvm::Constant*, val);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;

            /*Beefy::debug_ostream os;
            os << "BfIRCmd_DbgStaticCreateMemberType " << name.c_str() << "\n";
            scope->print(os);
            os << "\n";
            type->print(os);
            os << "\n";
            os.flush();*/

            auto member = mDIBuilder->createStaticMemberType((llvm::DIScope*)scope, name.c_str(), (llvm::DIFile*)file, lineNumber, (llvm::DIType*)type, diFlags, val, llvm::dwarf::DW_TAG_member);
            SetResult(curId, member);
            //OutputDebugStrF("BfIRCmd_DbgStaticCreateMemberType = %p\n", member);
		}
		break;
	case BfIRCmd_DbgCreateInheritance:
		{
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(llvm::MDNode*, baseType);
			CMD_PARAM(int64, baseOffset);
			CMD_PARAM(int, flags);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;

            auto member = mDIBuilder->createInheritance((llvm::DIType*)type, (llvm::DIType*)baseType, baseOffset, 0, diFlags);
            SetResult(curId, member);
            //OutputDebugStrF("BfIRCmd_DbgCreateInheritance = %p\n", member);
		}
		break;
	case BfIRCmd_DbgCreateMethod:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(bool, isLocalToUnit);
			CMD_PARAM(bool, isDefinition);
			CMD_PARAM(int, vk);
			CMD_PARAM(int, vIndex);
			CMD_PARAM(llvm::MDNode*, vTableHolder);
			CMD_PARAM(int, flags);
			CMD_PARAM(bool, isOptimized);
			CMD_PARAM(llvm::Value*, fn);
			CMD_PARAM(CmdParamVec<llvm::MDNode*>, genericArgs);
			CMD_PARAM(CmdParamVec<llvm::Constant*>, genericConstValueArgs);
			BF_ASSERT(file != NULL);

			llvm::DITemplateParameterArray templateParamArr = NULL;
			llvm::DINodeArray templateParamNodes;
			if (genericArgs.size() != 0)
			{
				llvm::SmallVector<llvm::Metadata*, 16> templateParams;
				for (int i = 0; i < (int)genericArgs.size(); i++)
				{
					auto genericArg = (llvm::DIType*)genericArgs[i];
					String name = StrFormat("T%d", i);

					llvm::Constant* constant = NULL;
					if (i < genericConstValueArgs.size())
						constant = genericConstValueArgs[i];

					if (constant != NULL)
						templateParams.push_back(mDIBuilder->createTemplateValueParameter(mDICompileUnit, name.c_str(), genericArg, false, constant));
					else
						templateParams.push_back(mDIBuilder->createTemplateTypeParameter(mDICompileUnit, name.c_str(), genericArg, false));
				}
				templateParamNodes = mDIBuilder->getOrCreateArray(templateParams);
				templateParamArr = templateParamNodes.get();
			}

			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;
			llvm::DISubprogram::DISPFlags dispFlags = llvm::DISubprogram::DISPFlags::SPFlagZero;
			if (isLocalToUnit)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagLocalToUnit);
			if (isDefinition)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagDefinition);
			if (isOptimized)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagOptimized);
			if (vk != 0)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagVirtual);

			auto diSubProgram = mDIBuilder->createMethod((llvm::DIScope*)context, name.c_str(), linkageName.c_str(), (llvm::DIFile*)file, lineNum,
				(llvm::DISubroutineType*)type, vIndex, 0, (llvm::DIType*)vTableHolder, diFlags, dispFlags, templateParamArr);
			if (fn != NULL)
				((llvm::Function*)fn)->setSubprogram(diSubProgram);

            SetResult(curId, diSubProgram);

            //OutputDebugStrF("BfIRCmd_DbgCreateMethod = %p\n", diSubProgram);
		}
		break;
	case BfIRCmd_DbgCreateFunction:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(bool, isLocalToUnit);
			CMD_PARAM(bool, isDefinition);
			CMD_PARAM(int, scopeLine);
			CMD_PARAM(int, flags);
			CMD_PARAM(bool, isOptimized);
			CMD_PARAM(llvm::Value*, fn);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;

			llvm::DISubprogram::DISPFlags dispFlags = llvm::DISubprogram::DISPFlags::SPFlagZero;
			if (isLocalToUnit)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagLocalToUnit);
			if (isDefinition)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagDefinition);
			if (isOptimized)
				dispFlags = (llvm::DISubprogram::DISPFlags)(dispFlags | llvm::DISubprogram::DISPFlags::SPFlagOptimized);

			auto diSubProgram = mDIBuilder->createFunction((llvm::DIScope*)context, name.c_str(), linkageName.c_str(), (llvm::DIFile*)file, lineNum,
				(llvm::DISubroutineType*)type, scopeLine, diFlags, dispFlags);
			if (fn != NULL)
				((llvm::Function*)fn)->setSubprogram(diSubProgram);
			SetResult(curId, diSubProgram);

            //OutputDebugStrF("BfIRCmd_DbgCreateFunction = %p\n", diSubProgram);
		}
		break;
	case BfIRCmd_DbgCreateParameterVariable:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(int, argNo);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(bool, alwaysPreserve);
			CMD_PARAM(int, flags);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)flags;
			SetResult(curId, mDIBuilder->createParameterVariable((llvm::DIScope*)scope, name.c_str(), argNo, (llvm::DIFile*)file, lineNum, (llvm::DIType*)type,
				alwaysPreserve, diFlags));
		}
		break;
	case BfIRCmd_DbgCreateSubroutineType:
		{
			CMD_PARAM(CmdParamVec<llvm::Metadata*>, elements);
			auto diArray = mDIBuilder->getOrCreateTypeArray(elements);
			SetResult(curId, mDIBuilder->createSubroutineType(diArray));
		}
		break;
	case BfIRCmd_DbgCreateAutoVariable:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNo);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(int, initType);
			BF_ASSERT(file != NULL);
			llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)0;

			auto loc = mIRBuilder->getCurrentDebugLocation();
			auto dbgLoc = loc.getAsMDNode();

			SetResult(curId, mDIBuilder->createAutoVariable((llvm::DIScope*)scope, name.c_str(), (llvm::DIFile*)file, lineNo, (llvm::DIType*)type, false, diFlags));
		}
		break;
	case BfIRCmd_DbgInsertValueIntrinsic:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(llvm::MDNode*, varInfo);

			auto diVariable = (llvm::DILocalVariable*)varInfo;

			if (val == NULL)
			{
				val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), 0);
			}
			else if (mIsCodeView)
			{
				if (auto constant = llvm::dyn_cast<llvm::Constant>(val))
				{
					int64 writeVal = 0;
					if (auto constantInt = llvm::dyn_cast<llvm::ConstantInt>(val))
					{
						writeVal = constantInt->getSExtValue();
					}

					auto nameRef = diVariable->getName();

					if (writeVal < 0)
						diVariable->replaceOperandWith(1, llvm::MDString::get(*mLLVMContext, (String(nameRef.data()) + StrFormat("$_%llu", -writeVal)).c_str()));
					else
						diVariable->replaceOperandWith(1, llvm::MDString::get(*mLLVMContext, (String(nameRef.data()) + StrFormat("$%llu", writeVal)).c_str()));
				}
			}

			mDIBuilder->insertDbgValueIntrinsic(val, diVariable, mDIBuilder->createExpression(),
				mIRBuilder->getCurrentDebugLocation(), (llvm::BasicBlock*)mIRBuilder->GetInsertBlock());
		}
		break;
	case BfIRCmd_DbgInsertDeclare:
		{
			CMD_PARAM(llvm::Value*, val);
			CMD_PARAM(llvm::MDNode*, varInfo);
			CMD_PARAM(llvm::Value*, insertBefore);

			llvm::Instruction* insertBeforeInst = NULL;
			if (insertBefore != NULL)
				insertBeforeInst = llvm::dyn_cast<llvm::Instruction>(insertBefore);

			// Protect against lack of debug location
			if (mIRBuilder->getCurrentDebugLocation())
			{
				if (insertBeforeInst != NULL)
				{
					SetResult(curId, mDIBuilder->insertDeclare(val, (llvm::DILocalVariable*)varInfo, mDIBuilder->createExpression(),
						mIRBuilder->getCurrentDebugLocation(), insertBeforeInst));
				}
				else
				{
					SetResult(curId, mDIBuilder->insertDeclare(val, (llvm::DILocalVariable*)varInfo, mDIBuilder->createExpression(),
						mIRBuilder->getCurrentDebugLocation(), mIRBuilder->GetInsertBlock()));
				}
			}
		}
		break;
	case BfIRCmd_DbgLifetimeEnd:
		{
			CMD_PARAM(llvm::MDNode*, varInfo);
		}
		break;
	case BfIRCmd_DbgCreateGlobalVariable:
		{
			CMD_PARAM(llvm::MDNode*, context);
			CMD_PARAM(String, name);
			CMD_PARAM(String, linkageName);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(llvm::MDNode*, type);
			CMD_PARAM(bool, isLocalToUnit);
			CMD_PARAM(llvm::Constant*, val);
			CMD_PARAM(llvm::MDNode*, decl);
			//BF_ASSERT(file != NULL);
			llvm::DIExpression* diExpr = NULL;
			auto gve = mDIBuilder->createGlobalVariableExpression((llvm::DIScope*)context, name.c_str(), linkageName.c_str(), (llvm::DIFile*)file, lineNum, (llvm::DIType*)type,
				isLocalToUnit, true, diExpr, decl);

			if (val != NULL)
			{
				if (auto globalVar = llvm::dyn_cast<llvm::GlobalVariable>(val))
				{
					globalVar->addDebugInfo(gve);
				}
			}

			SetResult(curId, diExpr);
		}
		break;
	case BfIRCmd_DbgCreateLexicalBlock:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(llvm::MDNode*, file);
			CMD_PARAM(int, lineNum);
			CMD_PARAM(int, col);
			if (lineNum == 0)
				col = 0;
			BF_ASSERT(file != NULL);
			SetResult(curId, mDIBuilder->createLexicalBlock((llvm::DIScope*)scope, (llvm::DIFile*)file, (unsigned)lineNum, (unsigned)col));
		}
		break;
	case BfIRCmd_DbgCreateAnnotation:
		{
			CMD_PARAM(llvm::MDNode*, scope);
			CMD_PARAM(String, name);
			CMD_PARAM(llvm::Value*, value);

			if (auto dbgFunc = llvm::dyn_cast<llvm::DISubprogram>(scope))
			{
				auto beType = value->getType();

				auto diType = mDIBuilder->createBasicType("int32", 4 * 8, llvm::dwarf::DW_ATE_signed);

				llvm::DINode::DIFlags diFlags = (llvm::DINode::DIFlags)0;

				auto loc = mIRBuilder->getCurrentDebugLocation();
				auto dbgLoc = loc.getAsMDNode();
				auto diScope = (llvm::DIScope*)scope;

				String dbgName = "#" + name;

				int64 writeVal = 0;
				if (auto constant = llvm::dyn_cast<llvm::ConstantInt>(value))
				{
					writeVal = constant->getSExtValue();
				}

				if (writeVal < 0)
					dbgName += StrFormat("$_%llu", -writeVal);
				else
					dbgName += StrFormat("$%llu", writeVal);

				auto dbgVar = mDIBuilder->createAutoVariable((llvm::DIScope*)scope, dbgName.c_str(), (llvm::DIFile*)diScope->getFile(), 0, diType, false, diFlags);

				mDIBuilder->insertDbgValueIntrinsic(value, dbgVar, mDIBuilder->createExpression(),
					mIRBuilder->getCurrentDebugLocation(), (llvm::BasicBlock*)mIRBuilder->GetInsertBlock());
			}
		}
		break;
	default:
		BF_FATAL("Unhandled");
		break;
	}
}

void BfIRCodeGen::SetCodeGenOptions(BfCodeGenOptions codeGenOptions)
{
	mCodeGenOptions = codeGenOptions;
}

void BfIRCodeGen::SetConfigConst(int idx, int value)
{
	auto constVal = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*mLLVMContext), value);
	BF_ASSERT(idx == (int)mConfigConsts32.size());
	mConfigConsts32.Add(constVal);

	constVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*mLLVMContext), value);
	BF_ASSERT(idx == (int)mConfigConsts64.size());
	mConfigConsts64.Add(constVal);
}

void BfIRCodeGen::SetActiveFunctionSimdType(BfIRSimdType type)
{
	BfIRSimdType currentType;
	bool contains = mFunctionsUsingSimd.TryGetValue(mActiveFunction, &currentType);

	if (!contains || type > currentType)
		mFunctionsUsingSimd[mActiveFunction] = type;
}

String BfIRCodeGen::GetSimdTypeString(BfIRSimdType type)
{
	switch (type)
	{
	case BfIRSimdType_SSE:
		return "+sse,+mmx";
	case BfIRSimdType_SSE2:
		return "+sse2,+sse,+mmx";
	case BfIRSimdType_AVX:
		return "+avx,+sse4.2,+sse4.1,+sse3,+sse2,+sse,+mmx";
	case BfIRSimdType_AVX2:
		return "+avx2,+avx,+sse4.2,+sse4.1,+sse3,+sse2,+sse,+mmx";
	case BfIRSimdType_AVX512:
		return "+avx512f,+avx2,+avx,+sse4.2,+sse4.1,+sse3,+sse2,+sse,+mmx";
	default:
		return "";
	}
}

BfIRSimdType BfIRCodeGen::GetSimdTypeFromFunction(llvm::Function* function)
{
	if (function->hasFnAttribute("target-features"))
	{
		auto str = function->getFnAttribute("target-features").getValueAsString();

		if (str.contains("+avx512f"))
			return BfIRSimdType_AVX512;
		if (str.contains("+avx2"))
			return BfIRSimdType_AVX2;
		if (str.contains("+avx"))
			return BfIRSimdType_AVX;
		if (str.contains("+sse2"))
			return BfIRSimdType_SSE2;
		if (str.contains("+sse"))
			return BfIRSimdType_SSE;
	}

	return BfIRSimdType_None;
}

BfIRTypedValue BfIRCodeGen::GetTypedValue(int id)
{
	auto& result = mResults[id];
	if (result.mKind == BfIRCodeGenEntryKind_TypedValue)
		return result.mTypedValue;
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMValue);
	
	BfIRTypedValue typedValue;
	typedValue.mTypeEx = NULL;
	typedValue.mValue = result.mLLVMValue;
	return typedValue;
}

llvm::Value* BfIRCodeGen::GetLLVMValue(int id)
{
	auto& result = mResults[id];
	if (result.mKind == BfIRCodeGenEntryKind_TypedValue)
		return result.mTypedValue.mValue;
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMValue);
	return result.mLLVMValue;
}

llvm::Type* BfIRCodeGen::GetLLVMType(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMType);
	return result.mLLVMType;
}

llvm::BasicBlock * BfIRCodeGen::GetLLVMBlock(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMBasicBlock);
	return result.mLLVMBlock;
}

llvm::MDNode* BfIRCodeGen::GetLLVMMetadata(int id)
{
	auto& result = mResults[id];
	BF_ASSERT(result.mKind == BfIRCodeGenEntryKind_LLVMMetadata);
	return result.mLLVMMetadata;
}

llvm::Type* BfIRCodeGen::GetLLVMTypeById(int id)
{
	return GetTypeEntry(id).mType->mLLVMType;
}

// LLVM/Clang 18.1.4
static void addSanitizers(const llvm::Triple& TargetTriple,	BfCodeGenOptions& CodeGenOpts, llvm::PassBuilder& PB) 
{
#if 0	
	auto SanitizersCallback = [&](llvm::ModulePassManager& MPM, llvm::OptimizationLevel Level) {
			if (CodeGenOpts.hasSanitizeCoverage()) 
			{
				auto SancovOpts = getSancovOptsFromCGOpts(CodeGenOpts);
				MPM.addPass(SanitizerCoveragePass(
					SancovOpts, CodeGenOpts.SanitizeCoverageAllowlistFiles,
					CodeGenOpts.SanitizeCoverageIgnorelistFiles));
			}

			if (CodeGenOpts.hasSanitizeBinaryMetadata()) {
				MPM.addPass(SanitizerBinaryMetadataPass(
					getSanitizerBinaryMetadataOptions(CodeGenOpts),
					CodeGenOpts.SanitizeMetadataIgnorelistFiles));
			}

			auto MSanPass = [&](SanitizerMask Mask, bool CompileKernel) {
				if (LangOpts.Sanitize.has(Mask)) {
					int TrackOrigins = CodeGenOpts.SanitizeMemoryTrackOrigins;
					bool Recover = CodeGenOpts.SanitizeRecover.has(Mask);

					MemorySanitizerOptions options(TrackOrigins, Recover, CompileKernel,
						CodeGenOpts.SanitizeMemoryParamRetval);
					MPM.addPass(MemorySanitizerPass(options));
					if (Level != OptimizationLevel::O0) {
						// MemorySanitizer inserts complex instrumentation that mostly follows
						// the logic of the original code, but operates on "shadow" values. It
						// can benefit from re-running some general purpose optimization
						// passes.
						MPM.addPass(RequireAnalysisPass<GlobalsAA, llvm::Module>());
						FunctionPassManager FPM;
						FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));
						FPM.addPass(InstCombinePass());
						FPM.addPass(JumpThreadingPass());
						FPM.addPass(GVNPass());
						FPM.addPass(InstCombinePass());
						MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
					}
				}
				};
			MSanPass(SanitizerKind::Memory, false);
			MSanPass(SanitizerKind::KernelMemory, true);

			if (LangOpts.Sanitize.has(SanitizerKind::Thread)) {
				MPM.addPass(ModuleThreadSanitizerPass());
				MPM.addPass(createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
			}

			auto ASanPass = [&](SanitizerMask Mask, bool CompileKernel) {
				if (LangOpts.Sanitize.has(Mask)) {
					bool UseGlobalGC = asanUseGlobalsGC(TargetTriple, CodeGenOpts);
					bool UseOdrIndicator = CodeGenOpts.SanitizeAddressUseOdrIndicator;
					llvm::AsanDtorKind DestructorKind =
						CodeGenOpts.getSanitizeAddressDtor();
					AddressSanitizerOptions Opts;
					Opts.CompileKernel = CompileKernel;
					Opts.Recover = CodeGenOpts.SanitizeRecover.has(Mask);
					Opts.UseAfterScope = CodeGenOpts.SanitizeAddressUseAfterScope;
					Opts.UseAfterReturn = CodeGenOpts.getSanitizeAddressUseAfterReturn();
					MPM.addPass(AddressSanitizerPass(Opts, UseGlobalGC, UseOdrIndicator,
						DestructorKind));
				}
				};
			ASanPass(SanitizerKind::Address, false);
			ASanPass(SanitizerKind::KernelAddress, true);

			auto HWASanPass = [&](SanitizerMask Mask, bool CompileKernel) {
				if (LangOpts.Sanitize.has(Mask)) {
					bool Recover = CodeGenOpts.SanitizeRecover.has(Mask);
					MPM.addPass(HWAddressSanitizerPass(
						{ CompileKernel, Recover,
						/*DisableOptimization=*/CodeGenOpts.OptimizationLevel == 0 }));
				}
				};
			HWASanPass(SanitizerKind::HWAddress, false);
			HWASanPass(SanitizerKind::KernelHWAddress, true);

			if (LangOpts.Sanitize.has(SanitizerKind::DataFlow)) {
				MPM.addPass(DataFlowSanitizerPass(LangOpts.NoSanitizeFiles));
			}
		};
	if (ClSanitizeOnOptimizerEarlyEP) {
		PB.registerOptimizerEarlyEPCallback(
			[SanitizersCallback](ModulePassManager& MPM, OptimizationLevel Level) {
				ModulePassManager NewMPM;
				SanitizersCallback(NewMPM, Level);
				if (!NewMPM.isEmpty()) {
					// Sanitizers can abandon<GlobalsAA>.
					NewMPM.addPass(RequireAnalysisPass<GlobalsAA, llvm::Module>());
					MPM.addPass(std::move(NewMPM));
				}
			});
	}
	else {
		// LastEP does not need GlobalsAA.
		PB.registerOptimizerLastEPCallback(SanitizersCallback);
	}
#endif
}

// LLVM/Clang 18.1.4
static void addKCFIPass(const llvm::Triple& TargetTriple, const BfCodeGenOptions& codeGenOpts, llvm::PassBuilder& PB) 
{
#if 0
	// If the back-end supports KCFI operand bundle lowering, skip KCFIPass.
	if (TargetTriple.getArch() == llvm::Triple::x86_64 ||
		TargetTriple.isAArch64(64) || TargetTriple.isRISCV())
		return;

	// Ensure we lower KCFI operand bundles with -O0.
	PB.registerOptimizerLastEPCallback(
		[&](ModulePassManager& MPM, OptimizationLevel Level) {
			if (Level == OptimizationLevel::O0 &&
				LangOpts.Sanitize.has(SanitizerKind::KCFI))
				MPM.addPass(createModuleToFunctionPassAdaptor(KCFIPass()));
		});

	// When optimizations are requested, run KCIFPass after InstCombine to
	// avoid unnecessary checks.
	PB.registerPeepholeEPCallback(
		[&](FunctionPassManager& FPM, OptimizationLevel Level) {
			if (Level != OptimizationLevel::O0 &&
				LangOpts.Sanitize.has(SanitizerKind::KCFI))
				FPM.addPass(KCFIPass());
		});
#endif
}

/// Check whether we should emit a module summary for regular LTO.
  /// The module summary should be emitted by default for regular LTO
  /// except for ld64 targets.
  ///
  /// \return True if the module summary should be emitted.
static bool shouldEmitRegularLTOSummary(const llvm::Triple& targetTriple, const BfCodeGenOptions& codeGenOptions, bool PrepareForLTO)
{
	return PrepareForLTO /*&& !CodeGenOpts.DisableLLVMPasses*/ &&
		targetTriple.getVendor() != llvm::Triple::Apple;
}

/// Check whether we should emit a flag for UnifiedLTO.
/// The UnifiedLTO module flag should be set when UnifiedLTO is enabled for
/// ThinLTO or Full LTO with module summaries.
static bool shouldEmitUnifiedLTOModueFlag(const llvm::Triple& targetTriple, const BfCodeGenOptions& codeGenOptions, bool PrepareForLTO)
{
	return false;
	/*return CodeGenOpts.UnifiedLTO &&
		(CodeGenOpts.PrepareForThinLTO || shouldEmitRegularLTOSummary());*/
}

void BfIRCodeGen::RunOptimizationPipeline(const llvm::Triple& targetTriple)
{
	bool verifyModule = true;

	std::optional<llvm::PGOOptions> pgoOptions;
	mLLVMTargetMachine->setPGOOption(pgoOptions);

	llvm::PipelineTuningOptions pto;
	pto.LoopUnrolling = !mCodeGenOptions.mDisableUnrollLoops;
	// For historical reasons, loop interleaving is set to mirror setting for loop unrolling.
	pto.LoopInterleaving = !mCodeGenOptions.mDisableUnrollLoops;
	pto.LoopVectorization = mCodeGenOptions.mLoopVectorize;
	pto.SLPVectorization = mCodeGenOptions.mSLPVectorize;
	pto.MergeFunctions = mCodeGenOptions.mMergeFunctions;
	//TODO:
	//pto.CallGraphProfile = ???
	//pto.UnifiedLTO = ???

	llvm::LoopAnalysisManager LAM;
	llvm::FunctionAnalysisManager FAM;
	llvm::CGSCCAnalysisManager CGAM;
	llvm::ModuleAnalysisManager MAM;

	llvm::PassInstrumentationCallbacks PIC;
// 	PrintPassOptions PrintPassOpts;
// 	PrintPassOpts.Indent = DebugPassStructure;
// 	PrintPassOpts.SkipAnalyses = DebugPassStructure;
// 	StandardInstrumentations SI(
// 		TheModule->getContext(),
// 		(CodeGenOpts.DebugPassManager || DebugPassStructure),
// 		CodeGenOpts.VerifyEach, PrintPassOpts);
// 	SI.registerCallbacks(PIC, &MAM);
	llvm::PassBuilder PB(mLLVMTargetMachine, pto, pgoOptions, &PIC);

	// Register all the basic analyses with the managers.
	PB.registerModuleAnalyses(MAM);
	PB.registerCGSCCAnalyses(CGAM);
	PB.registerFunctionAnalyses(FAM);
	PB.registerLoopAnalyses(LAM);
	PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
	
	//llvm::ModulePassManager MPM;
	// Add a verifier pass, before any other passes, to catch CodeGen issues.

	llvm::ModulePassManager MPM;
	if (verifyModule)
		MPM.addPass(llvm::VerifierPass());	

	bool disableLLVMPasses = false;
	if (!disableLLVMPasses)
	{
		llvm::OptimizationLevel Level;

		bool PrepareForLTO = false;
		bool PrepareForThinLTO = mCodeGenOptions.mLTOType == BfLTOType_Thin;
		//bool performThinLTO = false;

		Level = llvm::OptimizationLevel::O0;
		switch (mCodeGenOptions.mOptLevel)
		{
		case BfOptLevel_O0:
			Level = llvm::OptimizationLevel::O0;
			break;
		case BfOptLevel_O1:
			Level = llvm::OptimizationLevel::O1;
			break;
		case BfOptLevel_O2:
			Level = llvm::OptimizationLevel::O2;
			break;
		case BfOptLevel_O3:
			Level = llvm::OptimizationLevel::O3;
			break;
		case BfOptLevel_Og:			
			Level = llvm::OptimizationLevel::O1;
			break;
		}

		bool IsThinLTOPostLink = false;

#if 0
		// If we reached here with a non-empty index file name, then the index
		// file was empty and we are not performing ThinLTO backend compilation
		// (used in testing in a distributed build environment).

			bool IsThinLTOPostLink = !CodeGenOpts.ThinLTOIndexFile.empty();
			// If so drop any the type test assume sequences inserted for whole program
			// vtables so that codegen doesn't complain.
			if (IsThinLTOPostLink)
				PB.registerPipelineStartEPCallback(
					[](ModulePassManager& MPM, OptimizationLevel Level) {
						MPM.addPass(LowerTypeTestsPass(/*ExportSummary=*/nullptr,
						/*ImportSummary=*/nullptr,
						/*DropTypeTests=*/true));
					});

		// Register callbacks to schedule sanitizer passes at the appropriate part
		// of the pipeline.
			if (LangOpts.Sanitize.has(SanitizerKind::LocalBounds))
				PB.registerScalarOptimizerLateEPCallback(
					[](FunctionPassManager& FPM, OptimizationLevel Level) {
						FPM.addPass(BoundsCheckingPass());
					});
#endif

		// Don't add sanitizers if we are here from ThinLTO PostLink. That already
		// done on PreLink stage.
		if (!IsThinLTOPostLink) {
			addSanitizers(targetTriple, mCodeGenOptions, PB);
			addKCFIPass(targetTriple, mCodeGenOptions, PB);
		}

#if 0
		if (std::optional<GCOVOptions> Options =
			getGCOVOptions(CodeGenOpts, LangOpts))
			PB.registerPipelineStartEPCallback(
				[Options](ModulePassManager& MPM, OptimizationLevel Level) {
					MPM.addPass(GCOVProfilerPass(*Options));
				});
		if (std::optional<InstrProfOptions> Options =
			getInstrProfOptions(CodeGenOpts, LangOpts))
			PB.registerPipelineStartEPCallback(
				[Options](ModulePassManager& MPM, OptimizationLevel Level) {
					MPM.addPass(InstrProfilingLoweringPass(*Options, false));
				});

		// TODO: Consider passing the MemoryProfileOutput to the pass builder via
		// the PGOOptions, and set this up there.
		if (!CodeGenOpts.MemoryProfileOutput.empty()) {
			PB.registerOptimizerLastEPCallback(
				[](ModulePassManager& MPM, OptimizationLevel Level) {
					MPM.addPass(createModuleToFunctionPassAdaptor(MemProfilerPass()));
					MPM.addPass(ModuleMemProfilerPass());
				});
		}		
#endif

		if (mCodeGenOptions.mLTOType == BfLTOType_Fat)
		{
			MPM.addPass(PB.buildFatLTODefaultPipeline(
				Level, PrepareForThinLTO,
				PrepareForThinLTO || shouldEmitRegularLTOSummary(targetTriple, mCodeGenOptions, PrepareForLTO)));
		}
		else if (PrepareForThinLTO) 
		{
			MPM.addPass(PB.buildThinLTOPreLinkDefaultPipeline(Level));
		}
		else if (PrepareForLTO) 
		{
			MPM.addPass(PB.buildLTOPreLinkDefaultPipeline(Level));
		}
		else 
		{
			MPM.addPass(PB.buildPerModuleDefaultPipeline(Level));
		}
	}


	// Re-link against any bitcodes supplied via the -mlink-builtin-bitcode option
	// Some optimizations may generate new function calls that would not have
	// been linked pre-optimization (i.e. fused sincos calls generated by
	// AMDGPULibCalls::fold_sincos.)
	//TODO:
// 	if (ClRelinkBuiltinBitcodePostop)
// 		MPM.addPass(LinkInModulesPass(BC, false));

	// Add a verifier pass if requested. We don't have to do this if the action
	// requires code generation because there will already be a verifier pass in
	// the code-generation pipeline.
	// Since we already added a verifier pass above, this
	// might even not run the analysis, if previous passes caused no changes.
// 	if (!actionRequiresCodeGen(Action) && CodeGenOpts.VerifyModule)
// 		MPM.addPass(VerifierPass());

	//TODO:
#if 0
	if (Action == Backend_EmitBC || Action == Backend_EmitLL || CodeGenOpts.FatLTO)
	{
		if (CodeGenOpts.PrepareForThinLTO && !CodeGenOpts.DisableLLVMPasses) {
			if (!TheModule->getModuleFlag("EnableSplitLTOUnit"))
				TheModule->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit",
					CodeGenOpts.EnableSplitLTOUnit);
			if (Action == Backend_EmitBC) {
				if (!CodeGenOpts.ThinLinkBitcodeFile.empty()) {
					ThinLinkOS = openOutputFile(CodeGenOpts.ThinLinkBitcodeFile);
					if (!ThinLinkOS)
						return;
				}
				MPM.addPass(ThinLTOBitcodeWriterPass(
					*OS, ThinLinkOS ? &ThinLinkOS->os() : nullptr));
			}
			else if (Action == Backend_EmitLL) {
				MPM.addPass(PrintModulePass(*OS, "", CodeGenOpts.EmitLLVMUseLists,
					/*EmitLTOSummary=*/true));
			}
		}
		else {
			// Emit a module summary by default for Regular LTO except for ld64
			// targets
			bool EmitLTOSummary = shouldEmitRegularLTOSummary();
			if (EmitLTOSummary) {
				if (!TheModule->getModuleFlag("ThinLTO") && !CodeGenOpts.UnifiedLTO)
					TheModule->addModuleFlag(llvm::Module::Error, "ThinLTO", uint32_t(0));
				if (!TheModule->getModuleFlag("EnableSplitLTOUnit"))
					TheModule->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit",
						uint32_t(1));
			}
			if (Action == Backend_EmitBC) {
				MPM.addPass(BitcodeWriterPass(*OS, CodeGenOpts.EmitLLVMUseLists,
					EmitLTOSummary));
			}
			else if (Action == Backend_EmitLL) {
				MPM.addPass(PrintModulePass(*OS, "", CodeGenOpts.EmitLLVMUseLists,
					EmitLTOSummary));
			}
		}

		if (shouldEmitUnifiedLTOModueFlag())
			TheModule->addModuleFlag(llvm::Module::Error, "UnifiedLTO", uint32_t(1));
	}
#endif

	// Print a textual, '-passes=' compatible, representation of pipeline if
	// requested.
//	if (PrintPipelinePasses) {
// 		MPM.printPipeline(outs(), [&PIC](StringRef ClassName) {
// 			auto PassName = PIC.getPassNameForClassName(ClassName);
// 			return PassName.empty() ? ClassName : PassName;
// 			});
// 		outs() << "\n";
// 		return;
// 	}
// 
// 	if (LangOpts.HIPStdPar && !LangOpts.CUDAIsDevice &&
// 		LangOpts.HIPStdParInterposeAlloc)
// 		MPM.addPass(HipStdParAllocationInterpositionPass());

	// Now that we have all of the passes ready, run them.
	{
		//PrettyStackTraceString CrashInfo("Optimizer");
		llvm::TimeTraceScope TimeScope("Optimizer");
		MPM.run(*mLLVMModule, MAM);
	}	
}

bool BfIRCodeGen::WriteObjectFile(const StringImpl& outFileName)
{
	ApplySimdFeatures();

	// 	{
	// 		PassManagerBuilderWrapper pmBuilder;
	//
	//
	// 	}

	mHasDebugLoc = false; // So fails don't show a line number

	bool enableLTO = mCodeGenOptions.mLTOType != BfLTOType_None;

	if (enableLTO)
	{
		// We have some constructs which trip up ThinLTO, and it's not useful to LTO here anyway
		if (GetFileName(outFileName) == "vdata.obj")
		{
			enableLTO = false;
		}

		if (mHadDLLExport) // LTO bug in LLVM-link?
			enableLTO = false;
	}

	std::error_code EC;
	llvm::sys::fs::OpenFlags OpenFlags = llvm::sys::fs::OF_None;

	llvm::raw_fd_ostream out(outFileName.c_str(), EC, OpenFlags);

	if (EC)
		return false;
	// Build up all of the passes that we want to do to the module.
	//llvm::legacy::PassManager PM;
	llvm::legacy::PassManager PM;

	llvm::Triple theTriple = llvm::Triple(mLLVMModule->getTargetTriple());
	// Add an appropriate TargetLibraryInfo pass for the module's triple.
	llvm::TargetLibraryInfoImpl TLII(theTriple);

	PM.add(new llvm::TargetLibraryInfoWrapperPass(TLII));

	// Add the target data from the target machine, if it exists, or the module.
	//PM.add(new DataLayoutPass());
	RunOptimizationPipeline(theTriple);

	llvm::raw_fd_ostream* outStream = NULL;
	defer ( delete outStream; );

	if ((enableLTO) || (mCodeGenOptions.mWriteBitcode))
	{
		std::error_code ec;
		outStream = new llvm::raw_fd_ostream(outFileName.c_str(), ec, llvm::sys::fs::OF_None);
		if (outStream->has_error())
		{
			return false;
		}

// 		if (enableLTO)
// 			PM.add(createWriteThinLTOBitcodePass(*outStream, NULL));
		//else
			PM.add(createBitcodeWriterPass(*outStream, false));
	}

// 	TargetPassConfig *PassConfig = target->createPassConfig(PM);
// 	PM.add(new BfPass());
// 	PM.add(sBfPass);


	// Do
	{
		//formatted_raw_ostream FOS(out);
		//raw_pwrite_stream *OS = &out->os();

		//TODO:
 		llvm::AnalysisID StartAfterID = nullptr;
 		llvm::AnalysisID StopAfterID = nullptr;
 		const llvm::PassRegistry *PR = llvm::PassRegistry::getPassRegistry();
 
 		//WriteBitcode
 		bool noVerify = false; // Option
 
 		if ((!enableLTO) && (!mCodeGenOptions.mWriteBitcode))
 		{
 			// Ask the target to add backend passes as necessary.
 			if (mLLVMTargetMachine->addPassesToEmitFile(PM, out, NULL,
 				(mCodeGenOptions.mAsmKind != BfAsmKind_None) ? llvm::CodeGenFileType::AssemblyFile : llvm::CodeGenFileType::ObjectFile,
 				//TargetMachine::CGFT_AssemblyFile,
 				noVerify /*, StartAfterID, StopAfterID*/))
 			{
 				Fail("Target does not support generation of this file type");
 				/*errs() << argv[0] << ": target does not support generation of this"
 					<< " file type!\n";*/
 				return false;
 			}
 		}

		bool success = PM.run(*mLLVMModule);

		if ((mCodeGenOptions.mOptLevel > BfOptLevel_O0) && (mCodeGenOptions.mWriteLLVMIR))
		{
			BP_ZONE("BfCodeGen::RunLoop.LLVM.IR");
			String fileName = outFileName;
			int dotPos = (int)fileName.LastIndexOf('.');
			if (dotPos != -1)
				fileName.RemoveToEnd(dotPos);

			fileName += "_OPT.ll";
			String irError;
			WriteIR(fileName, irError);
		}
	}

	return true;
}

bool BfIRCodeGen::WriteIR(const StringImpl& outFileName, StringImpl& error)
{
	std::error_code ec;
	llvm::raw_fd_ostream outStream(outFileName.c_str(), ec, llvm::sys::fs::OpenFlags::OF_Text);
	if (ec)
	{
	 	error = ec.message();
		return false;
	}
	mLLVMModule->print(outStream, NULL);
	return true;
}

void BfIRCodeGen::ApplySimdFeatures()
{
	Array<std::tuple<llvm::Function*, BfIRSimdType>> functionsToProcess;

	for (auto pair : mFunctionsUsingSimd)
		functionsToProcess.Add({ pair.mKey, pair.mValue });

	while (functionsToProcess.Count() > 0)
	{
		auto tuple = functionsToProcess.front();
		functionsToProcess.RemoveAt(0);

		auto function = std::get<0>(tuple);
		auto simdType = std::get<1>(tuple);

		auto currentSimdType = GetSimdTypeFromFunction(function);
		simdType = simdType > currentSimdType ? simdType : currentSimdType;

		function->addFnAttr("target-features", GetSimdTypeString(simdType).c_str());

		if (function->hasFnAttribute(llvm::Attribute::AlwaysInline))
		{
			for (auto user : function->users())
			{
				if (auto call = llvm::dyn_cast<llvm::CallInst>(user))
				{
					auto func = call->getFunction();
					functionsToProcess.Add({ func, simdType });
				}
			}
		}
	}
}

int BfIRCodeGen::GetIntrinsicId(const StringImpl& name)
{
	auto itr = std::lower_bound(std::begin(gIntrinEntries), std::end(gIntrinEntries), name);
	if (itr != std::end(gIntrinEntries) && strcmp(itr->mName, name.c_str()) == 0)
	{
		int id = (int)(itr - gIntrinEntries);
		return id;
	}

	if (name.StartsWith("shuffle"))
		return BfIRIntrinsic_Shuffle;

	if (name.Contains(':'))
		return BfIRIntrinsic__PLATFORM;

	return -1;
}

const char* BfIRCodeGen::GetIntrinsicName(int intrinId)
{
	return gIntrinEntries[intrinId].mName;
}

void BfIRCodeGen::SetAsmKind(BfAsmKind asmKind)
{
	const char* args[] = {"", (asmKind == BfAsmKind_ATT) ? "-x86-asm-syntax=att" : "-x86-asm-syntax=intel" };
	llvm::cl::ParseCommandLineOptions(2, args);
}

#ifdef BF_PLATFORM_LINUX
//HACK: I don't know why this is needed, but we get link errors if we don't have it.
int BF_LinuxFixLinkage()
{
	llvm::MCContext* ctx = NULL;
	llvm::raw_pwrite_stream* stream = NULL;

	createWasmStreamer(*ctx, NULL, NULL, NULL, false);
	createMachOStreamer(*ctx, NULL, NULL, NULL, false, false, false);
	createAsmStreamer(*ctx, NULL, false, false, NULL, NULL, NULL, false);
	createELFStreamer(*ctx, NULL, NULL, NULL, false);

	return 0;
}
#endif

//#include "aarch64/Disassembler/X86DisassemblerDecoder.h"
//#include "X86/MCTargetDesc/X86MCTargetDesc.h"
//#include "X86/MCTargetDesc/X86BaseInfo.h"
//#include "X86InstrInfo.h"

#ifdef BF_PLATFORM_MACOS
#include "AArch64/MCTargetDesc/AArch64MCTargetDesc.h"
//#include "AArch64/MCTargetDesc/AArch64BaseInfo.h"
//#include "../X86InstrInfo.h"

int BF_AARC64_Linkage()
{
	LLVMInitializeAArch64TargetInfo();
	LLVMInitializeAArch64Target();
	LLVMInitializeAArch64TargetMC();
	return 0;
}
#endif

void BfIRCodeGen::StaticInit()
{
	LLVMInitializeX86TargetInfo();
	LLVMInitializeX86Target();
	LLVMInitializeX86TargetMC();
	LLVMInitializeX86AsmPrinter();
	LLVMInitializeX86AsmParser();
	LLVMInitializeX86Disassembler();

	LLVMInitializeARMTargetInfo();
	LLVMInitializeARMTarget();
	LLVMInitializeARMTargetMC();
	LLVMInitializeARMAsmPrinter();

	LLVMInitializeAArch64TargetInfo();
	LLVMInitializeAArch64Target();
	LLVMInitializeAArch64TargetMC();
	LLVMInitializeAArch64AsmPrinter();
	//LLVMInitializeAArch64Parser();
	//LLVMInitializeX86Disassembler();

	LLVMInitializeWebAssemblyTargetInfo();
	LLVMInitializeWebAssemblyTarget();
	LLVMInitializeWebAssemblyTargetMC();
	LLVMInitializeWebAssemblyAsmPrinter();
	//LLVMInitializeWebAssemblyAsmParser();
	LLVMInitializeWebAssemblyDisassembler();
}