#pragma once

#include "BfSystem.h"
#include "BfResolvedTypeUtils.h"

NS_BF_BEGIN

enum BfResolveType
{
	BfResolveType_None,
	BfResolveType_Classify,
	BfResolveType_ClassifyFullRefresh,
	BfResolveType_Autocomplete,
	BfResolveType_Autocomplete_HighPri,
	BfResolveType_GoToDefinition,
	BfResolveType_GetSymbolInfo,	
	BfResolveType_RenameSymbol,
	BfResolveType_ShowFileSymbolReferences,
	BfResolveType_GetNavigationData,
	BfResolveType_GetCurrentLocation,
	BfResolveType_GetFixits,
	BfResolveType_GetTypeDefList,
	BfResolveType_GetTypeDefInto,
	BfResolveType_GetResultString,
};

class BfLocalVariable;

enum BfGetSymbolReferenceKind
{
	BfGetSymbolReferenceKind_None,
	BfGetSymbolReferenceKind_Local,
	BfGetSymbolReferenceKind_Method,
	BfGetSymbolReferenceKind_Field,
	BfGetSymbolReferenceKind_Property,
	BfGetSymbolReferenceKind_Type,
	BfGetSymbolReferenceKind_TypeGenericParam,
	BfGetSymbolReferenceKind_MethodGenericParam,
	BfGetSymbolReferenceKind_Namespace
};

class BfResolvePassData
{
public:
	BfResolveType mResolveType;

	BfParser* mParser;
	BfAutoComplete* mAutoComplete;
	Array<BfTypeDef*> mAutoCompleteTempTypes; // Contains multiple values when we have nested types
	Dictionary<BfTypeDef*, BfStaticSearch> mStaticSearchMap;
	Dictionary<BfTypeDef*, BfInternalAccessSet> mInternalAccessMap;
	BfSourceClassifier* mSourceClassifier;
	Array<BfAstNode*> mExteriorAutocompleteCheckNodes;

	BfGetSymbolReferenceKind mGetSymbolReferenceKind;	
	String mQueuedReplaceTypeDef;
	BfTypeDef* mSymbolReferenceTypeDef;
	String mQueuedSymbolReferenceNamespace;
	BfAtomComposite mSymbolReferenceNamespace;		
	int mSymbolReferenceLocalIdx;
	int mSymbolReferenceFieldIdx;
	int mSymbolReferenceMethodIdx;
	int mSymbolReferencePropertyIdx;
	int mSymbolMethodGenericParamIdx;
	int mSymbolTypeGenericParamIdx;
	
	typedef Dictionary<BfParserData*, String> FoundSymbolReferencesParserDataMap;
	FoundSymbolReferencesParserDataMap mFoundSymbolReferencesParserData;
	//std::vector<BfIdentifierNode*> mSymbolReferenceIdentifiers;

public:
	void RecordReplaceNode(BfParserData* parser, int srcStart, int srcLen);
	void RecordReplaceNode(BfAstNode* node);	
	BfAstNode* FindBaseNode(BfAstNode* node);

public:
	BfResolvePassData();

	void HandleLocalReference(BfIdentifierNode* identifier, BfTypeDef* typeDef, BfMethodDef* methodDef, int localVarIdx);
	void HandleLocalReference(BfIdentifierNode* identifier, BfIdentifierNode* origNameNode, BfTypeDef* typeDef, BfMethodDef* methodDef, int localVarIdx);
	void HandleTypeGenericParam(BfAstNode* node, BfTypeDef* typeDef, int genericParamIdx);
	void HandleMethodGenericParam(BfAstNode* node, BfTypeDef* typeDef, BfMethodDef* methodDef, int genericParamIdx);
	void HandleMethodReference(BfAstNode* node, BfTypeDef* typeDef, BfMethodDef* methodDef);
	void HandleFieldReference(BfAstNode* node, BfTypeDef* typeDef, BfFieldDef* fieldDef);
	void HandlePropertyReference(BfAstNode* node, BfTypeDef* typeDef, BfPropertyDef* propDef);
	void HandleTypeReference(BfAstNode* node, BfTypeDef* typeDef);	
	void HandleNamespaceReference(BfAstNode* node, const BfAtomComposite& namespaceName);

	//void ReplaceIdentifiers();
};

NS_BF_END