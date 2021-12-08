#pragma once

#include "BfCompiler.h"
#include "BfSourceClassifier.h"
#include "BfResolvePass.h"

NS_BF_BEGIN

class BfMethodInstance;
class BfLocalVariable;

class AutoCompleteEntry
{
public:
	const char* mEntryType;
	const char* mDisplay;
	const char* mDocumentation;
	int8 mNamePrefixCount;
	int mScore;
	uint8 mMatches[256];

public:
	AutoCompleteEntry()
	{
		mNamePrefixCount = 0;
	}

	AutoCompleteEntry(const char* entryType, const char* display)
	{
		mEntryType = entryType;
		mDisplay = display;
		mDocumentation = NULL;
		mNamePrefixCount = 0;
		mScore = 0;
	}

	AutoCompleteEntry(const char* entryType, const StringImpl& display)
	{
		mEntryType = entryType;
		mDisplay = display.c_str();
		mDocumentation = NULL;
		mNamePrefixCount = 0;
		mScore = 0;
	}

	AutoCompleteEntry(const char* entryType, const StringImpl& display, int namePrefixCount)
	{
		mEntryType = entryType;
		mDisplay = display.c_str();
		mDocumentation = NULL;
		mNamePrefixCount = (int8)namePrefixCount;
		mScore = 0;
	}
	
	bool operator==(const AutoCompleteEntry& other) const
	{
		return strcmp(mDisplay, other.mDisplay) == 0;
	}
};

NS_BF_END

template <>
struct BeefHash<Beefy::AutoCompleteEntry>
{
	size_t operator()(const Beefy::AutoCompleteEntry& val)
	{		
		intptr hash = 0;
		const char* curPtr = val.mDisplay;		
		while (true)
		{
			char c = *(curPtr++);
			if (c == 0)
				break;
			hash = (hash ^ (intptr)c) - hash;			
		}

		return (size_t)hash;
	}
};

NS_BF_BEGIN

class AutoCompleteBase
{
public:	
	class EntryLess
	{
	public:
		bool operator()(const AutoCompleteEntry& left, const AutoCompleteEntry& right)
		{
			auto result = _stricmp(left.mDisplay, right.mDisplay);
			if (result == 0)
				result = strcmp(left.mDisplay, right.mDisplay);
			return result < 0;
		}
	};	

public:
	BumpAllocator mAlloc;
	HashSet<AutoCompleteEntry> mEntriesSet;

	bool mIsGetDefinition;
	bool mIsAutoComplete;
	int mInsertStartIdx;
	int mInsertEndIdx;

	bool DoesFilterMatch(const char* entry, const char* filter, int& score, uint8* matches, int maxMatches);
	AutoCompleteEntry* AddEntry(AutoCompleteEntry& entry, const StringImpl& filter);	
	AutoCompleteEntry* AddEntry(AutoCompleteEntry& entry, const char* filter);
	AutoCompleteEntry* AddEntry(const AutoCompleteEntry& entry);

	AutoCompleteBase();
	virtual ~AutoCompleteBase();

	void Clear();
};

class BfAutoComplete : public AutoCompleteBase
{
public:	
	class MethodMatchEntry
	{
	public:		
		BfMethodDef* mMethodDef;
		BfFieldInstance* mPayloadEnumField;
		BfTypeInstance* mTypeInstance;		
		BfTypeVector mGenericArguments;
		BfMethodInstance* mCurMethodInstance;

		MethodMatchEntry()
		{
			mMethodDef = NULL;
			mPayloadEnumField = NULL;
			mTypeInstance = NULL;			
			mCurMethodInstance = NULL;
		}
	};

	class MethodMatchInfo
	{
	public:		
		BfTypeInstance* mCurTypeInstance;
		BfMethodInstance* mCurMethodInstance;
		Array<MethodMatchEntry> mInstanceList;
		int mInvocationSrcIdx;
		int mBestIdx;		
		int mPrevBestIdx;		
		bool mHadExactMatch;
		int mMostParamsMatched;		
		Array<int> mSrcPositions; // start, commas, end
		
	public:
		MethodMatchInfo()
		{
			mInvocationSrcIdx = -1;
			mCurTypeInstance = NULL;
			mCurMethodInstance = NULL;
			mBestIdx = 0;			
			mPrevBestIdx = -1;			
			mHadExactMatch = false;
			mMostParamsMatched = 0;			
		}

		~MethodMatchInfo()
		{
		}
	};

public:	
	BfModule* mModule;
	BfCompiler* mCompiler;
	BfSystem* mSystem;	
	MethodMatchInfo* mMethodMatchInfo;
	bool mIsCapturingMethodMatchInfo;
	String mDefaultSelection;
	String mResultString;
	String mDocumentationEntryName;
	BfAstNode* mGetDefinitionNode;
	BfResolveType mResolveType;
	BfTypeInstance* mShowAttributeProperties;
	BfAstNode* mIdentifierUsed;
	bool mIgnoreFixits;
	bool mHasFriendSet;
	bool mUncertain; // May be an unknown identifier, do not aggressively autocomplete
	bool mForceAllowNonStatic;
	int mCursorLineStart;
	int mCursorLineEnd;
	
	int mReplaceLocalId;	
	BfMethodDef* mDefMethod;
	BfTypeDef* mDefType;
	BfFieldDef* mDefField;
	BfPropertyDef* mDefProp;
	BfAtomComposite mDefNamespace;
	int mDefMethodGenericParamIdx;
	int mDefTypeGenericParamIdx;

public:	
	bool CheckProtection(BfProtection protection, BfTypeDef* typeDef, bool allowProtected, bool allowPrivate);
	String GetFilter(BfAstNode* node);
	const char* GetTypeName(BfType* type);
	int GetCursorIdx(BfAstNode* node);
	bool IsAutocompleteNode(BfAstNode* node, int lengthAdd = 0, int startAdd = 0);
	bool IsAutocompleteNode(BfAstNode* startNode, BfAstNode* endNode, int lengthAdd = 0, int startAdd = 0);
	bool IsAutocompleteLineNode(BfAstNode* node);
	BfTypedValue LookupTypeRefOrIdentifier(BfAstNode* node, bool* isStatic, BfEvalExprFlags evalExprFlags = BfEvalExprFlags_None, BfType* expectingType = NULL);	
	void SetDefinitionLocation(BfAstNode* astNode, bool force = false);
	bool IsAttribute(BfTypeInstance* typeInst);	
	void AddMethod(BfTypeInstance* typeInstance, BfMethodDef* methodDef, BfMethodInstance* methodInstance, BfMethodDeclaration* methodDecl, const StringImpl& methodName, const StringImpl& filter);
	void AddField(BfTypeInstance* typeInst, BfFieldDef* fieldDef, BfFieldInstance* fieldInstance, const StringImpl& filter);
	void AddProp(BfTypeInstance* typeInst, BfPropertyDef* propDef, const StringImpl& filter);
	void AddTypeDef(BfTypeDef* typeDef, const StringImpl& filter, bool onlyAttribute = false);
	void AddInnerTypes(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate);
	void AddCurrentTypes(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate, bool onlyAttribute);
	void AddTypeMembers(BfTypeInstance* typeInst, bool addStatic, bool addNonStatic, const StringImpl& filter, BfTypeInstance* startType, bool allowInterfaces, bool allowImplicitThis, bool checkOuterType);
	void AddSelfResultTypeMembers(BfTypeInstance* typeInst, BfTypeInstance* selfType, const StringImpl& filter, bool allowPrivate);
	bool InitAutocomplete(BfAstNode* dotNode, BfAstNode* nameNode, String& filter);
	void AddEnumTypeMembers(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate);	
	void AddExtensionMethods(BfTypeInstance* targetType, BfTypeInstance* extensionContainer, const StringImpl& filter, bool allowProtected, bool allowPrivate);
	void AddTopLevelNamespaces(BfAstNode* identifierNode);
	void AddTopLevelTypes(BfAstNode* identifierNode, bool onlyAttribute = false);
	void AddOverrides(const StringImpl& filter);
	void UpdateReplaceData();	
	void AddTypeInstanceEntry(BfTypeInstance* typeInst);
	bool CheckDocumentation(AutoCompleteEntry* entry, BfCommentNode* documentation);
	bool GetMethodInfo(BfMethodInstance* methodInst, StringImpl* methodName, StringImpl* insertString, bool isImplementing, bool isExplicitInterface);
	void FixitGetParamString(const BfTypeVector& paramTypes, StringImpl& outStr);
	int FixitGetMemberInsertPos(BfTypeDef* typeDef);
	String FixitGetLocation(BfParserData* parserData, int insertPos);
	String ConstantToString(BfIRConstHolder* constHolder, BfIRValue id);	

public:
	BfAutoComplete(BfResolveType resolveType = BfResolveType_Autocomplete);
	~BfAutoComplete();

	void SetModule(BfModule* module);
	void Clear();		
	void RemoveMethodMatchInfo();
	void ClearMethodMatchEntries();

	void CheckIdentifier(BfAstNode* identifierNode, bool isInExpression = false, bool isUsingDirective = false);
	bool CheckMemberReference(BfAstNode* target, BfAstNode* dotToken, BfAstNode* memberName, bool onlyShowTypes = false, BfType* expectingType = NULL, bool isUsingDirective = false, bool onlyAttribute = false);
	bool CheckExplicitInterface(BfTypeInstance* interfaceType, BfAstNode* dotToken, BfAstNode* memberName);
	void CheckTypeRef(BfTypeReference* typeRef, bool mayBeIdentifier, bool isInExpression = false, bool onlyAttribute = false);
	void CheckAttributeTypeRef(BfTypeReference* typeRef);
	void CheckInvocation(BfAstNode* invocationNode, BfTokenNode* openParen, BfTokenNode* closeParen, const BfSizedArray<BfTokenNode*>& commas);	
	void CheckNode(BfAstNode* node);	
	void CheckMethod(BfMethodDeclaration* methodDeclaration, bool isLocalMethod);
	void CheckProperty(BfPropertyDeclaration* propertyDeclaration);	
	void CheckVarResolution(BfAstNode* varTypeRef, BfType* resolvedTypeRef);
	void CheckResult(BfAstNode* node, const BfTypedValue& typedValue);
	void CheckLocalDef(BfAstNode* identifierNode, BfLocalVariable* varDecl);
	void CheckLocalRef(BfAstNode* identifierNode, BfLocalVariable* varDecl);
	void CheckFieldRef(BfAstNode* identifierNode, BfFieldInstance* fieldInst);	
	void CheckLabel(BfIdentifierNode* identifierNode, BfAstNode* precedingNode, BfScopeData* scopeData);
	void CheckNamespace(BfAstNode* node, const BfAtomComposite& namespaceName);
	void CheckEmptyStart(BfAstNode* prevNode, BfType* type);	
	bool CheckFixit(BfAstNode* node);	
	void CheckInterfaceFixit(BfTypeInstance* typeInstance, BfAstNode* node);
	
	void FixitAddMember(BfTypeInstance* typeInst, BfType* fieldType, const StringImpl& fieldName, bool isStatic, BfTypeInstance* referencedFrom);
	void FixitAddCase(BfTypeInstance * typeInst, const StringImpl & caseName, const BfTypeVector & fieldTypes);
	void FixitAddMethod(BfTypeInstance* typeInst, const StringImpl& methodName, BfType* returnType, const BfTypeVector& paramTypes, bool wantStatic);
	void FixitAddNamespace(BfAstNode* refNode, const StringImpl& namespacStr);
	void FixitCheckNamespace(BfTypeDef* activeTypeDef, BfAstNode* typeRef, BfTokenNode* nextDotToken);
	void FixitAddConstructor(BfTypeInstance* typeInstance);

	void SetResultStringType(BfType* type);
};

NS_BF_END