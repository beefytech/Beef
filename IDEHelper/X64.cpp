#pragma warning(push)
#pragma warning(disable:4996)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4005)
#pragma warning(disable:4267)
#pragma warning(disable:4146)

#include "X64.h"
#include "X86.h"
#include <assert.h>
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/IR/InlineAsm.h"
//#include "llvm/Support/MemoryObject.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetMachine.h"
//#include "llvm/Target/TargetSubtargetInfo.h"
//#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "X86/Disassembler/X86DisassemblerDecoder.h"
#include "X86/MCTargetDesc/X86MCTargetDesc.h"
#include "X86/MCTargetDesc/X86BaseInfo.h"
#include "X86InstrInfo.h"
#include "BeefySysLib/util/HashSet.h"
#include "../../llvm/lib/Target/X86/TargetInfo/X86TargetInfo.h"

#pragma warning(pop)

#include "BeefySysLib/util/AllocDebug.h"

//#include "Support/TargetSelect.h"

//int arch_8086_disasm_instr(struct x86_instr *instr, addr_t pc, char *line, unsigned int max_line);

extern "C" void LLVMInitializeX86TargetMC();
extern "C" void LLVMInitializeX86Disassembler();

extern "C" void LLVMInitializeAArch64TargetMC();

USING_NS_BF;

const char* X64CPURegisters::sCPURegisterNames[] =
{
	"NONE",

	// integer general registers (DO NOT REORDER THESE; must exactly match DbgModule x86 register mappings)
	"RAX",
	"RDX",
	"RCX",
	"RBX",
	"RSI",
	"RDI",
	"RBP",
	"RSP",
	"R8",
	"R9",
	"R10",
	"R11",
	"R12",
	"R13",
	"R14",
	"R15",
	"RIP",
	"EFL",
	"GS",

	"EAX",
	"EDX",
	"ECX",
	"EBX",
	"ESI",
	"EDI",
	"R8D",
	"R9D",
	"R10D",
	"R11D",
	"R12D",
	"R13D",
	"R14D",
	"R15D",

	"AX",
	"DX",
	"CX",
	"BX",
	"SI",
	"DI",
	"R8W",
	"R9W",
	"R10W",
	"R11W",
	"R12W",
	"R13W",
	"R14W",
	"R15W",

	"AL",
	"DL",
	"CL",
	"BL",
	"AH",
	"DH",
	"CH",
	"BH",
	"SIL",
	"DIL",
	"R8B",
	"R9B",
	"R10B",
	"R11B",
	"R12B",
	"R13B",
	"R14B",
	"R15B",

	// fpu registers (80-bit st(#) reg stack)
	"FPST0",
	"FPST1",
	"FPST2",
	"FPST3",
	"FPST4",
	"FPST5",
	"FPST6",
	"FPST7",

	// mmx registers (alias to the low 64 bits of the matching st(#) register)
	"MM0",
	"MM1",
	"MM2",
	"MM3",
	"MM4",
	"MM5",
	"MM6",
	"MM7",

	"XMM0_f64",
	"XMM1_f64",
	"XMM2_f64",
	"XMM3_f64",
	"XMM4_f64",
	"XMM5_f64",
	"XMM6_f64",
	"XMM7_f64",
	"XMM8_f64",
	"XMM9_f64",
	"XMM10_f64",
	"XMM11_f64",
	"XMM12_f64",
	"XMM13_f64",
	"XMM14_f64",
	"XMM15_f64",

	"XMM0_f32",
	"XMM1_f32",
	"XMM2_f32",
	"XMM3_f32",
	"XMM4_f32",
	"XMM5_f32",
	"XMM6_f32",
	"XMM7_f32",
	"XMM8_f32",
	"XMM9_f32",
	"XMM10_f32",
	"XMM11_f32",
	"XMM12_f32",
	"XMM13_f32",
	"XMM14_f32",
	"XMM15_f32",

	// xmm registers
	"XMM00",
	"XMM01",
	"XMM02",
	"XMM03",
	"XMM10",
	"XMM11",
	"XMM12",
	"XMM13",
	"XMM20",
	"XMM21",
	"XMM22",
	"XMM23",
	"XMM30",
	"XMM31",
	"XMM32",
	"XMM33",
	"XMM40",
	"XMM41",
	"XMM42",
	"XMM43",
	"XMM50",
	"XMM51",
	"XMM52",
	"XMM53",
	"XMM60",
	"XMM61",
	"XMM62",
	"XMM63",
	"XMM70",
	"XMM71",
	"XMM72",
	"XMM73",
	"XMM80",
	"XMM81",
	"XMM82",
	"XMM83",
	"XMM90",
	"XMM91",
	"XMM92",
	"XMM93",
	"XMM100",
	"XMM101",
	"XMM102",
	"XMM103",
	"XMM110",
	"XMM111",
	"XMM112",
	"XMM13",
	"XMM120",
	"XMM121",
	"XMM122",
	"XMM123",
	"XMM130",
	"XMM131",
	"XMM132",
	"XMM133",
	"XMM140",
	"XMM141",
	"XMM142",
	"XMM143",
	"XMM150",
	"XMM151",
	"XMM152",
	"XMM153",

	// xmm 128-bit macro-registers (no stored data with these", they're symbolic for use as higher-level constructs", aliases to the individual xmm regs above)
	"XMM0",
	"XMM1",
	"XMM2",
	"XMM3",
	"XMM4",
	"XMM5",
	"XMM6",
	"XMM7",
	"XMM8",
	"XMM9",
	"XMM10",
	"XMM11",
	"XMM12",
	"XMM13",
	"XMM14",
	"XMM15",

	"CF_CARRY",
	"PF_PARITY",
	"AF_ADJUST",
	"ZF_ZERO",
	"SF_SIGN",
	"IF_INTERRUPT",
	"DF_DIRECTION",
	"OF_OVERFLOW",

	// category macro-registers
	"ALLREGS",
	"IREGS",
	"FPREGS",
	"MMREGS",
	"XMMREGS",
	"FLAGS",
};

using namespace Beefy;
using namespace llvm;

namespace Beefy
{
	//
	// The memory object created by LLVMDisasmInstruction().
	//
	//class DisasmMemoryObject : public MemoryObject
	//{
	//	uint8_t *Bytes;
	//	uint64_t Size;
	//	uint64_t BasePC;
	//public:
	//	DisasmMemoryObject(uint8_t *bytes, uint64_t size, uint64_t basePC) :
	//		Bytes(bytes), Size(size), BasePC(basePC) {}

	//	bool isValidAddress(uint64_t address) const override { return address >= BasePC && address < BasePC + Size; }
	//	uint64_t getExtent() const override { return Size; }

	//	uint64_t readBytes(uint8_t *Buf, uint64_t Size,
	//		uint64_t Address) const override {
	//		/*if (Addr - BasePC >= Size)
	//		return -1;
	//		*Byte = Bytes[Addr - BasePC];
	//		return 0;*/
	//		memcpy(Buf, Bytes + (Address - BasePC), Size);
	//		return Size;
	//	}

	//	virtual const uint8_t *getPointer(uint64_t address, uint64_t size) const override
	//	{
	//		return Bytes + (address - BasePC);
	//	}
	//};
} // end anonymous namespace

  //using namespace X86Disassembler;

int X64Instr::GetLength()
{
	return mSize;
}

bool X64Instr::IsBranch()
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Branch)) != 0;
}

bool X64Instr::IsCall()
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Call)) != 0;
}

bool X64Instr::IsRep(bool& isPrefixOnly)
{
	auto instFlags = mMCInst.getFlags();
	if ((instFlags & (X86::IP_HAS_REPEAT_NE | X86::IP_HAS_REPEAT)) != 0)
	{
		isPrefixOnly = false;
		return true;
	}
	auto opCode = mMCInst.getOpcode();
	if ((opCode >= X86::REPNE_PREFIX) && (opCode <= X86::REP_MOVSW_64))
	{
		isPrefixOnly = true;
		return true;
	}
	return false;
}

bool X64Instr::IsReturn()
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Return)) != 0;
}

bool X64Instr::IsLoadAddress()
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	if (instDesc.NumOperands >= 6)
	{
		if ((instDesc.OpInfo[0].OperandType == MCOI::OPERAND_REGISTER) &&
			(instDesc.OpInfo[4].OperandType == MCOI::OPERAND_MEMORY))
			return true;
	}

	return false;
}

static int ConvertRegNum(const MCOperand& operand)
{
	if (!operand.isReg())
		return -1;

	switch (operand.getReg())
	{
	case llvm::X86::AL:
	case llvm::X86::AH:
	case llvm::X86::EAX:
	case llvm::X86::RAX:
		return X64Reg_RAX;
	case llvm::X86::CL:
	case llvm::X86::CH:
	case llvm::X86::ECX:
	case llvm::X86::RCX:
		return X64Reg_RCX;
	case llvm::X86::DL:
	case llvm::X86::DH:
	case llvm::X86::EDX:
	case llvm::X86::RDX:
		return X64Reg_RDX;
	case llvm::X86::BL:
	case llvm::X86::BH:
	case llvm::X86::EBX:
	case llvm::X86::RBX:
		return X64Reg_RBX;
	case llvm::X86::RBP:
		return X64Reg_RBP;
	case llvm::X86::RSP:
		return X64Reg_RSP;

	case llvm::X86::R8:
	case llvm::X86::R8D:
	case llvm::X86::R8W:
	case llvm::X86::R8B:
		return X64Reg_R8;
	case llvm::X86::R9:
	case llvm::X86::R9D:
	case llvm::X86::R9W:
	case llvm::X86::R9B:
		return X64Reg_R9;
	case llvm::X86::R10:
	case llvm::X86::R10D:
	case llvm::X86::R10W:
	case llvm::X86::R10B:
		return X64Reg_R10;
	case llvm::X86::R11:
	case llvm::X86::R11D:
	case llvm::X86::R11W:
	case llvm::X86::R11B:
		return X64Reg_R11;
	case llvm::X86::R12:
	case llvm::X86::R12D:
	case llvm::X86::R12W:
	case llvm::X86::R12B:
		return X64Reg_R12;
	case llvm::X86::R13:
	case llvm::X86::R13D:
	case llvm::X86::R13W:
	case llvm::X86::R13B:
		return X64Reg_R13;
	case llvm::X86::R14:
	case llvm::X86::R14D:
	case llvm::X86::R14W:
	case llvm::X86::R14B:
		return X64Reg_R14;
	case llvm::X86::R15:
	case llvm::X86::R15D:
	case llvm::X86::R15W:
	case llvm::X86::R15B:
		return X64Reg_R15;

	case llvm::X86::RSI:
	case llvm::X86::ESI:
	case llvm::X86::SI:
	case llvm::X86::SIL:
		return X64Reg_RSI;
	case llvm::X86::RDI:
	case llvm::X86::EDI:
	case llvm::X86::DI:
	case llvm::X86::DIL:
		return X64Reg_RDI;
	case llvm::X86::RIP:
		return X64Reg_RIP;
	case llvm::X86::EFLAGS:
		return X64Reg_EFL;

	case llvm::X86::ST0:
		return X64Reg_FPST0;
	case llvm::X86::ST1:
		return X64Reg_FPST1;
	case llvm::X86::ST2:
		return X64Reg_FPST2;
	case llvm::X86::ST3:
		return X64Reg_FPST3;
	case llvm::X86::ST4:
		return X64Reg_FPST4;
	case llvm::X86::ST5:
		return X64Reg_FPST5;
	case llvm::X86::ST6:
		return X64Reg_FPST6;
	case llvm::X86::ST7:
		return X64Reg_FPST7;

	case llvm::X86::XMM0:
		return X64Reg_M128_XMM0;
	case llvm::X86::XMM1:
		return X64Reg_M128_XMM1;
	case llvm::X86::XMM2:
		return X64Reg_M128_XMM2;
	case llvm::X86::XMM3:
		return X64Reg_M128_XMM3;
	case llvm::X86::XMM4:
		return X64Reg_M128_XMM4;
	case llvm::X86::XMM5:
		return X64Reg_M128_XMM5;
	case llvm::X86::XMM6:
		return X64Reg_M128_XMM6;
	case llvm::X86::XMM7:
		return X64Reg_M128_XMM7;
	case llvm::X86::XMM8:
		return X64Reg_M128_XMM8;
	case llvm::X86::XMM9:
		return X64Reg_M128_XMM9;
	case llvm::X86::XMM10:
		return X64Reg_M128_XMM10;
	case llvm::X86::XMM11:
		return X64Reg_M128_XMM11;
	case llvm::X86::XMM12:
		return X64Reg_M128_XMM12;
	case llvm::X86::XMM13:
		return X64Reg_M128_XMM13;
	case llvm::X86::XMM14:
		return X64Reg_M128_XMM14;
	case llvm::X86::XMM15:
		return X64Reg_M128_XMM15;
	}

	return -1;
}

bool X64Instr::GetIndexRegisterAndOffset(int* outRegister, int* outOffset)
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());

	auto form = (instDesc.TSFlags & llvm::X86II::FormMask);
	if ((form == llvm::X86II::MRMDestMem) || (form == llvm::X86II::MRMSrcMem) ||
		((form >= llvm::X86II::MRM0m) && (form <= llvm::X86II::MRM7m)))
	{
		int regOffset = 0;
		if (form == llvm::X86II::MRMSrcMem)
			regOffset = 1;

		auto baseReg = mMCInst.getOperand(regOffset + llvm::X86::AddrBaseReg);
		auto scaleAmt = mMCInst.getOperand(regOffset + llvm::X86::AddrScaleAmt);
		auto indexReg = mMCInst.getOperand(regOffset + llvm::X86::AddrIndexReg);
		auto addrDisp = mMCInst.getOperand(regOffset + llvm::X86::AddrDisp);

		/*bool a = baseReg.isReg();
		bool b = scaleAmt.isImm();
		int c = scaleAmt.getImm();
		bool d = indexReg.isReg();
		int e = indexReg.getReg();
		bool f = addrDisp.isImm();*/

		bool isValid = false;

		// Look for [reg+offset] form
		if ((baseReg.isReg()) &&
			(scaleAmt.isImm()) && (scaleAmt.getImm() == 1) &&
			(indexReg.isReg()) && (indexReg.getReg() == llvm::X86::NoRegister) &&
			(addrDisp.isImm()))
		{
			int regNum = ConvertRegNum(baseReg);
			if (regNum == -1)
				return false;
			*outRegister = regNum;
			*outOffset = (int)addrDisp.getImm();
			return true;
		}
	}
	return false;
}

bool X64Instr::GetImmediate(uint64* outImm)
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());

	auto immediateType = (instDesc.TSFlags & llvm::X86II::ImmMask);
	if ((immediateType == 0) && (mMCInst.getNumOperands() < 6))
		return false;

	auto immOp = mMCInst.getOperand(5);
	if (!immOp.isImm())
		return false;
	*outImm = immOp.getImm();
	return true;
}

int X64Instr::GetJmpState(int flags)
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	if ((instDesc.getFlags() & (1 << MCID::Branch)) == 0)
		return -1;

	if (mMCInst.getNumOperands() < 1)
		return 0;

#define FLAGVAR(abbr, name) int flag##abbr = ((flags & ((uint64)1 << X64CPURegisters::GetFlagBitForRegister(X64Reg_FLAG_##abbr##_##name))) != 0) ? 1 : 0
	FLAGVAR(CF, CARRY);
	FLAGVAR(PF, PARITY);
	FLAGVAR(AF, ADJUST);
	FLAGVAR(ZF, ZERO);
	FLAGVAR(SF, SIGN);
	FLAGVAR(IF, INTERRUPT);
	FLAGVAR(DF, DIRECTION);
	FLAGVAR(OF, OVERFLOW);
#undef FLAGVAR

	int baseOpCode = llvm::X86II::getBaseOpcodeFor(instDesc.TSFlags);
	switch (baseOpCode)
	{
	case 0x70: // JO
		return (!flagOF) ? 1 : 0;
	case 0x71: // JNO
		return (!flagOF) ? 1 : 0;
	case 0x72: // JB
		return (flagCF) ? 1 : 0;
	case 0x73: // JAE
		return (!flagCF) ? 1 : 0;
	case 0x74: // JE
		return (flagZF) ? 1 : 0;
	case 0x75: // JNE
		return (!flagZF) ? 1 : 0;
	case 0x76: // JBE
		return ((flagCF) || (flagZF)) ? 1 : 0;
	case 0x77: // JA
		return ((!flagCF) && (!flagZF)) ? 1 : 0;
	case 0x78: // JNP
		return (flagSF) ? 1 : 0;
	case 0x79: // JNS
		return (!flagSF) ? 1 : 0;
	case 0x7A: // JP
		return (flagPF) ? 1 : 0;
	case 0x7B: // JPO
		return (!flagPF) ? 1 : 0;
	case 0x7C: // JL
		return (flagSF != flagOF) ? 1 : 0;
	case 0x7D: // JGE
		return (flagSF == flagOF) ? 1 : 0;
	case 0x7E: // JLE
		return ((flagZF) || (flagSF != flagOF)) ? 1 : 0;
	case 0x7F: // JG
		return ((!flagZF) && (flagSF == flagOF)) ? 1 : 0;
	case 0xEB: // JMP
		return 1;
	}

	return -1;
}

void X64Instr::MarkRegsUsed(Array<RegForm>& regsUsed, bool overrideForm)
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());

	//String dbgStr = mX64->InstructionToString(this, 0);

	int opCode = instDesc.getOpcode();
	auto form = (instDesc.TSFlags & llvm::X86II::FormMask);

	/*if (opCode == 1724)
	{
		// MOVAPSrr is emitted for all moves between XMM registers, regardless of
		//  their actual format, so we just copy the actual RegForm form here
		if (instDesc.getNumOperands() != 2)
			return;

		auto operand = mMCInst.getOperand(0);
		int regNumTo = ConvertRegNum(operand);
		operand = mMCInst.getOperand(1);
		int regNumFrom = ConvertRegNum(operand);

		if (regNumFrom == -1) // ??
			return;

		while (std::max(regNumFrom, regNumTo) >= (int)regsUsed.size())
			regsUsed.push_back(RegForm_Invalid);

		if ((regsUsed[regNumTo] <= RegForm_Unknown) || (overrideForm))
			regsUsed[regNumTo] = regsUsed[regNumFrom];
		return;
	}
	else if (opCode == 1722)
	{
		// MOVAPSmr is actually emitted for all moves from XMM reg to memory
		// So ignore the format
		return;
	}
	else if (opCode == 1723)
	{
		// MOVAPSrm is actually emitted for all moves from XMM reg to memory
		// So ignore the format
		return;
	}*/

	int checkIdx = opCode * 3;

	//const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
	//auto form = (instDesc.TSFlags & llvm::X86II::FormMask);

	for (int opIdx = 0; opIdx < std::min((int)instDesc.getNumOperands(), 3); opIdx++)
	{
		auto operand = mMCInst.getOperand(opIdx);
		int regNum = ConvertRegNum(operand);
		if (regNum != -1)
		{
			while (regNum >= (int)regsUsed.size())
				regsUsed.push_back(RegForm_Invalid);

			RegForm regForm = (RegForm)X86Instr::sRegForm[checkIdx];
			if ((regsUsed[regNum] <= RegForm_Unknown) || (overrideForm))
				regsUsed[regNum] = regForm;

			checkIdx++;
		}
	}
}

uint64 X64Instr::GetTarget(Debugger* debugger, X64CPURegisters* registers)
{
	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());

	if (mMCInst.getNumOperands() < 1)
		return 0;

	/*if ((debugger != NULL) && (registers != NULL))
	{
		int regNum = 0;
		int offset = 0;
		if (GetIndexRegisterAndOffset(&regNum, &offset))
		{
			uint64 addr = registers->mIntRegsArray[regNum] + offset;
			uint64 val = 0;
			debugger->ReadMemory(addr, 8, &val);
			return val;
		}
	}*/

	int opIdx = 0;
	auto operand = mMCInst.getOperand(0);
	if (mMCInst.getNumOperands() > 4)
	{
		if ((instDesc.OpInfo[0].OperandType == MCOI::OPERAND_REGISTER) && (instDesc.OpInfo[4].OperandType == MCOI::OPERAND_MEMORY))
		{
			opIdx = 4;
			operand = mMCInst.getOperand(opIdx);
		}
	}

	if (operand.isImm())
	{
		auto targetAddr = (uint64)operand.getImm();
		if (instDesc.OpInfo[opIdx].OperandType == MCOI::OPERAND_PCREL)
			targetAddr += mAddress + mSize;
		return targetAddr;
	}
	return 0;
}

bool X64Instr::PartialSimulate(Debugger* debugger, X64CPURegisters* registers)
{
//	const MCInstrDesc &instDesc = mX64->mInstrInfo->get(mMCInst.getOpcode());
//
//	for (int i = 0; i < instDesc.NumOperands; i++)
//	{
//		auto regInfo = mMCInst.getOperand(i);
//		NOP;
//	}
//
//	if (instDesc.getOpcode() == X86::MOV64rm)
//	{
//		auto form = (instDesc.TSFlags & llvm::X86II::FormMask);
//
//		if ((form == llvm::X86II::MRMSrcMem) && (instDesc.NumOperands == 6))
//		{
//			auto destReg = mMCInst.getOperand(llvm::X86::AddrBaseReg);
//			if (destReg.isReg())
//			{
//				int regNum = 0;
//				int offset = 0;
//				if (GetIndexRegisterAndOffset(&regNum, &offset))
//				{
//					uint64 addr = registers->mIntRegsArray[regNum] + offset;
//					uint64 val = 0;
//					debugger->ReadMemory(addr, 8, &val);
//
//					switch (destReg.getReg())
//					{
//
//					}
//				}
//			}
//		}
//
//// 		if ((form == llvm::X86II::MRMDestMem) || (form == llvm::X86II::MRMSrcMem) ||
//// 			((form >= llvm::X86II::MRM0m) && (form <= llvm::X86II::MRM7m)))
//// 		{
//// 		}
//	}
//
//	if (instDesc.getOpcode() == X86::XOR8rr)
//	{
//		if (instDesc.NumOperands == 3)
//		{
//			auto destReg = mMCInst.getOperand(0);
//			auto srcReg = mMCInst.getOperand(1);
//
//			if ((destReg.isReg()) && (srcReg.isReg()))
//			{
//				if (destReg.getReg() == srcReg.getReg())
//				{
//					switch (destReg.getReg())
//					{
//					case X86::AL:
//						((uint8*)&registers->mIntRegs.rax)[0] = 0;
//						break;
//					}
//				}
//			}
//		}
//	}

	return false;
}

X64CPU::X64CPU() :
	mWarningStream(mWarningString),
	mCommentStream(mCommentString)
{
	mDisAsm = NULL;
	mRegisterInfo = NULL;
	mAsmInfo = NULL;
	mInstrInfo = NULL;
	mInstPrinter = NULL;

	//InitializeAllTargets();

	auto& TheX86_64Target = llvm::getTheX86_64Target();

	const char* triple = "x86_64-pc-mingw32";
	mSubtargetInfo = TheX86_64Target.createMCSubtargetInfo(triple, "x86-64", "");

	mRegisterInfo = TheX86_64Target.createMCRegInfo(triple);
	if (!mRegisterInfo)
		return;

	MCTargetOptions options;
	// Get the assembler info needed to setup the MCContext.
	mAsmInfo = TheX86_64Target.createMCAsmInfo(*mRegisterInfo, triple, options);
	if (!mAsmInfo)
		return;

	mInstrInfo = TheX86_64Target.createMCInstrInfo();

	mMCContext = new MCContext(Triple(triple), mAsmInfo, mRegisterInfo, mSubtargetInfo);

	mMCObjectFileInfo = TheX86_64Target.createMCObjectFileInfo(*mMCContext, false);
	if (!mMCObjectFileInfo)
		return;

	mMCContext->setObjectFileInfo(mMCObjectFileInfo);

	MCDisassembler *disAsm = TheX86_64Target.createMCDisassembler(*mSubtargetInfo, *mMCContext);
	mDisAsm = disAsm;

	mInstPrinter = TheX86_64Target.createMCInstPrinter(Triple(triple), 1, *mAsmInfo,
		*mInstrInfo, *mRegisterInfo);

	mInstPrinter->setPrintImmHex(true);

	for (const X86StringToOpcodeEntry* entry = &gX86StringToOpcodeTable[0]; entry->Name != nullptr; ++entry)
	{
		mStringToOpcodeMap.insert(StringToOpcodeMap::value_type(entry->Name, entry->Opcode));
	}
}

extern "C" void LLVMShutdown();

X64CPU::~X64CPU()
{
	delete mInstPrinter;
	delete mDisAsm;
	delete mMCContext;
	delete mMCObjectFileInfo;
	delete mInstrInfo;
	delete mAsmInfo;
	delete mRegisterInfo;
	delete mSubtargetInfo;

	LLVMShutdown();
}

bool X64CPU::Decode(uint64 address, DbgModuleMemoryCache* memoryCache, X64Instr* inst)
{
	inst->mAddress = address;
	inst->mX64 = this;

	uint64 size = 0;
	uint8 data[15];
	memoryCache->Read(address, data, 15);

	mDisAsm->CommentStream = &inst->mAnnotationStream;
	ArrayRef<uint8_t> dataArrayRef(data, data + 15);
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, dataArrayRef, address, nulls());
	inst->mSize = (int)size;

	return S == MCDisassembler::Success;
}

bool X64CPU::Decode(uint64 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, X64Instr* inst)
{
	//X86GenericDisassembler assembler;

	//DisasmMemoryObject region((uint8*)dataBase, dataLength, baseAddress);
	//std::memorystream

	uint64 address = baseAddress + (dataPtr - dataBase);
	inst->mAddress = address;
	inst->mX64 = this;

	uint64 size = 0;
	//TODO: LLVM3.8
	//MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, region, address, nulls(), inst->mAnnotationStream);

	mDisAsm->CommentStream = &inst->mAnnotationStream;
	ArrayRef<uint8_t> dataArrayRef(dataPtr, dataLength - (dataPtr - dataBase));
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, dataArrayRef, address, nulls());
	inst->mSize = (int)size;

	return S == MCDisassembler::Success;
}

uint64 X64CPU::DecodeThunk(uint64 address, DbgModuleMemoryCache* memoryCache)
{
	uint8 inst = 0;
	memoryCache->Read(address, &inst, 1);
	if (inst == 0xE9)
	{
		int32 offset = 0;
		memoryCache->Read(address + 1, (uint8*)&offset, sizeof(int32));
		return address + offset + 5;
	}

	return 0;
}

void X64CPU::GetNextPC(uint64 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, uint32* regs, uint64 nextPCs[2])
{
	//DisasmMemoryObject region((uint8*)dataBase, dataLength, baseAddress);
	uint64 address = baseAddress + (dataPtr - dataBase);

	uint64 size = 0;
	MCInst mcInst;
	//MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(mcInst, size, region, address, nulls(), nulls());
	mDisAsm->CommentStream = &nulls();
	ArrayRef<uint8_t> dataArrayRef(dataPtr, dataLength - (dataPtr - dataBase));
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(mcInst, size, dataArrayRef, address, nulls());
}

bool X64CPU::IsReturnInstruction(X64Instr* inst)
{
	const MCInstrDesc &instDesc = mInstrInfo->get(inst->mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Return)) != 0;
}

String X64CPU::InstructionToString(X64Instr* inst, uint64 addr)
{
	StringRef annotationsStr;

	SmallVector<char, 256> insnStr;
	raw_svector_ostream OS(insnStr);
	//mInstPrinter->CurPCRelImmOffset = addr + inst->GetLength();
	mInstPrinter->printInst(&inst->mMCInst, addr, annotationsStr, *mSubtargetInfo, OS);
	//OS.flush();
	//llvm::StringRef str = OS.str();

	String result;
	for (int idx = 0; idx < (int)insnStr.size(); idx++)
	{
		char c = insnStr[idx];
		if (c == '\t')
		{
			if (idx != 0)
			{
				int numSpaces = 10 - ((int)result.length() % 10);
				result.Append(' ', numSpaces);
			}
		}
		else
			result.Append(c);
	}

	if (result.StartsWith("rep       "))
	{
		while ((result.length() > 10) && (result[10] == ' '))
			result.Remove(10);
	}

	/*String result = String(insnStr.data(), insnStr.size());
	for (int i = 0; i < (int)result.length(); i++)
	{
	if (result[i] == '\t')
	result[i] = ' ';
	}*/
	return result;
}

DbgBreakKind X64CPU::GetDbgBreakKind(uint64 address, DbgModuleMemoryCache* memoryCache, int64* regs, int64* outObjectPtr)
{
	// We've looking for a CMP BYTE PTR [<reg>], -0x80
	//  if <reg> is R12 then encoding takes an extra 2 bytes
	X64Instr inst;
	for (int checkLen = 5; checkLen >= 3; checkLen--)
	{
		int offset = -3 - checkLen;

		if (!Decode(address + offset, memoryCache, &inst))
			continue;

		if (inst.GetLength() != checkLen)
			continue;

		const MCInstrDesc &instDesc = mInstrInfo->get(inst.mMCInst.getOpcode());
		if (!instDesc.isCompare())
			continue;

		auto immediateType = (instDesc.TSFlags & llvm::X86II::ImmMask);
		if ((immediateType == llvm::X86II::Imm8) && (inst.mMCInst.getNumOperands() == 2))
		{
			// We're checking for a TEST [<reg>], 1
			if (inst.mMCInst.getOpcode() != llvm::X86::TEST8ri)
				continue;
			auto immOp = inst.mMCInst.getOperand(1);
			if (!immOp.isImm())
				continue;
			if (immOp.getImm() != 1)
				continue;
			return DbgBreakKind_ArithmeticOverflow;
		}

		if ((immediateType == 0) || (inst.mMCInst.getNumOperands() < 6))
			continue;

		auto form = (instDesc.TSFlags & llvm::X86II::FormMask);

		auto baseReg = inst.mMCInst.getOperand(llvm::X86::AddrBaseReg);
		/*auto scaleAmt = inst.mMCInst.getOperand(llvm::X86::AddrScaleAmt);
		auto indexReg = inst.mMCInst.getOperand(llvm::X86::AddrIndexReg);
		auto addrDisp = inst.mMCInst.getOperand(llvm::X86::AddrDisp);*/

		auto immOp = inst.mMCInst.getOperand(5);
		if (!immOp.isImm())
			continue;
		if (immOp.getImm() != -0x80)
			continue;

		int regNum = ConvertRegNum(baseReg);
		if (regNum == -1)
			continue;

		*outObjectPtr = (uint64)regs[regNum];

		return DbgBreakKind_ObjectAccess;
	}

	// check jno/jnb
	for (int offset = 3; offset <= 3; offset++)
	{
		if (!Decode(address - offset, memoryCache, &inst))
			continue;

		if (inst.GetLength() != 2)
			continue;

		const MCInstrDesc &instDesc = mInstrInfo->get(inst.mMCInst.getOpcode());
		if (!instDesc.isBranch())
			continue;

		auto immediateType = (instDesc.TSFlags & llvm::X86II::ImmMask);
		if ((immediateType == llvm::X86II::Imm8PCRel) && (inst.mMCInst.getNumOperands() == 2))
		{
			auto immOp = inst.mMCInst.getOperand(1);
			if (!immOp.isImm())
				continue;
			if ((immOp.getImm() != 1) && (immOp.getImm() != 3))
				continue;
			return DbgBreakKind_ArithmeticOverflow;
		}
	}

	return DbgBreakKind_None;
}

int X64CPU::GetOpcodesForMnemonic(const StringImpl& mnemonic, Array<int>& outOpcodes)
{
	String s(mnemonic);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	std::pair<StringToOpcodeMap::iterator, StringToOpcodeMap::iterator> range = mStringToOpcodeMap.equal_range(s);

	outOpcodes.Clear();
	for (StringToOpcodeMap::iterator it = range.first; it != range.second; ++it)
		outOpcodes.push_back(it->second);

	return (int)outOpcodes.size();
}

void X64CPU::GetClobbersForMnemonic(const StringImpl& mnemonic, int argCount, Array<int>& outImplicitClobberRegNums, int& outClobberArgCount, bool& outMayClobberMem)
{
	outImplicitClobberRegNums.Clear();
	outClobberArgCount = 0;
	outMayClobberMem = false;

	Array<int> opcodes;
	if (!GetOpcodesForMnemonic(mnemonic, opcodes))
		return;

	HashSet<int> impRegs;
	for (int op : opcodes)
	{
		const MCInstrDesc& desc = mInstrInfo->get(op);
		//if ((desc.getNumOperands() != argCount) && !desc.isVariadic())
		//continue; // not an operand form that we care about

		outClobberArgCount = std::max(outClobberArgCount, (int)desc.getNumDefs());
		if (!outMayClobberMem && desc.mayStore())
			outMayClobberMem = true;

		int numImplicits = desc.getNumImplicitDefs();
		auto impPtr = desc.getImplicitDefs();
		for (int iImp = 0; iImp<numImplicits; ++iImp)
			impRegs.Add(impPtr[iImp]);
	}

	for (auto const& r : impRegs)
	{
		int regNum = ConvertRegNum(MCOperand::createReg(r));
		if (regNum != -1)
			outImplicitClobberRegNums.push_back(regNum);
	}
}

bool X64CPU::ParseInlineAsmInstructionLLVM(const StringImpl&asmInst, String& outError)
{
	struct LocalThunk
	{
		std::function<void(const SMDiagnostic &)> mFunc;
		void Thunk(const SMDiagnostic& diag)
		{
			mFunc(diag);
		}
		static void StaticThunk(const SMDiagnostic& diag, void* diagInfo)
		{
			((LocalThunk*)diagInfo)->Thunk(diag);
		}
	};

	String diagMessage, diagLineContents;

	std::function<void(const SMDiagnostic &)> diagHandler = [&](const SMDiagnostic &diag)
	{
		if (diag.getKind() == SourceMgr::DK_Error)
		{
			diagMessage = diag.getMessage().data();
			diagLineContents = diag.getLineContents().data();
		}
	};
	LocalThunk localThunk;
	localThunk.mFunc = diagHandler;

	auto& TheX86_64Target = getTheX86_64Target();

	llvm::SourceMgr srcMgr;
	srcMgr.setDiagHandler(LocalThunk::StaticThunk, &localThunk);

	SmallString<512> asmString;
	asmString.assign(asmInst.c_str());
	//asmString.push_back('\n');
	asmString.push_back('\0');
	asmString.pop_back();
	std::unique_ptr<llvm::MemoryBuffer> buffer = llvm::MemoryBuffer::getMemBuffer(asmString, "inline asm");
	srcMgr.AddNewSourceBuffer(std::move(buffer), llvm::SMLoc());

	llvm::MCTargetOptions options;
	std::unique_ptr<llvm::MCStreamer> streamer(createNullStreamer(*mMCContext));
	std::unique_ptr<llvm::MCAsmParser> parser(createMCAsmParser(srcMgr, *mMCContext, *streamer.get(), *mAsmInfo));
	std::unique_ptr<llvm::MCTargetAsmParser> asmParser(TheX86_64Target.createMCAsmParser(*mSubtargetInfo, *parser.get(), *mInstrInfo, options));
	parser->setAssemblerDialect(llvm::InlineAsm::AD_Intel);
	parser->setTargetParser(*asmParser);

	bool result = parser->Run(false, true) == 0;

	if (result)
		outError.clear();
	else
		outError = diagMessage.c_str(); // we can ignore diagLineContents in this situation since we're only doing one line at a time anyway
										//outError = StrFormat("%s: \"%s\"", diagMessage.c_str(), diagLineContents.c_str());

	return result;
}