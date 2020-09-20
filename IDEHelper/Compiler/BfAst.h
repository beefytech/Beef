#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/DLIList.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BfAstAllocator.h"
#include "BfIRBuilder.h"

//#define BF_AST_HAS_PARENT_MEMBER
//#define BF_AST_COMPACT
//#define BF_AST_VTABLE

#ifdef _DEBUG
#define BF_AST_VTABLE
#endif

/*#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4996)
#pragma warning(disable:4267)
#pragma warning(disable:4291)
#pragma warning(disable:4267)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#pragma warning(pop)*/

namespace llvm
{
	class Value;
};

NS_BF_BEGIN

class BfType;
class BfParser;
class BfSource;
class BfAstNode;
class BfTokenNode;
class BfTokenPairNode;
class BfTypeReference;
class BfTypeDef;
class BfMethodDef;
class BfFieldDef;
class BfSystem;
class BfMethodInstance;
class BfPassInstance;

enum BfProtection : uint8
{
	BfProtection_Hidden,
	BfProtection_Private,	
	BfProtection_Protected,
	BfProtection_Public
};

enum BfCheckedKind : int8
{
	BfCheckedKind_NotSet,
	BfCheckedKind_Checked,
	BfCheckedKind_Unchecked
};

static bool CheckProtection(BfProtection protection, bool allowProtected, bool allowPrivate)
{		
	return (protection == BfProtection_Public) ||
		((protection == BfProtection_Protected) && (allowProtected)) ||
		((protection == BfProtection_Private) && (allowPrivate));
}

struct BfVariant
{
	BfTypeCode mTypeCode;
	int mWarnType;
	union
	{
		bool mBool;
		int8 mInt8;
		uint8 mUInt8;
		int16 mInt16;
		uint16 mUInt16;
		int32 mInt32;
		uint32 mUInt32;
		int64 mInt64;
		uint64 mUInt64;
		float mSingle;
		double mDouble;		
		String* mString;
		void* mPtr;
	};	
	double ToDouble()
	{
		if (mTypeCode == BfTypeCode_Double)
			return mDouble;
		if (mTypeCode == BfTypeCode_Float)
			return mSingle;
		return (double)mInt64;
	}
};

enum BfToken : uint8
{
	BfToken_None,
	BfToken_Abstract,
	BfToken_AlignOf,
	BfToken_AllocType,
	BfToken_Append,
	BfToken_As,
	BfToken_Asm,
	BfToken_AsmNewline,
	BfToken_Base,
	BfToken_Box,
	BfToken_Break,
	BfToken_Case,
	BfToken_Catch,
	BfToken_Checked,
	BfToken_Class,
	BfToken_Concrete,
	BfToken_Const,
	BfToken_Continue,
	BfToken_Decltype,
	BfToken_Default,
	BfToken_Defer,
	BfToken_Delegate,
	BfToken_Delete,
	BfToken_Do,
	BfToken_Else,
	BfToken_Enum,
	BfToken_Explicit,
	BfToken_Extern,
	BfToken_Extension,
	BfToken_Fallthrough,	
	BfToken_Finally,
	BfToken_Fixed,
	BfToken_For,
	BfToken_Function,
	BfToken_Goto,
	BfToken_If,	
	BfToken_Implicit,
	BfToken_In,	
	BfToken_Inline,
	BfToken_Interface,
	BfToken_Internal,
	BfToken_Is,
	BfToken_Let,
	BfToken_Mixin,
	BfToken_Mut,
	BfToken_NameOf,
	BfToken_Namespace,
	BfToken_New,	
	BfToken_Null,
	BfToken_Nullable,
	BfToken_Operator,
	BfToken_Out,
	BfToken_Override,
	BfToken_Params,	
	BfToken_Private,
	BfToken_Protected,
	BfToken_Public,
	BfToken_ReadOnly,
	BfToken_Repeat,
	BfToken_Ref,
	BfToken_RetType,
	BfToken_Return,	
	BfToken_Scope,
	BfToken_Sealed,
	BfToken_SizeOf,	
	BfToken_Stack,
	BfToken_Static,
	BfToken_StrideOf,
	BfToken_Struct,
	BfToken_Switch,
	BfToken_This,
	BfToken_Throw,
	BfToken_Try,
	BfToken_TypeAlias,
	BfToken_TypeOf,
	BfToken_Unchecked,
	BfToken_Unsigned,
	BfToken_Using,
	BfToken_Var,
	BfToken_Virtual,
	BfToken_Volatile,
	BfToken_When,
	BfToken_Where,	
	BfToken_While,	
	BfToken_Yield,
	BfToken_AssignEquals,	
	BfToken_CompareEquals,
	BfToken_CompareStrictEquals,
	BfToken_CompareNotEquals,
	BfToken_CompareStrictNotEquals,
	BfToken_LessEquals,
	BfToken_GreaterEquals,
	BfToken_Spaceship,
	BfToken_PlusEquals,
	BfToken_MinusEquals,
	BfToken_MultiplyEquals,

	BfToken_DivideEquals,
	BfToken_ModulusEquals,
	BfToken_ShiftLeftEquals,
	BfToken_ShiftRightEquals,
	BfToken_AndEquals,
	BfToken_OrEquals,
	BfToken_XorEquals,
	BfToken_NullCoalsceEquals,
	BfToken_LBrace,
	BfToken_RBrace,
	BfToken_LParen,
	BfToken_RParen,
	BfToken_LBracket,
	BfToken_RBracket,
	BfToken_LChevron,
	BfToken_RChevron,
	BfToken_LDblChevron,
	BfToken_RDblChevron,
	BfToken_Semicolon,	
	BfToken_Colon,
	BfToken_Comma,
	BfToken_Dot,
	BfToken_DotDot,
	BfToken_DotDotDot,
	BfToken_QuestionDot,
	BfToken_QuestionLBracket,
	BfToken_AutocompleteDot,
	BfToken_Plus,
	BfToken_Minus,
	BfToken_DblPlus,
	BfToken_DblMinus,	
	BfToken_Star,
	BfToken_ForwardSlash,	
	BfToken_Modulus,
	BfToken_Ampersand,
	BfToken_At,
	BfToken_DblAmpersand,
	BfToken_Bar,
	BfToken_DblBar,
	BfToken_Bang,
	BfToken_Carat,
	BfToken_Tilde,
	BfToken_Question,
	BfToken_DblQuestion,
	BfToken_Arrow,
	BfToken_FatArrow,
};

class BfAstNode;
class BfScopeNode;
class BfNewNode;
class BfLabeledBlock;
class BfGenericArgumentsNode;
class BfStatement;
class BfLabelableStatement;
class BfExpression;
class BfExpressionStatement;
class BfAttributedExpression;
class BfAttributedStatement;
class BfLiteralExpression;
class BfBlock;
class BfBlockExtension;
class BfRootNode;
class BfErrorNode;
class BfTokenNode;
class BfIdentifierNode;
class BfAttributedIdentifierNode;
class BfQualifiedNameNode;
class BfNamespaceDeclaration;
class BfTypeDeclaration;
class BfTypeAliasDeclaration;
class BfMethodDeclaration;
class BfOperatorDeclaration;
class BfFieldDeclaration;
class BfEnumCaseDeclaration;
class BfParameterDeclaration;
class BfForStatement;
class BfUsingStatement;
class BfDoStatement;
class BfRepeatStatement;
class BfWhileStatement;
class BfMemberDeclaration;
class BfTypeReference;
class BfParameterDeclaration;
class BfVariableDeclaration;
class BfLocalMethodDeclaration;
class BfScopedInvocationTarget;
class BfInvocationExpression;
class BfDeferStatement;
class BfReturnStatement;
class BfYieldStatement;
class BfUnaryOperatorExpression;
class BfBinaryOperatorExpression;
class BfArrayTypeRef;
class BfPointerTypeRef;
class BfDotTypeReference;
class BfVarTypeReference;
class BfVarRefTypeReference;
class BfLetTypeReference;
class BfGenericInstanceTypeRef;
class BfTupleTypeRef;
class BfDelegateTypeRef;
class BfDeclTypeRef;
class BfCommentNode;
class BfIfStatement;
class BfParenthesizedExpression;
class BfTupleExpression;
class BfAssignmentExpression;
class BfNamedTypeReference;
class BfObjectCreateExpression;
class BfBoxExpression;
class BfDelegateBindExpression;
class BfLambdaBindExpression;
class BfCastExpression;
class BfGenericParamsDeclaration;
class BfThisExpression;
class BfBaseExpression;
class BfMixinExpression;
class BfTryStatement;
class BfCatchStatement;
class BfFinallyStatement;
class BfCheckedStatement;
class BfUncheckedStatement;
class BfBreakStatement;
class BfContinueStatement;
class BfFallthroughStatement;
class BfThrowStatement;
class BfDeleteStatement;
class BfIndexerExpression;
class BfMemberReferenceExpression;
class BfDynamicCastExpression;
class BfCheckTypeExpression;
class BfConstructorDeclaration;
class BfDestructorDeclaration;
class BfQualifiedTypeReference;
class BfUsingDirective;
class BfUsingStaticDirective;
class BfPropertyMethodDeclaration;
class BfPropertyBodyExpression;
class BfPropertyDeclaration;
class BfIndexerDeclaration;
class BfPreprocesorIgnoredSectionNode;
class BfPreprocessorNode;
class BfPreprocessorDefinedExpression;
class BfTypeOfExpression;
class BfEnumCaseBindExpression;
class BfSwitchCase;
class BfCaseExpression;
class BfWhenExpression;
class BfSwitchStatement;
class BfForEachStatement;
class BfTypedValueExpression;
class BfTypeAttrExpression;
class BfSizeOfExpression;
class BfAlignOfExpression;
class BfStrideOfExpression;
class BfDefaultExpression;
class BfUninitializedExpression;
class BfConditionalExpression;
class BfInitializerExpression;
class BfCollectionInitializerExpression;
class BfSizedArrayCreateExpression;
class BfEmptyStatement;
class BfGenericOperatorConstraint;
class BfGenericConstraintsDeclaration;
class BfAttributeDirective;
class BfNullableTypeRef;
class BfRefTypeRef;
class BfModifiedTypeRef;
class BfConstTypeRef;
class BfConstExprTypeRef;
class BfInlineAsmStatement;
class BfInlineAsmInstruction;
class BfFieldDtorDeclaration;

class BfStructuralVisitor
{
public:
	bool mCapturingChildRef;
	BfAstNode** mCurChildRef;

public:	
	void VisitMembers(BfBlock* node);
	void VisitChildNoRef(BfAstNode* nodeRef);
	void DoVisitChild(BfAstNode*& nodeRef);
	void AssertValidChildAddr(BfAstNode** nodeRef);
	template <typename T>
	void VisitChild(T& nodeRef)
	{
		/*if ((BfAstNode*)nodeRef == NULL)
			return;
		nodeRef->Accept(this);*/

		if (nodeRef == NULL)
			return;
		if (mCapturingChildRef)
		{
			mCurChildRef = ((BfAstNode**) &nodeRef);
			//AssertValidChildAddr(mCurChildRef);
		}
		nodeRef->Accept(this);
		mCurChildRef = NULL;
	}	
	template <typename T>
	void VisitChildNoRef(const T& nodeRef)
	{
		if ((BfAstNode*)nodeRef == NULL)
			return;
		nodeRef->Accept(this);
	};

public:
	BfStructuralVisitor();

	virtual void Visit(BfAstNode* bfAstNode) {}	
	virtual void Visit(BfErrorNode* bfErrorNode);
	virtual void Visit(BfScopeNode* scopeNode);
	virtual void Visit(BfNewNode* newNode);
	virtual void Visit(BfLabeledBlock* labeledBlock);
	virtual void Visit(BfExpression* expr);
	virtual void Visit(BfExpressionStatement* exprStmt);
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

	virtual void Visit(BfEmptyStatement* emptyStmt);	
	virtual void Visit(BfTokenNode* tokenNode);	
	virtual void Visit(BfTokenPairNode* tokenPairNode);	
	virtual void Visit(BfLiteralExpression* literalExpr);
	virtual void Visit(BfIdentifierNode* identifierNode);
	virtual void Visit(BfAttributedIdentifierNode* attrIdentifierNode);
	virtual void Visit(BfQualifiedNameNode* nameNode);	
	virtual void Visit(BfThisExpression* thisExpr);
	virtual void Visit(BfBaseExpression* baseExpr);
	virtual void Visit(BfMixinExpression* thisExpr);
	virtual void Visit(BfSizedArrayCreateExpression* createExpr);
	virtual void Visit(BfInitializerExpression* collectionInitExpr);
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
	virtual void Visit(BfDelegateTypeRef* typeRef);
	virtual void Visit(BfDeclTypeRef* declTypeRef);
	virtual void Visit(BfPointerTypeRef* typeRef);
	virtual void Visit(BfNullableTypeRef* typeRef);
	virtual void Visit(BfVariableDeclaration* varDecl);	
	virtual void Visit(BfLocalMethodDeclaration* methodDecl);
	virtual void Visit(BfParameterDeclaration* paramDecl);		
	virtual void Visit(BfTypeAttrExpression* typeAttrExpr);
	virtual void Visit(BfTypeOfExpression* typeOfExpr);
	virtual void Visit(BfSizeOfExpression* sizeOfExpr);	
	virtual void Visit(BfAlignOfExpression* alignOfExpr);
	virtual void Visit(BfStrideOfExpression* strideOfExpr);	
	virtual void Visit(BfDefaultExpression* defaultExpr);
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
	virtual void Visit(BfDeferStatement* deferStmt);
	virtual void Visit(BfConditionalExpression* condExpr);
	virtual void Visit(BfAssignmentExpression* assignExpr);
	virtual void Visit(BfParenthesizedExpression* parenExpr);
	virtual void Visit(BfTupleExpression* parenExpr);
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr);
	virtual void Visit(BfIndexerExpression* indexerExpr);
	virtual void Visit(BfUnaryOperatorExpression* binOpExpr);
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr);
	virtual void Visit(BfConstructorDeclaration* ctorDeclaration);
	virtual void Visit(BfDestructorDeclaration* dtorDeclaration);
	virtual void Visit(BfMethodDeclaration* methodDeclaration);
	virtual void Visit(BfOperatorDeclaration* operatorDeclaration);
	virtual void Visit(BfPropertyMethodDeclaration* propertyMethodDeclaration);
	virtual void Visit(BfPropertyBodyExpression* propertyBodyExpression);
	virtual void Visit(BfPropertyDeclaration* propertyDeclaration);
	virtual void Visit(BfIndexerDeclaration* indexerDeclaration);
	virtual void Visit(BfFieldDeclaration* fieldDeclaration);
	virtual void Visit(BfEnumCaseDeclaration* enumCaseDeclaration);
	virtual void Visit(BfFieldDtorDeclaration* fieldDtorDeclaration);
	virtual void Visit(BfTypeDeclaration* typeDeclaration);
	virtual void Visit(BfTypeAliasDeclaration* typeDeclaration);
	virtual void Visit(BfUsingDirective* usingDirective);
	virtual void Visit(BfUsingStaticDirective* usingDirective);
	virtual void Visit(BfNamespaceDeclaration* namespaceDeclaration);
	virtual void Visit(BfBlock* block);
	virtual void Visit(BfBlockExtension* block);
	virtual void Visit(BfRootNode* rootNode);
	virtual void Visit(BfInlineAsmStatement* asmStmt);
	virtual void Visit(BfInlineAsmInstruction* asmInst);
};

enum BfTypedValueKind
{
	BfTypedValueKind_Addr,
	BfTypedValueKind_ReadOnlyAddr,
	BfTypedValueKind_TempAddr,
	BfTypedValueKind_RestrictedTempAddr,
	BfTypedValueKind_ReadOnlyTempAddr,
	BfTypedValueKind_ThisAddr,
	BfTypedValueKind_BaseAddr,
	BfTypedValueKind_ReadOnlyThisAddr,	
	BfTypedValueKind_ReadOnlyBaseAddr,

	BfTypedValueKind_Value,
	BfTypedValueKind_ThisValue,	
	BfTypedValueKind_BaseValue,	
	BfTypedValueKind_ReadOnlyThisValue,	
	BfTypedValueKind_ReadOnlyBaseValue,
	BfTypedValueKind_MutableValue, // Only applicable for generic params
	BfTypedValueKind_SplatHead,
	BfTypedValueKind_ThisSplatHead,
	BfTypedValueKind_BaseSplatHead,	
	BfTypedValueKind_SplatHead_NeedsCasting,
	BfTypedValueKind_ParamsSplat,
	BfTypedValueKind_Params,

	BfTypedValueKind_NoValue,
	BfTypedValueKind_UntypedValue,
	BfTypedValueKind_GenericConstValue
};

class BfTypedValue
{
public:
	//llvm::Value* mValue;
	BfIRValue mValue;
	BfType* mType;
	BfTypedValueKind mKind; // Is address of variable	

public:
	BfTypedValue()
	{		
		mType = NULL;
		mKind = BfTypedValueKind_NoValue;
	}

	BfTypedValue(BfTypedValueKind kind)
	{
		mType = NULL;
		mKind = kind;
	}

	BfTypedValue(BfType* resolvedType)
	{
		mType = resolvedType;
		mKind = BfTypedValueKind_NoValue;
	}

	BfTypedValue(BfIRValue val, BfType* resolvedType, bool isAddr)
	{
		BF_ASSERT((!val) || (resolvedType != NULL));
		mValue = val;
		mType = resolvedType;		
		mKind = isAddr ? BfTypedValueKind_Addr : BfTypedValueKind_Value;
#ifdef _DEBUG
		//DbgCheckType();
#endif
		BF_ASSERT(val);

		/*if ((!val) && (resolvedType != NULL))
		{
			BF_ASSERT(IsValuelessType());
		}
		else if (IsValuelessType())
		{
			//BF_ASSERT(!val || IsAddr());
		}*/
	}

	BfTypedValue(BfIRValue val, BfType* resolvedType, BfTypedValueKind kind = BfTypedValueKind_Value)
	{	
		BF_ASSERT((!val) || (resolvedType != NULL));
		mValue = val;
		mType = resolvedType;
		mKind = kind;
#ifdef _DEBUG
		//DbgCheckType();
#endif
		if ((!val) && (resolvedType != NULL))
		{
			BF_ASSERT(IsValuelessType());
		}
		/*else if (IsValuelessType())
		{
			BF_ASSERT(!val || IsAddr());
		}*/
	}

	void DbgCheckType() const;
	bool IsValuelessType() const;

	bool HasType() const
	{
		return mType != NULL;
	}

	bool IsStatic() const
	{
		return !mValue;
	}

	bool IsAddr() const
	{
		return (mKind < BfTypedValueKind_Value);
	}

	bool IsTempAddr() const
	{
		return ((mKind == BfTypedValueKind_ReadOnlyTempAddr) || (mKind == BfTypedValueKind_RestrictedTempAddr) || (mKind == BfTypedValueKind_TempAddr));
	}

	bool IsReadOnly() const
	{
		switch (mKind)
		{
		case BfTypedValueKind_ReadOnlyAddr:
		case BfTypedValueKind_ReadOnlyTempAddr:
		case BfTypedValueKind_ReadOnlyThisValue:
		case BfTypedValueKind_ReadOnlyBaseValue:
		case BfTypedValueKind_ReadOnlyThisAddr:
		case BfTypedValueKind_ReadOnlyBaseAddr:
		case BfTypedValueKind_MutableValue: // 'mutable' means we can call mut methods, not that we can assign to it
		case BfTypedValueKind_SplatHead:
		case BfTypedValueKind_ThisSplatHead:
		case BfTypedValueKind_BaseSplatHead:
		case BfTypedValueKind_ParamsSplat:
		case BfTypedValueKind_Params:
			return true;
		default:
			return false;
		}

		return false;
	}

	bool IsThis() const
	{
		return (mKind == BfTypedValueKind_ThisValue) || (mKind == BfTypedValueKind_ThisAddr) || (mKind == BfTypedValueKind_ReadOnlyThisValue) || 
			(mKind == BfTypedValueKind_ReadOnlyThisAddr) || (mKind == BfTypedValueKind_ThisSplatHead);
	}

	bool IsBase() const
	{
		return (mKind == BfTypedValueKind_BaseValue) || (mKind == BfTypedValueKind_BaseAddr) || (mKind == BfTypedValueKind_ReadOnlyBaseValue) || 
			(mKind == BfTypedValueKind_ReadOnlyBaseAddr) || (mKind == BfTypedValueKind_BaseSplatHead);
	}

	void ToBase()
	{
		BF_ASSERT(IsThis());
		mKind = (BfTypedValueKind)((int)mKind + 1);
	}

	void ToThis()
	{
		BF_ASSERT(IsBase());
		mKind = (BfTypedValueKind)((int)mKind - 1);
	}

	bool IsSplat() const
	{
		return (mKind >= BfTypedValueKind_SplatHead) && (mKind <= BfTypedValueKind_ParamsSplat);
	}
	
	bool IsUntypedValue() const
	{
		return (mKind == BfTypedValueKind_UntypedValue);
	}

	bool IsParams()
	{
		return (mKind == BfTypedValueKind_ParamsSplat) || (mKind == BfTypedValueKind_Params);
	}

	operator bool() const
	{
		//return (mKind != BfTypedValueKind_NoValue) && ((mValue) || ((mType != NULL) && (IsValuelessType())));
		return (mKind != BfTypedValueKind_NoValue);
	}

	void MakeReadOnly()
	{
		switch (mKind)
		{
		case BfTypedValueKind_Addr:
			mKind = BfTypedValueKind_ReadOnlyAddr;
			break;
		case BfTypedValueKind_TempAddr:
		case BfTypedValueKind_RestrictedTempAddr:
			mKind = BfTypedValueKind_ReadOnlyTempAddr;
			break;
		default:
			break;
		}
	}

	bool CanModify() const;
};

#define BF_AST_TYPE(name, TBase) \
	static BfAstTypeInfo sTypeInfo;\
	static void ClassAccept(BfAstNode* node, BfStructuralVisitor* bfVisitor) { bfVisitor->Visit((name*)node); } \
	TBase* ToBase() { return (TBase*)this; } \
	name() { InitWithTypeId(sTypeInfo.mTypeId); }

#ifdef BF_AST_DO_IMPL
#define BF_AST_DECL(name, TBase) \
	BfAstTypeInfo name::sTypeInfo(#name, &TBase::sTypeInfo, &name::ClassAccept);
#else
#define BF_AST_DECL(name, TBase)
#endif

class BfAstNode;

template <typename T>
class BfChunkedArray
{
public:
	static const int sLeafSize = 8;

	T** mRoots;
	int mSize;

public:
	BfChunkedArray()
	{
		mSize = 0;
	}

	int GetRootCount()
	{
		return (mSize + sLeafSize - 1) / sLeafSize;
	}

	void Add(T val, BfAstAllocator* bumpAlloc)
	{
		int idx = mSize;
		if ((mSize % sLeafSize) == 0)
		{
			int rootCount = GetRootCount();
			mSize++;
			int newRootCount = GetRootCount();
			if (rootCount != newRootCount)
			{
				T** newRoots = (T**)bumpAlloc->AllocBytes(newRootCount * sizeof(T**), sizeof(T**));
				memcpy(newRoots, mRoots, rootCount * sizeof(T*));
				mRoots = newRoots;
			}
			mRoots[idx / sLeafSize] = (T*)bumpAlloc->AllocBytes(sLeafSize * sizeof(T*), sizeof(T*));
		}
		else
			mSize++;
		mRoots[idx / sLeafSize][idx % sLeafSize] = val;
	}

	bool IsEmpty()
	{
		return mSize == 0;
	}

	void SetSize(int size)
	{
		BF_ASSERT(size <= mSize);
		mSize = size;
	}

	T& operator[](int idx)
	{
		return mRoots[idx / sLeafSize][idx % sLeafSize];
	}

	T Get(int idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return (T)0;
		return mRoots[idx / sLeafSize][idx % sLeafSize];
	}

	T GetLast()
	{
		if (mSize == 0)
			return (T)0;
		return (*this)[mSize - 1];
	}

	T GetFirst()
	{
		if (mSize == 0)
			return (T)0;
		return (*this)[0];
	}
};

template <typename T>
class BfDebugArray
{
public:
	static const int STATIC_SIZE = 1024;

	Array<T> mElements;
	int mSize;

public:
	BfDebugArray()
	{
		mSize = 0;
	}

	void Add(T val, BfAstAllocator* bumpAlloc)
	{
		mElements.push_back(val);
		mSize++;
	}

	bool IsEmpty()
	{
		return mSize == 0;
	}

	void SetSize(int size)
	{
		BF_ASSERT(size <= mSize);
		mSize = size;
	}

	T& operator[](int idx)
	{
		return mElements[idx];
	}

	T Get(int idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return (T)0;
		return mElements[idx];
	}

	T GetLast()
	{
		if (mSize == 0)
			return (T)0;
		return mElements[mSize - 1];
	}

	T GetFirst()
	{
		if (mSize == 0)
			return (T)0;
		return (*this)[0];
	}
};

template <typename T>
class BfDeferredSizedArray : public llvm::SmallVector<T, 8>
{
public:
	BfSizedArray<T>* mSizedArray;
	BfAstAllocator* mAlloc;

public:
	BfDeferredSizedArray(BfSizedArray<T>& arr, BfAstAllocator* alloc)
	{
		mSizedArray = &arr;
		mAlloc = alloc;
	}

	~BfDeferredSizedArray()
	{		
		mSizedArray->mSize = (int)this->size();
		if (mSizedArray->mSize > 0)
		{
			mSizedArray->mVals = (T*)mAlloc->AllocBytes(mSizedArray->mSize * sizeof(T), sizeof(T));
			memcpy(mSizedArray->mVals, &(*this)[0], mSizedArray->mSize * sizeof(T));
		}
	}
};

#ifdef BF_USE_NEAR_NODE_REF
#define ASTREF(T) BfNearNodeRef<T>
#else
#define ASTREF(T) T
#endif

template <typename T, typename T2>
static void BfSizedArrayInitIndirect(BfSizedArray<T>& sizedArray, const SizedArrayImpl<T2>& vec, BfAstAllocator* alloc)
{	
	sizedArray.mSize = (int)vec.size();
	BF_ASSERT(sizedArray.mSize >= 0);
	if (sizedArray.mSize > 0)
	{
		sizedArray.mVals = (T*)alloc->AllocBytes(sizedArray.mSize * sizeof(T), sizeof(T));
		for (int i = 0; i < sizedArray.mSize; i++)
			sizedArray.mVals[i] = vec[i];
	}	
}

template <typename T>
class BfDeferredAstSizedArray : public SizedArray<T, 8>
{
public:	
	BfSizedArray<ASTREF(T)>* mSizedArray;
	BfAstAllocator* mAlloc;

public:
	BfDeferredAstSizedArray(BfSizedArray<ASTREF(T)>& arr, BfAstAllocator* alloc)
	{		
		mSizedArray = &arr;
		mAlloc = alloc;		
	}

	~BfDeferredAstSizedArray()
	{		
		BfSizedArrayInitIndirect(*mSizedArray, *this, mAlloc);		
	}
};

template <typename T>
class BfDeferredAstNodeSizedArray : public SizedArray<T, 8>
{
public:
	BfAstNode* mParentNode;
	BfSizedArray<ASTREF(T)>* mSizedArray;
	BfAstAllocator* mAlloc;

public:
	BfDeferredAstNodeSizedArray(BfAstNode* parentNode, BfSizedArray<ASTREF(T)>& arr, BfAstAllocator* alloc)
	{
		mParentNode = parentNode;
		mSizedArray = &arr;
		mAlloc = alloc;
	}

	~BfDeferredAstNodeSizedArray()
	{
		BfSizedArrayInitIndirect(*mSizedArray, *this, mAlloc);
		if (!this->mSizedArray->IsEmpty())
		{
			int endPos = this->mSizedArray->back()->mSrcEnd;
			if (endPos > this->mParentNode->mSrcEnd)
				this->mParentNode->mSrcEnd = endPos;
		}
	}
};

typedef void(*BfAstAcceptFunc)(BfAstNode* node, BfStructuralVisitor* visitor);

class BfAstTypeInfo
{
public:
	const char* mName;
	BfAstTypeInfo* mBaseType;
	Array<BfAstTypeInfo*> mDerivedTypes;
	uint8 mTypeId;
	uint8 mFullDerivedCount; // Including all ancestors
	BfAstAcceptFunc mAcceptFunc;

	BfAstTypeInfo(const char* name, BfAstTypeInfo* baseType, BfAstAcceptFunc acceptFunc);

public:
	static void Init();
};

#ifdef BF_AST_COMPACT
struct BfAstInfo
{
	int mTriviaStart;
	int mSrcStart;
	int mSrcEnd;
	uint8 mTypeId;
	BfToken mToken;
};
#endif

class BfAstNode
{
public:
	static BfAstTypeInfo sTypeInfo;	
	
#ifndef BF_AST_ALLOCATOR_USE_PAGES
	BfSourceData* mSourceData;
#endif
#ifdef BF_AST_HAS_PARENT_MEMBER
	BfAstNode* mParent;	
#endif

#ifdef BF_AST_COMPACT
	union
	{
		struct
		{			
			uint8 mCompact_TriviaLen;
			uint8 mCompact_SrcLen;
			uint8 mCompact_TypeId;
			BfToken mCompact_Token;
			int mCompact_SrcStart : 31;
			int mIsCompact : 1; 
		};
		BfAstInfo* mAstInfo;
	};
#else
	int mTriviaStart;
	int mSrcStart;
	int mSrcEnd;	
	uint8 mTypeId;
	BfToken mToken;
#endif

public:	
	BfAstNode()
	{
#ifdef BF_AST_COMPACT
		// Nothing
		mIsCompact = true;
#else
		//mParent = NULL;
		mTriviaStart = -1;
		mSrcStart = 0x7FFFFFFF;
		mSrcEnd = 0;
		//mSrcEnd = 0;
#endif
	}

#ifdef BF_AST_VTABLE
	virtual ~BfAstNode()
	{
	}
#endif
	
	void RemoveSelf();
	void DeleteSelf();
	void RemoveNextSibling();
	void DeleteNextSibling();
	bool IsTemporary();
	int GetStartCharId();
	BfSourceData* GetSourceData();
	BfParserData* GetParserData();
	BfParser* GetParser();
	bool IsFromParser(BfParser* parser);
	String ToString();
	StringView ToStringView();
	void ToString(StringImpl& str);
	bool Equals(const StringImpl& str);
	bool Equals(const char* str);
	void Init(BfParser* bfParser);
	void Accept(BfStructuralVisitor* bfVisitor);
	static void ClassAccept(BfAstNode* node, BfStructuralVisitor* bfVisitor) { bfVisitor->Visit(node); }
	bool LocationEquals(BfAstNode* otherNode);
	bool LocationEndEquals(BfAstNode* otherNode);
	void Add(BfAstNode* bfAstNode);
	bool IsMissingSemicolon();
	bool IsExpression();
	bool WantsWarning(int warningNumber);
	
	template <typename T>
	bool IsA()
	{		
		return (uint)GetTypeId() - (uint)T::sTypeInfo.mTypeId <= (uint)T::sTypeInfo.mFullDerivedCount;		
	}

	template <typename T>
	bool IsExact()
	{		
		return (uint)GetTypeId() == (uint)T::sTypeInfo.mTypeId;
	}

#ifdef BF_AST_COMPACT
	BfAstInfo* AllocAstInfo();

	void InitEmpty()
	{		
		mIsCompact = true;
		mCompact_SrcStart = 0;
		mCompact_SrcLen = 0;
		mCompact_TriviaLen = 0;
	}

	void InitWithTypeId(int typeId)
	{
		mCompact_TypeId = typeId;
	}

	bool IsInitialized()
	{
		return (!mIsCompact) || (mCompact_SrcLen != 0);
	}

	BfToken GetToken()
	{
		if (mIsCompact)
			return mCompact_Token;
		return mAstInfo->mToken;
	}

	void SetToken(BfToken token)
	{
		if (mIsCompact)
			mCompact_Token = token;
		else
			mAstInfo->mToken = token;
	}

	void Init(int triviaStart, int srcStart, int srcEnd)
	{
		int triviaLen = srcStart - triviaStart;
		int srcLen = srcEnd - srcStart;
		if ((triviaLen <= 255) && (srcLen <= 255))		
		{
			mCompact_SrcStart = srcStart;
			mIsCompact = 1;
			mCompact_TriviaLen = (uint8)triviaLen;
			mCompact_SrcLen = (uint8)srcLen;
		}
		else
		{
			auto astInfo = AllocAstInfo();
			astInfo->mTypeId = mCompact_TypeId;
			astInfo->mToken = mCompact_Token;
			astInfo->mTriviaStart = triviaStart;
			astInfo->mSrcStart = srcStart;
			astInfo->mSrcEnd = srcEnd;
			mAstInfo = astInfo;
		}		
	}	

	int GetTypeId()
	{
		if (mIsCompact)
			return mCompact_TypeId;
		return mAstInfo->mTypeId;
	}

	void GetSrcPositions(int& triviaStart, int& srcStart, int& srcEnd)
	{
		if (mIsCompact)
		{
			srcStart = mCompact_SrcStart;
			srcEnd = srcStart + mCompact_SrcLen;
			triviaStart = srcStart - mCompact_TriviaLen;
		}
		else
		{
			triviaStart = mAstInfo->mTriviaStart;
			srcStart = mAstInfo->mSrcStart;
			srcEnd = mAstInfo->mSrcEnd;
		}
	}

	int GetTriviaStart()
	{
		if (mIsCompact)
			return mCompact_SrcStart - mCompact_TriviaLen;
		return mAstInfo->mTriviaStart;
	}

	void SetTriviaStart(int triviaStart)
	{
		if (mIsCompact)
		{
			int triviaLen = mCompact_SrcStart - triviaStart;
			if (triviaLen <= 255)
			{
				mCompact_TriviaLen = (uint8)triviaLen;
				return;
			}
			
			auto astInfo = AllocAstInfo();
			astInfo->mTypeId = mCompact_TypeId;
			astInfo->mToken = mCompact_Token;
			astInfo->mSrcStart = mCompact_SrcStart;
			astInfo->mSrcEnd = mCompact_SrcStart + mCompact_SrcLen;
			mAstInfo = astInfo;
		}
		
		mAstInfo->mTriviaStart = triviaStart;
	}

	int GetSrcStart()
	{
		if (mIsCompact)
			return mCompact_SrcStart;
		return mAstInfo->mSrcStart;
	}

	void SetSrcStart(int srcStart)
	{
		if (mIsCompact)
		{
			int startAdjust = srcStart - mCompact_SrcStart;
			uint32 triviaLen = (uint32)((int)mCompact_TriviaLen + startAdjust);
			uint32 srcLen = (uint32)((int)mCompact_SrcLen - startAdjust);

			if ((triviaLen <= 255) && (srcLen <= 255))
			{
				mCompact_SrcStart = srcStart;
				mCompact_TriviaLen = (uint8)triviaLen;
				mCompact_SrcLen = (uint8)srcLen;
			}
			else
			{
				auto astInfo = AllocAstInfo();
				astInfo->mTypeId = mCompact_TypeId;
				astInfo->mSrcStart = srcStart;
				astInfo->mTriviaStart = srcStart - triviaLen;
				astInfo->mSrcEnd = srcStart + srcLen;
				mAstInfo = astInfo;
			}			
		}
		else
			mAstInfo->mSrcStart = srcStart;
	}

	int GetSrcEnd()
	{
		if (mIsCompact)
			return mCompact_SrcStart + mCompact_SrcLen;
		return mAstInfo->mSrcEnd;
	}

	void SetSrcEnd(int srcEnd)
	{
		if (mIsCompact)
		{
			int srcLen = srcEnd - mCompact_SrcStart;
			if (srcLen <= 255)
			{
				mCompact_SrcLen = (uint8)srcLen;
				return;
			}

			auto astInfo = AllocAstInfo();
			astInfo->mTypeId = mCompact_TypeId;
			astInfo->mSrcStart = mCompact_SrcStart;
			astInfo->mTriviaStart = mCompact_SrcStart - mCompact_TriviaLen;
			mAstInfo = astInfo;
		}

		mAstInfo->mSrcEnd = srcEnd;
	}

	void AdjustSrcEnd(BfAstNode* srcNode)
	{
		int srcEnd = srcNode->GetSrcEnd();
		if (srcEnd > GetSrcEnd())
			SetSrcEnd(srcEnd);
	}

	int GetSrcLength()
	{
		if (mIsCompact)
			return mCompact_SrcLen;
		return mAstInfo->mSrcEnd - mAstInfo->mSrcStart;
	}

	bool Contains(int srcPos)
	{
		if (mIsCompact)
			return (srcPos >= mCompact_SrcStart) && (srcPos < mCompact_SrcStart + mCompact_SrcLen);
		return (srcPos >= mAstInfo->mSrcStart) && (srcPos < mAstInfo->mSrcEnd);
	}
#else
	void InitEmpty()
	{
		mTriviaStart = 0;
		mSrcStart = 0;
		mSrcEnd = 0;		
	}

	void InitWithTypeId(int typeId)
	{
		mTypeId = typeId;
	}

	bool IsInitialized()
	{
		return mSrcStart != 0x7FFFFFFF;
	}

	void Init(int triviaStart, int srcStart, int srcEnd)
	{
		mTriviaStart = triviaStart;
		mSrcStart = srcStart;
		mSrcEnd = srcEnd;
	}

	BfToken GetToken()
	{
		return mToken;
	}

	void SetToken(BfToken token)
	{
		mToken = token;
	}

	int GetTypeId()
	{
		return mTypeId;
	}

	void GetSrcPositions(int& triviaStart, int& srcStart, int& srcEnd)
	{
		triviaStart = mTriviaStart;
		srcStart = mSrcStart;
		srcEnd = mSrcEnd;
	}

	int GetTriviaStart()
	{
		return mTriviaStart;
	}

	void SetTriviaStart(int triviaStart)
	{
		mTriviaStart = triviaStart;
	}

	int GetSrcStart()
	{
		return mSrcStart;
	}

	void SetSrcStart(int srcStart)
	{
		mSrcStart = srcStart;
	}

	int GetSrcEnd()
	{
		return mSrcEnd;
	}

	void SetSrcEnd(int srcEnd)
	{
		mSrcEnd = srcEnd;
	}

	void AdjustSrcEnd(BfAstNode* srcNode)
	{
		int srcEnd = srcNode->GetSrcEnd();
		if (srcEnd > GetSrcEnd())
			SetSrcEnd(srcEnd);
	}

	int GetSrcLength()
	{
		return mSrcEnd - mSrcStart;
	}

	bool Contains(int srcPos)
	{
		return (srcPos >= mSrcStart) && (srcPos < mSrcEnd);
	}

	bool Contains(int srcPos, int lenAdd, int startAdd)
	{
		return (srcPos >= mSrcStart + startAdd) && (srcPos < mSrcEnd + lenAdd);
	}
#endif


#ifdef BF_AST_HAS_PARENT_MEMBER
	template <typename T>
	T* FindParentOfType()
	{
		BfAstNode* checkParent = mParent;
		while (checkParent != NULL)
		{
			if (checkParent->IsA<T>())
				return (T*)checkParent;
			checkParent = checkParent->mParent;
		}
		return NULL;
	}
#endif

#ifdef BF_AST_HAS_PARENT_MEMBER
	template <typename T>
	static T* ZeroedAlloc()
	{
		T* val = new T();
		memset((uint8*)val + offsetof(T, mParent), 0, sizeof(T) - offsetof(T, mParent));
		return val;
	}
#else
	template <typename T>
	static T* ZeroedAlloc()
	{
		T* val = new T();
#ifdef BF_AST_COMPACT
		memset((uint8*)val + offsetof(T, mAstInfo), 0, sizeof(T) - offsetof(T, mAstInfo));
#else
		memset((uint8*)val + offsetof(T, mTriviaStart), 0, sizeof(T) - offsetof(T, mTriviaStart));
#endif
		val->InitWithTypeId(T::sTypeInfo.mTypeId);
		return val;
	}

	template <typename T>
	static void Zero(T* val)
	{		
#ifdef BF_AST_COMPACT
		memset((uint8*)val + offsetof(T, mAstInfo), 0, sizeof(T) - offsetof(T, mAstInfo));
#else
		memset((uint8*)val + offsetof(T, mTriviaStart), 0, sizeof(T) - offsetof(T, mTriviaStart));
#endif
		val->InitWithTypeId(T::sTypeInfo.mTypeId);		
	}
#endif
};
#ifdef BF_AST_DO_IMPL
BfAstTypeInfo BfAstNode::sTypeInfo("BfAstNode", NULL, &BfAstNode::ClassAccept);
#endif

template <typename T>
bool BfNodeIsA(BfAstNode* node) 
{ 
	if (node == NULL)
		return false;
	
	bool canCast = (uint)node->GetTypeId() - (uint)T::sTypeInfo.mTypeId <= (uint)T::sTypeInfo.mFullDerivedCount;
	return canCast;
}

template <typename T>
bool BfNodeIsExact(BfAstNode* node)
{
	if (node == NULL)
		return false;

	bool canCast = (uint)node->GetTypeId() == (uint)T::sTypeInfo.mTypeId;
	return canCast;
}

template <typename T>
T* BfNodeDynCast(BfAstNode* node)
{
	if (node == NULL)
		return NULL;	
	
	bool canCast = (uint)node->GetTypeId() - (uint)T::sTypeInfo.mTypeId <= (uint)T::sTypeInfo.mFullDerivedCount;
	//BF_ASSERT(canCast == (node->DynCast(T::TypeId) != NULL));
	return canCast ? (T*)node : NULL;
}

template <typename T>
T* BfNodeDynCastExact(BfAstNode* node)
{
	if (node == NULL)
		return NULL;
	
	bool canCast = node->GetTypeId() == T::sTypeInfo.mTypeId;
	//BF_ASSERT(canCast == (node->GetTypeId() == T::TypeId));
	return canCast ? (T*)node : NULL;
}

BfIdentifierNode* BfIdentifierCast(BfAstNode* node);
BfAstNode* BfNodeToNonTemporary(BfAstNode* node);

class BfStatement : public BfAstNode
{
public:
	BF_AST_TYPE(BfStatement, BfAstNode);

	BfTokenNode* mTrailingSemicolon;

//	bool IsMissingSemicolon();
// 	{
// 		return mTrailingSemicolon == false;
// 	}
};	BF_AST_DECL(BfStatement, BfAstNode);

class BfExpression : public BfAstNode
{
public:
	BF_AST_TYPE(BfExpression, BfAstNode);

// 	bool IsUsedAsStatement()
// 	{
// 		return mTrailingSemicolon != NULL;
// 	}

	bool VerifyIsStatement(BfPassInstance* passInstance, bool ignoreError = false);
};	BF_AST_DECL(BfExpression, BfAstNode);

class BfErrorNode : public BfExpression
{
public:
	BF_AST_TYPE(BfErrorNode, BfExpression);

	BfAstNode* mRefNode;
};	BF_AST_DECL(BfErrorNode, BfExpression);

class BfExpressionStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfExpressionStatement, BfStatement);
	BfExpression* mExpression;
};  BF_AST_DECL(BfExpressionStatement, BfStatement);

class BfBlockExtension : public BfAstNode
{
public:
	BF_AST_TYPE(BfBlockExtension, BfAstNode);

	BfSizedArray<ASTREF(BfAstNode*)> mChildArr;
};	BF_AST_DECL(BfBlockExtension, BfAstNode);

class BfBlock : public BfExpression
{
public:
	struct Iterator
	{
	public:
		ASTREF(BfAstNode*)* mPtr;
		int mValsLeft;

	public:
		Iterator()
		{
			mPtr = NULL;
			mValsLeft = 0;
		}

		Iterator(ASTREF(BfAstNode*)* ptr, int valsLeft)
		{
			mPtr = ptr;
			mValsLeft = valsLeft;
			if (mValsLeft == 0)
				mPtr = NULL;
		}

		Iterator& operator++()
		{
			BF_ASSERT(mValsLeft >= 0);
			mValsLeft--;
			mPtr++;
			if (mValsLeft == 0)
			{
				mPtr = NULL;
			}
			else
			{
				BfAstNode* curNode = *mPtr;
				if (auto blockExpr = BfNodeDynCastExact<BfBlockExtension>(*mPtr))
				{
					BF_ASSERT(mValsLeft == 1);
					mPtr = blockExpr->mChildArr.mVals;
					mValsLeft = blockExpr->mChildArr.mSize;
				}
			}
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{			
			return itr.mPtr != mPtr;
		}

		bool operator==(const Iterator& itr) const
		{		
			return itr.mPtr == mPtr;
		}

		ASTREF(BfAstNode*)& operator*()
		{
			return *mPtr;
		}

		BfAstNode* Get()
		{
			if (mValsLeft == 0)
				return NULL;
			return *mPtr;
		}

		bool IsLast()
		{
			return mValsLeft == 1;
		}
	};

public:
	BF_AST_TYPE(BfBlock, BfExpression);

	ASTREF(BfTokenNode*) mOpenBrace;
	ASTREF(BfTokenNode*) mCloseBrace;
	//BfDebugArray<BfAstNode*> mChildArr;
	BfSizedArray<ASTREF(BfAstNode*)> mChildArr;

public:	
	using BfAstNode::Init;
	void Init(const SizedArrayImpl<BfAstNode*>& vec, BfAstAllocator* alloc);
	BfAstNode* GetFirst();
	BfAstNode* GetLast();
	int GetSize();
	void SetSize(int wantSize);
// 	virtual bool IsMissingSemicolon() override
// 	{
// 		return false;
// 	}

	ASTREF(BfAstNode*)& operator[](int idx)
	{
#ifdef BF_USE_NEAR_NODE_REF
		BfSizedArray<ASTREF(BfAstNode*)>* childArr = &mChildArr;

		while (true)
		{
			if (idx < childArr->mSize - 1)
				return childArr->mVals[idx];
			if (idx == childArr->mSize - 1)
			{
				auto& checkNode = childArr->mVals[childArr->mSize - 1];
				if (!checkNode->IsA<BfBlockExtension>())
					return checkNode;
			}

			idx -= childArr->mSize - 1;
			BfBlockExtension* blockExt = (BfBlockExtension*)(BfAstNode*)childArr->mVals[childArr->mSize - 1];
			BF_ASSERT(blockExt->GetTypeId() == BfBlockExtension::TypeId);
			childArr = &blockExt->mChildArr;
		}		
#else
		return mChildArr.mVals[idx];
#endif
	}

	Iterator begin()
	{
		return Iterator(mChildArr.mVals, mChildArr.mSize);
	}

	Iterator end()
	{
		return Iterator(NULL, 0);
	}
};	BF_AST_DECL(BfBlock, BfExpression);

class BfTypedValueExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfTypedValueExpression, BfExpression);

	BfTypedValue mTypedValue;
	BfAstNode* mRefNode;

public:
	void Init(const BfTypedValue& typedValue)
	{
		mTypedValue = typedValue;
		mRefNode = NULL;
#ifdef BF_AST_HAS_PARENT_MEMBER
		mParent = NULL;		
#endif
	}
};	BF_AST_DECL(BfTypedValueExpression, BfExpression);

// Compound statements don't require semicolon termination
class BfCompoundStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfCompoundStatement, BfStatement);
};	BF_AST_DECL(BfCompoundStatement, BfStatement);

class BfLabelNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfLabelNode, BfAstNode);

	BfIdentifierNode* mLabel;
	BfTokenNode* mColonToken;
};	BF_AST_DECL(BfLabelNode, BfAstNode);

class BfLabelableStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfLabelableStatement, BfCompoundStatement);

	BfLabelNode* mLabelNode;
};	BF_AST_DECL(BfLabelableStatement, BfCompoundStatement);

class BfLabeledBlock : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfLabeledBlock, BfLabelableStatement);

	BfBlock* mBlock;
};	BF_AST_DECL(BfLabeledBlock, BfLabelableStatement);

enum BfBinaryOp
{
	BfBinaryOp_None,
	BfBinaryOp_Add,
	BfBinaryOp_Subtract,
	BfBinaryOp_Multiply,
	BfBinaryOp_Divide,
	BfBinaryOp_Modulus,
	BfBinaryOp_BitwiseAnd,
	BfBinaryOp_BitwiseOr,
	BfBinaryOp_ExclusiveOr,
	BfBinaryOp_LeftShift,
	BfBinaryOp_RightShift,
	BfBinaryOp_Equality,
	BfBinaryOp_StrictEquality,
	BfBinaryOp_InEquality,
	BfBinaryOp_StrictInEquality,
	BfBinaryOp_GreaterThan,
	BfBinaryOp_LessThan,
	BfBinaryOp_GreaterThanOrEqual,
	BfBinaryOp_LessThanOrEqual,
	BfBinaryOp_Compare,
	BfBinaryOp_ConditionalAnd,
	BfBinaryOp_ConditionalOr,
	BfBinaryOp_NullCoalesce,
	BfBinaryOp_Is,
	BfBinaryOp_As
};

enum BfAssignmentOp
{
	BfAssignmentOp_None,
	BfAssignmentOp_Assign,
	BfAssignmentOp_Add,
	BfAssignmentOp_Subtract,
	BfAssignmentOp_Multiply,
	BfAssignmentOp_Divide,
	BfAssignmentOp_Modulus,
	BfAssignmentOp_ShiftLeft,
	BfAssignmentOp_ShiftRight,
	BfAssignmentOp_BitwiseAnd,
	BfAssignmentOp_BitwiseOr,
	BfAssignmentOp_ExclusiveOr,
	BfAssignmentOp_NullCoalesce
};

enum BfUnaryOp
{
	BfUnaryOp_None,
	BfUnaryOp_AddressOf,
	BfUnaryOp_Dereference,
	BfUnaryOp_Negate,
	BfUnaryOp_Not,
	BfUnaryOp_Positive,
	BfUnaryOp_InvertBits,
	BfUnaryOp_Increment,
	BfUnaryOp_Decrement,
	BfUnaryOp_PostIncrement,
	BfUnaryOp_PostDecrement,
	BfUnaryOp_NullConditional,
	BfUnaryOp_Ref,
	BfUnaryOp_Out,
	BfUnaryOp_Mut,
	BfUnaryOp_Params,	
};

class BfTokenNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfTokenNode, BfAstNode);	
};	BF_AST_DECL(BfTokenNode, BfAstNode);

class BfScopeNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfScopeNode, BfAstNode);

	BfTokenNode* mScopeToken;
	BfTokenNode* mColonToken;
	BfAstNode* mTargetNode; // . : or identifier
	BfAttributeDirective* mAttributes;
};	BF_AST_DECL(BfScopeNode, BfAstNode);

class BfNewNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfNewNode, BfAstNode);

	BfTokenNode* mNewToken;
	BfTokenNode* mColonToken;	
	BfAstNode* mAllocNode; // Expression or BfScopedInvocationTarget
	BfAttributeDirective* mAttributes;
};	BF_AST_DECL(BfNewNode, BfAstNode);

enum BfCommentKind
{
	BfCommentKind_Line,
	BfCommentKind_Block,
	BfCommentKind_Documentation_Block_Pre,
	BfCommentKind_Documentation_Line_Pre,
	BfCommentKind_Documentation_Block_Post,
	BfCommentKind_Documentation_Line_Post,			
};

class BfCommentNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfCommentNode, BfAstNode);
	BfCommentKind mCommentKind;
};	BF_AST_DECL(BfCommentNode, BfAstNode);

class BfPreprocesorIgnoredSectionNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfPreprocesorIgnoredSectionNode, BfAstNode);
};	BF_AST_DECL(BfPreprocesorIgnoredSectionNode, BfAstNode);

class BfPreprocessorNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfPreprocessorNode, BfAstNode);

	BfIdentifierNode* mCommand;
	BfBlock* mArgument;	
};	BF_AST_DECL(BfPreprocessorNode, BfAstNode);

class BfPreprocessorDefinedExpression : public BfExpression
{ 
public:
	BF_AST_TYPE(BfPreprocessorDefinedExpression, BfExpression);

	BfIdentifierNode*     mIdentifier;	
};	BF_AST_DECL(BfPreprocessorDefinedExpression, BfExpression);

class BfReplaceNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfReplaceNode, BfAstNode);
};	BF_AST_DECL(BfReplaceNode, BfAstNode);

//TODO: Should we have a seperate BfIdentifierExpression?
class BfIdentifierNode : public BfExpression
{
public:
	BF_AST_TYPE(BfIdentifierNode, BfExpression);
};	BF_AST_DECL(BfIdentifierNode, BfExpression);

class BfAttributedIdentifierNode : public BfExpression
{
public:
	BF_AST_TYPE(BfAttributedIdentifierNode, BfExpression);

	BfIdentifierNode* mIdentifier;
	BfAttributeDirective* mAttributes;	
};	BF_AST_DECL(BfAttributedIdentifierNode, BfExpression);

class BfQualifiedNameNode : public BfIdentifierNode
{
public:
	BF_AST_TYPE(BfQualifiedNameNode, BfIdentifierNode);

	ASTREF(BfIdentifierNode*) mLeft;
	ASTREF(BfTokenNode*) mDot;
	ASTREF(BfIdentifierNode*) mRight;
};	BF_AST_DECL(BfQualifiedNameNode, BfIdentifierNode);

class BfUsingDirective : public BfStatement
{
public:
	BF_AST_TYPE(BfUsingDirective, BfStatement);

	BfTokenNode* mUsingToken;	
	BfIdentifierNode* mNamespace;	
};	BF_AST_DECL(BfUsingDirective, BfStatement);

class BfUsingStaticDirective : public BfStatement
{
public:
	BF_AST_TYPE(BfUsingStaticDirective, BfStatement);

	BfTokenNode* mUsingToken;
	BfTokenNode* mStaticToken;
	BfTypeReference* mTypeRef;
};	BF_AST_DECL(BfUsingStaticDirective, BfStatement);

class BfAttributeTargetSpecifier : public BfAstNode
{
public:
	BF_AST_TYPE(BfAttributeTargetSpecifier, BfAstNode);

	ASTREF(BfAstNode*) mTargetToken;
	ASTREF(BfTokenNode*) mColonToken;
};	BF_AST_DECL(BfAttributeTargetSpecifier, BfAstNode);

class BfAttributeDirective : public BfAstNode
{
public:	
	BF_AST_TYPE(BfAttributeDirective, BfAstNode);

	ASTREF(BfTokenNode*) mAttrOpenToken; // [ @ ,
	ASTREF(BfTokenNode*) mAttrCloseToken;
	ASTREF(BfAstNode*) mAttributeTargetSpecifier;

	ASTREF(BfTypeReference*) mAttributeTypeRef;
	ASTREF(BfTokenNode*) mCtorOpenParen;
	ASTREF(BfTokenNode*) mCtorCloseParen;
	BfSizedArray<ASTREF(BfExpression*)> mArguments;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;

	ASTREF(BfAttributeDirective*) mNextAttribute;

public:
	bool Contains(const StringImpl& findName);
};	BF_AST_DECL(BfAttributeDirective, BfAstNode);

class BfNamespaceDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfNamespaceDeclaration, BfAstNode);

	BfTokenNode* mNamespaceNode;
	BfIdentifierNode* mNameNode;
	BfBlock* mBlock;
};	BF_AST_DECL(BfNamespaceDeclaration, BfAstNode);

class BfBinaryOperatorExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfBinaryOperatorExpression, BfExpression);

	BfBinaryOp mOp;
	ASTREF(BfTokenNode*) mOpToken;
	ASTREF(BfExpression*) mLeft;
	ASTREF(BfExpression*) mRight;
};	BF_AST_DECL(BfBinaryOperatorExpression, BfExpression);

class BfConditionalExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfConditionalExpression, BfExpression);

	BfExpression* mConditionExpression;
	BfTokenNode* mQuestionToken;
	BfExpression* mTrueExpression;
	BfTokenNode* mColonToken;
	BfExpression* mFalseExpression;
};	BF_AST_DECL(BfConditionalExpression, BfExpression);

class BfAssignmentExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfAssignmentExpression, BfExpression);

	BfAssignmentOp mOp;
	ASTREF(BfTokenNode*) mOpToken;
	ASTREF(BfExpression*) mLeft;
	ASTREF(BfExpression*) mRight;
};	BF_AST_DECL(BfAssignmentExpression, BfExpression);

class BfMethodBoundExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfMethodBoundExpression, BfExpression);
};	BF_AST_DECL(BfMethodBoundExpression, BfExpression);

class BfIndexerExpression : public BfMethodBoundExpression
{
public:
	BF_AST_TYPE(BfIndexerExpression, BfMethodBoundExpression);

	BfExpression* mTarget;
	BfTokenNode* mOpenBracket;
	BfTokenNode* mCloseBracket;
	BfSizedArray<ASTREF(BfExpression*)> mArguments;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;
};	BF_AST_DECL(BfIndexerExpression, BfMethodBoundExpression);

class BfMemberReferenceExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfMemberReferenceExpression, BfExpression);

	ASTREF(BfTokenNode*) mDotToken;
	ASTREF(BfAstNode*) mTarget; // Can be expression or typeRef
	ASTREF(BfAstNode*) mMemberName; // Either or BfIdentiferNode or a BfLiteralNode (for tuple "name.0" type lookups)
};	BF_AST_DECL(BfMemberReferenceExpression, BfExpression);

class BfUnaryOperatorExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfUnaryOperatorExpression, BfExpression);

	BfUnaryOp mOp;
	ASTREF(BfTokenNode*) mOpToken;
	ASTREF(BfExpression*) mExpression;
};	BF_AST_DECL(BfUnaryOperatorExpression, BfExpression);

class BfMixinExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfMixinExpression, BfExpression);
};	BF_AST_DECL(BfMixinExpression, BfExpression);

class BfThisExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfThisExpression, BfExpression);
};	BF_AST_DECL(BfThisExpression, BfExpression);

class BfBaseExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfBaseExpression, BfExpression);
};	BF_AST_DECL(BfBaseExpression, BfExpression);

class BfLiteralExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfLiteralExpression, BfExpression);

	BfVariant mValue;
};	BF_AST_DECL(BfLiteralExpression, BfExpression);

class BfInitializerExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfInitializerExpression, BfExpression);

	BfExpression* mTarget;
	BfTokenNode* mOpenBrace;
	BfSizedArray<BfExpression*> mValues;
	BfSizedArray<BfTokenNode*> mCommas;
	BfTokenNode* mCloseBrace;
};  BF_AST_DECL(BfInitializerExpression, BfExpression);

class BfCollectionInitializerExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfCollectionInitializerExpression, BfExpression);

	BfTokenNode* mOpenBrace;
	BfSizedArray<ASTREF(BfExpression*)> mValues;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;
	BfTokenNode* mCloseBrace;	
};	BF_AST_DECL(BfCollectionInitializerExpression, BfExpression);

class BfSizedArrayCreateExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfSizedArrayCreateExpression, BfExpression);

	BfArrayTypeRef* mTypeRef;
	BfCollectionInitializerExpression* mInitializer;
};	BF_AST_DECL(BfSizedArrayCreateExpression, BfExpression);

class BfParenthesizedExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfParenthesizedExpression, BfExpression);

	BfTokenNode* mOpenParen;
	BfExpression* mExpression;
	BfTokenNode* mCloseParen;
};	BF_AST_DECL(BfParenthesizedExpression, BfExpression);

class BfTupleNameNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfTupleNameNode, BfAstNode);

	BfIdentifierNode* mNameNode;
	BfTokenNode* mColonToken;
};	BF_AST_DECL(BfTupleNameNode, BfAstNode);

class BfTupleExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfTupleExpression, BfExpression);

	BfTokenNode* mOpenParen;
	BfSizedArray<ASTREF(BfTupleNameNode*)> mNames;	
	BfSizedArray<ASTREF(BfExpression*)> mValues;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;
	ASTREF(BfTokenNode*) mCloseParen;
};	BF_AST_DECL(BfTupleExpression, BfExpression);

class BfWhenExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfWhenExpression, BfExpression);

	BfTokenNode* mWhenToken;
	BfExpression* mExpression;
};	BF_AST_DECL(BfWhenExpression, BfExpression);

class BfEnumCaseBindExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfEnumCaseBindExpression, BfExpression);

	BfTokenNode* mBindToken; // Either 'var' or 'let'
	BfAstNode* mEnumMemberExpr; // Either a BfMemberReferenceExpression or a BfIdentifierNode
	BfTupleExpression* mBindNames;
};	BF_AST_DECL(BfEnumCaseBindExpression, BfExpression);

class BfCaseExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfCaseExpression, BfExpression);

	BfTokenNode* mCaseToken;
	BfExpression* mCaseExpression;
	BfTokenNode* mEqualsNode;
	BfExpression* mValueExpression;
};	BF_AST_DECL(BfCaseExpression, BfExpression);

class BfSwitchCase : public BfAstNode
{
public:
	BF_AST_TYPE(BfSwitchCase, BfAstNode);

	BfTokenNode* mCaseToken;
	BfSizedArray<ASTREF(BfExpression*)> mCaseExpressions;
	BfSizedArray<ASTREF(BfTokenNode*)> mCaseCommas;
	BfTokenNode* mColonToken;	
	BfBlock* mCodeBlock; // May or may not have braces set
	BfTokenNode* mEndingToken; // Null, Fallthrough, or Break
	BfTokenNode* mEndingSemicolonToken;
};	BF_AST_DECL(BfSwitchCase, BfAstNode);

class BfSwitchStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfSwitchStatement, BfLabelableStatement);

	BfTokenNode* mSwitchToken;
	BfTokenNode* mOpenParen;
	BfExpression* mSwitchValue;
	BfTokenNode* mCloseParen;

	BfTokenNode* mOpenBrace;
	BfSizedArray<ASTREF(BfSwitchCase*)> mSwitchCases;
	BfSwitchCase* mDefaultCase;
	BfTokenNode* mCloseBrace;
};	BF_AST_DECL(BfSwitchStatement, BfLabelableStatement);

class BfIfStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfIfStatement, BfLabelableStatement);

	BfTokenNode* mIfToken;
	BfTokenNode* mOpenParen;
	BfExpression* mCondition;
	BfTokenNode* mCloseParen;
	BfAstNode* mTrueStatement;
	BfTokenNode* mElseToken;
	BfAstNode* mFalseStatement;
};	BF_AST_DECL(BfIfStatement, BfLabelableStatement);

class BfEmptyStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfEmptyStatement, BfStatement);
};	BF_AST_DECL(BfEmptyStatement, BfStatement);

class BfRootNode : public BfBlock
{
public:
	BF_AST_TYPE(BfRootNode, BfBlock);
};	BF_AST_DECL(BfRootNode, BfBlock);

class BfGenericConstraintsDeclaration;

class BfTypeDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfTypeDeclaration, BfAstNode);

	BfCommentNode* mDocumentation;
	BfAttributeDirective* mAttributes;
	BfTokenNode* mAbstractSpecifier;	
	BfTokenNode* mSealedSpecifier;	
	BfTokenNode* mProtectionSpecifier;
	BfTokenNode* mStaticSpecifier;
	BfTokenNode* mPartialSpecifier;
	BfTokenNode* mTypeNode;
	BfIdentifierNode* mNameNode;	
	BfAstNode* mDefineNode;		
	BfGenericParamsDeclaration* mGenericParams;
	BfGenericConstraintsDeclaration* mGenericConstraintsDeclaration;
	bool mIgnoreDeclaration;

	BfTokenNode* mColonToken;
	BfSizedArray<ASTREF(BfTypeReference*)> mBaseClasses;
	BfSizedArray<ASTREF(BfAstNode*)> mBaseClassCommas;
	
};	BF_AST_DECL(BfTypeDeclaration, BfAstNode);

class BfTypeAliasDeclaration : public BfTypeDeclaration
{
public:
	BF_AST_TYPE(BfTypeAliasDeclaration, BfTypeDeclaration);

	BfTokenNode* mEqualsToken;
	BfTypeReference* mAliasToType;
	BfTokenNode* mEndSemicolon;

};	BF_AST_DECL(BfTypeAliasDeclaration, BfTypeDeclaration);

class BfTypeReference : public BfAstNode
{
public:
	BF_AST_TYPE(BfTypeReference, BfAstNode);	

	bool IsNamedTypeReference();
	bool IsTypeDefTypeReference();

};	BF_AST_DECL(BfTypeReference, BfAstNode);

class BfDirectTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDirectTypeReference, BfAstNode);	

	BfType* mType;
	void Init(BfType* type)
	{
		mType = type;
		InitEmpty();
	}	

};	BF_AST_DECL(BfDirectTypeReference, BfAstNode);

class BfDirectTypeDefReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDirectTypeDefReference, BfTypeReference);

	BfTypeDef* mTypeDef;
	void Init(BfTypeDef* type)
	{
		mTypeDef = type;
		InitEmpty();
	}

};	BF_AST_DECL(BfDirectTypeDefReference, BfTypeReference);

// class BfTypeDefTypeReference : public BfTypeReference
// {
// public:
// 	BF_AST_TYPE(BfTypeDefTypeReference, BfTypeReference);
// 
// 	BfTypeDef* mTypeDef;
// };	BF_AST_DECL(BfTypeDefTypeReference, BfTypeReference);

class BfDirectStrTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDirectStrTypeReference, BfTypeReference);

	String mTypeName;

	using BfAstNode::Init;

	void Init(const StringImpl& str)
	{
		mTypeName = str;
#ifdef BF_AST_HAS_PARENT_MEMBER
		mParent = NULL;
#endif
		InitEmpty();
		//mTypeDef = NULL;		
	}	
};	BF_AST_DECL(BfDirectStrTypeReference, BfTypeReference);

class BfDotTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDotTypeReference, BfTypeReference);

	BfTokenNode* mDotToken;
};	BF_AST_DECL(BfDotTypeReference, BfTypeReference);

class BfVarTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfVarTypeReference, BfTypeReference);

	BfTokenNode* mVarToken;
};	BF_AST_DECL(BfVarTypeReference, BfTypeReference);

class BfVarRefTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfVarRefTypeReference, BfTypeReference);

	BfTokenNode* mVarToken;
	BfTokenNode* mRefToken;
};	BF_AST_DECL(BfVarRefTypeReference, BfTypeReference);

class BfLetTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfLetTypeReference, BfTypeReference);

	BfTokenNode* mLetToken;
};	BF_AST_DECL(BfLetTypeReference, BfTypeReference);

class BfWildcardTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfWildcardTypeReference, BfTypeReference);

	BfTokenNode* mWildcardToken;
};	BF_AST_DECL(BfWildcardTypeReference, BfTypeReference);

class BfQualifiedTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfQualifiedTypeReference, BfTypeReference);

	ASTREF(BfTypeReference*) mLeft;
	ASTREF(BfTokenNode*) mDot;
	ASTREF(BfTypeReference*) mRight;
};	BF_AST_DECL(BfQualifiedTypeReference, BfTypeReference);

class BfResolvedTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfResolvedTypeReference, BfTypeReference);

	BfType* mType;
};	BF_AST_DECL(BfResolvedTypeReference, BfTypeReference);

// "Named" means no wrapping (ie: not array, not generic instance, not pointer, etc
class BfNamedTypeReference : public BfTypeReference
{
public:
	BF_AST_TYPE(BfNamedTypeReference, BfTypeReference);

	ASTREF(BfIdentifierNode*) mNameNode;
};	BF_AST_DECL(BfNamedTypeReference, BfTypeReference);

class BfElementedTypeRef : public BfTypeReference
{
public:
	BF_AST_TYPE(BfElementedTypeRef, BfTypeReference);

	ASTREF(BfTypeReference*) mElementType;
};	BF_AST_DECL(BfElementedTypeRef, BfTypeReference);

class BfModifiedTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfModifiedTypeRef, BfElementedTypeRef);

	BfTokenNode* mRetTypeToken;
	BfTokenNode* mOpenParen;
	BfTokenNode* mCloseParen;
};	BF_AST_DECL(BfModifiedTypeRef, BfElementedTypeRef);

class BfArrayTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfArrayTypeRef, BfElementedTypeRef);

	int mDimensions;
	BfTokenNode* mOpenBracket;
	BfSizedArray<ASTREF(BfAstNode*)> mParams; // Either commas or constant size expression
	BfTokenNode* mCloseBracket;
};	BF_AST_DECL(BfArrayTypeRef, BfElementedTypeRef);

class BfNullableTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfNullableTypeRef, BfElementedTypeRef);

	BfTokenNode* mQuestionToken;
};	BF_AST_DECL(BfNullableTypeRef, BfElementedTypeRef);

class BfGenericInstanceTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfGenericInstanceTypeRef, BfElementedTypeRef);

	BfTokenNode* mOpenChevron;
	BfSizedArray<BfTypeReference*> mGenericArguments;	
	BfSizedArray<ASTREF(BfAstNode*)> mCommas;
	BfTokenNode* mCloseChevron;
	int GetGenericArgCount()
	{
		if (!mCommas.empty())
			return (int)mCommas.size() + 1;
		return std::max(1, (int)mGenericArguments.size());
	}
};	BF_AST_DECL(BfGenericInstanceTypeRef, BfElementedTypeRef);

class BfTupleTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfTupleTypeRef, BfElementedTypeRef);

	ASTREF(BfTokenNode*) mOpenParen;
	BfSizedArray<ASTREF(BfTypeReference*)> mFieldTypes;
	BfSizedArray<ASTREF(BfIdentifierNode*)> mFieldNames;
	BfSizedArray<ASTREF(BfAstNode*)> mCommas;
	ASTREF(BfTokenNode*) mCloseParen;
	int GetGenericArgCount()
	{
		if (!mCommas.empty())
			return (int)mCommas.size() + 1;
		return std::max(1, (int)mFieldTypes.size());
	}
};	BF_AST_DECL(BfTupleTypeRef, BfElementedTypeRef);

class BfDelegateTypeRef : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDelegateTypeRef, BfTypeReference);

	BfTokenNode* mTypeToken; // Delegate or Function

	BfAttributeDirective* mAttributes;
	BfTypeReference* mReturnType;
	BfAstNode* mOpenParen;
	BfSizedArray<BfParameterDeclaration*> mParams;
	BfSizedArray<BfTokenNode*> mCommas;
	BfAstNode* mCloseParen;
};	BF_AST_DECL(BfDelegateTypeRef, BfTypeReference);

class BfDeclTypeRef : public BfTypeReference
{
public:
	BF_AST_TYPE(BfDeclTypeRef, BfTypeReference);

	BfTokenNode* mToken;
	BfTokenNode* mOpenParen;
	BfExpression* mTarget;
	BfTokenNode* mCloseParen;
};	BF_AST_DECL(BfDeclTypeRef, BfTypeReference);

enum BfGenericParamKind
{	
	BfGenericParamKind_Type,
	BfGenericParamKind_Method
};

class BfGenericParamTypeRef : public BfTypeReference
{
public:
	BF_AST_TYPE(BfGenericParamTypeRef, BfTypeReference);

	BfGenericParamKind mGenericParamKind;
	int mGenericParamIdx;
};	BF_AST_DECL(BfGenericParamTypeRef, BfTypeReference);

class BfPointerTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfPointerTypeRef, BfElementedTypeRef);
	
	BfTokenNode* mStarNode;
};	BF_AST_DECL(BfPointerTypeRef, BfElementedTypeRef);

class BfConstTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfConstTypeRef, BfElementedTypeRef);
	BfTokenNode* mConstToken;
};	BF_AST_DECL(BfConstTypeRef, BfElementedTypeRef);

class BfConstExprTypeRef : public BfTypeReference
{
public:
	BF_AST_TYPE(BfConstExprTypeRef, BfTypeReference);
	BfTokenNode* mConstToken;
	BfExpression* mConstExpr;
};	BF_AST_DECL(BfConstExprTypeRef, BfTypeReference);

class BfUnsignedTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfUnsignedTypeRef, BfElementedTypeRef);
	BfTokenNode* mUnsignedToken;
};	BF_AST_DECL(BfUnsignedTypeRef, BfElementedTypeRef);

class BfRefTypeRef : public BfElementedTypeRef
{
public:
	BF_AST_TYPE(BfRefTypeRef, BfElementedTypeRef);
	BfTokenNode* mRefToken;
};	BF_AST_DECL(BfRefTypeRef, BfElementedTypeRef);

class BfTypeAttrExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfTypeAttrExpression, BfExpression);

	BfTokenNode* mToken;
	BfTokenNode* mOpenParen;
	BfTypeReference* mTypeRef;
	BfTokenNode* mCloseParen;
};	BF_AST_DECL(BfTypeAttrExpression, BfExpression);

class BfTypeOfExpression : public BfTypeAttrExpression
{
public:
	BF_AST_TYPE(BfTypeOfExpression, BfTypeAttrExpression);
};	BF_AST_DECL(BfTypeOfExpression, BfTypeAttrExpression);

class BfSizeOfExpression : public BfTypeAttrExpression
{
public:
	BF_AST_TYPE(BfSizeOfExpression, BfTypeAttrExpression);
};	BF_AST_DECL(BfSizeOfExpression, BfTypeAttrExpression);

class BfAlignOfExpression : public BfTypeAttrExpression
{
public:
	BF_AST_TYPE(BfAlignOfExpression, BfTypeAttrExpression);
};	BF_AST_DECL(BfAlignOfExpression, BfTypeAttrExpression);

class BfStrideOfExpression : public BfTypeAttrExpression
{
public:
	BF_AST_TYPE(BfStrideOfExpression, BfTypeAttrExpression);	
};	BF_AST_DECL(BfStrideOfExpression, BfTypeAttrExpression);

class BfDefaultExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfDefaultExpression, BfExpression);

	BfTokenNode* mDefaultToken;
	BfTokenNode* mOpenParen;
	BfTypeReference* mTypeRef;
	BfTokenNode* mCloseParen;
};	BF_AST_DECL(BfDefaultExpression, BfExpression);

class BfUninitializedExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfUninitializedExpression, BfExpression);

	BfTokenNode* mQuestionToken;
};	BF_AST_DECL(BfUninitializedExpression, BfExpression);

class BfCheckTypeExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfCheckTypeExpression, BfExpression);

	BfExpression* mTarget;
	BfTokenNode* mIsToken;
	BfTypeReference* mTypeRef;
};	BF_AST_DECL(BfCheckTypeExpression, BfExpression);

class BfDynamicCastExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfDynamicCastExpression, BfExpression);

	BfExpression* mTarget;
	BfTokenNode* mAsToken;
	BfTypeReference* mTypeRef;	
};	BF_AST_DECL(BfDynamicCastExpression, BfExpression);

class BfCastExpression : public BfUnaryOperatorExpression
{
public:
	BF_AST_TYPE(BfCastExpression, BfUnaryOperatorExpression);
	
	BfTokenNode* mOpenParen;
	BfTypeReference* mTypeRef;
	BfTokenNode* mCloseParen;	
};	BF_AST_DECL(BfCastExpression, BfUnaryOperatorExpression);

class BfDelegateBindExpression : public BfMethodBoundExpression
{
public:
	BF_AST_TYPE(BfDelegateBindExpression, BfMethodBoundExpression);

	BfAstNode* mNewToken;
	BfTokenNode* mFatArrowToken;
	BfExpression* mTarget;
	BfGenericArgumentsNode* mGenericArgs;
};	BF_AST_DECL(BfDelegateBindExpression, BfMethodBoundExpression);

class BfLambdaBindExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfLambdaBindExpression, BfExpression);

	BfAstNode* mNewToken;	
	BfTokenNode* mOpenParen;
	BfTokenNode* mCloseParen;		
	BfSizedArray<ASTREF(BfIdentifierNode*)> mParams;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;
	BfTokenNode* mFatArrowToken;	
	BfAstNode* mBody; // Either expression or block
	BfFieldDtorDeclaration* mDtor;
};	BF_AST_DECL(BfLambdaBindExpression, BfExpression);

class BfAttributedExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfAttributedExpression, BfExpression);

	BfAttributeDirective* mAttributes;
	BfExpression* mExpression;
};	BF_AST_DECL(BfAttributedExpression, BfExpression);

class BfAttributedStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfAttributedStatement, BfStatement);

	BfAttributeDirective* mAttributes;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfAttributedStatement, BfStatement);

class BfObjectCreateExpression : public BfMethodBoundExpression
{
public:
	BF_AST_TYPE(BfObjectCreateExpression, BfMethodBoundExpression);

	BfAstNode* mNewNode;
	BfTokenNode* mStarToken;
	BfTypeReference* mTypeRef;	
	BfTokenNode* mOpenToken;
	BfTokenNode* mCloseToken;	
	BfSizedArray<BfExpression*> mArguments;
	BfSizedArray<BfTokenNode*> mCommas;	
};	BF_AST_DECL(BfObjectCreateExpression, BfMethodBoundExpression);

class BfBoxExpression : public BfExpression
{
public:
	BF_AST_TYPE(BfBoxExpression, BfExpression);

	BfAstNode* mAllocNode;
	BfTokenNode* mBoxToken;
	BfExpression* mExpression;
};	BF_AST_DECL(BfBoxExpression, BfExpression);

class BfDeleteStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfDeleteStatement, BfStatement);

	BfTokenNode* mDeleteToken;	
	BfTokenNode* mTargetTypeToken; // colon token
	BfAstNode* mAllocExpr;
	BfAttributeDirective* mAttributes;
	BfExpression* mExpression;
};	BF_AST_DECL(BfDeleteStatement, BfStatement);

class BfDeferBindNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfDeferBindNode, BfAstNode);
	
	BfTokenNode* mOpenBracket;
	BfTokenNode* mCloseBracket;
	BfSizedArray<ASTREF(BfIdentifierNode*)> mParams;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;	
};	BF_AST_DECL(BfDeferBindNode, BfAstNode);

class BfDeferStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfDeferStatement, BfStatement);

	BfTokenNode* mDeferToken;
	BfTokenNode* mColonToken;
	BfAstNode* mScopeName; // :, mixin, or identifier

	BfDeferBindNode* mBind;

	//Legacy compat, remove
	BfTokenNode* mOpenParen;
	BfTokenNode* mScopeToken;
	BfTokenNode* mCloseParen;

	BfAstNode* mTargetNode;

// 	virtual bool IsMissingSemicolon() override
// 	{
// 		return BfNodeDynCastExact<BfBlock>(mTargetNode) == NULL;
// 	}
};	BF_AST_DECL(BfDeferStatement, BfStatement);

class BfThrowStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfThrowStatement, BfStatement);

	BfTokenNode* mThrowToken;
	BfExpression* mExpression;
};	BF_AST_DECL(BfThrowStatement, BfStatement);

class BfScopedInvocationTarget : public BfAstNode
{
public:
	BF_AST_TYPE(BfScopedInvocationTarget, BfAstNode);

	BfAstNode* mTarget;
	BfTokenNode* mColonToken;
	BfAstNode* mScopeName; // :, mixin, or identifier
};	BF_AST_DECL(BfScopedInvocationTarget, BfAstNode);

class BfInvocationExpression : public BfMethodBoundExpression
{
public:
	BF_AST_TYPE(BfInvocationExpression, BfMethodBoundExpression);

	ASTREF(BfAstNode*) mTarget;
	ASTREF(BfTokenNode*) mOpenParen;
	ASTREF(BfTokenNode*) mCloseParen;
	ASTREF(BfGenericArgumentsNode*) mGenericArgs;
	BfSizedArray<ASTREF(BfExpression*)> mArguments;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;		
};	BF_AST_DECL(BfInvocationExpression, BfMethodBoundExpression);

class BfEnumCaseDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfEnumCaseDeclaration, BfAstNode);

	ASTREF(BfTokenNode*) mCaseToken;
	BfSizedArray<ASTREF(BfFieldDeclaration*)> mEntries;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;	
};	BF_AST_DECL(BfEnumCaseDeclaration, BfAstNode);

class BfMemberDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfMemberDeclaration, BfAstNode);

	BfAttributeDirective* mAttributes;	
	BfTokenNode* mProtectionSpecifier;
	BfTokenNode* mStaticSpecifier;	
	BfTokenNode* mReadOnlySpecifier; // Also stores 'inline'
};	BF_AST_DECL(BfMemberDeclaration, BfAstNode);

class BfVariableDeclaration : public BfExpression
{
public:
	BF_AST_TYPE(BfVariableDeclaration, BfExpression);

	ASTREF(BfAttributeDirective*) mAttributes;
	ASTREF(BfTokenNode*) mModSpecifier;
	ASTREF(BfTypeReference*) mTypeRef;
	ASTREF(BfTokenNode*) mPrecedingComma;
	BfAstNode* mNameNode; // Either BfIdentifierNode or BfTupleExpression
	ASTREF(BfTokenNode*) mEqualsNode;
	ASTREF(BfExpression*) mInitializer;	
};	BF_AST_DECL(BfVariableDeclaration, BfExpression);

class BfLocalMethodDeclaration : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfLocalMethodDeclaration, BfCompoundStatement);

	BfMethodDeclaration* mMethodDeclaration;
};	BF_AST_DECL(BfLocalMethodDeclaration, BfCompoundStatement);

class BfParameterDeclaration : public BfVariableDeclaration
{
public:
	BF_AST_TYPE(BfParameterDeclaration, BfVariableDeclaration);
	BfTokenNode* mModToken; // 'Params'
};	BF_AST_DECL(BfParameterDeclaration, BfVariableDeclaration);

class BfGenericParamsDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfGenericParamsDeclaration, BfAstNode);

	ASTREF(BfTokenNode*) mOpenChevron;
	BfSizedArray<ASTREF(BfIdentifierNode*)> mGenericParams;
	BfSizedArray<ASTREF(BfAstNode*)> mCommas;
	ASTREF(BfTokenNode*) mCloseChevron;
};	BF_AST_DECL(BfGenericParamsDeclaration, BfAstNode);

class BfGenericArgumentsNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfGenericArgumentsNode, BfAstNode);

	ASTREF(BfTokenNode*) mOpenChevron;
	BfSizedArray<ASTREF(BfTypeReference*)> mGenericArgs;
	BfSizedArray<ASTREF(BfAstNode*)> mCommas;
	ASTREF(BfTokenNode*) mCloseChevron;
};	BF_AST_DECL(BfGenericArgumentsNode, BfAstNode);

class BfTokenPairNode : public BfAstNode
{
public:
	BF_AST_TYPE(BfTokenPairNode, BfAstNode);

	BfTokenNode* mLeft;
	BfTokenNode* mRight;
};	BF_AST_DECL(BfTokenPairNode, BfAstNode);

class BfGenericOperatorConstraint : public BfAstNode
{
public:
	BF_AST_TYPE(BfGenericOperatorConstraint, BfAstNode);
		
	BfTokenNode* mOperatorToken;
	BfTypeReference* mLeftType;
	BfTokenNode* mOpToken;
	BfTypeReference* mRightType;
};  BF_AST_DECL(BfGenericOperatorConstraint, BfAstNode);

class BfGenericConstraint : public BfAstNode
{
public:
	BF_AST_TYPE(BfGenericConstraint, BfAstNode);

	BfTokenNode* mWhereToken;
	BfTypeReference* mTypeRef;
	BfTokenNode* mColonToken;
	BfSizedArray<BfAstNode*> mConstraintTypes;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;	
};	BF_AST_DECL(BfGenericConstraint, BfAstNode);

class BfGenericConstraintsDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfGenericConstraintsDeclaration, BfAstNode);
	BfSizedArray<BfGenericConstraint*> mGenericConstraints;
};	BF_AST_DECL(BfGenericConstraintsDeclaration, BfAstNode);

class BfMethodDeclaration : public BfMemberDeclaration
{
public:
	BF_AST_TYPE(BfMethodDeclaration, BfMemberDeclaration);
	
	BfCommentNode* mDocumentation;
	BfAttributeDirective* mReturnAttributes;
	BfTokenNode* mExternSpecifier;
	BfTokenNode* mVirtualSpecifier; // either 'virtual', 'override', or 'abstract'
	BfTokenNode* mNewSpecifier;
	BfTokenNode* mMixinSpecifier;
	BfTokenNode* mPartialSpecifier;
	BfTokenNode* mMutSpecifier;
	BfTypeReference* mReturnType;
	BfTypeReference* mExplicitInterface;
	BfTokenNode* mExplicitInterfaceDotToken;
	BfIdentifierNode* mNameNode;	
	BfTokenNode* mOpenParen;
	BfTokenNode* mThisToken;
	BfSizedArray<BfParameterDeclaration*> mParams;
	BfSizedArray<BfTokenNode*> mCommas;
	BfTokenNode* mCloseParen;
	BfGenericParamsDeclaration* mGenericParams;
	BfGenericConstraintsDeclaration* mGenericConstraintsDeclaration;
	BfAstNode* mEndSemicolon;
	BfTokenNode* mFatArrowToken;
	BfAstNode* mBody; // Either expression or block	

	//BfMethodDef* mMethodDef;

	bool mHadYield;
};	BF_AST_DECL(BfMethodDeclaration, BfMemberDeclaration);

class BfOperatorDeclaration : public BfMethodDeclaration
{
public:
	BF_AST_TYPE(BfOperatorDeclaration, BfMethodDeclaration);
	
	BfTokenNode* mExplicitToken; // Explicit or Implicit
	BfTokenNode* mOperatorToken;
	BfTokenNode* mOpTypeToken;
	bool mIsConvOperator;
	BfUnaryOp mUnaryOp;
	BfBinaryOp mBinOp;	
	BfAssignmentOp mAssignOp;
};	BF_AST_DECL(BfOperatorDeclaration, BfMethodDeclaration);

class BfConstructorDeclaration : public BfMethodDeclaration
{
public:
	BF_AST_TYPE(BfConstructorDeclaration, BfMethodDeclaration);

	BfTokenNode* mThisToken;
	
	BfTokenNode* mInitializerColonToken;
	BfInvocationExpression* mInitializer;
	
};	BF_AST_DECL(BfConstructorDeclaration, BfMethodDeclaration);

class BfDestructorDeclaration : public BfMethodDeclaration
{
public:	
	BF_AST_TYPE(BfDestructorDeclaration, BfMethodDeclaration);

	BfTokenNode* mTildeToken;
	BfTokenNode* mThisToken;
};	BF_AST_DECL(BfDestructorDeclaration, BfMethodDeclaration);

class BfFieldDtorDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfFieldDtorDeclaration, BfAstNode);

	BfTokenNode* mTildeToken;

	BfAstNode* mBody;
	BfFieldDtorDeclaration* mNextFieldDtor;
};	BF_AST_DECL(BfFieldDtorDeclaration, BfAstNode);

class BfFieldDeclaration : public BfMemberDeclaration
{
public:
	BF_AST_TYPE(BfFieldDeclaration, BfMemberDeclaration);
	
	BfCommentNode* mDocumentation;
	BfTokenNode* mPrecedingComma;
	BfTokenNode* mConstSpecifier;	
	BfTokenNode* mVolatileSpecifier;
	BfTokenNode* mNewSpecifier;
	BfTokenNode* mExternSpecifier;
	BfTypeReference* mTypeRef;
	BfIdentifierNode* mNameNode;
	BfTokenNode* mEqualsNode;
	BfExpression* mInitializer;
	BfFieldDtorDeclaration* mFieldDtor;
	
	BfFieldDef* mFieldDef;
};	BF_AST_DECL(BfFieldDeclaration, BfMemberDeclaration);

class BfEnumEntryDeclaration : public BfFieldDeclaration
{
public:
	BF_AST_TYPE(BfEnumEntryDeclaration, BfFieldDeclaration);

};	BF_AST_DECL(BfEnumEntryDeclaration, BfFieldDeclaration);

class BfPropertyMethodDeclaration : public BfAstNode
{
public:
	BF_AST_TYPE(BfPropertyMethodDeclaration, BfAstNode);
	BfPropertyDeclaration* mPropertyDeclaration;
	BfAttributeDirective* mAttributes;
	BfTokenNode* mProtectionSpecifier;
	BfTokenNode* mMutSpecifier;	
	BfIdentifierNode* mNameNode;
	BfTokenNode* mFatArrowToken;
	BfAstNode* mBody;		
	BfAstNode* mEndSemicolon;
};	BF_AST_DECL(BfPropertyMethodDeclaration, BfAstNode);

class BfPropertyBodyExpression : public BfAstNode
{
public:
	BF_AST_TYPE(BfPropertyBodyExpression, BfAstNode);
	BfTokenNode* mFatTokenArrow;		
};  BF_AST_DECL(BfPropertyBodyExpression, BfAstNode);

class BfPropertyDeclaration : public BfFieldDeclaration
{
public:
	BF_AST_TYPE(BfPropertyDeclaration, BfFieldDeclaration);

	BfTokenNode* mVirtualSpecifier; // either 'virtual', 'override', or 'abstract'
	BfTypeReference* mExplicitInterface;
	BfTokenNode* mExplicitInterfaceDotToken;	
	BfAstNode* mDefinitionBlock;

	BfSizedArray<BfPropertyMethodDeclaration*> mMethods;		

	BfPropertyMethodDeclaration* GetMethod(const StringImpl& name);
};	BF_AST_DECL(BfPropertyDeclaration, BfFieldDeclaration);

class BfIndexerDeclaration : public BfPropertyDeclaration
{
public:
	BF_AST_TYPE(BfIndexerDeclaration, BfPropertyDeclaration);

	BfTokenNode* mThisToken;
	BfTokenNode* mOpenBracket;
	BfSizedArray<ASTREF(BfParameterDeclaration*)> mParams;
	BfSizedArray<ASTREF(BfTokenNode*)> mCommas;
	BfTokenNode* mCloseBracket;
};	BF_AST_DECL(BfIndexerDeclaration, BfPropertyDeclaration);

class BfBreakStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfBreakStatement, BfStatement);

	BfTokenNode* mBreakNode;
	BfAstNode* mLabel;
};	BF_AST_DECL(BfBreakStatement, BfStatement);

class BfTryStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfTryStatement, BfCompoundStatement);

	BfTokenNode* mTryToken;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfTryStatement, BfCompoundStatement);

class BfCatchStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfCatchStatement, BfCompoundStatement);

	BfTokenNode* mCatchToken;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfCatchStatement, BfCompoundStatement);

class BfFinallyStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfFinallyStatement, BfCompoundStatement);

	BfTokenNode* mFinallyToken;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfFinallyStatement, BfCompoundStatement);

class BfCheckedStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfCheckedStatement, BfCompoundStatement);

	BfTokenNode* mCheckedToken;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfCheckedStatement, BfCompoundStatement);

class BfUncheckedStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfUncheckedStatement, BfCompoundStatement);

	BfTokenNode* mUncheckedToken;
	BfAstNode* mStatement;
};	BF_AST_DECL(BfUncheckedStatement, BfCompoundStatement);

class BfContinueStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfContinueStatement, BfStatement);

	BfTokenNode* mContinueNode;
	BfAstNode* mLabel;
};	BF_AST_DECL(BfContinueStatement, BfStatement);

class BfFallthroughStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfFallthroughStatement, BfStatement);

	BfTokenNode* mFallthroughToken;
};	BF_AST_DECL(BfFallthroughStatement, BfStatement);

class BfForEachStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfForEachStatement, BfLabelableStatement);

	BfTokenNode* mForToken;
	BfTokenNode* mOpenParen;
	BfTokenNode* mReadOnlyToken;
	BfTypeReference* mVariableTypeRef;
	BfAstNode* mVariableName; // Either BfIdentifierNode or BfTupleExpression
	BfTokenNode* mInToken;
	BfExpression* mCollectionExpression;
	BfTokenNode* mCloseParen;
	BfAstNode* mEmbeddedStatement;
};	BF_AST_DECL(BfForEachStatement, BfLabelableStatement);

class BfForStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfForStatement, BfLabelableStatement);

	BfTokenNode* mForToken;
	BfTokenNode* mOpenParen;
	BfSizedArray<ASTREF(BfAstNode*)> mInitializers;
	BfSizedArray<ASTREF(BfTokenNode*)> mInitializerCommas;
	BfTokenNode* mInitializerSemicolon;
	BfExpression* mCondition;
	BfTokenNode* mConditionSemicolon;
	BfSizedArray<ASTREF(BfAstNode*)> mIterators;
	BfSizedArray<ASTREF(BfTokenNode*)> mIteratorCommas;
	BfTokenNode* mCloseParen;
	BfAstNode* mEmbeddedStatement;
};	BF_AST_DECL(BfForStatement, BfLabelableStatement);

class BfUsingStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfUsingStatement, BfCompoundStatement);

	BfTokenNode* mUsingToken;
	BfTokenNode* mOpenParen;
	BfVariableDeclaration* mVariableDeclaration;	
	BfTokenNode* mCloseParen;
	BfAstNode* mEmbeddedStatement;
};	BF_AST_DECL(BfUsingStatement, BfCompoundStatement);

class BfDoStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfDoStatement, BfLabelableStatement);

	BfTokenNode* mDoToken;
	BfAstNode* mEmbeddedStatement;
};	BF_AST_DECL(BfDoStatement, BfLabelableStatement);

class BfRepeatStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfRepeatStatement, BfLabelableStatement);

	BfTokenNode* mRepeatToken;
	BfAstNode* mEmbeddedStatement;
	BfTokenNode* mWhileToken;
	BfTokenNode* mOpenParen;
	BfExpression* mCondition;
	BfTokenNode* mCloseParen;	
};	BF_AST_DECL(BfRepeatStatement, BfLabelableStatement);

class BfWhileStatement : public BfLabelableStatement
{
public:
	BF_AST_TYPE(BfWhileStatement, BfLabelableStatement);

	BfTokenNode* mWhileToken;
	BfTokenNode* mOpenParen;
	BfExpression* mCondition;
	BfTokenNode* mCloseParen;
	BfAstNode* mEmbeddedStatement;
};	BF_AST_DECL(BfWhileStatement, BfLabelableStatement);

class BfReturnStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfReturnStatement, BfStatement);

	BfTokenNode* mReturnToken;
	BfExpression* mExpression;
};	BF_AST_DECL(BfReturnStatement, BfStatement);

class BfYieldStatement : public BfStatement
{
public:
	BF_AST_TYPE(BfYieldStatement, BfStatement);

	BfTokenNode* mReturnOrBreakToken;
	BfExpression* mExpression;
};	BF_AST_DECL(BfYieldStatement, BfStatement);

class BfInlineAsmStatement : public BfCompoundStatement
{
public:
	BF_AST_TYPE(BfInlineAsmStatement, BfCompoundStatement);

	BfTokenNode* mOpenBrace;
	BfTokenNode* mCloseBrace;

	Array<BfInlineAsmInstruction*> mInstructions;
	//TODO: Make a block here
};	BF_AST_DECL(BfInlineAsmStatement, BfCompoundStatement);

class BfInlineAsmInstruction : public BfAstNode
{
public:
	class AsmArg
	{
	public:
		enum EArgType
		{
			ARGTYPE_Immediate,		// immediate integer; uses mInt only
			ARGTYPE_FloatReg,		// float point st register; mInt is st reg index
			ARGTYPE_IntReg,			// general integer register; mReg is register name
			ARGTYPE_Memory,			// memory access; arg mem flags indicate additive permutation, [baseReg + adjReg*scalar + immDisplacement]
		};
		enum EArgMemFlags
		{
			ARGMEMF_ImmediateDisp	= (1 << 0),		// uses immediate displacement constant
			ARGMEMF_BaseReg			= (1 << 1),		// uses unscaled base register
			ARGMEMF_AdjReg			= (1 << 2),		// uses scaled adjustment register and scalar value
		};

		EArgType mType;
		unsigned long mMemFlags;
		String mSegPrefix; // if non-empty, cs|ds|es|fs|gs|ss
		String mSizePrefix; // if non-empty, size prefix for memory accesses (e.g. byte|word|dword), syntactically followed by "ptr" although that's omitted here
		int mInt; // memory displacement, immediate integer, fp st register index, etc.
		String mReg; // general register, or unscaled base register for memory accesses
		String mAdjReg; // scaled adjustment register for memory accesses
		int mAdjRegScalar; // adjustment scalar (e.g. 2, 4, or 8).
		String mMemberSuffix; // if non-empty, struct member suffix following memory access, e.g. [ebx].mFoo

		AsmArg();

		String ToString();
	};
	
	class AsmInst
	{
	public:
		String mLabel;
		Array<String> mOpPrefixes;
		String mOpCode;
		Array<AsmArg> mArgs;
		int mDebugLine;

		AsmInst();

		String ToString();
	};

public:
	BF_AST_TYPE(BfInlineAsmInstruction, BfAstNode);

	AsmInst mAsmInst;
};	BF_AST_DECL(BfInlineAsmInstruction, BfAstNode);

const char* BfTokenToString(BfToken token);
bool BfTokenIsKeyword(BfToken token);
BfBinaryOp BfAssignOpToBinaryOp(BfAssignmentOp assignmentOp);
BfBinaryOp BfGetOppositeBinaryOp(BfBinaryOp origOp);
BfBinaryOp BfGetFlippedBinaryOp(BfBinaryOp origOp);
bool BfBinOpEqualityCheck(BfBinaryOp binOp);
int BfGetBinaryOpPrecendence(BfBinaryOp binOp);
const char* BfGetOpName(BfBinaryOp binOp);
const char* BfGetOpName(BfUnaryOp unaryOp);
bool BfCanOverloadOperator(BfUnaryOp unaryOp);
BfBinaryOp BfTokenToBinaryOp(BfToken token);
BfUnaryOp BfTokenToUnaryOp(BfToken token);
BfAssignmentOp BfTokenToAssignmentOp(BfToken token);
bool BfIsCommentBlock(BfCommentKind commentKind);

NS_BF_END