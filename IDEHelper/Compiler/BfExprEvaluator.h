#pragma once

#include "BfCompiler.h"
#include "BfSourceClassifier.h"

NS_BF_BEGIN

enum BfArgFlags
{
	BfArgFlag_None = 0,	
	BfArgFlag_DelegateBindAttempt = 1,
	BfArgFlag_LambdaBindAttempt = 2,
	BfArgFlag_UnqualifiedDotAttempt = 4,
	BfArgFlag_DeferredValue = 8,
	BfArgFlag_FromParamComposite = 0x10,
	BfArgFlag_UntypedDefault = 0x20,
	BfArgFlag_DeferredEval = 0x40,
	BfArgFlag_ExpectedTypeCast = 0x80,
	BfArgFlag_VariableDeclaration = 0x100,
	BfArgFlag_ParamsExpr = 0x200,
	BfArgFlag_UninitializedExpr = 0x400,
	BfArgFlag_StringInterpolateFormat = 0x800,
	BfArgFlag_StringInterpolateArg = 0x1000,
	BfArgFlag_Cascade = 0x2000
};

enum BfResolveArgsFlags
{
	BfResolveArgsFlag_None = 0,
	BfResolveArgsFlag_DeferFixits = 1,
	BfResolveArgsFlag_DeferParamValues = 2, // We still evaluate but don't generate code until the method is selected (for SkipCall support)
	BfResolveArgsFlag_DeferParamEval = 4,
	BfResolveArgsFlag_AllowUnresolvedTypes = 8,
	BfResolveArgsFlag_InsideStringInterpolationAlloc = 0x10
};

enum BfResolveArgFlags
{
	BfResolveArgFlag_None = 0,
	BfResolveArgFlag_FromGeneric = 1,
	BfResolveArgFlag_FromGenericParam = 2
};

enum BfCreateFallFlags
{
	BfCreateFallFlags_None,
	BfCreateFallFlags_BypassVirtual = 1,
	BfCreateFallFlags_SkipThis = 2,
	BfCreateFallFlags_AllowImplicitRef = 4
};

class BfResolvedArg
{
public:
	BfTypedValue mTypedValue;
	BfType* mResolvedType;
	BfAstNode* mExpression;
	BfArgFlags mArgFlags;	
	BfType* mExpectedType;
	BfType* mBestBoundType;
	bool mWantsRecalc;

public:
	BfResolvedArg()
	{
		mResolvedType = NULL;
		mExpression = NULL;
		mArgFlags = BfArgFlag_None;
		mExpectedType = NULL;
		mBestBoundType = NULL;
		mWantsRecalc = false;
	}

	bool IsDeferredEval()
	{
		return (mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt | BfArgFlag_UntypedDefault | BfArgFlag_DeferredEval)) != 0;
	}
};

struct BfResolvedArgs
{
	SizedArray<BfResolvedArg, 4> mResolvedArgs;	
	BfTokenNode* mOpenToken;
	const BfSizedArray<BfExpression*>* mArguments;
	const BfSizedArray<BfTokenNode*>* mCommas;	
	BfTokenNode* mCloseToken;

public:
	BfResolvedArgs()
	{
		mOpenToken = NULL;
		mArguments = NULL;
		mCommas = NULL;
		mCloseToken = NULL;
	}

	BfResolvedArgs(BfSizedArray<BfExpression*>* args)
	{
		mOpenToken = NULL;
		mArguments = args;
		mCommas = NULL;
		mCloseToken = NULL;
	}

	BfResolvedArgs(BfTokenNode* openToken, BfSizedArray<BfExpression*>* args, BfSizedArray<BfTokenNode*>* commas, BfTokenNode* closeToken)
	{
		mOpenToken = openToken;
		mArguments = args;
		mCommas = commas;
		mCloseToken = closeToken;
	}

	void Init(BfTokenNode* openToken, BfSizedArray<BfExpression*>* args, BfSizedArray<BfTokenNode*>* commas, BfTokenNode* closeToken)
	{
		mOpenToken = openToken;
		mArguments = args;
		mCommas = commas;
		mCloseToken = closeToken;
	}

	void Init(const BfSizedArray<BfExpression*>* args)
	{
		mOpenToken = NULL;
		mArguments = args;
		mCommas = NULL;
		mCloseToken = NULL;
	}

	void HandleFixits(BfModule* module);	
	
};

class BfGenericInferContext
{
public:
	HashSet<BfType*> mCheckedTypeSet;
	BfModule* mModule;
	BfTypeVector* mCheckMethodGenericArguments;
	SizedArray<BfIRValue, 4> mPrevArgValues;
	int mInferredCount;

public:
	BfGenericInferContext()
	{
		mModule = NULL;
		mInferredCount = 0;
	}
	bool InferGenericArgument(BfMethodInstance* methodInstance, BfType* argType, BfType* wantType, BfIRValue argValue);
	int GetUnresolvedCount()
	{
		return (int)mCheckMethodGenericArguments->size() - mInferredCount;
	}
	bool InferGenericArguments(BfMethodInstance* methodInstance, int srcGenericIdx);
	void InferGenericArguments(BfMethodInstance* methodInstance);
};

class BfMethodMatcher
{
public:
	struct BfAmbiguousEntry
	{
		BfMethodInstance* mMethodInstance;
		BfTypeVector mBestMethodGenericArguments;
	};

	enum MatchFailKind
	{
		MatchFailKind_None,
		MatchFailKind_Protection,
		MatchFailKind_CheckedMismatch
	};

	enum BackupMatchKind
	{
		BackupMatchKind_None,
		BackupMatchKind_TooManyArgs,
		BackupMatchKind_EarlyMismatch,		
		BackupMatchKind_PartialLastArgMatch
	};

public:
	BfAstNode* mTargetSrc;
	BfTypedValue mTarget;
	BfTypedValue mOrigTarget;
	BfModule* mModule;
	BfTypeDef* mActiveTypeDef;
	String mMethodName;	
	BfMethodInstance* mInterfaceMethodInstance;
	SizedArrayImpl<BfResolvedArg>& mArguments;
	BfMethodType mMethodType;
	BfCheckedKind mCheckedKind;
	bool mHadExplicitGenericArguments;	
	bool mHasVarArguments;	
	bool mHadVarConflictingReturnType;
	bool mBypassVirtual;
	bool mAllowImplicitThis;
	bool mAllowImplicitRef;
	bool mAllowStatic;
	bool mAllowNonStatic;	
	bool mSkipImplicitParams;	
	bool mAutoFlushAmbiguityErrors;	
	BfEvalExprFlags mBfEvalExprFlags;
	int mMethodCheckCount;	
	BfType* mExplicitInterfaceCheck;	
	MatchFailKind mMatchFailKind;
		
	BfTypeVector mCheckMethodGenericArguments;		

	BfType* mSelfType; // Only when matching interfaces when 'Self' needs to refer back to the implementing type
	BfMethodDef* mBackupMethodDef;
	BackupMatchKind mBackupMatchKind;	
	int mBackupArgMatchCount;
	BfMethodDef* mBestMethodDef;
	BfTypeInstance* mBestMethodTypeInstance;		
	BfMethodInstance* mBestRawMethodInstance;
	BfModuleMethodInstance mBestMethodInstance;
	SizedArray<int, 4> mBestMethodGenericArgumentSrcs;
	BfTypeVector mBestMethodGenericArguments;
	BfTypeVector mExplicitMethodGenericArguments;
	bool mFakeConcreteTarget;
	Array<BfAmbiguousEntry> mAmbiguousEntries;	

public:
	BfTypedValue ResolveArgTypedValue(BfResolvedArg& resolvedArg, BfType* checkType, BfTypeVector* genericArgumentsSubstitute, BfType *origCheckType = NULL, BfResolveArgFlags flags = BfResolveArgFlag_None);
	bool InferFromGenericConstraints(BfMethodInstance* methodInstance, BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs);
	void CompareMethods(BfMethodInstance* prevMethodInstance, BfTypeVector* prevGenericArgumentsSubstitute,
		BfMethodInstance* newMethodInstance, BfTypeVector* genericArgumentsSubstitute, 
		bool* outNewIsBetter, bool* outNewIsWorse, bool allowSpecializeFail);
	void FlushAmbiguityError();		
	bool IsType(BfTypedValue& val, BfType* type);
	int GetMostSpecificType(BfType* lhs, BfType* rhs); // 0, 1, or -1

public:
	BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, const StringImpl& methodName, SizedArrayImpl<BfResolvedArg>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments);
	BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* interfaceMethodInstance, SizedArrayImpl<BfResolvedArg>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments = NULL);
	void Init(/*SizedArrayImpl<BfResolvedArg>& arguments, */BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments);
	bool IsMemberAccessible(BfTypeInstance* typeInst, BfTypeDef* declaringType);
	bool CheckType(BfTypeInstance* typeInstance, BfTypedValue target, bool isFailurePass, bool forceOuterCheck = false);
	void CheckOuterTypeStaticMethods(BfTypeInstance* typeInstance, bool isFailurePass);
	bool WantsCheckMethod(BfProtectionCheckFlags& flags, BfTypeInstance* startTypeInstance, BfTypeInstance* checkTypeInstance, BfMethodDef* methodDef);
	bool CheckMethod(BfTypeInstance* targetTypeInstance, BfTypeInstance* typeInstance, BfMethodDef* checkMethod, bool isFailurePass);
	void TryDevirtualizeCall(BfTypedValue target, BfTypedValue* origTarget = NULL, BfTypedValue* staticResult = NULL);
	bool HasVarGenerics();
	bool IsVarCall(BfType*& outReturnType);
};



class BfBaseClassWalker
{
public:
	struct Entry
	{
		BfType* mSrcType;
		BfTypeInstance* mTypeInstance;

		Entry(BfType* srcType, BfTypeInstance* typeInstance)
		{
			mSrcType = srcType;
			mTypeInstance = typeInstance;
		}

		Entry()
		{
			mSrcType = NULL;
			mTypeInstance = NULL;
		}

		bool operator==(const Entry& rhs)
		{
			return (mSrcType == rhs.mSrcType) && (mTypeInstance == rhs.mTypeInstance);
		}
	};

	BfTypeInstance* mTypes[2];
	Array<Entry> mManualList;
	bool mMayBeFromInterface;

public:
	BfBaseClassWalker();
	BfBaseClassWalker(BfType* typeA, BfType* typeB, BfModule* module);
	void AddConstraints(BfType* srcType, BfGenericParamInstance* genericParam);

public:
	Entry Next();
};

class BfFunctionBindResult
{
public:
	BfTypedValue mOrigTarget;
	BfTypedValue mTarget;	
	BfIRValue mFunc;	
	BfMethodInstance* mMethodInstance;
	BfType* mBindType;
	bool mSkipThis;
	bool mSkipMutCheck;
	bool mWantsArgs;
	bool mCheckedMultipleMethods;
	SizedArray<BfIRValue, 2> mIRArgs;

public:
	BfFunctionBindResult()
	{				
		mMethodInstance = NULL;
		mBindType = NULL;
		mSkipMutCheck = false;
		mWantsArgs = false;
		mSkipThis = false;
		mCheckedMultipleMethods = false;
	}
};

struct DeferredTupleAssignData
{
	struct Entry
	{		
		BfExpression* mExpr;
		BfExprEvaluator* mExprEvaluator;
		DeferredTupleAssignData* mInnerTuple;
	};

	Array<Entry> mChildren;
	BfTypeInstance* mTupleType;

	~DeferredTupleAssignData();
};

enum BfImplicitParamKind
{
	BfImplicitParamKind_General,
	BfImplicitParamKind_GenericMethodMember,	
	BfImplicitParamKind_GenericTypeMember,	
	BfImplicitParamKind_GenericTypeMember_Addr,
};

enum BfLookupFieldFlags
{
	BfLookupFieldFlag_None = 0,
	BfLookupFieldFlag_IsImplicitThis = 1,
	BfLookupFieldFlag_BaseLookup = 2,
	BfLookupFieldFlag_CheckingOuter = 4,
	BfLookupFieldFlag_IgnoreProtection = 8,
	BfLookupFieldFlag_BindOnly = 0x10
};

enum BfUnaryOpFlags
{
	BfUnaryOpFlag_None = 0,
	BfUnaryOpFlag_IsConstraintCheck = 1
};

enum BfBinOpFlags
{
	BfBinOpFlag_None = 0,
	BfBinOpFlag_NoClassify = 1,
	BfBinOpFlag_ForceLeftType = 2,
	BfBinOpFlag_IgnoreOperatorWithWrongResult = 4,
	BfBinOpFlag_IsConstraintCheck = 8,
	BfBinOpFlag_DeferRight = 0x10
};

class BfExprEvaluator : public BfStructuralVisitor
{
public:
	BfModule* mModule;
	BfTypedValue mResult;
	BfEvalExprFlags mBfEvalExprFlags;
	BfLocalVariable* mResultLocalVar;
	BfFieldInstance* mResultFieldInstance;
	int mResultLocalVarField;
	int mResultLocalVarFieldCount; // > 1 for structs with multiple members
	BfAstNode* mResultLocalVarRefNode;
	BfAstNode* mPropSrc;
	BfTypedValue mPropTarget;
	BfTypedValue mOrigPropTarget;
	BfGetMethodInstanceFlags mPropGetMethodFlags;
	BfCheckedKind mPropCheckedKind;
	BfPropertyDef* mPropDef;
	BfType* mExpectingType;		
	BfAttributeState* mPrefixedAttributeState;
	BfTypedValue* mReceivingValue;	
	BfFunctionBindResult* mFunctionBindResult;
	SizedArray<BfResolvedArg, 2> mIndexerValues;	
	BfAstNode* mDeferCallRef;
	BfScopeData* mDeferScopeAlloc;
	bool mUsedAsStatement;
	bool mPropDefBypassVirtual;
	bool mExplicitCast;
	bool mResolveGenericParam;
	bool mNoBind;
	bool mIsVolatileReference;
	bool mIsHeapReference;
	bool mResultIsTempComposite;
	bool mAllowReadOnlyReference;
	bool mInsidePendingNullable;

public:
	BfExprEvaluator(BfModule* module);
	~BfExprEvaluator();

	void GetLiteral(BfAstNode* refNode, const BfVariant& variant);
	void FinishExpressionResult();	
	virtual bool CheckAllowValue(const BfTypedValue& typedValue, BfAstNode* refNode);
	BfAutoComplete* GetAutoComplete();	
	bool IsComptime();
	bool IsComptimeEntry();
	int GetStructRetIdx(BfMethodInstance* methodInstance, bool forceStatic = false);
	BfTypedValue SetupNullConditional(BfTypedValue target, BfTokenNode* dotToken);
	void Evaluate(BfAstNode* astNode, bool propogateNullConditional = false, bool ignoreNullConditional = false, bool allowSplat = true);	
	BfType* BindGenericType(BfAstNode* node, BfType* bindType);
	BfType* ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType = BfPopulateType_Data, BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)0);
	void ResolveGenericType();	
	void ResolveArgValues(BfResolvedArgs& resolvedArgs, BfResolveArgsFlags flags = BfResolveArgsFlag_None);
	BfAllocTarget ResolveAllocTarget(BfAstNode* newNode, BfTokenNode*& newToken, BfCustomAttributes** outCustomAttributes = NULL);
	BfTypedValue ResolveArgValue(BfResolvedArg& resolvedArg, BfType* wantType, BfTypedValue* receivingValue = NULL, BfParamKind paramKind = BfParamKind_Normal, BfIdentifierNode* paramNameNode = NULL);
	BfMethodDef* GetPropertyMethodDef(BfPropertyDef* propDef, BfMethodType methodType, BfCheckedKind checkedKind, BfTypedValue propTarget);
	BfModuleMethodInstance GetPropertyMethodInstance(BfMethodDef* methodDef);
	void CheckPropFail(BfMethodDef* propMethodDef, BfMethodInstance* methodInstance, bool checkProt);
	bool HasResult();
	BfTypedValue GetResult(bool clearResult = false, bool resolveGenericType = false);				
	void CheckResultForReading(BfTypedValue& typedValue);
	void MarkResultUsed();
	void MarkResultAssigned();
	void MakeResultAsValue();	
	bool CheckIsBase(BfAstNode* checkNode);
	bool CheckModifyResult(BfTypedValue typeValue, BfAstNode* refNode, const char* modifyType, bool onlyNeedsMut = false, bool emitWarning = false, bool skipCopyOnMutate = false);
	bool CheckGenericCtor(BfGenericParamType* genericParamType, BfResolvedArgs& argValues, BfAstNode* targetSrc);
	BfTypedValue LookupField(BfAstNode* targetSrc, BfTypedValue target, const StringImpl& fieldName, BfLookupFieldFlags flags = BfLookupFieldFlag_None);	
	void CheckObjectCreateTypeRef(BfType* expectingType, BfAstNode* afterNode);
	void LookupQualifiedName(BfQualifiedNameNode* nameNode, bool ignoreInitialError = false, bool* hadError = NULL);
	void LookupQualifiedName(BfAstNode* nameNode, BfIdentifierNode* nameLeft, BfIdentifierNode* nameRight, bool ignoreInitialError, bool* hadError = NULL);
	void LookupQualifiedStaticField(BfQualifiedNameNode* nameNode, bool ignoreIdentifierNotFoundError);	
	void LookupQualifiedStaticField(BfAstNode* nameNode, BfIdentifierNode* nameLeft, BfIdentifierNode* nameRight, bool ignoreIdentifierNotFoundError);
	bool CheckConstCompare(BfBinaryOp binaryOp, BfAstNode* opToken, const BfTypedValue& leftValue, const BfTypedValue& rightValue);
	void AddStrings(const BfTypedValue& leftValue, const BfTypedValue& rightValue, BfAstNode* refNode);
	bool PerformBinaryOperation_NullCoalesce(BfTokenNode* opToken, BfExpression* leftExpression, BfExpression* rightExpression, BfTypedValue leftValue, BfType* wantType);
	void PerformBinaryOperation(BfType* resultType, BfIRValue convLeftValue, BfIRValue convRightValue, BfBinaryOp binaryOp, BfAstNode* opToken);
	void PerformBinaryOperation(BfAstNode* leftExpression, BfAstNode* rightExpression, BfBinaryOp binaryOp, BfAstNode* opToken, BfBinOpFlags flags, BfTypedValue leftValue, BfTypedValue rightValue);
	void PerformBinaryOperation(BfExpression* leftNode, BfExpression* rightNode, BfBinaryOp binaryOp, BfTokenNode* opToken, BfBinOpFlags flags, BfTypedValue leftValue);
	void PerformBinaryOperation(BfExpression* leftNode, BfExpression* rightNode, BfBinaryOp binaryOp, BfTokenNode* opToken, BfBinOpFlags flags);
	BfTypedValue LoadLocal(BfLocalVariable* varDecl, bool allowRef = false);
	BfTypedValue LookupIdentifier(BfAstNode* identifierNode, const StringImpl& findName, bool ignoreInitialError = false, bool* hadError = NULL);
	BfTypedValue LookupIdentifier(BfIdentifierNode* identifierNode, bool ignoreInitialError = false, bool* hadError = NULL);
	void AddCallDependencies(BfMethodInstance* methodInstance);
	void PerformCallChecks(BfMethodInstance* methodInstance, BfAstNode* targetSrc);
	BfTypedValue CreateCall(BfAstNode* targetSrc, BfMethodInstance* methodInstance, BfIRValue func, bool bypassVirtual, SizedArrayImpl<BfIRValue>& irArgs, BfTypedValue* sret = NULL, bool isTailCall = false);
	BfTypedValue CreateCall(BfAstNode* targetSrc, const BfTypedValue& target, const BfTypedValue& origTarget, BfMethodDef* methodDef, BfModuleMethodInstance methodInstance, BfCreateFallFlags callFlags, SizedArrayImpl<BfResolvedArg>& argValues, BfTypedValue* argCascade = NULL);
	BfTypedValue CreateCall(BfMethodMatcher* methodMatcher, BfTypedValue target);
	void MakeBaseConcrete(BfTypedValue& typedValue);
	void SplatArgs(BfTypedValue value, SizedArrayImpl<BfIRValue>& irArgs);
	void PushArg(BfTypedValue argVal, SizedArrayImpl<BfIRValue>& irArgs, bool disableSplat = false, bool disableLowering = false, bool isIntrinsic = false);
	void PushThis(BfAstNode* targetSrc, BfTypedValue callTarget, BfMethodInstance* methodInstance, SizedArrayImpl<BfIRValue>& irArgs, bool skipMutCheck = false);
	BfTypedValue MatchConstructor(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, BfTypeInstance* targetType, 
		BfResolvedArgs& argValues, bool callCtorBodyOnly, bool allowAppendAlloc, BfTypedValue* appendIndexValue = NULL);
	BfTypedValue CheckEnumCreation(BfAstNode* targetSrc, BfTypeInstance* enumType, const StringImpl& caseName, BfResolvedArgs& argValues);
	BfTypedValue MatchMethod(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, bool allowImplicitThis, bool bypassVirtual, const StringImpl& name, 
		BfResolvedArgs& argValue, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments, BfCheckedKind checkedKind = BfCheckedKind_NotSet);
	BfTypedValue MakeCallableTarget(BfAstNode* targetSrc, BfTypedValue target);		
	BfModuleMethodInstance GetSelectedMethod(BfAstNode* targetSrc, BfTypeInstance* curTypeInst, BfMethodDef* methodDef, BfMethodMatcher& methodMatcher, BfType** overrideReturnType = NULL);
	bool CheckVariableDeclaration(BfAstNode* checkNode, bool requireSimpleIfExpr, bool exprMustBeTrue, bool silentFail);
	bool HasVariableDeclaration(BfAstNode* checkNode);
	void DoInvocation(BfAstNode* target, BfMethodBoundExpression* methodBoundExpr, const BfSizedArray<BfExpression*>& args, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArgs, BfTypedValue* outCascadeValue = NULL);	
	int GetMixinVariable();	
	void CheckLocalMethods(BfAstNode* targetSrc, BfTypeInstance* typeInstance, const StringImpl& methodName, BfMethodMatcher& methodMatcher, BfMethodType methodType);
	void InjectMixin(BfAstNode* targetSrc, BfTypedValue target, bool allowImplicitThis, const StringImpl& name, const BfSizedArray<BfExpression*>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArgs);
	void SetMethodElementType(BfAstNode* target);
	BfTypedValue DoImplicitArgCapture(BfAstNode* refNode, BfIdentifierNode* identifierNode, int shadowIdx);
	BfTypedValue DoImplicitArgCapture(BfAstNode* refNode, BfMethodInstance* methodInstance, int paramIdx, bool& failed, BfImplicitParamKind paramKind = BfImplicitParamKind_General, const BfTypedValue& methodRefTarget = BfTypedValue());
	bool CanBindDelegate(BfDelegateBindExpression* delegateBindExpr, BfMethodInstance** boundMethod = NULL, BfType* origMethodExpectingType = NULL, BfTypeVector* methodGenericArgumentsSubstitute = NULL);
	bool IsExactMethodMatch(BfMethodInstance* methodA, BfMethodInstance* methodB, bool ignoreImplicitParams = false);		
	BfTypeInstance* VerifyBaseDelegateType(BfTypeInstance* delegateType);	
	void ConstResolve(BfExpression* expr);
	void ProcessArrayInitializer(BfTokenNode* openToken, const BfSizedArray<BfExpression*>& values, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken, int dimensions, SizedArrayImpl<int64>& dimLengths, int dim, bool& hasFailed);
	BfLambdaInstance* GetLambdaInstance(BfLambdaBindExpression* lambdaBindExpr, BfAllocTarget& allocTarget);
	void VisitLambdaBodies(BfAstNode* body, BfFieldDtorDeclaration* fieldDtor);	
	void FixitAddMember(BfTypeInstance* typeInst, BfType* fieldType, const StringImpl& fieldName, bool isStatic);	
	void PerformUnaryOperation(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags);
	BfTypedValue PerformUnaryOperation_TryOperator(const BfTypedValue& inValue, BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags);
	void PerformUnaryOperation_OnResult(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags);
	void PerformAssignment(BfAssignmentExpression* assignExpr, bool evaluatedLeft, BfTypedValue rightValue, BfTypedValue* outCascadeValue = NULL);	
	void PopulateDeferrredTupleAssignData(BfTupleExpression* tupleExr, DeferredTupleAssignData& deferredTupleAssignData);
	void AssignDeferrredTupleAssignData(BfAssignmentExpression* assignExpr, DeferredTupleAssignData& deferredTupleAssignData, BfTypedValue rightValue);
	void DoTupleAssignment(BfAssignmentExpression* assignExpr);	
	void FinishDeferredEvals(SizedArrayImpl<BfResolvedArg>& argValues);	
	void FinishDeferredEvals(BfResolvedArgs& argValues);
	bool LookupTypeProp(BfTypeOfExpression* typeOfExpr, BfIdentifierNode* propName);
	void DoTypeIntAttr(BfTypeReference* typeRef, BfToken token);
	//void InitializedSizedArray(BfTupleExpression* createExpr, BfSizedArrayType* arrayType);
	void InitializedSizedArray(BfSizedArrayType* sizedArrayType, BfTokenNode* openToken, const BfSizedArray<BfExpression*>& values, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken, BfTypedValue* receivingValue = NULL);
	void CheckDotToken(BfTokenNode* tokenNode);
	void DoMemberReference(BfMemberReferenceExpression* memberRefExpr, BfTypedValue* outCascadeValue);
	void CreateObject(BfObjectCreateExpression* objCreateExpr, BfAstNode* allocNode, BfType* allocType);

	//////////////////////////////////////////////////////////////////////////
	
	virtual void Visit(BfErrorNode* errorNode) override;
	virtual void Visit(BfTypeReference* typeRef) override;
	virtual void Visit(BfAttributedExpression* attribExpr) override;
	virtual void Visit(BfBlock* blockExpr) override;
	virtual void Visit(BfVariableDeclaration* varDecl) override;
	virtual void Visit(BfCaseExpression* caseExpr) override;
	virtual void Visit(BfTypedValueExpression* typedValueExpr) override;
	virtual void Visit(BfLiteralExpression* literalExpr) override;	
	virtual void Visit(BfStringInterpolationExpression* stringInterpolationExpression) override;
	virtual void Visit(BfIdentifierNode* identifierNode) override;
	virtual void Visit(BfAttributedIdentifierNode* attrIdentifierNode) override;
	virtual void Visit(BfQualifiedNameNode* nameNode) override;
	virtual void Visit(BfThisExpression* thisExpr) override;
	virtual void Visit(BfBaseExpression* baseExpr) override;
	virtual void Visit(BfMixinExpression* mixinExpr) override;
	virtual void Visit(BfSizedArrayCreateExpression* createExpr) override;
	virtual void Visit(BfInitializerExpression* initExpr) override;
	virtual void Visit(BfCollectionInitializerExpression* initExpr) override;	
	virtual void Visit(BfTypeOfExpression* typeOfExpr) override;
	virtual void Visit(BfSizeOfExpression* sizeOfExpr) override;
	virtual void Visit(BfAlignOfExpression* alignOfExpr) override;
	virtual void Visit(BfStrideOfExpression* strideOfExpr) override;	
	virtual void Visit(BfDefaultExpression* defaultExpr) override;
	virtual void Visit(BfUninitializedExpression* uninitialziedExpr) override;
	virtual void Visit(BfCheckTypeExpression* checkTypeExpr) override;
	virtual void Visit(BfDynamicCastExpression* dynCastExpr) override;
	virtual void Visit(BfCastExpression* castExpr) override;
	virtual void Visit(BfDelegateBindExpression* delegateBindExpr) override;
	virtual void Visit(BfLambdaBindExpression* lambdaBindExpr) override;
	virtual void Visit(BfObjectCreateExpression* objCreateExpr) override;
	virtual void Visit(BfBoxExpression* boxExpr) override;
	virtual void Visit(BfInvocationExpression* invocationExpr) override;	
	virtual void Visit(BfConditionalExpression* condExpr) override;
	virtual void Visit(BfAssignmentExpression* assignExpr) override;
	virtual void Visit(BfParenthesizedExpression* parenExpr) override;
	virtual void Visit(BfTupleExpression* tupleExpr) override;
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override;
	virtual void Visit(BfIndexerExpression* indexerExpr) override;
	virtual void Visit(BfUnaryOperatorExpression* unaryOpExpr) override;
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr) override; 	
};

NS_BF_END