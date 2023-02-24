#include "BfAutoComplete.h"
#include "BfParser.h"
#include "BfExprEvaluator.h"
#include "BfSourceClassifier.h"
#include "BfResolvePass.h"
#include "BfFixits.h"
#include "BfResolvedTypeUtils.h"
#include "CeMachine.h"
#include "CeDebugger.h"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "../third_party/FtsFuzzyMatch.h"
#include "BeefySysLib/util/UTF8.h"

#pragma warning(disable:4996)

using namespace llvm;

USING_NS_BF;

AutoCompleteBase::AutoCompleteBase()
{
	mIsGetDefinition = false;
	mIsAutoComplete = true;
	mDoFuzzyAutoComplete = false;
	mInsertStartIdx = -1;
	mInsertEndIdx = -1;
}

AutoCompleteBase::~AutoCompleteBase()
{
	Clear();
}

AutoCompleteEntry* AutoCompleteBase::AddEntry(const AutoCompleteEntry& entry, const StringImpl& filter)
{
	uint8 matches[256];

	if (!DoesFilterMatch(entry.mDisplay, filter.c_str(), entry.mScore, matches, 256) || (entry.mNamePrefixCount < 0))
		return NULL;

	entry.mMatchesLength = mDoFuzzyAutoComplete ? u8_strlen((char*)filter.c_str()) : 0;
	entry.mMatches = (entry.mMatchesLength > 0) ? matches : nullptr;

	auto result = AddEntry(entry);

	// Reset matches because the array will be invalid after return
	entry.mMatches = nullptr;
	entry.mMatchesLength = 0;

	return result;
}

AutoCompleteEntry* AutoCompleteBase::AddEntry(const AutoCompleteEntry& entry, const char* filter)
{
	uint8 matches[256];

	if (!DoesFilterMatch(entry.mDisplay, filter, entry.mScore, matches, 256) || (entry.mNamePrefixCount < 0))
		return NULL;

	entry.mMatchesLength = mDoFuzzyAutoComplete ? u8_strlen((char*)filter) : 0;
	entry.mMatches = (entry.mMatchesLength > 0) ? matches : nullptr;

	auto result = AddEntry(entry);

	// Reset matches because the array will be invalid after return
	entry.mMatches = nullptr;
	entry.mMatchesLength = 0;

	return result;
}

AutoCompleteEntry* AutoCompleteBase::AddEntry(const AutoCompleteEntry& entry)
{
	if (mEntriesSet.mAllocSize == 0)
	{
		mEntriesSet.Reserve(128);
	}

	AutoCompleteEntry* insertedEntry = NULL;
	if (mEntriesSet.TryAdd(entry, &insertedEntry))
	{
		insertedEntry->mEntryType = entry.mEntryType;

		const char* display = entry.mDisplay;
		int size = (int)strlen(display) + 1;
		insertedEntry->mDisplay = (char*)mAlloc.AllocBytes(size);
		memcpy((char*)insertedEntry->mDisplay, display, size);

		if (entry.mMatchesLength > 0)
		{
			insertedEntry->mMatches = (uint8*)mAlloc.AllocBytes(insertedEntry->mMatchesLength);
			memcpy((char*)insertedEntry->mMatches, entry.mMatches, insertedEntry->mMatchesLength);
		}
	}

	return insertedEntry;
}

bool AutoCompleteBase::DoesFilterMatch(const char* entry, const char* filter, int& score, uint8* matches, int maxMatches)
{
	if (mIsGetDefinition)
	{
		int entryLen = (int)strlen(entry);
		if (entry[entryLen - 1] == '=')
			return (strncmp(filter, entry, entryLen - 1) == 0);
		return (strcmp(filter, entry) == 0);
	}

	if (!mIsAutoComplete)
		return false;

	if (filter[0] == '\0')
		return true;

	int filterLen = (int)strlen(filter);
	int entryLen = (int)strlen(entry);

	if (filterLen > entryLen)
		return false;

	if (mDoFuzzyAutoComplete)
	{
		return fts::fuzzy_match(filter, entry, score, matches, maxMatches);
	}
	else
	{
		bool hasUnderscore = false;
		bool checkInitials = filterLen > 1;
		for (int i = 0; i < (int)filterLen; i++)
		{
			char c = filter[i];
			if (c == '_')
				hasUnderscore = true;
			else if (islower((uint8)filter[i]))
				checkInitials = false;
		}

		if (hasUnderscore)
			return strnicmp(filter, entry, filterLen) == 0;

		char initialStr[256];
		char* initialStrP = initialStr;

		//String initialStr;
		bool prevWasUnderscore = false;

		for (int entryIdx = 0; entryIdx < entryLen; entryIdx++)
		{
			char entryC = entry[entryIdx];

			if (entryC == '_')
			{
				prevWasUnderscore = true;
				continue;
			}

			if ((entryIdx == 0) || (prevWasUnderscore) || (isupper((uint8)entryC) || (isdigit((uint8)entryC))))
			{
				if (strnicmp(filter, entry + entryIdx, filterLen) == 0)
					return true;
				if (checkInitials)
					*(initialStrP++) = entryC;
			}
			prevWasUnderscore = false;

			if (filterLen == 1)
				break; // Don't check inners for single-character case
		}

		if (!checkInitials)
			return false;
		*(initialStrP++) = 0;
		return strnicmp(filter, initialStr, filterLen) == 0;
	}
}

void AutoCompleteBase::Clear()
{
	//mEntries.clear();
	mAlloc.Clear();
	mEntriesSet.Clear();
}

//////////////////////////////////////////////////////////////////////////

BfAutoComplete::BfAutoComplete(BfResolveType resolveType, bool doFuzzyAutoComplete)
{
	mResolveType = resolveType;
	mModule = NULL;
	mCompiler = NULL;
	mSystem = NULL;
	mIsCapturingMethodMatchInfo = false;
	mIgnoreFixits = false;
	mHasFriendSet = false;
	mUncertain = false;
	mForceAllowNonStatic = false;
	mMethodMatchInfo = NULL;
	mIsGetDefinition =
		(resolveType == BfResolveType_GetSymbolInfo) ||
		(resolveType == BfResolveType_GoToDefinition);
	mIsAutoComplete = (resolveType == BfResolveType_Autocomplete);

	mDoFuzzyAutoComplete = doFuzzyAutoComplete;

	mGetDefinitionNode = NULL;
	mShowAttributeProperties = NULL;
	mIdentifierUsed = NULL;

	//mReplaceMethodInstance = NULL;
	mReplaceLocalId = -1;
	mDefType = NULL;
	mDefField = NULL;
	mDefMethod = NULL;
	mDefProp = NULL;
	mDefMethodGenericParamIdx = -1;
	mDefTypeGenericParamIdx = -1;

	mCursorLineStart = -1;
	mCursorLineEnd = -1;
}

BfAutoComplete::~BfAutoComplete()
{
	Clear();
}

void BfAutoComplete::SetModule(BfModule* module)
{
	if (module != NULL)
	{
		mModule = module;
		mCompiler = mModule->mCompiler;
		mSystem = mCompiler->mSystem;
	}
	else
	{
		mModule = NULL;
		mCompiler = NULL;
		mSystem = NULL;
	}
}

void BfAutoComplete::Clear()
{
	if (mMethodMatchInfo != NULL)
	{
		if (mMethodMatchInfo->mInstanceList.size() == 0)
		{
			delete mMethodMatchInfo;
			mMethodMatchInfo = NULL;
		}
		else
		{
			// Keep mBestIdx - for when we match but then backspace
			mMethodMatchInfo->mPrevBestIdx = mMethodMatchInfo->mBestIdx;
			mMethodMatchInfo->mMostParamsMatched = 0;
			mMethodMatchInfo->mHadExactMatch = false;
			mMethodMatchInfo->mInstanceList.Clear();
			mMethodMatchInfo->mSrcPositions.Clear();
		}
	}

	mInsertStartIdx = -1;
	mInsertEndIdx = -1;
	mIsCapturingMethodMatchInfo = false;

	AutoCompleteBase::Clear();
}

void BfAutoComplete::RemoveMethodMatchInfo()
{
	mIsCapturingMethodMatchInfo = false;
	delete mMethodMatchInfo;
	mMethodMatchInfo = NULL;
}

void BfAutoComplete::ClearMethodMatchEntries()
{
	mMethodMatchInfo->mInstanceList.Clear();
}

int BfAutoComplete::GetCursorIdx(BfAstNode* node)
{
	if (node == NULL)
		return -1;

// 	if (!node->IsFromParser(mCompiler->mResolvePassData->mParser))
// 		return -1;

	if (node->IsTemporary())
		return false;
	auto bfParser = node->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return -1;

	if ((bfParser->mParserFlags & ParserFlag_Autocomplete) == 0)
		return -1;

	return bfParser->mCursorIdx;
}

bool BfAutoComplete::IsAutocompleteNode(BfAstNode* node, int lengthAdd, int startAdd)
{
	if (mModule == NULL)
		return false;
	if (node == NULL)
		return false;

// 	if (!node->IsFromParser(mCompiler->mResolvePassData->mParser))
// 		return false;

	if (node->IsTemporary())
		return false;
	auto bfParser = node->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return false;
	if ((bfParser->mParserFlags & ParserFlag_Autocomplete) == 0)
		return false;

 	//if (mCompiler->mResolvePassData->mResolveType != BfResolveType_Autocomplete)
 	lengthAdd++;

	int cursorIdx = bfParser->mCursorCheckIdx;
	int nodeSrcStart = node->GetSrcStart();
	if ((cursorIdx < nodeSrcStart + startAdd) || (cursorIdx >= node->GetSrcEnd() + lengthAdd))
		return false;
	return true;
}

bool BfAutoComplete::IsAutocompleteNode(BfAstNode* startNode, BfAstNode* endNode, int lengthAdd, int startAdd)
{
	if ((startNode == NULL) || (endNode == NULL))
		return false;

// 	if (!startNode->IsFromParser(mCompiler->mResolvePassData->mParser))
// 		return false;

	if (startNode->IsTemporary())
		return false;
	auto bfParser = startNode->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return false;
	if ((bfParser->mParserFlags & ParserFlag_Autocomplete) == 0)
		return false;

	//if (mCompiler->mResolvePassData->mResolveType != BfResolveType_Autocomplete)
	lengthAdd++;

	int cursorIdx = bfParser->mCursorCheckIdx;
	int nodeSrcStart = startNode->GetSrcStart();
	if ((cursorIdx < nodeSrcStart + startAdd) || (cursorIdx >= endNode->GetSrcEnd() + lengthAdd))
		return false;
	return true;
}

bool BfAutoComplete::IsAutocompleteLineNode(BfAstNode* node)
{
	if (node == NULL)
		return false;

// 	if (!node->IsFromParser(mCompiler->mResolvePassData->mParser))
// 		return false;

	if (node->IsTemporary())
		return false;
	auto bfParser = node->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return false;
	if ((bfParser->mParserFlags & ParserFlag_Autocomplete) == 0)
		return false;

	int startAdd = 0;

	if (mCursorLineStart == -1)
	{
		auto nodeSource = node->GetSourceData();

		mCursorLineStart = bfParser->mCursorIdx;
		while (mCursorLineStart > 0)
		{
		    if (nodeSource->mSrc[mCursorLineStart - 1] == '\n')
				break;
			mCursorLineStart--;
		}

		mCursorLineEnd = bfParser->mCursorIdx;
		while (mCursorLineEnd < nodeSource->mSrcLength)
		{
			if (nodeSource->mSrc[mCursorLineEnd] == '\n')
				break;
			mCursorLineEnd++;
		}
	}

	int srcStart = node->GetSrcStart();
	return (srcStart >= mCursorLineStart) && (srcStart <= mCursorLineEnd);
}

// The parser thought this was a type reference but it may not be
BfTypedValue BfAutoComplete::LookupTypeRefOrIdentifier(BfAstNode* node, bool* isStatic, BfEvalExprFlags evalExprFlags, BfType* expectingType)
{
	SetAndRestoreValue<bool> prevIgnoreClassifying(mModule->mIsInsideAutoComplete, true);

	auto _FixType = [](const BfTypedValue& typedValue)
	{
		if ((typedValue.mType != NULL) && (typedValue.mType->IsAllocType()))
		{
			BfTypedValue ret = typedValue;
			ret.mType = ret.mType->GetUnderlyingType();
			return ret;
		}
		return typedValue;
	};

	if (auto typeRef = BfNodeDynCast<BfTypeReference>(node))
	{
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
		{
			BfExprEvaluator exprEvaluator(mModule);
			auto identifierResult = exprEvaluator.LookupIdentifier(namedTypeRef->mNameNode);
			if (identifierResult)
				return identifierResult;
			identifierResult = exprEvaluator.GetResult();// We need 'GetResult' to read property values
			if (identifierResult.HasType())
				return _FixType(identifierResult);
		}
		else if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
		{
			auto leftValue = LookupTypeRefOrIdentifier(qualifiedTypeRef->mLeft, isStatic);

			if (leftValue.mType)
			{
				if (leftValue.mType->IsPointer())
				{
					mModule->LoadValue(leftValue);
					leftValue.mType = leftValue.mType->GetUnderlyingType();
					leftValue.mKind = BfTypedValueKind_Addr;
				}

				if (auto rightNamedTypeRef = BfNodeDynCast<BfNamedTypeReference>(qualifiedTypeRef->mRight))
				{
					// This keeps the classifier from colorizing properties - this causes 'flashing' when we go back over this with a resolve pass
					//  that wouldn't catch this
					SetAndRestoreValue<bool> prevClassifier(mModule->mCompiler->mResolvePassData->mIsClassifying, false);

					BfExprEvaluator exprEvaluator(mModule);
					auto fieldResult = exprEvaluator.LookupField(qualifiedTypeRef->mRight, leftValue, rightNamedTypeRef->mNameNode->ToString());
					if (!fieldResult) // Was property?
						fieldResult = exprEvaluator.GetResult();
					if (fieldResult.HasType())
					{
						*isStatic = false;
						return _FixType(fieldResult);
					}
				}
			}
		}

		BfResolveTypeRefFlags resolveTypeRefFlags = BfResolveTypeRefFlag_AllowUnboundGeneric;
		if (mResolveType == BfResolveType_VerifyTypeName)
			resolveTypeRefFlags = (BfResolveTypeRefFlags)(resolveTypeRefFlags | BfResolveTypeRefFlag_ForceUnboundGeneric);
		BfType* type = mModule->ResolveTypeRef(typeRef, BfPopulateType_Identity, resolveTypeRefFlags);
		if (type != NULL)
		{
			*isStatic = true;
			return _FixType(BfTypedValue(type));
		}
	}
	if (auto identifier = BfNodeDynCast<BfIdentifierNode>(node))
	{
		BfExprEvaluator exprEvaluator(mModule);
		auto identifierResult = exprEvaluator.LookupIdentifier(identifier, false, NULL);
		if (!identifierResult)
			identifierResult = exprEvaluator.GetResult();
		if (identifierResult)
			return _FixType(identifierResult);

		if (auto qualifiedIdentifier = BfNodeDynCast<BfQualifiedNameNode>(node))
		{
			bool leftIsStatic = false;
			auto leftValue = LookupTypeRefOrIdentifier(qualifiedIdentifier->mLeft, &leftIsStatic);
			if (leftValue.mType)
			{
				auto findName = qualifiedIdentifier->mRight->ToString();
				if (findName == "base")
				{
					return BfTypedValue(leftValue);
				}

				BfExprEvaluator exprEvaluator(mModule);
				auto fieldResult = exprEvaluator.LookupField(node, leftValue, findName);
				if (fieldResult)
					return fieldResult;
				auto result = exprEvaluator.GetResult();
				if (result)
					return _FixType(result);
			}
		}

		auto type = mModule->ResolveTypeRef(identifier, NULL);
		if (type != NULL)
		{
			*isStatic = true;
			return _FixType(BfTypedValue(type));
		}
	}
	else if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(node))
	{
		return _FixType(mModule->CreateValueFromExpression(memberRefExpr, expectingType, evalExprFlags));
	}
	else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(node))
	{
		// Don't pass BfEvalExprFlags_IgnoreNullConditional, since parenExprs end nullable chains and we actually
		//  DO want the nullable at this point
		return _FixType(mModule->CreateValueFromExpression(parenExpr));
	}
	else if (auto targetExpr = BfNodeDynCast<BfExpression>(node))
	{
		return _FixType(mModule->CreateValueFromExpression(targetExpr, expectingType, evalExprFlags));
	}

	return BfTypedValue();
}

void BfAutoComplete::SetDefinitionLocation(BfAstNode* astNode, bool force)
{
	if (mIsGetDefinition)
	{
		if ((mGetDefinitionNode == NULL) || (force))
			mGetDefinitionNode = astNode;
	}
}

bool BfAutoComplete::IsAttribute(BfTypeInstance* typeInst)
{
	auto checkTypeInst = typeInst;
	while (checkTypeInst != NULL)
	{
		if (checkTypeInst->mTypeDef->GetLatest() == mModule->mCompiler->mAttributeTypeDef->GetLatest())
			return true;

		checkTypeInst = checkTypeInst->mBaseType;
	}
	return false;
}

void BfAutoComplete::AddMethod(BfTypeInstance* typeInstance, BfMethodDef* methodDef, BfMethodInstance* methodInstance, BfMethodDeclaration* methodDecl, const StringImpl& methodName, const StringImpl& filter)
{
	int wantPrefixCount = 0;
	const char* filterStr = filter.c_str();
	while (filterStr[0] == '@')
	{
		filterStr++;
		wantPrefixCount++;
	}
	String replaceName;
	AutoCompleteEntry entry("method", methodName, methodDef->mNamePrefixCount - wantPrefixCount);
	if (methodDecl != NULL)
	{
		if (methodDecl->mMixinSpecifier != NULL)
		{
			replaceName = entry.mDisplay;
			replaceName += "!";
			entry.mDisplay = replaceName.c_str();
			entry.mEntryType = "mixin";
		}
	}
	if (methodDef->mMethodType == BfMethodType_Extension)
		entry.mEntryType = "extmethod";

	if (auto entryAdded = AddEntry(entry, filterStr))
	{
		if (methodDecl != NULL)
		{
			if ((methodInstance != NULL) && (methodInstance->mMethodDef->mIsLocalMethod) && (methodDecl->mReturnType != NULL) && (GetCursorIdx(methodDecl) == methodDecl->mReturnType->mSrcEnd))
			{
				// This isn't really a local method decl, it just looks like one
				return;
			}

			if (CheckDocumentation(entryAdded, NULL))
			{
				String str;
				if ((methodInstance == NULL) && (methodDef != NULL))
					methodInstance = mModule->GetRawMethodInstance(typeInstance, methodDef);
				if (methodInstance != NULL)
				{
					SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mModule->mCurTypeInstance, typeInstance);
					SetAndRestoreValue<BfMethodInstance*> prevCurMethodInstance(mModule->mCurMethodInstance, methodInstance);
					str = mModule->MethodToString(methodInstance, (BfMethodNameFlags)(BfMethodNameFlag_IncludeReturnType | BfMethodNameFlag_ResolveGenericParamNames));
				}

				if (entryAdded->mDocumentation != NULL)
				{
					str += "\x04";
					str.Append(entryAdded->mDocumentation);
				}
				else if (methodDecl->mDocumentation != NULL)
				{
					if (!str.IsEmpty())
						str += "\x05";
					methodDecl->mDocumentation->GetDocString(str);
				}
				if (!str.IsEmpty())
					entryAdded->mDocumentation = mAlloc.AllocString(str);
			}
		}

		if ((mResolveType == BfResolveType_GoToDefinition) && (mGetDefinitionNode == NULL) && (methodDecl != NULL) && (methodDecl->mNameNode != NULL))
			SetDefinitionLocation(methodDecl->mNameNode);
	}
}

void BfAutoComplete::AddTypeDef(BfTypeDef* typeDef, const StringImpl& filter, bool onlyAttribute)
{
	BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Emitted);

	if (typeDef->mTypeDeclaration == NULL)
		return;

	StringT<64> name(typeDef->mName->ToString());
	if (name == "@")
		return;
	int gravePos = (int)name.IndexOf('`');
	if (gravePos != -1)
		name = name.Substring(0, gravePos) + "<>";

	if (onlyAttribute)
	{
		if ((mIsGetDefinition) && (name == filter + "Attribute"))
		{
			SetDefinitionLocation(typeDef->mTypeDeclaration->mNameNode);
			return;
		}

		int score;
		uint8 matches[256];
		if (!DoesFilterMatch(name.c_str(), filter.c_str(), score, matches, sizeof(matches)))
			return;

		auto type = mModule->ResolveTypeDef(typeDef, BfPopulateType_Declaration);
		if (type != NULL)
		{
			auto typeInst = type->ToTypeInstance();
			if (!IsAttribute(typeInst))
				return;
		}

		const char* attrStr = "Attribute";
		const int attrStrLen = (int)strlen(attrStr);
		if (((int)name.length() > attrStrLen) && ((int)name.length() - (int)attrStrLen >= filter.length()) && (strcmp(name.c_str() + (int)name.length() - attrStrLen, attrStr) == 0))
		{
			// Shorter name - remove "Attribute"
			name = name.Substring(0, name.length() - attrStrLen);
		}
	}

	AutoCompleteEntry* entryAdded = NULL;
	if (typeDef->mTypeCode == BfTypeCode_Object)
		entryAdded = AddEntry(AutoCompleteEntry("class", name), filter);
	else if (typeDef->mTypeCode == BfTypeCode_Interface)
		entryAdded = AddEntry(AutoCompleteEntry("interface", name), filter);
	else
		entryAdded = AddEntry(AutoCompleteEntry("valuetype", name), filter);

	if (entryAdded != NULL)
	{
		if ((CheckDocumentation(entryAdded, NULL)) && (entryAdded->mDocumentation == NULL))
		{
			auto type = mModule->ResolveTypeDef(typeDef, BfPopulateType_IdentityNoRemapAlias);
			StringT<1024> str;
			if (type != NULL)
			{
				SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mModule->mCurTypeInstance, type->ToTypeInstance());
				SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mModule->mCurMethodInstance, NULL);
				str = mModule->TypeToString(type, (BfTypeNameFlags)(BfTypeNameFlag_ExtendedInfo | BfTypeNameFlag_ResolveGenericParamNames));
			}
			if (typeDef->mTypeDeclaration->mDocumentation != NULL)
			{
				if (!str.IsEmpty())
					str += "\x05";
				typeDef->mTypeDeclaration->mDocumentation->GetDocString(str);
			}
			entryAdded->mDocumentation = mAlloc.AllocString(str);
		}
	}
}

bool BfAutoComplete::CheckProtection(BfProtection protection, BfTypeDef* typeDef, bool allowProtected, bool allowPrivate)
{
	if (mResolveType == BfResolveType_GetSymbolInfo)
	{
		// This is needed for nameof on private inner types
		return true;
	}

	if ((protection == BfProtection_Internal) && (typeDef != NULL))
	{
		return mModule->CheckProtection(protection, typeDef, allowProtected, allowPrivate);
	}

	return (mHasFriendSet) || (protection == BfProtection_Public) ||
		((protection == BfProtection_Protected) && (allowProtected)) ||
		((protection == BfProtection_Private) && (allowPrivate));
}

const char* BfAutoComplete::GetTypeName(BfType* type)
{
	if (type != NULL)
	{
		if (type->IsPointer())
			return "pointer";
		if (type->IsObjectOrInterface())
			return "object";
	}
	return "value";
}

void BfAutoComplete::AddInnerTypes(BfTypeInstance* typeInst, const StringImpl& filter, BfTypeInstance* startType, bool allowProtected, bool allowPrivate)
{
	if (typeInst->IsEnum())
		AddEntry(AutoCompleteEntry("valuetype", "UnderlyingType"), filter);

	BfShow checkShow = (typeInst == startType) ? BfShow_Hide : BfShow_HideIndirect;

	for (auto innerType : typeInst->mTypeDef->mNestedTypes)
	{
		if (innerType->mShow >= checkShow)
			continue;

		if (innerType->mOuterType->mTypeCode == BfTypeCode_Extension)
		{
			if (typeInst->mDefineState < BfTypeDefineState_Defined)
				mModule->PopulateType(typeInst);

			if ((typeInst->mGenericTypeInfo != NULL) && (typeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL))
			{
				if (!typeInst->mGenericTypeInfo->mGenericExtensionInfo->mConstraintsPassedSet.IsSet(innerType->mOuterType->mPartialIdx))
					continue;
			}
		}

		if (CheckProtection(innerType->mProtection, innerType, allowProtected, allowPrivate))
			AddTypeDef(innerType, filter);
	}

	allowPrivate = false;
	if (typeInst->mBaseType != NULL)
		AddInnerTypes(typeInst->mBaseType, filter, startType, allowProtected, allowPrivate);
}

void BfAutoComplete::AddCurrentTypes(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate, bool onlyAttribute)
{
	if (typeInst != mModule->mCurTypeInstance)
		AddTypeDef(typeInst->mTypeDef->GetDefinition(), filter, onlyAttribute);

	auto typeDef = typeInst->mTypeDef->GetDefinition();
	for (auto nestedTypeDef : typeDef->mNestedTypes)
	{
		if (nestedTypeDef->mIsPartial)
		{
			nestedTypeDef = mSystem->GetCombinedPartial(nestedTypeDef);
			if (nestedTypeDef == NULL)
				continue;
		}

	 	if (CheckProtection(nestedTypeDef->mProtection, nestedTypeDef, allowProtected, allowPrivate))
	 		AddTypeDef(nestedTypeDef, filter, onlyAttribute);
	}

	auto outerType = mModule->GetOuterType(typeInst);
	if (outerType != NULL)
		AddCurrentTypes(outerType, filter, allowProtected, allowPrivate, onlyAttribute);

	allowPrivate = false;
	auto baseType = mModule->GetBaseType(typeInst);
	if (baseType != NULL)
		AddCurrentTypes(baseType, filter, allowProtected, allowPrivate, onlyAttribute);
}

void BfAutoComplete::AddField(BfTypeInstance* typeInst, BfFieldDef* fieldDef, BfFieldInstance* fieldInstance, const StringImpl& filter)
{
	int wantPrefixCount = 0;
	const char* filterStr = filter.c_str();
	while (filterStr[0] == '@')
	{
		filterStr++;
		wantPrefixCount++;
	}
	AutoCompleteEntry entry(GetTypeName(fieldInstance->mResolvedType), fieldDef->mName, fieldDef->mNamePrefixCount - wantPrefixCount);
	if (auto entryAdded = AddEntry(entry, filterStr))
	{
		auto fieldDecl = fieldDef->GetFieldDeclaration();
		auto documentation = (fieldDecl != NULL) ? fieldDecl->mDocumentation : NULL;
		if (CheckDocumentation(entryAdded, documentation))
		{
			mModule->PopulateType(typeInst);

			String str;
			str += mModule->TypeToString(fieldInstance->mResolvedType);
			str += " ";
			str += mModule->TypeToString(typeInst);
			str += ".";
			str += fieldDef->mName;
			if (entryAdded->mDocumentation != NULL)
			{
				str += "\x04";
				str.Append(entryAdded->mDocumentation);
			}
			else if (documentation != NULL)
			{
				str += "\x05";
				documentation->GetDocString(str);
			}
			entryAdded->mDocumentation = mAlloc.AllocString(str);
		}

		if ((mIsGetDefinition) && (mDefType == NULL))
		{
			mDefType = typeInst->mTypeDef;
			mDefField = fieldDef;
			if (auto nameNode = fieldDef->GetNameNode())
				SetDefinitionLocation(nameNode);
		}
	}
}

void BfAutoComplete::AddProp(BfTypeInstance* typeInst, BfPropertyDef* propDef, const StringImpl& filter)
{
	int wantPrefixCount = 0;
	const char* filterStr = filter.c_str();
	while (filterStr[0] == '@')
	{
		filterStr++;
		wantPrefixCount++;
	}
	BfCommentNode* documentation = NULL;
	auto fieldDecl = propDef->GetFieldDeclaration();
	if (fieldDecl != NULL)
		documentation = fieldDecl->mDocumentation;
	AutoCompleteEntry entry("property", propDef->mName, propDef->mNamePrefixCount - wantPrefixCount);
	if (auto entryAdded = AddEntry(entry, filterStr))
	{
		if (CheckDocumentation(entryAdded, documentation))
		{
			BfType* propType = NULL;
			bool hasGetter = false;
			bool hasSetter = false;

			for (auto methodDef : propDef->mMethods)
			{
				auto methodInstance = mModule->GetRawMethodInstance(typeInst, methodDef);
				if (methodInstance == NULL)
					continue;
				if (methodDef->mMethodType == BfMethodType_PropertyGetter)
				{
					hasGetter = true;
					propType = methodInstance->mReturnType;
				}
				if (methodDef->mMethodType == BfMethodType_PropertySetter)
				{
					hasSetter = true;
					if (methodInstance->GetParamCount() > 0)
						propType = methodInstance->GetParamType(0);
				}
			}

			String str;
			if (propType != NULL)
			{
				str += mModule->TypeToString(propType);
				str += " ";
			}

			str += mModule->TypeToString(typeInst);
			str += ".";
			str += propDef->mName;

			str += " { ";
			if (hasGetter)
				str += "get; ";
			if (hasSetter)
				str += "set; ";
			str += "}";

			if (entryAdded->mDocumentation != NULL)
			{
				str += "\x04";
				str.Append(entryAdded->mDocumentation);
			}
			else if (documentation != NULL)
			{
				str += "\x05";
				documentation->GetDocString(str);
			}
			entryAdded->mDocumentation = mAlloc.AllocString(str);
		}
		if ((mIsGetDefinition) && (fieldDecl != NULL))
			SetDefinitionLocation(fieldDecl->mNameNode);
	}
}

void BfAutoComplete::AddTypeMembers(BfTypeInstance* typeInst, bool addStatic, bool addNonStatic, const StringImpl& filter, BfTypeInstance* startType, bool allowInterfaces, bool allowImplicitThis, bool checkOuterType)
{
	bool isInterface = false;

	if (mForceAllowNonStatic)
		addNonStatic = true;

	auto activeTypeDef = mModule->GetActiveTypeDef();

	if ((addStatic) && (mModule->mCurMethodInstance == NULL) && (typeInst->IsEnum()) && (allowImplicitThis))
	{
		AddEntry(AutoCompleteEntry("value", "_"), filter);
	}

#define CHECK_STATIC(staticVal) ((staticVal && addStatic) || (!staticVal && addNonStatic))

	mModule->PopulateType(typeInst, BfPopulateType_Data);

	BfShow checkShow = (startType == typeInst) ? BfShow_Hide : BfShow_HideIndirect;

	BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
	for (auto& fieldInst : typeInst->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if (fieldDef == NULL)
			continue;

		if (fieldDef->mShow >= checkShow)
			continue;

		if ((CHECK_STATIC(fieldDef->mIsStatic)) &&
			((mIsGetDefinition) || (mModule->CheckProtection(protectionCheckFlags, typeInst, fieldDef->mDeclaringType->mProject, fieldDef->mProtection, startType))))
		{
			if ((!typeInst->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, mModule)) ||
				(!typeInst->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
				continue;

			AddField(typeInst, fieldDef, &fieldInst, filter);
		}
	}

	for (auto methodDef : typeInst->mTypeDef->mMethods)
	{
		if (methodDef->mIsOverride)
			continue;
		if (methodDef->mShow >= checkShow)
			continue;
		if (methodDef->mName.IsEmpty())
			continue;

		if (methodDef->mExplicitInterface != NULL)
			continue;
		if ((!typeInst->IsTypeMemberIncluded(methodDef->mDeclaringType, activeTypeDef, mModule)) ||
			(!typeInst->IsTypeMemberAccessible(methodDef->mDeclaringType, activeTypeDef)))
			continue;

		bool canUseMethod;
		canUseMethod = (methodDef->mMethodType == BfMethodType_Normal) || (methodDef->mMethodType == BfMethodType_Mixin);
		if (isInterface)
		{
			// Always allow
			canUseMethod &= addNonStatic;
		}
		else
		{
			canUseMethod &= (CHECK_STATIC(methodDef->mIsStatic) &&
				(mModule->CheckProtection(protectionCheckFlags, typeInst, methodDef->mDeclaringType->mProject, methodDef->mProtection, startType)));
		}
		if (canUseMethod)
		{
			AddMethod(typeInst, methodDef, NULL, methodDef->GetMethodDeclaration(), methodDef->mName, filter);
		}
	}

	for (auto propDef : typeInst->mTypeDef->mProperties)
	{
		if (propDef->mShow >= checkShow)
			continue;

		if ((!typeInst->IsTypeMemberIncluded(propDef->mDeclaringType, activeTypeDef, mModule)) ||
			(!typeInst->IsTypeMemberAccessible(propDef->mDeclaringType, activeTypeDef)))
			continue;

		if ((CHECK_STATIC(propDef->mIsStatic)) && (mModule->CheckProtection(protectionCheckFlags, typeInst, propDef->mDeclaringType->mProject, propDef->mProtection, startType)))
		{
			if ((!allowInterfaces) && (propDef->HasExplicitInterface()))
				continue;
			if (propDef->mName == "[]")
				continue;

			AddProp(typeInst, propDef, filter);
		}
	}

	if (allowInterfaces)
	{
		for (auto iface : typeInst->mInterfaces)
			AddTypeMembers(iface.mInterfaceType, addStatic, addNonStatic, filter, startType, false, allowImplicitThis, false);
	}

	if (typeInst->mBaseType != NULL)
		AddTypeMembers(typeInst->mBaseType, addStatic, addNonStatic, filter, startType, false, allowImplicitThis, false);
	else
	{
		if (typeInst->IsStruct())
			AddTypeMembers(mModule->mContext->mBfObjectType, addStatic, addNonStatic, filter, startType, false, allowImplicitThis, false);
	}

	if (typeInst->IsInterface())
	{
		for (auto interface : typeInst->mInterfaces)
			AddTypeMembers(interface.mInterfaceType, addStatic, addNonStatic, filter, startType, false, allowImplicitThis, false);
	}

	if ((addStatic) && (allowImplicitThis) && (checkOuterType))
	{
		auto outerType = mModule->GetOuterType(typeInst);
		if (outerType != NULL)
		{
			AddTypeMembers(outerType, true, false, filter, startType, false, allowImplicitThis, false);
		}
	}

	if ((typeInst->mTypeDef->mHasUsingFields) &&
		((typeInst->mTypeInfoEx == NULL) || (typeInst->mTypeInfoEx->mUsingFieldData == NULL)))
		mModule->PopulateUsingFieldData(typeInst);

	if ((typeInst->mTypeInfoEx != NULL) && (typeInst->mTypeInfoEx->mUsingFieldData != NULL))
	{
		for (int pass = 0; pass < 2; pass++)
		{
			auto& dict = (pass == 0) ? typeInst->mTypeInfoEx->mUsingFieldData->mEntries : typeInst->mTypeInfoEx->mUsingFieldData->mMethods;
			for (auto& entryKV : dict)
			{
				for (auto& entryList : entryKV.mValue.mLookups)
				{
					auto& endEntry = entryList.back();

					bool isStatic = endEntry.IsStatic();
					if ((isStatic) && (!addStatic))
						continue;
					if ((!isStatic) && (!addNonStatic))
						continue;

					bool passesProtection = true;
					for (int entryIdx = 0; entryIdx < entryList.mSize; entryIdx++)
					{
						auto& entry = entryList[entryIdx];
						if (!mModule->CheckProtection(protectionCheckFlags, entry.mTypeInstance, entry.GetDeclaringType(mModule)->mProject,
							(entryIdx < entryList.mSize - 1) ? entry.GetUsingProtection() : entry.GetProtection(), typeInst))
						{
							passesProtection = false;
							break;
						}
					}
					if (!passesProtection)
						continue;

					switch (endEntry.mKind)
					{
					case BfUsingFieldData::MemberRef::Kind_Field:
						if (endEntry.mTypeInstance->mDefineState < BfTypeDefineState_Defined)
							mModule->PopulateType(endEntry.mTypeInstance);
						AddField(endEntry.mTypeInstance, endEntry.mTypeInstance->mTypeDef->mFields[endEntry.mIdx], &endEntry.mTypeInstance->mFieldInstances[endEntry.mIdx], filter);
						break;
					case BfUsingFieldData::MemberRef::Kind_Property:
						AddProp(endEntry.mTypeInstance, endEntry.mTypeInstance->mTypeDef->mProperties[endEntry.mIdx], filter);
						break;
					case BfUsingFieldData::MemberRef::Kind_Method:
					{
						auto methodDef = endEntry.mTypeInstance->mTypeDef->mMethods[endEntry.mIdx];
						AddMethod(endEntry.mTypeInstance, methodDef, NULL, methodDef->GetMethodDeclaration(), methodDef->mName, filter);
					}
					break;
					}
				}
			}
		}
	}
}

void BfAutoComplete::AddSelfResultTypeMembers(BfTypeInstance* typeInst, BfTypeInstance* selfType, const StringImpl& filter, bool allowPrivate)
{
	bool isInterface = false;
	bool allowProtected = allowPrivate;

	auto activeTypeDef = mModule->GetActiveTypeDef();

	mModule->PopulateType(typeInst, BfPopulateType_Data);

	BfShow checkShow = allowPrivate ? BfShow_Hide : BfShow_HideIndirect;

	for (auto& fieldInst : typeInst->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if (fieldDef == NULL)
			continue;

		if (fieldDef->mShow > checkShow)
			continue;

		if ((fieldDef->mIsStatic) && (CheckProtection(fieldDef->mProtection, fieldDef->mDeclaringType, allowProtected, allowPrivate)))
		{
			if (!mModule->CanCast(BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), fieldInst.mResolvedType), selfType))
				continue;

			if ((!typeInst->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, mModule)) ||
				(!typeInst->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
				continue;

			AddField(typeInst, fieldDef, &fieldInst, filter);
		}
	}

	for (auto methodDef : typeInst->mTypeDef->mMethods)
	{
		if (methodDef->mIsOverride)
			continue;
		if (methodDef->mShow > checkShow)
			continue;
		if (methodDef->mName.IsEmpty())
			continue;

		if (methodDef->mExplicitInterface != NULL)
			continue;
		if ((!typeInst->IsTypeMemberIncluded(methodDef->mDeclaringType, activeTypeDef, mModule)) ||
			(!typeInst->IsTypeMemberAccessible(methodDef->mDeclaringType, activeTypeDef)))
			continue;

		if (!methodDef->mIsStatic)
			continue;

		bool canUseMethod;
		canUseMethod = (methodDef->mMethodType == BfMethodType_Normal) || (methodDef->mMethodType == BfMethodType_Mixin);
		canUseMethod &= CheckProtection(methodDef->mProtection, methodDef->mDeclaringType, allowProtected, allowPrivate);

		if (methodDef->mMethodType != BfMethodType_Normal)
			continue;

		auto methodInstance = mModule->GetRawMethodInstanceAtIdx(typeInst, methodDef->mIdx);
		if (methodInstance == NULL)
			continue;
		if (methodInstance->mReturnType->IsUnspecializedType())
			continue;
		if (!mModule->CanCast(BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), methodInstance->mReturnType), selfType))
			continue;

		if (canUseMethod)
		{
			if (auto methodDeclaration = methodDef->GetMethodDeclaration())
			{
				AddMethod(typeInst, methodDef, NULL, methodDeclaration, methodDef->mName, filter);
			}
		}
	}

	for (auto propDef : typeInst->mTypeDef->mProperties)
	{
		if (propDef->mShow > checkShow)
			continue;

		if ((!typeInst->IsTypeMemberIncluded(propDef->mDeclaringType, activeTypeDef, mModule)) ||
			(!typeInst->IsTypeMemberAccessible(propDef->mDeclaringType, activeTypeDef)))
			continue;

		if (!propDef->mIsStatic)
			continue;

		BfMethodDef* getMethod = NULL;
		for (auto methodDef : propDef->mMethods)
		{
			if (methodDef->mMethodType == BfMethodType_PropertyGetter)
			{
				getMethod = methodDef;
				break;
			}
		}

		if (getMethod == NULL)
			continue;

		auto methodInstance = mModule->GetRawMethodInstanceAtIdx(typeInst, getMethod->mIdx);
		if (methodInstance == NULL)
			continue;
		if (methodInstance->mReturnType != selfType)
			continue;

		if (CheckProtection(propDef->mProtection, propDef->mDeclaringType, allowProtected, allowPrivate))
		{
			if (propDef->HasExplicitInterface())
				continue;
			if (propDef->mName == "[]")
				continue;

			AddProp(typeInst, propDef, filter);
		}
	}

	auto outerType = mModule->GetOuterType(typeInst);
	if (outerType != NULL)
	{
		AddSelfResultTypeMembers(outerType, selfType, filter, false);
	}
}

bool BfAutoComplete::InitAutocomplete(BfAstNode* dotNode, BfAstNode* nameNode, String& filter)
{
	bool isDot = (dotNode != NULL) && (dotNode->mToken == BfToken_Dot);
	if (IsAutocompleteNode(nameNode))
	{
		auto bfParser = nameNode->GetSourceData()->ToParser();

		if ((mIsGetDefinition) || (!isDot))
		{
			mInsertStartIdx = nameNode->GetSrcStart();
			mInsertEndIdx = nameNode->GetSrcEnd();
		}
		else
		{
			mInsertStartIdx = dotNode->GetSrcEnd();
			mInsertEndIdx = std::min(bfParser->mCursorIdx + 1, nameNode->GetSrcEnd());
		}

		filter.Append(bfParser->mSrc + nameNode->GetSrcStart(), mInsertEndIdx - nameNode->GetSrcStart());
		return true;
	}

	int lenAdd = 0;
	if (!isDot)
		lenAdd++;

	if ((dotNode != NULL) && (IsAutocompleteNode(dotNode, lenAdd, 1)))
	{
		mInsertStartIdx = dotNode->GetSrcEnd();
		mInsertEndIdx = dotNode->GetSrcEnd();
		return true;
	}

	return false;
}

void BfAutoComplete::AddEnumTypeMembers(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate)
{
	mModule->PopulateType(typeInst, BfPopulateType_Data);

	auto activeTypeDef = mModule->GetActiveTypeDef();

	for (auto& fieldInst : typeInst->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if ((fieldDef != NULL) && (fieldDef->mIsConst) &&
			((fieldInst.mResolvedType == typeInst) || (fieldInst.mIsEnumPayloadCase)) &&
			(CheckProtection(fieldDef->mProtection, fieldDef->mDeclaringType, allowProtected, allowPrivate)))
		{
			if ((!typeInst->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, mModule)) ||
				(!typeInst->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
				continue;

			bool hasPayload = false;
			if ((fieldInst.mIsEnumPayloadCase) && (fieldInst.mResolvedType->IsTuple()))
			{
				auto payloadType = (BfTypeInstance*)fieldInst.mResolvedType;
				if (!payloadType->mFieldInstances.empty())
					hasPayload = true;
			}

			AutoCompleteEntry entry(hasPayload ? "payloadEnum" : "value", fieldDef->mName);
			if (auto entryAdded = AddEntry(entry, filter))
			{
				if (CheckDocumentation(entryAdded, fieldDef->GetFieldDeclaration()->mDocumentation))
				{
				}

				if (mIsGetDefinition)
				{
					mDefType = typeInst->mTypeDef;
					mDefField = fieldDef;
					if (fieldDef->mFieldDeclaration != NULL)
						SetDefinitionLocation(fieldDef->GetFieldDeclaration()->mNameNode);
				}
			}
		}
	}
}

void BfAutoComplete::AddExtensionMethods(BfTypeInstance* targetType, BfTypeInstance* extensionContainer, const StringImpl & filter, bool allowProtected, bool allowPrivate)
{
	if (!extensionContainer->mTypeDef->mHasExtensionMethods)
		return;

	mModule->PopulateType(extensionContainer, BfPopulateType_Data);

	BfShow checkShow = allowPrivate ? BfShow_Hide : BfShow_HideIndirect;

	for (auto methodDef : extensionContainer->mTypeDef->mMethods)
	{
		if (methodDef->mMethodType != BfMethodType_Extension)
			continue;
		if (methodDef->mShow >= checkShow)
			continue;
		if (methodDef->mName.IsEmpty())
			continue;

		bool canUseMethod = true;
		canUseMethod &= CheckProtection(methodDef->mProtection, methodDef->mDeclaringType, allowProtected, allowPrivate);

		auto methodInstance = mModule->GetRawMethodInstanceAtIdx(extensionContainer, methodDef->mIdx);
		if (methodInstance == NULL)
			continue;

		int score;
		uint8 matches[256];
		// Do filter match first- may be cheaper than generic validation
		if (!DoesFilterMatch(methodDef->mName.c_str(), filter.c_str(), score, matches, sizeof(matches)))
			continue;

		auto thisType = methodInstance->GetParamType(0);
		bool paramValidated = false;
		if (methodInstance->GetNumGenericParams() > 0)
		{
			if ((thisType->IsGenericParam()) && (methodInstance->GetNumGenericParams() == 1))
			{
				auto genericParamType = (BfGenericParamType*)thisType;
				if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
				{
					auto& genericParams = methodInstance->mMethodInfoEx->mGenericParams;
					if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), targetType, NULL, genericParams[genericParamType->mGenericParamIdx], NULL, NULL))
						continue;
					paramValidated = true;
				}
			}

			if (((thisType->IsUnspecializedTypeVariation()) || (thisType->IsGenericParam())) &&
				(!paramValidated))
			{
				BfTypeVector genericTypeVector;
				genericTypeVector.resize(methodInstance->GetNumGenericParams());
				BfGenericInferContext genericInferContext;
				genericInferContext.mCheckMethodGenericArguments = &genericTypeVector;
				genericInferContext.mModule = mModule;
				genericInferContext.mPrevArgValues.resize(methodInstance->GetNumGenericParams());

				if (!genericInferContext.InferGenericArgument(methodInstance, targetType, thisType, BfIRValue()))
					continue;
				genericInferContext.InferGenericArguments(methodInstance);

				thisType = mModule->ResolveGenericType(thisType, NULL, &genericTypeVector, mModule->mCurTypeInstance, false);
				if (thisType == NULL)
					continue;

				auto& genericParams = methodInstance->mMethodInfoEx->mGenericParams;
				bool validateError = false;
				for (int genericIdx = 0; genericIdx < (int)genericTypeVector.size(); genericIdx++)
				{
					auto genericArg = genericTypeVector[genericIdx];
					if (genericArg == NULL)
						continue;
					if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), genericArg, NULL, genericParams[genericIdx], &genericTypeVector, NULL))
					{
						validateError = true;
						break;
					}
				}

				if (validateError)
					continue;
			}
		}

		if (!paramValidated)
		{
			if (!mModule->CanCast(BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), targetType), thisType))
				continue;
		}

		if (canUseMethod)
		{
			if (auto methodDeclaration = methodDef->GetMethodDeclaration())
			{
				AddMethod(extensionContainer, methodDef, NULL, methodDeclaration, methodDef->mName, filter);
			}
		}
	}
}

BfProject* BfAutoComplete::GetActiveProject()
{
	BfProject* bfProject = NULL;
	auto activeTypeDef = mModule->GetActiveTypeDef();
	if (activeTypeDef != NULL)
		bfProject = activeTypeDef->mProject;
	else if (!mCompiler->mResolvePassData->mParsers.IsEmpty())
		bfProject = mCompiler->mResolvePassData->mParsers[0]->mProject;
	return bfProject;
}

bool BfAutoComplete::WantsEntries()
{
	if (mModule == NULL)
		return false;

	return (mResolveType == BfResolveType_Autocomplete) ||
		(mResolveType == BfResolveType_Autocomplete_HighPri) ||
		(mResolveType == BfResolveType_GetSymbolInfo) ||
		(mResolveType == BfResolveType_GoToDefinition) ||
		(mResolveType == BfResolveType_VerifyTypeName);
}

void BfAutoComplete::AddTopLevelNamespaces(BfAstNode* identifierNode)
{
	String filter;
	if (identifierNode != NULL)
	{
		filter = identifierNode->ToString();
		mInsertStartIdx = identifierNode->GetSrcStart();
		mInsertEndIdx = identifierNode->GetSrcEnd();
	}

	BfProject* bfProject = GetActiveProject();

	auto _AddProjectNamespaces = [&](BfProject* project)
	{
		for (auto namespacePair : project->mNamespaces)
		{
			const BfAtomComposite& namespaceComposite = namespacePair.mKey;
			if (namespaceComposite.GetPartsCount() == 1)
			{
				AddEntry(AutoCompleteEntry("namespace", namespaceComposite.ToString()), filter);
			}
		}
	};

	if (bfProject != NULL)
	{
		for (int depIdx = -1; depIdx < (int) bfProject->mDependencies.size(); depIdx++)
		{
			BfProject* depProject = (depIdx == -1) ? bfProject : bfProject->mDependencies[depIdx];
			_AddProjectNamespaces(depProject);
		}
	}
	else
	{
		for (auto project : mSystem->mProjects)
			_AddProjectNamespaces(project);
	}
}

void BfAutoComplete::AddTopLevelTypes(BfAstNode* identifierNode, bool onlyAttribute)
{
	String filter;

	if (identifierNode != NULL)
	{
		filter = identifierNode->ToString();
		mInsertStartIdx = identifierNode->GetSrcStart();
		mInsertEndIdx = identifierNode->GetSrcEnd();
	}

	AddEntry(AutoCompleteEntry("token", "function"), filter);
	AddEntry(AutoCompleteEntry("token", "delegate"), filter);
	AddEntry(AutoCompleteEntry("token", "decltype"), filter);

	if (mModule->mCurTypeInstance != NULL)
	{
		if (!onlyAttribute)
		{
			auto activeTypeDef = mModule->GetActiveTypeDef();
			for (auto genericParam : activeTypeDef->mGenericParamDefs)
				AddEntry(AutoCompleteEntry("generic", genericParam->mName), filter);
		}

		AddCurrentTypes(mModule->mCurTypeInstance, filter, true, true, onlyAttribute);
	}

	if (mModule->mCurMethodInstance != NULL)
	{
		if (!onlyAttribute)
		{
			for (auto genericParam : mModule->mCurMethodInstance->mMethodDef->mGenericParams)
				AddEntry(AutoCompleteEntry("generic", genericParam->mName), filter);
		}
	}

	if (!onlyAttribute)
	{
		BfTypeDef* showTypeDef = NULL;
		for (auto& systemTypeDefEntry : mModule->mSystem->mSystemTypeDefs)
		{
			auto systemTypeDef = systemTypeDefEntry.mValue;
			if ((systemTypeDef->mTypeCode == BfTypeCode_IntUnknown) || (systemTypeDef->mTypeCode == BfTypeCode_UIntUnknown))
				continue;
			if ((AddEntry(AutoCompleteEntry("valuetype", systemTypeDef->mName->mString.mPtr), filter)) && (mIsGetDefinition))
				showTypeDef = systemTypeDef;
		}
		AddEntry(AutoCompleteEntry("valuetype", "SelfBase"), filter);
		AddEntry(AutoCompleteEntry("valuetype", "SelfOuter"), filter);

		if (showTypeDef != NULL)
		{
			auto showType = mModule->ResolveTypeDef(showTypeDef);
			BfTypeInstance* showTypeInst = NULL;
			if (showType->IsPrimitiveType())
				showTypeInst = mModule->GetWrappedStructType(showType);
			else
				showTypeInst = showType->ToTypeInstance();
			if (showTypeInst != NULL)
				SetDefinitionLocation(showTypeInst->mTypeDef->mTypeDeclaration->mNameNode);
		}
	}

	auto activeTypeDef = mModule->GetActiveTypeDef();
	if (activeTypeDef != NULL)
	{
		BfProject* curProject = activeTypeDef->mProject;

		if (mModule->mCurTypeInstance != NULL)
		{
			for (auto innerTypeDef : mModule->mCurTypeInstance->mTypeDef->mNestedTypes)
			{
				if (!mModule->mCurTypeInstance->IsTypeMemberAccessible(innerTypeDef, activeTypeDef))
					continue;
				AddTypeDef(innerTypeDef, filter, onlyAttribute);
			}
		}

		auto& namespaceSearch = activeTypeDef->mNamespaceSearch;
		String prevName;
		for (auto typeDef : mModule->mSystem->mTypeDefs)
		{
			if (typeDef->mIsPartial)
				continue;
			//TODO :Check protection
			if ((curProject != NULL) && (curProject->ContainsReference(typeDef->mProject)))
			{
				bool matches = false;
				if (typeDef->mOuterType == NULL)
				{
					if (((typeDef->mNamespace.IsEmpty()) ||
						(namespaceSearch.Contains(typeDef->mNamespace))))
						matches = true;
				}

				if (matches)
				{
					AddTypeDef(typeDef, filter, onlyAttribute);
				}
			}
		}
	}
	else
	{
		BfProject* curProject = NULL;
		if (!mModule->mCompiler->mResolvePassData->mParsers.IsEmpty())
			curProject = mModule->mCompiler->mResolvePassData->mParsers[0]->mProject;

		String prevName;
		for (auto typeDef : mModule->mSystem->mTypeDefs)
		{
			if (typeDef->mIsPartial)
				continue;
			//TODO :Check protection
			if ((curProject != NULL) && (curProject->ContainsReference(typeDef->mProject)))
			{
				bool matches = false;
				if (typeDef->mOuterType == NULL)
				{
					if (typeDef->mNamespace.IsEmpty())
						matches = true;
				}

				if (matches)
				{
					AddTypeDef(typeDef, filter, onlyAttribute);
				}
			}
		}
	}
}

void BfAutoComplete::CheckIdentifier(BfAstNode* identifierNode, bool isInExpression, bool isUsingDirective)
{
	if ((identifierNode != NULL) && (!IsAutocompleteNode(identifierNode)))
		return;

 	mIdentifierUsed = identifierNode;

	if ((mModule->mParentNodeEntry != NULL) && (mModule->mCurMethodState != NULL))
	{
		if (auto binExpr = BfNodeDynCast<BfBinaryOperatorExpression>(mModule->mParentNodeEntry->mNode))
		{
			auto parentBlock = mModule->mCurMethodState->mCurScope->mAstBlock;
			if ((identifierNode == binExpr->mRight) && (binExpr->mOp == BfBinaryOp_Multiply) && (parentBlock != NULL))
			{
				// If we are the last identifier in a block then we MAY be a partially-typed variable declaration
				if (parentBlock->mChildArr.back() == binExpr)
				{
					mUncertain = true;
				}
			}
		}
	}

	if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(identifierNode))
	{
		CheckMemberReference(qualifiedNameNode->mLeft, qualifiedNameNode->mDot, qualifiedNameNode->mRight, false, NULL, isUsingDirective);
		return;
	}

	AddTopLevelNamespaces(identifierNode);
	if (isUsingDirective)
		return; // Only do namespaces

	AddTopLevelTypes(identifierNode);

	String filter;
	if (identifierNode != NULL)
	{
		filter = identifierNode->ToString();
		mInsertStartIdx = identifierNode->GetSrcStart();
		mInsertEndIdx = identifierNode->GetSrcEnd();
	}

	String addStr;

	if (mShowAttributeProperties != NULL)
	{
		auto showAttrTypeDef = mShowAttributeProperties->mTypeDef;
		for (auto prop : showAttrTypeDef->mProperties)
		{
			if (auto entryAdded = AddEntry(AutoCompleteEntry("property", prop->mName + "="), filter))
			{
				auto propertyDeclaration = prop->GetFieldDeclaration();
				if (propertyDeclaration != NULL)
				{
					if (CheckDocumentation(entryAdded, propertyDeclaration->mDocumentation))
					{
					}
					if (mIsGetDefinition)
						SetDefinitionLocation(propertyDeclaration->mNameNode);
				}
			}
		}

		for (auto field : showAttrTypeDef->mFields)
		{
			if (auto entryAdded = AddEntry(AutoCompleteEntry("field", field->mName + "="), filter))
			{
				auto fieldDeclaration = field->GetFieldDeclaration();
				if (fieldDeclaration != NULL)
				{
					if (CheckDocumentation(entryAdded, fieldDeclaration->mDocumentation))
					{
					}
					if (mIsGetDefinition)
						SetDefinitionLocation(fieldDeclaration->mNameNode);
				}
			}
		}
	}

	if ((mModule->mContext->mCurTypeState != NULL) && (mModule->mContext->mCurTypeState->mType != NULL))
	{
		BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mType);

		BfGlobalLookup globalLookup;
		globalLookup.mKind = BfGlobalLookup::Kind_All;
		mModule->PopulateGlobalContainersList(globalLookup);
		for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
		{
			if (globalContainer.mTypeInst != NULL)
				AddTypeMembers(globalContainer.mTypeInst, true, false, filter, globalContainer.mTypeInst, true, true, false);
		}
	}

	BfStaticSearch* staticSearch = mModule->GetStaticSearch();
	if (staticSearch != NULL)
	{
		for (auto typeInst : staticSearch->mStaticTypes)
		{
			AddTypeMembers(typeInst, true, false, filter, typeInst, true, true, false);
			AddInnerTypes(typeInst, filter, typeInst, false, false);
		}
	}

	if (auto ceDbgState = mModule->GetCeDbgState())
	{
		auto ceDebugger = mModule->mCompiler->mCeMachine->mDebugger;
		auto ceContext = ceDebugger->mCurDbgState->mCeContext;
		auto activeFrame = ceDebugger->mCurDbgState->mActiveFrame;
		if (activeFrame->mFunction->mDbgInfo != NULL)
		{
			int instIdx = activeFrame->GetInstIdx();
			for (auto& dbgVar : activeFrame->mFunction->mDbgInfo->mVariables)
			{
				if ((instIdx >= dbgVar.mStartCodePos) && (instIdx < dbgVar.mEndCodePos))
					AddEntry(AutoCompleteEntry(GetTypeName(dbgVar.mType), dbgVar.mName), filter);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////

	BfMethodInstance* curMethodInstance = mModule->mCurMethodInstance;
	if (mModule->mCurMethodState != NULL)
		curMethodInstance = mModule->mCurMethodState->GetRootMethodState()->mMethodInstance;
	if (curMethodInstance != NULL)
	{
		if (!curMethodInstance->mMethodDef->mIsStatic)
		{
			if (mModule->mCurTypeInstance->IsObject())
				AddEntry(AutoCompleteEntry("object", "this"), filter);
			else
				AddEntry(AutoCompleteEntry("pointer", "this"), filter);
			AddTypeMembers(mModule->mCurTypeInstance, true, true, filter, mModule->mCurTypeInstance, mModule->mCurTypeInstance->IsInterface(), true, true);
		}
		else
		{
			AddTypeMembers(mModule->mCurTypeInstance, true, false, filter, mModule->mCurTypeInstance, mModule->mCurTypeInstance->IsInterface(), true, true);
		}

		if (mModule->mCurMethodState != NULL)
		{
			int varSkipCount = 0;
			StringT<128> wantName = filter;
			while (wantName.StartsWith("@"))
			{
				varSkipCount++;
				wantName.Remove(0);
			}

			if (varSkipCount > 0)
			{
				Dictionary<String, int> localCount;

				auto varMethodState = mModule->mCurMethodState;
				while (varMethodState != NULL)
				{
					for (int localIdx = (int)varMethodState->mLocals.size() - 1; localIdx >= 0; localIdx--)
					{
						auto local = varMethodState->mLocals[localIdx];

						int* findIdx = NULL;
						if (localCount.TryAdd(local->mName, NULL, &findIdx))
						{
							*findIdx = 0;
						}
						else
						{
							(*findIdx)++;
						}

						if (varSkipCount - local->mNamePrefixCount - *findIdx == 0)
						{
							if ((AddEntry(AutoCompleteEntry(GetTypeName(local->mResolvedType), local->mName, varSkipCount - local->mNamePrefixCount - *findIdx), wantName)) && (mIsGetDefinition))
							{
							}
						}
					}

					varMethodState = varMethodState->mPrevMethodState;
					if ((varMethodState == NULL) ||
						(varMethodState->mMixinState != NULL) ||
						((varMethodState->mClosureState != NULL) && (!varMethodState->mClosureState->mCapturing)))
						break;
				}

				mInsertStartIdx += varSkipCount;
			}
			else
			{
				auto varMethodState = mModule->mCurMethodState;
				while (varMethodState != NULL)
				{
					for (auto& local : varMethodState->mLocals)
					{
						if ((AddEntry(AutoCompleteEntry(GetTypeName(local->mResolvedType), local->mName), wantName)) && (mIsGetDefinition))
						{
						}
					}

					varMethodState = varMethodState->mPrevMethodState;
					if ((varMethodState == NULL) ||
						(varMethodState->mMixinState != NULL) ||
						((varMethodState->mClosureState != NULL) && (!varMethodState->mClosureState->mCapturing)))
						break;
				}
			}
		}
	}
	else if (mModule->mCurTypeInstance != NULL)
	{
		bool staticOnly = true;
		if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mTempKind == BfMethodState::TempKind_NonStatic))
			staticOnly = false;

		//BF_ASSERT(mModule->mCurTypeInstance->mResolvingConstField);
		AddTypeMembers(mModule->mCurTypeInstance, true, !staticOnly, filter, mModule->mCurTypeInstance, false, true, true);
	}

	auto checkMethodState = mModule->mCurMethodState;
	while (checkMethodState != NULL)
	{
		for (auto localMethod : checkMethodState->mLocalMethods)
		{
			if (localMethod->mMethodInstanceGroup != NULL)
				AddMethod(mModule->mCurTypeInstance, localMethod->mMethodDef, localMethod->mMethodInstanceGroup->mDefault, localMethod->mMethodDeclaration, localMethod->mMethodName, filter);
		}
		checkMethodState = checkMethodState->mPrevMethodState;
	}

	if (isInExpression)
	{
		const char* tokens [] =
		{
			"alignof", "append", "as", "asm", "base", "break", "case", "catch", "checked", "continue", "const", "default", "defer",
			"delegate", "delete", "do", "else", "false", "finally",
			"fixed", "for", "function", "if", "implicit", "in", "internal", "is", "isconst", "new", "mixin", "null",
			"offsetof", "out", "params", "readonly", "ref", "rettype", "return",
			"sealed", "sizeof", "scope", "static", "strideof", "struct", "switch", /*"this",*/ "try", "true", "typeof", "unchecked",
			"using", "var", "virtual", "volatile", "where", "while",
			"alloctype", "comptype", "decltype", "nullable",
		};

		for (int i = 0; i < sizeof(tokens) / sizeof(char*); i++)
			AddEntry(AutoCompleteEntry("token", tokens[i]), filter);

		if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mBreakData != NULL) && (mModule->mCurMethodState->mBreakData->mIRFallthroughBlock))
		{
			AddEntry(AutoCompleteEntry("token", "fallthrough"), filter);
		}
	}
	else
	{
		const char* tokens[] =
		{
			"abstract", "append", "base", "class", "const",
			"delegate", "extern", "enum", "explicit", "extension", "function",
			"interface", "in", "implicit", "internal", "mixin", "namespace", "new",
			"operator", "out", "override", "params", "private", "protected", "public", "readonly", "ref", "rettype", "return",
			"scope", "sealed", "static", "struct", "this", "typealias",
			"using", "virtual", "volatile", "T", "where"
		};
		for (int i = 0; i < sizeof(tokens)/sizeof(char*); i++)
			AddEntry(AutoCompleteEntry("token", tokens[i]), filter);
	}

	//if ((identifierNode != NULL) && ((mModule->mCurMethodInstance == NULL) || (BfNodeDynCast<BfExpression>(identifierNode->mParent) != NULL)))
	/*if ((identifierNode != NULL) && ((mModule->mCurMethodInstance == NULL) || (isInExpression)))
	{
		AddEntry(AutoCompleteEntry("token", "#if"), filter);
		AddEntry(AutoCompleteEntry("token", "#elif"), filter);
		AddEntry(AutoCompleteEntry("token", "#endif"), filter);
	}*/

	//OutputDebugStrF("Autocomplete: %s\n", str.c_str());
}

String BfAutoComplete::GetFilter(BfAstNode* node)
{
	String filter = node->ToString();

	if (mIsGetDefinition)
	{
		mInsertEndIdx = node->GetSrcEnd();
	}
	else
	{
		// Only use member name up to cursor
		auto bfParser = node->GetSourceData()->ToParser();
		int cursorIdx = bfParser->mCursorIdx;
		filter = filter.Substring(0, BF_CLAMP(cursorIdx - node->GetSrcStart(), 0, (int)filter.length()));
		mInsertEndIdx = cursorIdx;
	}

	const char* cPtr = filter.c_str();
	while (cPtr[0] == '@')
	{
		mInsertStartIdx++;
		cPtr++;
	}

	return filter;
}

bool BfAutoComplete::CheckMemberReference(BfAstNode* target, BfAstNode* dotToken, BfAstNode* memberName, bool onlyShowTypes, BfType* expectingType, bool isUsingDirective, bool onlyAttribute)
{
	if (!WantsEntries())
		return false;

	BfAttributedIdentifierNode* attrIdentifier = NULL;
	bool isAutocompletingName = false;
	if ((attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(memberName)))
	{
		memberName = attrIdentifier->mIdentifier;
		if (IsAutocompleteNode(attrIdentifier->mAttributes))
		{
			auto bfParser = attrIdentifier->mAttributes->GetSourceData()->ToParser();
			int cursorIdx = bfParser->mCursorIdx;
			if (cursorIdx == attrIdentifier->mAttributes->GetSrcEnd())
				isAutocompletingName = true;
			else
				return false;
		}
	}

	if (memberName != NULL)
		isAutocompletingName = IsAutocompleteNode(dotToken, memberName, 0, 1);

	if ((IsAutocompleteNode(dotToken, 0, 1)) || (isAutocompletingName))
	{
		BfLogSys(mModule->mSystem, "Triggered autocomplete\n");

		bool isFriend = false;

		mInsertStartIdx = dotToken->GetSrcEnd();
		if (attrIdentifier != NULL)
		{
			BfAttributeState attributeState;
			attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_MemberAccess | BfAttributeTargets_Invocation);
			attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifier->mAttributes, attributeState.mTarget);
			if ((attributeState.mCustomAttributes != NULL) && (attributeState.mCustomAttributes->Contains(mModule->mCompiler->mFriendAttributeTypeDef)))
			{
				isFriend = true;
				attributeState.mUsed = true;
			}

			mInsertStartIdx = attrIdentifier->mAttributes->GetSrcEnd();
		}

		if (memberName != NULL)
		{
			//Member name MAY be incorrectly identified in cases like:
			// val._
			// OtherCall();
			int cursorIdx = GetCursorIdx(memberName);
			if ((cursorIdx != -1) && (cursorIdx >= memberName->GetSrcStart()))
				mInsertStartIdx = memberName->GetSrcStart();
		}

		SetAndRestoreValue<bool> prevFriendSet(mHasFriendSet, mHasFriendSet || isFriend);

		String filter;
		if ((memberName != NULL) && (IsAutocompleteNode(memberName)))
		{
			filter = GetFilter(memberName);
		}
		else if (mResolveType != BfResolveType_Autocomplete)
			mInsertStartIdx = -1; // Require a full span for everything but autocomplete

		SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);

		bool isStatic = false;
		BfTypedValue targetValue = LookupTypeRefOrIdentifier(target, &isStatic, (BfEvalExprFlags)(BfEvalExprFlags_IgnoreNullConditional | BfEvalExprFlags_NoCast), expectingType);
		if ((targetValue) && (dotToken->mToken == BfToken_Arrow))
		{
			SetAndRestoreValue<bool> prevIgnoreClassifying(mModule->mIsInsideAutoComplete, true);
			BfExprEvaluator exprEvaluator(mModule);
			auto arrowValue = exprEvaluator.PerformUnaryOperation_TryOperator(targetValue, NULL, BfUnaryOp_Arrow, BfNodeDynCast<BfTokenNode>(dotToken), BfUnaryOpFlag_None);
			if (arrowValue)
				targetValue = arrowValue;
		}

		bool hadResults = false;
		bool doAsNamespace = true;
		if ((targetValue.mType) && (!isUsingDirective))
		{
			doAsNamespace = false;
			if (auto dotTokenNode = BfNodeDynCast<BfTokenNode>(dotToken))
			{
				if (dotTokenNode->GetToken() == BfToken_QuestionDot)
				{
					if (!targetValue.mType->IsNullable())
					{
						// We need this for Result<T>
						SetAndRestoreValue<bool> prevIgnore(mModule->mBfIRBuilder->mIgnoreWrites, true);
						BfExprEvaluator exprEvaluator(mModule);
						auto opResult = exprEvaluator.PerformUnaryOperation_TryOperator(targetValue, NULL, BfUnaryOp_NullConditional, dotTokenNode, BfUnaryOpFlag_None);
						if (opResult)
							targetValue = opResult;
					}

					// ?. should look inside nullable types
					if (targetValue.mType->IsNullable())
					{
						BfTypeInstance* nullableType = (BfTypeInstance*)targetValue.mType->ToTypeInstance();
						targetValue = mModule->MakeAddressable(targetValue);
						BfIRValue valuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(targetValue.mValue, 0, 1); // mValue
						targetValue = BfTypedValue(valuePtr, nullableType->mGenericTypeInfo->mTypeGenericArguments[0], true);
					}
				}
			}

			// Statics, inner types

			auto checkType = targetValue.mType;
			if (checkType->IsConcreteInterfaceType())
				checkType = checkType->GetUnderlyingType();

			if (checkType->IsGenericParam())
			{
				auto genericParamType = (BfGenericParamType*)checkType;
				auto genericParamInstance = mModule->GetGenericParamInstance(genericParamType);

				auto _HandleGenericParamInstance = [&](BfGenericParamInstance* genericParamInstance)
				{
					bool showStatics = !targetValue.mValue;

					for (auto interfaceConstraint : genericParamInstance->mInterfaceConstraints)
						AddTypeMembers(interfaceConstraint, showStatics, !showStatics, filter, interfaceConstraint, true, false, false);

					if (genericParamInstance->mTypeConstraint != NULL)
						checkType = genericParamInstance->mTypeConstraint;
					else
						checkType = mModule->mContext->mBfObjectType;

					if ((genericParamInstance->IsEnum()))
					{
						if (isStatic)
							AddEntry(AutoCompleteEntry("valuetype", "UnderlyingType"), filter);
						else
						{
							AddEntry(AutoCompleteEntry("value", "Underlying"), filter);
							AddEntry(AutoCompleteEntry("value", "UnderlyingRef"), filter);
						}
					}
				};
				_HandleGenericParamInstance(genericParamInstance);

				// Check method generic constraints
				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (int genericParamIdx = (int)mModule->mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
						genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
						if (genericParam->mExternType == genericParamType)
							_HandleGenericParamInstance(genericParam);
					}
				}
			}

			if (checkType->IsPointer())
			{
				auto underlyingType = checkType->GetUnderlyingType();
				if ((underlyingType != NULL) && (underlyingType->IsValueType()))
					checkType = underlyingType;
			}
			auto typeInst = checkType->ToTypeInstance();
			if ((typeInst == NULL) &&
				((checkType->IsPrimitiveType()) || (checkType->IsSizedArray())))
				typeInst = mModule->GetWrappedStructType(checkType);

			if (typeInst != NULL)
			{
				if (typeInst->mTypeDef->IsGlobalsContainer())
					doAsNamespace = true; // Also list the types in this namespace

 				bool allowPrivate = (mModule->mCurTypeInstance == typeInst) || (mModule->IsInnerType(mModule->mCurTypeInstance, typeInst));
 				bool allowProtected = allowPrivate;

				if (isStatic)
					AddInnerTypes(typeInst, filter, typeInst, allowProtected, allowPrivate);

				if (!onlyShowTypes)
				{
					AddTypeMembers(typeInst, isStatic, !isStatic, filter, typeInst, false, false, false);

					if (!isStatic)
					{
						auto checkTypeInst = mModule->mCurTypeInstance;
						while (checkTypeInst != NULL)
						{
							AddExtensionMethods(typeInst, checkTypeInst, filter, true, true);
							checkTypeInst = mModule->GetOuterType(checkTypeInst);
						}

						if ((mModule->mContext->mCurTypeState != NULL) && (mModule->mContext->mCurTypeState->mType != NULL))
						{
							BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mType);

							BfGlobalLookup globalLookup;
							globalLookup.mKind = BfGlobalLookup::Kind_All;
							mModule->PopulateGlobalContainersList(globalLookup);
							for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
								if (globalContainer.mTypeInst != NULL)
									AddExtensionMethods(typeInst, globalContainer.mTypeInst, filter, false, false);
						}

						BfStaticSearch* staticSearch = mModule->GetStaticSearch();
						if (staticSearch != NULL)
						{
							for (auto staticTypeInst : staticSearch->mStaticTypes)
								AddExtensionMethods(typeInst, staticTypeInst, filter, false, false);
						}
					}
				}

				if (typeInst->IsInterface())
				{
					AddTypeMembers(mModule->mContext->mBfObjectType, isStatic, !isStatic, filter, mModule->mContext->mBfObjectType, true, false, false);
				}
			}

			hadResults = true;
		}

		if (doAsNamespace) // Lookup namespaces
		{
			String targetStr = target->ToString();
			BfAtomComposite targetComposite;
			bool isValid = mSystem->ParseAtomComposite(targetStr, targetComposite);

			BfProject* bfProject = GetActiveProject();
			auto _CheckProject = [&](BfProject* project)
			{
				if ((isValid) && (project->mNamespaces.ContainsKey(targetComposite)))
				{
					for (auto namespacePair : project->mNamespaces)
					{
						const BfAtomComposite& namespaceComposite = namespacePair.mKey;
						if ((namespaceComposite.StartsWith(targetComposite)) && (namespaceComposite.GetPartsCount() > targetComposite.GetPartsCount()))
						{
							BfAtom* subNamespace = namespaceComposite.mParts[targetComposite.mSize];
							AutoCompleteEntry entry("namespace", subNamespace->mString.mPtr);
							AddEntry(entry, filter);
						}
					}

					if (!isUsingDirective)
					{
						BfProject* activeProject = GetActiveProject();
						for (auto typeDef : mSystem->mTypeDefs)
						{
							if ((typeDef->mNamespace == targetComposite) && (typeDef->mOuterType == NULL) &&
								(!typeDef->mIsPartial) &&
								((activeProject == NULL) || (activeProject->ContainsReference(typeDef->mProject))))
							{
								AddTypeDef(typeDef, filter, onlyAttribute);
							}
						}
					}

					hadResults = true;
				}
			};

			if (bfProject != NULL)
			{
				for (int depIdx = -1; depIdx < (int)bfProject->mDependencies.size(); depIdx++)
				{
					BfProject* depProject = (depIdx == -1) ? bfProject : bfProject->mDependencies[depIdx];
					_CheckProject(depProject);
				}
			}
			else
			{
				for (auto project : mSystem->mProjects)
					_CheckProject(project);
			}
		}

		return hadResults;
	}
	else
	{
		auto identifierNode = BfNodeDynCast<BfIdentifierNode>(target);
		if (identifierNode != NULL)
			CheckIdentifier(identifierNode);
		CheckTypeRef(BfNodeDynCast<BfTypeReference>(target), true, false, onlyAttribute);
	}

	return false;
}

bool BfAutoComplete::CheckExplicitInterface(BfTypeInstance* interfaceType, BfAstNode* dotToken, BfAstNode* memberName)
{
	bool isAutocompletingName = false;
	if (memberName != NULL)
		isAutocompletingName = IsAutocompleteNode(dotToken, memberName, 0, 1);

	if (isAutocompletingName)
	{
		//
	}
	else if (IsAutocompleteNode(dotToken, 0, 1))
	{
		mInsertStartIdx = dotToken->GetSrcEnd();
		mInsertEndIdx = mInsertStartIdx;
	}
	else
		return false;

	mModule->PopulateType(interfaceType, BfPopulateType_DataAndMethods);

	String filter;
	if (isAutocompletingName)
		filter = GetFilter(memberName);

	auto activeTypeDef = mModule->GetActiveTypeDef();

	BfShow checkShow = BfShow_Hide;

	for (auto methodDef : interfaceType->mTypeDef->mMethods)
	{
		if (methodDef->mIsOverride)
			continue;
		if (methodDef->mShow >= checkShow)
			continue;
		if (methodDef->mName.IsEmpty())
			continue;

		if (methodDef->mExplicitInterface != NULL)
			continue;
		if ((!interfaceType->IsTypeMemberIncluded(methodDef->mDeclaringType, activeTypeDef, mModule)) ||
			(!interfaceType->IsTypeMemberAccessible(methodDef->mDeclaringType, activeTypeDef)))
			continue;

		if (methodDef->mIsStatic)
			continue;

		bool canUseMethod;
		canUseMethod = (methodDef->mMethodType == BfMethodType_Normal);
		if (canUseMethod)
		{
			AddMethod(interfaceType, methodDef, NULL, methodDef->GetMethodDeclaration(), methodDef->mName, filter);
		}
	}

	return false;
}

void BfAutoComplete::CheckTypeRef(BfTypeReference* typeRef, bool mayBeIdentifier, bool isInExpression, bool onlyAttribute)
{
	if ((typeRef == NULL) || (typeRef->IsTemporary()) || (!IsAutocompleteNode(typeRef)))
		return;

	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		CheckTypeRef(genericTypeRef->mElementType, mayBeIdentifier, isInExpression, onlyAttribute);
		for (auto genericArg : genericTypeRef->mGenericArguments)
			CheckNode(genericArg, false, isInExpression);
		return;
	}

	if (!onlyAttribute)
	{
		if (auto tupleTypeRef = BfNodeDynCast<BfTupleTypeRef>(typeRef))
		{
			for (auto fieldTypeRef : tupleTypeRef->mFieldTypes)
				CheckTypeRef(fieldTypeRef, false, isInExpression, false);
			return;
		}

		if (auto delegateTypeRef = BfNodeDynCast<BfDelegateTypeRef>(typeRef))
		{
			CheckTypeRef(delegateTypeRef->mReturnType, false, isInExpression);
			for (auto param : delegateTypeRef->mParams)
			{
				auto attributes = param->mAttributes;
				while (attributes != NULL)
				{
					if (attributes->mAttributeTypeRef != NULL)
					{
						CheckAttributeTypeRef(attributes->mAttributeTypeRef);
					}

					attributes = attributes->mNextAttribute;
				}
				CheckTypeRef(param->mTypeRef, false, isInExpression);
			}
			return;
		}

		if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeRef))
		{
			// "May be identifier" where pointer types could actually end up be multiplies, etc.
			CheckTypeRef(elementedTypeRef->mElementType, true, isInExpression);
			return;
		}
	}

 	if ((mayBeIdentifier) && (mResolveType != BfResolveType_GoToDefinition))
	{
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
		{
			CheckIdentifier(namedTypeRef->mNameNode, isInExpression);
			return;
		}
		else if (auto varTypeRef = BfNodeDynCast<BfVarTypeReference>(typeRef))
		{
			CheckIdentifier(varTypeRef->mVarToken, isInExpression);
			return;
		}
		else if (auto varTypeRef = BfNodeDynCast<BfLetTypeReference>(typeRef))
		{
			CheckIdentifier(varTypeRef->mLetToken, isInExpression);
			return;
		}
	}

	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		// Only consider the left side as an identifier if there's space after the dot. Consider this:
		//   mVal.
		//   Type a = null;
		// vs
		//   mVal.Type a = null;
		// The first one is clearly a member reference being typed out even though it looks the same
		//  to the parser except for the spacing
		if ((qualifiedTypeRef->mRight == NULL) || (qualifiedTypeRef->mDot->GetSrcEnd() < qualifiedTypeRef->mRight->GetSrcStart()))
		{
			BfAutoParentNodeEntry autoParentNodeEntry(mModule, qualifiedTypeRef);
			//CheckMemberReference(qualifiedTypeRef->mLeft, qualifiedTypeRef->mDot, NULL, !mayBeIdentifier, NULL, false, onlyAttribute);
			CheckMemberReference(qualifiedTypeRef->mLeft, qualifiedTypeRef->mDot, qualifiedTypeRef->mRight, !mayBeIdentifier, NULL, false, onlyAttribute);
		}
		else if (auto rightNamedTypeRef = BfNodeDynCast<BfNamedTypeReference>(qualifiedTypeRef->mRight))
		{
			BfAutoParentNodeEntry autoParentNodeEntry(mModule, qualifiedTypeRef);
			if (CheckMemberReference(qualifiedTypeRef->mLeft, qualifiedTypeRef->mDot, rightNamedTypeRef->mNameNode, false, NULL, false, onlyAttribute))
				return;
		}
	}

	if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
	{
		AddTopLevelNamespaces(namedTypeRef->mNameNode);
		AddTopLevelTypes(namedTypeRef->mNameNode, onlyAttribute);
	}
}

void BfAutoComplete::CheckAttributeTypeRef(BfTypeReference* typeRef)
{
	if (!IsAutocompleteNode(typeRef))
		return;

	CheckTypeRef(typeRef, false, false, true);
}

void BfAutoComplete::CheckInvocation(BfAstNode* invocationNode, BfTokenNode* openParen, BfTokenNode* closeParen, const BfSizedArray<ASTREF(BfTokenNode*)>& commas)
{
	if (!mIsAutoComplete)
		return;

	bool wasCapturingMethodMatchInfo = mIsCapturingMethodMatchInfo;
	mIsCapturingMethodMatchInfo = false;

	int lenAdd = 0;
	if (closeParen == NULL)
	{
		// Unterminated invocation expression - allow for space after last comma in param list
		lenAdd = 1;
	}
	else
	{
		// Ignore close paren
		lenAdd = -1;
	}

	if (!IsAutocompleteNode(invocationNode, lenAdd))
		return;

	if (openParen == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	auto bfParser = invocationNode->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return;
	int cursorIdx = bfParser->mCursorIdx;

	BfAstNode* target = invocationNode;

	if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(invocationNode))
	{
		target = invocationExpr->mTarget;
		if (auto memberTarget = BfNodeDynCast<BfMemberReferenceExpression>(target))
		{
			if (memberTarget->mMemberName != NULL)
				target = memberTarget->mMemberName;
		}
		else if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(target))
		{
			if (qualifiedTypeRef->mRight != NULL)
				target = qualifiedTypeRef->mRight;
		}
		else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(target))
		{
			if (qualifiedNameNode->mRight != NULL)
				target = qualifiedNameNode->mRight;
		}

		if (auto attributedMember = BfNodeDynCast<BfAttributedIdentifierNode>(target))
			if (attributedMember->mIdentifier != NULL)
				target = attributedMember->mIdentifier;
	}

	bool doCapture = (bfParser->mCursorIdx >= openParen->GetSrcStart());
	if (mIsGetDefinition)
	{
		doCapture |= (target != NULL) && (bfParser->mCursorIdx >= target->GetSrcStart());
	}

	if (doCapture)
	{
		mIsCapturingMethodMatchInfo = true;

		delete mMethodMatchInfo;
		mMethodMatchInfo = new MethodMatchInfo();

		mMethodMatchInfo->mInvocationSrcIdx = target->GetSrcStart();
		mMethodMatchInfo->mCurMethodInstance = mModule->mCurMethodInstance;
		mMethodMatchInfo->mCurTypeInstance = mModule->mCurTypeInstance;

		mMethodMatchInfo->mSrcPositions.Clear();
		mMethodMatchInfo->mSrcPositions.push_back(openParen->GetSrcStart());
		for (auto comma : commas)
			mMethodMatchInfo->mSrcPositions.push_back(comma->GetSrcStart());
		mMethodMatchInfo->mSrcPositions.push_back(invocationNode->GetSrcEnd() + lenAdd);
	}
}

void BfAutoComplete::CheckNode(BfAstNode* node, bool mayBeIdentifier, bool isInExpression)
{
	if (!IsAutocompleteNode(node))
		return;

	if (auto identifer = BfNodeDynCast<BfIdentifierNode>(node))
		CheckIdentifier(identifer);
	if (auto typeRef = BfNodeDynCast<BfTypeReference>(node))
		CheckTypeRef(typeRef, mayBeIdentifier, isInExpression);
	if (auto memberRef = BfNodeDynCast<BfMemberReferenceExpression>(node))
	{
		if (memberRef->mTarget != NULL)
			CheckMemberReference(memberRef->mTarget, memberRef->mDotToken, memberRef->mMemberName);
	}
}

bool BfAutoComplete::GetMethodInfo(BfMethodInstance* methodInst, StringImpl* showString, StringImpl* insertString, bool isImplementing, bool isExplicitInterface)
{
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mModule->mCurMethodInstance, methodInst);

	auto methodDef = methodInst->mMethodDef;
	bool isInterface = methodInst->GetOwner()->IsInterface();

	BfTypeNameFlags nameFlags = (BfTypeNameFlags)(BfTypeNameFlag_ReduceName | BfTypeNameFlag_ResolveGenericParamNames);

	if (methodDef->mMethodType == BfMethodType_Normal)
	{
		StringT<128> methodPrefix;
		StringT<128> methodName;
		StringT<256> impString;

		bool isAbstract = (methodDef->mIsAbstract) || (isInterface) || (!methodDef->mIsVirtual);

		if (isAbstract)
		{
			if (!methodInst->mReturnType->IsVoid())
				impString += "return default;";
		}
		else if (!isAbstract)
		{
			if (!methodInst->mReturnType->IsVoid())
				impString = "return ";

			impString += "base.";
			impString += methodDef->mName;
			impString += "(";
		}

		auto methodDeclaration = methodDef->GetMethodDeclaration();

		if (isInterface)
		{
			if (!isExplicitInterface)
				methodPrefix += "public ";
		}
		else if (methodDeclaration->mProtectionSpecifier != NULL)
			methodPrefix += methodDeclaration->mProtectionSpecifier->ToString() + " ";
		if (!isInterface)
			methodPrefix += "override ";
		if (methodDef->mIsStatic)
			methodPrefix += "static ";

		methodPrefix += mModule->TypeToString(methodInst->mReturnType, nameFlags);
		methodPrefix += " ";
		if (isExplicitInterface)
		{
			methodName += mModule->TypeToString(methodInst->GetOwner(), nameFlags);
			methodName += ".";
		}

		methodName += methodDef->mName;

		if (methodInst->GetNumGenericArguments() > 0)
		{
			methodName += "<";
			for (int genericArgIdx = 0; genericArgIdx < (int)methodInst->mMethodInfoEx->mGenericParams.size(); genericArgIdx++)
			{
				if (genericArgIdx > 0)
					methodName += ", ";

				auto genericParam = methodInst->mMethodInfoEx->mGenericParams[genericArgIdx];
				methodName += genericParam->GetName();
			}
			methodName += ">";
		}

		methodName += "(";
		for (int paramIdx = 0; paramIdx < (int)methodInst->GetParamCount(); paramIdx++)
		{
			if (paramIdx > 0)
			{
				methodName += ", ";
				if (!isAbstract)
					impString += ", ";
			}

			if (methodInst->GetParamKind(paramIdx) == BfParamKind_Params)
				methodName += "params ";

			methodName += mModule->TypeToString(methodInst->GetParamType(paramIdx), nameFlags);
			methodName += " ";
			methodName += methodDef->mParams[paramIdx]->mName;

			auto paramInitializer = methodInst->GetParamInitializer(paramIdx);
			if (paramInitializer != NULL)
			{
				methodName += " = ";
				paramInitializer->ToString(methodName);
			}

			if (!isAbstract)
			{
				if (methodInst->GetParamKind(paramIdx) == BfParamKind_Params)
					impString += "params ";

				impString += methodDef->mParams[paramIdx]->mName;
			}
		}
		methodName += ")";

		if (methodInst->GetNumGenericArguments() > 0)
		{
			for (int genericArgIdx = 0; genericArgIdx < (int)methodInst->mMethodInfoEx->mGenericParams.size(); genericArgIdx++)
			{
				auto genericParam = methodInst->mMethodInfoEx->mGenericParams[genericArgIdx];

				if (genericParam->mTypeConstraint != NULL)
					methodName += " where " + genericParam->GetName() + " : " + mModule->TypeToString(genericParam->mTypeConstraint, nameFlags);

				for (auto ifaceConstraint : genericParam->mInterfaceConstraints)
					methodName += " where " + genericParam->GetName() + " : " + mModule->TypeToString(ifaceConstraint, nameFlags);

				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Class) != 0)
					methodName += " where " + genericParam->GetName() + " : class";
				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Struct) != 0)
					methodName += " where " + genericParam->GetName() + " : struct";
				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_StructPtr) != 0)
					methodName += " where " + genericParam->GetName() + " : struct*";
				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_New) != 0)
					methodName += " where " + genericParam->GetName() + " : new";
				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Delete) != 0)
					methodName += " where " + genericParam->GetName() + " : delete";
				if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
					methodName += " where " + genericParam->GetName() + " : var";
			}
		}

		if (!isAbstract)
			impString += ");";

		if (showString != NULL)
			*showString += methodName;
		if (insertString != NULL)
		{
			if (showString == insertString)
				*insertString += "\t";

			*insertString += methodPrefix + methodName + "\t" + impString;
		}

		return true;
	}
	else if ((methodDef->mMethodType == BfMethodType_PropertyGetter) || (methodDef->mMethodType == BfMethodType_PropertySetter))
	{
		auto propDeclaration = methodDef->GetPropertyDeclaration();
		bool hasGet = propDeclaration->GetMethod("get") != NULL;
		bool hasSet = propDeclaration->GetMethod("set") != NULL;

		if ((methodDef->mMethodType == BfMethodType_PropertyGetter) || (!hasGet))
		{
			StringT<128> propName;
			StringT<256> impl;

			if (propDeclaration->mNameNode != NULL)
				propDeclaration->mNameNode->ToString(propName);
			else
			{
				StringT<128> args;

				propName += "this[";
				for (int paramIdx = 0, count = methodInst->GetParamCount(); paramIdx < count; ++paramIdx)
				{
					if (paramIdx > 0)
						args += ", ";

					args += mModule->TypeToString(methodInst->GetParamType(paramIdx), nameFlags);
					args += " ";
					args += methodDef->mParams[paramIdx]->mName;
				}
				propName += args;
				propName += "]";
			}

			bool isAbstract = methodDef->mIsAbstract;

			if (propDeclaration->mProtectionSpecifier != NULL)
				impl += propDeclaration->mProtectionSpecifier->ToString() + " ";
			else if (isInterface && !isExplicitInterface)
				impl += "public ";

			if (!isInterface)
				impl += "override ";
			else if (methodDef->mIsStatic)
				impl += "static ";

			BfType* propType = methodInst->mReturnType;
			if (methodDef->mMethodType == BfMethodType_PropertySetter)
				propType = methodInst->GetParamType(0);

			impl += mModule->TypeToString(propType, nameFlags);
			impl += " ";
			if (isExplicitInterface)
			{
				impl += mModule->TypeToString(methodInst->GetOwner(), nameFlags);
				impl += ".";
			}

			impl += propName;
			impl += "\t";
			if (hasGet)
			{
				impl += "get\t";
				if (!isAbstract)
				{
					if (isInterface)
					{
						impl += "return default;";
					}
					else
					{
						impl += "return base.";
						impl += propName;
						impl += ";";
					}
				}
				impl += "\b";
			}
			if (hasSet)
			{
 				if (hasGet)
 					impl += "\r\r";

				impl += "set\t";
				if (!isAbstract)
				{
					if (!isInterface)
					{
						impl += "base.";
						impl += propName;
						impl += " = value;";
					}
				}

				impl += "\b";
			}

			if (showString != NULL)
				*showString += propName;
			if (insertString != NULL)
			{
				if (showString == insertString)
					*insertString += "\t";
				*insertString += impl;
			}

			return true;
		}
	}

	return false;
}

void BfAutoComplete::AddOverrides(const StringImpl& filter)
{
	if (!mIsAutoComplete)
		return;

	auto activeTypeDef = mModule->GetActiveTypeDef();

	BfShow checkShow = BfShow_Hide;

	BfTypeInstance* curType = mModule->mCurTypeInstance;
	while (curType != NULL)
	{
		for (auto methodDef : curType->mTypeDef->mMethods)
		{
			if (methodDef->mShow >= checkShow)
				continue;

			bool allowInternalOverride = false;
			if (curType == mModule->mCurTypeInstance)
			{
				// The "normal" case, and only case for types without extensions
				if (methodDef->mDeclaringType == activeTypeDef)
					continue;

				if ((methodDef->mDeclaringType->IsExtension()) && (methodDef->mDeclaringType->mProject == activeTypeDef->mProject))
					continue;

				if (!curType->IsTypeMemberAccessible(methodDef->mDeclaringType, activeTypeDef))
					continue;

				if (methodDef->mIsExtern)
					allowInternalOverride = true;
			}

			auto& methodGroup = curType->mMethodInstanceGroups[methodDef->mIdx];
			if (methodGroup.mDefault == NULL)
			{
				continue;
			}
			auto methodInst = methodGroup.mDefault;

			if (allowInternalOverride)
			{
				//
			}
			else if ((!methodDef->mIsVirtual) || (methodDef->mIsOverride))
				continue;

			if ((methodDef->mMethodType != BfMethodType_Normal) &&
				(methodDef->mMethodType != BfMethodType_PropertyGetter) &&
				(methodDef->mMethodType != BfMethodType_PropertySetter))
				continue;

			if ((methodInst->mVirtualTableIdx >= 0) && (methodInst->mVirtualTableIdx < mModule->mCurTypeInstance->mVirtualMethodTable.size()))
			{
				auto& vEntry = mModule->mCurTypeInstance->mVirtualMethodTable[methodInst->mVirtualTableIdx];
				if (vEntry.mImplementingMethod.mTypeInstance == mModule->mCurTypeInstance)
					continue;
			}

			StringT<512> insertString;
			GetMethodInfo(methodInst, &insertString, &insertString, true, false);
			if (insertString.IsEmpty())
				continue;
			AddEntry(AutoCompleteEntry("override", insertString), filter);
		}

		if (curType->IsStruct())
			curType = mModule->mContext->mBfObjectType;
		else
			curType = curType->mBaseType;
	}
}

void BfAutoComplete::UpdateReplaceData()
{
}

void BfAutoComplete::CheckMethod(BfMethodDeclaration* methodDeclaration, bool isLocalMethod)
{
	if (/*(propertyDeclaration->mDefinitionBlock == NULL) &&*/ (methodDeclaration->mVirtualSpecifier != NULL) &&
		(methodDeclaration->mVirtualSpecifier->GetToken() == BfToken_Override))
	{
		auto bfParser = methodDeclaration->mVirtualSpecifier->GetSourceData()->ToParser();
		if (bfParser == NULL)
			return;
		int cursorIdx = bfParser->mCursorIdx;

		bool isInTypeRef = IsAutocompleteNode(methodDeclaration->mReturnType);
		bool isInNameNode = IsAutocompleteNode(methodDeclaration->mNameNode);

		if (((IsAutocompleteNode(methodDeclaration, 1)) && (cursorIdx == methodDeclaration->mVirtualSpecifier->GetSrcEnd())) ||
			(isInTypeRef) || (isInNameNode))
		{
			if (mIsAutoComplete)
			{
				mInsertStartIdx = methodDeclaration->GetSrcStart();
				mInsertEndIdx = methodDeclaration->GetSrcEnd();
				if (methodDeclaration->mBody != NULL)
				{
					if (methodDeclaration->mBody->mTriviaStart != -1)
						mInsertEndIdx = methodDeclaration->mBody->mTriviaStart;
					else
						mInsertEndIdx = methodDeclaration->mBody->GetSrcStart();
				}
			}

			String filter;
			if ((isInNameNode || isInTypeRef))
			{
				if (methodDeclaration->mNameNode != NULL)
					filter = methodDeclaration->mNameNode->ToString();
				else if (methodDeclaration->mReturnType != NULL)
					filter = methodDeclaration->mReturnType->ToString();
			}
			else if (methodDeclaration->mBody != NULL)
			{
				// We're just inside 'override' - we may be inserting a new method
				mInsertEndIdx = methodDeclaration->mVirtualSpecifier->GetSrcEnd();
			}

			AddOverrides(filter);
		}
	}

	if (methodDeclaration->mReturnType != NULL)
		CheckTypeRef(methodDeclaration->mReturnType, true, isLocalMethod);
}

void BfAutoComplete::CheckProperty(BfPropertyDeclaration* propertyDeclaration)
{
	if (IsAutocompleteNode(propertyDeclaration->mNameNode))
	{
		mInsertStartIdx = propertyDeclaration->mNameNode->GetSrcStart();
		mInsertEndIdx = propertyDeclaration->mNameNode->GetSrcEnd();
	}

	if (propertyDeclaration->mExplicitInterface != NULL)
	{
		BfTypeInstance* typeInst = NULL;

		auto type = mModule->ResolveTypeRef(propertyDeclaration->mExplicitInterface, BfPopulateType_Identity);
		if (type != NULL)
			typeInst = type->ToTypeInstance();

		if (typeInst != NULL)
			CheckExplicitInterface(typeInst, propertyDeclaration->mExplicitInterfaceDotToken, propertyDeclaration->mNameNode);
	}

	if ((propertyDeclaration->mVirtualSpecifier != NULL) &&
		(propertyDeclaration->mVirtualSpecifier->GetToken() == BfToken_Override))
	{
		if (!mIsAutoComplete)
			return;

		auto bfParser = propertyDeclaration->mVirtualSpecifier->GetSourceData()->ToParser();
		if (bfParser == NULL)
			return;
		int cursorIdx = bfParser->mCursorIdx;

		bool isInTypeRef = IsAutocompleteNode(propertyDeclaration->mTypeRef);
		bool isInNameNode = IsAutocompleteNode(propertyDeclaration->mNameNode);

		if (((IsAutocompleteNode(propertyDeclaration, 1)) && (cursorIdx == propertyDeclaration->mVirtualSpecifier->GetSrcEnd())) ||
			(isInTypeRef) || (isInNameNode))
		{
			mInsertStartIdx = propertyDeclaration->mVirtualSpecifier->GetSrcStart();

			String filter;
			if ((isInNameNode || isInTypeRef))
			{
				BfAstNode* defNode = NULL;
				if (isInNameNode)
					defNode = propertyDeclaration->mNameNode;
				else if (isInTypeRef)
					defNode = propertyDeclaration->mTypeRef;

				filter = defNode->ToString();
				mInsertEndIdx = defNode->GetSrcEnd();
			}
			else if (propertyDeclaration->mTypeRef != NULL)
			{
				// We're just inside 'override' - we may be inserting a new method
				mInsertEndIdx = propertyDeclaration->mVirtualSpecifier->GetSrcEnd();
			}
			else
			{
				mInsertEndIdx = propertyDeclaration->mVirtualSpecifier->GetSrcEnd();
			}
			AddOverrides(filter);
			mInsertStartIdx = propertyDeclaration->mSrcStart;
		}
	}
	else
	{
		if (propertyDeclaration->mTypeRef != NULL)
			CheckTypeRef(propertyDeclaration->mTypeRef, true);
	}
}

void BfAutoComplete::CheckVarResolution(BfAstNode* varTypeRef, BfType* resolvedType)
{
	if (IsAutocompleteNode(varTypeRef))
	{
		if ((resolvedType == NULL) || (resolvedType->IsVar()) || (resolvedType->IsLet()))
			return;

		if (mIsGetDefinition)
		{
			auto typeInst = resolvedType->ToTypeInstance();
			if (typeInst != NULL)
			{
				if (typeInst->mTypeDef->mTypeDeclaration != NULL)
					SetDefinitionLocation(typeInst->mTypeDef->mTypeDeclaration->mNameNode);
			}
		}

		if (mResolveType == BfResolveType_GetResultString)
		{
			mResultString = ":";
			mResultString += mModule->TypeToString(resolvedType, (BfTypeNameFlags)(BfTypeNameFlag_ExtendedInfo | BfTypeNameFlag_ResolveGenericParamNames));
		}
	}
}

void BfAutoComplete::CheckResult(BfAstNode* node, const BfTypedValue& typedValue)
{
	if (mResolveType != BfResolveType_GetResultString)
		return;

	if (!IsAutocompleteNode(node))
		return;

	if (!typedValue.mValue.IsConst())
		return;

	if (typedValue.mType->IsPointer())
		return;
	if (typedValue.mType->IsObject())
		return;

	auto constant = mModule->mBfIRBuilder->GetConstant(typedValue.mValue);
	if (BfIRConstHolder::IsInt(constant->mTypeCode))
	{
		mResultString = StrFormat(":%lld", constant->mInt64);
	}
	else if (BfIRConstHolder::IsFloat(constant->mTypeCode))
	{
		mResultString = StrFormat(":%f", constant->mDouble);
	}
}

void BfAutoComplete::CheckLocalDef(BfAstNode* identifierNode, BfLocalVariable* varDecl)
{
	CheckLocalRef(identifierNode, varDecl);
}

void BfAutoComplete::CheckLocalRef(BfAstNode* identifierNode, BfLocalVariable* varDecl)
{
	if (mReplaceLocalId != -1)
		return;

	if (mResolveType == BfResolveType_GoToDefinition)
	{
		if (IsAutocompleteNode(identifierNode))
		{
			if (varDecl->mNameNode != NULL)
				SetDefinitionLocation(varDecl->mNameNode, true);
			else if (varDecl->mIsThis)
				SetDefinitionLocation(mModule->mCurTypeInstance->mTypeDef->GetRefNode(), true);
		}
	}
	else if (mResolveType == BfResolveType_GetSymbolInfo)
	{
		if ((IsAutocompleteNode(identifierNode)) &&
			((!varDecl->mIsShadow) || (varDecl->mShadowedLocal != NULL)))
		{
			if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) &&
				(!mModule->mCurMethodState->mClosureState->mCapturing))
			{
				// For closures, only get locals during the 'capturing' stage
				return;
			}

			auto rootMethodInstance = mModule->mCurMethodState->GetRootMethodState()->mMethodInstance;
			if (rootMethodInstance == NULL)
				return;

			if (varDecl->mIsThis)
				return;

			auto resolvePassData = mModule->mCompiler->mResolvePassData;
			mDefType = mModule->mCurTypeInstance->mTypeDef;

			mReplaceLocalId = varDecl->mLocalVarId;
			mDefMethod = rootMethodInstance->mMethodDef;
			if (mInsertStartIdx == -1)
			{
				mInsertStartIdx = identifierNode->GetSrcStart();
				mInsertEndIdx = identifierNode->GetSrcEnd();
			}
		}
	}
	else if (mResolveType == BfResolveType_GetResultString)
	{
		if (IsAutocompleteNode(identifierNode))
		{
			String constStr;
			if (varDecl->mConstValue.IsConst())
				constStr = ConstantToString(mModule->mBfIRBuilder, varDecl->mConstValue);
			if (!constStr.IsEmpty())
			{
				mResultString = constStr;
			}
			else
			{
				SetResultStringType(varDecl->mResolvedType);
			}
		}
	}
}

void BfAutoComplete::CheckFieldRef(BfAstNode* identifierNode, BfFieldInstance* fieldInst)
{
	if (mResolveType == BfResolveType_GetSymbolInfo)
	{
		if (mDefField != NULL)
			return;

		if (IsAutocompleteNode(identifierNode))
		{
			while (true)
			{
				if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(identifierNode))
				{
					identifierNode = qualifiedName->mRight;
					if (!IsAutocompleteNode(identifierNode))
						return;
				}
				else
					break;
			}

			//mReplaceTypeDef = fieldInst->mOwner->mTypeDef;
			//mReplaceFieldDef = fieldInst->GetFieldDef();
			mDefType = fieldInst->mOwner->mTypeDef;
			mDefField = fieldInst->GetFieldDef();

			mInsertStartIdx = identifierNode->GetSrcStart();
			mInsertEndIdx = identifierNode->GetSrcEnd();
		}
	}
}

void BfAutoComplete::CheckLabel(BfIdentifierNode* identifierNode, BfAstNode* precedingNode, BfScopeData* scopeData)
{
	String filter;
	if (identifierNode != NULL)
	{
		if ((mModule->mCompiler->mResolvePassData != NULL) && (scopeData != NULL))
		{
			auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
			mModule->mCompiler->mResolvePassData->HandleLocalReference(identifierNode, rootMethodState->mMethodInstance->GetOwner()->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, scopeData->mScopeLocalId);
		}

		if (!IsAutocompleteNode(identifierNode))
			return;

		if (scopeData != NULL)
		{
			if (mResolveType == BfResolveType_GoToDefinition)
			{
				SetDefinitionLocation(scopeData->mLabelNode);
			}
			else if (mResolveType == BfResolveType_GetSymbolInfo)
			{
				auto rootMethodInstance = mModule->mCurMethodState->GetRootMethodState()->mMethodInstance;
				if (rootMethodInstance == NULL)
					return;

				mDefType = mModule->mCurTypeInstance->mTypeDef;

				mReplaceLocalId = scopeData->mScopeLocalId;
				mDefMethod = rootMethodInstance->mMethodDef;
				if (mInsertStartIdx == -1)
				{
					mInsertStartIdx = identifierNode->GetSrcStart();
					mInsertEndIdx = identifierNode->GetSrcEnd();
				}
			}

			if (scopeData->mLabelNode == identifierNode)
				return;
		}

		filter = identifierNode->ToString();
		mInsertStartIdx = identifierNode->GetSrcStart();
		mInsertEndIdx = identifierNode->GetSrcEnd();
	}
	else
	{
		if (precedingNode == NULL)
			return;

		int expectSpacing = 1;
		if (auto precedingToken = BfNodeDynCast<BfTokenNode>(precedingNode))
			if (precedingToken->GetToken() == BfToken_Colon)
				expectSpacing = 0;

		if (!IsAutocompleteNode(precedingNode, expectSpacing))
			return;

		auto bfParser = precedingNode->GetSourceData()->ToParser();
		if (bfParser->mCursorIdx != precedingNode->GetSrcEnd() + expectSpacing - 1)
			return;

		mInsertStartIdx = precedingNode->GetSrcEnd() + expectSpacing;
		mInsertEndIdx = mInsertStartIdx;
	}

	auto checkScope = mModule->mCurMethodState->mCurScope;
	while (checkScope != NULL)
	{
		if (!checkScope->mLabel.empty())
			AddEntry(AutoCompleteEntry("label", checkScope->mLabel), filter);
		checkScope = checkScope->mPrevScope;
	}
}

void BfAutoComplete::CheckNamespace(BfAstNode* node, const BfAtomComposite& namespaceName)
{
	if (mResolveType == BfResolveType_GetSymbolInfo)
	{
		if (IsAutocompleteNode(node))
		{
			int namespaceCount = namespaceName.mSize;
			auto checkNode = node;

			while (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkNode))
			{
				if (!IsAutocompleteNode(qualifiedTypeRef->mLeft))
					break;
				namespaceCount--;
				checkNode = qualifiedTypeRef->mLeft;
			}
			while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNode))
			{
				if (!IsAutocompleteNode(qualifiedNameNode->mLeft))
					break;
				namespaceCount--;
				checkNode = qualifiedNameNode->mLeft;
			}

			while (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkNode))
				checkNode = qualifiedTypeRef->mRight;
			while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNode))
				checkNode = qualifiedNameNode->mRight;

			if (checkNode != NULL)
			{
				mInsertStartIdx = checkNode->GetSrcStart();
				mInsertEndIdx = checkNode->GetSrcEnd();
				mDefNamespace.Set(namespaceName.mParts, namespaceCount, NULL, 0);
			}
		}
	}
}

void BfAutoComplete::AddTypeInstanceEntry(BfTypeInstance* typeInst)
{
	String bestTypeName = mModule->TypeToString(typeInst, BfTypeNameFlag_ReduceName);

	if (typeInst->IsValueType())
		AddEntry(AutoCompleteEntry("valuetype", bestTypeName));
	else
		AddEntry(AutoCompleteEntry("class", bestTypeName));
	mDefaultSelection = bestTypeName;
}

bool BfAutoComplete::CheckDocumentation(AutoCompleteEntry* entry, BfCommentNode* documentation)
{
	if (mDocumentationEntryName.IsEmpty())
		return false;

	if (mDocumentationEntryName != entry->mDisplay)
		return false;

	if (documentation != NULL)
	{
		StringT<128> str;
		documentation->GetDocString(str);
		entry->mDocumentation = mAlloc.AllocString(str);
	}

	return true;
}

void BfAutoComplete::CheckEmptyStart(BfAstNode* prevNode, BfType* type)
{
	// Temporarily (?) removed?
	return;

	if (IsAutocompleteNode(prevNode, 2))
	{
		if (!type->IsEnum())
			return;

		int wantCursorIdx = prevNode->GetSrcEnd() - 1;
		String prevNodeString = prevNode->ToString();
		if (prevNodeString != "(")
			wantCursorIdx++;
		if (prevNode->GetSourceData()->ToParser()->mCursorIdx != wantCursorIdx)
			return;

		AddTypeInstanceEntry(type->ToTypeInstance());
		CheckIdentifier(NULL);
		mInsertStartIdx = wantCursorIdx + 1;
		mInsertEndIdx = mInsertStartIdx;
	}
}

bool BfAutoComplete::CheckFixit(BfAstNode* node)
{
	if (mIgnoreFixits)
		return false;
	if (mModule == NULL)
		return false;
	if (mCompiler->mResolvePassData->mResolveType != BfResolveType_GetFixits)
		return false;
	if (!IsAutocompleteLineNode(node))
		return false;
	if (mInsertStartIdx == -1)
	{
		mInsertStartIdx = node->GetSrcStart();
		mInsertEndIdx = node->GetSrcStart();
	}
	return true;
}

int BfAutoComplete::FixitGetMemberInsertPos(BfTypeDef* typeDef)
{
	BfTypeDeclaration* typeDecl = typeDef->mTypeDeclaration;
	BfTokenNode* openNode = NULL;
	BfTokenNode* closeNode = NULL;
	if (auto blockNode = BfNodeDynCast<BfBlock>(typeDecl->mDefineNode))
	{
		openNode = blockNode->mOpenBrace;
		closeNode = blockNode->mCloseBrace;
	}

	int insertPos = -1;
	BfParserData* parser = typeDef->mTypeDeclaration->GetSourceData()->ToParserData();
	if ((parser != NULL) && (closeNode != NULL))
	{
		int startPos = openNode->mSrcStart + 1;
		insertPos = closeNode->mSrcStart;
		while (insertPos > startPos)
		{
			char prevC = parser->mSrc[insertPos - 1];
			if (prevC == '\n')
				break;
			insertPos--;
		}
		if (insertPos > startPos)
			insertPos--;
	}
	return insertPos;
}

void BfAutoComplete::CheckInterfaceFixit(BfTypeInstance* typeInstance, BfAstNode* node)
{
	if (!CheckFixit(node))
		return;
	if (typeInstance == NULL)
		return;

	if (typeInstance->IsInterface())
		return;

	for (auto& ifaceTypeInst : typeInstance->mInterfaces)
	{
		Array<BfMethodInstance*> missingMethods;

		auto ifaceInst = ifaceTypeInst.mInterfaceType;
		int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
		int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();
		auto declTypeDef = ifaceTypeInst.mDeclaringType;

		for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)
		{
			auto matchedMethodRef = &typeInstance->mInterfaceMethodTable[iMethodIdx + startIdx].mMethodRef;
			BfMethodInstance* matchedMethod = *matchedMethodRef;
			auto ifaceMethodInst = ifaceInst->mMethodInstanceGroups[iMethodIdx].mDefault;
			if (ifaceMethodInst == NULL)
				continue;

			auto iReturnType = ifaceMethodInst->mReturnType;
			if (iReturnType->IsSelf())
				iReturnType = typeInstance;

			if (ifaceMethodInst->mMethodDef->mIsOverride)
				continue; // Don't consider overrides here

			// If we have "ProjA depends on LibBase", "ProjB depends on LibBase", then a type ClassC in LibBase implementing IFaceD,
			//  where IFaceD gets extended with MethodE in ProjA, an implementing MethodE is still required to exist on ClassC --
			//  the visibility is bidirectional.  A type ClassF implementing IFaceD inside ProjB will not be required to implement
			//  MethodE, however
			if ((!ifaceInst->IsTypeMemberAccessible(ifaceMethodInst->mMethodDef->mDeclaringType, ifaceTypeInst.mDeclaringType)) &&
				(!ifaceInst->IsTypeMemberAccessible(ifaceTypeInst.mDeclaringType, ifaceMethodInst->mMethodDef->mDeclaringType)))
				continue;

			if (!ifaceInst->IsTypeMemberIncluded(ifaceMethodInst->mMethodDef->mDeclaringType, ifaceTypeInst.mDeclaringType))
				continue;

			bool hadMatch = matchedMethod != NULL;
			bool hadPubFailure = false;
			bool hadMutFailure = false;

			if (!hadMatch)
				missingMethods.Add(ifaceMethodInst);
		}

		if (!missingMethods.IsEmpty())
		{
			BfParserData* parser = declTypeDef->mTypeDeclaration->GetSourceData()->ToParserData();
			if (parser != NULL)
			{
				int insertPos = FixitGetMemberInsertPos(declTypeDef);

				bool wantsBreak = false;
				String insertStr = "\f";
				for (auto methodInst : missingMethods)
				{
					if (wantsBreak)
					{
						insertStr += "\r\r";
						wantsBreak = false;
					}
					if (GetMethodInfo(methodInst, NULL, &insertStr, true, false))
					{
						insertStr += "\b";
						wantsBreak = true;
					}
				}

				wantsBreak = false;
				String explicitInsertStr = "\f";
				for (auto methodInst : missingMethods)
				{
					if (wantsBreak)
					{
						explicitInsertStr += "\r\r";
						wantsBreak = false;
					}
					if (GetMethodInfo(methodInst, NULL, &explicitInsertStr, true, true))
					{
						explicitInsertStr += "\b";
						wantsBreak = true;
					}
				}

				if (insertPos != -1)
				{
					mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("Implement interface '%s'\tusing|%s|%s",
						mModule->TypeToString(ifaceInst).c_str(), FixitGetLocation(parser, insertPos).c_str(), insertStr.c_str()).c_str()));
					mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("Implement interface '%s' explicitly\tusing|%s|%s",
						mModule->TypeToString(ifaceInst).c_str(), FixitGetLocation(parser, insertPos).c_str(), explicitInsertStr.c_str()).c_str()));
				}
			}
		}
	}

	if ((!typeInstance->IsInterface()) && (!typeInstance->IsUnspecializedTypeVariation()) && (!typeInstance->IsBoxed()))
	{
		if (!typeInstance->mTypeDef->mIsAbstract)
		{
			Array<BfMethodInstance*> missingMethods;

			for (int methodIdx = 0; methodIdx < (int)typeInstance->mVirtualMethodTable.size(); methodIdx++)
			{
				auto& methodRef = typeInstance->mVirtualMethodTable[methodIdx].mImplementingMethod;
				if (methodRef.mMethodNum == -1)
				{
					BF_ASSERT(mCompiler->mOptions.mHasVDataExtender);
					if (methodRef.mTypeInstance == typeInstance)
					{
						if (typeInstance->GetImplBaseType() != NULL)
							BF_ASSERT(methodIdx == (int)typeInstance->GetImplBaseType()->mVirtualMethodTableSize);
					}
					continue;
				}
				auto methodInstance = (BfMethodInstance*)methodRef;
				if ((methodInstance != NULL) && (methodInstance->mMethodDef->mIsAbstract))
				{
					if (methodInstance->mMethodDef->mIsAbstract)
					{
						if (!typeInstance->IsUnspecializedTypeVariation())
							missingMethods.Add(methodInstance);
					}
				}
			}

			if (!missingMethods.IsEmpty())
			{
				auto declTypeDef = typeInstance->mTypeDef;
				BfParserData* parser = declTypeDef->mTypeDeclaration->GetSourceData()->ToParserData();
				if (parser != NULL)
				{
					int insertPos = FixitGetMemberInsertPos(declTypeDef);

					bool wantsBreak = false;
					String insertStr = "\f";
					for (auto methodInst : missingMethods)
					{
						if (wantsBreak)
						{
							insertStr += "\r\r";
							wantsBreak = false;
						}

						if (GetMethodInfo(methodInst, NULL, &insertStr, true, false))
						{
							insertStr += "\b";
							wantsBreak = true;
						}
					}

					if (insertPos != -1)
					{
						mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("Implement abstract methods\tmethod|%s|%s",
							FixitGetLocation(parser, insertPos).c_str(), insertStr.c_str()).c_str()));
					}
				}
			}
		}
	}
}

void BfAutoComplete::FixitAddMember(BfTypeInstance* typeInst, BfType* fieldType, const StringImpl& fieldName, bool isStatic, BfTypeInstance* referencedFrom)
{
	if (typeInst == mModule->mContext->mBfObjectType)
		return;

	auto parser = typeInst->mTypeDef->GetDefinition()->mSource->ToParser();
	if (parser == NULL)
		return;

	String fullName = typeInst->mTypeDef->mFullName.ToString();
	String fieldStr;

	if (typeInst == referencedFrom)
	{
		// Implicitly private
	}
	else if ((referencedFrom != NULL) && (mModule->TypeIsSubTypeOf(referencedFrom, typeInst)))
	{
		fieldStr += "protected ";
	}
	else
	{
		fieldStr += "public ";
	}

	if (isStatic)
		fieldStr += "static ";

	if (fieldType != NULL)
		fieldStr += mModule->TypeToString(fieldType, BfTypeNameFlag_ReduceName);
	else
		fieldStr += "Object";

	fieldStr += " " + fieldName + ";";

	int fileLoc = typeInst->mTypeDef->mTypeDeclaration->GetSrcEnd();

	if (auto defineBlock = BfNodeDynCast<BfBlock>(typeInst->mTypeDef->mTypeDeclaration->mDefineNode))
		fileLoc = BfFixitFinder::FindLineStartAfter(defineBlock->mOpenBrace);
	if (!typeInst->mTypeDef->mFields.empty())
	{
		auto fieldDecl = typeInst->mTypeDef->mFields.back()->mFieldDeclaration;
		if (fieldDecl != NULL)
		{
			fileLoc = BfFixitFinder::FindLineStartAfter(fieldDecl);
		}
	}

	const char* memberName = "field";
	if (isStatic)
		memberName = "static field";
	AddEntry(AutoCompleteEntry("fixit", StrFormat("Create %s '%s' in '%s'\taddField|%s||%s", memberName, fieldName.c_str(), fullName.c_str(),
		FixitGetLocation(parser->mParserData, fileLoc).c_str(), fieldStr.c_str()).c_str()));
}

void BfAutoComplete::FixitAddCase(BfTypeInstance* typeInst, const StringImpl& caseName, const BfTypeVector& fieldTypes)
{
	if (typeInst == mModule->mContext->mBfObjectType)
		return;

	auto parser = typeInst->mTypeDef->GetDefinition()->mSource->ToParser();
	if (parser == NULL)
		return;

	String fullName = typeInst->mTypeDef->mFullName.ToString();
	String fieldStr;

	int fileLoc = typeInst->mTypeDef->mTypeDeclaration->GetSrcEnd();

	if (auto defineBlock = BfNodeDynCast<BfBlock>(typeInst->mTypeDef->mTypeDeclaration->mDefineNode))
		fileLoc = BfFixitFinder::FindLineStartAfter(defineBlock->mOpenBrace);
	if (!typeInst->mTypeDef->mFields.empty())
	{
		auto fieldDecl = typeInst->mTypeDef->mFields.back()->mFieldDeclaration;
		if (fieldDecl != NULL)
		{
			fileLoc = BfFixitFinder::FindLineStartAfter(fieldDecl);
		}
	}

	bool isSimpleCase = false;
	if (!typeInst->mTypeDef->mFields.IsEmpty())
	{
		if (auto block = BfNodeDynCast<BfBlock>(typeInst->mTypeDef->mTypeDeclaration->mDefineNode))
		{
			bool endsInComma = false;

			if (!block->mChildArr.IsEmpty())
			{
				auto lastNode = block->mChildArr.back();
				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(lastNode))
				{
					if (tokenNode->mToken == BfToken_Comma)
					{
						isSimpleCase = true;
						endsInComma = true;
					}
				}
				else if (auto enumEntryDecl = BfNodeDynCast<BfEnumEntryDeclaration>(lastNode))
				{
					isSimpleCase = true;
				}
			}

			if (isSimpleCase)
			{
				if (endsInComma)
				{
					fieldStr += "|";
					fieldStr += caseName;
				}
				else
				{
					auto fieldDef = typeInst->mTypeDef->mFields.back();
					fileLoc = fieldDef->mFieldDeclaration->GetSrcEnd();
					fieldStr += ",\r";
					fieldStr += caseName;
				}
			}
		}
	}

	if (!isSimpleCase)
	{
		fieldStr += "|case ";
		fieldStr += caseName;

		if (!fieldTypes.IsEmpty())
		{
			fieldStr += "(";
			FixitGetParamString(fieldTypes, fieldStr);
			fieldStr += ")";
		}
		fieldStr += ";";
	}

	AddEntry(AutoCompleteEntry("fixit", StrFormat("Create case '%s' in '%s'\taddField|%s|%s", caseName.c_str(), fullName.c_str(),
		FixitGetLocation(parser->mParserData, fileLoc).c_str(), fieldStr.c_str()).c_str()));
}

void BfAutoComplete::FixitGetParamString(const BfTypeVector& paramTypes, StringImpl& outStr)
{
	std::set<String> usedNames;

	for (int argIdx = 0; argIdx < (int)paramTypes.size(); argIdx++)
	{
		if (argIdx > 0)
			outStr += ", ";
		BfType* paramType = paramTypes[argIdx];
		String checkName = "param";
		if (paramType != NULL)
		{
			bool isOut = false;
			bool isArr = false;

			BfType* checkType = paramType;
			while (true)
			{
				if ((checkType->IsArray()) || (checkType->IsSizedArray()))
				{
					isArr = true;
					checkType = checkType->GetUnderlyingType();
				}
				else if (checkType->IsRef())
				{
					BfRefType* refType = (BfRefType*)checkType;
					if (refType->mRefKind == BfRefType::RefKind_Out)
						isOut = true;
					checkType = refType->GetUnderlyingType();
				}
				else if (checkType->IsTypeInstance())
				{
					BfTypeInstance* typeInst = (BfTypeInstance*)checkType;
					checkName = typeInst->mTypeDef->mName->ToString();
					if (checkName == "String")
						checkName = "Str";
					if (checkName == "Object")
						checkName = "Obj";
					if (isOut)
						checkName = "out" + checkName;
					else if (isupper(checkName[0]))
					{
						checkName[0] = tolower(checkName[0]);
						for (int i = 1; i < (int)checkName.length(); i++)
						{
							if ((i + 1 < (int)checkName.length()) &&
								(islower(checkName[i + 1])))
								break;
							checkName[i] = tolower(checkName[i]);
						}
					}
					if (isArr)
						checkName += "Arr";
					break;
				}
				else
					break;
			}

			outStr += mModule->TypeToString(paramType, BfTypeNameFlag_ReduceName);
		}
		else
		{
			checkName = "param";
			outStr += "Object";
		}

		for (int i = 1; i < 10; i++)
		{
			String lookupName = checkName;
			if (i > 1)
				lookupName += StrFormat("%d", i);
			if (usedNames.insert(lookupName).second)
			{
				outStr += " " + lookupName;
				break;
			}
		}
	}
}

String BfAutoComplete::FixitGetLocation(BfParserData* parser, int insertPos)
{
	int line = 0;
	int lineChar = 0;
	parser->GetLineCharAtIdx(insertPos, line, lineChar);
	return StrFormat("%s|%d:%d", parser->mFileName.c_str(), line, lineChar);
}

String BfAutoComplete::ConstantToString(BfIRConstHolder* constHolder, BfIRValue id)
{
	char str[32];

	int stringId = mModule->GetStringPoolIdx(id, constHolder);
	if (stringId != -1)
	{
		BfStringPoolEntry* entry;
		if (mModule->mContext->mStringObjectIdMap.TryGetValue(stringId, &entry))
		{
			String result = "\"";
			result += SlashString(entry->mString, true, true, true);
			result += "\"";
			return result;
		}
	}

	auto constant = constHolder->GetConstant(id);
	switch (constant->mTypeCode)
	{
	case BfTypeCode_Boolean:
		return StrFormat(":(bool) %s", constant->mBool ? "true" : "false");
	case BfTypeCode_UInt8:
		return StrFormat(":(uint8) %llu", constant->mUInt64);
	case BfTypeCode_UInt16:
		return StrFormat(":(uint16) %llu", constant->mUInt64);
	case BfTypeCode_UInt32:
		return StrFormat(":(uint32) %llu", constant->mUInt64);
	case BfTypeCode_UInt64:
		return StrFormat(":(uint64) %llu", constant->mUInt64);

	case BfTypeCode_Int8:
		return StrFormat(":(int8) %lld", constant->mInt64);
	case BfTypeCode_Int16:
		return StrFormat(":(int16) %lld", constant->mInt64);
	case BfTypeCode_Int32:
		return StrFormat(":(int32) %lld", constant->mInt64);
	case BfTypeCode_Int64:
		return StrFormat(":(int64) %lld", constant->mInt64);

	case BfTypeCode_Float:
		{
			ExactMinimalFloatToStr((float)constant->mDouble, str);
			String result;
			result += str;
			result += "f";
			return result;
		}
	case BfTypeCode_Double:
		{
			ExactMinimalDoubleToStr(constant->mDouble, str);
			String result;
			result += str;
			return result;
		}
	default:
		break;
	}

	return "";
}

void BfAutoComplete::FixitAddMethod(BfTypeInstance* typeInst, const StringImpl& methodName, BfType* returnType, const BfTypeVector& paramTypes, bool wantStatic)
{
	if ((typeInst->IsEnum()) && (returnType == typeInst) && (wantStatic))
	{
		FixitAddCase(typeInst, methodName, paramTypes);
		return;
	}

	if ((typeInst->mTypeDef->mSource != NULL) && (typeInst != mModule->mContext->mBfObjectType))
	{
		auto parser = typeInst->mTypeDef->GetDefinition()->mSource->ToParser();
		if (parser != NULL)
		{
			String fullName = typeInst->mTypeDef->mFullName.ToString();
			String methodStr;

			methodStr += "\f\a";

			if (typeInst == mModule->mCurTypeInstance)
			{
				// Implicitly private
			}
			else if (mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, typeInst))
			{
				methodStr += "protected ";
			}
			else
			{
				methodStr += "public ";
			}

			if (wantStatic)
				methodStr += "static ";

			if (returnType != NULL)
				methodStr += mModule->TypeToString(returnType, BfTypeNameFlag_ReduceName);
			else
				methodStr += "void";

			methodStr += " " + methodName + "(";

			FixitGetParamString(paramTypes, methodStr);

			int insertPos = FixitGetMemberInsertPos(typeInst->mTypeDef);

			methodStr += ")";
			methodStr += "\t";
			AddEntry(AutoCompleteEntry("fixit", StrFormat("Create method '%s' in '%s'\taddMethod|%s|%s", methodName.c_str(), fullName.c_str(), FixitGetLocation(parser->mParserData, insertPos).c_str(), methodStr.c_str()).c_str()));
		}
	}
}

void BfAutoComplete::FixitAddNamespace(BfAstNode* refNode, const StringImpl& namespaceStr)
{
	auto parserData = refNode->GetParserData();

	BfUsingFinder usingFinder;
	usingFinder.mFromIdx = refNode->mSrcStart;
	usingFinder.VisitMembers(refNode->GetSourceData()->mRootNode);
	AddEntry(AutoCompleteEntry("fixit", StrFormat("using %s;\t.using|%s|%d||using %s;", namespaceStr.c_str(), parserData->mFileName.c_str(),
		usingFinder.mLastIdx, namespaceStr.c_str()).c_str()));
}

void BfAutoComplete::FixitCheckNamespace(BfTypeDef* activeTypeDef, BfAstNode* typeRef, BfTokenNode* nextDotToken)
{
	if (nextDotToken == NULL)
		return;

	auto parserData = typeRef->GetParserData();

	BfSizedAtomComposite namespaceComposite;
	String namespaceString = typeRef->ToString();
	bool isValid = mSystem->ParseAtomComposite(namespaceString, namespaceComposite);

	bool hasNamespace = false;
	if (activeTypeDef != NULL)
		hasNamespace = activeTypeDef->mNamespaceSearch.Contains(namespaceComposite);

	if (hasNamespace)
	{
		AddEntry(AutoCompleteEntry("fixit", StrFormat("Remove unneeded '%s'\taddMethod|%s-%d|", typeRef->ToString().c_str(),
			FixitGetLocation(parserData, typeRef->GetSrcStart()).c_str(), nextDotToken->GetSrcEnd() - typeRef->GetSrcStart()).c_str()));
	}
	else
	{
		FixitAddNamespace(typeRef, namespaceString);
	}
}

void BfAutoComplete::FixitAddConstructor(BfTypeInstance *typeInstance)
{
	auto baseType = typeInstance->mBaseType;
	auto parser = typeInstance->mTypeDef->GetDefinition()->mSource->ToParser();
	if (parser != NULL)
	{
		for (auto methodDef : baseType->mTypeDef->mMethods)
		{
			if (methodDef->mMethodType != BfMethodType_Ctor)
				continue;
			if (methodDef->mIsStatic)
				continue;
			if (methodDef->mProtection <= BfProtection_Private)
				continue;

			auto methodInstance = mModule->GetRawMethodInstanceAtIdx(baseType, methodDef->mIdx);

			String ctorShowName;

			int insertPos = FixitGetMemberInsertPos(mModule->mCurTypeInstance->mTypeDef);
			String methodStr = "\f\a";
			if (methodInstance->mMethodDef->mHasAppend)
				methodStr += "[AllowAppend]\r";
			methodStr += "public this(";
			int useParamIdx = 0;
			for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
			{
				if (useParamIdx > 0)
					methodStr += ", ";

				auto paramKind = methodInstance->GetParamKind(paramIdx);
				if (paramKind == BfParamKind_AppendIdx)
					continue;

				switch (paramKind)
				{
				case BfParamKind_Params:
					methodStr += "params ";
					break;
				default:
					break;
				}
				methodStr += mModule->TypeToString(methodInstance->GetParamType(paramIdx), BfTypeNameFlag_ReduceName);
				methodStr += " ";
				methodStr += methodInstance->GetParamName(paramIdx);
				useParamIdx++;
			}
			methodStr += ") : base(";

			ctorShowName += "this(";
			useParamIdx = 0;
			for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
			{
				if (useParamIdx > 0)
				{
					ctorShowName += ", ";
					methodStr += ", ";
				}

				auto paramKind = methodInstance->GetParamKind(paramIdx);
				if (paramKind == BfParamKind_AppendIdx)
					continue;

				switch (paramKind)
				{
				case BfParamKind_Params:
					methodStr += "params ";
					break;
				default:
					break;
				}

				auto paramType = methodInstance->GetParamType(paramIdx);
				if (paramType->IsRef())
				{
					switch (((BfRefType*)paramType)->mRefKind)
					{
					case BfRefType::RefKind_Ref: methodStr += "ref "; break;
					case BfRefType::RefKind_Out: methodStr += "out "; break;
					case BfRefType::RefKind_Mut: methodStr += "mut "; break;
					}
				}

				methodStr += methodInstance->GetParamName(paramIdx);
				ctorShowName += methodInstance->GetParamName(paramIdx);
				useParamIdx++;
			}
			ctorShowName += ")";

			methodStr += ")\t";
			AddEntry(AutoCompleteEntry("fixit", StrFormat("Create constructor '%s'\taddMethod|%s|%s", ctorShowName.c_str(),
				FixitGetLocation(parser->mParserData, insertPos).c_str(), methodStr.c_str()).c_str()));
		}
	}
}

void BfAutoComplete::SetResultStringType(BfType * type)
{
	SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mModule->mCurTypeInstance, type->ToTypeInstance());
	SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mModule->mCurMethodInstance, NULL);

	mResultString = ":";
	mResultString += mModule->TypeToString(type);
	if (type->IsObject())
		mResultString += "\n:type\tclass";
	else if (type->IsInterface())
		mResultString += "\n:type\tinterface";
	else if (type->IsPointer())
		mResultString += "\n:type\tpointer";
	else
		mResultString += "\n:type\tvaluetype";
}

void BfAutoComplete::FixitAddFullyQualify(BfAstNode* refNode, const StringImpl& findName, const SizedArrayImpl<BfUsingFieldData::MemberRef>& foundList)
{
	BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
	String fullName;
	for (int entryIdx = 0; entryIdx < foundList.mSize - 1; entryIdx++)
	{
		auto& entry = foundList[entryIdx];
		if (entryIdx > 0)
			fullName += ".";
		if (!mModule->CheckProtection(protectionCheckFlags, entry.mTypeInstance, entry.GetDeclaringType(mModule)->mProject, entry.GetProtection(), mModule->mCurTypeInstance))
			fullName += "[Friend]";
		fullName += entry.GetName(mModule);
	}

	BfParserData* parser = refNode->GetSourceData()->ToParserData();
	if (parser != NULL)
	{
		AddEntry(AutoCompleteEntry("fixit", StrFormat("Fully qualify 'using' name as '%s.%s'\tqualify|%s|%d|%s.",
			fullName.c_str(), findName.c_str(),
			parser->mFileName.c_str(), refNode->mSrcStart,
			fullName.c_str()).c_str()));
	}
}