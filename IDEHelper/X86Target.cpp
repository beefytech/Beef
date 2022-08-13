#pragma warning(disable:4996)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4005)
#pragma warning(disable:4267)

#include "X86Target.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/TargetRegistry.h"
//#include "llvm/Support/MemoryObject.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetMachine.h"
//#include "llvm/Target/TargetSubtargetInfo.h"
//#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
//#include "llvm/MC/MCTargetAsmParser.h"
#include "llvm/MC/MCObjectFileInfo.h"
//#include "X86/Disassembler/X86Disassembler.h"
#include "X86/Disassembler/X86DisassemblerDecoder.h"
#include "X86/MCTargetDesc/X86MCTargetDesc.h"
#include "X86/MCTargetDesc/X86BaseInfo.h"
#include "llvm/Support/TargetSelect.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm-c/Initialization.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

X86Target* Beefy::gX86Target = NULL;

using namespace llvm;

extern "C" void LLVM_NATIVE_TARGETINFO();
extern "C" void LLVM_NATIVE_TARGET();
extern "C" void LLVM_NATIVE_TARGETMC();
extern "C" void LLVM_NATIVE_ASMPARSER();
extern "C" void LLVM_NATIVE_ASMPRINTER();
extern "C" void LLVM_NATIVE_DISASSEMBLER();

X86Target::X86Target()
{
	llvm::InitializeNativeTarget();
  	llvm::InitializeNativeTargetAsmPrinter();
  	llvm::InitializeNativeTargetAsmParser();
  	llvm::InitializeNativeTargetDisassembler();

	/*LLVM_NATIVE_TARGETINFO();
	LLVM_NATIVE_TARGET();
	LLVM_NATIVE_TARGETMC();
	LLVM_NATIVE_ASMPARSER();
	LLVM_NATIVE_ASMPRINTER();
	LLVM_NATIVE_DISASSEMBLER();*/

#ifdef PLATFORM_LINUX
	/*for (Target *T : {&getTheX86_32Target(), &getTheX86_64Target()}) {
		RegisterELFStreamer(*T, create);
		RegisterMachOStreamer();
		RegisterWasmStreamer();
	}*/
#endif

	/*LLVMInitializeX86Target();
	LLVMInitializeX86TargetMC();
	LLVMInitializeX86AsmParser();
	LLVMInitializeX86Disassembler();
	LLVMInitializeX86TargetInfo();
	LLVMInitializeX86AsmPrinter();*/

	PassRegistry *Registry = PassRegistry::getPassRegistry();
	initializeCore(*Registry);
	initializeCodeGen(*Registry);
	initializeLoopStrengthReducePass(*Registry);
	initializeLowerIntrinsicsPass(*Registry);
	initializeUnreachableBlockElimLegacyPassPass(*Registry);

	mX86CPU = new X86CPU();
	mX64CPU = new X64CPU();
}

X86Target::~X86Target()
{
	delete mX86CPU;
	delete mX64CPU;
}