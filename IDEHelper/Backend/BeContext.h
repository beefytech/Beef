#pragma once

#include "../Beef/BfCommon.h"
#include "../Compiler/BfCompiler.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_BEGIN

class BePointerType;
class BeContext;

enum BeTypeCode
{
	BeTypeCode_None,
	BeTypeCode_NullPtr,
	BeTypeCode_Boolean,
	BeTypeCode_Int8,
	BeTypeCode_Int16,
	BeTypeCode_Int32,
	BeTypeCode_Int64,
	BeTypeCode_Float,
	BeTypeCode_Double,
	BeTypeCode_M128,
	BeTypeCode_M256,
	BeTypeCode_M512,
	BeTypeCode_Struct,
	BeTypeCode_Function,
	BeTypeCode_Pointer,
	BeTypeCode_SizedArray,
	BeTypeCode_Vector,
	BeTypeCode_CmpResult, // Psuedo

	BeTypeCode_COUNT
};

class BeHashContext : public HashContext
{
public:
	int mCurHashId;

	BeHashContext()
	{
		mCurHashId = 1;
	}
};

class BeHashble
{
public:
	int mHashId;

	BeHashble()
	{
		mHashId = -1;
	}

	virtual void HashContent(BeHashContext& hashCtx) = 0;

	void HashReference(BeHashContext& hashCtx)
	{
		if (mHashId == -1)
		{
			mHashId = hashCtx.mCurHashId++;
			hashCtx.Mixin(mHashId);
			HashContent(hashCtx);
		}
		else
			hashCtx.Mixin(mHashId);
	}
};

class BeType : public BeHashble
{
public:
	BeTypeCode mTypeCode;
	BePointerType* mPointerType;
	int mSize;
	int mAlign;

public:
	BeType()
	{
		mTypeCode = BeTypeCode_None;
		mPointerType = NULL;
		mSize = -1;
		mAlign = -1;
	}

	virtual ~BeType()
	{
	}

	int GetStride()
	{
		return BF_ALIGN(mSize, mAlign);
	}

	bool IsPointer()
	{
		return (mTypeCode == BeTypeCode_Pointer) || (mTypeCode == BeTypeCode_NullPtr);
	}

	bool IsInt()
	{
		return (mTypeCode >= BeTypeCode_Int8) && (mTypeCode <= BeTypeCode_Int64);
	}

	bool IsIntable()
	{
		return ((mTypeCode >= BeTypeCode_Boolean) && (mTypeCode <= BeTypeCode_Int64)) || (mTypeCode == BeTypeCode_Pointer);
	}

	bool IsIntegral()
	{
		return (mTypeCode >= BeTypeCode_Int8) && (mTypeCode <= BeTypeCode_Int64);
	}

	bool IsFloat()
	{
		return (mTypeCode == BeTypeCode_Float) || (mTypeCode == BeTypeCode_Double);
	}

	bool IsStruct()
	{
		return (mTypeCode == BeTypeCode_Struct);
	}

	bool IsSizedArray()
	{
		return (mTypeCode == BeTypeCode_SizedArray);
	}

	bool IsVector()
	{
		return (mTypeCode == BeTypeCode_Vector) || (mTypeCode == BeTypeCode_M128) || (mTypeCode == BeTypeCode_M256) || (mTypeCode == BeTypeCode_M512);
	}

	bool IsExplicitVectorType()
	{
		return (mTypeCode == BeTypeCode_Vector);
	}

	bool IsFloatOrVector()
	{
		return (mTypeCode == BeTypeCode_Float) || (mTypeCode == BeTypeCode_Double) ||
			(mTypeCode == BeTypeCode_Vector) || (mTypeCode == BeTypeCode_M128) || (mTypeCode == BeTypeCode_M256) || (mTypeCode == BeTypeCode_M512);
	}

	bool IsComposite()
	{
		return (mTypeCode == BeTypeCode_Struct) || (mTypeCode == BeTypeCode_SizedArray) || (mTypeCode == BeTypeCode_Vector) || (mTypeCode == BeTypeCode_M128) || (mTypeCode == BeTypeCode_M256) || (mTypeCode == BeTypeCode_M512);
	}

	bool IsNonVectorComposite()
	{
		return (mTypeCode == BeTypeCode_Struct) || (mTypeCode == BeTypeCode_SizedArray);
	}

	virtual void HashContent(BeHashContext& hashCtx)
	{
		BF_ASSERT(mTypeCode < BeTypeCode_Struct);
		hashCtx.Mixin(mTypeCode);
	}
};

class BeStructMember
{
public:
	BeType* mType;
	int mByteOffset;
};

class BeStructType : public BeType
{
public:
	BeContext* mContext;
	String mName;
	Array<BeStructMember> mMembers;
	bool mIsPacked;
	bool mIsOpaque;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.MixinStr(mName);
		hashCtx.Mixin(mMembers.size());
		for (auto& member : mMembers)
		{
			member.mType->HashReference(hashCtx);
			hashCtx.Mixin(member.mByteOffset);
		}
		hashCtx.Mixin(mIsPacked);
		hashCtx.Mixin(mIsOpaque);
	}
};

class BePointerType : public BeType
{
public:
	BeType* mElementType;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(BeTypeCode_Pointer);
		mElementType->HashReference(hashCtx);
	}
};

class BeSizedArrayType : public BeType
{
public:
	BeContext* mContext;
	BeType* mElementType;
	int mLength;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(BeTypeCode_SizedArray);
		hashCtx.Mixin(mLength);
		mElementType->HashReference(hashCtx);
	}
};

class BeVectorType : public BeType
{
public:
	BeContext* mContext;
	BeType* mElementType;
	int mLength;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(BeTypeCode_Vector);
		hashCtx.Mixin(mLength);
		mElementType->HashReference(hashCtx);
	}
};

class BeFunctionTypeParam
{
public:
	BeType* mType;
};

class BeFunctionType : public BeType
{
public:
	String mName;
	BeType* mReturnType;
	Array<BeFunctionTypeParam> mParams;
	bool mIsVarArg;

	virtual void HashContent(BeHashContext& hashCtx) override
	{
		hashCtx.Mixin(BeTypeCode_Function);
		hashCtx.MixinStr(mName);
		mReturnType->HashReference(hashCtx);
		hashCtx.Mixin(mParams.size());
		for (auto& param : mParams)
		{
			param.mType->HashReference(hashCtx);
		}
		hashCtx.Mixin(mIsVarArg);
	}
};

class BeContext
{
public:
	int mPointerSize;
	//BumpAllocator mAlloc;
	BeType* mPrimitiveTypes[BeTypeCode_COUNT];
	OwnedVector<BeType> mTypes;
	Dictionary<Array<BeType*>, BeStructType*> mAnonymousStructMap;

public:
	void NotImpl();

public:
	BeContext();
	BeType* GetPrimitiveType(BeTypeCode typeCode);
	BeType* GetVoidPtrType();
	BeStructType* CreateStruct(const StringImpl& name);
	BeStructType* CreateStruct(const SizedArrayImpl<BeType*>& types);
	BePointerType* GetPointerTo(BeType* beType);
	void SetStructBody(BeStructType* structType, const SizedArrayImpl<BeType*>& types, bool packed);
	BeSizedArrayType* CreateSizedArrayType(BeType* type, int length);
	BeVectorType* CreateVectorType(BeType* type, int length);
	BeFunctionType* CreateFunctionType(BeType* returnType, const SizedArrayImpl<BeType*>& paramTypes, bool isVarArg);

	bool AreTypesEqual(BeType* lhs, BeType* rhs);
};

NS_BF_END
