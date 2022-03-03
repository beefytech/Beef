#pragma once

#include "X86Target.h"
#include "Compiler/BfAst.h"
#include "DwAutoComplete.h"
#include "DbgModule.h"

namespace Beefy
{
	class BfPassInstance;
	class BfExpression;
}

NS_BF_DBG_BEGIN

class WinDebugger;
class WdStackFrame;

typedef WdStackFrame CPUStackFrame;

enum DbgEvalExprFlags
{
	DbgEvalExprFlags_None = 0,
	DbgEvalExprFlags_AllowTypeResult = 1,
	DbgEvalExprFlags_NoCast = 2
};

class DbgTypedValue
{
public:
	DbgType* mType;
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
		const char* mCharPtr;
		addr_target mPtr;
		const void* mLocalPtr;
		intptr mLocalIntPtr;
		DbgVariable* mVariable;
	};
	bool mIsLiteral;	
	bool mHasNoValue;
	bool mIsReadOnly;
	union 
	{
		int mRegNum;
		int mDataLen;
	};	
	addr_target mSrcAddress;

public:
	DbgTypedValue()
	{
		mType = NULL;
		mUInt64 = 0;
		mIsLiteral = false;		
		mSrcAddress = 0;
		mHasNoValue = false;
		mIsReadOnly = false;
		mRegNum = -1;
	}
	
	DbgType* ResolveTypeDef() const
	{
		auto typeDef = mType;
		while ((typeDef != NULL) && (typeDef->mTypeCode == DbgType_TypeDef))
			typeDef = typeDef->mTypeParam;
		return typeDef;
	}

	int64 GetSExtInt() const
	{		
		auto resolvedType = mType->RemoveModifiers();
		switch (resolvedType->mTypeCode)
		{
		case DbgType_Bool:
		case DbgType_i8:
			return (int64) mInt8;
		case DbgType_i16:
			return (int64) mInt16;
		case DbgType_i32:
			return (int64) mInt32;
		case DbgType_i64:
			return (int64) mInt64;
		default:
			BF_FATAL("Invalid type");
		}
		return 0;
	}

	int64 GetInt64() const
	{
		auto resolvedType = mType->RemoveModifiers();

		if (resolvedType->mTypeCode == DbgType_Enum)
		{
			if (resolvedType->mTypeParam->GetByteCount() == 0)
				resolvedType = resolvedType->GetPrimaryType();
			resolvedType = resolvedType->mTypeParam;
		}
		else if (resolvedType->mTypeCode == DbgType_Struct)
		{
			resolvedType = resolvedType->GetPrimaryType();
			if (resolvedType->IsTypedPrimitive())
				resolvedType = resolvedType->GetUnderlyingType();
		}

		switch (resolvedType->mTypeCode)
		{
		case DbgType_Bool:
		case DbgType_i8:
		case DbgType_SChar:
			return (int64) mInt8;
		case DbgType_i16:
		case DbgType_SChar16:
			return (int64) mInt16;
		case DbgType_i32:
		case DbgType_SChar32:
			return (int64) mInt32;
		case DbgType_i64:
			return (int64) mInt64;
		case DbgType_UChar:
		case DbgType_u8:
			return (int64) mUInt8;
		case DbgType_UChar16:
		case DbgType_u16:
			return (int64) mUInt16;
		case DbgType_UChar32:
		case DbgType_u32:
			return (int64) mUInt32;
		case DbgType_u64:
			return (int64) mUInt64;		
		case DbgType_Ptr:
			return (int64) mUInt64;
		default:
			return 0;
			//BF_FATAL("Invalid type");
		}
		return 0;
	}

	addr_target GetPointer() const
	{
		if (mType->IsSizedArray())
			return mSrcAddress;
		if (mType->IsInteger())
			return GetInt64();
		return mPtr;
	}

	String GetString() const
	{
		if (!mType->IsPointer())
			return "";
		if ((mType->mTypeParam->mTypeCode != DbgType_SChar) && (mType->mTypeParam->mTypeCode != DbgType_UChar))
			return "";
		if (mIsLiteral)
			return mCharPtr;
		//return (const char*)mPtr;
		return "";
	}

	operator bool() const
	{
		return (mType != NULL) && (!mHasNoValue);
	}

	static DbgTypedValue GetValueless(DbgType* dbgType)
	{
		DbgTypedValue dbgTypedValue;
		dbgTypedValue.mType = dbgType;
		dbgTypedValue.mHasNoValue = true;
		return dbgTypedValue;
	}
};

struct DbgMethodArgument
{
	DbgTypedValue mTypedValue;
	bool mWantsRef;

	DbgMethodArgument()
	{
		mWantsRef = false;
	}
};

typedef Array<DbgType*> DwTypeVector;

class DbgExprEvaluator;

class DwMethodMatcher
{
public:
	BfAstNode* mTargetSrc;
	DbgExprEvaluator* mExprEvaluator;
	String mMethodName;
	SizedArrayImpl<DbgTypedValue>& mArguments;
	bool mHadExplicitGenericArguments;
	BfType* mExplicitInterfaceCheck;
	bool mTargetIsConst;

	DwTypeVector mCheckMethodGenericArguments;

	DbgSubprogram* mBackupMethodDef;
	DbgSubprogram* mBestMethodDef;
	DbgType* mBestMethodTypeInstance;
	SizedArray<int, 4> mBestMethodGenericArgumentSrcs;
	DwTypeVector mBestMethodGenericArguments;

public:	
	void CompareMethods(DbgSubprogram* prevMethodInstance, DwTypeVector* prevGenericArgumentsSubstitute,
		DbgSubprogram* newMethodInstance, DwTypeVector* genericArgumentsSubstitute,
		bool* outNewIsBetter, bool* outNewIsWorse, bool allowSpecializeFail);

public:
	DwMethodMatcher(BfAstNode* targetSrc, DbgExprEvaluator* exprEvaluator, const StringImpl& methodName, SizedArrayImpl<DbgTypedValue>& arguments, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments);
	bool CheckType(DbgType* typeInstance, bool isFailurePass);
	bool CheckMethod(DbgType* typeInstance, DbgSubprogram* checkMethod);
};

class DbgCallResult
{
public:
	DbgSubprogram* mSubProgram;
	DbgTypedValue mResult;
	DbgTypedValue mStructRetVal;
	Array<uint8> mSRetData;
};

class DbgExprEvaluator : public BfStructuralVisitor
{
public:
	struct NodeReplaceRecord
	{
		BfAstNode** mNodeRef;
		BfAstNode* mNode;
		bool mForceTypeRef;

		NodeReplaceRecord(BfAstNode* node, BfAstNode** nodeRef, bool forceTypeRef = false)
		{
			mNode = node;
			mNodeRef = nodeRef;
			mForceTypeRef = forceTypeRef;
		}
	};

	struct SplatLookupEntry
	{
		String mFindName;
		bool mIsConst;
		DbgTypedValue mResult;

		SplatLookupEntry()
		{
			mIsConst = false;
		}
	};

public:
	DebugTarget* mDebugTarget;	
	DbgModule* mOrigDbgModule;
	DbgModule* mDbgModule;
	DbgCompileUnit* mDbgCompileUnit;	

	DbgLanguage mLanguage;
	BfPassInstance* mPassInstance;	
	WinDebugger* mDebugger;
	String mExpectingTypeName;
	String mSubjectExpr;
	DbgTypedValue mSubjectValue;
	DbgType* mExpectingType;
	DbgSubprogram* mCurMethod;
	DbgTypedValue mResult;
	DbgTypedValue* mReceivingValue;
	intptr mCountResultOverride;
	DbgTypedValue mExplicitThis;	
	BfExpression* mExplicitThisExpr;
	Array<DbgCallResult>* mCallResults;	
	addr_target mCallStackPreservePos;
	int mCallResultIdx;
	String mNamespaceSearchStr;
	Array<DbgType*> mNamespaceSearch;
	Dictionary<BfAstNode*, SplatLookupEntry> mSplatLookupMap;

	DbgTypedValue mPropTarget;
	DbgSubprogram* mPropSet;
	DbgSubprogram* mPropGet;
	BfAstNode* mPropSrc;
	SizedArray<BfExpression*, 2> mIndexerExprValues;
	SizedArray<DbgTypedValue, 2> mIndexerValues;

	bool mIsEmptyTarget;
	DwEvalExpressionFlags mExpressionFlags;
	bool mHadSideEffects;
	bool mBlockedSideEffects;	
	bool mIgnoreErrors;
	bool mCreatedPendingCall;
	bool mValidateOnly;
	int mCallStackIdx;
	int mCursorPos;	

	DwAutoComplete* mAutoComplete;

	Array<Array<uint8>> mTempStorage;
	Array<NodeReplaceRecord> mDeferredInsertExplicitThisVector;

	String* mReferenceId;
	bool mHadMemberReference;
	bool mIsComplexExpression;

public:
	DbgTypedValue ReadTypedValue(BfAstNode* targetSrc, DbgType* type, uint64 valAddr, DbgAddrType addrType);
	bool CheckTupleCreation(addr_target receiveAddr, BfAstNode* targetSrc, DbgType* tupleType, const BfSizedArray<BfExpression*>& argValues, BfSizedArray<BfTupleNameNode*>* names);
	DbgTypedValue CheckEnumCreation(BfAstNode* targetSrc, DbgType* enumType, const StringImpl& caseName, const BfSizedArray<BfExpression*>& argValues);
	void DoInvocation(BfAstNode* target, BfSizedArray<ASTREF(BfExpression*)>& args, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments);
	bool ResolveArgValues(const BfSizedArray<ASTREF(BfExpression*)>& arguments, SizedArrayImpl<DbgTypedValue>& outArgValues);	
	DbgTypedValue CreateCall(DbgSubprogram* method, DbgTypedValue thisVal, DbgTypedValue structRetVal, bool bypassVirtual, CPURegisters* registers);
	DbgTypedValue CreateCall(DbgSubprogram* method, SizedArrayImpl<DbgMethodArgument>& argPushQueue, bool bypassVirtual);
	DbgTypedValue CreateCall(BfAstNode* targetSrc, DbgTypedValue target, DbgSubprogram* methodDef, bool bypassVirtual, const BfSizedArray<ASTREF(BfExpression*)>& arguments, SizedArrayImpl<DbgTypedValue>& argValues);
	DbgTypedValue MatchMethod(BfAstNode* targetSrc, DbgTypedValue target, bool allowImplicitThis, bool bypassVirtual, const StringImpl& methodName,
		const BfSizedArray<ASTREF(BfExpression*)>& arguments, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments);		
	DbgType* ResolveSubTypeRef(DbgType* checkType, const StringImpl& name);
	void PerformBinaryOperation(ASTREF(BfExpression*)& leftExpression, ASTREF(BfExpression*)& rightExpression, BfBinaryOp binaryOp, BfTokenNode* opToken, bool forceLeftType);
	void PerformBinaryOperation(DbgType* resultType, DbgTypedValue convLeftValue, DbgTypedValue convRightValue, BfBinaryOp binaryOp, BfTokenNode* opToken);
	void PerformUnaryExpression(BfAstNode* opToken, BfUnaryOp unaryOp, ASTREF(BfExpression*)& expr);
	DbgTypedValue CreateValueFromExpression(ASTREF(BfExpression*)& expr, DbgType* castToType = NULL, DbgEvalExprFlags flags = DbgEvalExprFlags_None);	
	const char* GetTypeName(DbgType* type);
	DbgTypedValue GetResult();
	bool HasPropResult();
	DbgLanguage GetLanguage();
	DbgFlavor GetFlavor();

public:
	DbgExprEvaluator(WinDebugger* winDebugger, DbgModule* dbgModule, BfPassInstance* passInstance, int callStackIdx, int cursorPos);
	~DbgExprEvaluator();
	
	DbgTypedValue GetInt(int value);
	DbgTypedValue GetString(const StringImpl& str);

	void Fail(const StringImpl& error, BfAstNode* node);	
	void Warn(const StringImpl& error, BfAstNode* node);
	DbgType* GetExpectingType();
	void GetNamespaceSearch();
	DbgType* FixType(DbgType* dbgType);
	DbgTypedValue FixThis(const DbgTypedValue& thisVal);
	DbgType* ResolveTypeRef(BfTypeReference* typeRef);
	DbgType* ResolveTypeRef(BfAstNode* typeRef, BfAstNode** parentChildRef = NULL);
	DbgType* ResolveTypeRef(const StringImpl& typeRef);
	static bool TypeIsSubTypeOf(DbgType* srcType, DbgType* wantType, int* thisOffset = NULL, addr_target* thisAddr = NULL);	
	DbgTypedValue GetBeefTypeById(int typeId);
	void BeefStringToString(addr_target addr, String& outStr);
	void BeefStringToString(const DbgTypedValue& val, String& outStr);
	void BeefTypeToString(const DbgTypedValue& val, String& outStr);	
	CPUStackFrame* GetStackFrame();
	CPURegisters* GetRegisters();
	DbgTypedValue GetRegister(const StringImpl& regName);
	DbgSubprogram* GetCurrentMethod();
	DbgType* GetCurrentType();
	DbgTypedValue GetThis();
	DbgTypedValue GetDefaultTypedValue(DbgType* srcType);
	bool IsAutoCompleteNode(BfAstNode* node, int lengthAdd = 0);
	void AutocompleteCheckType(BfTypeReference* typeReference);
	void AutocompleteAddTopLevelTypes(const StringImpl& filter);
	void AutocompleteAddMethod(const char* methodName, const StringImpl& filter);
	void AutocompleteAddMembers(DbgType* dbgType, bool wantsStatic, bool wantsNonStatic, const StringImpl& filter, bool isCapture = false);	
	void AutocompleteCheckMemberReference(BfAstNode* target, BfAstNode* dotToken, BfAstNode* memberName);
	DbgTypedValue RemoveRef(DbgTypedValue typedValue);	
	bool StoreValue(DbgTypedValue& ptr, DbgTypedValue& value, BfAstNode* refNode);
	bool StoreValue(DbgTypedValue& ptr, BfExpression* expr);

	String TypeToString(DbgType* type);
	bool CheckHasValue(DbgTypedValue typedValue, BfAstNode* refNode);
	bool CanCast(DbgTypedValue typedVal, DbgType* toType, BfCastFlags castFlags = BfCastFlags_None);
	DbgTypedValue Cast(BfAstNode* srcNode, const DbgTypedValue& val, DbgType* toType, bool explicitCast = false, bool silentFail = false);
	bool HasField(DbgType* type, const StringImpl& fieldName);
	DbgTypedValue DoLookupField(BfAstNode* targetSrc, DbgTypedValue target, DbgType* curCheckType, const StringImpl& fieldName, CPUStackFrame* stackFrame, bool allowImplicitThis);	
	DbgTypedValue LookupField(BfAstNode* targetSrc, DbgTypedValue target, const StringImpl& fieldName);	
	DbgTypedValue LookupIdentifier(BfAstNode* identifierNode, bool ignoreInitialError = false, bool* hadError = NULL);
	void LookupSplatMember(const DbgTypedValue& target, const StringImpl& fieldName);
	void LookupSplatMember(BfAstNode* srcNode, BfAstNode* lookupNode, const DbgTypedValue& target, const StringImpl& fieldName, String* outFindName = NULL, bool* outIsConst = NULL, StringImpl* forceName = NULL);
	void LookupQualifiedName(BfQualifiedNameNode* nameNode, bool ignoreInitialError = false, bool* hadError = NULL);
	DbgType* FindSubtype(DbgType* type, const StringImpl& name);
	void LookupQualifiedStaticField(BfQualifiedNameNode* nameNode, bool ignoreIdentifierNotFoundError = false);		
	bool EnsureRunning(BfAstNode* astNode);	

	virtual void Visit(BfAssignmentExpression* assignExpr) override;
	virtual void Visit(BfParenthesizedExpression* parenExpr) override;
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override;
	virtual void Visit(BfIndexerExpression* indexerExpr) override;
	virtual void Visit(BfQualifiedNameNode* nameNode) override;
	virtual void Visit(BfThisExpression* thisExpr) override;
	virtual void Visit(BfIdentifierNode* node) override;
	virtual void Visit(BfAttributedIdentifierNode* node) override;
	virtual void Visit(BfMixinExpression* mixinExpr) override;
	virtual void Visit(BfAstNode* node) override;
	virtual void Visit(BfDefaultExpression* defaultExpr) override;
	virtual void Visit(BfLiteralExpression* literalExpr) override;
	virtual void Visit(BfCastExpression* castExpr) override;
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr) override;
	virtual void Visit(BfUnaryOperatorExpression* unaryOpExpr) override;
	virtual void Visit(BfInvocationExpression* invocationExpr) override;
	virtual void Visit(BfConditionalExpression* condExpr) override;
	virtual void Visit(BfTypeAttrExpression* typeAttrExpr) override;	
	virtual void Visit(BfTupleExpression* tupleExpr) override;

	DbgTypedValue Resolve(BfExpression* expr, DbgType* wantType = NULL);	
	BfAstNode* FinalizeExplicitThisReferences(BfAstNode* headNode);
};

NS_BF_DBG_END