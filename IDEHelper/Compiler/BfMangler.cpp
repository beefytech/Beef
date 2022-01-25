#include "BfMangler.h"
#include "BfDemangler.h"
#include "BfCompiler.h"
#pragma warning(disable:4996)

USING_NS_BF;

int BfGNUMangler::ParseSubIdx(StringImpl& name, int strIdx)
{
	const char* charPtr = name.c_str() + strIdx + 1;
	if (*charPtr == '_')
		return 0;
	int idx = atoi(charPtr) + 1;
	return idx;
}

void BfGNUMangler::AddSubIdx(StringImpl& name, int subIdx)
{
	name += 'S';	
	if (subIdx != 0)
	{
		int showIdx = subIdx - 1;
		if (showIdx > 36)
		{
			name += '0' + (showIdx / 36);
			showIdx %= 36;
		}
		if (showIdx < 10)
			name += '0' + showIdx;
		else
			name += 'A' + (showIdx - 10);
	}	
	name += '_';
}

void BfGNUMangler::AddSizedString(StringImpl& name, const StringImpl& addStr)
{
	char str[16];
	itoa((int)addStr.length(), str, 10);
	name += str;
	name += addStr;
}

BfTypeCode BfGNUMangler::GetPrimTypeAt(MangleContext& mangleContext, StringImpl& name, int strIdx)
{
	char startChar = name[strIdx];

	auto module = mangleContext.mModule;
	switch (startChar)
	{
	case 'v': return BfTypeCode_None;
	case 'b': return BfTypeCode_Boolean;
	case 'a': return BfTypeCode_Int8;
	case 'h': return BfTypeCode_UInt8;
	case 's': return BfTypeCode_Int16;
	case 't': return BfTypeCode_UInt16;
	case 'i': return BfTypeCode_Int32;
	case 'j': return BfTypeCode_UInt32;
    case 'l': return BfTypeCode_Int64;
    case 'm': return BfTypeCode_UInt64;
    case 'x': return BfTypeCode_Int64;
    case 'y': return BfTypeCode_UInt64;
	case 'u': 
		if (name[strIdx + 1] == '3')
			return BfTypeCode_IntPtr;
		if (name[strIdx + 1] == '4')
			return BfTypeCode_UIntPtr;
		break;	
	case 'c': return BfTypeCode_Char8;
	case 'D': 
		if (name[strIdx + 1] == 'i')
			return BfTypeCode_Char32;
		else if (name[strIdx + 1] == 's')
			return BfTypeCode_Char16;
		break;
	case 'f': return BfTypeCode_Float;		
	case 'd': return BfTypeCode_Double;
	}
	return (BfTypeCode)-1;
}

void BfGNUMangler::AddPrefix(MangleContext& mangleContext, StringImpl& name, int startIdx, const char* prefix)
{
	int subIdx;
	char startChar = name[startIdx];
	if (startChar == 'S')
	{
		subIdx = ParseSubIdx(name, startIdx);
		for (int matchIdx = subIdx + 1; matchIdx < (int)mangleContext.mSubstituteList.size(); matchIdx++)
		{
			auto& entry = mangleContext.mSubstituteList[matchIdx];
			if ((entry.mKind == NameSubstitute::Kind_Prefix) && (entry.mExtendsIdx == subIdx) && (entry.mPrefix == prefix))
			{
				BF_ASSERT(name.EndsWith('_'));
				name.RemoveToEnd(startIdx);
				AddSubIdx(name, matchIdx);				
				return;
			}
		}
	}
	else
	{
		auto typeCode = GetPrimTypeAt(mangleContext, name, startIdx);
		if (typeCode != (BfTypeCode)-1)
		{
			for (int matchIdx = 0; matchIdx < (int)mangleContext.mSubstituteList.size(); matchIdx++)
			{
				auto& entry = mangleContext.mSubstituteList[matchIdx];
				if ((entry.mKind == NameSubstitute::Kind_PrimitivePrefix) && (entry.mExtendsTypeCode == typeCode) && (entry.mPrefix == prefix))
				{					
					name.RemoveToEnd(startIdx);
					AddSubIdx(name, matchIdx);				
					return;
				}
			}

			NameSubstitute nameSub;
			nameSub.mKind = NameSubstitute::Kind_PrimitivePrefix;
			nameSub.mExtendsTypeCode = typeCode;
			nameSub.mPrefix = prefix;
			mangleContext.mSubstituteList.push_back(nameSub);

			name.Insert(startIdx, prefix);
			return;
		}
		else
		{
			// Applies to last-added one		
			subIdx = (int)mangleContext.mSubstituteList.size() - 1;
            BF_ASSERT(isdigit(startChar) || (startChar == 'A') || (startChar == 'N') || (startChar == 'P') || (startChar == 'R') || (startChar == 'U'));
		}
	}

	NameSubstitute nameSub;
	nameSub.mKind = NameSubstitute::Kind_Prefix;
	nameSub.mExtendsIdx = subIdx;
	nameSub.mPrefix = prefix;
	mangleContext.mSubstituteList.push_back(nameSub);

	name.Insert(startIdx, prefix);
}

void BfGNUMangler::FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub)
{
	int curMatchIdx = -1;
	bool matchFailed = false;
	FindOrCreateNameSub(mangleContext, name, newNameSub, curMatchIdx, matchFailed);
	if (!matchFailed)
		AddSubIdx(name, curMatchIdx);	
}

void BfGNUMangler::FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub, int& curMatchIdx, bool& matchFailed)
{	
	int parentIdx = curMatchIdx;
	if (!matchFailed)
	{		
		curMatchIdx++;
		for ( ; curMatchIdx < (int)mangleContext.mSubstituteList.size(); curMatchIdx++)
		{
			auto& entry = mangleContext.mSubstituteList[curMatchIdx];
			if ((entry.mExtendsIdx == parentIdx) && (entry.mKind == newNameSub.mKind) && (entry.mParam == newNameSub.mParam))
			{				
				return;
			}
		}
		
		matchFailed = true;
		if (newNameSub.mKind != BfGNUMangler::NameSubstitute::Kind_GenericParam)
			name += "N";
		if (parentIdx != -1)
			AddSubIdx(name, parentIdx);		
	}
	
	if (newNameSub.mKind == NameSubstitute::Kind_NamespaceAtom)
	{
		AddSizedString(name, newNameSub.mAtom->mString.mPtr);
	}	
	else if (newNameSub.mKind == NameSubstitute::Kind_TypeInstName)
	{
		auto typeDef = newNameSub.mTypeInst->mTypeDef;
		// Mixing this in could create linking errors since the references to the "correct" version won't necessarily be recompiled
		//  when we remove the "incorrect" version.  I believe this is no longer needed since our compilation model has changed
		/*if (typeDef->mDupDetectedRevision != -1)
		{
			char str[64];			
			sprintf(str, "_%p", typeDef);
			name += str;
		}*/
		if ((typeDef->mIsDelegate) && (newNameSub.mTypeInst->IsClosure()))
		{			
			auto closureType = (BfClosureType*)newNameSub.mTypeInst;
			if (!closureType->mCreatedTypeDef)			
				name += closureType->mNameAdd;			
		}
		AddSizedString(name, typeDef->mName->mString.mPtr);
	}
	else if (newNameSub.mKind == BfGNUMangler::NameSubstitute::Kind_TypeGenericArgs)
	{
		int genericParamStart = 0;
		if (newNameSub.mTypeInst->mTypeDef->mOuterType != NULL)
			genericParamStart = (int)newNameSub.mTypeInst->mTypeDef->mOuterType->mGenericParamDefs.size();
		
		name += "I";

		auto typeDef = newNameSub.mTypeInst->mTypeDef;

		BfType* checkType = newNameSub.mTypeInst;
		if (checkType->IsClosure())
		{
			checkType = ((BfClosureType*)checkType)->mSrcDelegate;
		}

		BF_ASSERT(checkType->IsGenericTypeInstance());
		BfTypeInstance* genericTypeInstance = (BfTypeInstance*)checkType;

		for (int genericParamIdx = genericParamStart; genericParamIdx < (int) typeDef->mGenericParamDefs.size(); genericParamIdx++)
		{	
			auto genericParam = genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments[genericParamIdx];

			Mangle(mangleContext, name, genericParam);			
		}

		name += "E";
	}
	else if (newNameSub.mKind == BfGNUMangler::NameSubstitute::Kind_GenericParam)
	{
		auto genericParamType = (BfGenericParamType*)newNameSub.mType;
		char str[16];
		if (genericParamType->mGenericParamIdx < 10)
			name += "3";
		else
			name += "4";		
		if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
			name += "`M";
		else
			name += "`T";		
		itoa(genericParamType->mGenericParamIdx, str, 10);
		name += str;
	}
		
	curMatchIdx = (int)mangleContext.mSubstituteList.size();
	mangleContext.mSubstituteList.push_back(newNameSub);
	
	auto& nameSubRef = mangleContext.mSubstituteList.back();
	nameSubRef.mExtendsIdx = parentIdx;
}

void BfGNUMangler::FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInst, int& curMatchIdx, bool& matchFailed)
{
	auto typeDef = typeInst->mTypeDef;
	if (typeDef->IsGlobalsContainer())
		return;

	int numOuterGenericParams = 0;
	if (typeDef->mOuterType != NULL)
	{
		numOuterGenericParams = (int)typeDef->mOuterType->mGenericParamDefs.size();
		auto useModule = typeInst->mModule;
		if (useModule == NULL)
			useModule = mangleContext.mModule;
		auto outerType = useModule->GetOuterType(typeInst);
		if (outerType != NULL)
			FindOrCreateNameSub(mangleContext, name, outerType, curMatchIdx, matchFailed);
		else
			useModule->Fail("Failed to mangle name in BfGNUMangler::FindOrCreateNameSub");
	}

	FindOrCreateNameSub(mangleContext, name, NameSubstitute(NameSubstitute::Kind_TypeInstName, typeInst), curMatchIdx, matchFailed);

	if (typeDef->mGenericParamDefs.size() != numOuterGenericParams)
	{
		FindOrCreateNameSub(mangleContext, name, NameSubstitute(NameSubstitute::Kind_TypeGenericArgs, typeInst), curMatchIdx, matchFailed);	
	}
}

void BfGNUMangler::MangleTypeInst(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInst, BfTypeInstance* postfixTypeInstance, bool* isEndOpen)
{		
	static int sCallCount = 0;
	sCallCount++;
	
	if (typeInst->IsTuple())
	{		
		auto tupleType = (BfTypeInstance*)typeInst;
		name += "N7__TUPLEI";
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for '__TUPLE'
		for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			String fieldName = fieldDef->mName;
			if ((fieldName[0] < '0') || (fieldName[0] > '9'))
				name += StrFormat("U%d`%s", fieldName.length() + 1, fieldName.c_str());
			Mangle(mangleContext, name, fieldInstance->mResolvedType, postfixTypeInstance);
		}
		name += "E";
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for '__TUPLE<T>'
		if (isEndOpen != NULL)		
			*isEndOpen = true;
		else
			name += "E";
		return;
	}
	else if ((typeInst->IsDelegateFromTypeRef()) || (typeInst->IsFunctionFromTypeRef()))
	{
		BF_ASSERT(typeInst->mTypeDef->mMethods[0]->mName == "Invoke");
		auto delegateInfo = typeInst->GetDelegateInfo();
		auto methodDef = typeInst->mTypeDef->mMethods[0];

		if (typeInst->IsDelegate())
			name += "N8delegateI";
		else
			name += "N8functionI";
		SizedArray<BfType*, 8> typeVec;
		typeVec.push_back(BfNodeDynCast<BfDirectTypeReference>(methodDef->mReturnTypeRef)->mType);		
		if (methodDef->mIsMutating)
			name += "_mut_";
		for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)
		{
			name += "_";
			name += methodDef->mParams[paramIdx]->mName;
			if (methodDef->mParams[paramIdx]->mParamKind == BfParamKind_VarArgs)
			{
				name += "__varargs";
				continue;
			}
			typeVec.push_back(BfNodeDynCast<BfDirectTypeReference>(methodDef->mParams[paramIdx]->mTypeRef)->mType);
		}
		for (auto type : typeVec)
			Mangle(mangleContext, name, type, postfixTypeInstance);
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for 'Delegate'
		name += "E";
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for 'Delegate<T>'
		if (isEndOpen != NULL)
			*isEndOpen = true;
		else
			name += "E";
		name += "E";
	}
	else if (typeInst->IsBoxed())	
	{
		auto boxedType = (BfBoxedType*)typeInst;		
		name += "N3BoxI";
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for 'Box'		
		Mangle(mangleContext, name, boxedType->GetModifiedElementType(), postfixTypeInstance);
		name += "E";		
		mangleContext.mSubstituteList.push_back(NameSubstitute(BfGNUMangler::NameSubstitute::Kind_None, NULL)); // Insert entry for 'Box<T>'
		if (isEndOpen != NULL)		
			*isEndOpen = true;
		else
			name += "E";
		return;
	}

	int curMatchIdx = -1;
	bool matchFailed = false;
	for (int typeIdx = 0; typeIdx < 2; typeIdx++)
	{
		BfTypeInstance* useTypeInst = typeInst;
		if (typeIdx == 1)
		{
			if (postfixTypeInstance == NULL)
				break;
			useTypeInst = postfixTypeInstance;
		}

		auto typeDef = useTypeInst->mTypeDef;

		if (!mangleContext.mCPPMangle)
			FindOrCreateNameSub(mangleContext, name, NameSubstitute(NameSubstitute::Kind_NamespaceAtom, typeInst->mModule->mSystem->mBfAtom), curMatchIdx, matchFailed);
		
		for (int i = 0; i < typeDef->mNamespace.mSize; i++)
			FindOrCreateNameSub(mangleContext, name, NameSubstitute(NameSubstitute::Kind_NamespaceAtom, typeDef->mNamespace.mParts[i]), curMatchIdx, matchFailed);
		
		FindOrCreateNameSub(mangleContext, name, useTypeInst, curMatchIdx, matchFailed);
	}
	if (isEndOpen != NULL)
	{		
		if (!matchFailed)
		{			
			if (curMatchIdx != -1)
			{
				name += "N";
				AddSubIdx(name, curMatchIdx);			
				*isEndOpen = true;
			}
			else
				*isEndOpen = false;
		}
		else
			*isEndOpen = true;
		return;
	}

	if (matchFailed)
	{
		name += "E";
	}
	else
	{
		AddSubIdx(name, curMatchIdx);		
	}
}

void BfGNUMangler::Mangle(MangleContext& mangleContext, StringImpl& name, BfType* type, BfType* postfixType, bool isConst)
{	
	static int sCallCount = 0;
	sCallCount++;
	
	if (type->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)type;		

		switch (primType->mTypeDef->mTypeCode)
		{
		case BfTypeCode_NullPtr:
			{
				auto pointerType = (BfPointerType*)type;
				int startIdx = (int)name.length();	
				name += "v";
				AddPrefix(mangleContext, name, startIdx, "P");		
			}
			return;
		case BfTypeCode_Dot:			
			name += "U3dot";
			return;
		case BfTypeCode_None:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
				name += "v";
			else
				name += "U4void";
			return;		
		case BfTypeCode_Self:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
				name += "U8concrete";
			else
				name += "U4self";
			return;
		case BfTypeCode_Boolean:
			name += "b"; return;
		case BfTypeCode_Int8:
			name += "a"; return;
		case BfTypeCode_UInt8:
			name += "h"; return;
		case BfTypeCode_Int16:
			name += "s"; return;
		case BfTypeCode_UInt16:
			name += "t"; return;
		case BfTypeCode_Int32:
			name += "i"; return;
		case BfTypeCode_UInt32:
			name += "j"; return;
        case BfTypeCode_Int64:
			if (mangleContext.mModule->mCompiler->mOptions.mCLongSize == 8)
				name += "l";
			else
				name += "x";
			return;
        case BfTypeCode_UInt64:
			if (mangleContext.mModule->mCompiler->mOptions.mCLongSize == 8)
				name += "m";
			else
				name += "y";
			return;
		case BfTypeCode_UIntPtr:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
			{
				if (mangleContext.mModule->mCompiler->mOptions.mCLongSize == 8)
					name += (primType->mSize == 8) ? "m" : "j";
				else
					name += (primType->mSize == 8) ? "y" : "j";
				return;
			}
			name += "u4uint";
			return;
		case BfTypeCode_IntPtr:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
			{
				if (mangleContext.mModule->mCompiler->mOptions.mCLongSize == 8)
					name += (primType->mSize == 8) ? "l" : "i";
				else
					name += (primType->mSize == 8) ? "x" : "i";
				return;
			}
			name += "u3int";
			return;
		case BfTypeCode_Char8:
			name += "c"; return;
		case BfTypeCode_Char16:
			name += "Ds"; return;
		case BfTypeCode_Char32:
			name += "Di"; return;
		case BfTypeCode_Float:
			name += "f"; return;
		case BfTypeCode_Double:
			name += "d"; return;

		case BfTypeCode_Var:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
				name += "v";
			else
				name += "U3var";
			return;
		case BfTypeCode_Let:
			name += "U3let"; return;
		case BfTypeCode_IntUnknown:
			name += "U4iunk"; return;
		case BfTypeCode_UIntUnknown:
			name += "U4uunk"; return;
		default: break;
		}

		name += "?"; return;
		//name += Mangle(primType->mTypeDef, NULL, addName, NULL, substituteList);
	}
	else if (type->IsTypeInstance())
	{		
		BfTypeInstance* postfixTypeInst = NULL;
		if (postfixType != NULL)
			postfixTypeInst = postfixType->ToTypeInstance();

		auto typeInstance = (BfTypeInstance*)type;	

		int startIdx = (int)name.length();
		MangleTypeInst(mangleContext, name, typeInstance, postfixTypeInst);
		if ((type->IsObjectOrInterface()) && (mangleContext.mPrefixObjectPointer))
			AddPrefix(mangleContext, name, startIdx, "P");		
	}	
	else if (type->IsGenericParam())
	{		
		FindOrCreateNameSub(mangleContext, name, NameSubstitute(NameSubstitute::Kind_GenericParam, type));
	}
	else if (type->IsPointer())
	{		
		auto pointerType = (BfPointerType*)type;
		int startIdx = (int)name.length();	
		Mangle(mangleContext, name, pointerType->mElementType);
		AddPrefix(mangleContext, name, startIdx, "P");		
		return;
	}
	else if (type->IsRef())
	{
		BfRefType* refType = (BfRefType*)type;
		if (refType->mRefKind == BfRefType::RefKind_In)
		{
			isConst = true;
		}
		else if ((refType->mRefKind == BfRefType::RefKind_Mut) && (!mangleContext.mCCompat))
		{
			name += "U3mut";
			Mangle(mangleContext, name, refType->mElementType);
			return;
		}		
		else if ((refType->mRefKind == BfRefType::RefKind_Out) && (!mangleContext.mCCompat))
		{
			name += "U3out";
			Mangle(mangleContext, name, refType->mElementType);
			return;
		}
		int startIdx = (int)name.length();		
		Mangle(mangleContext, name, refType->mElementType);	
		AddPrefix(mangleContext, name, startIdx, isConst ? "RK" : "R");
		return;
	}
	else if (type->IsModifiedTypeType())
	{
		BfModifiedTypeType* retTypeType = (BfModifiedTypeType*)type;
		if (retTypeType->mModifiedKind == BfToken_RetType)
			name += "U7rettype";
		else if (retTypeType->mModifiedKind == BfToken_AllocType)
			name += "U5alloc";
		else if (retTypeType->mModifiedKind == BfToken_Nullable)
			name += "U8nullable";
		else
			BF_FATAL("Unhandled");
		Mangle(mangleContext, name, retTypeType->mElementType);
		return;
	}
	else if (type->IsConcreteInterfaceType())
	{
		BfConcreteInterfaceType* concreteInterfaceType = (BfConcreteInterfaceType*)type;
		name += "U8concrete";
		Mangle(mangleContext, name, concreteInterfaceType->mInterface);
		return;
	}
	else if (type->IsSizedArray())
	{
		if (type->IsUnknownSizedArrayType())
		{
			BfUnknownSizedArrayType* arrayType = (BfUnknownSizedArrayType*)type;
			name += "A_";
			Mangle(mangleContext, name, arrayType->mElementType);
			name += "_";
			Mangle(mangleContext, name, arrayType->mElementCountSource);
			return;
		}
		else
		{
			BfSizedArrayType* arrayType = (BfSizedArrayType*)type;
			name += StrFormat("A%d_", arrayType->mElementCount);
			Mangle(mangleContext, name, arrayType->mElementType);
			return;
		}
	}
	else if (type->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)type;				
		String mrefName = "mref_";
		String mangleName;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;
		if (methodInstance == NULL)
		{
			BF_ASSERT(!methodRefType->mMangledMethodName.IsEmpty());
			mangleName = methodRefType->mMangledMethodName;
		}
		else		
		{
			if (methodInstance->mIsAutocompleteMethod)
				name += "AUTOCOMPLETE";

			auto module = methodInstance->GetOwner()->mModule;
			if (module->mCompiler->mIsResolveOnly)
			{
				// There are cases where we will reprocess a method in ResolveOnly for things like
				//  GetSymbolReferences, so we will have duplicate live local methodInstances in those cases
				mrefName += HashEncode64((uint64)methodRefType->mMethodRef);
			}			
			else
			{
				mangleName = Mangle(methodInstance);
				methodRefType->mMangledMethodName = mangleName;
			}
		}

		if (!mangleName.IsEmpty())
		{			
			Val128 val128 = Hash128(mangleName.c_str(), (int)mangleName.length());
			mrefName += HashEncode128(val128);
		}

		//TODO: Make a 'U' name?
		name += mrefName;
	}
	else if (type->IsConstExprValue())
	{		
		BfConstExprValueType* constExprValueType = (BfConstExprValueType*)type;
		int64 val = constExprValueType->mValue.mInt64;		

		if ((!constExprValueType->mType->IsPrimitiveType()) ||
			(((BfPrimitiveType*)constExprValueType->mType)->mTypeDef->mTypeCode != BfTypeCode_IntPtr))
		{
			Mangle(mangleContext, name, constExprValueType->mType);
		}

		name += "$0";		

		if (val < 0)
		{
			name += "?";
			val = -val;
		}
		if ((val >= 1) && (val < 10))
		{
			name += (char)('0' + val - 1);
		}
		else
		{
			char str[64];
			char* strP = str + 63;
			*strP = 0;

			while (val > 0)
			{
				*(--strP) = (char)((val % 0x10) + 'A');
				val /= 0x10;				
			}

			name += strP;
			name += '`';
		}		

		if (constExprValueType->mValue.mTypeCode == BfTypeCode_Let)
			name += "Undef";
	}
	else
	{
		BF_FATAL("Not handled");	
	}
}

String BfGNUMangler::Mangle(BfType* type, BfModule* module)
{
	StringT<256> name;	
	name += "_ZTS";
	MangleContext mangleContext;		
	mangleContext.mModule = module;
	Mangle(mangleContext, name, type);
	return name;
}

String BfGNUMangler::Mangle(BfMethodInstance* methodInst)
{
	StringT<256> name;
		
	if ((methodInst->mMethodDef->mCLink) && (!methodInst->mMangleWithIdx))
	{		
		return methodInst->mMethodDef->mName;
	}

	auto methodDef = methodInst->mMethodDef;
	auto methodDeclaration = BfNodeDynCastExact<BfMethodDeclaration>(methodDef->mMethodDeclaration);
	auto typeInst = methodInst->GetOwner();
	auto typeDef = typeInst->mTypeDef;

	MangleContext mangleContext;	
	mangleContext.mModule = methodInst->GetOwner()->mModule;
	if (methodInst->mCallingConvention != BfCallingConvention_Unspecified)
		mangleContext.mCCompat = true;		
	bool isCMangle = false;	
	HandleCustomAttributes(methodInst->GetCustomAttributes(), typeInst->mConstHolder, mangleContext.mModule, name, isCMangle, mangleContext.mCPPMangle);
	if (isCMangle)	
		name += methodInst->mMethodDef->mName;
	if (!name.IsEmpty())
		return name;

	bool mangledMethodIdx = false;
	bool prefixLen = false;

	bool isNameOpen = false;
	name += "_Z";	
	MangleTypeInst(mangleContext, name, methodInst->mMethodInstanceGroup->mOwner, methodInst->GetExplicitInterface(), &isNameOpen);

	if (methodInst->GetForeignType() != NULL)
	{
		// This won't demangle correctly.  TODO: Do this 'correctly'
		MangleTypeInst(mangleContext, name, methodInst->GetForeignType());
	}
	
	mangleContext.mPrefixObjectPointer = true;
	StringT<128> methodName = methodInst->mMethodDef->mName;
	for (int i = 0; i < (int)methodName.length(); i++)
	{
		if (methodName[i] == '@')
			methodName[i] = '$';
	}
	if ((!mangleContext.mCPPMangle) && (!methodDef->mIsMutating) && (!methodDef->mIsStatic) && (methodInst->GetOwner()->IsValueType()))
		methodName += "__im";

	if (methodInst->mMethodDef->mIsOperator)
	{
		auto operatorDef = (BfOperatorDef*)methodInst->mMethodDef;
		if (operatorDef->mOperatorDeclaration->mIsConvOperator)
		{
			methodName = "cv";
			Mangle(mangleContext, methodName, methodInst->mReturnType);
		}
		else
		{
			switch (operatorDef->mOperatorDeclaration->mBinOp)
			{
			case BfBinaryOp_Add:
				methodName = "pl";
				break;
			case BfBinaryOp_Subtract:
				methodName = "mi";
				break;
			case BfBinaryOp_Multiply:
				methodName = "ml";
				break;
			case BfBinaryOp_OverflowAdd:
				methodName = "opl";
				break;
			case BfBinaryOp_OverflowSubtract:
				methodName = "omi";
				break;
			case BfBinaryOp_OverflowMultiply:
				methodName = "oml";
				break;
			case BfBinaryOp_Divide:
				methodName = "dv";
				break;
			case BfBinaryOp_Modulus:
				methodName = "rm";
				break;
			case BfBinaryOp_BitwiseAnd:
				methodName = "an";
				break;
			case BfBinaryOp_BitwiseOr:
				methodName = "or";
				break;
			case BfBinaryOp_ExclusiveOr:
				methodName = "eo";
				break;
			case BfBinaryOp_LeftShift:
				methodName = "ls";
				break;
			case BfBinaryOp_RightShift:
				methodName = "rs";
				break;
			case BfBinaryOp_Equality:
				methodName = "eq";
				break;
			case BfBinaryOp_InEquality:
				methodName = "ne";
				break;
			case BfBinaryOp_GreaterThan:
				methodName = "gt";
				break;
			case BfBinaryOp_LessThan:
				methodName = "lt";
				break;
			case BfBinaryOp_GreaterThanOrEqual:
				methodName = "ge";
				break;
			case BfBinaryOp_LessThanOrEqual:
				methodName = "le";
				break;
			case BfBinaryOp_ConditionalAnd:
				methodName = "aa";
				break;
			case BfBinaryOp_ConditionalOr:
				methodName = "oo";
				break;
			case BfBinaryOp_NullCoalesce:
				methodName = "2nc";
				break;
			case BfBinaryOp_Is:
				methodName = "2is";
				break;
			case BfBinaryOp_As:
				methodName = "2as";
				break;		
			default: break;
			}

			switch (operatorDef->mOperatorDeclaration->mUnaryOp)
			{
			case BfUnaryOp_AddressOf:
				methodName = "ad";
				break;
			case BfUnaryOp_Dereference:
				methodName = "de";
				break;
			case BfUnaryOp_Negate:
				methodName = "ng";
				break;
			case BfUnaryOp_Not:
				methodName = "nt";
				break;
			case BfUnaryOp_Positive:
				methodName = "ps";
				break;
			case BfUnaryOp_InvertBits:
				methodName = "co";
				break;
			case BfUnaryOp_Increment:
				methodName = "pp";
				break;
			case BfUnaryOp_Decrement:
				methodName = "mm";
				break;
			case BfUnaryOp_PostIncrement:
				methodName = "pp";
				break;
			case BfUnaryOp_PostDecrement:
				methodName = "mm";
				break;
			case BfUnaryOp_Ref:
				methodName = "3ref";
				break;
			case BfUnaryOp_Mut:
				methodName = "3mut";
				break;
			case BfUnaryOp_Out:
				methodName = "3out";
				break;
			default: break;
			}
		}
	}
	else if (methodInst->mMethodDef->mMethodType == BfMethodType_Ctor)
	{
		if (methodInst->mMethodDef->mIsStatic)
		{			
			methodName = "__BfStaticCtor";
			prefixLen = true;
		}
		else
			methodName = "C1";
	}
	else if (methodInst->mMethodDef->mMethodType == BfMethodType_Dtor)
	{
		if (methodInst->mMethodDef->mIsStatic)
		{			
			methodName = "__BfStaticDtor";
			prefixLen = true;
		}
		else
			methodName = "D1";
	}
	else
	{
		if (methodInst->mMangleWithIdx)
		{
			methodName += StrFormat("`%d", methodInst->mMethodDef->mIdx);
			mangledMethodIdx = true;
		}

		prefixLen = true;
	}

	if (methodDef->mCheckedKind == BfCheckedKind_Checked)
		name += "`CHK";
	else if (methodDef->mCheckedKind == BfCheckedKind_Unchecked)
		name += "`UCHK";

	if (methodDef->mHasComptime)
		name += "`COMPTIME";

	if (((methodInst->GetOwner()->mTypeDef->IsGlobalsContainer()) && 
		 ((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_Dtor) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))) ||
		((methodInst->mMethodDef->mDeclaringType->mPartialIdx != -1) && (methodInst->mMethodDef->mDeclaringType->IsExtension()) && 
		 (!methodInst->mIsForeignMethodDef) && (!methodInst->mMethodDef->mIsExtern) && 
		 ((!methodInst->mMethodDef->mIsOverride) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mMethodType == BfMethodType_Dtor))))
	{
		auto declType = methodInst->mMethodDef->mDeclaringType;
		BF_ASSERT(methodInst->GetOwner()->mTypeDef->mIsCombinedPartial);
		auto declProject = declType->mProject;
		bool addProjectName = (declProject != typeInst->mTypeDef->mProject);
		bool addIndex = true;

		if (typeInst->mTypeDef->IsGlobalsContainer())
		{
			addProjectName = true;

			if ((methodInst->mCallingConvention == BfCallingConvention_Cdecl) ||
				(methodInst->mCallingConvention == BfCallingConvention_Stdcall))
			{
				addProjectName = false;
				addIndex = false;
			}
		}

		if (addProjectName)
		{
			name += declProject->mName;
			name += "$";
		}
		if (addIndex)
			name += StrFormat("%d$", declType->mPartialIdx);
	}

	if ((methodInst->mMangleWithIdx) && (!mangledMethodIdx))
	{
		methodName += StrFormat("`%d", methodInst->mMethodDef->mIdx);
	}

	//

	if ((prefixLen) && (methodInst->mMethodInstanceGroup->mOwner->mTypeDef->IsGlobalsContainer()) && (methodInst->mMethodDef->mMethodDeclaration == NULL))
	{
		methodName += '`';
		methodName += methodInst->mMethodInstanceGroup->mOwner->mTypeDef->mProject->mName;
	}

	if (prefixLen)			
		AddSizedString(name, methodName);	
	else	
		name += methodName;
			
	if (methodInst->GetNumGenericArguments() != 0)
	{		
		auto& methodGenericArguments = methodInst->mMethodInfoEx->mMethodGenericArguments;
		NameSubstitute nameSub(NameSubstitute::Kind_MethodName, methodInst);
		nameSub.mExtendsIdx = (int)mangleContext.mSubstituteList.size() - 1;		
		mangleContext.mSubstituteList.push_back(nameSub);

		name += 'I';
		for (auto genericArg : methodGenericArguments)
			Mangle(mangleContext, name, genericArg);
		name += 'E';		
	}
	else if (methodInst->mMethodDef->mGenericParams.size() != 0)
	{
		NameSubstitute nameSub(NameSubstitute::Kind_MethodName, methodInst);
		nameSub.mExtendsIdx = (int)mangleContext.mSubstituteList.size() - 1;
		mangleContext.mSubstituteList.push_back(nameSub);

		name += 'I';
		for (auto genericArg : methodInst->mMethodDef->mGenericParams)
			name += 'v';
		name += 'E';
	}

	if (isNameOpen)
		name += 'E';
	
	if (methodInst->mMethodDef->mGenericParams.size() != 0)
		Mangle(mangleContext, name, methodInst->mReturnType);

	mangleContext.mInArgs = true;

	bool doExplicitThis = (!methodDef->mIsStatic) && (typeInst->IsTypedPrimitive());

	if (doExplicitThis) // Explicit "_this"
		Mangle(mangleContext, name, typeInst->GetUnderlyingType());

	for (int paramIdx = 0; paramIdx < (int)methodInst->GetParamCount(); paramIdx++)	
	{		
		BfType* paramType = methodInst->GetParamType(paramIdx);

		bool isConst = false;
		if ((methodDeclaration != NULL) && (paramIdx < methodDeclaration->mParams.mSize))
		{
			auto paramDecl = methodDeclaration->mParams[paramIdx];
			HandleParamCustomAttributes(paramDecl->mAttributes, false, isConst);
		}

		auto paramKind = methodInst->GetParamKind(paramIdx);
		if (paramKind == BfParamKind_Params)
			name += "U6params";
		else if (paramKind == BfParamKind_DelegateParam)
			name += "U5param";
		Mangle(mangleContext, name, paramType, NULL, isConst);
	}
	if ((methodInst->GetParamCount() == 0) && (!doExplicitThis))
		name += 'v';

	return name;
}

String BfGNUMangler::MangleMethodName(BfTypeInstance* type, const StringImpl& methodName)
{		
	MangleContext mangleContext;
	mangleContext.mModule = type->mModule;
	StringT<256> name = "_Z";	

	auto typeInst = type->ToTypeInstance();
	BF_ASSERT(typeInst != NULL);
	
	bool isNameOpen;
	MangleTypeInst(mangleContext, name, typeInst, NULL, &isNameOpen);	
	AddSizedString(name, methodName);	
	if (isNameOpen)
		name += "E";
	name += "v";	
	return name;
}

String BfGNUMangler::MangleStaticFieldName(BfTypeInstance* type, const StringImpl& fieldName)
{
	MangleContext mangleContext;
	mangleContext.mModule = type->mModule;

	auto typeInst = type->ToTypeInstance();
	auto typeDef = typeInst->mTypeDef;
	if ((typeDef->IsGlobalsContainer()) && (typeDef->mNamespace.IsEmpty()))
		return fieldName;
	
	StringT<256> name = "_Z";	
	bool isNameOpen;
	MangleTypeInst(mangleContext, name, typeInst, NULL, &isNameOpen);
	AddSizedString(name, fieldName);	
	if (isNameOpen)
		name += "E";
	return name;
}

String BfGNUMangler::Mangle(BfFieldInstance* fieldInstance)
{
	StringT<256> name;
	MangleContext mangleContext;
	mangleContext.mModule = fieldInstance->mOwner->mModule;
	
	bool isCMangle = false;
	HandleCustomAttributes(fieldInstance->mCustomAttributes, fieldInstance->mOwner->mConstHolder, mangleContext.mModule, name, isCMangle, mangleContext.mCPPMangle);
	if (isCMangle)
		name += fieldInstance->GetFieldDef()->mName;
	if (!name.IsEmpty())
		return name;

	auto typeInst = fieldInstance->mOwner->ToTypeInstance();
	auto typeDef = typeInst->mTypeDef;
	if ((typeDef->IsGlobalsContainer()) && (typeDef->mNamespace.IsEmpty()))
		return fieldInstance->GetFieldDef()->mName;

	name += "_Z";
	bool isNameOpen;
	MangleTypeInst(mangleContext, name, typeInst, NULL, &isNameOpen);
	AddSizedString(name, fieldInstance->GetFieldDef()->mName);
	if (isNameOpen)
		name += "E";
	return name;
}

//////////////////////////////////////////////////////////////////////////

BfModule* BfMSMangler::MangleContext::GetUnreifiedModule()
{
	if (mModule == NULL)
		return NULL;
	return mModule->mContext->mUnreifiedModule;
}

//////////////////////////////////////////////////////////////////////////

void BfMSMangler::AddGenericArgs(MangleContext& mangleContext, StringImpl& name, const SizedArrayImpl<BfType*>& genericArgs, int numOuterGenericParams)
{
	if (numOuterGenericParams == (int)genericArgs.size())
		return;

	auto prevSubList = mangleContext.mSubstituteList;
	auto prevSubTypeList = mangleContext.mSubstituteTypeList;
	mangleContext.mSubstituteList.clear();
		
	for (int genericIdx = numOuterGenericParams; genericIdx < (int)genericArgs.size(); genericIdx++)
		Mangle(mangleContext, name, genericArgs[genericIdx]);	
	
	mangleContext.mSubstituteList = prevSubList;
	mangleContext.mSubstituteTypeList = prevSubTypeList;	
}

void BfMSMangler::AddStr(MangleContext& mangleContext, StringImpl& name, const StringImpl& str)
{
	if ((int)mangleContext.mSubstituteList.size() < 10)
	{
		// Add NULL entry... shouldn't match anything
		mangleContext.mSubstituteList.push_back(NameSubstitute(NameSubstitute::Kind_None, NULL));
	}

	name += str;
	name += '@';
}

void BfMSMangler::AddSubIdx(StringImpl& name, int strIdx)
{
	//TODO: BF_ASSERT(strIdx < 10);
	char str[16];
	itoa(strIdx, str, 10);
	name += str;
}

void BfMSMangler::AddTypeStart(MangleContext& mangleContext, StringImpl& name, BfType* type)
{
	if (mangleContext.mIsSafeMangle)
	{
		name += "V"; // Mangle everything as a struct so we don't need to require the type to be populated
		return;
	}

	if (!type->IsDeclared())
	{
		if (mangleContext.mModule != NULL)
		{
			auto unreifiedModule = mangleContext.GetUnreifiedModule();
			if (unreifiedModule != NULL)
				unreifiedModule->PopulateType(type, BfPopulateType_Declaration);
		}
		else
		{
			BF_FATAL("No module");
		}
	}

	if ((type->IsEnum()) && (type->IsTypedPrimitive()))
	{
		auto unreifiedModule = mangleContext.GetUnreifiedModule();
		if (unreifiedModule != NULL)
			unreifiedModule->PopulateType(type, BfPopulateType_Data);

		BF_ASSERT(type->mSize >= 0);

		// The enum size is supposed to be encoded, but VC always uses '4'
		//name += "W";		
		//name += ('0' + type->mSize);
		name += "W4";
		return;
	}

	name += type->IsStruct() ? "U" : "V";
}

bool BfMSMangler::FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub)
{
	for (int curMatchIdx = 0; curMatchIdx < (int)mangleContext.mSubstituteList.size(); curMatchIdx++)
	{
		auto& entry = mangleContext.mSubstituteList[curMatchIdx];
		if ((entry.mKind == newNameSub.mKind) && (entry.mParam == newNameSub.mParam) && (entry.mExtendsTypeId == newNameSub.mExtendsTypeId))
		{
			AddSubIdx(name, curMatchIdx);
			return true;
		}
	}	

	if (newNameSub.mKind == NameSubstitute::Kind_NamespaceAtom)
	{
		name += newNameSub.mAtom->mString;
		name += '@';
	}
	else if (newNameSub.mKind == NameSubstitute::Kind_TypeInstName)
	{
		if (mangleContext.mWantsGroupStart)
		{			
			AddTypeStart(mangleContext, name, newNameSub.mTypeInst);
			mangleContext.mWantsGroupStart = false;
		}

		if (newNameSub.mTypeInst->IsTuple())
		{
			auto tupleType = (BfTypeInstance*)newNameSub.mType;
			name += "?$__TUPLE";
			SizedArray<BfType*, 8> typeVec;
			for (auto& fieldInst : tupleType->mFieldInstances)
			{
				BfFieldDef* fieldDef = fieldInst.GetFieldDef();
				String fieldName = fieldDef->mName;
				if ((fieldName[0] < '0') || (fieldName[0] > '9'))
					name += StrFormat("_%s", fieldName.c_str());
				typeVec.push_back(fieldInst.mResolvedType);
			}
			name += '@';
			if (!typeVec.empty())
				AddGenericArgs(mangleContext, name, typeVec);
			name += '@';
		}
		else if ((newNameSub.mTypeInst->IsDelegateFromTypeRef()) || (newNameSub.mTypeInst->IsFunctionFromTypeRef()))
		{
			BF_ASSERT(newNameSub.mTypeInst->mTypeDef->mMethods[0]->mName == "Invoke");
			
			auto delegateInfo = newNameSub.mTypeInst->GetDelegateInfo();
			
			auto methodDef = newNameSub.mTypeInst->mTypeDef->mMethods[0];
			if (newNameSub.mTypeInst->IsDelegate())
				name += "?$delegate";
			else
				name += "?$function";
			if (methodDef->mIsMutating)
				name += "_mut_";
			SizedArray<BfType*, 8> typeVec;
			typeVec.push_back(BfNodeDynCast<BfDirectTypeReference>(methodDef->mReturnTypeRef)->mType);
			
			for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)
			{								
				name += "_";
				name += methodDef->mParams[paramIdx]->mName;
				if (methodDef->mParams[paramIdx]->mParamKind == BfParamKind_VarArgs)
				{
					name += "__varargs";
					continue;
				}
				typeVec.push_back(BfNodeDynCast<BfDirectTypeReference>(methodDef->mParams[paramIdx]->mTypeRef)->mType);
			}
			name += '@';
			if (!typeVec.empty())
				AddGenericArgs(mangleContext, name, typeVec);
			name += '@';			
		}
		else if (newNameSub.mTypeInst->IsBoxed())
		{
			auto boxedType = (BfBoxedType*)newNameSub.mTypeInst;
			name += "?$Box@";
			SizedArray<BfType*, 8> typeVec;
			typeVec.push_back(boxedType->GetModifiedElementType());			
			AddGenericArgs(mangleContext, name, typeVec);
			name += '@';
		}
		else
		{
			auto typeDef = newNameSub.mTypeInst->mTypeDef;

			BfTypeInstance* genericTypeInst = NULL;
			if (newNameSub.mTypeInst->IsGenericTypeInstance())		
				genericTypeInst = (BfTypeInstance*)newNameSub.mTypeInst;			

			int numOuterGenericParams = 0;
			if ((!mangleContext.mIsSafeMangle) && (typeDef->mOuterType != NULL))
			{
				numOuterGenericParams = (int)typeDef->mOuterType->mGenericParamDefs.size();
			}

			if (genericTypeInst != NULL)
			{
				// If we don't have our own generic params then don't treat us as a generic
				if ((int)typeDef->mGenericParamDefs.size() == numOuterGenericParams)
					genericTypeInst = NULL;
			}

			if (genericTypeInst != NULL)
			{
				name += "?$";
				mangleContext.mWantsGroupStart = false;				
			}
			
// 			name += *typeDef->mName->mString;			
// 			if ((typeDef->mIsDelegate) && (newNameSub.mTypeInst->IsClosure()))
// 			{
// 				auto closureType = (BfClosureType*)newNameSub.mTypeInst;
// 				if (!closureType->mCreatedTypeDef)
// 					name += closureType->mNameAdd;
// 			}		

			if (newNameSub.mTypeInst->IsClosure())
			{
				auto closureType = (BfClosureType*)newNameSub.mTypeInst;
				name += closureType->mSrcDelegate->mTypeDef->mName->mString;
				name += closureType->mNameAdd;
			}
			else
			{
				name += typeDef->mName->mString;
			}

			name += '@';

			if (genericTypeInst != NULL)
			{
				AddGenericArgs(mangleContext, name, genericTypeInst->mGenericTypeInfo->mTypeGenericArguments, numOuterGenericParams);
				name += '@';
			}
		}
	}		
	else if (newNameSub.mKind == BfGNUMangler::NameSubstitute::Kind_GenericParam)
	{
		name += "U"; // Struct
		auto genericParamType = (BfGenericParamType*)newNameSub.mType;				
		if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
			name += "_M";
		else
			name += "_T";
		char str[16];
		itoa(genericParamType->mGenericParamIdx, str, 10);
		name += str;
		name += '@';
		name += '@';
	}
	
	mangleContext.mSubstituteList.push_back(newNameSub);

	return false;
}

void BfMSMangler::Mangle(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInstance, bool isAlreadyStarted, bool isOuterType)
{		
	BfTypeInstance* genericTypeInst = NULL;
	if (typeInstance->IsGenericTypeInstance())
	{
		genericTypeInst = (BfTypeInstance*)typeInstance;
	}

	auto typeDef = typeInstance->mTypeDef;
	bool hasNamespace = typeDef->mNamespace.mSize != 0;
	if (!isAlreadyStarted)
	{
		if ((hasNamespace) || (typeDef->mOuterType != NULL))
		{
			AddTypeStart(mangleContext, name, typeInstance);			
		}
		else
		{
			if (typeDef->IsGlobalsContainer())
				return;
			mangleContext.mWantsGroupStart = true;
		}
	}

	if (!typeDef->IsGlobalsContainer())
		FindOrCreateNameSub(mangleContext, name, NameSubstitute(BfMangler::NameSubstitute::Kind_TypeInstName, typeInstance));
	mangleContext.mWantsGroupStart = false;	

	auto useModule = typeInstance->mModule;
	if (useModule == NULL)
		useModule = mangleContext.mModule;
	
	if ((typeDef->mOuterType != NULL) && (!typeInstance->IsBoxed()))
	{	
		if (mangleContext.mIsSafeMangle)
		{
			auto outerType = typeDef->mOuterType;
			while (outerType != NULL)
			{
				name += ":";
				name += outerType->mFullName.ToString();
				outerType = outerType->mOuterType;
			}
		}
		else
		{
			auto unreifiedModule = useModule->mContext->mUnreifiedModule;
			auto outerType = unreifiedModule->GetOuterType(typeInstance);
			if (outerType != NULL)
				Mangle(mangleContext, name, outerType, true, true);
			else	
				useModule->Fail("Failed to mangle name in BfMSMangler::Mangle");
		}
	}

	if (!isOuterType)
	{
		if (!typeInstance->IsBoxed())
		{
			for (int namespaceIdx = (int)typeDef->mNamespace.mSize - 1; namespaceIdx >= 0; namespaceIdx--)
			{
				auto namePart = typeDef->mNamespace.mParts[namespaceIdx];
				FindOrCreateNameSub(mangleContext, name, NameSubstitute(BfMangler::NameSubstitute::Kind_NamespaceAtom, namePart));
			}

			if (!mangleContext.mCPPMangle)
				FindOrCreateNameSub(mangleContext, name, NameSubstitute(BfMangler::NameSubstitute::Kind_NamespaceAtom, useModule->mSystem->mBfAtom));
		}

		name += '@';
	}
}

void BfMSMangler::MangleConst(MangleContext & mangleContext, StringImpl & name, int64 val)
{
	name += "$0";
	if (val < 0)
	{
		name += "?";
		val = -val;
	}
	if ((val > 0) && (val <= 10))
	{
		name += (char)('0' + val - 1);
	}
	else
	{
		char str[64];
		char* strP = str + 63;
		*strP = 0;

		while (val > 0)
		{
			*(--strP) = (char)((val % 0x10) + 'A');
			val /= 0x10;
		}

		name += strP;
		name += '@';
	}
}

void BfMSMangler::AddPrefix(MangleContext& mangleContext, StringImpl& name, int startIdx, const char* prefix)
{
	int subIdx;
	char startChar = name[startIdx];
	if ((startChar >= '0') && (startChar <= '9'))
	{
		//subIdx = ParseSubIdx(name, startIdx);
		subIdx = startChar - '0';
		for (int matchIdx = subIdx + 1; matchIdx < (int)mangleContext.mSubstituteList.size(); matchIdx++)
		{
			auto& entry = mangleContext.mSubstituteList[matchIdx];
			if ((entry.mKind == NameSubstitute::Kind_Prefix) && (entry.mExtendsIdx == subIdx) && (entry.mPrefix == prefix))
			{
				BF_ASSERT(name.EndsWith('_'));
				name.RemoveToEnd(startIdx);
				AddSubIdx(name, matchIdx);
				return;
			}
		}
	}
	else
	{
		//auto typeCode = GetPrimTypeAt(mangleContext, name, startIdx);
		auto typeCode = BfTypeCode_None;
		if (typeCode != (BfTypeCode)-1)
		{
			for (int matchIdx = 0; matchIdx < (int)mangleContext.mSubstituteList.size(); matchIdx++)
			{
				auto& entry = mangleContext.mSubstituteList[matchIdx];
				if ((entry.mKind == NameSubstitute::Kind_PrimitivePrefix) && (entry.mExtendsTypeCode == typeCode) && (entry.mPrefix == prefix))
				{
					name.RemoveToEnd(startIdx);
					AddSubIdx(name, matchIdx);
					return;
				}
			}

			NameSubstitute nameSub;
			nameSub.mKind = NameSubstitute::Kind_PrimitivePrefix;
			nameSub.mExtendsTypeCode = typeCode;
			nameSub.mPrefix = prefix;
			mangleContext.mSubstituteList.push_back(nameSub);

			name.Insert(startIdx, prefix);
			return;
		}
		else
		{
			// Applies to last-added one		
			subIdx = (int)mangleContext.mSubstituteList.size() - 1;
			BF_ASSERT(isdigit(startChar) || (startChar == 'N') || (startChar == 'P') || (startChar == 'R'));
		}
	}

	NameSubstitute nameSub;
	nameSub.mKind = NameSubstitute::Kind_Prefix;
	nameSub.mExtendsIdx = subIdx;
	nameSub.mPrefix = prefix;
	mangleContext.mSubstituteList.push_back(nameSub);

	name.Insert(startIdx, prefix);
}

void BfMSMangler::Mangle(MangleContext& mangleContext, StringImpl& name, BfType* type, bool useTypeList, bool isConst)
{	
	bool isLongPrim = false;

	if (type->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)type;

		switch (primType->mTypeDef->mTypeCode)
		{
		case BfTypeCode_NullPtr:
			{
				name += "P";
				if (mangleContext.mIs64Bit)
					name += "E";
				name += "AV";
				name += "v";
			}
			return;
		case BfTypeCode_None:
			name += "X"; return;		
		case BfTypeCode_Int8:
			name += "C"; return;
		case BfTypeCode_UInt8:
			name += "E"; return;
		case BfTypeCode_Int16:
			name += "F"; return;
		case BfTypeCode_UInt16:
			name += "G"; return;
		case BfTypeCode_Int32:
			name += "H"; return;
		case BfTypeCode_UInt32:
			name += "I"; return;							
		case BfTypeCode_Char8:
			name += "D"; return;
		case BfTypeCode_Char16:
			name += "_S"; return; //char16_t
		case BfTypeCode_Float:
			name += "M"; return;
		case BfTypeCode_Double:
			name += "N"; return;
		case BfTypeCode_Int64:
		case BfTypeCode_UInt64:
		case BfTypeCode_Boolean:		
		case BfTypeCode_Char32:					
			isLongPrim = true;
			break;		
		case BfTypeCode_IntPtr:
			if ((primType->mSize == 4) && (mangleContext.mCCompat))
			{
				name += "H";
				return;
			}
			isLongPrim = true;
			break;
		case BfTypeCode_UIntPtr:
			if ((primType->mSize == 4) && (mangleContext.mCCompat))
			{
				name += "I";
				return;
			}
			isLongPrim = true;
			break;

		case BfTypeCode_Dot:
			name += "Tdot@@"; return;
		case BfTypeCode_Var:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
				name += "X";
			else
				name += "Tvar@@";
			return;
		case BfTypeCode_Self:
			if ((mangleContext.mCCompat) || (mangleContext.mInArgs))
				name += "X";
			else
				name += "Tself@@";
			return;
		case BfTypeCode_Let:
			name += "Tlet@@"; return;
		case BfTypeCode_IntUnknown:
			name += "Tiunk@@"; return;
		case BfTypeCode_UIntUnknown:
			name += "Tuunk@@"; return;

		default:
			name += "?"; return;
		}		
	}	
		
	if (useTypeList)
	{
		for (int checkIdx = 0; checkIdx < (int)mangleContext.mSubstituteTypeList.size(); checkIdx++)
		{
			if (mangleContext.mSubstituteTypeList[checkIdx] == type)
			{
				name += ('0' + checkIdx);
				return;
			}
		}
	}

	if (isLongPrim)
	{
		auto primType = (BfPrimitiveType*)type;
		switch (primType->mTypeDef->mTypeCode)
		{
		case BfTypeCode_Boolean:
			name += "_N"; break;
		case BfTypeCode_Int64:
			name += "_J"; break;
		case BfTypeCode_UInt64:
			name += "_K"; break;
		case BfTypeCode_UIntPtr:
			if (primType->mSize == 4)
				name += "_I";
 			else if (mangleContext.mCCompat)
				name += "_K";
 			else
 				name += "Tuint@@";
			break;
		case BfTypeCode_IntPtr:
			if (primType->mSize == 4)
				name += "_H";
			else if (mangleContext.mCCompat)
				name += "_J";
			else
				name += "Tint@@";			
			break;
		case BfTypeCode_Char32:
			name += "_U"; break;
		default: break;
		}
	}
	else if (type->IsTypeInstance())
	{
		auto typeInstance = (BfTypeInstance*)type;
		auto typeDef = typeInstance->mTypeDef;

		if (type->IsObjectOrInterface())
		{
			name += "P";
			if (mangleContext.mIs64Bit)
				name += "E";
			name += "A";
		}
		else if (((type->IsGenericTypeInstance()) || (type->IsComposite()) || (type->IsEnum())) && (mangleContext.mInRet))
			name += "?A";

		Mangle(mangleContext, name, typeInstance, false);		
	}
	else if (type->IsGenericParam())
	{
		FindOrCreateNameSub(mangleContext, name, NameSubstitute(BfMangler::NameSubstitute::Kind_GenericParam, type));
	}
	else if (type->IsPointer())
	{
		auto pointerType = (BfPointerType*)type;

		const char* strAdd = mangleContext.mIs64Bit ? "PEA" : "PA";

		if (mangleContext.mIs64Bit)
			name += "PE";
		else
			name += "P";		
		if (isConst)
			name += "B";
		else
			name += "A";
		Mangle(mangleContext, name, pointerType->mElementType);		
	}
	else if (type->IsRef())
	{
		auto refType = (BfRefType*)type;
		name += "A";
		if (mangleContext.mIs64Bit)
			name += "E";
		if ((isConst) || (refType->mRefKind == BfRefType::RefKind_In))
			name += "B";
		else
			name += "A";
		if (refType->mRefKind == BfRefType::RefKind_Mut)
			name += "mut$";		
		else if (refType->mRefKind == BfRefType::RefKind_Out)
			name += "out$";
		Mangle(mangleContext, name, refType->mElementType);
	}	
	else if (type->IsModifiedTypeType())
	{
		auto retType = (BfModifiedTypeType*)type;
		if (retType->mModifiedKind == BfToken_RetType)
			name += "rettype$";
		else if (retType->mModifiedKind == BfToken_AllocType)
			name += "alloc$";
		else if (retType->mModifiedKind == BfToken_Nullable)
			name += "nullable$";
		else
			BF_FATAL("Unhandled");
		Mangle(mangleContext, name, retType->mElementType);
	}
	else if (type->IsConcreteInterfaceType())
	{
		auto concreteType = (BfConcreteInterfaceType*)type;
		name += "concrete$";
		Mangle(mangleContext, name, concreteType->mInterface);
	}
	else if (type->IsSizedArray())
	{
		if (type->IsUnknownSizedArrayType())
		{
			auto arrType = (BfUnknownSizedArrayType*)type;
			name += StrFormat("arr_$", arrType->mSize);
			Mangle(mangleContext, name, arrType->mElementType);
			name += "$";
			Mangle(mangleContext, name, arrType->mElementCountSource);
		}
		else
		{
			// We can't use MS mangling of "_O" because it isn't size-specific 
			auto arrType = (BfSizedArrayType*)type;
			//name += StrFormat("arr_%d$", arrType->mSize);
			//Mangle(mangleContext, name, arrType->mElementType);

			name += "?$_ARRAY@";			
			Mangle(mangleContext, name, arrType->mElementType);
			MangleConst(mangleContext, name, arrType->mElementCount);
			name += '@';
		}
	}
	else if (type->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)type;		
		name += "Tmref_";
		
		StringT<128> mangleName;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;
		if (methodInstance == NULL)
		{
			BF_ASSERT(!methodRefType->mMangledMethodName.IsEmpty());
			mangleName = methodRefType->mMangledMethodName;
		}
		else
		{
			if (methodInstance->mIsAutocompleteMethod)
				name += "AUTOCOMPLETE";

			auto module = methodInstance->GetOwner()->mModule;
			if (module->mCompiler->mIsResolveOnly)
			{
				// There are cases where we will reprocess a method in ResolveOnly for things like
				//  GetSymbolReferences, so we will have duplicate live local methodInstances in those cases
				name += HashEncode64((uint64)methodRefType->mMethodRef);
			}
			else
			{
				Mangle(mangleName, mangleContext.mIs64Bit, methodInstance);
				methodRefType->mMangledMethodName = mangleName;
			}
		}

		if (!mangleName.IsEmpty())
		{
			Val128 val128 = Hash128(mangleName.c_str(), (int)mangleName.length());
			name += HashEncode128(val128);
		}

// 		if (methodInstance->mIsAutocompleteMethod)
// 			name += "AUTOCOMPLETE";		
// 
// 		String mangleAdd = Mangle(mangleContext.mIs64Bit, methodInstance);
// 
// 		auto module = methodInstance->GetOwner()->mModule;
// 		if (module->mCompiler->mIsResolveOnly)
// 		{
// 			// There are cases where we will reprocess a method in ResolveOnly for things like
// 			//  GetSymbolReferences, so we will have duplicate live local methodInstances in those cases
// 			name += HashEncode64((uint64)methodRefType->mMethodRef);
// 		}
// 		else
// 		{
// 			Val128 val128 = Hash128(mangleAdd.c_str(), (int)mangleAdd.length());
// 			name += HashEncode128(val128);
// 		}
		name += "@@";		 		
	}
	else if (type->IsConstExprValue())
	{
		BfConstExprValueType* constExprValueType = (BfConstExprValueType*)type;
		int64 val = constExprValueType->mValue.mInt64;
		if ((!constExprValueType->mType->IsPrimitiveType()) || 
			(((BfPrimitiveType*)constExprValueType->mType)->mTypeDef->mTypeCode != BfTypeCode_IntPtr))
		{
			Mangle(mangleContext, name, constExprValueType->mType);
			name += "$";
		}
		MangleConst(mangleContext, name, val);		
		if (constExprValueType->mValue.mTypeCode == BfTypeCode_Let)
			name += "Undef";
	}
	else
	{
		BF_ASSERT("Unhandled");
	}
	
	if ((useTypeList) && (!mangleContext.mInRet) && ((int)mangleContext.mSubstituteTypeList.size() < 10))
		mangleContext.mSubstituteTypeList.push_back(type);
}

void BfMSMangler::Mangle(StringImpl& name, bool is64Bit, BfType* type, BfModule* module)
{
	if (type->IsTypeAlias())
	{
		auto typeAlias = (BfTypeAliasType*)type;
		name += "__ALIAS_";
		name += typeAlias->mTypeDef->mName->ToString();
		name += '@';
		Mangle(name, is64Bit, typeAlias->mAliasToType, module);
		return;
	}

	MangleContext mangleContext;
	mangleContext.mIs64Bit = is64Bit;
	mangleContext.mModule = module;
	auto typeInst = type->ToTypeInstance();
	if ((typeInst != NULL) && (typeInst->mModule != NULL))
		mangleContext.mModule = typeInst->mModule;	
	if (typeInst != NULL)
		Mangle(mangleContext, name, typeInst, true);
	else
		Mangle(mangleContext, name, type);
	while ((name.length() > 0) && (name[name.length() - 1] == '@'))
		name.Remove(name.length() - 1);
}

void BfMSMangler::Mangle(StringImpl& name, bool is64Bit, BfMethodInstance* methodInst)
{
	static int mangleIdx = 0;
	mangleIdx++;
	
	int startNameLen = name.mLength;
	if ((methodInst->mMethodDef->mCLink) && (!methodInst->mMangleWithIdx))
	{
		name += methodInst->mMethodDef->mName;
		return;
	}

	auto methodDef = methodInst->mMethodDef;
	auto methodDeclaration = BfNodeDynCastExact<BfMethodDeclaration>(methodDef->mMethodDeclaration);
	auto typeInst = methodInst->GetOwner();
	auto typeDef = typeInst->mTypeDef;	
	
	MangleContext mangleContext;
	mangleContext.mIs64Bit = is64Bit;
	mangleContext.mModule = methodInst->GetOwner()->mModule;	
	if (methodInst->mCallingConvention != BfCallingConvention_Unspecified)
		mangleContext.mCCompat = true;
	bool isCMangle = false;	
	HandleCustomAttributes(methodInst->GetCustomAttributes(), typeInst->mConstHolder, mangleContext.mModule, name, isCMangle, mangleContext.mCPPMangle);
	if (isCMangle)
		name += methodInst->mMethodDef->mName;
	if (name.mLength > startNameLen)
		return;
	
	name += '?';

	if (methodInst->GetNumGenericArguments() != 0)
	{
		name += "?$";
	}

	bool isSpecialFunc = false;
	if (methodInst->mMethodDef->mIsOperator)
	{
		String methodName;

		auto operatorDef = (BfOperatorDef*)methodInst->mMethodDef;
		if (operatorDef->mOperatorDeclaration->mIsConvOperator)
		{
			name += "?B";
		}
		else
		{
			switch (operatorDef->mOperatorDeclaration->mBinOp)
			{
			case BfBinaryOp_Add:
				name += "?H";
				break;
			case BfBinaryOp_Subtract:
				name += "?G";
				break;
			case BfBinaryOp_Multiply:
				name += "?D";
				break;
			case BfBinaryOp_OverflowAdd:
				name += "?OH";
				break;
			case BfBinaryOp_OverflowSubtract:
				name += "?OG";
				break;
			case BfBinaryOp_OverflowMultiply:
				name += "?OD";
				break;
			case BfBinaryOp_Divide:
				name += "?K";
				break;
			case BfBinaryOp_Modulus:
				name += "?L";
				break;
			case BfBinaryOp_BitwiseAnd:
				name += "?I";
				break;
			case BfBinaryOp_BitwiseOr:
				name += "?U";
				break;
			case BfBinaryOp_ExclusiveOr:
				name += "?T";
				break;
			case BfBinaryOp_LeftShift:
				name += "?6";
				break;
			case BfBinaryOp_RightShift:
				name += "?5";
				break;
			case BfBinaryOp_Equality:
				name += "?8";
				break;
			case BfBinaryOp_InEquality:
				name += "?9";
				break;
			case BfBinaryOp_GreaterThan:
				name += "?O";
				break;
			case BfBinaryOp_LessThan:
				name += "?M";
				break;
			case BfBinaryOp_GreaterThanOrEqual:
				name += "?P";
				break;
			case BfBinaryOp_LessThanOrEqual:
				name += "?N";
				break;
			case BfBinaryOp_Compare:
				name += "__cmp__";
				break;
			case BfBinaryOp_ConditionalAnd:
				name += "?V";
				break;
			case BfBinaryOp_ConditionalOr:
				name += "?W";
				break;
			case BfBinaryOp_NullCoalesce:
				methodName = "__nc__";
				break;
			case BfBinaryOp_Is:
				methodName = "__is__";
				break;
			case BfBinaryOp_As:
				methodName = "__as__";
				break;
			default: break;
			}

			switch (operatorDef->mOperatorDeclaration->mUnaryOp)
			{
			case BfUnaryOp_AddressOf:
				name += "?I";
				break;
			case BfUnaryOp_Dereference:
				name += "?D";
				break;
			case BfUnaryOp_Negate:
				name += "?G";
				break;
			case BfUnaryOp_Not:
				name += "?7";
				break;
			case BfUnaryOp_Positive:
				name += "?H";
				break;
			case BfUnaryOp_InvertBits:
				name += "?S";
				break;
			case BfUnaryOp_Increment:
				name += "?E";
				break;
			case BfUnaryOp_Decrement:
				name += "?F";
				break;
			case BfUnaryOp_PostIncrement:
				methodName = "__pi__";
				break;
			case BfUnaryOp_PostDecrement:
				methodName = "__pd__";
				break;
			case BfUnaryOp_Ref:
				methodName = "__ref__";
				break;
			case BfUnaryOp_Out:
				methodName = "__out__";
				break;
			default: break;
			}

			switch (operatorDef->mOperatorDeclaration->mAssignOp)
			{			
			case BfAssignmentOp_Assign:
				methodName += "__a__";
				break;
			case BfAssignmentOp_Add:
				methodName += "__a_add__";
				break;
			case BfAssignmentOp_Subtract:
				methodName += "__a_sub__";
				break;
			case BfAssignmentOp_Multiply:
				methodName += "__a_mul__";
				break;
			case BfAssignmentOp_Divide:
				methodName += "__a_div__";
				break;
			case BfAssignmentOp_Modulus:
				methodName += "__a_mod__";
				break;
			case BfAssignmentOp_ShiftLeft:
				methodName += "__a_shl__";
				break;
			case BfAssignmentOp_ShiftRight:
				methodName += "__a_shr__";
				break;
			case BfAssignmentOp_BitwiseAnd:
				methodName += "__a_bwa__";
				break;
			case BfAssignmentOp_BitwiseOr:
				methodName += "__a_bwo__";
				break;
			case BfAssignmentOp_ExclusiveOr:
				methodName += "__a_xor__";
				break;
			default: break;
			}
		}

		if (!methodName.empty())
		{
			AddStr(mangleContext, name, methodName);
		}		
	}
	/*else if ((methodDef->mMethodType == BfMethodType_Ctor) && (!methodDef->mIsStatic))
	{
		isSpecialFunc = true;
		name += "?0";
		//AddAtomStr(mangleContext, name, typeDef->mName);
	}
	else if ((methodDef->mMethodType == BfMethodType_Dtor) && (!methodDef->mIsStatic))
	{
		isSpecialFunc = true;
		name += "?1";
		//AddAtomStr(mangleContext, name, typeDef->mName);
	}*/
	else if (methodInst->GetNumGenericArguments() != 0)
	{
		AddStr(mangleContext, name, methodDef->mName);
		AddGenericArgs(mangleContext, name, methodInst->mMethodInfoEx->mMethodGenericArguments);	
		name += '@';
	}
	else
	{
		if ((!mangleContext.mCPPMangle) && (!methodDef->mIsMutating) && (!methodDef->mIsStatic) && (methodInst->GetOwner()->IsValueType()))
			AddStr(mangleContext, name, methodDef->mName + "__im");
		else
			AddStr(mangleContext, name, methodDef->mName);		
	}

	if (((methodInst->GetOwner()->mTypeDef->IsGlobalsContainer()) && 
		 ((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_Dtor) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))) ||
		((methodInst->mMethodDef->mDeclaringType->mPartialIdx != -1) && (methodInst->mMethodDef->mDeclaringType->IsExtension()) && 
		 (!methodInst->mIsForeignMethodDef) && (!methodInst->mMethodDef->mIsExtern) && 
		 ((!methodInst->mMethodDef->mIsOverride) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mMethodType == BfMethodType_Dtor))))
	{
		auto declType = methodInst->mMethodDef->mDeclaringType;
		BF_ASSERT(methodInst->GetOwner()->mTypeDef->mIsCombinedPartial);
		auto declProject = declType->mProject;
		bool addProjectName = (declProject != typeInst->mTypeDef->mProject);
		bool addIndex = true;
		
		if (typeInst->mTypeDef->IsGlobalsContainer())
		{	
			addProjectName = true;

			if ((methodInst->mCallingConvention == BfCallingConvention_Cdecl) ||
				(methodInst->mCallingConvention == BfCallingConvention_Stdcall))
			{
				addProjectName = false;
				addIndex = false;
			}			
		}
		
		if (addProjectName)
		{
			name += declProject->mName;
			name += '$';
		}
		if (addIndex)
			name += StrFormat("%d$", declType->mPartialIdx);
	}

	if (!mangleContext.mCPPMangle)
	{
		if (methodInst->mMangleWithIdx)
			name += StrFormat("i%d$", methodInst->mMethodDef->mIdx);
		if (methodDef->mCheckedKind == BfCheckedKind_Checked)
			name += "CHK$";
		else if (methodDef->mCheckedKind == BfCheckedKind_Unchecked)
			name += "UCHK$";	

		if (methodDef->mHasComptime)
			name += "COMPTIME$";
	}

	/*if ((methodInst->mMethodInstanceGroup->mOwner->mTypeDef->IsGlobalsContainer()) && (methodInst->mMethodDef->mMethodDeclaration == NULL))
	{
		name += methodInst->mMethodInstanceGroup->mOwner->mTypeDef->mProject->mName;
		name += "$";
	}*/

	if (methodInst->GetForeignType() != NULL)
		Mangle(mangleContext, name, methodInst->GetForeignType(), true);
	if (methodInst->GetExplicitInterface() != NULL)
		Mangle(mangleContext, name, methodInst->GetExplicitInterface(), true);
	
	///
	{
		// Only use CCompat for params, not for the owning type name - unless we're doing an explicit CPP mangle
		SetAndRestoreValue<bool> prevCCompat(mangleContext.mCCompat, (mangleContext.mCCompat && mangleContext.mCPPMangle) ? true : false);
		Mangle(mangleContext, name, methodInst->GetOwner(), true);
	}

	/*bool isCppDecl = false;
	if (!isCppDecl)
	{
		if (name.EndsWith("@"))
			name.Remove(name.length() - 1);
		FindOrCreateNameSub(mangleContext, name, NameSubstitute(BfMangler::NameSubstitute::Kind_NamespaceAtom, typeInst->mModule->mSystem->mBfAtom));
		name += '@';
	}*/

	//QEA AXXZ

	char attrib ='A';
	if (methodDef->mProtection == BfProtection_Protected)
		attrib = 'I';
	else if (methodDef->mProtection == BfProtection_Public)
		attrib = 'Q';

	bool doExplicitThis = (!methodDef->mIsStatic) && (typeInst->IsTypedPrimitive());

	if ((methodDef->mIsStatic) || (doExplicitThis))
		attrib += 2;

	if ((methodDef->mIsVirtual) && (!methodDef->mIsOverride))
		attrib += 4;
	
	name += attrib;

	auto bfSystem = methodInst->GetOwner()->mModule->mSystem;
	if ((!methodDef->mIsStatic) && (!doExplicitThis))
	{ 		
		/*char cvQualifier = 'A';
		if (mangleContext.mIs64Bit)		
			cvQualifier = 'E';
		name += cvQualifier;*/
		if (mangleContext.mIs64Bit)
			name += "E";

		char qualifier = 'A'; // const / volatile
		name += qualifier;
	}	

	auto callingConvention = mangleContext.mModule->GetIRCallingConvention(methodInst);

	char callingConv = 'A';
	if (callingConvention == BfIRCallingConv_StdCall)
		callingConv = 'G';
	else if (callingConvention == BfIRCallingConv_ThisCall)
		callingConv = 'E';
	name += callingConv;

	if (isSpecialFunc)
		name += '@';

	//
	if (!isSpecialFunc)
	{
		bool isConst = false;
		if (methodDeclaration != NULL)
			HandleParamCustomAttributes(methodDeclaration->mAttributes, true, isConst);

		mangleContext.mInRet = true;
		Mangle(mangleContext, name, methodInst->mReturnType, false, isConst);
		mangleContext.mInRet = false;
	}
	if ((methodInst->mParams.size() == 0) && (!doExplicitThis))
	{
		name += 'X';
	}
	else
	{
		// Is this right?
		if (methodInst->GetNumGenericArguments() != 0)
			mangleContext.mSubstituteList.clear();

		mangleContext.mInArgs = true;
		if (doExplicitThis)
		{			
			Mangle(mangleContext, name, typeInst->GetUnderlyingType(), true);
		}
		for (auto& param : methodInst->mParams)
		{
			bool isConst = false;
			if ((param.mParamDefIdx >= 0) && (methodDeclaration != NULL) && (param.mParamDefIdx < methodDeclaration->mParams.mSize))
			{
				auto paramDecl = methodDeclaration->mParams[param.mParamDefIdx];						
				HandleParamCustomAttributes(paramDecl->mAttributes, false, isConst);
			}

			Mangle(mangleContext, name, param.mResolvedType, true, isConst);
		}
		name += '@';
	}

	name += 'Z';
	
	bool wantLog = false;
	if (wantLog)
	{
		BfLog2("Mangling #%d : %s -> %s\n", mangleIdx, mangleContext.mModule->MethodToString(methodInst).c_str(), name.c_str());

		if (!methodInst->mIsUnspecialized)
		{
			String demangled = BfDemangler::Demangle(name, DbgLanguage_Beef);
			BfLog2(" Demangled %d: %s\n", mangleIdx, demangled.c_str());
		}
	}	
	
}

void BfMSMangler::Mangle(StringImpl& name, bool is64Bit, BfFieldInstance* fieldInstance)
{
	auto fieldDef = fieldInstance->GetFieldDef();
	if (fieldDef->mFieldDeclaration != NULL)
	{
		auto module = fieldInstance->mOwner->mModule;
		if ((fieldInstance->mCustomAttributes != NULL) && (fieldInstance->mCustomAttributes->Contains(module->mCompiler->mCLinkAttributeTypeDef)))
		{
			name += fieldDef->mName;
			return;
		}
	}

	BF_ASSERT(fieldDef->mIsStatic);
	MangleContext mangleContext;
	mangleContext.mIs64Bit = is64Bit;
	mangleContext.mModule = fieldInstance->mOwner->mModule;	

	bool isCMangle = false;	
	HandleCustomAttributes(fieldInstance->mCustomAttributes, fieldInstance->mOwner->mConstHolder, mangleContext.mModule, name, isCMangle, mangleContext.mCPPMangle);
	if (isCMangle)
		name += fieldInstance->GetFieldDef()->mName;
	if (!name.IsEmpty())
		return;
	
	name += '?';
	AddStr(mangleContext, name, fieldDef->mName);	
	Mangle(mangleContext, name, fieldInstance->mOwner, true);
	name += '2'; //TODO: Don't always mark as 'public'
	Mangle(mangleContext, name, fieldInstance->mResolvedType);
	name += ('A' + /*(fieldDef->mIsConst ? 1 : 0) +*/ (fieldDef->mIsVolatile ? 2 : 0));	
}

void BfMSMangler::MangleMethodName(StringImpl& name, bool is64Bit, BfTypeInstance* type, const StringImpl& methodName)
{
	MangleContext mangleContext;
	mangleContext.mIs64Bit = is64Bit;
	mangleContext.mModule = type->GetModule();	
	name += '?';
	AddStr(mangleContext, name, methodName);	
	Mangle(mangleContext, name, type, true);	
	name += "SAXXZ";	
}

void BfMSMangler::MangleStaticFieldName(StringImpl& name, bool is64Bit, BfTypeInstance* owner, const StringImpl& fieldName, BfType* fieldType)
{	
	MangleContext mangleContext;
	mangleContext.mIs64Bit = is64Bit;
	mangleContext.mModule = owner->GetModule();	
	name += '?';
	AddStr(mangleContext, name, fieldName);	
	Mangle(mangleContext, name, owner, true);
	//name += "@@";
	name += '2'; // public
	if (fieldType == NULL)
		name += 'H';
	else
		Mangle(mangleContext, name, fieldType);
	name += "A"; // static		
}

//////////////////////////////////////////////////////////////////////////

String BfSafeMangler::Mangle(BfType* type, BfModule* module)
{
	MangleContext mangleContext;
	mangleContext.mIs64Bit = true;
	mangleContext.mModule = module;
	mangleContext.mIsSafeMangle = true;
	auto typeInst = type->ToTypeInstance();	
	String name;
	if (typeInst != NULL)
		BfMSMangler::Mangle(mangleContext, name, typeInst, true);
	else
		BfMSMangler::Mangle(mangleContext, name, type);
	return name;
}

//////////////////////////////////////////////////////////////////////////

void BfMangler::Mangle(StringImpl& outStr, MangleKind mangleKind, BfType* type, BfModule* module)
{
	if (mangleKind == BfMangler::MangleKind_GNU)
		outStr += BfGNUMangler::Mangle(type, module);
	else
		BfMSMangler::Mangle(outStr, mangleKind == BfMangler::MangleKind_Microsoft_64, type, module);
}

void BfMangler::Mangle(StringImpl& outStr, MangleKind mangleKind, BfMethodInstance* methodInst)
{
	if (mangleKind == BfMangler::MangleKind_GNU)
		outStr += BfGNUMangler::Mangle(methodInst);
	else
		BfMSMangler::Mangle(outStr, mangleKind == BfMangler::MangleKind_Microsoft_64, methodInst);
}

void BfMangler::Mangle(StringImpl& outStr, MangleKind mangleKind, BfFieldInstance* fieldInstance)
{		
	if (mangleKind == BfMangler::MangleKind_GNU)
	{		
		outStr += BfGNUMangler::Mangle(fieldInstance);
	}
	else
		BfMSMangler::Mangle(outStr, mangleKind == BfMangler::MangleKind_Microsoft_64, fieldInstance);
}

void BfMangler::MangleMethodName(StringImpl& outStr, MangleKind mangleKind, BfTypeInstance* type, const StringImpl& methodName)
{
	if (mangleKind == BfMangler::MangleKind_GNU)
		outStr += BfGNUMangler::MangleMethodName(type, methodName);
	else
		BfMSMangler::MangleMethodName(outStr, mangleKind == BfMangler::MangleKind_Microsoft_64, type, methodName);
}

void BfMangler::MangleStaticFieldName(StringImpl& outStr, MangleKind mangleKind, BfTypeInstance* type, const StringImpl& fieldName, BfType* fieldType)
{
	if (mangleKind == BfMangler::MangleKind_GNU)
		outStr += BfGNUMangler::MangleStaticFieldName(type, fieldName);
	else
		BfMSMangler::MangleStaticFieldName(outStr, mangleKind == BfMangler::MangleKind_Microsoft_64, type, fieldName, fieldType);
}

void BfMangler::HandleCustomAttributes(BfCustomAttributes* customAttributes, BfIRConstHolder* constHolder, BfModule* module, StringImpl& name, bool& isCMangle, bool& isCPPMangle)
{
	if (customAttributes == NULL)
		return;
	
	auto linkNameAttr = customAttributes->Get(module->mCompiler->mLinkNameAttributeTypeDef);
	if (linkNameAttr != NULL)
	{
		if (linkNameAttr->mCtorArgs.size() == 1)
		{
			if (module->TryGetConstString(constHolder, linkNameAttr->mCtorArgs[0], name))
				if (!name.IsWhitespace())
					return;

			auto constant = constHolder->GetConstant(linkNameAttr->mCtorArgs[0]);
			if (constant != NULL)
			{
				if (constant->mInt32 == 1) // C
				{					
					isCMangle = true;
				}
				else if (constant->mInt32 == 2) // CPP
				{
					isCPPMangle = true;
				}
			}
		}
	}	
}

void BfMangler::HandleParamCustomAttributes(BfAttributeDirective* attributes, bool isReturn, bool& isConst)
{
	while (attributes != NULL)
	{
		if (attributes->mAttributeTypeRef != NULL)
		{
			auto typeRefName = attributes->mAttributeTypeRef->ToCleanAttributeString();
			if (typeRefName == "MangleConst")
				isConst = true;
		}

		attributes = attributes->mNextAttribute;
	}
}
