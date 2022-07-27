#pragma once

#include "../Beef/BfCommon.h"
#include "../Compiler/BfIRBuilder.h"
#include "BeContext.h"
#include "BeModule.h"

NS_BF_BEGIN

class BeModule;
class BeDbgType;
class BfIRBuilder;
class ChunkedDataBuffer;

enum BeIRCodeGenEntryKind
{
	BeIRCodeGenEntryKind_None,
	BeIRCodeGenEntryKind_Value,
	BeIRCodeGenEntryKind_Type,
	BeIRCodeGenEntryKind_Block,
	BeIRCodeGenEntryKind_Metadata
};

struct BeIRCodeGenEntry
{
	BeIRCodeGenEntryKind mKind;
	union
	{
		BeValue* mBeValue;
		BeType* mBeType;
		BeBlock* mBeBlock;
		BeMDNode* mBeMetadata;
	};
};

class BeIRTypeEntry
{
public:
	int mTypeId;
	BeDbgType* mDIType;
	BeDbgType* mInstDIType;
	BeType* mBeType;
	BeType* mInstBeType;

public:
	BeIRTypeEntry()
	{
		mTypeId = -1;
		mDIType = NULL;
		mInstDIType = NULL;
		mBeType = NULL;
		mInstBeType = NULL;
	}
};

class BeState
{
public:
	BeFunction* mActiveFunction;
	Array<BeDbgLoc*> mSavedDebugLocs;
	bool mHasDebugLoc;

	BeBlock* mActiveBlock;
	int mInsertPos;
	BeDbgLoc* mCurDbgLoc;
	BeDbgLoc* mPrevDbgLocInline;
	BeDbgLoc* mLastDbgLoc;
};

template <typename T>
class CmdParamVec : public SizedArray<T, 8>
{};

class BeIRCodeGen : public BfIRCodeGenBase
{
public:
	bool mDebugging;

	BfIRBuilder* mBfIRBuilder;
	BeFunction* mActiveFunction;

	BeContext* mBeContext;
	BeModule* mBeModule;
	Array<BeDbgLoc*> mSavedDebugLocs;
	bool mHasDebugLoc;

	int mCmdCount;
	Dictionary<int, BeIRCodeGenEntry> mResults;
	Dictionary<int, BeIRTypeEntry> mTypes;

	Dictionary<BeType*, BeDbgType*> mOnDemandTypeMap;
	Dictionary<BeType*, BeValue*> mReflectDataMap;
	Array<int> mConfigConsts;

public:
	void FatalError(const StringImpl& str);
	void NotImpl();
	BfTypeCode GetTypeCode(BeType* type, bool isSigned);
	void SetResult(int id, BeValue* value);
	void SetResult(int id, BeType* type);
	void SetResult(int id, BeBlock* value);
	void SetResult(int id, BeMDNode* md);

	BeType* GetBeType(BfTypeCode typeCode, bool& isSigned);
	BeIRTypeEntry& GetTypeEntry(int typeId);

	void FixValues(BeStructType* structType, CmdParamVec<BeValue*>& values);

public:
	BeIRCodeGen();
	~BeIRCodeGen();

	void Hash(BeHashContext& hashCtx);
	bool IsModuleEmpty();

	int64 ReadSLEB128();
	void Read(StringImpl& str);
	void Read(int& i);
	void Read(int64& i);
	void Read(Val128& i);
	void Read(bool& val);
	void Read(int8& val);
	void Read(BeIRTypeEntry*& type);
	void Read(BeType*& beType);
	void Read(BeFunctionType*& beType);
	void Read(BeValue*& beValue);
	void Read(BeConstant*& beConstant);
	void Read(BeFunction*& beFunc);
	void Read(BeBlock*& beBlock);
	void Read(BeMDNode*& beMD);

	template <typename T>
	void Read(SizedArrayImpl<T>& vec)
	{
		int len = (int)ReadSLEB128();
		for (int i = 0; i < len; i++)
		{
			T result;
			Read(result);
			vec.push_back(result);
		}
	}

	void Init(const BfSizedArray<uint8>& buffer);
	void Process();

	virtual void ProcessBfIRData(const BfSizedArray<uint8>& buffer) override;
	virtual void HandleNextCmd() override;
	virtual void SetConfigConst(int idx, int value) override;

	BeValue* GetBeValue(int streamId);
	BeType* GetBeType(int streamId);
	BeBlock* GetBeBlock(int streamId);
	BeMDNode* GetBeMetadata(int streamId);

	BeType* GetBeTypeById(int id);

	BeState GetState();
	void SetState(const BeState& state);
};

NS_BF_END