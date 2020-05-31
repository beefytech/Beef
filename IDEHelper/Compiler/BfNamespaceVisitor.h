#pragma once

#include "BfElementVisitor.h"
#include "BfSystem.h"

NS_BF_BEGIN

class BfResolvePassData;

class BfNamespaceVisitor : public BfStructuralVisitor
{
public:	
	BfSystem* mSystem;
	BfResolvePassData* mResolvePassData;
	BfAtomComposite mNamespace;

public:
	BfNamespaceVisitor()
	{
		mSystem = NULL;
		mResolvePassData = NULL;
	}
	
	virtual void Visit(BfUsingDirective* usingDirective) override;
	virtual void Visit(BfUsingStaticDirective* usingDirective) override;
	virtual void Visit(BfNamespaceDeclaration* namespaceDeclaration) override;
	virtual void Visit(BfBlock* block) override;
	virtual void Visit(BfRootNode* rootNode) override;
};

NS_BF_END