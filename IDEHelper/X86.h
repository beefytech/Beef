#pragma once

#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4624)
#pragma warning(disable:4996)
#pragma warning(disable:4267)
#pragma warning(disable:4244)

#include "DebugCommon.h"
#include "BeefySysLib/Common.h"
#include <sstream>
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include "CPU.h"
#include "Debugger.h"

namespace llvm
{
	class MCSubtargetInfo;
	class MCRegisterInfo;
	class MCDisassembler;
	class MCInst;
	class MCInstrInfo;
	class MCAsmInfo;
	class MCContext;
	class MCObjectFileInfo;
}

NS_BF_BEGIN

class X86CPU;

enum X86CPURegister
{
	// integer general registers (DO NOT REORDER THESE; must exactly match DbgModule x86 register mappings)
	X86Reg_EAX = 0,
	X86Reg_ECX,
	X86Reg_EDX,
	X86Reg_EBX,
	X86Reg_ESP,
	X86Reg_EBP,
	X86Reg_ESI,
	X86Reg_EDI,
	X86Reg_EIP,
	X86Reg_EFL,

	// fpu registers (80-bit st(#) reg stack)
	X86Reg_FPST0,
	X86Reg_FPST1,
	X86Reg_FPST2,
	X86Reg_FPST3,
	X86Reg_FPST4,
	X86Reg_FPST5,
	X86Reg_FPST6,
	X86Reg_FPST7,

	// mmx registers (alias to the low 64 bits of the matching st(#) register)
	X86Reg_MM0,
	X86Reg_MM1,
	X86Reg_MM2,
	X86Reg_MM3,
	X86Reg_MM4,
	X86Reg_MM5,
	X86Reg_MM6,
	X86Reg_MM7,

	// xmm registers
	X86Reg_XMM00,
	X86Reg_XMM01,
	X86Reg_XMM02,
	X86Reg_XMM03,
	X86Reg_XMM10,
	X86Reg_XMM11,
	X86Reg_XMM12,
	X86Reg_XMM13,
	X86Reg_XMM20,
	X86Reg_XMM21,
	X86Reg_XMM22,
	X86Reg_XMM23,
	X86Reg_XMM30,
	X86Reg_XMM31,
	X86Reg_XMM32,
	X86Reg_XMM33,
	X86Reg_XMM40,
	X86Reg_XMM41,
	X86Reg_XMM42,
	X86Reg_XMM43,
	X86Reg_XMM50,
	X86Reg_XMM51,
	X86Reg_XMM52,
	X86Reg_XMM53,
	X86Reg_XMM60,
	X86Reg_XMM61,
	X86Reg_XMM62,
	X86Reg_XMM63,
	X86Reg_XMM70,
	X86Reg_XMM71,
	X86Reg_XMM72,
	X86Reg_XMM73,

	// xmm 128-bit macro-registers (no stored data with these, they're symbolic for use as higher-level constructs, aliases to the individual xmm regs above)
	X86Reg_M128_XMM0,
	X86Reg_M128_XMM1,
	X86Reg_M128_XMM2,
	X86Reg_M128_XMM3,
	X86Reg_M128_XMM4,
	X86Reg_M128_XMM5,
	X86Reg_M128_XMM6,
	X86Reg_M128_XMM7,

	// flags boolean pseudo-registers (aliases to individual flags in EFL)
	X86Reg_FLAG_CF_CARRY,
	X86Reg_FLAG_PF_PARITY,
	X86Reg_FLAG_AF_ADJUST,
	X86Reg_FLAG_ZF_ZERO,
	X86Reg_FLAG_SF_SIGN,
	X86Reg_FLAG_IF_INTERRUPT,
	X86Reg_FLAG_DF_DIRECTION,
	X86Reg_FLAG_OF_OVERFLOW,

	// category macro-registers
	X86Reg_CAT_ALLREGS,
	X86Reg_CAT_IREGS,
	X86Reg_CAT_FPREGS,
	X86Reg_CAT_MMREGS,
	X86Reg_CAT_XMMREGS,
	X86Reg_CAT_FLAGS,

	X86Reg_COUNT,

	X86Reg_INTREG_FIRST = X86Reg_EAX,
	X86Reg_INTREG_LAST = X86Reg_EFL,
	X86Reg_INTREG_COUNT = (X86Reg_INTREG_LAST - X86Reg_INTREG_FIRST) + 1,
	X86Reg_FPSTREG_FIRST = X86Reg_FPST0,
	X86Reg_FPSTREG_LAST = X86Reg_FPST7,
	X86Reg_FPSTREG_COUNT = (X86Reg_FPSTREG_LAST - X86Reg_FPSTREG_FIRST) + 1,
	X86Reg_MMREG_FIRST = X86Reg_MM0,
	X86Reg_MMREG_LAST = X86Reg_MM7,
	X86Reg_MMREG_COUNT = (X86Reg_MMREG_LAST - X86Reg_MMREG_FIRST) + 1,
	X86Reg_XMMREG_FIRST = X86Reg_XMM00,
	X86Reg_XMMREG_LAST = X86Reg_XMM73,
	X86Reg_XMMREG_SINGLE_COUNT = (X86Reg_XMMREG_LAST - X86Reg_XMMREG_FIRST) + 1,
	X86Reg_M128_XMMREG_FIRST = X86Reg_M128_XMM0,
	X86Reg_M128_XMMREG_LAST = X86Reg_M128_XMM7,
	X86Reg_M128_XMMREG_COUNT = (X86Reg_M128_XMMREG_LAST - X86Reg_M128_XMMREG_FIRST) + 1,
	X86Reg_FLAG_FIRST = X86Reg_FLAG_CF_CARRY,
	X86Reg_FLAG_LAST = X86Reg_FLAG_OF_OVERFLOW,
	X86Reg_FLAG_COUNT = (X86Reg_FLAG_LAST - X86Reg_FLAG_FIRST) + 1,
	X86Reg_CAT_FIRST = X86Reg_CAT_ALLREGS,
	X86Reg_CAT_LAST = X86Reg_CAT_FLAGS,
	X86Reg_CAT_COUNT = (X86Reg_CAT_LAST - X86Reg_CAT_FIRST) + 1,
};

#pragma pack(push, 1)
class X86CPURegisters
{
public:
	static const int kNumIntRegs = 10;
	static const int kNumFpMmRegs = 8;
	static const int kNumXmmRegs = 8;

	struct IntRegs
	{
		int32 eax;
		int32 ecx;
		int32 edx;
		int32 ebx;
		uint32 esp;
		uint32 ebp;
		int32 esi;
		int32 edi;
		uint32 eip;
		int32 efl;
	};

	struct Fp80Reg
	{
		uint8 fp80[10]; // 80-bit FP value, must be down-converted to use as a double
	};
	union FpMmReg
	{
		Fp80Reg fp;
		int64 mm;
	};

	struct XmmReg
	{
		float f[4];
	};

	struct XmmDReg
	{
		double d[2];
	};	

	struct XmmI32Reg
	{
		int32 i[4];
	};

	struct XmmI64Reg
	{
		int64 i[2];
	};

	union
	{
		IntRegs mIntRegs;
		int32 mIntRegsArray[kNumIntRegs];
	};
	FpMmReg mFpMmRegsArray[kNumFpMmRegs];
	union
	{
		XmmReg mXmmRegsArray[kNumXmmRegs];
		XmmDReg mXmmDRegsArray[kNumXmmRegs];
		XmmI32Reg mXmmI32RegsARray[kNumXmmRegs];
		XmmI64Reg mXmmI64RegsARray[kNumXmmRegs];
	};

	X86CPURegisters()
	{
		memset(&mIntRegs, 0, sizeof(mIntRegs));
		memset(mFpMmRegsArray, 0, sizeof(mFpMmRegsArray));
		memset(mXmmRegsArray, 0, sizeof(mXmmRegsArray));
	}

	inline uint32 GetPC() { return mIntRegs.eip; }
	inline uint32* GetPCRegisterRef() { return &mIntRegs.eip; }

	inline uint32 GetSP() { return mIntRegs.esp; }
	inline uint32* GetSPRegisterRef() { return &mIntRegs.esp; }

	inline uint32 GetBP() { return mIntRegs.ebp; }
	inline uint32* GetBPRegisterRef() { return &mIntRegs.ebp; }

	inline XmmReg* GetXMMRegRef(int regNum)
	{
		return &mXmmRegsArray[regNum];
	}

	inline int32* GetExceptionRegisterRef(int regNum)
	{
		return NULL;
	}

	static int GetFlagBitForRegister(int flagRegister)
	{
		int flagBit = -1;
		switch (flagRegister)
		{
		case X86Reg_FLAG_CF_CARRY: flagBit = 0; break;
		case X86Reg_FLAG_PF_PARITY: flagBit = 2; break;
		case X86Reg_FLAG_AF_ADJUST: flagBit = 4; break;
		case X86Reg_FLAG_ZF_ZERO: flagBit = 6; break;
		case X86Reg_FLAG_SF_SIGN: flagBit = 7; break;
		case X86Reg_FLAG_IF_INTERRUPT: flagBit = 9; break;
		case X86Reg_FLAG_DF_DIRECTION: flagBit = 10; break;
		case X86Reg_FLAG_OF_OVERFLOW: flagBit = 11; break;
		default: break;
		}
		return flagBit;
	}

	static int GetCompositeRegister(int regNum)
	{
		if ((regNum >= X86Reg_XMMREG_FIRST) && (regNum <= X86Reg_XMMREG_LAST))
			return X86Reg_M128_XMMREG_FIRST + ((regNum - X86Reg_XMM00) / 4);
		return regNum;
	}

	static const char* sCPURegisterNames[];
	static const char* GetRegisterName(int regNum)
	{
		return sCPURegisterNames[regNum];
	}
};
#pragma pack(pop)

class X86Instr
{
public:
	X86CPU* mX86;
	llvm::MCInst mMCInst;
	uint32 mAddress;
	int mSize;
	llvm::SmallVector<char, 64> mAnnotationStr;
	llvm::raw_svector_ostream mAnnotationStream;
	static uint8 sRegForm[];

	X86Instr() : 
		mAnnotationStream(mAnnotationStr)
	{
		mX86 = NULL;
		mAddress = 0;
		mSize = 0;
	}

	int GetLength();	
	bool StackAdjust(uint32& adjust);
	bool IsBranch();
	bool IsCall();
	bool IsRep(bool& isPrefixOnly);
	bool IsReturn();
	bool IsLoadAddress();
	bool GetIndexRegisterAndOffset(int* outRegister, int* outOffset); // IE: [ebp + 0x4]
	bool GetImmediate(uint32* outImm);
	int GetJmpState(int flags);
	void MarkRegsUsed(Array<RegForm>& regsUsed, bool overrideForm);

	uint32 GetTarget(Debugger* debugger = NULL, X86CPURegisters* registers = NULL);
	bool PartialSimulate(Debugger* debugger, X86CPURegisters* registers);
};

class X86CPU
{
public:
	typedef std::multimap<String, int> StringToOpcodeMap;

	llvm::MCContext* mMCContext;
	llvm::MCObjectFileInfo* mMCObjectFileInfo;
	llvm::MCSubtargetInfo* mSubtargetInfo;
	llvm::MCRegisterInfo* mRegisterInfo;
	llvm::MCAsmInfo* mAsmInfo;
	llvm::MCInstPrinter* mInstPrinter;
	llvm::MCDisassembler* mDisAsm;
	llvm::MCInstrInfo* mInstrInfo;
	String mWarningString;
	std::stringstream mWarningStream;
	String mCommentString;
	std::stringstream mCommentStream;
	StringToOpcodeMap mStringToOpcodeMap;

public:
	X86CPU();
	~X86CPU();

	bool Decode(uint32 address, DbgModuleMemoryCache* memoryCache, X86Instr* inst);
	bool Decode(uint32 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, X86Instr* inst);
	uint32 DecodeThunk(uint32 address, DbgModuleMemoryCache* memoryCache) { return 0; }

	bool IsReturnInstruction(X86Instr* inst);	
	String InstructionToString(X86Instr* inst, uint32 addr);

	void GetNextPC(uint32 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, uint32* regs, uint32 nextPCs[2]);
	bool IsObjectAccessBreak(uint32 address, DbgModuleMemoryCache* memoryCache, int32* regs, int32* outObjectPtr);

	int GetOpcodesForMnemonic(const StringImpl& mnemonic, Array<int>& outOpcodes);
	void GetClobbersForMnemonic(const StringImpl& mnemonic, int argCount, Array<int>& outImplicitClobberRegNums, int& outClobberArgCount, bool& outMayClobberMem);
	bool ParseInlineAsmInstructionLLVM(const StringImpl&asmInst, String& outError);
};

#ifdef BF_DBG_32
#define CPURegisters X86CPURegisters
#define CPU X86CPU
typedef X86Instr CPUInst;
#define DwarfReg_SP X86Reg_ESP
#define CPUReg_CAT_ALLREGS X86Reg_CAT_ALLREGS
#define CPUReg_CAT_IREGS X86Reg_CAT_IREGS
#define CPUReg_CAT_FPREGS X86Reg_CAT_FPREGS
#define CPUReg_CAT_MMREGS X86Reg_CAT_MMREGS
#define CPUReg_CAT_XMMREGS X86Reg_CAT_XMMREGS
#define CPUReg_CAT_FLAGS X86Reg_CAT_FLAGS
#define CPUReg_FLAG_CF_CARRY X86Reg_FLAG_CF_CARRY
#define CPUReg_FLAG_PF_PARITY X86Reg_FLAG_PF_PARITY
#define CPUReg_FLAG_AF_ADJUST X86Reg_FLAG_AF_ADJUST
#define CPUReg_FLAG_ZF_ZERO X86Reg_FLAG_ZF_ZERO
#define CPUReg_FLAG_SF_SIGN X86Reg_FLAG_SF_SIGN
#define CPUReg_FLAG_IF_INTERRUPT X86Reg_FLAG_IF_INTERRUPT
#define CPUReg_FLAG_DF_DIRECTION X86Reg_FLAG_DF_DIRECTION
#define CPUReg_FLAG_OF_OVERFLOW X86Reg_FLAG_OF_OVERFLOW
#define CPUReg_XMMREG_FIRST X86Reg_XMMREG_FIRST
#define CPUReg_XMMREG_LAST X86Reg_XMMREG_LAST
#define CPUReg_M128_XMMREG_FIRST X86Reg_M128_XMMREG_FIRST
#define CPUReg_M128_XMMREG_LAST X86Reg_M128_XMMREG_LAST
#define CPUReg_MMREG_FIRST X86Reg_MMREG_FIRST
#define CPUReg_FPSTREG_FIRST X86Reg_FPSTREG_FIRST
#endif

static_assert(X86Reg_FPSTREG_COUNT == X86Reg_MMREG_COUNT, "CPUReg register count mismatch"); // these alias to the same regs
static_assert(sizeof(X86CPURegisters::IntRegs) == X86CPURegisters::kNumIntRegs*sizeof(int32), "X86CPURegisters size mismatch");
static_assert(sizeof(X86CPURegisters) == (X86Reg_INTREG_COUNT*sizeof(int32) + X86Reg_MMREG_COUNT*sizeof(X86CPURegisters::FpMmReg) + (X86Reg_XMMREG_SINGLE_COUNT / 4)*sizeof(X86CPURegisters::XmmReg)), "X86CPURegisters size mismatch");
static_assert(offsetof(X86CPURegisters, mIntRegs) == 0, "X86CPURegisters layout mismatch");
static_assert(offsetof(X86CPURegisters::IntRegs, eax) == 0, "X86CPURegisters layout mismatch");

NS_BF_END