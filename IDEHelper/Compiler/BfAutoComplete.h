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
	BfCommentNode* mDocumentation;

public:
	AutoCompleteEntry()
	{
	}

	AutoCompleteEntry(const char* entryType, const char* display)
	{
		mEntryType = entryType;
		mDisplay = display;
		mDocumentation = NULL;
	}

	AutoCompleteEntry(const char* entryType, const char* display, BfCommentNode* documentation)
	{
		mEntryType = entryType;
		mDisplay = display;
		mDocumentation = documentation;
	}

	AutoCompleteEntry(const char* entryType, const StringImpl& display)
	{
		mEntryType = entryType;
		mDisplay = display.c_str();
		mDocumentation = NULL;
	}

	AutoCompleteEntry(const char* entryType, const StringImpl& display, BfCommentNode* documentation)
	{
		mEntryType = entryType;
		mDisplay = display.c_str();
		mDocumentation = documentation;
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

	bool DoesFilterMatch(const char* entry, const char* filter);	
	AutoCompleteEntry* AddEntry(const AutoCompleteEntry& entry, const StringImpl& filter);	
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
	BfIdentifierNode* mIdentifierUsed;
	bool mIgnoreFixits;
	bool mHasFriendSet;
	bool mUncertain; // May be an unknown identifier, do not aggressively autocomplete
	int mCursorLineStart;
	int mCursorLineEnd;
	
	//BfMethodInstance* mReplaceMethodInstance;
	
	int mReplaceLocalId;
	//int mDefMethodIdx;
	BfMethodDef* mDefMethod;
	BfTypeDef* mDefType;
	BfFieldDef* mDefField;
	BfPropertyDef* mDefProp;
	int mDefMethodGenericParamIdx;
	int mDefTypeGenericParamIdx;

public:	
	bool CheckProtection(BfProtection protection, bool allowProtected, bool allowPrivate);
	String GetFilter(BfAstNode* node);
	const char* GetTypeName(BfType* type);
	int GetCursorIdx(BfAstNode* node);
	bool IsAutocompleteNode(BfAstNode* node, int lengthAdd = 0, int startAdd = 0);
	bool IsAutocompleteNode(BfAstNode* startNode, BfAstNode* endNode, int lengthAdd = 0, int startAdd = 0);
	bool IsAutocompleteLineNode(BfAstNode* node);
	BfTypedValue LookupTypeRefOrIdentifier(BfAstNode* node, bool* isStatic, BfEvalExprFlags evalExprFlags = BfEvalExprFlags_None, BfType* expectingType = NULL);	
	void SetDefinitionLocation(BfAstNode* astNode, bool force = false);
	bool IsAttribute(BfTypeInstance* typeInst);	
	void AddMethod(BfMethodDeclaration* methodDecl, const StringImpl& methodName, const StringImpl& filter);
	void AddTypeDef(BfTypeDef* typeDef, const StringImpl& filter, bool onlyAttribute = false);
	void AddInnerTypes(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate);
	void AddCurrentTypes(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate, bool onlyAttribute);
	void AddTypeMembers(BfTypeInstance* typeInst, bool addStatic, bool addNonStatic, const StringImpl& filter, BfTypeInstance* startType, bool allowInterfaces, bool allowImplicitThis);
	void AddSelfResultTypeMembers(BfTypeInstance* typeInst, BfTypeInstance* selfType, const StringImpl& filter, bool allowPrivate);
	bool InitAutocomplete(BfAstNode* dotNode, BfAstNode* nameNode, String& filter);
	void AddEnumTypeMembers(BfTypeInstance* typeInst, const StringImpl& filter, bool allowProtected, bool allowPrivate);	
	void AddTopLevelNamespaces(BfIdentifierNode* identifierNode);
	void AddTopLevelTypes(BfIdentifierNode* identifierNode, bool onlyAttribute = false);
	void AddOverrides(const StringImpl& filter);
	void UpdateReplaceData();	
	void AddTypeInstanceEntry(BfTypeInstance* typeInst);
	void CheckDocumentation(AutoCompleteEntry* entry, BfCommentNode* documentation);	

public:
	BfAutoComplete(BfResolveType resolveType = BfResolveType_Autocomplete);
	~BfAutoComplete();

	void SetModule(BfModule* module);
	void Clear();		
	void RemoveMethodMatchInfo();
	void ClearMethodMatchEntries();

	void CheckIdentifier(BfIdentifierNode* identifierNode, bool isInExpression = false, bool isUsingDirective = false);
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
	void CheckLocalDef(BfIdentifierNode* identifierNode, BfLocalVariable* varDecl);
	void CheckLocalRef(BfIdentifierNode* identifierNode, BfLocalVariable* varDecl);
	void CheckFieldRef(BfIdentifierNode* identifierNode, BfFieldInstance* fieldInst);
	void CheckLabel(BfIdentifierNode* identifierNode, BfAstNode* precedingNode = NULL);
	void CheckEmptyStart(BfAstNode* prevNode, BfType* type);	
	bool CheckFixit(BfAstNode* node);	

	void FixitAddMember(BfTypeInstance* typeInst, BfType* fieldType, const StringImpl& fieldName, bool isStatic, BfTypeInstance* referencedFrom);
	
};

NS_BF_END