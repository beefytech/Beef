#include "BfNamespaceVisitor.h"
#include "BfResolvePass.h"
#include "BfAutoComplete.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

void BfNamespaceVisitor::Visit(BfUsingDirective* usingDirective)
{
	if (usingDirective->mNamespace == NULL)
	{
		return;
	}

	String usingString = usingDirective->mNamespace->ToString();
	BfAtomCompositeT<16> usingComposite;
	mSystem->ParseAtomComposite(usingString, usingComposite);

	if (mResolvePassData->mAutoComplete != NULL)
		mResolvePassData->mAutoComplete->CheckNamespace(usingDirective->mNamespace, usingComposite);
	mResolvePassData->HandleNamespaceReference(usingDirective->mNamespace, usingComposite);
}

void BfNamespaceVisitor::Visit(BfUsingModDirective* usingDirective)
{
	BfAstNode* useNode = usingDirective->mTypeRef;
	BfAstNode* checkNode = usingDirective->mTypeRef;
	while (true)
	{
		if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkNode))
			checkNode = qualifiedTypeRef->mLeft;
		else if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(checkNode))
		{
			checkNode = elementedTypeRef->mElementType;
			useNode = checkNode;
		}
		else
			break;
	}

	if (useNode == NULL)
		return;

	String usingString = useNode->ToString();

	BfAtomCompositeT<16> usingComposite;
	if (mSystem->ParseAtomComposite(usingString, usingComposite))
		mResolvePassData->HandleNamespaceReference(useNode, usingComposite);
}

void BfNamespaceVisitor::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	BfAtomCompositeT<16> prevNamespace = mNamespace;

	if (namespaceDeclaration->mNameNode == NULL)
		return;

	String namespaceLeft = namespaceDeclaration->mNameNode->ToString();
	while (true)
	{
		int dotIdx = (int)namespaceLeft.IndexOf('.');
		if (dotIdx == -1)
		{
			BfAtom* namespaceAtom = mSystem->FindAtom(namespaceLeft);
			mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);
			break;
		}

		BfAtom* namespaceAtom = mSystem->FindAtom(namespaceLeft.Substring(0, dotIdx));
		mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);
		namespaceLeft = namespaceLeft.Substring(dotIdx + 1);
	}

	if (mResolvePassData->mAutoComplete != NULL)
		mResolvePassData->mAutoComplete->CheckNamespace(namespaceDeclaration->mNameNode, mNamespace);
	mResolvePassData->HandleNamespaceReference(namespaceDeclaration->mNameNode, mNamespace);
	VisitChild(namespaceDeclaration->mBody);
	mNamespace = prevNamespace;
}

void BfNamespaceVisitor::Visit(BfBlock* block)
{
	VisitMembers(block);
}

void BfNamespaceVisitor::Visit(BfRootNode* rootNode)
{
	VisitMembers(rootNode);
}