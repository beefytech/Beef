#pragma once

#include "BfIRBuilder.h"
#include "BfSystem.h"
#include "BfTargetTriple.h"

namespace llvm
{
	class Constant;
	class Value;
	class Type;
	class BasicBlock;
	class Function;
	class FunctionType;
	class MDNode;
	class InlineAsm;
	class DIType;
	class DIBuilder;
	class DICompileUnit;
	class AttributeList;
	class Module;
	class LLVMContext;
	class TargetMachine;
};

NS_BF_BEGIN

enum BfIRCodeGenEntryKind
{
	BfIRCodeGenEntryKind_None,
	BfIRCodeGenEntryKind_LLVMValue,
	BfIRCodeGenEntryKind_LLVMValue_Aligned,
	BfIRCodeGenEntryKind_LLVMType,
	BfIRCodeGenEntryKind_LLVMBasicBlock,
	BfIRCodeGenEntryKind_LLVMMetadata,
	BfIRCodeGenEntryKind_IntrinsicData,
};

class BfIRIntrinsicData
{
public:
	String mName;
	BfIRIntrinsic mIntrinsic;
	llvm::Type* mReturnType;
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
		BfIRIntrinsicData* mIntrinsicData;
	};
};

class BfIRTypeEntry
{
public:
	int mTypeId;
	int mSize;
	int mAlign;
	llvm::DIType* mDIType;
	llvm::DIType* mInstDIType;
	llvm::Type* mLLVMType;
	llvm::Type* mAlignLLVMType;
	llvm::Type* mInstLLVMType;

public:
	BfIRTypeEntry()
	{
		mTypeId = -1;
		mSize = -1;
		mAlign = -1;
		mDIType = NULL;
		mInstDIType = NULL;
		mLLVMType = NULL;
		mAlignLLVMType = NULL;
		mInstLLVMType = NULL;
	}
};

enum BfIRSizeAlignKind
{
	BfIRSizeAlignKind_NoTransform,
	BfIRSizeAlignKind_Original,
	BfIRSizeAlignKind_Aligned,
};

enum BfIRSimdType
{
	BfIRSimdType_None,
	BfIRSimdType_SSE,
	BfIRSimdType_SSE2,
	BfIRSimdType_AVX,
	BfIRSimdType_AVX2,
	BfIRSimdType_AVX512
};

class BfIRCodeGen : public BfIRCodeGenBase
{
public:
	BfIRBuilder* mBfIRBuilder;

	BumpAllocator mAlloc;
	BfTargetTriple mTargetTriple;
	String mTargetCPU;
    String mModuleName;
	llvm::LLVMContext* mLLVMContext;
	llvm::Module* mLLVMModule;
	llvm::Function* mActiveFunction;
	llvm::IRBuilder<>* mIRBuilder;
	llvm::AttributeList* mAttrSet;
	llvm::DIBuilder* mDIBuilder;
	llvm::DICompileUnit* mDICompileUnit;
	llvm::TargetMachine* mLLVMTargetMachine;
	Array<llvm::DebugLoc> mSavedDebugLocs;
	llvm::InlineAsm* mNopInlineAsm;
	llvm::InlineAsm* mObjectCheckAsm;
	llvm::InlineAsm* mOverflowCheckAsm;
	llvm::DebugLoc mDebugLoc;
	BfCodeGenOptions mCodeGenOptions;
	bool mHasDebugLoc;
	bool mIsCodeView;
	bool mHadDLLExport;
	int mConstValIdx;

	int mCmdCount;
	Dictionary<int, BfIRCodeGenEntry> mResults;
	Dictionary<int, BfIRTypeEntry> mTypes;
	Dictionary<int, llvm::Function*> mIntrinsicMap;
	Dictionary<llvm::Function*, int> mIntrinsicReverseMap;
	Array<llvm::Constant*> mConfigConsts32;
	Array<llvm::Constant*> mConfigConsts64;
	Dictionary<llvm::Type*, llvm::Value*> mReflectDataMap;
	Dictionary<llvm::Type*, llvm::Type*> mAlignedTypeToNormalType;
	Dictionary<llvm::Type*, int> mTypeToTypeIdMap;
	HashSet<llvm::BasicBlock*> mLockedBlocks;
	OwnedArray<BfIRIntrinsicData> mIntrinsicData;
	Dictionary<llvm::Function*, BfIRSimdType> mFunctionsUsingSimd;

public:
	void InitTarget();
	void FixValues(llvm::StructType* structType, llvm::SmallVector<llvm::Value*, 8>& values);
	void FixIndexer(llvm::Value*& val);
	BfTypeCode GetTypeCode(llvm::Type* type, bool isSigned);
	llvm::Type* GetLLVMType(BfTypeCode typeCode, bool& isSigned);
	BfIRTypeEntry& GetTypeEntry(int typeId);
	BfIRTypeEntry* GetTypeEntry(llvm::Type* type);
	void SetResult(int id, llvm::Value* value);
	void SetResultAligned(int id, llvm::Value* value);
	void SetResult(int id, llvm::Type* value);
	void SetResult(int id, llvm::BasicBlock* value);
	void SetResult(int id, llvm::MDNode* value);
	void CreateMemSet(llvm::Value* addr, llvm::Value* val, llvm::Value* size, int alignment, bool isVolatile = false);
	void AddNop();
	llvm::Value* TryToVector(llvm::Value* value);
	llvm::Value* TryToVector(llvm::Value* value, llvm::Type* elemType);
	llvm::Type* GetElemType(llvm::Value* value);
	bool TryMemCpy(llvm::Value* ptr, llvm::Value* val);
	bool TryVectorCpy(llvm::Value* ptr, llvm::Value* val);
	llvm::Type* GetSizeAlignedType(BfIRTypeEntry* typeEntry);
	llvm::Value* GetAlignedPtr(llvm::Value* val);
	llvm::Value* FixGEP(llvm::Value* fromValue, llvm::Value* result);
	llvm::Value* DoCheckedIntrinsic(llvm::Intrinsic::ID intrin, llvm::Value* lhs, llvm::Value* rhs, bool useAsm);

public:
	BfIRCodeGen();
	~BfIRCodeGen();

	void FatalError(const StringImpl& str);
	virtual void Fail(const StringImpl& error) override;

	void ProcessBfIRData(const BfSizedArray<uint8>& buffer) override;
	void PrintModule();
	void PrintFunction();

	int64 ReadSLEB128();
	void Read(StringImpl& str);
	void Read(int& i);
	void Read(int64& i);
	void Read(Val128& i);
	void Read(bool& val);
	void Read(int8& val);
	void Read(BfIRTypeEntry*& type);
	void Read(llvm::Type*& llvmType, BfIRTypeEntry** outTypeEntry = NULL);
	void Read(llvm::FunctionType*& llvmType);
	void Read(llvm::Value*& llvmValue, BfIRCodeGenEntry** codeGenEntry = NULL, BfIRSizeAlignKind sizeAlignKind = BfIRSizeAlignKind_Original);
	void Read(llvm::Constant*& llvmConstant, BfIRSizeAlignKind sizeAlignKind = BfIRSizeAlignKind_Original);
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

	void Read(llvm::SmallVectorImpl<llvm::Value*>& vec, BfIRSizeAlignKind sizeAlignKind = BfIRSizeAlignKind_Original)
	{
		int len = (int)ReadSLEB128();
		for (int i = 0; i < len; i++)
		{
			llvm::Value* result;
			Read(result, NULL, sizeAlignKind);
			vec.push_back(result);
		}
	}

	void Read(llvm::SmallVectorImpl<llvm::Constant*>& vec, BfIRSizeAlignKind sizeAlignKind = BfIRSizeAlignKind_Original)
	{
		int len = (int)ReadSLEB128();
		for (int i = 0; i < len; i++)
		{
			llvm::Constant* result;
			Read(result, sizeAlignKind);
			vec.push_back(result);
		}
	}

	void HandleNextCmd() override;
	void SetCodeGenOptions(BfCodeGenOptions codeGenOptions);
	void SetConfigConst(int idx, int value) override;

	void SetActiveFunctionSimdType(BfIRSimdType type);
	String GetSimdTypeString(BfIRSimdType type);
	BfIRSimdType GetSimdTypeFromFunction(llvm::Function* function);

	llvm::Value* GetLLVMValue(int streamId);
	llvm::Type* GetLLVMType(int streamId);
	llvm::BasicBlock* GetLLVMBlock(int streamId);
	llvm::MDNode* GetLLVMMetadata(int streamId);

	llvm::Type* GetLLVMTypeById(int id);

	///

	bool WriteObjectFile(const StringImpl& outFileName);
	bool WriteIR(const StringImpl& outFileName, StringImpl& error);

	void ApplySimdFeatures();

	static int GetIntrinsicId(const StringImpl& name);
	static const char* GetIntrinsicName(int intrinId);
	static void SetAsmKind(BfAsmKind asmKind);

	static void StaticInit();
};

NS_BF_END
