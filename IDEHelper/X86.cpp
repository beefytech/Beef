#pragma warning(push)
#pragma warning(disable:4996)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4005)
#pragma warning(disable:4267)

#include "X86.h"
#include <assert.h>
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/IR/InlineAsm.h"
//#include "llvm/Target/TargetSubtargetInfo.h"
//#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
//#include "llvm/MC/MCTargetAsmParser.h"
#include "llvm/MC/MCObjectFileInfo.h"
//#include "X86/Disassembler/X86Disassembler.h"
#include "X86/Disassembler/X86DisassemblerDecoder.h"
#include "X86/MCTargetDesc/X86MCTargetDesc.h"
#include "X86/MCTargetDesc/X86BaseInfo.h"
#include "X86InstrInfo.h"
#include "BeefySysLib/util/HashSet.h"

#pragma warning(pop)

#include "BeefySysLib/util/AllocDebug.h"

//#include "Support/TargetSelect.h"

//int arch_8086_disasm_instr(struct x86_instr *instr, addr_t pc, char *line, unsigned int max_line);

extern "C" void LLVMInitializeX86TargetMC();
extern "C" void LLVMInitializeX86Disassembler();

USING_NS_BF;

const char* X86CPURegisters::sCPURegisterNames[] =
{
	// integer general registers (DO NOT REORDER THESE; must exactly match DbgModule x86 register mappings)
	"EAX",
	"ECX",
	"EDX",
	"EBX",
	"ESP",
	"EBP",
	"ESI",
	"EDI",
	"EIP",
	"EFL",

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

	// xmm 128-bit macro-registers (no stored data with these", they're symbolic for use as higher-level constructs", aliases to the individual xmm regs above)
	"XMM0",
	"XMM1",
	"XMM2",
	"XMM3",
	"XMM4",
	"XMM5",
	"XMM6",
	"XMM7",

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

//using namespace X86Disassembler;

int X86Instr::GetLength()
{
	return mSize;
}

bool X86Instr::StackAdjust(uint32& adjust)
{
	if (adjust == 0)
		return true;

	if (mMCInst.getOpcode() != X86::SUB32ri8)
		return true;
	
	auto operand0 = mMCInst.getOperand(0);
	if (operand0.getReg() != llvm::X86::ESP)
		return true;	
	auto operand2 = mMCInst.getOperand(2);
	if (!operand2.isImm())
		return false;

	adjust -= (uint32)operand2.getImm();

	return true;
}

bool X86Instr::IsBranch()
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Branch)) != 0;
}

bool X86Instr::IsCall()
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Call)) != 0;
}

bool X86Instr::IsReturn()
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
	return (instDesc.getFlags() & (1 << MCID::Return)) != 0;
}

bool X86Instr::IsRep(bool& isPrefixOnly)
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

bool X86Instr::IsLoadAddress()
{	
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
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
		return X86Reg_EAX;
	case llvm::X86::CL:
	case llvm::X86::CH:
	case llvm::X86::ECX:
		return X86Reg_ECX;		
	case llvm::X86::DL:
	case llvm::X86::DH:
	case llvm::X86::EDX:
		return X86Reg_EDX;		
	case llvm::X86::BL:
	case llvm::X86::BH:
	case llvm::X86::EBX:
		return X86Reg_EBX;		
	case llvm::X86::ESP:
		return X86Reg_ESP;		
	case llvm::X86::EBP:
		return X86Reg_EBP;		
	case llvm::X86::ESI:
		return X86Reg_ESI;		
	case llvm::X86::EDI:
		return X86Reg_EDI;		
	case llvm::X86::EIP:
		return X86Reg_EIP;		
	case llvm::X86::EFLAGS:
		return X86Reg_EFL;

	case llvm::X86::ST0:
		return X86Reg_FPST0;
	case llvm::X86::ST1:
		return X86Reg_FPST1;
	case llvm::X86::ST2:
		return X86Reg_FPST2;
	case llvm::X86::ST3:
		return X86Reg_FPST3;
	case llvm::X86::ST4:
		return X86Reg_FPST4;
	case llvm::X86::ST5:
		return X86Reg_FPST5;
	case llvm::X86::ST6:
		return X86Reg_FPST6;
	case llvm::X86::ST7:
		return X86Reg_FPST7;
			
	case llvm::X86::XMM0:
		return X86Reg_M128_XMM0;
	case llvm::X86::XMM1:
		return X86Reg_M128_XMM1;
	case llvm::X86::XMM2:
		return X86Reg_M128_XMM2;
	case llvm::X86::XMM3:
		return X86Reg_M128_XMM3;
	case llvm::X86::XMM4:
		return X86Reg_M128_XMM4;
	case llvm::X86::XMM5:
		return X86Reg_M128_XMM5;
	case llvm::X86::XMM6:
		return X86Reg_M128_XMM6;
	case llvm::X86::XMM7:
		return X86Reg_M128_XMM7;
	}

	return -1;
}

bool X86Instr::GetIndexRegisterAndOffset(int* outRegister, int* outOffset)
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());

	auto form = (instDesc.TSFlags & llvm::X86II::FormMask);
	if ((form == llvm::X86II::MRMDestMem) || (form == llvm::X86II::MRMSrcMem) || 
		((form >= llvm::X86II::MRM0m) && (form <= llvm::X86II::MRM7m)))
	{		
		auto baseReg = mMCInst.getOperand(llvm::X86::AddrBaseReg);
		auto scaleAmt = mMCInst.getOperand(llvm::X86::AddrScaleAmt);
		auto indexReg = mMCInst.getOperand(llvm::X86::AddrIndexReg);
		auto addrDisp = mMCInst.getOperand(llvm::X86::AddrDisp);

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

bool X86Instr::GetImmediate(uint32* outImm)
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
	
	auto immediateType = (instDesc.TSFlags & llvm::X86II::ImmMask);
	if ((immediateType == 0) && (mMCInst.getNumOperands() < 6))
		return false;
	
	auto immOp = mMCInst.getOperand(5);
	if (!immOp.isImm())
		return false;
	*outImm = (uint32)immOp.getImm();
	return true;
}

int X86Instr::GetJmpState(int flags)
{
	return -1;
}

void X86Instr::MarkRegsUsed(Array<RegForm>& regsUsed, bool overrideForm)
{
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());

	int checkIdx = instDesc.getOpcode() * 3;

	for (int opIdx = 0; opIdx < (int)instDesc.getNumOperands(); opIdx++)
	{
		auto operand = mMCInst.getOperand(opIdx);
		int regNum = ConvertRegNum(operand);
		if (regNum != -1)
		{
			while (regNum >= (int)regsUsed.size())
				regsUsed.push_back(RegForm_Invalid);

			if ((regsUsed[regNum] <= RegForm_Unknown) || (overrideForm))
				regsUsed[regNum] = (RegForm)sRegForm[checkIdx];
			checkIdx++;
		}
	}
}

uint32 X86Instr::GetTarget(X86CPURegisters* registers)
{	
	const MCInstrDesc &instDesc = mX86->mInstrInfo->get(mMCInst.getOpcode());
	
	if (mMCInst.getNumOperands() < 1)
		return 0;

	int opIdx = 0;
	auto operand = mMCInst.getOperand(0);
	if ((instDesc.OpInfo[0].OperandType == MCOI::OPERAND_REGISTER) && (instDesc.OpInfo[4].OperandType == MCOI::OPERAND_MEMORY))
	{
		opIdx = 4;
		operand = mMCInst.getOperand(opIdx);
	}

	if (operand.isImm())
	{
		auto targetAddr = (uint32)operand.getImm();
		//TODO: LLVM3.8 - add changes to MCInst?
		/*if (instDesc.OpInfo[opIdx].OperandType == MCOI::OPERAND_PCREL)
			targetAddr += mMCInst.getPCAddr() + mMCInst.getInstLength();*/
		if (instDesc.OpInfo[opIdx].OperandType == MCOI::OPERAND_PCREL)
			targetAddr += mAddress + mSize;
		return targetAddr;
	}
	return 0;
}

X86CPU::X86CPU() :
	mWarningStream(mWarningString),
	mCommentStream(mCommentString)
{
	mDisAsm = NULL;
	mRegisterInfo = NULL;
	mAsmInfo = NULL;
	mInstrInfo = NULL;
	mInstPrinter = NULL;

	//InitializeAllTargets();		

	auto& TheX86_32Target = getTheX86_32Target();

	const char* triple = "i686-pc-mingw32";
	mSubtargetInfo = TheX86_32Target.createMCSubtargetInfo(triple, "x86-64", "");

	mRegisterInfo = TheX86_32Target.createMCRegInfo(triple);
	if (!mRegisterInfo)
		return;

	// Get the assembler info needed to setup the MCContext.
	mAsmInfo = TheX86_32Target.createMCAsmInfo(*mRegisterInfo, triple);
	if (!mAsmInfo)
		return;

	//TargetOptions targetOptions;
	//TargetMachine* targetMachine = TheX86_32Target.createTargetMachine(triple, "x86", "", targetOptions);

	//const MCInstrInfo* MII = targetMachine->getSubtargetImpl()->getInstrInfo();	
	//STI->getIntr

	mInstrInfo = TheX86_32Target.createMCInstrInfo();

	mMCObjectFileInfo = new MCObjectFileInfo();
	mMCContext = new MCContext(mAsmInfo, mRegisterInfo, mMCObjectFileInfo);

	MCDisassembler *disAsm = TheX86_32Target.createMCDisassembler(*mSubtargetInfo, *mMCContext);
	mDisAsm = disAsm;	

	//TODO: LLVM3.8 - changed params
	/*mInstPrinter = TheX86_32Target.createMCInstPrinter(1, *mAsmInfo,
		*mInstrInfo, *mRegisterInfo, *mSubtargetInfo);*/	
	mInstPrinter = TheX86_32Target.createMCInstPrinter(Triple(triple), 1, *mAsmInfo,
		*mInstrInfo, *mRegisterInfo);

	mInstPrinter->setPrintImmHex(true);

	for (const X86StringToOpcodeEntry* entry = &gX86StringToOpcodeTable[0]; entry->Name != nullptr; ++entry)
	{
		mStringToOpcodeMap.insert(StringToOpcodeMap::value_type(entry->Name, entry->Opcode));
	}
}

extern "C" void LLVMShutdown();

X86CPU::~X86CPU()
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

bool X86CPU::Decode(uint32 address, DbgModuleMemoryCache* memoryCache, X86Instr* inst)
{	
	inst->mAddress = address;
	inst->mX86 = this;

	uint64 size = 0;		
	uint8 data[15];
	memoryCache->Read(address, data, 15);

	ArrayRef<uint8_t> dataArrayRef(data, data + 15);
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, dataArrayRef, address, nulls(), inst->mAnnotationStream);
	inst->mSize = (int)size;

	return S == MCDisassembler::Success;
}

bool X86CPU::Decode(uint32 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, X86Instr* inst)
{
	//X86GenericDisassembler assembler;
		
	//DisasmMemoryObject region((uint8*)dataBase, dataLength, baseAddress);
	//std::memorystream
	
	uint32 address = baseAddress + (uint32)(dataPtr - dataBase);
	inst->mAddress = address;
	inst->mX86 = this;

	uint64 size = 0;	
	//TODO: LLVM3.8
	//MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, region, address, nulls(), inst->mAnnotationStream);

	ArrayRef<uint8_t> dataArrayRef(dataPtr, dataLength - (dataPtr - dataBase));
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(inst->mMCInst, size, dataArrayRef, address, nulls(), inst->mAnnotationStream);
	inst->mSize = (int)size;

	return S == MCDisassembler::Success;
}

void X86CPU::GetNextPC(uint32 baseAddress, const uint8* dataBase, int dataLength, const uint8* dataPtr, uint32* regs, uint32 nextPCs[2])
{
	//DisasmMemoryObject region((uint8*) dataBase, dataLength, baseAddress);	
	uint32 address = baseAddress + (uint32)(dataPtr - dataBase);

	uint64 size = 0;
	MCInst mcInst;
	//MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(mcInst, size, region, address, nulls(), nulls());
	ArrayRef<uint8_t> dataArrayRef(dataPtr, dataLength - (dataPtr - dataBase));
	MCDisassembler::DecodeStatus S = mDisAsm->getInstruction(mcInst, size, dataArrayRef, address, nulls(), nulls());
	
}

bool X86CPU::IsReturnInstruction(X86Instr* inst)
{
	const MCInstrDesc &instDesc = mInstrInfo->get(inst->mMCInst.getOpcode());
	return (instDesc.getFlags() & (1<<MCID::Return)) != 0;	
}

String X86CPU::InstructionToString(X86Instr* inst, uint32 addr)
{		
	StringRef annotationsStr;

	SmallVector<char, 256> insnStr;
	raw_svector_ostream OS(insnStr);
	//mInstPrinter->CurPCRelImmOffset = addr + inst->GetLength();
	mInstPrinter->printInst(&inst->mMCInst, OS, annotationsStr, *mSubtargetInfo);
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
	
	/*String result = String(insnStr.data(), insnStr.size());	
	for (int i = 0; i < (int)result.length(); i++)
	{
		if (result[i] == '\t')
			result[i] = ' ';
	}*/
	return result;
}

bool X86CPU::IsObjectAccessBreak(uint32 address, DbgModuleMemoryCache* memoryCache, int32* regs, int32* outObjectPtr)
{
	// We've looking for a CMP BYTE PTR [<reg>], -0x80
	//  if <reg> is R12 then encoding takes an extra 2 bytes
	X86Instr inst;	
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

		return true;
	}

	return false;
}

int X86CPU::GetOpcodesForMnemonic(const StringImpl& mnemonic, Array<int>& outOpcodes)
{
	String s(mnemonic);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	std::pair<StringToOpcodeMap::iterator, StringToOpcodeMap::iterator> range = mStringToOpcodeMap.equal_range(s);
		
	outOpcodes.Clear();
	for (StringToOpcodeMap::iterator it = range.first; it != range.second; ++it)
		outOpcodes.push_back(it->second);

	return (int)outOpcodes.size();	
}

void X86CPU::GetClobbersForMnemonic(const StringImpl& mnemonic, int argCount, Array<int>& outImplicitClobberRegNums, int& outClobberArgCount, bool& outMayClobberMem)
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
		for (int iImp=0; iImp<numImplicits; ++iImp)
			impRegs.Add(impPtr[iImp]);
	}

	for (auto const& r : impRegs)
	{
		int regNum = ConvertRegNum(MCOperand::createReg(r));
		if (regNum != -1)
			outImplicitClobberRegNums.push_back(regNum);
	}
}

bool X86CPU::ParseInlineAsmInstructionLLVM(const StringImpl&asmInst, String& outError)
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

	auto& TheX86_32Target = getTheX86_32Target();

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
	std::unique_ptr<llvm::MCTargetAsmParser> asmParser(TheX86_32Target.createMCAsmParser(*mSubtargetInfo, *parser.get(), *mInstrInfo, options));
	parser->setAssemblerDialect(llvm::InlineAsm::AD_Intel);
	parser->setTargetParser(*asmParser);

	bool result = parser->Run(false, true)==0;

	if (result)
		outError.clear();
	else
		outError = diagMessage.c_str(); // we can ignore diagLineContents in this situation since we're only doing one line at a time anyway
		//outError = StrFormat("%s: \"%s\"", diagMessage.c_str(), diagLineContents.c_str());

	return result;
}