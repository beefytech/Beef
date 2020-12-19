#include "BeContext.h"

USING_NS_BF;

BeContext::BeContext()
{
	mPointerSize = 8;
	for (int primIdx = 0; primIdx < (int)BeTypeCode_COUNT; primIdx++)
		mPrimitiveTypes[primIdx] = NULL;
}

void BeContext::NotImpl()
{
	BF_FATAL("Not implemented");
}

BeType* BeContext::GetPrimitiveType(BeTypeCode typeCode)
{
	if (typeCode == BeTypeCode_NullPtr)
	{
		return GetPointerTo(GetPrimitiveType(BeTypeCode_None));
	}

	if (mPrimitiveTypes[(int)typeCode] != NULL)
		return mPrimitiveTypes[(int)typeCode];

	BeType* primType = mTypes.Alloc<BeType>();
	primType->mTypeCode = typeCode;
	switch (typeCode)
	{
	case BeTypeCode_None:		
		primType->mSize = 0;
		primType->mAlign = 0;
		break;
	case BeTypeCode_NullPtr:
		primType->mSize = primType->mAlign = mPointerSize;
		break;
	case BeTypeCode_Boolean:
		primType->mSize = primType->mAlign = 1;
		break;
	case BeTypeCode_Int8:
		primType->mSize = primType->mAlign = 1;
		break;
	case BeTypeCode_Int16:
		primType->mSize = primType->mAlign = 2;
		break;
	case BeTypeCode_Int32:
		primType->mSize = primType->mAlign = 4;
		break;
	case BeTypeCode_Int64:
		primType->mSize = primType->mAlign = 8;
		break;
	case BeTypeCode_Float:
		primType->mSize = primType->mAlign = 4;
		break;
	case BeTypeCode_Double:
		primType->mSize = primType->mAlign = 8;
		break;
	case BeTypeCode_M128:
		primType->mSize = primType->mAlign = 16;
		break;
	case BeTypeCode_M256:
		primType->mSize = primType->mAlign = 32;
		break;
	case BeTypeCode_M512:
		primType->mSize = primType->mAlign = 64;
		break;
	}
	mPrimitiveTypes[(int)typeCode] = primType;
	return primType;
}

BeType* BeContext::GetVoidPtrType()
{
	return GetPointerTo(GetPrimitiveType(BeTypeCode_None));
}

BeStructType* BeContext::CreateStruct(const StringImpl& name)
{
	BeStructType* structType = mTypes.Alloc<BeStructType>();
	structType->mContext = this;
	structType->mTypeCode = BeTypeCode_Struct;
	structType->mName = name;	
	structType->mIsOpaque = true;
	return structType;
}

BeStructType* BeContext::CreateStruct(const SizedArrayImpl<BeType*>& types)
{	
	BeStructType** valuePtr = NULL;	
	if (mAnonymousStructMap.TryGetValueWith(types, &valuePtr))	
		return *valuePtr;	

	Array<BeType*> key;
	for (auto type : types)
		key.Add(type);
	
	BeStructType* structType = CreateStruct("");
	SetStructBody(structType, types, false);
	mAnonymousStructMap.TryAdd(key, structType);	
	return structType;
}

BePointerType* BeContext::GetPointerTo(BeType* beType)
{
	if (beType->mPointerType == NULL)
	{
		BePointerType* pointerType = mTypes.Alloc<BePointerType>();
		pointerType->mTypeCode = BeTypeCode_Pointer;
		pointerType->mElementType = beType;
		pointerType->mSize = mPointerSize;
		pointerType->mAlign = mPointerSize;		
		beType->mPointerType = pointerType;

		/*if (beType->IsSizedArray())
		{
			auto sizedArrayType = (BeSizedArrayType*)beType;
			pointerType->mElementType = sizedArrayType->mElementType;
		}*/
	}
	return beType->mPointerType;
}

void BeContext::SetStructBody(BeStructType* structType, const SizedArrayImpl<BeType*>& types, bool packed)
{
	BF_ASSERT(structType->mMembers.IsEmpty());

	int dataPos = 0;
	for (auto& beType : types)
	{
		if (!packed)
		{
			int alignSize = beType->mAlign;
			dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
		}
		BF_ASSERT(beType->mSize >= 0);
		BeStructMember member;
		member.mType = beType;
		member.mByteOffset = dataPos;
		dataPos += beType->mSize;
		if (packed)
			structType->mAlign = 1;
		else
			structType->mAlign = std::max(structType->mAlign, beType->mAlign);
		structType->mMembers.push_back(member);
	}
	if (!packed)
	{
		int alignSize = structType->mAlign;
		dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
	}
	structType->mSize = dataPos;
	structType->mIsPacked = packed;
	structType->mIsOpaque = false;
}

BeSizedArrayType* BeContext::CreateSizedArrayType(BeType* type, int length)
{
	auto arrayType = mTypes.Alloc<BeSizedArrayType>();
	arrayType->mContext = this;
	arrayType->mTypeCode = BeTypeCode_SizedArray;
	arrayType->mElementType = type;
	arrayType->mLength = length;
	arrayType->mSize = type->mSize * length;
	arrayType->mAlign = type->mAlign;
	return arrayType;
}

BeVectorType* BeContext::CreateVectorType(BeType* type, int length)
{
	auto arrayType = mTypes.Alloc<BeVectorType>();
	arrayType->mContext = this;
	arrayType->mTypeCode = BeTypeCode_Vector;
	arrayType->mElementType = type;
	arrayType->mLength = length;
	arrayType->mSize = type->mSize * length;
	arrayType->mAlign = type->mAlign;
	return arrayType;
}

BeFunctionType* BeContext::CreateFunctionType(BeType* returnType, const SizedArrayImpl<BeType*>& paramTypes, bool isVarArg)
{
	auto funcType = mTypes.Alloc<BeFunctionType>();
	funcType->mTypeCode = BeTypeCode_Function;
	funcType->mReturnType = returnType;
	for (auto& paramType : paramTypes)
	{
		BeFunctionTypeParam funcParam;
		funcParam.mType = paramType;
		funcType->mParams.push_back(funcParam);
	}
	funcType->mIsVarArg = isVarArg;
	return funcType;
}

bool BeContext::AreTypesEqual(BeType* lhs, BeType* rhs)
{
	if (lhs == rhs)
		return true;
	
	if (lhs->mTypeCode != rhs->mTypeCode)
		return false;

	switch (lhs->mTypeCode)
	{	
 	case BeTypeCode_None:
 	case BeTypeCode_NullPtr:
 	case BeTypeCode_Boolean:
 	case BeTypeCode_Int8:
 	case BeTypeCode_Int16:
 	case BeTypeCode_Int32:
 	case BeTypeCode_Int64:
 	case BeTypeCode_Float:
 	case BeTypeCode_Double:
		return true;
	case BeTypeCode_Pointer:
		return AreTypesEqual(((BePointerType*)lhs)->mElementType, ((BePointerType*)rhs)->mElementType);	
	case BeTypeCode_SizedArray:
		{
			auto lhsSizedArray = (BeSizedArrayType*)lhs;
			auto rhsSizedArray = (BeSizedArrayType*)rhs;
			if (lhsSizedArray->mLength != rhsSizedArray->mLength)
				return false;
			return AreTypesEqual(lhsSizedArray->mElementType, rhsSizedArray->mElementType);
		}	
	case BeTypeCode_Vector:
		{
			auto lhsSizedArray = (BeVectorType*)lhs;
			auto rhsSizedArray = (BeVectorType*)rhs;
			if (lhsSizedArray->mLength != rhsSizedArray->mLength)
				return false;
			return AreTypesEqual(lhsSizedArray->mElementType, rhsSizedArray->mElementType);
		}
	}
	return false;
}

