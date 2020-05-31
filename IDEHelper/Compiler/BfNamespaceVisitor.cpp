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
	BfAtomComposite usingComposite;
	mSystem->ParseAtomComposite(usingString, usingComposite, true);

	if (mResolvePassData->mAutoComplete != NULL)
		mResolvePassData->mAutoComplete->CheckNamespace(usingDirective->mNamespace, usingComposite);
	mResolvePassData->HandleNamespaceReference(usingDirective->mNamespace, usingComposite);
}

void BfNamespaceVisitor::Visit(BfUsingStaticDirective* usingDirective)
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
	
	String usingString = useNode->ToString();
	
	BfAtomComposite usingComposite;
	if (mSystem->ParseAtomComposite(usingString, usingComposite, true))
		mResolvePassData->HandleNamespaceReference(useNode, usingComposite);
}

void BfNamespaceVisitor::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	BfAtomComposite prevNamespace = mNamespace;
	
	if (namespaceDeclaration->mNameNode == NULL)
		return;

	Array<BfAtom*> derefAtoms;

	String namespaceLeft = namespaceDeclaration->mNameNode->ToString();
	while (true)
	{
		int dotIdx = (int)namespaceLeft.IndexOf('.');
		if (dotIdx == -1)
		{
			BfAtom* namespaceAtom = mSystem->GetAtom(namespaceLeft);
			mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);			
			derefAtoms.Add(namespaceAtom);
			break;
		}

		BfAtom* namespaceAtom = mSystem->GetAtom(namespaceLeft.Substring(0, dotIdx));
		mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);
		namespaceLeft = namespaceLeft.Substring(dotIdx + 1);

		derefAtoms.Add(namespaceAtom);		
	}

	if (mResolvePassData->mAutoComplete != NULL)
		mResolvePassData->mAutoComplete->CheckNamespace(namespaceDeclaration->mNameNode, mNamespace);
	mResolvePassData->HandleNamespaceReference(namespaceDeclaration->mNameNode, mNamespace);
	VisitChild(namespaceDeclaration->mBlock);	
	mNamespace = prevNamespace;

	for (auto atom : derefAtoms)
		mSystem->ReleaseAtom(atom);
}

void BfNamespaceVisitor::Visit(BfBlock* block)
{
	VisitMembers(block);
}

void BfNamespaceVisitor::Visit(BfRootNode* rootNode)
{
	VisitMembers(rootNode);
}
