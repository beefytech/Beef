#pragma once

#include "BfModule.h"
#include "BeefySysLib/util/Deque.h"

NS_BF_BEGIN

class BfWorkListEntry
{
public:
	BfType* mType;
	BfModule* mFromModule;
	int mRevision;
	int mSignatureRevision;
	int mFromModuleRevision;	
	int mFromModuleRebuildIdx;
	int mReqId;	
	static int sCurReqId;		

public:
	BfWorkListEntry()
	{
		mType = NULL;
		mFromModule = NULL;
		mRevision = -1;		
		mSignatureRevision = -1;
		mFromModuleRevision = -1;
		mFromModuleRebuildIdx = -1;
		mReqId = ++sCurReqId;		
	}	
};

class BfTypeProcessRequest : public BfWorkListEntry
{
public:
	bool mRebuildType;

	BfTypeProcessRequest()
	{
		mRebuildType = false;
	}
};

class BfMethodSpecializationRequest : public BfWorkListEntry
{
public:	
	int32 mMethodIdx;
	BfTypeVector mMethodGenericArguments;
	BfGetMethodInstanceFlags mFlags;
	BfTypeInstance* mForeignType;

public:
	BfMethodSpecializationRequest()
	{		
		mMethodIdx = -1;
		mFlags = BfGetMethodInstanceFlag_None;
		mForeignType = NULL;
	}

	void Init(BfTypeInstance* typeInstance, BfTypeInstance* foreignType, BfMethodDef* methodDef)
	{
		mType = typeInstance;
		mMethodIdx = methodDef->mIdx;
		mForeignType = foreignType;
		if (foreignType != NULL)
			BF_ASSERT(foreignType->mTypeDef->mMethods[mMethodIdx] == methodDef);
		else
			BF_ASSERT(typeInstance->mTypeDef->mMethods[mMethodIdx] == methodDef);
	}
};

class BfMethodProcessRequest : public BfWorkListEntry
{
public:
	BfMethodInstance* mMethodInstance;

	BfMethodProcessRequest()
	{
		mMethodInstance = NULL;
	}

	~BfMethodProcessRequest()
	{
		Disable();
	}

	void Disable()
	{
		if (mMethodInstance != NULL)
		{
			BF_ASSERT(mMethodInstance->mMethodProcessRequest == this);
			mMethodInstance->mMethodProcessRequest = NULL;
			mMethodInstance = NULL;
		}
	}
};

class BfInlineMethodRequest : public BfMethodProcessRequest
{
public:	
	BfIRFunction mFunc;
	
	~BfInlineMethodRequest()
	{
		mMethodInstance = NULL;
	}
};

class BfTypeRefVerifyRequest : public BfWorkListEntry
{
public:
	BfTypeInstance* mCurTypeInstance;
	BfAstNode* mRefNode;	
};

class BfMidCompileRequest : public BfWorkListEntry
{
public:	
	String mReason;
};

struct BfStringPoolEntry
{
	String mString;
	int mLastUsedRevision;
	int mFirstUsedRevision;
};

class BfGlobalContainerEntry
{
public:
	BfTypeDef* mTypeDef;
	BfTypeInstance* mTypeInst;
};

class BfTypeState
{
public:
	enum ResolveKind
	{
		ResolveKind_None,
		ResolveKind_BuildingGenericParams,
		ResolveKind_ResolvingVarType,
		ResolveKind_UnionInnerType,
		ResolveKind_LocalVariable,
		ResolveKind_Attributes,
		ResolveKind_FieldType,
		ResolveKind_ConstField
	};

public:
	BfTypeState* mPrevState;

	BfType* mType;
	BfTypeDef* mGlobalContainerCurUserTypeDef;
	Array<BfGlobalContainerEntry> mGlobalContainers; // All global containers that are visible	

	BfPopulateType mPopulateType;
	BfTypeReference* mCurBaseTypeRef;
	BfTypeInstance* mCurBaseType;
	BfTypeReference* mCurAttributeTypeRef;
	BfFieldDef* mCurFieldDef;	
	BfTypeDef* mCurTypeDef;
	BfTypeDef* mForceActiveTypeDef;	
	BfProject* mActiveProject;
	ResolveKind mResolveKind;
	BfAstNode* mCurVarInitializer;
	int mArrayInitializerSize;

public:
	BfTypeState()
	{
		mPrevState = NULL;
		mType = NULL;
		mGlobalContainerCurUserTypeDef = NULL;

		mPopulateType = BfPopulateType_Identity;
		mCurBaseTypeRef = NULL;
		mCurBaseType = NULL;
		mCurFieldDef = NULL;
		mCurAttributeTypeRef = NULL;
		mCurTypeDef = NULL;
		mForceActiveTypeDef = NULL;
		mActiveProject = NULL;
		mCurVarInitializer = NULL;
		mArrayInitializerSize = -1;
		mResolveKind = ResolveKind_None;
	}

	BfTypeState(BfType* type, BfTypeState* prevState = NULL)
	{
		mPrevState = prevState;
		mType = type;
		mGlobalContainerCurUserTypeDef = NULL;

		mPopulateType = BfPopulateType_Declaration;
		mCurBaseTypeRef = NULL;
		mCurBaseType = NULL;
		mCurFieldDef = NULL;
		mCurAttributeTypeRef = NULL;
		mCurTypeDef = NULL;
		mForceActiveTypeDef = NULL;
		mCurVarInitializer = NULL;
		mArrayInitializerSize = -1;
		mResolveKind = ResolveKind_None;
	}
};

class BfSavedTypeData
{
public:
	BfHotTypeData* mHotTypeData;	
	int mTypeId;

public:
	~BfSavedTypeData()
	{
		delete mHotTypeData;
	}
};

struct SpecializedErrorData
{
	// We need to store errors during type specialization and method specialization, 
	//  because if the type is deleted we need to clear the errors
	BfTypeInstance* mRefType;
	BfModule* mModule;
	BfMethodInstance* mMethodInstance;
	BfError* mError;

	SpecializedErrorData()
	{
		mRefType = NULL;
		mModule = NULL;
		mMethodInstance = NULL;
		mError = NULL;
	}
};

struct BfCaseInsensitiveStringHash
{
	size_t operator()(const StringImpl& str) const
	{
		int curHash = 0;		
		for (int i = 0; i < (int)str.length(); i++)		
			curHash = ((curHash ^ (int)(intptr)toupper(str[i])) << 5) - curHash;
		return curHash;
	}
};

struct BfCaseInsensitiveStringEquals
{
	bool operator()(const StringImpl& lhs, const StringImpl& rhs) const
	{		
		if (lhs.length() != rhs.length())
			return false;
		return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
	}
};

template <typename T>
class WorkQueue : public Deque<T*>
{
public:
	BumpAllocator mWorkAlloc;

	int RemoveAt(int idx)
	{
		if (idx == 0)
		{
			T*& ref = (*this)[idx];
			if (ref != NULL)
				(*ref).~T();			
			Deque<T*>::RemoveAt(0);
			if (this->mSize == 0)
			{
				mWorkAlloc.Clear();
				this->mOffset = 0;
			}

			return idx - 1;
		}
		else
		{
			T*& ref = (*this)[idx];
			if (ref != NULL)
			{
				(*ref).~T();
				ref = NULL;
			}
			return idx;
		}
	}

	void Clear()
	{
		Deque<T*>::Clear();
		mWorkAlloc.Clear();
	}

	T* Alloc()
	{
		T* item = mWorkAlloc.Alloc<T>();
		this->Add(item);
		return item;
	}	
};

template <typename T>
class PtrWorkQueue : public Deque<T>
{
public:
	int RemoveAt(int idx)
	{
		if (idx == 0)
		{
			Deque<T>::RemoveAt(0);
			return idx - 1;
		}
		else
		{
			(*this)[idx] = NULL;
			return idx;
		}
	}
};

class BfConstraintState
{
public:
	BfGenericParamInstance* mGenericParamInstance;
	BfType* mLeftType;
	BfType* mRightType;
	BfConstraintState* mPrevState;
	BfMethodInstance* mMethodInstance;
	BfTypeVector* mMethodGenericArgsOverride;

public:
	BfConstraintState()
	{
		mGenericParamInstance = NULL;
		mLeftType = NULL;
		mRightType = NULL;
		mMethodInstance = NULL;
		mMethodGenericArgsOverride = NULL;
		mPrevState = NULL;
	}

	bool operator==(const BfConstraintState& other) const
	{
		return 
			(mGenericParamInstance == other.mGenericParamInstance) &&
			(mLeftType == other.mLeftType) &&
			(mRightType == other.mRightType);
	}
};

class BfContext
{
public:
	CritSect mCritSect;	
	bool mDeleting;	
	
	BfTypeState* mCurTypeState;
	BfSizedArray<BfNamespaceDeclaration*>* mCurNamespaceNodes;
	BfConstraintState* mCurConstraintState;
	bool mResolvingVarField;
	int mMappedObjectRevision;
	bool mAssertOnPopulateType;

	BfSystem* mSystem;
	BfCompiler* mCompiler;		
	
	bool mAllowLockYield;
	bool mLockModules;
	BfModule* mScratchModule;
	BfModule* mUnreifiedModule;	
	HashSet<String> mUsedModuleNames;
	Dictionary<BfProject*, BfModule*> mProjectModule;
	Array<BfModule*> mModules;
	Array<BfModule*> mDeletingModules;	
	HashSet<BfTypeInstance*> mFailTypes; // All types handled after a failure need to be rebuild on subsequent compile		
	HashSet<BfTypeInstance*> mReferencedIFaceSlots;

	BfMethodInstance* mValueTypeDeinitSentinel;

	Array<BfAstNode*> mTempNodes;
	BfResolvedTypeSet mResolvedTypes;	
	Array<BfType*> mTypes; // Can contain NULLs for deleted types
	Array<BfFieldInstance*> mFieldResolveReentrys; // For detecting 'var' field circular refs	
	Dictionary<String, BfSavedTypeData*> mSavedTypeDataMap;
	Array<BfSavedTypeData*> mSavedTypeData;

	BfTypeInstance* mBfTypeType;
	BfTypeInstance* mBfObjectType;
	bool mCanSkipObjectCtor;
	bool mCanSkipValueTypeCtor;
	BfPointerType* mBfClassVDataPtrType;

	PtrWorkQueue<BfModule*> mReifyModuleWorkList;
	WorkQueue<BfMethodProcessRequest> mMethodWorkList;
	WorkQueue<BfInlineMethodRequest> mInlineMethodWorkList;	
	WorkQueue<BfTypeProcessRequest> mPopulateTypeWorkList;
	WorkQueue<BfMethodSpecializationRequest> mMethodSpecializationWorkList;
	WorkQueue<BfTypeRefVerifyRequest> mTypeRefVerifyWorkList;
	WorkQueue<BfMidCompileRequest> mMidCompileWorkList;
	PtrWorkQueue<BfModule*> mFinishedSlotAwaitModuleWorkList;
	PtrWorkQueue<BfModule*> mFinishedModuleWorkList;
	bool mHasReifiedQueuedRebuildTypes;

	Array<BfGenericParamType*> mGenericParamTypes[3];
	
	Array<BfType*> mTypeGraveyard;
	Array<BfTypeDef*> mTypeDefGraveyard;
	Array<BfLocalMethod*> mLocalMethodGraveyard;

	Dictionary<String, int> mStringObjectPool;
	Dictionary<int, BfStringPoolEntry> mStringObjectIdMap;
	int mCurStringObjectPoolId;

	HashSet<BfTypeInstance*> mQueuedSpecializedMethodRebuildTypes;	

	BfAllocPool<BfPointerType> mPointerTypePool;	
	BfAllocPool<BfArrayType> mArrayTypePool;
	BfAllocPool<BfSizedArrayType> mSizedArrayTypePool;
	BfAllocPool<BfUnknownSizedArrayType> mUnknownSizedArrayTypePool;
	BfAllocPool<BfBoxedType> mBoxedTypePool;
	BfAllocPool<BfTupleType> mTupleTypePool;
	BfAllocPool<BfTypeAliasType> mAliasTypePool;
	BfAllocPool<BfRefType> mRefTypePool;
	BfAllocPool<BfModifiedTypeType> mModifiedTypeTypePool;
	BfAllocPool<BfTypeInstance> mGenericTypeInstancePool;	
	BfAllocPool<BfArrayType> mArrayTypeInstancePool;
	BfAllocPool<BfGenericParamType> mGenericParamTypePool;
	BfAllocPool<BfDirectTypeDefReference> mTypeDefTypeRefPool;
	BfAllocPool<BfConcreteInterfaceType> mConcreteInterfaceTypePool;
	BfAllocPool<BfConstExprValueType> mConstExprValueTypePool;
	BfAllocPool<BfDelegateType> mDelegateTypePool;	
	BfPrimitiveType* mPrimitiveTypes[BfTypeCode_Length];
	BfPrimitiveType* mPrimitiveStructTypes[BfTypeCode_Length];

public:
	void AssignModule(BfType* type);
	void HandleTypeWorkItem(BfType* type);
	void EnsureHotMangledVirtualMethodName(BfMethodInstance* methodInstance);
	void EnsureHotMangledVirtualMethodNames();
	void PopulateHotTypeDataVTable(BfTypeInstance* typeInstance);
	void DeleteType(BfType* type, bool deferDepRebuilds = false);
	void UpdateAfterDeletingTypes();
	void VerifyTypeLookups(BfTypeInstance* typeInst);		
	void GenerateModuleName_TypeInst(BfTypeInstance* typeInst, String& name);
	void GenerateModuleName_Type(BfType* type, String& name);
	String GenerateModuleName(BfTypeInstance* typeInst);
	bool IsSentinelMethod(BfMethodInstance* methodInstance);
	void SaveDeletingType(BfType* type);		
	BfType* FindType(const StringImpl& typeName);
	String TypeIdToString(int typeId);
	BfHotTypeData* GetHotTypeData(int typeId);	
	void ReflectInit();

public:
	BfContext(BfCompiler* compiler);
	~BfContext();

	void ReportMemory(MemReporter* memReporter);
	void ProcessMethod(BfMethodInstance* methodInstance);
	int GetStringLiteralId(const StringImpl& str);		
	void CheckLockYield();
	bool IsCancellingAndYield();
	void QueueFinishModule(BfModule * module);
	void CancelWorkItems();
	bool ProcessWorkList(bool onlyReifiedTypes, bool onlyReifiedMethods);
	void HandleChangedTypeDef(BfTypeDef* typeDef, bool isAutoCompleteTempType = false);
	void PreUpdateRevisedTypes();	
	void UpdateRevisedTypes();	
	void VerifyTypeLookups();	
	void QueueMethodSpecializations(BfTypeInstance* typeInst, bool checkSpecializedMethodRebuildFlag);
	void MarkAsReferenced(BfDependedType* depType);
	void RemoveInvalidFailTypes();
	void RemoveInvalidWorkItems();	
	BfType* FindTypeById(int typeId);
	void AddTypeToWorkList(BfType* type);
	void ValidateDependencies();
	void RebuildType(BfType* type, bool deleteOnDemandTypes = true, bool rebuildModule = true, bool placeSpecializiedInPurgatory = true);
	void RebuildDependentTypes(BfDependedType* dType);	
	void QueueMidCompileRebuildDependentTypes(BfDependedType* dType, const String& reason);
	void RebuildDependentTypes_MidCompile(BfDependedType* dType, const String& reason);
	bool CanRebuild(BfType* type);
	void TypeDataChanged(BfDependedType* dType, bool isNonStaticDataChange);
	void TypeMethodSignaturesChanged(BfTypeInstance* typeInst);
	void TypeInlineMethodInternalsChanged(BfTypeInstance* typeInst);
	void TypeConstEvalChanged(BfTypeInstance* typeInst);
	void TypeConstEvalFieldChanged(BfTypeInstance* typeInst);
	void CheckSpecializedErrorData();
	void TryUnreifyModules();
	void MarkUsedModules(BfProject* project, BfModule* module);
	void RemapObject();
	void Finish();
	void Cleanup();
};

NS_BF_END
