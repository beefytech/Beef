#pragma once

#include "BfAst.h"

NS_BF_BEGIN

class BfElementVisitor : public BfStructuralVisitor
{
public:
	BfElementVisitor();

	virtual void Visit(BfAstNode* bfAstNode) {}
	virtual void Visit(BfErrorNode* bfErrorNode);
	virtual void Visit(BfScopeNode* scopeNode);
	virtual void Visit(BfNewNode* newNode);
	virtual void Visit(BfLabeledBlock* labeledBlock);
	virtual void Visit(BfExpression* expr);
	virtual void Visit(BfExpressionStatement* exprStmt);
	virtual void Visit(BfNamedExpression* namedExpr);
	virtual void Visit(BfAttributedExpression* attribExpr);
	virtual void Visit(BfStatement* stmt);
	virtual void Visit(BfAttributedStatement* attribStmt);
	virtual void Visit(BfLabelableStatement* labelableStmt);
	virtual void Visit(BfTypedValueExpression* typedValueExpr);

	virtual void Visit(BfCommentNode* commentNode);
	virtual void Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection);
	virtual void Visit(BfPreprocessorNode* preprocessorNode);
	virtual void Visit(BfPreprocessorDefinedExpression* definedExpr);

	virtual void Visit(BfAttributeDirective* attributeDirective);
	virtual void Visit(BfGenericParamsDeclaration* genericParams);
	virtual void Visit(BfGenericOperatorConstraint* genericConstraints);
	virtual void Visit(BfGenericConstraintsDeclaration* genericConstraints);
	virtual void Visit(BfGenericArgumentsNode* genericArgumentsNode);
	virtual void Visit(BfCtorExplicitNode* genericArgumentsNode);

	virtual void Visit(BfEmptyStatement* emptyStmt);
	virtual void Visit(BfTokenNode* tokenNode);
	virtual void Visit(BfTokenPairNode* tokenPairNode);
	virtual void Visit(BfUsingSpecifierNode* usingSpecifier);
	virtual void Visit(BfLiteralExpression* literalExpr);
	virtual void Visit(BfStringInterpolationExpression* stringInterpolationExpression);
	virtual void Visit(BfIdentifierNode* identifierNode);
	virtual void Visit(BfAttributedIdentifierNode* attrIdentifierNode);
	virtual void Visit(BfQualifiedNameNode* nameNode);
	virtual void Visit(BfThisExpression* thisExpr);
	virtual void Visit(BfBaseExpression* baseExpr);
	virtual void Visit(BfMixinExpression* thisExpr);
	virtual void Visit(BfSizedArrayCreateExpression* createExpr);
	virtual void Visit(BfInitializerExpression* initExpr);
	virtual void Visit(BfCollectionInitializerExpression* collectionInitExpr);
	virtual void Visit(BfTypeReference* typeRef);
	virtual void Visit(BfNamedTypeReference* typeRef);
	virtual void Visit(BfQualifiedTypeReference* qualifiedType);
	virtual void Visit(BfDotTypeReference* typeRef);
	virtual void Visit(BfVarTypeReference* typeRef);
	virtual void Visit(BfVarRefTypeReference* typeRef);
	virtual void Visit(BfLetTypeReference* typeRef);
	virtual void Visit(BfConstTypeRef* typeRef);
	virtual void Visit(BfConstExprTypeRef* typeRef);
	virtual void Visit(BfRefTypeRef* typeRef);
	virtual void Visit(BfModifiedTypeRef* typeRef);
	virtual void Visit(BfArrayTypeRef* typeRef);
	virtual void Visit(BfGenericInstanceTypeRef* typeRef);
	virtual void Visit(BfTupleTypeRef* typeRef);
	virtual void Visit(BfExprModTypeRef* typeRef);
	virtual void Visit(BfDelegateTypeRef* typeRef);
	virtual void Visit(BfPointerTypeRef* typeRef);
	virtual void Visit(BfNullableTypeRef* typeRef);
	virtual void Visit(BfVariableDeclaration* varDecl);
	virtual void Visit(BfLocalMethodDeclaration* methodDecl);
	virtual void Visit(BfParameterDeclaration* paramDecl);
	virtual void Visit(BfTypeAttrExpression* typeAttrExpr);
	virtual void Visit(BfOffsetOfExpression* offsetOfExpr);
	virtual void Visit(BfNameOfExpression* nameOfExpr);
	virtual void Visit(BfDefaultExpression* defaultExpr);
	virtual void Visit(BfIsConstExpression* isConstExpr);
	virtual void Visit(BfUninitializedExpression* uninitializedExpr);
	virtual void Visit(BfCheckTypeExpression* checkTypeExpr);
	virtual void Visit(BfDynamicCastExpression* dynCastExpr);
	virtual void Visit(BfCastExpression* castExpr);
	virtual void Visit(BfDelegateBindExpression* delegateBindExpr);
	virtual void Visit(BfLambdaBindExpression* lambdaBindExpr);
	virtual void Visit(BfObjectCreateExpression* objCreateExpr);
	virtual void Visit(BfBoxExpression* boxExpr);
	virtual void Visit(BfScopedInvocationTarget* scopedTarget);
	virtual void Visit(BfInvocationExpression* invocationExpr);
	virtual void Visit(BfDeferStatement* deferStmt);
	virtual void Visit(BfEnumCaseBindExpression* caseBindExpr);
	virtual void Visit(BfCaseExpression* caseExpr);
	virtual void Visit(BfSwitchCase* switchCase);
	virtual void Visit(BfWhenExpression* whenExpr);
	virtual void Visit(BfSwitchStatement* switchStmt);
	virtual void Visit(BfTryStatement* tryStmt);
	virtual void Visit(BfCatchStatement* catchStmt);
	virtual void Visit(BfFinallyStatement* finallyStmt);
	virtual void Visit(BfCheckedStatement* checkedStmt);
	virtual void Visit(BfUncheckedStatement* uncheckedStmt);
	virtual void Visit(BfIfStatement* ifStmt);
	virtual void Visit(BfThrowStatement* throwStmt);
	virtual void Visit(BfDeleteStatement* deleteStmt);
	virtual void Visit(BfReturnStatement* returnStmt);
	virtual void Visit(BfYieldStatement* returnStmt);
	virtual void Visit(BfBreakStatement* breakStmt);
	virtual void Visit(BfContinueStatement* continueStmt);
	virtual void Visit(BfFallthroughStatement* fallthroughStmt);
	virtual void Visit(BfUsingStatement* whileStmt);
	virtual void Visit(BfDoStatement* whileStmt);
	virtual void Visit(BfRepeatStatement* repeatStmt);
	virtual void Visit(BfWhileStatement* whileStmt);
	virtual void Visit(BfForStatement* forStmt);
	virtual void Visit(BfForEachStatement* forEachStmt);
	virtual void Visit(BfConditionalExpression* condExpr);
	virtual void Visit(BfAssignmentExpression* assignExpr);
	virtual void Visit(BfParenthesizedExpression* parenExpr);
	virtual void Visit(BfTupleExpression* parenExpr);
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr);
	virtual void Visit(BfIndexerExpression* indexerExpr);
	virtual void Visit(BfUnaryOperatorExpression* binOpExpr);
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr);
	virtual void Visit(BfConstructorDeclaration* ctorDeclaration);
	virtual void Visit(BfAutoConstructorDeclaration* ctorDeclaration);
	virtual void Visit(BfDestructorDeclaration* dtorDeclaration);
	virtual void Visit(BfMethodDeclaration* methodDeclaration);
	virtual void Visit(BfOperatorDeclaration* operatorDeclaration);
	virtual void Visit(BfPropertyMethodDeclaration* propertyDeclaration);
	virtual void Visit(BfPropertyBodyExpression* propertyBodyExpression);
	virtual void Visit(BfPropertyDeclaration* propertyDeclaration);
	virtual void Visit(BfIndexerDeclaration* indexerDeclaration);
	virtual void Visit(BfFieldDeclaration* fieldDeclaration);
	virtual void Visit(BfEnumCaseDeclaration* enumCaseDeclaration);
	virtual void Visit(BfFieldDtorDeclaration* fieldDtorDeclaration);
	virtual void Visit(BfTypeDeclaration* typeDeclaration);
	virtual void Visit(BfTypeAliasDeclaration* typeDeclaration);
	virtual void Visit(BfUsingDirective* usingDirective);
	virtual void Visit(BfUsingModDirective* usingDirective);
	virtual void Visit(BfNamespaceDeclaration* namespaceDeclaration);
	virtual void Visit(BfBlock* block);
	virtual void Visit(BfRootNode* rootNode);
	virtual void Visit(BfInlineAsmStatement* asmStmt);
	virtual void Visit(BfInlineAsmInstruction* asmInst);
};

NS_BF_END