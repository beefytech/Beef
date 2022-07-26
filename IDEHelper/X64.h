#pragma once

#pragma warning(disable:4141)
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

class X64CPU;

enum X64CPURegister
{
	X64Reg_None = -1,

	// integer general registers (DO NOT REORDER THESE; must exactly match DbgModule X64 register mappings)
	X64Reg_RAX = 0,
	X64Reg_RDX,
	X64Reg_RCX,
	X64Reg_RBX,
	X64Reg_RSI,
	X64Reg_RDI,
	X64Reg_RBP,
	X64Reg_RSP,
	X64Reg_R8,
	X64Reg_R9,
	X64Reg_R10,
	X64Reg_R11,
	X64Reg_R12,
	X64Reg_R13,
	X64Reg_R14,
	X64Reg_R15,
	X64Reg_RIP,
	X64Reg_EFL,
	X64Reg_GS,

	X64Reg_EAX,
	X64Reg_EDX,
	X64Reg_ECX,
	X64Reg_EBX,
	X64Reg_ESI,
	X64Reg_EDI,
	X64Reg_R8D,
	X64Reg_R9D,
	X64Reg_R10D,
	X64Reg_R11D,
	X64Reg_R12D,
	X64Reg_R13D,
	X64Reg_R14D,
	X64Reg_R15D,

	X64Reg_AX,
	X64Reg_DX,
	X64Reg_CX,
	X64Reg_BX,
	X64Reg_SI,
	X64Reg_DI,
	X64Reg_R8W,
	X64Reg_R9W,
	X64Reg_R10W,
	X64Reg_R11W,
	X64Reg_R12W,
	X64Reg_R13W,
	X64Reg_R14W,
	X64Reg_R15W,

	X64Reg_AL,
	X64Reg_DL,
	X64Reg_CL,
	X64Reg_BL,
	X64Reg_AH,
	X64Reg_DH,
	X64Reg_CH,
	X64Reg_BH,
	X64Reg_SIL,
	X64Reg_DIL,
	X64Reg_R8B,
	X64Reg_R9B,
	X64Reg_R10B,
	X64Reg_R11B,
	X64Reg_R12B,
	X64Reg_R13B,
	X64Reg_R14B,
	X64Reg_R15B,

	// fpu registers (80-bit st(#) reg stack)
	X64Reg_FPST0,
	X64Reg_FPST1,
	X64Reg_FPST2,
	X64Reg_FPST3,
	X64Reg_FPST4,
	X64Reg_FPST5,
	X64Reg_FPST6,
	X64Reg_FPST7,

	// mmx registers (alias to the low 64 bits of the matching st(#) register)
	X64Reg_MM0,
	X64Reg_MM1,
	X64Reg_MM2,
	X64Reg_MM3,
	X64Reg_MM4,
	X64Reg_MM5,
	X64Reg_MM6,
	X64Reg_MM7,

	// XMM registers, using first element as a double
	X64Reg_XMM0_f64,
	X64Reg_XMM1_f64,
	X64Reg_XMM2_f64,
	X64Reg_XMM3_f64,
	X64Reg_XMM4_f64,
	X64Reg_XMM5_f64,
	X64Reg_XMM6_f64,
	X64Reg_XMM7_f64,
	X64Reg_XMM8_f64,
	X64Reg_XMM9_f64,
	X64Reg_XMM10_f64,
	X64Reg_XMM11_f64,
	X64Reg_XMM12_f64,
	X64Reg_XMM13_f64,
	X64Reg_XMM14_f64,
	X64Reg_XMM15_f64,

	// XMM registers, using first element as a float
	X64Reg_XMM0_f32,
	X64Reg_XMM1_f32,
	X64Reg_XMM2_f32,
	X64Reg_XMM3_f32,
	X64Reg_XMM4_f32,
	X64Reg_XMM5_f32,
	X64Reg_XMM6_f32,
	X64Reg_XMM7_f32,
	X64Reg_XMM8_f32,
	X64Reg_XMM9_f32,
	X64Reg_XMM10_f32,
	X64Reg_XMM11_f32,
	X64Reg_XMM12_f32,
	X64Reg_XMM13_f32,
	X64Reg_XMM14_f32,
	X64Reg_XMM15_f32,

	// xmm registers
	X64Reg_XMM00,
	X64Reg_XMM01,
	X64Reg_XMM02,
	X64Reg_XMM03,
	X64Reg_XMM10,
	X64Reg_XMM11,
	X64Reg_XMM12,
	X64Reg_XMM13,
	X64Reg_XMM20,
	X64Reg_XMM21,
	X64Reg_XMM22,
	X64Reg_XMM23,
	X64Reg_XMM30,
	X64Reg_XMM31,
	X64Reg_XMM32,
	X64Reg_XMM33,
	X64Reg_XMM40,
	X64Reg_XMM41,
	X64Reg_XMM42,
	X64Reg_XMM43,
	X64Reg_XMM50,
	X64Reg_XMM51,
	X64Reg_XMM52,
	X64Reg_XMM53,
	X64Reg_XMM60,
	X64Reg_XMM61,
	X64Reg_XMM62,
	X64Reg_XMM63,
	X64Reg_XMM70,
	X64Reg_XMM71,
	X64Reg_XMM72,
	X64Reg_XMM73,
	X64Reg_XMM80,
	X64Reg_XMM81,
	X64Reg_XMM82,
	X64Reg_XMM83,
	X64Reg_XMM90,
	X64Reg_XMM91,
	X64Reg_XMM92,
	X64Reg_XMM93,
	X64Reg_XMM10_0,
	X64Reg_XMM10_1,
	X64Reg_XMM10_2,
	X64Reg_XMM10_3,
	X64Reg_XMM11_0,
	X64Reg_XMM11_1,
	X64Reg_XMM11_2,
	X64Reg_XMM11_3,
	X64Reg_XMM12_0,
	X64Reg_XMM12_1,
	X64Reg_XMM12_2,
	X64Reg_XMM12_3,
	X64Reg_XMM13_0,
	X64Reg_XMM13_1,
	X64Reg_XMM13_2,
	X64Reg_XMM13_3,
	X64Reg_XMM14_0,
	X64Reg_XMM14_1,
	X64Reg_XMM14_2,
	X64Reg_XMM14_3,
	X64Reg_XMM15_0,
	X64Reg_XMM15_1,
	X64Reg_XMM15_2,
	X64Reg_XMM15_3,

	// xmm 128-bit macro-registers (no stored data with these, they're symbolic for use as higher-level constructs, aliases to the individual xmm regs above)
	X64Reg_M128_XMM0,
	X64Reg_M128_XMM1,
	X64Reg_M128_XMM2,
	X64Reg_M128_XMM3,
	X64Reg_M128_XMM4,
	X64Reg_M128_XMM5,
	X64Reg_M128_XMM6,
	X64Reg_M128_XMM7,
	X64Reg_M128_XMM8,
	X64Reg_M128_XMM9,
	X64Reg_M128_XMM10,
	X64Reg_M128_XMM11,
	X64Reg_M128_XMM12,
	X64Reg_M128_XMM13,
	X64Reg_M128_XMM14,
	X64Reg_M128_XMM15,

	// flags boolean pseudo-registers (aliases to individual flags in EFL)
	X64Reg_FLAG_CF_CARRY,
	X64Reg_FLAG_PF_PARITY,
	X64Reg_FLAG_AF_ADJUST,
	X64Reg_FLAG_ZF_ZERO,
	X64Reg_FLAG_SF_SIGN,
	X64Reg_FLAG_IF_INTERRUPT,
	X64Reg_FLAG_DF_DIRECTION,
	X64Reg_FLAG_OF_OVERFLOW,

	// category macro-registers
	X64Reg_CAT_ALLREGS,
	X64Reg_CAT_IREGS,
	X64Reg_CAT_FPREGS,
	X64Reg_CAT_MMREGS,
	X64Reg_CAT_XMMREGS,
	X64Reg_CAT_FLAGS,

	X64Reg_COUNT,

	X64Reg_INTREG_FIRST = X64Reg_RAX,
	X64Reg_INTREG_LAST = X64Reg_GS,
	X64Reg_INTREG_COUNT = (X64Reg_INTREG_LAST - X64Reg_INTREG_FIRST) + 1,
	X64Reg_FPSTREG_FIRST = X64Reg_FPST0,
	X64Reg_FPSTREG_LAST = X64Reg_FPST7,
	X64Reg_FPSTREG_COUNT = (X64Reg_FPSTREG_LAST - X64Reg_FPSTREG_FIRST) + 1,
	X64Reg_MMREG_FIRST = X64Reg_MM0,
	X64Reg_MMREG_LAST = X64Reg_MM7,
	X64Reg_MMREG_COUNT = (X64Reg_MMREG_LAST - X64Reg_MMREG_FIRST) + 1,
	X64Reg_XMMREG_FIRST = X64Reg_XMM00,
	X64Reg_XMMREG_LAST = X64Reg_XMM15_3,
	X64Reg_XMMREG_SINGLE_COUNT = (X64Reg_XMMREG_LAST - X64Reg_XMMREG_FIRST) + 1,
	X64Reg_M128_XMMREG_FIRST = X64Reg_M128_XMM0,
	X64Reg_M128_XMMREG_LAST = X64Reg_M128_XMM15,
	X64Reg_M128_XMMREG_COUNT = (X64Reg_M128_XMMREG_LAST - X64Reg_M128_XMMREG_FIRST) + 1,
	X64Reg_FLAG_FIRST = X64Reg_FLAG_CF_CARRY,
	X64Reg_FLAG_LAST = X64Reg_FLAG_OF_OVERFLOW,
	X64Reg_FLAG_COUNT = (X64Reg_FLAG_LAST - X64Reg_FLAG_FIRST) + 1,
	X64Reg_CAT_FIRST = X64Reg_CAT_ALLREGS,
	X64Reg_CAT_LAST = X64Reg_CAT_FLAGS,
	X64Reg_CAT_COUNT = (X64Reg_CAT_LAST - X64Reg_CAT_FIRST) + 1,
};

#pragma pack(push, 1)
class X64CPURegisters
{
public:
	static const int kNumIntRegs = 19;
	static const int kNumFpMmRegs = 8;
	static const int kNumXmmRegs = 16;

	struct IntRegs
	{
		int64 rax;
		int64 rdx;
		int64 rcx;
		int64 rbx;
		int64 rsi;
		int64 rdi;
		uint64 rbp;
		uint64 rsp;
		int64 r8;
		int64 r9;
		int64 r10;
		int64 r11;
		int64 r12;
		int64 r13;
		int64 r14;
		int64 r15;
		uint64 rip;
		int64 efl; // Really just int32

		int64 gs; // For TLS
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
		int64 mIntRegsArray[kNumIntRegs];
	};
	FpMmReg mFpMmRegsArray[kNumFpMmRegs];
	union
	{
		XmmReg mXmmRegsArray[kNumXmmRegs];
		XmmDReg mXmmDRegsArray[kNumXmmRegs];
		XmmI32Reg mXmmI32RegsARray[kNumXmmRegs];
		XmmI64Reg mXmmI64RegsARray[kNumXmmRegs];
	};

	X64CPURegisters()
	{
		memset(&mIntRegs, 0, sizeof(mIntRegs));
		memset(mFpMmRegsArray, 0, sizeof(mFpMmRegsArray));
		memset(mXmmRegsArray, 0, sizeof(mXmmRegsArray));
	}

	inline uint64 GetPC() { return mIntRegs.rip; }
	inline uint64* GetPCRegisterRef() { return &mIntRegs.rip; }

	inline uint64 GetSP() { return mIntRegs.rsp; }
	inline uint64* GetSPRegisterRef() { return &mIntRegs.rsp; }

	inline uint64 GetBP() { return mIntRegs.rbp; }
	inline uint64* GetBPRegisterRef() { return &mIntRegs.rbp; }

	inline XmmReg* GetXMMRegRef(int regNum)
	{
		return &mXmmRegsArray[regNum];
	}

	inline int64* GetExceptionRegisterRef(int regNum)
	{
		int regRemapping[] =
			{ X64Reg_RAX,
				X64Reg_RCX,
				X64Reg_RDX,
				X64Reg_RBX,
				X64Reg_RSP,
				X64Reg_RBP,
				X64Reg_RSI,
				X64Reg_RDI,
				X64Reg_R8,
				X64Reg_R9,
				X64Reg_R10,
				X64Reg_R11,
				X64Reg_R12,
				X64Reg_R13,
				X64Reg_R14,
				X64Reg_R15,
				X64Reg_RIP,
				X64Reg_EFL };
		return &mIntRegsArray[regRemapping[regNum]];
	}

	static int GetFlagBitForRegister(int flagRegister)
	{
		int flagBit = -1;
		switch (flagRegister)
		{
		case X64Reg_FLAG_CF_CARRY: flagBit = 0; break;
		case X64Reg_FLAG_PF_PARITY: flagBit = 2; break;
		case X64Reg_FLAG_AF_ADJUST: flagBit = 4; break;
		case X64Reg_FLAG_ZF_ZERO: flagBit = 6; break;
		case X64Reg_FLAG_SF_SIGN: flagBit = 7; break;
		case X64Reg_FLAG_IF_INTERRUPT: flagBit = 9; break;
		case X64Reg_FLAG_DF_DIRECTION: flagBit = 10; break;
		case X64Reg_FLAG_OF_OVERFLOW: flagBit = 11; break;
		default: break;
		}
		return flagBit;
	}

	static int GetCompositeRegister(int regNum)
	{
		if ((regNum >= X64Reg_XMMREG_FIRST) && (regNum <= X64Reg_XMMREG_LAST))
			return X64Reg_M128_XMMREG_FIRST + ((regNum - X64Reg_XMM00) / 4);
		return regNum;
	}

	static const char* sCPURegisterNames[];
	static const char* GetRegisterName(int regNum)
	{
		return sCPURegisterNames[regNum + 1];
	}
};
#pragma pack(pop)

class X64Instr
{
public:
	X64CPU* mX64;
	llvm::MCInst mMCInst;
	uint64 mAddress;
	int mSize;
	llvm::SmallVector<char, 64> mAnnotationStr;
	llvm::raw_svector_ostream mAnnotationStream;
	//static uint8 sRegForm[];

	X64Instr() :
		mAnnotationStream(mAnnotationStr)
	{
		mX64 = NULL;
		mAddress = 0;
		mSize = 0;
	}

	int GetLength();
	bool IsBranch();
	bool IsCall();
	bool IsRep(bool& isPrefixOnly);
	bool IsReturn();
	bool IsLoadAddress();
	bool GetIndexRegisterAndOffset(int* outRegister, int* outOffset); // IE: [ebp + 0x4]
	bool GetImmediate(uint64* outImm);
	int GetJmpState(int flags);
	void MarkRegsUsed(Array<RegForm>& regsUsed, bool overrideForm);

	uint64 GetTarget(Debugger* debugger = NULL, X64CPURegisters* registers = NULL);
	bool PartialSimulate(Debugger* debugger, X64CPURegisters* registers);
};

class X64CPU
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
	X64CPU();
	~X64CPU();

	bool Decode(uint64 address, DbgModuleMemoryCache* memoryCache, X64Instr* inst);
	bool Decode(uint64 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, X64Instr* inst);
	uint64 DecodeThunk(uint64 address, DbgModuleMemoryCache* memoryCache);

	bool IsReturnInstruction(X64Instr* inst);
	String InstructionToString(X64Instr* inst, uint64 addr);

	void GetNextPC(uint64 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, uint32* regs, uint64 nextPCs[2]);
	DbgBreakKind GetDbgBreakKind(uint64 address, DbgModuleMemoryCache* memoryCache, int64* regs, int64* outObjectPtr);

	int GetOpcodesForMnemonic(const StringImpl& mnemonic, Array<int>& outOpcodes);
	void GetClobbersForMnemonic(const StringImpl& mnemonic, int argCount, Array<int>& outImplicitClobberRegNums, int& outClobberArgCount, bool& outMayClobberMem);
	bool ParseInlineAsmInstructionLLVM(const StringImpl&asmInst, String& outError);
};

#ifdef BF_DBG_64
#define CPURegisters X64CPURegisters
#define CPU X64CPU
typedef X64Instr CPUInst;
#define DwarfReg_SP X64Reg_RSP
#define CPUReg_CAT_ALLREGS X64Reg_CAT_ALLREGS
#define CPUReg_CAT_IREGS X64Reg_CAT_IREGS
#define CPUReg_CAT_FPREGS X64Reg_CAT_FPREGS
#define CPUReg_CAT_MMREGS X64Reg_CAT_MMREGS
#define CPUReg_CAT_XMMREGS X64Reg_CAT_XMMREGS
#define CPUReg_CAT_FLAGS X64Reg_CAT_FLAGS
#define CPUReg_FLAG_CF_CARRY X64Reg_FLAG_CF_CARRY
#define CPUReg_FLAG_PF_PARITY X64Reg_FLAG_PF_PARITY
#define CPUReg_FLAG_AF_ADJUST X64Reg_FLAG_AF_ADJUST
#define CPUReg_FLAG_ZF_ZERO X64Reg_FLAG_ZF_ZERO
#define CPUReg_FLAG_SF_SIGN X64Reg_FLAG_SF_SIGN
#define CPUReg_FLAG_IF_INTERRUPT X64Reg_FLAG_IF_INTERRUPT
#define CPUReg_FLAG_DF_DIRECTION X64Reg_FLAG_DF_DIRECTION
#define CPUReg_FLAG_OF_OVERFLOW X64Reg_FLAG_OF_OVERFLOW
#define CPUReg_XMMREG_FIRST X64Reg_XMMREG_FIRST
#define CPUReg_XMMREG_LAST X64Reg_XMMREG_LAST
#define CPUReg_M128_XMMREG_FIRST X64Reg_M128_XMMREG_FIRST
#define CPUReg_M128_XMMREG_LAST X64Reg_M128_XMMREG_LAST
#define CPUReg_MMREG_FIRST X64Reg_MMREG_FIRST
#define CPUReg_FPSTREG_FIRST X64Reg_FPSTREG_FIRST
#endif

static_assert(X64Reg_FPSTREG_COUNT == X64Reg_MMREG_COUNT, "CPUReg register count mismatch"); // these alias to the same regs
static_assert(sizeof(X64CPURegisters::IntRegs) == X64CPURegisters::kNumIntRegs*sizeof(int64), "X64CPURegisters size mismatch");
static_assert(sizeof(X64CPURegisters) == (X64Reg_INTREG_COUNT*sizeof(int64) + X64Reg_MMREG_COUNT*sizeof(X64CPURegisters::FpMmReg) + (X64Reg_XMMREG_SINGLE_COUNT / 4)*sizeof(X64CPURegisters::XmmReg)), "X64CPURegisters size mismatch");
static_assert(offsetof(X64CPURegisters, mIntRegs) == 0, "X64CPURegisters layout mismatch");
static_assert(offsetof(X64CPURegisters::IntRegs, rax) == 0, "X64CPURegisters layout mismatch");

NS_BF_END