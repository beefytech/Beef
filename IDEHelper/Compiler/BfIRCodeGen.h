#pragma once

#include "BfIRBuilder.h"
#include "BfSystem.h"

NS_BF_BEGIN

enum BfIRCodeGenEntryKind
{
	BfIRCodeGenEntryKind_None,
	BfIRCodeGenEntryKind_LLVMValue,
	BfIRCodeGenEntryKind_LLVMType,
	BfIRCodeGenEntryKind_LLVMBasicBlock,
	BfIRCodeGenEntryKind_LLVMMetadata,
	BfIRCodeGenEntryKind_FakeIntrinsic,
};

struct BfIRCodeGenEntry
{
	BfIRCodeGenEntryKind mKind;
	union
	{
		llvm::Value* mLLVMValue;
		llvm::Type* mLLVMType;
		llvm::BasicBlock* mLLVMBlock;
		llvm::MDNode* mLLVMMetadata;	
		BfIRIntrinsic mIntrinsic;
	};
};

class BfIRTypeEntry
{
public:
	int mTypeId;
	llvm::DIType* mDIType;
	llvm::DIType* mInstDIType;
	llvm::Type* mLLVMType;
	llvm::Type* mInstLLVMType;	

public:
	BfIRTypeEntry()
	{
		mTypeId = -1;
		mDIType = NULL;
		mInstDIType = NULL;
		mLLVMType = NULL;
		mInstLLVMType = NULL;
	}
};

class BfIRCodeGen : public BfIRCodeGenBase
{
public:	
	BfIRBuilder* mBfIRBuilder;	
	
    String mModuleName;
	llvm::LLVMContext* mLLVMContext;
	llvm::Module* mLLVMModule;
	llvm::Function* mActiveFunction;
	llvm::IRBuilder<>* mIRBuilder;
	llvm::AttributeList* mAttrSet;
	llvm::DIBuilder* mDIBuilder;
	llvm::DICompileUnit* mDICompileUnit;
	Array<llvm::DebugLoc> mSavedDebugLocs;
	llvm::InlineAsm* mNopInlineAsm;
	llvm::InlineAsm* mAsmObjectCheckAsm;	
	llvm::DebugLoc mDebugLoc;
	bool mHasDebugLoc;	
	bool mIsCodeView;	

	int mCmdCount;
	Dictionary<int, BfIRCodeGenEntry> mResults;
	Dictionary<int, BfIRTypeEntry> mTypes;
	Dictionary<int, llvm::Function*> mIntrinsicMap;
	Dictionary<llvm::Function*, int> mIntrinsicReverseMap;
	Array<llvm::Constant*> mConfigConsts32;
	Array<llvm::Constant*> mConfigConsts64;	

public:		
	BfTypeCode GetTypeCode(llvm::Type* type, bool isSigned);
	llvm::Type* GetLLVMType(BfTypeCode typeCode, bool& isSigned);
	BfIRTypeEntry& GetTypeEntry(int typeId);
	void SetResult(int id, llvm::Value* value);
	void SetResult(int id, llvm::Type* value);	
	void SetResult(int id, llvm::BasicBlock* value);
	void SetResult(int id, llvm::MDNode* value);	
	void CreateMemSet(llvm::Value* addr, llvm::Value* val, llvm::Value* size, int alignment, bool isVolatile = false);
	void AddNop();

public:
	BfIRCodeGen();
	~BfIRCodeGen();

	virtual void Fail(const StringImpl& error) override;

	void ProcessBfIRData(const BfSizedArray<uint8>& buffer) override;
	void PrintModule();
	void PrintFunction();

	int64 ReadSLEB128();
	void Read(StringImpl& str);	
	void Read(int& i);
	void Read(int64& i);
	void Read(bool& val);
	void Read(BfIRTypeEntry*& type);
	void Read(llvm::Type*& llvmType);
	void Read(llvm::FunctionType*& llvmType);
	void Read(llvm::Value*& llvmValue, BfIRCodeGenEntry** codeGenEntry = NULL);
	void Read(llvm::Constant*& llvmConstant);
	void Read(llvm::Function*& llvmFunc);
	void Read(llvm::BasicBlock*& llvmBlock);
	void Read(llvm::MDNode*& llvmMD);
	void Read(llvm::Metadata*& llvmMD);

	template <typename T>
	void Read(llvm::SmallVectorImpl<T>& vec)
	{
		int len = (int)ReadSLEB128();
		for (int i = 0; i < len; i++)
		{
			T result;
			Read(result);
			vec.push_back(result);
		}
	}

	void HandleNextCmd() override;
	void SetConfigConst(int idx, int value) override;

	llvm::Value* GetLLVMValue(int streamId);
	llvm::Type* GetLLVMType(int streamId);
	llvm::BasicBlock* GetLLVMBlock(int streamId);
	llvm::MDNode* GetLLVMMetadata(int streamId);

	llvm::Type* GetLLVMTypeById(int id);

	///

	bool WriteObjectFile(const StringImpl& outFileName, const BfCodeGenOptions& codeGenOptions);
	bool WriteIR(const StringImpl& outFileName, StringImpl& error);

	static int GetIntrinsicId(const StringImpl& name);	
	static const char* GetIntrinsicName(int intrinId);
	static void SetAsmKind(BfAsmKind asmKind);

	static void StaticInit();
};

NS_BF_END

