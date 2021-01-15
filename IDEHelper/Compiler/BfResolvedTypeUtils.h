#pragma once
#pragma once

#include "BfAst.h"

#include <unordered_map>
#include "BfSource.h"
#include "BfIRBuilder.h"
#include "BeefySysLib/util/MultiHashSet.h"

NS_BF_BEGIN

class BfModule;
class BfType;
class BfTypeInstance;
class BfContext;
class BfCustomAttributes;

enum BfResolveTypeRefFlags
{
	BfResolveTypeRefFlag_None = 0,
	BfResolveTypeRefFlag_NoResolveGenericParam = 1,
	BfResolveTypeRefFlag_AllowRef = 2,
	BfResolveTypeRefFlag_AllowRefGeneric = 4,
	BfResolveTypeRefFlag_IgnoreLookupError = 8,
	BfResolveTypeRefFlag_AllowGenericTypeParamConstValue = 0x10,
	BfResolveTypeRefFlag_AllowGenericMethodParamConstValue = 0x20,
	BfResolveTypeRefFlag_AllowGenericParamConstValue = 0x10 | 0x20,
	BfResolveTypeRefFlag_AutoComplete = 0x40,
	BfResolveTypeRefFlag_FromIndirectSource = 0x80, // Such as a type alias or a generic parameter 
	BfResolveTypeRefFlag_Attribute = 0x100,
	BfResolveTypeRefFlag_NoReify = 0x200,
	BfResolveTypeRefFlag_NoCreate = 0x400,
	BfResolveTypeRefFlag_NoWarnOnMut = 0x800
};

enum BfTypeNameFlags : uint16
{
	BfTypeNameFlags_None = 0,
	BfTypeNameFlag_ResolveGenericParamNames = 1,
	BfTypeNameFlag_UseUnspecializedGenericParamNames = 2,
	BfTypeNameFlag_OmitNamespace = 4,
	BfTypeNameFlag_OmitOuterType = 8,
	BfTypeNameFlag_ReduceName = 0x10,
	BfTypeNameFlag_UseArrayImplType = 0x20,
	BfTypeNameFlag_DisambiguateDups = 0x40, // Add a disambiguation if mDupDetectedRevision is set	
	BfTypeNameFlag_AddGlobalContainerName = 0x80,
	BfTypeNameFlag_InternalName = 0x100, // Use special delimiters to remove ambiguities (ie: '+' for inner types)	
	BfTypeNameFlag_HideGlobalName = 0x200,
	BfTypeNameFlag_ExtendedInfo = 0x400
};

enum BfMethodNameFlags : uint8
{
	BfMethodNameFlag_None = 0,
	BfMethodNameFlag_ResolveGenericParamNames = 1,
	BfMethodNameFlag_OmitTypeName = 2,
	BfMethodNameFlag_IncludeReturnType = 4,
	BfMethodNameFlag_OmitParams = 8
};

enum BfGetMethodInstanceFlags : uint16
{
	BfGetMethodInstanceFlag_None = 0,
	BfGetMethodInstanceFlag_UnspecializedPass = 1,
	BfGetMethodInstanceFlag_ExplicitSpecializedModule = 2,
	BfGetMethodInstanceFlag_ExplicitResolveOnlyPass = 4,
	BfGetMethodInstanceFlag_ForeignMethodDef = 8,
	BfGetMethodInstanceFlag_Unreified = 0x10,
	BfGetMethodInstanceFlag_NoForceReification = 0x20,
	BfGetMethodInstanceFlag_ResultNotUsed = 0x40,
	BfGetMethodInstanceFlag_ForceInline = 0x80,
	BfGetMethodInstanceFlag_Friend = 0x100,
	BfGetMethodInstanceFlag_DisableObjectAccessChecks = 0x200,
	BfGetMethodInstanceFlag_NoInline = 0x400,
	BfGetMethodInstanceFlag_DepthExceeded = 0x800,
	BfGetMethodInstanceFlag_NoReference = 0x1000,
	BfGetMethodInstanceFlag_MethodInstanceOnly = 0x2000
};

class BfDependencyMap
{
public:
	enum DependencyFlags
	{
		DependencyFlag_None					= 0,
		DependencyFlag_Calls				= 1,
		DependencyFlag_InlinedCall			= 2,
		DependencyFlag_ReadFields			= 4,
		DependencyFlag_DerivedFrom			= 8,
		DependencyFlag_OuterType			= 0x10,
		DependencyFlag_ImplementsInterface	= 0x20,
		DependencyFlag_ValueTypeMemberData	= 0x40,
		DependencyFlag_PtrMemberData		= 0x80,
		DependencyFlag_StaticValue			= 0x100,
		DependencyFlag_ConstValue			= 0x200,
		DependencyFlag_ConstEvalConstField	= 0x400, // Used a field that was CE-generated
		DependencyFlag_ConstEval			= 0x800,
		DependencyFlag_MethodGenericArg		= 0x1000,
		DependencyFlag_LocalUsage			= 0x2000,
		DependencyFlag_ExprTypeReference	= 0x4000,
		DependencyFlag_TypeGenericArg		= 0x8000,
		DependencyFlag_UnspecializedType    = 0x10000,
		DependencyFlag_GenericArgRef		= 0x20000,
		DependencyFlag_ParamOrReturnValue	= 0x40000,
		DependencyFlag_CustomAttribute		= 0x80000,
		DependencyFlag_Constraint			= 0x100000,
		DependencyFlag_StructElementType	= 0x200000,
		DependencyFlag_TypeReference		= 0x400000, // Explicit type reference for things like tuples, so all referencing types get passed over on symbol reference
		DependencyFlag_Allocates			= 0x800000,
		DependencyFlag_NameReference		= 0x1000000,
		DependencyFlag_VirtualCall			= 0x2000000,
		DependencyFlag_WeakReference		= 0x4000000, // Keeps alive but won't rebuild		

		DependencyFlag_DependentUsageMask = ~(DependencyFlag_UnspecializedType | DependencyFlag_MethodGenericArg | DependencyFlag_GenericArgRef)
	};

	struct DependencyEntry
	{
		int mRevision;		
		DependencyFlags mFlags;

		DependencyEntry(int revision, DependencyFlags flags)
		{
			mRevision = revision;			
			mFlags = flags;			
		}
	};

public:
	typedef Dictionary<BfType*, DependencyEntry> TypeMap;	
	DependencyFlags mFlagsUnion;
	TypeMap mTypeSet;
	int mMinDependDepth;

public:
	BfDependencyMap()
	{
		mMinDependDepth = 0;
		mFlagsUnion = DependencyFlag_None;
	}

	bool AddUsedBy(BfType* dependentType, DependencyFlags flags);	
	bool IsEmpty();
	TypeMap::iterator begin();
	TypeMap::iterator end();
	TypeMap::iterator erase(TypeMap::iterator& itr);	
};

enum BfHotDepDataKind : int8
{
	BfHotDepDataKind_Unknown,
	BfHotDepDataKind_TypeVersion,
	BfHotDepDataKind_ThisType,
	BfHotDepDataKind_Allocation,	
	BfHotDepDataKind_Method,
	BfHotDepDataKind_DupMethod,
	BfHotDepDataKind_DevirtualizedMethod,
	BfHotDepDataKind_InnerMethod,
	BfHotDepDataKind_FunctionPtr,
	BfHotDepDataKind_VirtualDecl
};

enum BfHotDepDataFlags : int8
{
	BfHotDepDataFlag_None = 0,
	BfHotDepDataFlag_AlwaysCalled = 1,
	BfHotDepDataFlag_NotReified = 2,
	BfHotDepDataFlag_IsOriginalBuild = 4,
	BfHotDepDataFlag_IsBound = 8,
	BfHotDepDataFlag_HasDup = 0x10,
	BfHotDepDataFlag_RetainMethodWithoutBinding = 0x20,
};

class BfHotDepData
{
public:
	BfHotDepDataKind mDataKind;
	BfHotDepDataFlags mFlags;
	int32 mRefCount;

	BfHotDepData()
	{
		mDataKind = BfHotDepDataKind_Unknown;
		mFlags = BfHotDepDataFlag_None;
		mRefCount = 0;
	}

	void Deref();

#ifdef _DEBUG
	virtual ~BfHotDepData()
	{		
		BF_ASSERT(mRefCount == 0);
	}
#endif
};

struct BfHotTypeDataEntry
{
public:
	String mFuncName; // Name of original virtual function, not overrides		
};

class BfHotTypeVersion : public BfHotDepData
{
public:	
	Val128 mDataHash; // Determines when the user's declaration changed - changes do not cascade into including types
	BfHotTypeVersion* mBaseType;
	Array<BfHotTypeVersion*> mMembers; // Struct members directly included (not via pointers)
	int mDeclHotCompileIdx;
	int mCommittedHotCompileIdx;
	int mTypeId;

	Array<int> mInterfaceMapping;	
	bool mPopulatedInterfaceMapping;

	BfHotTypeVersion()
	{
		mBaseType = NULL;
		mDataKind = BfHotDepDataKind_TypeVersion;		
		mPopulatedInterfaceMapping = 0;
	}

	~BfHotTypeVersion();
};

class BfHotThisType : public BfHotDepData
{
public:
	BfHotTypeVersion* mTypeVersion;

	BfHotThisType(BfHotTypeVersion* typeVersion)
	{
		mDataKind = BfHotDepDataKind_ThisType;
		mTypeVersion = typeVersion;
		mTypeVersion->mRefCount++;
	}

	~BfHotThisType()
	{
		mTypeVersion->Deref();		
	}
};

class BfHotAllocation : public BfHotDepData
{
public:
	BfHotTypeVersion* mTypeVersion;

	BfHotAllocation(BfHotTypeVersion* typeVersion)
	{
		mDataKind = BfHotDepDataKind_Allocation;
		mTypeVersion = typeVersion;
		mTypeVersion->mRefCount++;
	}

	~BfHotAllocation()
	{
		mTypeVersion->Deref();		
	}
};

//#define BF_DBG_HOTMETHOD_NAME
//#define BF_DBG_HOTMETHOD_IDX

class BfHotMethod : public BfHotDepData
{
public:	
#ifdef BF_DBG_HOTMETHOD_NAME
	String mMangledName;
#endif
#ifdef BF_DBG_HOTMETHOD_IDX
	int mMethodIdx;
#endif	
	BfHotTypeVersion* mSrcTypeVersion;	
	Array<BfHotDepData*> mReferences;
	BfHotMethod* mPrevVersion;	

	BfHotMethod()
	{
#ifdef BF_DBG_HOTMETHOD_IDX
		mMethodIdx = -1;
#endif		
		mDataKind = BfHotDepDataKind_Method;
		mSrcTypeVersion = NULL;		
		mPrevVersion = NULL;
	}
	void Clear(bool keepDupMethods = false);
	~BfHotMethod();
};

class BfHotDupMethod : public BfHotDepData
{
public:
	BfHotMethod* mMethod;

public:
	BfHotDupMethod(BfHotMethod* hotMethod)
	{
		mDataKind = BfHotDepDataKind_DupMethod;
		mMethod = hotMethod;
		mMethod->mRefCount++;
	}

	~BfHotDupMethod()
	{
		mMethod->Deref();
	}
};


class BfHotDevirtualizedMethod : public BfHotDepData
{
public:
	BfHotMethod* mMethod;

	BfHotDevirtualizedMethod(BfHotMethod* method)
	{
		mDataKind = BfHotDepDataKind_DevirtualizedMethod;
		mMethod = method;
		mMethod->mRefCount++;
	}

	~BfHotDevirtualizedMethod()
	{
		mMethod->Deref();		
	}
};

class BfHotFunctionReference : public BfHotDepData
{
public:
	BfHotMethod* mMethod;

	BfHotFunctionReference(BfHotMethod* method)
	{
		mDataKind = BfHotDepDataKind_FunctionPtr;
		mMethod = method;
		mMethod->mRefCount++;
	}

	~BfHotFunctionReference()
	{
		mMethod->Deref();		
	}
};

class BfHotInnerMethod : public BfHotDepData
{
public:
	BfHotMethod* mMethod;

	BfHotInnerMethod(BfHotMethod* method)
	{
		mDataKind = BfHotDepDataKind_InnerMethod;
		mMethod = method;
		mMethod->mRefCount++;
	}

	~BfHotInnerMethod()
	{
		mMethod->Deref();		
	}
};

class BfHotVirtualDeclaration : public BfHotDepData
{
public:
	BfHotMethod* mMethod;

	BfHotVirtualDeclaration(BfHotMethod* method)
	{
		mDataKind = BfHotDepDataKind_VirtualDecl;
		mMethod = method;
		mMethod->mRefCount++;
	}

	~BfHotVirtualDeclaration()
	{
		mMethod->Deref();		
	}
};

class BfHotDataReferenceBuilder
{
public:	
	HashSet<BfHotTypeVersion*> mAllocatedData; // Any usage that depends on size or layout	
	HashSet<BfHotTypeVersion*> mUsedData; // Any usage that depends on size or layout	
	HashSet<BfHotMethod*> mCalledMethods;
	HashSet<BfHotMethod*> mDevirtualizedCalledMethods;
	HashSet<BfHotMethod*> mFunctionPtrs;
	HashSet<BfHotMethod*> mInnerMethods;
};

class BfDependedType;
class BfTypeInstance;
class BfTypeInstance;
class BfPrimitiveType;

enum BfTypeRebuildFlags
{
	BfTypeRebuildFlag_None = 0,
	BfTypeRebuildFlag_StaticChange = 1,
	BfTypeRebuildFlag_NonStaticChange = 2,
	BfTypeRebuildFlag_MethodInlineInternalsChange = 4,
	BfTypeRebuildFlag_MethodSignatureChange = 8,	
	BfTypeRebuildFlag_ConstEvalChange = 0x10,
	BfTypeRebuildFlag_ConstEvalFieldChange = 0x20,
	BfTypeRebuildFlag_DeleteQueued = 0x40,
	BfTypeRebuildFlag_Deleted = 0x80,
	BfTypeRebuildFlag_AddedToWorkList = 0x100,
	BfTypeRebuildFlag_AwaitingReference = 0x200,
	BfTypeRebuildFlag_SpecializedMethodRebuild = 0x400, // Temporarily set
	BfTypeRebuildFlag_SpecializedByAutocompleteMethod = 0x800,
	BfTypeRebuildFlag_UnderlyingTypeDeferred = 0x1000,
	BfTypeRebuildFlag_TypeDataSaved = 0x2000,
	BfTypeRebuildFlag_InTempPool = 0x4000,
	BfTypeRebuildFlag_ResolvingBase = 0x8000,
	BfTypeRebuildFlag_InFailTypes = 0x10000,
	BfTypeRebuildFlag_RebuildQueued = 0x20000,
};

class BfTypeDIReplaceCallback;

enum BfTypeDefineState : uint8
{
	BfTypeDefineState_Undefined,
	BfTypeDefineState_Declared,
	BfTypeDefineState_ResolvingBaseType,	
	BfTypeDefineState_HasInterfaces,
	BfTypeDefineState_CETypeInit,
	BfTypeDefineState_CEPostTypeInit,
	BfTypeDefineState_Defined,
	BfTypeDefineState_CEAfterFields,
	BfTypeDefineState_DefinedAndMethodsSlotted,
};

class BfDelegateInfo
{
public:
	Array<BfAstNode*> mDirectAllocNodes;
	BfType* mReturnType;	
	Array<BfType*> mParams;
	bool mHasExplicitThis;

public:
	BfDelegateInfo()
	{
		mReturnType = NULL;
		mHasExplicitThis = false;
	}

	~BfDelegateInfo()
	{
		for (auto directAllocNode : mDirectAllocNodes)
			delete directAllocNode;
	}
};

enum BfTypeUsage
{
	BfTypeUsage_Unspecified,
	BfTypeUsage_Return_Static,
	BfTypeUsage_Return_NonStatic,	
	BfTypeUsage_Parameter,
};

class BfType
{
public:
	BfTypeRebuildFlags mRebuildFlags;
	
	BfContext* mContext;
	int mTypeId;
	int mRevision;	
	
	// For Objects, align and size is ref-sized (object*).  	
	//  Use mInstSize/mInstAlign for actual data size/align
	int mSize;	
	int16 mAlign; 	
	bool mDirty;
	BfTypeDefineState mDefineState;

public:
	BfType();	
	virtual ~BfType();
	
	BfTypeInstance* FindUnderlyingTypeInstance();

	virtual BfModule* GetModule();
	
	int GetStride() { return BF_ALIGN(mSize, mAlign); }	
	bool IsSizeAligned() { return (mSize == 0) || (mSize % mAlign == 0); }
	virtual bool NeedsExplicitAlignment() { return !IsSizeAligned(); }
	virtual bool IsInstanceOf(BfTypeDef* typeDef) { return false; }

	virtual bool HasBeenReferenced() { return mDefineState != BfTypeDefineState_Undefined; }
	virtual bool HasTypeFailed() { return false; }
	virtual bool IsDataIncomplete() { return mDefineState == BfTypeDefineState_Undefined; }
	virtual bool IsIncomplete() { return mDefineState < BfTypeDefineState_Defined; }
	virtual bool IsDeleting() { return ((mRebuildFlags & (BfTypeRebuildFlag_Deleted | BfTypeRebuildFlag_DeleteQueued)) != 0); }
	virtual bool IsDeclared() { return mDefineState >= BfTypeDefineState_Declared; }
	
	virtual BfDependedType* ToDependedType() { return NULL; }
	virtual BfTypeInstance* ToTypeInstance() { return NULL; }
	virtual BfTypeInstance* ToGenericTypeInstance() { return NULL; }
	virtual BfPrimitiveType* ToPrimitiveType() { return NULL; }	
	virtual bool IsDependendType() { return false; }
	virtual bool IsTypeInstance() { return false; }
	virtual bool IsGenericTypeInstance() { return false; }
	virtual bool IsUnspecializedType() { return false; }
	virtual bool IsReified() { return true; }
	virtual bool IsSpecializedType() { return false; }
	virtual bool IsSpecializedByAutoCompleteMethod() { return false; }
	virtual bool IsUnspecializedTypeVariation() { return false; }
	virtual bool IsSplattable() { return false; }
	virtual int GetSplatCount() { return 1; }
	virtual bool IsVoid() { return false; }
	virtual bool IsVoidPtr() { return false; }
	virtual bool CanBeValuelessType() { return false; }
	virtual bool IsValuelessType() { BF_ASSERT(mSize != -1); BF_ASSERT(mDefineState >= BfTypeDefineState_Defined); return mSize == 0; }
	virtual bool IsSelf() { return false; }
	virtual bool IsDot() { return false; }
	virtual bool IsVar() { return false; }	
	virtual bool IsLet() { return false; }
	virtual bool IsNull(){ return false; }
	virtual bool IsNullable() { return false; }	
	virtual bool IsBoxed() { return false; }
	virtual bool IsInterface() { return false; }
	virtual bool IsEnum() { return false; }
	virtual bool IsPayloadEnum() { return false; }
	virtual bool IsTypedPrimitive() { return false; }
	virtual bool IsComposite() { return IsStruct(); }
	virtual bool IsStruct() { return false; }
	virtual bool IsStructPtr() { return false; }
	virtual bool IsUnion() { return false; }
	virtual bool IsStructOrStructPtr() { return false; }	
	virtual bool IsObject() { return false; }
	virtual bool IsObjectOrStruct() { return false; }
	virtual bool IsObjectOrInterface() { return false; }
	virtual bool IsString() { return false; }
	virtual bool IsSizedArray() { return false; }
	virtual bool IsUndefSizedArray() { return false; }
	virtual bool IsUnknownSizedArrayType() { return false; }
	virtual bool IsArray() { return false; }	
	virtual bool IsDelegate() { return false; }
	virtual bool IsFunction() { return false; }
	virtual bool IsDelegateFromTypeRef() { return false; }
	virtual bool IsFunctionFromTypeRef() { return false; }
	virtual BfDelegateInfo* GetDelegateInfo() { return NULL;  }
	virtual bool IsValueType() { return false; }	
	virtual bool IsValueTypeOrValueTypePtr() { return false; }	
	virtual bool IsWrappableType() { return false; }
	virtual bool IsPrimitiveType() { return false; }	
	virtual bool IsBoolean() { return false; }
	virtual bool IsInteger() { return false; }
	virtual bool IsIntegral() { return false; }
	virtual bool IsIntPtr() { return false; }
	virtual bool IsSigned() { return false; }
	virtual bool IsSignedInt() { return false; }
	virtual bool IsIntUnknown() { return false; }
	virtual bool IsChar() { return false; }
	virtual bool IsFloat() { return false; }		
	virtual bool IsPointer() { return false; }
	virtual bool IsAllocType() { return false; }
	virtual bool IsIntPtrable() { return false;  }	
	virtual bool IsRef() { return false; }	
	virtual bool IsGenericParam() { return false; }
	virtual bool IsClosure() { return false; }	
	virtual bool IsMethodRef() { return false; }
	virtual bool IsTuple() { return false; }
	virtual bool IsOnDemand() { return false; }
	virtual bool IsTemporary() { return false; }	
	virtual bool IsModifiedTypeType() { return false; }
	virtual bool IsConcreteInterfaceType() { return false; }
	virtual bool IsTypeAlias() { return false; }
	virtual bool HasPackingHoles() { return false; }
	virtual bool IsConstExprValue() { return false; }
	virtual bool IsDependentOnUnderlyingType() { return false; }
	virtual bool WantsGCMarking() { return false; }
	virtual bool GetLoweredType(BfTypeUsage typeUsage, BfTypeCode* outTypeCode = NULL, BfTypeCode* outTypeCode2 = NULL) { return false; }
	virtual BfType* GetUnderlyingType() { return NULL; }	
	virtual bool HasWrappedRepresentation() { return IsWrappableType(); }
	virtual bool IsTypeMemberIncluded(BfTypeDef* declaringTypeDef, BfTypeDef* activeTypeDef = NULL, BfModule* module = NULL) { return true; } // May be 'false' only for generic extensions with constraints
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfTypeDef* activeTypeDef) { return true; }
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProject* curProject) { return true; }
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProjectSet* visibleProjectSet) { return true; }

	virtual void ReportMemory(MemReporter* memReporter);
};

// This is explicitly used for generics
typedef SizedArray<BfType*, 2> BfTypeVector;
typedef SizedArray<BfTypeReference*, 2> BfTypeRefVector;

class BfPrimitiveType : public BfType
{
public:
	BfTypeDef* mTypeDef;
	
public:
	virtual bool IsPrimitiveType() override { return true; }
	virtual bool IsWrappableType() override { return (mTypeDef->mTypeCode >= BfTypeCode_Boolean) && (mTypeDef->mTypeCode <= BfTypeCode_Double); }
	virtual BfPrimitiveType* ToPrimitiveType() override { return this; }
	//virtual bool IsValueType() override { return mTypeDef->mTypeCode != BfTypeCode_None; }
	//virtual bool IsValueTypeOrValueTypePtr() override { return mTypeDef->mTypeCode != BfTypeCode_None; }
	virtual bool IsValueType() override { return true; }
	virtual bool IsValueTypeOrValueTypePtr() override { return true; }
	virtual bool IsBoolean() override { return mTypeDef->mTypeCode == BfTypeCode_Boolean; }
	virtual bool IsIntegral() override { return (mTypeDef->mTypeCode >= BfTypeCode_Int8) && (mTypeDef->mTypeCode <= BfTypeCode_Char32); }
	virtual bool IsInteger() override { return (mTypeDef->mTypeCode >= BfTypeCode_Int8) && (mTypeDef->mTypeCode <= BfTypeCode_UIntUnknown); }	
	virtual bool IsIntPtr() override { return (mTypeDef->mTypeCode == BfTypeCode_IntPtr) || (mTypeDef->mTypeCode == BfTypeCode_UIntPtr); }
	virtual bool IsIntPtrable() override 
		{ 
			return (mTypeDef->mTypeCode == BfTypeCode_IntPtr) || (mTypeDef->mTypeCode == BfTypeCode_UIntPtr) ||
				((mTypeDef->mSystem->mPtrSize == 8) && ((mTypeDef->mTypeCode == BfTypeCode_Int64) || (mTypeDef->mTypeCode == BfTypeCode_UInt64))) ||
				((mTypeDef->mSystem->mPtrSize == 4) && ((mTypeDef->mTypeCode == BfTypeCode_Int32) || (mTypeDef->mTypeCode == BfTypeCode_UInt32)));
		}
	virtual bool IsSigned() override { return (mTypeDef->mTypeCode == BfTypeCode_Int8) || (mTypeDef->mTypeCode == BfTypeCode_Int16) || 
		(mTypeDef->mTypeCode == BfTypeCode_Int32) || (mTypeDef->mTypeCode == BfTypeCode_Int64) || (mTypeDef->mTypeCode == BfTypeCode_IntPtr) || 
		(mTypeDef->mTypeCode == BfTypeCode_IntUnknown) ||
		(mTypeDef->mTypeCode == BfTypeCode_Float) || (mTypeDef->mTypeCode == BfTypeCode_Double); }
	virtual bool IsSignedInt() override { return (mTypeDef->mTypeCode == BfTypeCode_Int8) || (mTypeDef->mTypeCode == BfTypeCode_Int16) || 
		(mTypeDef->mTypeCode == BfTypeCode_Int32) || (mTypeDef->mTypeCode == BfTypeCode_Int64) || (mTypeDef->mTypeCode == BfTypeCode_IntPtr) ||
		(mTypeDef->mTypeCode == BfTypeCode_IntUnknown); }
	virtual bool IsIntUnknown() override { return (mTypeDef->mTypeCode == BfTypeCode_IntUnknown) || (mTypeDef->mTypeCode == BfTypeCode_UIntUnknown); }
	virtual bool IsChar() override { return (mTypeDef->mTypeCode == BfTypeCode_Char8) || (mTypeDef->mTypeCode == BfTypeCode_Char16) || (mTypeDef->mTypeCode == BfTypeCode_Char32); }
	virtual bool IsFloat() override { return (mTypeDef->mTypeCode == BfTypeCode_Float) || (mTypeDef->mTypeCode == BfTypeCode_Double); }
	virtual bool IsNull() override { return mTypeDef->mTypeCode == BfTypeCode_NullPtr; }
	virtual bool IsVoid() override { return mTypeDef->mTypeCode == BfTypeCode_None; }	
	virtual bool CanBeValuelessType() override { return mTypeDef->mTypeCode == BfTypeCode_None; }
	virtual bool IsValuelessType() override { return mTypeDef->mTypeCode == BfTypeCode_None; }
	virtual bool IsSelf() override { return mTypeDef->mTypeCode == BfTypeCode_Self; }
	virtual bool IsDot() override { return mTypeDef->mTypeCode == BfTypeCode_Dot; }
	virtual bool IsVar() override { return mTypeDef->mTypeCode == BfTypeCode_Var; }	
	virtual bool IsLet() override { return mTypeDef->mTypeCode == BfTypeCode_Let; }
};

class BfTypeInstance;
class BfMethodInstanceGroup;
class BfGenericParamInstance;
class BfGenericMethodParamInstance;

class BfDeferredMethodCallData
{
public:
	// void* mPrev, int mMethodId, <method params>
	BfIRType mDeferType;
	BfIRType mDeferTypePtr;
	BfIRMDNode mDeferDIType;
	int mAlign;
	int mSize;
	int64 mMethodId; // Usually matches methodInstance mIdHash unless there's a collision

public:
	BfDeferredMethodCallData()
	{		
		mAlign = 0;
		mSize = 0;
		mMethodId = 0;
	}

	static int64 GenerateMethodId(BfModule* module, int64 methodId);
};

class BfMethodCustomAttributes
{
public:
	BfCustomAttributes* mCustomAttributes;
	BfCustomAttributes* mReturnCustomAttributes;
	Array<BfCustomAttributes*> mParamCustomAttributes;

public:
	BfMethodCustomAttributes()
	{
		mCustomAttributes = NULL;
		mReturnCustomAttributes = NULL;
	}

	~BfMethodCustomAttributes();
};

class BfMethodParam
{
public:	
	BfType* mResolvedType;
	int16 mParamDefIdx;
	int16 mDelegateParamIdx;
	bool mDelegateParamNameCombine; // 'false' means to directly use param name (if we can), otherwise name as <baseParamName>__<delegateParamName>
	bool mWasGenericParam;
	bool mIsSplat;
	bool mReferencedInConstPass;

public:
	BfMethodParam()
	{		
		mResolvedType = NULL;
		mParamDefIdx = -1;
		mDelegateParamIdx = -1;
		mDelegateParamNameCombine = false;
		mWasGenericParam = false;
		mIsSplat = false;
		mReferencedInConstPass = false;
	}	

	BfMethodInstance* GetDelegateParamInvoke();
};

enum BfMethodOnDemandKind : int8
{
	BfMethodOnDemandKind_NotSet,
	BfMethodOnDemandKind_AlwaysInclude,
	BfMethodOnDemandKind_NoDecl_AwaitingReference, // Won't be declared until we reference
	BfMethodOnDemandKind_Decl_AwaitingReference, // Will be declared immediately but not processed until referenced
	BfMethodOnDemandKind_Decl_AwaitingDecl, // Temporary state before we switch to BfMethodOnDemandKind_Decl_AwaitingReference
	BfMethodOnDemandKind_InWorkList,
	BfMethodOnDemandKind_Referenced
};

enum BfMethodChainType : int8
{
	BfMethodChainType_None,
	BfMethodChainType_ChainHead,
	BfMethodChainType_ChainMember,
	BfMethodChainType_ChainSkip,
};

class BfMethodProcessRequest;
class BfLocalMethod;
class BfClosureState;

struct BfClosureCapturedEntry
{
	BfType* mType;
	String mName;
	BfIdentifierNode* mNameNode;
	bool mExplicitlyByReference;
	int mShadowIdx; // Only relative to matching nameNodes

	BfClosureCapturedEntry()
	{
		mNameNode = NULL;
		mType = NULL;
		mExplicitlyByReference = false;
		mShadowIdx = 0;
	}

	bool operator<(const BfClosureCapturedEntry& other) const
	{
		if (mName == "__this")
			return true;
		else if (other.mName == "__this")
			return false;
		return mName < other.mName;
	}
};

class BfClosureInstanceInfo
{
public:
	BfTypeInstance* mThisOverride;
	BfLocalMethod* mLocalMethod;
	Dictionary<int64, BfMethodDef*> mLocalMethodBindings; // We create binding during capture and use it during mDeferredLocalMethods processing
	BfClosureState* mCaptureClosureState;
	Array<BfClosureCapturedEntry> mCaptureEntries;

public:
	BfClosureInstanceInfo()
	{
		mThisOverride = NULL;
		mLocalMethod = NULL;
		mCaptureClosureState = NULL;		
	}
};

class BfMethodInfoEx
{
public:
	StringT<0> mMangledName; // Only populated during hot loading for virtual methods
	BfTypeInstance* mExplicitInterface;
	BfTypeInstance* mForeignType;
	BfClosureInstanceInfo* mClosureInstanceInfo;
	Array<BfGenericMethodParamInstance*> mGenericParams;
	BfTypeVector mMethodGenericArguments;
	Dictionary<int64, BfType*> mGenericTypeBindings;
	BfMethodCustomAttributes* mMethodCustomAttributes;
	int mMinDependDepth;	

	BfMethodInfoEx()
	{
		mExplicitInterface = NULL;
		mForeignType = NULL;
		mClosureInstanceInfo = NULL;
		mMethodCustomAttributes = NULL;
		mMinDependDepth = -1;		
	}
	
	~BfMethodInfoEx();	
};

enum BfImportCallKind
{
	BfImportCallKind_None,
	BfImportCallKind_GlobalVar,
	BfImportCallKind_GlobalVar_Hot // We need to check against NULL in this case
};

class BfMethodInstance
{
public:	
	int mVirtualTableIdx;
	BfIRFunction mIRFunction; // Only valid for owning module
	int16 mAppendAllocAlign;
	int16 mEndingAppendAllocAlign;
	bool mIsUnspecialized:1;
	bool mIsUnspecializedVariation:1;
	bool mIsReified:1;
	bool mHasBeenProcessed:1;	
	bool mHasFailed:1;
	bool mFailedConstraints:1;
	bool mMangleWithIdx:1;
	bool mHadGenericDelegateParams:1;
	bool mIgnoreBody:1;
	bool mIsAutocompleteMethod:1;
	bool mRequestedByAutocomplete : 1;
	bool mIsClosure:1;
	bool mMayBeConst:1; // Only used for calcAppend currently	
	bool mIsForeignMethodDef:1;
	bool mAlwaysInline:1;
	bool mIsIntrinsic:1;
	bool mHasMethodRefType:1;
	bool mDisallowCalling:1;		
	bool mIsInnerOverride:1;
	bool mInCEMachine:1;
	bool mIsDisposed:1;
	BfMethodChainType mChainType;
	BfComptimeFlags mComptimeFlags;
	BfCallingConvention mCallingConvention;
	BfMethodInstanceGroup* mMethodInstanceGroup;
	BfMethodDef* mMethodDef;
	BfType* mReturnType;
	Array<BfMethodParam> mParams;	
	Array<BfTypedValue> mDefaultValues;
	BfModule* mDeclModule;
	BfMethodProcessRequest* mMethodProcessRequest;	
	BfMethodInfoEx* mMethodInfoEx;	
	int64 mIdHash; // Collisions are (essentially) benign	
	BfHotMethod* mHotMethod;

public:
	BfMethodInstance()
	{		
		mVirtualTableIdx = -1;				
		mIsUnspecialized = false;		
		mIsUnspecializedVariation = false;
		mIsReified = true;
		mHasBeenProcessed = false;
		mHasFailed = false;
		mFailedConstraints = false;
		mMangleWithIdx = false;
		mHadGenericDelegateParams = false;
		mIgnoreBody = false;
		mIsAutocompleteMethod = false;
		mRequestedByAutocomplete = false;
		mIsClosure = false;
		mMayBeConst = true;		
		mIsForeignMethodDef = false;
		mAlwaysInline = false;
		mIsIntrinsic = false;
		mHasMethodRefType = false;
		mDisallowCalling = false;
		mIsInnerOverride = false;
		mInCEMachine = false;
		mIsDisposed = false;
		mChainType = BfMethodChainType_None;
		mComptimeFlags = BfComptimeFlag_None;
		mCallingConvention = BfCallingConvention_Unspecified;
		mMethodInstanceGroup = NULL;
		mMethodDef = NULL;									
		mReturnType = NULL;		
		mIdHash = 0;
		mAppendAllocAlign = -1;		
		mEndingAppendAllocAlign = -1;
		mDeclModule = NULL;		
		mMethodProcessRequest = NULL;
		mMethodInfoEx = NULL;		
		mHotMethod = NULL;
	}

	~BfMethodInstance();
	void Dispose(bool isDeleting = false);

	void CopyFrom(BfMethodInstance* methodInstance);

	bool IsMixin()
	{
		return mMethodDef->mMethodType == BfMethodType_Mixin;
	}

	BfImportKind GetImportKind();
	BfMethodFlags GetMethodFlags();
	void UndoDeclaration(bool keepIRFunction = false);
	BfTypeInstance* GetOwner();	
	BfModule* GetModule();
	bool IsSpecializedGenericMethod();
	bool IsSpecializedGenericMethodOrType();
	bool IsSpecializedByAutoCompleteMethod();
	bool IsOrInUnspecializedVariation();
	bool HasExternConstraints();
	bool HasThis();	
	bool IsVirtual();
	BfType* GetThisType();
	int GetThisIdx();
	bool HasExplicitThis();
	bool HasParamsArray();
	int GetStructRetIdx(bool forceStatic = false);
	bool HasSelf();
	bool GetLoweredReturnType(BfTypeCode* loweredTypeCode = NULL, BfTypeCode* loweredTypeCode2 = NULL);	
	bool WantsStructsAttribByVal();
	bool IsAutocompleteMethod() { /*return mIdHash == -1;*/ return mIsAutocompleteMethod; }
	bool IsSkipCall(bool bypassVirtual = false);	
	bool IsVarArgs();
	bool AlwaysInline();
	BfImportCallKind GetImportCallKind();	
	bool IsTestMethod();
	bool AllowsSplatting(int paramIdx);
	int GetParamCount();
	int GetImplicitParamCount();	
	void GetParamName(int paramIdx, StringImpl& name);
	String GetParamName(int paramIdx);	
	BfType* GetParamType(int paramIdx, bool returnUnderlyingParamsType = false);
	bool GetParamIsSplat(int paramIdx);
	BfParamKind GetParamKind(int paramIdx);
	bool WasGenericParam(int paramIdx);
	bool IsParamSkipped(int paramIdx); // void/zero-sized
	bool IsImplicitCapture(int paramIdx);
	BfExpression* GetParamInitializer(int paramIdx);
	BfTypeReference* GetParamTypeRef(int paramIdx);
	BfIdentifierNode* GetParamNameNode(int paramIdx);
	int DbgGetVirtualMethodNum();

	void GetIRFunctionInfo(BfModule* module, BfIRType& returnType, SizedArrayImpl<BfIRType>& paramTypes, bool forceStatic = false);
	int GetIRFunctionParamCount(BfModule* module);

	bool IsExactMatch(BfMethodInstance* other, bool ignoreImplicitParams = false, bool checkThis = false);	
	bool IsReifiedAndImplemented();	

	BfMethodInfoEx* GetMethodInfoEx();
	BfCustomAttributes* GetCustomAttributes()
	{
		if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mMethodCustomAttributes != NULL))
			return mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes;
		return NULL;
	}
	int GetNumGenericParams()
	{
		if (mMethodInfoEx != NULL)
			return (int)mMethodInfoEx->mGenericParams.size();
		return 0;
	}
	int GetNumGenericArguments()
	{
		if (mMethodInfoEx != NULL)
			return (int)mMethodInfoEx->mMethodGenericArguments.size();
		return 0;
	}
	BfTypeInstance* GetExplicitInterface()
	{
		if (mMethodInfoEx != NULL)
			return mMethodInfoEx->mExplicitInterface;
		return NULL;
	}
	BfTypeInstance* GetForeignType()
	{
		if (mMethodInfoEx != NULL)
			return mMethodInfoEx->mForeignType;
		return NULL;
	}
	void ReportMemory(MemReporter* memReporter);
};

class BfOperatorInfo
{
public:
	BfMethodDef* mMethodDef;
	BfType* mReturnType;
	BfType* mLHSType;
	BfType* mRHSType;

	BfOperatorInfo()
	{
		mMethodDef = NULL;
		mReturnType = NULL;
		mLHSType = NULL;
		mRHSType = NULL;
	}
};

class BfDllImportEntry
{
public:
	BfMethodInstance* mMethodInstance;
	BfIRValue mFuncVar;
	//BfIRFunctionType mFuncType;
};

class BfModuleMethodInstance
{
public:
	BfMethodInstance* mMethodInstance;
	BfIRValue mFunc;

public:
	BfModuleMethodInstance()
	{
		mMethodInstance = NULL;		
	}

	BfModuleMethodInstance(BfMethodInstance* methodInstance);	

	BfModuleMethodInstance(BfMethodInstance* methodInstance, BfIRValue func) : mFunc(func)
	{
		mMethodInstance = methodInstance;		
	}

	operator bool() const
	{
		return mMethodInstance != NULL;
	}
};

// The way these work is a bit nuanced
//  When we use these as keys, we allow collisions between genericParams from different type and
//  method instances.  That's okay, as the only constraint there is that they generate valid
//  generic-pass methods, which will all be the same.  Within the SCOPE of a given type and
//  method instance, however, we cannot have collision between types (such as a List<T> and List<T2>)
//  because that can cause false equalties in the type checker
class BfGenericParamType : public BfType
{
public:
	BfGenericParamKind mGenericParamKind;
	int mGenericParamIdx;
	
public:
	bool IsGenericParam() override { return true; }
	virtual bool IsUnspecializedType() override { return true; }
	virtual bool IsReified() override { return false; }
};

// This just captures rettype(T)/nullable(T) since it can't be resolved directly
class BfModifiedTypeType : public BfType
{
public:
	BfToken mModifiedKind;
	BfType* mElementType;

	virtual bool IsModifiedTypeType() override { return true; }
	virtual bool CanBeValuelessType() override { return true; }
	virtual bool IsValuelessType() override { return true; }

	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsReified() override { return mElementType->IsReified(); }
	virtual bool IsDependentOnUnderlyingType() override { return true; }	
	virtual bool IsAllocType() override { return mModifiedKind == BfToken_AllocType; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
};

class BfGenericOperatorConstraintInstance
{
public:
	BfType* mLeftType;
	BfBinaryOp mBinaryOp;
	BfUnaryOp mUnaryOp;
	BfToken mCastToken;
	BfType* mRightType;

public:
	BfGenericOperatorConstraintInstance()
	{
		mLeftType = NULL;
		mBinaryOp = BfBinaryOp_None;
		mUnaryOp = BfUnaryOp_None;
		mCastToken = BfToken_None;
		mRightType = NULL;
	}

	bool operator==(const BfGenericOperatorConstraintInstance& other) const
	{
		return
			(mLeftType == other.mLeftType) &&
			(mBinaryOp == other.mBinaryOp) &&
			(mUnaryOp == other.mUnaryOp) &&
			(mCastToken == other.mCastToken) &&
			(mRightType == other.mRightType);
	}
};

class BfGenericParamInstance 
{
public:	
	int mGenericParamFlags;
	BfType* mExternType;
	Array<BfTypeInstance*> mInterfaceConstraints;
	Array<BfGenericOperatorConstraintInstance> mOperatorConstraints;
	Array<BfTypeReference*> mComptypeConstraint;
	BfType* mTypeConstraint;
	int mRefCount;

	BfGenericParamInstance()
	{
		mExternType = NULL;
		mGenericParamFlags = 0;
		mTypeConstraint = NULL;
		mRefCount = 1;
	}

	void Release()
	{
		if (--mRefCount == 0)
			delete this;
	}	

	virtual ~BfGenericParamInstance()
	{
	}
	virtual BfConstraintDef* GetConstraintDef() = 0;
	virtual BfGenericParamDef* GetGenericParamDef() = 0;
	virtual BfExternalConstraintDef* GetExternConstraintDef() = 0;
	virtual String GetName() = 0;	
};

class BfGenericTypeParamInstance : public BfGenericParamInstance
{
public:
	BfTypeDef* mTypeDef;
	int mGenericIdx;

public:
	BfGenericTypeParamInstance(BfTypeDef* typeDef, int genericIdx)
	{
		mTypeDef = typeDef;
		mGenericIdx = genericIdx;
		mGenericParamFlags = GetConstraintDef()->mGenericParamFlags;
		mTypeConstraint = NULL;
	}

	BfGenericTypeParamInstance* AddRef()
	{
		mRefCount++;
		return this;
	}

	virtual BfConstraintDef* GetConstraintDef() override
	{
		if (mGenericIdx < (int)mTypeDef->mGenericParamDefs.size())
			return mTypeDef->mGenericParamDefs[mGenericIdx];
		return &mTypeDef->mExternalConstraints[mGenericIdx - (int)mTypeDef->mGenericParamDefs.size()];
	}

	virtual BfGenericParamDef* GetGenericParamDef() override
	{
		if (mGenericIdx < (int)mTypeDef->mGenericParamDefs.size())
			return mTypeDef->mGenericParamDefs[mGenericIdx];
		return NULL;
	}

	virtual BfExternalConstraintDef* GetExternConstraintDef() override
	{
		if (mGenericIdx < (int)mTypeDef->mGenericParamDefs.size())
			return NULL;
		return &mTypeDef->mExternalConstraints[mGenericIdx - (int)mTypeDef->mGenericParamDefs.size()];
	}

	virtual String GetName() override
	{
		if (mGenericIdx < (int)mTypeDef->mGenericParamDefs.size())
			return mTypeDef->mGenericParamDefs[mGenericIdx]->mName;
		return mTypeDef->mExternalConstraints[mGenericIdx - (int)mTypeDef->mGenericParamDefs.size()].mTypeRef->ToString();
	}
};

class BfGenericMethodParamInstance : public BfGenericParamInstance
{
public:
	BfMethodDef* mMethodDef;
	int mGenericIdx;

public:
	BfGenericMethodParamInstance(BfMethodDef* methodDef, int genericIdx)
	{
		mMethodDef = methodDef;
		mGenericIdx = genericIdx;
		mGenericParamFlags = GetConstraintDef()->mGenericParamFlags;
		mTypeConstraint = NULL;
	}

	BfGenericMethodParamInstance* AddRef()
	{
		mRefCount++;
		return this;
	}

	virtual BfConstraintDef* GetConstraintDef() override
	{
		if (mGenericIdx < (int)mMethodDef->mGenericParams.size())
			return mMethodDef->mGenericParams[mGenericIdx];
		return &mMethodDef->mExternalConstraints[mGenericIdx - (int)mMethodDef->mGenericParams.size()];
	}

	virtual BfGenericParamDef* GetGenericParamDef() override
	{
		if (mGenericIdx < (int)mMethodDef->mGenericParams.size())
			return mMethodDef->mGenericParams[mGenericIdx];
		return NULL;
	}

	virtual BfExternalConstraintDef* GetExternConstraintDef() override
	{
		if (mGenericIdx < (int)mMethodDef->mGenericParams.size())
			return NULL;
		return &mMethodDef->mExternalConstraints[mGenericIdx - (int)mMethodDef->mGenericParams.size()];
	}

	virtual String GetName() override
	{
		if (mGenericIdx < (int)mMethodDef->mGenericParams.size())
			return mMethodDef->mGenericParams[mGenericIdx]->mName;
		return mMethodDef->mExternalConstraints[mGenericIdx - (int)mMethodDef->mGenericParams.size()].mTypeRef->ToString();
	}
};

#define BF_VALCOMP(val) if (val != 0) return val

struct BfTypeVectorHash
{	
	size_t operator()(const BfTypeVector& val) const;
};

struct BfTypeVectorEquals
{
	bool operator()(const BfTypeVector& lhs, const BfTypeVector& rhs) const;
};

// Specialized for Class<T> but not Method<T>
class BfMethodInstanceGroup
{
public:
	BfTypeInstance* mOwner;	
	BfMethodInstance* mDefault;	
	typedef Dictionary<BfTypeVector, BfMethodInstance*> MapType;
	MapType* mMethodSpecializationMap;	
	int mMethodIdx;
	int mRefCount; // External references from BfMethodRefType
	BfMethodOnDemandKind mOnDemandKind;
	bool mExplicitlyReflected;

public:
	BfMethodInstanceGroup()
	{
		mOwner = NULL;
		mDefault = NULL;
		mMethodSpecializationMap = NULL;
		mMethodIdx = -1;
		mOnDemandKind = BfMethodOnDemandKind_NotSet;
		mRefCount = 0;
		mExplicitlyReflected = false;
	}

	~BfMethodInstanceGroup();	

	BfMethodInstanceGroup(BfMethodInstanceGroup&& prev) noexcept
	{
		mOwner = prev.mOwner;
		mDefault = prev.mDefault;
		mMethodSpecializationMap = prev.mMethodSpecializationMap;
		mMethodIdx = prev.mMethodIdx;
		mRefCount = prev.mRefCount;
		mOnDemandKind = prev.mOnDemandKind;
		if (mDefault != NULL)
			mDefault->mMethodInstanceGroup = this;
		if (mMethodSpecializationMap != NULL)
		{
			for (auto& pair : *mMethodSpecializationMap)
				pair.mValue->mMethodInstanceGroup = this;
		}

		prev.mRefCount = 0;
		prev.mDefault = NULL;
		prev.mMethodSpecializationMap = NULL;
	}

	bool IsImplemented()
	{
		return (mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude) || 
			(mOnDemandKind == BfMethodOnDemandKind_Referenced) ||
			(mOnDemandKind == BfMethodOnDemandKind_InWorkList);
	}
};

class BfFieldInstance
{
public:
	BfTypeInstance* mOwner;	
	BfType* mResolvedType;	
	BfCustomAttributes* mCustomAttributes;
	int mConstIdx;
	int mFieldIdx;
	int mDataIdx; // mFieldIdx includes statics & consts, mDataIdx does not
	int mMergedDataIdx; // Like mDataIdx but contains base class's data
	int mDataOffset;
	int mDataSize;
	bool mFieldIncluded;
	bool mIsEnumPayloadCase;
	bool mIsThreadLocal;
	bool mIsInferredType;
	bool mHadConstEval;
	int mLastRevisionReferenced;

public:
	BfFieldDef* GetFieldDef();	

	BfFieldInstance(const BfFieldInstance& copyFrom)
	{
		BF_ASSERT(copyFrom.mCustomAttributes == NULL);		
		mOwner = copyFrom.mOwner;		
		mResolvedType = copyFrom.mResolvedType;
		
		mCustomAttributes = copyFrom.mCustomAttributes;
		mConstIdx = copyFrom.mConstIdx;
		mFieldIdx = copyFrom.mFieldIdx;
		mDataIdx = copyFrom.mDataIdx;
		mMergedDataIdx = copyFrom.mMergedDataIdx;
		mDataOffset = copyFrom.mDataOffset;
		mDataSize = copyFrom.mDataSize;
		mFieldIncluded = copyFrom.mFieldIncluded;		
		mIsEnumPayloadCase = copyFrom.mIsEnumPayloadCase;
		mIsThreadLocal = copyFrom.mIsThreadLocal;
		mIsInferredType = copyFrom.mIsInferredType;		
		mHadConstEval = copyFrom.mHadConstEval;
		mLastRevisionReferenced = copyFrom.mLastRevisionReferenced;		
	}

	BfFieldInstance()
	{
		mFieldIdx = -1;
		mOwner = NULL;		
		mResolvedType = NULL;
		mIsEnumPayloadCase = false;
		mCustomAttributes = NULL;		
		mConstIdx = -1;
		mDataIdx = -1;
		mMergedDataIdx = -1;
		mDataOffset = -1;
		mDataSize = 0;
		mFieldIncluded = true;
		mIsThreadLocal = false;
		mIsInferredType = false;
		mHadConstEval = false;
		mLastRevisionReferenced = -1;
	}		

	~BfFieldInstance();

	BfType* GetResolvedType();
	void SetResolvedType(BfType* type);
	void GetDataRange(int& dataIdx, int& dataCount);
};

enum BfMethodRefKind
{
	BfMethodRefKind_VExtMarker = -1,
	BfMethodRefKind_AmbiguousRef = -2
};

class BfNonGenericMethodRef
{
public:
	BfTypeInstance* mTypeInstance;
	union
	{
		int mMethodNum;
		BfMethodRefKind mKind;
	};	
	int mSignatureHash;

	BfNonGenericMethodRef()
	{
		mTypeInstance = NULL;
		mMethodNum = 0;
		mSignatureHash = 0;
	}

	BfNonGenericMethodRef(BfMethodInstance* methodInstance);

	operator BfMethodInstance*() const;
	bool IsNull() { return mTypeInstance == NULL; };
	BfMethodInstance* operator->() const;
	BfNonGenericMethodRef& operator=(BfMethodInstance* methodInstance);
	bool operator==(const BfNonGenericMethodRef& methodRef) const;
	bool operator==(BfMethodInstance* methodInstance) const;
	
	struct Hash
	{
		size_t operator()(const BfNonGenericMethodRef& val) const;
	};

	struct Equals
	{
		bool operator()(const BfNonGenericMethodRef& lhs, const BfNonGenericMethodRef& rhs) const
		{
			return lhs == rhs;
		}
	};
};

enum BfMethodRefFlags : uint8
{
	BfMethodRefFlag_None = 0,
	BfMethodRefFlag_AlwaysInclude = 1
};

class BfMethodRef
{
public:	
	BfTypeInstance* mTypeInstance;
	union
	{
		int mMethodNum;
		BfMethodRefKind mKind;
	};	
	Array<BfType*> mMethodGenericArguments;
	int mSignatureHash;
	BfMethodRefFlags mMethodRefFlags;

	BfMethodRef()
	{
		mTypeInstance = NULL;
		mMethodNum = 0;
		mSignatureHash = 0;
		mMethodRefFlags = BfMethodRefFlag_None;
	}

	BfMethodRef(BfMethodInstance* methodInstance);	

	operator BfMethodInstance*() const;
	bool IsNull() { return mTypeInstance == NULL; };
	BfMethodInstance* operator->() const;
	BfMethodRef& operator=(BfMethodInstance* methodInstance);
	bool operator==(const BfMethodRef& methodRef) const;
	bool operator==(BfMethodInstance* methodInstance) const;
	
	struct Hash
	{	
		size_t operator()(const BfMethodRef& val) const;
	};

	struct Equals
	{
		bool operator()(const BfMethodRef& lhs, const BfMethodRef& rhs) const
		{
			return lhs == rhs;
		}
	};
};

class BfFieldRef
{
public:
	BfTypeInstance* mTypeInstance;
	int mFieldIdx;

public:
	BfFieldRef()
	{
		mTypeInstance = NULL;
		mFieldIdx = 0;
	}

	BfFieldRef(BfTypeInstance* typeInst, BfFieldDef* fieldDef);
	BfFieldRef(BfFieldInstance* fieldInstance);

	bool operator==(const BfFieldRef& other) const
	{
		return (mTypeInstance == other.mTypeInstance) && (mFieldIdx == other.mFieldIdx);
	}

	operator BfFieldInstance*() const;
	operator BfFieldDef*() const;
	operator BfPropertyDef*() const;
};

class BfPropertyRef
{
public:
	BfTypeInstance* mTypeInstance;
	int mPropIdx;

public:
	BfPropertyRef()
	{
		mTypeInstance = NULL;
		mPropIdx = 0;
	}

	BfPropertyRef(BfTypeInstance* typeInst, BfPropertyDef* propDef);
	
	operator BfPropertyDef*() const;
};

class BfTypeInterfaceEntry
{
public:	
	BfTypeDef* mDeclaringType;
	BfTypeInstance* mInterfaceType;
	int mStartInterfaceTableIdx;
	int mStartVirtualIdx; // Relative to start of virtual interface methods
	bool mIsRedeclared;		
};

class BfTypeInterfaceMethodEntry
{
public:
	BfNonGenericMethodRef mMethodRef;
	//int mVirtualIdx;

public:
	BfTypeInterfaceMethodEntry()
	{
		//mVirtualIdx = -1;
	}
};

enum BfAttributeTargets : int32
{
	BfAttributeTargets_SkipValidate = -1,

	BfAttributeTargets_None         = 0,
	BfAttributeTargets_Assembly     = 0x0001,
	BfAttributeTargets_Module       = 0x0002,
	BfAttributeTargets_Class        = 0x0004,
	BfAttributeTargets_Struct       = 0x0008,
	BfAttributeTargets_Enum         = 0x0010,
	BfAttributeTargets_Constructor  = 0x0020,
	BfAttributeTargets_Method       = 0x0040,
	BfAttributeTargets_Property     = 0x0080,
	BfAttributeTargets_Field        = 0x0100,
	BfAttributeTargets_StaticField  = 0x0200,
	BfAttributeTargets_Interface    = 0x0400,
	BfAttributeTargets_Parameter    = 0x0800,
	BfAttributeTargets_Delegate     = 0x1000,
	BfAttributeTargets_Function     = 0x2000,
	BfAttributeTargets_ReturnValue  = 0x4000,	
	BfAttributeTargets_GenericParameter = 0x8000,
	BfAttributeTargets_Invocation   = 0x10000,
	BfAttributeTargets_MemberAccess = 0x20000,
	BfAttributeTargets_Alloc        = 0x40000,
	BfAttributeTargets_Delete       = 0x80000,
	BfAttributeTargets_Alias        = 0x100000,
	BfAttributeTargets_Block		= 0x200000,
	BfAttributeTargets_All          = 0x3FFFFF
};

class BfAttributeData
{
public:
	BfAttributeTargets mAttributeTargets;
	BfAlwaysIncludeFlags mAlwaysIncludeUser;
	bool mInherited;
	bool mAllowMultiple;

public:
	BfAttributeData()
	{
		mAttributeTargets = BfAttributeTargets_All;
		mAlwaysIncludeUser = BfAlwaysIncludeFlag_None;
		mInherited = true;
		mAllowMultiple = false;
	}
};

class BfHotTypeData
{
public:	
	Array<BfHotTypeVersion*> mTypeVersions;
	Array<BfHotTypeDataEntry> mVTableEntries; // Doesn't fill in until we rebuild or delete type	
	int mVTableOrigLength;
	int mOrigInterfaceMethodsLength;
	bool mPendingDataChange;
	bool mHadDataChange; // True if we have EVER had a hot data change
	
public:
	BfHotTypeData()
	{
		mVTableOrigLength = -1;
		mOrigInterfaceMethodsLength = -1;				
		mPendingDataChange = false;
		mHadDataChange = false;
	}

	~BfHotTypeData();
	
	BfHotTypeVersion* GetTypeVersion(int hotCommitedIdx);
	BfHotTypeVersion* GetLatestVersion();
	BfHotTypeVersion* GetLatestVersionHead(); // The oldest version that has the same data
	void ClearVersionsAfter(int hotIdx);
};

struct BfTypeLookupEntry
{
	BfAtomComposite mName;
	int mNumGenericParams;
	uint32 mAtomUpdateIdx;	
	BfTypeDef* mUseTypeDef;

	bool operator==(const BfTypeLookupEntry& rhs) const
	{
		return (mName == rhs.mName) &&
			(mNumGenericParams == rhs.mNumGenericParams) &&
			(mUseTypeDef == rhs.mUseTypeDef);
	}
};

NS_BF_END;

namespace std
{
	template<>
	struct hash<Beefy::BfTypeLookupEntry>
	{
		size_t operator()(const Beefy::BfTypeLookupEntry& entry) const
		{
			int curHash = 0;
			for (int i = 0; i < entry.mName.mSize; i++)
				curHash = ((curHash ^ (int)(intptr)entry.mName.mParts[i]) << 5) - curHash;
			curHash = ((curHash ^ entry.mNumGenericParams) << 5) - curHash;
			curHash ^= (intptr)entry.mUseTypeDef;
			return curHash;
		}
	};
}

NS_BF_BEGIN;

struct BfTypeLookupResult
{
	BfTypeDef* mTypeDef;
	bool mForceLookup;

	BfTypeLookupResult()
	{
		mTypeDef = NULL;
		mForceLookup = false;
	}
};

class BfDependedType : public BfType
{
public:
	BfDependencyMap mDependencyMap;	// This is a list of types that depend on THIS type

public:
	virtual bool IsDependendType() override { return true; }
	virtual BfDependedType* ToDependedType() override { return this; }
};

struct BfVirtualMethodEntry
{
	BfNonGenericMethodRef mDeclaringMethod;
	BfNonGenericMethodRef mImplementingMethod;
};

struct BfSpecializedMethodRefInfo
{
	bool mHasReifiedRef;
	
	BfSpecializedMethodRefInfo()
	{
		mHasReifiedRef = false;		
	}
};

class BfStaticSearch
{
public:
	Array<BfTypeInstance*> mStaticTypes;
};

class BfInternalAccessSet
{
public:
	Array<BfTypeInstance*> mTypes;
	Array<BfAtomComposite> mNamespaces;
};

class BfTypeInfoEx
{
public:
	BfType* mUnderlyingType;
	int64 mMinValue;
	int64 mMaxValue;

	BfTypeInfoEx()
	{
		mUnderlyingType = NULL;
		mMinValue = 0;
		mMaxValue = 0;
	}
};


class BfGenericExtensionEntry
{
public:
	Array<BfGenericTypeParamInstance*> mGenericParams;
	bool mConstraintsPassed;

public:
	BfGenericExtensionEntry(BfGenericExtensionEntry&& prev) :
		mGenericParams(std::move(prev.mGenericParams)),
		mConstraintsPassed(prev.mConstraintsPassed)
	{
	}

	BfGenericExtensionEntry()
	{
		mConstraintsPassed = true;
	}
	~BfGenericExtensionEntry();
};

class BfGenericExtensionInfo
{
public:
	Dictionary<BfTypeDef*, BfGenericExtensionEntry> mExtensionMap;
};

// Note on nested generic types- mGenericParams is the accumulation of all generic params from outer to inner, so
// class ClassA<T> { class ClassB<T2> {} } will create a ClassA.ClassB<T, T2>
class BfGenericTypeInfo
{
public:
	typedef Array<BfGenericTypeParamInstance*> GenericParamsVector;

	Array<BfTypeReference*> mTypeGenericArgumentRefs;
	BfTypeVector mTypeGenericArguments;
	GenericParamsVector mGenericParams;
	BfGenericExtensionInfo* mGenericExtensionInfo;
	bool mIsUnspecialized;
	bool mIsUnspecializedVariation;
	bool mValidatedGenericConstraints;
	bool mHadValidateErrors;
	bool mInitializedGenericParams;
	bool mFinishedGenericParams;	
	Array<BfProject*> mProjectsReferenced; // Generic methods that only refer to these projects don't need a specialized extension

public:
	BfGenericTypeInfo()
	{
		mGenericExtensionInfo = NULL;
		mHadValidateErrors = false;
		mIsUnspecialized = false;
		mIsUnspecializedVariation = false;
		mValidatedGenericConstraints = false;
		mInitializedGenericParams = false;
		mFinishedGenericParams = false;
	}

	~BfGenericTypeInfo();
	
	void ReportMemory(MemReporter* memReporter);
};


// Instance of struct or class
class BfTypeInstance : public BfDependedType
{
public:		
	int mSignatureRevision;
	int mLastNonGenericUsedRevision;
	int mInheritanceId;
	int mInheritanceCount;
	BfModule* mModule;

	BfTypeDef* mTypeDef;
	BfTypeInstance* mBaseType;
	BfCustomAttributes* mCustomAttributes;
	BfAttributeData* mAttributeData;
	BfTypeInfoEx* mTypeInfoEx;
	BfGenericTypeInfo* mGenericTypeInfo;

	Array<BfTypeInterfaceEntry> mInterfaces;	
	Array<BfTypeInterfaceMethodEntry> mInterfaceMethodTable;
	Array<BfMethodInstanceGroup> mMethodInstanceGroups;
	Array<BfOperatorInfo*> mOperatorInfo;
	Array<BfVirtualMethodEntry> mVirtualMethodTable;	
	BfHotTypeData* mHotTypeData;	
	int mVirtualMethodTableSize; // With hot reloading, mVirtualMethodTableSize can be larger than mInterfaceMethodTable (live vtable versioning)	
	Array<BfFieldInstance> mFieldInstances;
	Array<BfMethodInstance*> mInternalMethods;			
	Dictionary<BfTypeDef*, BfStaticSearch> mStaticSearchMap;
	Dictionary<BfTypeDef*, BfInternalAccessSet> mInternalAccessMap;
	Array<BfLocalMethod*> mOwnedLocalMethods; // Local methods in CEMachine
	bool mHasStaticInitMethod;
	bool mHasStaticDtorMethod;
	bool mHasStaticMarkMethod;	
	bool mHasTLSFindMethod;
	BfIRConstHolder* mConstHolder;	
	Dictionary<BfMethodRef, BfSpecializedMethodRefInfo> mSpecializedMethodReferences; // Includes both specialized methods and OnDemand methods

	// We store lookup results so we can rebuild this type if a name resolves differently due to a new type being created (ie: a type name found first in	
	Dictionary<BfTypeLookupEntry, BfTypeLookupResult> mLookupResults;
	
	int mTypeOptionsIdx;
	int mMergedFieldDataCount;	
	int mInstAlign;
	int mInstSize;	
	int16 mInheritDepth;
	int16 mSlotNum;
	BfAlwaysIncludeFlags mAlwaysIncludeFlags;
	bool mHasBeenInstantiated;
	bool mIsReified;
	bool mIsTypedPrimitive;
	bool mIsCRepr;	
	bool mIsUnion;	
	bool mIsPacked;
	bool mIsSplattable;
	bool mHasUnderlyingArray;
	bool mTypeIncomplete;
	bool mTypeFailed;
	bool mTypeWarned;
	bool mResolvingVarField;
	bool mResolvingConstField;
	bool mSkipTypeProtectionChecks;
	bool mNeedsMethodProcessing;
	bool mBaseTypeMayBeIncomplete;		
	bool mHasParameterizedBase; // Generic, array, etc
	bool mIsFinishingType;	
	bool mHasPackingHoles;		
	bool mWantsGCMarking;
	bool mHasDeclError;	

public:
	BfTypeInstance()
	{	
		mModule = NULL;
		mSignatureRevision = -1;
		mLastNonGenericUsedRevision = -1;
		mInheritanceId = 0;
		mInheritanceCount = 0;

		mTypeDef = NULL;
		mRevision = -1;
		mIsReified = true;
		mIsSplattable = false;
		mHasUnderlyingArray = false;
		mIsPacked = false;
		mBaseType = NULL;
		mCustomAttributes = NULL;
		mAttributeData = NULL;
		mTypeInfoEx = NULL;
		mGenericTypeInfo = NULL;
		//mClassVData = NULL;
		mVirtualMethodTableSize = 0;
		mHotTypeData = NULL;		
		mHasStaticInitMethod = false;
		mHasStaticMarkMethod = false;
		mHasStaticDtorMethod = false;		
		mHasTLSFindMethod = false;
		mSlotNum = -1;
		mTypeOptionsIdx = -2; // -2 = not checked, -1 = none
		mInstSize = -1;
		mInstAlign = -1;
		mInheritDepth = 0;		
		mIsTypedPrimitive = false;
		mTypeIncomplete = true;
		mIsCRepr = false;		
		mIsUnion = false;
		mTypeFailed = false;
		mTypeWarned = false;
		mResolvingVarField = false;
		mSkipTypeProtectionChecks = false;
		mNeedsMethodProcessing = false;
		mBaseTypeMayBeIncomplete = false;
		mIsFinishingType = false;
		mResolvingConstField = false;
		mHasPackingHoles = false;	
		mAlwaysIncludeFlags = BfAlwaysIncludeFlag_None;
		mHasBeenInstantiated = false;		
		mWantsGCMarking = false;
		mHasParameterizedBase = false;
		mHasDeclError = false;
		mMergedFieldDataCount = 0;
		mConstHolder = NULL;
	}
	
	~BfTypeInstance();	
	
	void ReleaseData();

	virtual bool IsInstanceOf(BfTypeDef* typeDef) override { return typeDef == mTypeDef; }
	virtual BfModule* GetModule() override { return mModule; }
	virtual BfTypeInstance* ToTypeInstance() override { return this; }
	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfPrimitiveType* ToPrimitiveType() override { return GetUnderlyingType()->ToPrimitiveType();  }
	virtual bool HasWrappedRepresentation() override { return IsTypedPrimitive(); }
	
	int GetEndingInstanceAlignment() { if (mInstSize % mInstAlign == 0) return mInstAlign; return mInstSize % mInstAlign; }
	virtual bool HasTypeFailed() override { return mTypeFailed; } 
	virtual bool IsReified() override { return mIsReified; }
	virtual bool NeedsExplicitAlignment() override { return !IsSizeAligned() || mIsPacked; }	
	virtual bool IsDataIncomplete() override { return ((mTypeIncomplete) || (mBaseTypeMayBeIncomplete)) && (!mNeedsMethodProcessing); }	
	virtual bool IsIncomplete() override { return (mTypeIncomplete) || (mBaseTypeMayBeIncomplete); }
	virtual bool IsSplattable() override { BF_ASSERT((mInstSize >= 0) || (!IsComposite())); return mIsSplattable; }
	virtual int GetSplatCount() override;
	virtual bool IsTypeInstance() override { return true; }
	virtual bool IsInterface() override { return mTypeDef->mTypeCode == BfTypeCode_Interface; }
	virtual bool IsValueType() override { return (mTypeDef->mTypeCode == BfTypeCode_Struct) || (mTypeDef->mTypeCode == BfTypeCode_Enum); }
	virtual bool IsStruct() override { return ((mTypeDef->mTypeCode == BfTypeCode_Struct) || (mTypeDef->mTypeCode == BfTypeCode_Enum)) && (!mIsTypedPrimitive); }	
	virtual bool IsUnion() override { return mIsUnion; }
	virtual bool IsDelegate() override { return mTypeDef->mIsDelegate; }
	virtual bool IsFunction() override { return mTypeDef->mIsFunction; }
	virtual bool IsString() override;
	virtual bool IsIntPtrable() override { return (mTypeDef->mTypeCode == BfTypeCode_Object) || (mTypeDef->mTypeCode == BfTypeCode_Interface); };
	virtual bool IsEnum() override { return mTypeDef->mTypeCode == BfTypeCode_Enum; }
	virtual bool IsPayloadEnum() override { return (mTypeDef->mTypeCode == BfTypeCode_Enum) && (!mIsTypedPrimitive); }
	virtual bool IsTypedPrimitive() override { return mIsTypedPrimitive; }
	virtual bool IsStructOrStructPtr() override { return mTypeDef->mTypeCode == BfTypeCode_Struct; }
	virtual bool IsValueTypeOrValueTypePtr() override { return (mTypeDef->mTypeCode == BfTypeCode_Struct) || (mTypeDef->mTypeCode == BfTypeCode_Enum); }
	virtual bool IsObject() override { return mTypeDef->mTypeCode == BfTypeCode_Object; }	
	virtual bool IsObjectOrStruct() override { return (mTypeDef->mTypeCode == BfTypeCode_Object) || (mTypeDef->mTypeCode == BfTypeCode_Struct); }
	virtual bool IsObjectOrInterface() override { return (mTypeDef->mTypeCode == BfTypeCode_Object) || (mTypeDef->mTypeCode == BfTypeCode_Interface); }
	virtual BfType* GetUnderlyingType() override;
	//virtual bool IsValuelessType() override { return (mIsTypedPrimitive) && (mInstSize == 0); }	
	virtual bool CanBeValuelessType() override { return (mTypeDef->mTypeCode == BfTypeCode_Struct) || (mTypeDef->mTypeCode == BfTypeCode_Enum); }
	virtual bool IsValuelessType() override;
	virtual bool HasPackingHoles() override { return mHasPackingHoles; }
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfTypeDef* activeTypeDef) override;
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProject* curProject) override;	
	virtual bool IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProjectSet* visibleProjectSet) override;
	virtual bool WantsGCMarking() override;
	virtual bool GetLoweredType(BfTypeUsage typeUsage, BfTypeCode* outTypeCode = NULL, BfTypeCode* outTypeCode2 = NULL) override;

	BfGenericTypeInfo* GetGenericTypeInfo() { return mGenericTypeInfo; }
 
	virtual BfTypeInstance* ToGenericTypeInstance() override { return (mGenericTypeInfo != NULL) ? this : NULL; }
 	virtual bool IsGenericTypeInstance() override { return mGenericTypeInfo != NULL; }
 	virtual bool IsSpecializedType() override { return (mGenericTypeInfo != NULL) && (!mGenericTypeInfo->mIsUnspecialized); }
 	virtual bool IsSpecializedByAutoCompleteMethod() override;
 	virtual bool IsUnspecializedType() override { return (mGenericTypeInfo != NULL) && (mGenericTypeInfo->mIsUnspecialized); }
 	virtual bool IsUnspecializedTypeVariation() override { return (mGenericTypeInfo != NULL) && (mGenericTypeInfo->mIsUnspecializedVariation); }
 	virtual bool IsNullable() override;
 	virtual bool HasVarConstraints();	
 	virtual bool IsTypeMemberIncluded(BfTypeDef* declaringTypeDef, BfTypeDef* activeTypeDef = NULL, BfModule* module = NULL) override;

	virtual BfTypeInstance* GetImplBaseType() { return mBaseType; }

	virtual bool IsIRFuncUsed(BfIRFunction func);

	void CalcHotVirtualData(/*Val128& vtHash, */Array<int>* ifaceMapping);
	int GetOrigVTableSize();
	int GetSelfVTableSize();
	int GetOrigSelfVTableSize();	
	int GetImplBaseVTableSize();
	int GetOrigImplBaseVTableSize();
	int GetIFaceVMethodSize();
	BfType* GetUnionInnerType(bool* wantSplat = NULL);
	BfPrimitiveType* GetDiscriminatorType(int* outDataIdx = NULL);		
	void GetUnderlyingArray(BfType*& type, int& size, bool& isVector);
	bool HasEquivalentLayout(BfTypeInstance* compareTo);
	BfIRConstHolder* GetOrCreateConstHolder();
	BfIRValue CreateConst(BfConstant* fromConst, BfIRConstHolder* fromHolder);	
	int GetInstStride() { return BF_ALIGN(mInstSize, mInstAlign); }
	bool HasOverrideMethods();	
	bool GetResultInfo(BfType*& valueType, int& okTagId);
	BfGenericTypeInfo::GenericParamsVector* GetGenericParamsVector(BfTypeDef* declaringTypeDef);
	void GenerateProjectsReferenced();
	bool IsAlwaysInclude();	
	bool HasBeenInstantiated() { return mHasBeenInstantiated || ((mAlwaysIncludeFlags & BfAlwaysIncludeFlag_AssumeInstantiated) != 0); }
	bool IncludeAllMethods() { return ((mAlwaysIncludeFlags & BfAlwaysIncludeFlag_IncludeAllMethods) != 0); }
	bool DefineStateAllowsStaticMethods() { return mDefineState >= BfTypeDefineState_HasInterfaces; }

	virtual void ReportMemory(MemReporter* memReporter) override;
};

template <typename T>
class LogAlloc
{
public:
	T* allocate(intptr count)
	{
		auto ptr = (T*)malloc(sizeof(T) * count);
		OutputDebugStrF("LogAlloc.allocate: %p\n", ptr);
		return ptr;
	}

	void deallocate(T* ptr)
	{
		OutputDebugStrF("LogAlloc.deallocate: %p\n", ptr);
		free(ptr);
	}

	void* rawAllocate(intptr size)
	{
		auto ptr = malloc(size);
		OutputDebugStrF("LogAlloc.rawAllocate: %p\n", ptr);
		return ptr;
	}

	void rawDeallocate(void* ptr)
	{
		OutputDebugStrF("LogAlloc.rawFree: %p\n", ptr);
		free(ptr);
	}
};


class BfBoxedType : public BfTypeInstance
{
public:
	enum BoxedFlags
	{
		BoxedFlags_None = 0,
		BoxedFlags_StructPtr = 1
	};

public:
	BfType* mElementType;
	BfBoxedType* mBoxedBaseType;
	BoxedFlags mBoxedFlags;

public:
	BfBoxedType()
	{
		mElementType = NULL;
		mBoxedBaseType = NULL;
		mBoxedFlags = BoxedFlags_None;
	}

	virtual bool IsBoxed() override { return true; }

	virtual bool IsValueType() override { return false; }
	virtual bool IsStruct() override { return false; }
	virtual bool IsEnum() override { return false; }
	virtual bool IsStructOrStructPtr() override { return false; }
	virtual bool IsObject() override { return true; }
	virtual bool IsObjectOrStruct() override { return true; }
	virtual bool IsObjectOrInterface() override { return true; }
	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
	
	virtual BfTypeInstance* ToGenericTypeInstance() override { return mElementType->ToGenericTypeInstance(); }
	virtual bool IsSpecializedType() override { return !mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedTypeVariation(); }		

	virtual BfTypeInstance* GetImplBaseType() override { return (mBoxedBaseType != NULL) ? mBoxedBaseType : mBaseType; }

	bool IsBoxedStructPtr()
	{
		return (mBoxedFlags & BoxedFlags_StructPtr) != 0;
	}

	BfType* GetModifiedElementType();
};

class BfTypeAliasType : public BfTypeInstance
{
public:
	BfType* mAliasToType;

public:
	BfTypeAliasType()
	{
		mAliasToType = NULL;		
	}

	virtual bool IsTypeAlias() override { return true; }	
	virtual BfType* GetUnderlyingType() override { return mAliasToType; }
	virtual bool WantsGCMarking() override { return mAliasToType->WantsGCMarking(); }
};

enum BfCaptureType
{
	BfCaptureType_None,
	BfCaptureType_Copy,
	BfCaptureType_Reference,	
};

class BfClosureType : public BfTypeInstance
{
public:
	Val128 mClosureHash; // Includes base type and capture info
	BfTypeInstance* mSrcDelegate;	
	bool mCreatedTypeDef;
	String mNameAdd;
	BfSource mSource;	
	Array<BfAstNode*> mDirectAllocNodes;
	bool mIsUnique;

public:
	BfClosureType(BfTypeInstance* srcDelegate, Val128 closureHash);
	~BfClosureType();

	void Init(BfProject* bfProject);
	BfFieldDef* AddField(BfType* type, const StringImpl& name);
	BfMethodDef* AddDtor();
	void Finish();

	virtual bool IsClosure() override { return true; }	
	virtual bool IsOnDemand() override { return true; }	
};

class BfDelegateType : public BfTypeInstance
{
public:		
	BfDelegateInfo mDelegateInfo;
	bool mIsUnspecializedType;
	bool mIsUnspecializedTypeVariation;

public:
	BfDelegateType()
	{
		mIsUnspecializedType = false;
		mIsUnspecializedTypeVariation = false;
	}
	~BfDelegateType();
	
	virtual bool IsOnDemand() override { return true; }
	
	virtual bool IsDelegate() override { return mTypeDef->mIsDelegate; }
	virtual bool IsDelegateFromTypeRef() override { return mTypeDef->mIsDelegate;  }

	virtual bool IsFunction() override { return !mTypeDef->mIsDelegate; }
	virtual bool IsFunctionFromTypeRef() override { return !mTypeDef->mIsDelegate; }

	virtual bool IsUnspecializedType() override { return mIsUnspecializedType; }
	virtual bool IsUnspecializedTypeVariation() override { return mIsUnspecializedTypeVariation; }

	virtual BfDelegateInfo* GetDelegateInfo() override { return &mDelegateInfo; }
};

class BfTupleType : public BfTypeInstance
{
public:		
	bool mCreatedTypeDef;
	String mNameAdd;	
	BfSource* mSource;
	bool mIsUnspecializedType;
	bool mIsUnspecializedTypeVariation;
	
public:
	BfTupleType();
	~BfTupleType();

	void Init(BfProject* bfProject, BfTypeInstance* valueTypeInstance);
	BfFieldDef* AddField(const StringImpl& name);
	void Finish();

	virtual bool IsOnDemand() override { return true; }
	virtual bool IsTuple() override { return true; }		

	virtual bool IsUnspecializedType() override { return mIsUnspecializedType; }
	virtual bool IsUnspecializedTypeVariation() override { return mIsUnspecializedTypeVariation; }
};

class BfConcreteInterfaceType : public BfType
{
public:
	BfTypeInstance* mInterface;

	virtual bool IsConcreteInterfaceType() override { return true; }
	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mInterface; }
};

class BfPointerType : public BfType
{
public:
	BfType* mElementType;

public:
	virtual bool IsWrappableType() override { return true; }
	virtual bool IsReified() override { return mElementType->IsReified(); }
	virtual bool IsPointer() override { return true; }
	virtual bool IsIntPtrable() override { return true; }
	virtual bool IsStructPtr() override { return mElementType->IsStruct(); }
	virtual bool IsStructOrStructPtr() override { return mElementType->IsStruct(); }
	virtual bool IsValueTypeOrValueTypePtr() override { return mElementType->IsValueType(); }
	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedTypeVariation(); }
	virtual bool IsVoidPtr() override { return mElementType->IsVoid(); }
};

// This is used for direct method references for generics so we can directly call methods rather than indirectly through delegates
class BfMethodRefType : public BfDependedType
{
public:
	BfMethodInstance* mMethodRef;
	String mMangledMethodName;
	BfTypeInstance* mOwner;
	int mOwnerRevision;
	bool mIsAutoCompleteMethod;	
	Array<int> mParamToDataIdx;
	Array<int> mDataToParamIdx;
	bool mIsUnspecialized;
	bool mIsUnspecializedVariation;

public:
	BfMethodRefType()
	{		
		mMethodRef = NULL;
		mOwner = NULL;
		mOwnerRevision = -1;
		mIsAutoCompleteMethod = false;		
		mIsUnspecialized = false;
		mIsUnspecializedVariation = false;
	}

	~BfMethodRefType();	
	
	//virtual bool IsValuelessType() override { return mSize != 0;  }
	virtual bool IsValueType() override { return true; }
	virtual bool IsComposite() override { return true; }
	virtual bool IsMethodRef() override { return true; }
	virtual bool IsSplattable() override { return true; }
	virtual int GetSplatCount() override { return (int)mDataToParamIdx.mSize; }

	virtual bool IsOnDemand() override { return true; }
	virtual bool IsTemporary() override { return true; }
	virtual bool IsUnspecializedType() override { return mIsUnspecialized; }
	virtual bool IsUnspecializedTypeVariation() override { return mIsUnspecializedVariation; }

	int GetCaptureDataCount();
	BfType* GetCaptureType(int captureDataIdx);	
	int GetDataIdxFromParamIdx(int paramIdx);
	int GetParamIdxFromDataIdx(int dataIdx);
	bool WantsDataPassedAsSplat(int dataIdx);
	
	//virtual BfType* GetUnderlyingType() override { return mOwner; }
};


class BfRefType : public BfType
{
public:
	enum RefKind
	{
		RefKind_Ref,
		RefKind_Out,
		RefKind_Mut
	};

	BfType* mElementType;
	RefKind mRefKind;

	// When an element gets rebuild, it may become valueless which makes us valueless
	void CheckElement()
	{
		if ((mDefineState >= BfTypeDefineState_Defined) && (mElementType->mDefineState < BfTypeDefineState_Defined) && (mElementType->CanBeValuelessType()))
			mDefineState = BfTypeDefineState_Declared;
	}

	virtual bool IsDataIncomplete() override { CheckElement(); return mDefineState < BfTypeDefineState_Defined; }
	virtual bool IsIncomplete() override { CheckElement(); return mDefineState < BfTypeDefineState_Defined; }
	virtual bool IsReified() override { return mElementType->IsReified(); }

	virtual bool IsRef() override { return true; }	
	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedTypeVariation(); }
	virtual bool CanBeValuelessType() override { return mElementType->CanBeValuelessType(); }
	virtual bool IsValuelessType() override { return mElementType->IsValuelessType(); }		
};

class BfArrayType : public BfTypeInstance
{
public:
	int mDimensions;

public:
	virtual bool IsArray() override { return true; }

	virtual bool IsValueType() override { return false; }
	virtual bool IsStruct() override { return false; }
	virtual bool IsStructOrStructPtr() override { return false; }
	virtual bool IsObject() override { return true; }
	virtual bool IsObjectOrStruct() override { return true; }
	virtual bool IsObjectOrInterface() override { return true; }

	int GetLengthBitCount();
};

class BfSizedArrayType : public BfDependedType
{
public:
	BfType* mElementType;
	intptr mElementCount;
	bool mWantsGCMarking;

public:
	virtual bool NeedsExplicitAlignment() override { return mElementType->NeedsExplicitAlignment(); }

	virtual bool IsSizedArray() override { return true; }
	virtual bool IsUndefSizedArray() override { return mElementCount == -1; }
		
	virtual bool IsWrappableType() override { return true; }
	virtual bool IsValueType() override { return true; } // Is a type of struct
	virtual bool IsValueTypeOrValueTypePtr() override { return true; }
	virtual bool IsComposite() override { return true; }
	virtual bool IsStruct() override { return false; } // But IsStruct infers a definition, which it does not have
	virtual bool IsStructOrStructPtr() override { return false; }
	virtual bool IsReified() override { return mElementType->IsReified(); }

	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedTypeVariation(); }
	virtual bool CanBeValuelessType() override { return true; }
	virtual bool WantsGCMarking() override { BF_ASSERT(mDefineState >= BfTypeDefineState_Defined); return mWantsGCMarking; }
	// Leave the default "zero sized" definition
	//virtual bool IsValuelessType()  override { return mElementType->IsValuelessType(); }
};

// This is used when a sized array is sized by a const generic argument
class BfUnknownSizedArrayType : public BfSizedArrayType
{
public:	
	BfType* mElementCountSource;

public:
	virtual bool NeedsExplicitAlignment() override { return mElementType->NeedsExplicitAlignment(); }

	virtual bool IsUnknownSizedArrayType() override { return true; }

	virtual bool IsWrappableType() override { return true; }
	virtual bool IsValueType() override { return true; } // Is a type of struct
	virtual bool IsValueTypeOrValueTypePtr() override { return true; }
	virtual bool IsComposite() override { return true; }
	virtual bool IsStruct() override { return false; } // But IsStruct infers a definition, which it does not have
	virtual bool IsStructOrStructPtr() override { return false; }
	virtual bool IsReified() override { return mElementType->IsReified(); }

	virtual bool IsDependentOnUnderlyingType() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mElementType; }
	virtual bool IsUnspecializedType() override { return mElementType->IsUnspecializedType() || mElementCountSource->IsUnspecializedType(); }
	virtual bool IsUnspecializedTypeVariation() override { return mElementType->IsUnspecializedTypeVariation() || mElementCountSource->IsUnspecializedTypeVariation(); }
	virtual bool CanBeValuelessType() override { return true; }	
	// Leave the default "zero sized" definition
	//virtual bool IsValuelessType()  override { return mElementType->IsValuelessType(); }
};

class BfConstExprValueType : public BfDependedType
{
public:
	BfType* mType;
	BfVariant mValue;

public:	
	~BfConstExprValueType();

	virtual bool IsConstExprValue() override { return true; }
	virtual BfType* GetUnderlyingType() override { return mType; }
};

/*class BfCustomAttributeArgument
{
public:	
	llvm::Constant* mConstParam;
};*/

class BfCustomAttributeSetProperty
{
public:	
	BfPropertyRef mPropertyRef;
	BfTypedValue mParam;
};

class BfCustomAttributeSetField
{
public:	
	BfFieldRef mFieldRef;
	BfTypedValue mParam;
};

class BfCustomAttribute
{
public:
	BfAttributeDirective* mRef;
	BfTypeInstance* mType;
	BfMethodDef* mCtor;
	Array<BfIRValue> mCtorArgs;
	Array<BfCustomAttributeSetProperty> mSetProperties;
	Array<BfCustomAttributeSetField> mSetField;
	bool mAwaitingValidation;

	BfAstNode* GetRefNode()
	{
		if (mRef->mAttributeTypeRef != NULL)
			return mRef->mAttributeTypeRef;
		return mRef;
	}
};

class BfCustomAttributes
{
public:
	Array<BfCustomAttribute> mAttributes;	
	bool Contains(BfTypeDef* typeDef);
	BfCustomAttribute* Get(BfTypeDef* typeDef);
	BfCustomAttribute* Get(BfType* type);

	void ReportMemory(MemReporter* memReporter);
};

class BfResolvedTypeSetFuncs : public MultiHashSetFuncs
{

};

class BfResolvedTypeSet : public MultiHashSet<BfType*, BfResolvedTypeSetFuncs>
{
public:
	enum BfHashFlags
	{
		BfHashFlag_None = 0,
		BfHashFlag_AllowRef = 1,
		BfHashFlag_AllowGenericParamConstValue = 2		
	};
	
	class LookupContext
	{	
	public:
		BfModule* mModule;
		BfTypeReference* mRootTypeRef;
		BfTypeDef* mRootTypeDef;		
		BfTypeInstance* mRootOuterTypeInstance;
		BfType* mResolvedType;		
		BfResolveTypeRefFlags mResolveFlags;		
		bool mHadVar;
		bool mFailed;		

	public:
		LookupContext()
		{
			mRootTypeRef = NULL;
			mRootTypeDef = NULL;
			mRootOuterTypeInstance = NULL;
			mModule = NULL;
			mResolvedType = NULL;
			mFailed = false;
			mHadVar = false;
			mResolveFlags = BfResolveTypeRefFlag_None;
		}

		BfType* ResolveTypeRef(BfTypeReference* typeReference);
		BfTypeDef* ResolveToTypeDef(BfTypeReference* typeReference, BfType** outType = NULL);
	};

public:
	static BfTypeDef* FindRootCommonOuterType(BfTypeDef* outerType, LookupContext* ctx, BfTypeInstance*& outCheckTypeInstance);
	static BfVariant EvaluateToVariant(LookupContext* ctx, BfExpression* expr, BfType*& outType);
	static bool GenericTypeEquals(BfTypeInstance* lhsGenericType, BfTypeVector* lhsTypeGenericArguments, BfTypeReference* rhs, LookupContext* ctx, int& genericParamOffset);
	static bool GenericTypeEquals(BfTypeInstance* lhsGenericType, BfTypeVector* typeGenericArguments, BfTypeReference* rhs, BfTypeDef* rhsTypeDef, LookupContext* ctx);
	static void HashGenericArguments(BfTypeReference* typeRef, LookupContext* ctx, int& hash);
	static int Hash(BfType* type, LookupContext* ctx, bool allowRef = false);
	static int DirectHash(BfTypeReference* typeRef, LookupContext* ctx, BfHashFlags flags = BfHashFlag_None);
	static int Hash(BfTypeReference* typeRef, LookupContext* ctx, BfHashFlags flags = BfHashFlag_None);
	static bool Equals(BfType* lhs, BfType* rhs, LookupContext* ctx);	
	static bool Equals(BfType* lhs, BfTypeReference* rhs, LookupContext* ctx);
	static bool Equals(BfType* lhs, BfTypeReference* rhs, BfTypeDef* rhsTypeDef, LookupContext* ctx);

public:
	BfResolvedTypeSet()
	{
		mHashSize = 9973;
		mHashHeads = (Entry**)AllocateZero(sizeof(Entry*) * mHashSize, alignof(Entry*));
	}	

	~BfResolvedTypeSet();
	
	template <typename T>
	bool Insert(T* findType, LookupContext* ctx, BfResolvedTypeSet::Entry** entryPtr)
	{
		CheckRehash();

		int tryCount = 0;		
		ctx->mFailed = false;
		int hashVal = Hash(findType, ctx, BfHashFlag_AllowRef);
		if ((ctx->mFailed) || (ctx->mHadVar))
		{			
			return false;
		}
		int bucket = (hashVal & 0x7FFFFFFF) % mHashSize;
		auto checkEntry = mHashHeads[bucket];
		while (checkEntry != NULL)
		{			
			// checkEntry->mType can be NULL if we're in the process of filling it in (and this Insert is from an element type)
			//  OR if the type resolution failed after node insertion
			if ((checkEntry->mValue != NULL) && (hashVal == checkEntry->mHash) && (Equals(checkEntry->mValue, findType, ctx)))
			{
				*entryPtr = checkEntry;
				return false;
			}
			checkEntry = checkEntry->mNext;

			tryCount++;
			// If this fires off, this may indicate that our hashes are equivalent but Equals fails
			if (tryCount >= 10)
			{
				NOP;
			}
			BF_ASSERT(tryCount < 10);
		}
		
		if ((ctx->mResolveFlags & BfResolveTypeRefFlag_NoCreate) != 0)
			return false;

		mCount++;
		Entry* entry = (Entry*)BfResolvedTypeSetFuncs::Allocate(sizeof(Entry), alignof(Entry));
		entry->mValue = NULL;
// 		if (mHashHeads[bucket] != NULL)
// 			mHashHeads[bucket]->mPrev = entry;
		entry->mNext = mHashHeads[bucket];
		entry->mHash = hashVal;
		mHashHeads[bucket] = entry;
		*entryPtr = entry;
		return true;
	}
	
// 	Iterator begin();
// 	Iterator end();
// 	Iterator erase(Iterator& itr);
	void RemoveEntry(Entry* entry);
};

class BfTypeUtils
{
public:
	static String HashEncode64(uint64 val); // Note: this only encodes 60 bits
	static String TypeToString(BfTypeReference* typeRef);
	static String TypeToString(BfTypeDef* typeDef, BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None);
	static bool TypeToString(StringImpl& str, BfTypeDef* typeDef, BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None);
	static bool TypeEquals(BfType* typeA, BfType* typeB, BfType* selfType);

	template <typename T>
	static void GetProjectList(BfType* checkType, T* projectList, int immutableLength)
	{
		if (checkType->IsBoxed())
			GetProjectList(((BfBoxedType*)checkType)->mElementType, projectList, immutableLength);

		BfTypeInstance* typeInst = checkType->ToTypeInstance();
		if (typeInst != NULL)
		{
			auto genericTypeInst = typeInst->ToGenericTypeInstance();
			if (genericTypeInst != NULL)
			{
				for (auto genericArg : genericTypeInst->mGenericTypeInfo->mTypeGenericArguments)
					GetProjectList(genericArg, projectList, immutableLength);
			}

			BfProject* bfProject = typeInst->mTypeDef->mProject;
			if (!projectList->Contains(bfProject))
			{
				bool handled = false;
				for (int idx = 0; idx < (int)projectList->size(); idx++)
				{
					auto checkProject = (*projectList)[idx];
					bool isBetter = bfProject->ContainsReference(checkProject);
					bool isWorse = checkProject->ContainsReference(bfProject);
					if (isBetter == isWorse)
						continue;
					if (isBetter)
					{
						if (idx >= immutableLength)
						{
							// This is even more specific, so replace with this one
							(*projectList)[idx] = bfProject;
							handled = true;
						}
					}
					else
					{
						// This is less specific, ignore
						handled = true;
					}
					break;
				}
				if (!handled)
				{
					projectList->Add(bfProject);
				}
			}

			if (checkType->IsTuple())
			{
				auto tupleType = (BfTupleType*)checkType;
				for (auto& fieldInstance : tupleType->mFieldInstances)
					GetProjectList(fieldInstance.mResolvedType, projectList, immutableLength);
			}

			auto delegateInfo = checkType->GetDelegateInfo();
			if (delegateInfo != NULL)
			{
				GetProjectList(delegateInfo->mReturnType, projectList, immutableLength);
				for (auto param : delegateInfo->mParams)
					GetProjectList(param, projectList, immutableLength);
			}
		}
		else if (checkType->IsPointer())
			GetProjectList(((BfPointerType*)checkType)->mElementType, projectList, immutableLength);
		else if (checkType->IsRef())
			GetProjectList(((BfPointerType*)checkType)->mElementType, projectList, immutableLength);
		else if (checkType->IsSizedArray())
			GetProjectList(((BfSizedArrayType*)checkType)->mElementType, projectList, immutableLength);
		else if (checkType->IsMethodRef())
			GetProjectList(((BfMethodRefType*)checkType)->mOwner, projectList, immutableLength);				
	}

	static BfPrimitiveType* GetPrimitiveType(BfModule* module, BfTypeCode typeCode);
	static void PopulateType(BfModule* module, BfType* type);

	template <typename T>
	static void SplatIterate(const T& dataLambda, BfType* checkType)
	{
		auto checkTypeInstance = checkType->ToTypeInstance();

		if ((checkTypeInstance != NULL) && (checkTypeInstance->IsValueType()) && (checkTypeInstance->IsDataIncomplete()))
			PopulateType(checkTypeInstance->mModule, checkTypeInstance);

		if (checkType->IsStruct())
		{															
			if (checkTypeInstance->mBaseType != NULL)
				SplatIterate<T>(dataLambda, checkTypeInstance->mBaseType);						
			
			if (checkTypeInstance->mIsUnion)
			{
				BfType* unionInnerType = checkTypeInstance->GetUnionInnerType();
				SplatIterate<T>(dataLambda, unionInnerType);
			}
			else
			{
				for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
				{
					auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
					if (fieldInstance->mDataIdx >= 0)
					{
						SplatIterate<T>(dataLambda, fieldInstance->GetResolvedType());
					}
				}
			}

			if (checkTypeInstance->IsEnum())
			{
				// Add discriminator
				auto dscrType = checkTypeInstance->GetDiscriminatorType();
				dataLambda(dscrType);
			}
		}
		else if (checkType->IsMethodRef())
		{
			auto methodRefType = (BfMethodRefType*)checkType;
			for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
			{
				if (methodRefType->WantsDataPassedAsSplat(dataIdx))
					SplatIterate<T>(dataLambda, methodRefType->GetCaptureType(dataIdx));
				else
					dataLambda(methodRefType->GetCaptureType(dataIdx));
			}
		}
		else if (!checkType->IsValuelessType())
		{
			dataLambda(checkType);
		}
	}

	static int GetSplatCount(BfType* type);
};

//void DbgCheckType(llvm::Type* checkType);

NS_BF_END

namespace std
{
	template<>
	struct hash<Beefy::BfTypeVector>
	{
		size_t operator()(const Beefy::BfTypeVector& val) const
		{
			int curHash = 0;
			for (auto type : val)
				curHash = ((curHash ^ type->mTypeId) << 5) - curHash;
			return curHash;
		}
	};

	template<>
	struct hash<Beefy::BfFieldRef>
	{
		size_t operator()(const Beefy::BfFieldRef& fieldRef) const
		{
			return (size_t)fieldRef.mTypeInstance + fieldRef.mFieldIdx;
		}
	};

	template<>
	struct hash<Beefy::BfHotDevirtualizedMethod>
	{
		size_t operator()(const Beefy::BfHotDevirtualizedMethod& val) const
		{
			return (size_t)val.mMethod;
		}
	};

	template<>
	struct hash<Beefy::BfHotFunctionReference>
	{
		size_t operator()(const Beefy::BfHotFunctionReference& val) const
		{
			return (size_t)val.mMethod;
		}
	};
}
