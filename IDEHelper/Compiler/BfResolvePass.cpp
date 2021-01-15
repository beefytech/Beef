#include "BfResolvePass.h" 
#include "BfParser.h"
#include "BfModule.h"

USING_NS_BF;

BfResolvePassData::BfResolvePassData()
{	
	mGetSymbolReferenceKind = BfGetSymbolReferenceKind_None;

	mSymbolReferenceTypeDef = NULL;	

	mSymbolReferenceLocalIdx = -1;
	mSymbolReferenceFieldIdx = -1;
	mSymbolReferenceMethodIdx = -1;
	mSymbolReferencePropertyIdx = -1;
	mSymbolMethodGenericParamIdx = -1;
	mSymbolTypeGenericParamIdx = -1;

	mAutoComplete = NULL;
	mSourceClassifier = NULL;
	mResolveType = BfResolveType_None;
	mParser = NULL;
}

void BfResolvePassData::RecordReplaceNode(BfParserData* parser, int srcStart, int srcLen)
{
	String* stringPtr = NULL;
	if (!mFoundSymbolReferencesParserData.TryAdd(parser, NULL, &stringPtr))
	{
		*stringPtr += " ";
	}
	*stringPtr += StrFormat("%d %d", srcStart, srcLen);
}

void BfResolvePassData::RecordReplaceNode(BfAstNode* node)
{
	if (node->IsTemporary())
		return;
	auto parser = node->GetSourceData()->ToParserData();
	if (node->GetSrcStart() >= parser->mSrcLength)
		return;
		
	while (true)
	{
		if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(node))
		{
			node = qualifiedName->mRight;
		}
		else
			break;
	}	

	RecordReplaceNode(parser, node->GetSrcStart(), node->GetSrcLength());
}

void BfResolvePassData::HandleMethodReference(BfAstNode* node, BfTypeDef* typeDef, BfMethodDef* methodDef)
{	
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Method) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferenceMethodIdx == methodDef->mIdx))	
		RecordReplaceNode(node);
}

void BfResolvePassData::HandleFieldReference(BfAstNode* node, BfTypeDef* typeDef, BfFieldDef* fieldDef)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferenceFieldIdx == fieldDef->mIdx))
		RecordReplaceNode(node);
}

void BfResolvePassData::HandlePropertyReference(BfAstNode* node, BfTypeDef* typeDef, BfPropertyDef* propDef)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Property) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferencePropertyIdx == propDef->mIdx))
		RecordReplaceNode(node);
}

void BfResolvePassData::HandleLocalReference(BfIdentifierNode* identifier, BfTypeDef* typeDef, BfMethodDef* methodDef, int localVarIdx)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Local) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferenceMethodIdx == methodDef->mIdx) && (localVarIdx == mSymbolReferenceLocalIdx))
		RecordReplaceNode(identifier);
}

void BfResolvePassData::HandleTypeGenericParam(BfAstNode* node, BfTypeDef* typeDef, int genericParamIdx)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_TypeGenericParam) && (mSymbolReferenceTypeDef == typeDef) && (genericParamIdx == mSymbolTypeGenericParamIdx))
		RecordReplaceNode(node);
}

void BfResolvePassData::HandleMethodGenericParam(BfAstNode* node, BfTypeDef* typeDef, BfMethodDef* methodDef, int genericParamIdx)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_MethodGenericParam) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferenceMethodIdx == methodDef->mIdx) && (genericParamIdx == mSymbolMethodGenericParamIdx))
		RecordReplaceNode(node);
}

void BfResolvePassData::HandleLocalReference(BfIdentifierNode* identifier, BfIdentifierNode* origNameNode, BfTypeDef* typeDef, BfMethodDef* methodDef, int localVarIdx)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Local) && (mSymbolReferenceTypeDef == typeDef) && (mSymbolReferenceMethodIdx == methodDef->mIdx) && (localVarIdx == mSymbolReferenceLocalIdx))
	{
		if (origNameNode == NULL)
			origNameNode = identifier;

		int origLen = origNameNode->GetSrcLength();
		int refLen = identifier->GetSrcLength();

		// The lengths can be different if we have one or more @'s prepended	
		RecordReplaceNode(identifier->GetSourceData()->ToParserData(), identifier->GetSrcStart() + (refLen - origLen), origLen);
	}
}

BfAstNode* BfResolvePassData::FindBaseNode(BfAstNode* node)
{
	BfAstNode* baseNode = node;
	while (true)
	{
		if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(baseNode))
		{
			baseNode = qualifiedTypeRef->mRight;
		}
		else if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(baseNode))
		{
			baseNode = elementedTypeRef->mElementType;
		}
		else if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(baseNode))
		{
			baseNode = namedTypeRef->mNameNode;
		}
		else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(baseNode))
		{
			baseNode = qualifiedNameNode->mRight;
		}
		else if (auto declTypeRef = BfNodeDynCast<BfExprModTypeRef>(baseNode))
		{
			baseNode = NULL;
			break;
		}
		else
			break;
	}
	return baseNode;
}

void BfResolvePassData::HandleTypeReference(BfAstNode* node, BfTypeDef* typeDef)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Type) && (mSymbolReferenceTypeDef == typeDef))
	{
		auto baseNode = FindBaseNode(node);
		if (baseNode != NULL)
			RecordReplaceNode(baseNode);
	}
}

void BfResolvePassData::HandleNamespaceReference(BfAstNode* node, const BfAtomComposite& namespaceName)
{
	if ((mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Namespace) && (namespaceName.StartsWith(mSymbolReferenceNamespace)))
	{
		BfAstNode* recordNode = node;

		int leftCount = namespaceName.mSize - mSymbolReferenceNamespace.mSize;
		for (int i = 0; i < leftCount; i++)
		{
			if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(recordNode))
			{
				recordNode = qualifiedTypeRef->mLeft;
			}
			else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(recordNode))
			{
				recordNode = qualifiedNameNode->mLeft;
			}
			else
				return;
		}

		auto baseNode = FindBaseNode(recordNode);
		if (baseNode != NULL)
			RecordReplaceNode(baseNode);
	}
}
