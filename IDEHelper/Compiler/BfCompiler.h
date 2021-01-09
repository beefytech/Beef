#pragma once

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/String.h"
#include "BfAst.h"
#include "BfSystem.h"
#include "llvm/Support/Compiler.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "BfResolvedTypeUtils.h"
#include <unordered_set>
#include "BfContext.h"
#include "BfCodeGen.h"
#include "BfMangler.h"

#pragma warning(pop)

NS_BF_BEGIN

class BfType;
class BfResolvedType;
class BfTypeInstance;
class BfModule;
class BfFileInstance;
class BfAutoComplete;
class BfMethodInstance;
class BfSourceClassifier;
class BfResolvePassData;
class CeMachine;

enum BfCompileOnDemandKind
{
	BfCompileOnDemandKind_AlwaysInclude,
	BfCompileOnDemandKind_ResolveUnused,
	BfCompileOnDemandKind_SkipUnused
};

class BfCompiler 
{
public:
	enum CompileState
	{
		CompileState_None,
		CompileState_Normal,
		CompileState_Unreified,
		CompileState_VData,
		CompileState_Cleanup
	};

	struct Stats
	{	
		int mTotalTypes;

		int mMethodDeclarations;
		int mTypesPopulated;
		int mMethodsProcessed;
		int mUnreifiedMethodsProcessed;		

		int mQueuedTypesProcessed;
		int mTypesQueued;
		int mTypesDeleted;
		int mMethodsQueued;		

		int mModulesStarted;
		int mModulesFinished;
		
		int mModulesReified;
		int mModulesUnreified;

		int mReifiedModuleCount;
		int mIRBytes;
		int mConstBytes;		
	};
	Stats mStats;

	struct Options
	{
		BfProject* mHotProject;
		int mHotCompileIdx;
		
		int32 mForceRebuildIdx;
		BfCompileOnDemandKind mCompileOnDemandKind;		
		String mTargetTriple;
		BfPlatformType mPlatformType;
		BfMachineType mMachineType;
		int mCLongSize;
		BfToolsetType mToolsetType;
		BfSIMDSetting mSIMDSetting;		
		String mMallocLinkName;
		String mFreeLinkName;
		bool mIncrementalBuild;		

		bool mEmitDebugInfo;
		bool mEmitLineInfo;

		bool mNoFramePointerElim;
		bool mInitLocalVariables;
		bool mRuntimeChecks;
		bool mAllowStructByVal;
		bool mEmitDynamicCastCheck;
						
		bool mAllowHotSwapping;
		bool mObjectHasDebugFlags;		
		bool mEnableRealtimeLeakCheck;
		bool mEmitObjectAccessCheck; // Only valid with mObjectHasDebugFlags		
		bool mEnableCustodian;
		bool mEnableSideStack;
		bool mHasVDataExtender; 
		bool mDebugAlloc;
		bool mOmitDebugHelpers;

		bool mUseDebugBackingParams;

		bool mWriteIR;
		bool mGenerateObj;
		bool mGenerateBitcode;

		int mAllocStackCount;
		bool mExtraResolveChecks;
		int mMaxSplatRegs;

		String mErrorString;

		Options()
		{
			mMallocLinkName = "malloc";
			mFreeLinkName = "free";

			mHotCompileIdx = 0;
			mForceRebuildIdx = 0;
			mCompileOnDemandKind = BfCompileOnDemandKind_AlwaysInclude;
			mPlatformType = BfPlatformType_Unknown;
			mMachineType = BfMachineType_x86;
			mCLongSize = 4;
			mToolsetType = BfToolsetType_Microsoft;
			mSIMDSetting = BfSIMDSetting_None;
			mHotProject = NULL;
			mDebugAlloc = false;
			mOmitDebugHelpers = false;
			mIncrementalBuild = true;
			mEmitDebugInfo = false;
			mEmitLineInfo = false;
			mNoFramePointerElim = true;
			mInitLocalVariables = false;
			mRuntimeChecks = true;
			mAllowStructByVal = false;
			mEmitDynamicCastCheck = true;
			mAllowHotSwapping = false;
			mEmitObjectAccessCheck = false;
			mObjectHasDebugFlags = false;			
			mEnableRealtimeLeakCheck = false;
			mWriteIR = false;
			mGenerateObj = true;
			mGenerateBitcode = false;
			mEnableCustodian = false;
			mEnableSideStack = false;
			mHasVDataExtender = false;
			mUseDebugBackingParams = true;
			mAllocStackCount = 1;

			mExtraResolveChecks = false;
#ifdef _DEBUG
			mExtraResolveChecks = false;
#endif
			mMaxSplatRegs = 4;
		}

		bool IsCodeView()
		{
#ifdef BF_PLATFORM_WINDOWS
			return mToolsetType != BfToolsetType_GNU;
#else
			return false;
#endif
		}
	};
	Options mOptions;
	
	enum HotTypeFlags
	{
		HotTypeFlag_None		= 0,
		HotTypeFlag_UserNotUsed = 1,
		HotTypeFlag_UserUsed	= 2,				
		
		HotTypeFlag_Heap		= 4,
		HotTypeFlag_ActiveFunction = 8, // Only set for a type version mismatch
		HotTypeFlag_Delegate	= 0x10,    // Only set for a type version mismatch
		HotTypeFlag_FuncPtr     = 0x20,   // Only set for a type version mismatch
		HotTypeFlag_CanAllocate = 0x40
	};	

	enum HotResolveFlags
	{
		HotResolveFlag_None = 0,
		HotResolveFlag_HadDataChanges = 1
	};

	struct HotReachableData
	{
		HotTypeFlags mTypeFlags;
		bool mHadNonDevirtualizedCall;

		HotReachableData()
		{
			mTypeFlags = HotTypeFlag_None;
			mHadNonDevirtualizedCall = false;
		}
	};

	class HotResolveData
	{
	public:
		HotResolveFlags mFlags;
		Dictionary<BfHotMethod*, HotReachableData> mReachableMethods;
		HashSet<BfHotMethod*> mActiveMethods;
		Dictionary<BfHotTypeVersion*, HotTypeFlags> mHotTypeFlags;
		Array<HotTypeFlags> mHotTypeIdFlags;
		Array<BfHotDepData*> mReasons;
		HashSet<BfHotMethod*> mDeferredThisCheckMethods;
				
		~HotResolveData();
	};

	class HotData
	{
	public:
		BfCompiler* mCompiler;
		Dictionary<String, BfHotMethod*> mMethodMap;
		Dictionary<BfHotMethod*, String*> mMethodNameMap;
		Dictionary<BfHotTypeVersion*, BfHotThisType*> mThisType;
		Dictionary<BfHotTypeVersion*, BfHotAllocation*> mAllocation;
		Dictionary<BfHotMethod*, BfHotDevirtualizedMethod*> mDevirtualizedMethods;
		Dictionary<BfHotMethod*, BfHotFunctionReference*> mFuncPtrs;
		Dictionary<BfHotMethod*, BfHotVirtualDeclaration*> mVirtualDecls;
		Dictionary<BfHotMethod*, BfHotInnerMethod*> mInnerMethods;

	public:
		~HotData();
		void ClearUnused(bool isHotCompile);

		BfHotThisType* GetThisType(BfHotTypeVersion* hotVersion);
		BfHotAllocation* GetAllocation(BfHotTypeVersion* hotVersion);
		BfHotDevirtualizedMethod* GetDevirtualizedMethod(BfHotMethod* hotMethod);
		BfHotFunctionReference* GetFunctionReference(BfHotMethod* hotMethod);		
		BfHotVirtualDeclaration* GetVirtualDeclaration(BfHotMethod* hotMethod);
		BfHotInnerMethod* GetInnerMethod(BfHotMethod* hotMethod);
	};

	class HotState
	{
	public:
		BfProject* mHotProject;
		int mLastStringId;		
		int mCommittedHotCompileIdx;
		bool mHasNewTypes;
		bool mHasNewInterfaceTypes;		
		Array<BfCodeGenFileEntry> mQueuedOutFiles; // Queues up when we have failed hot compiles
		HashSet<int> mSlotDefineTypeIds;
		HashSet<int> mNewlySlottedTypeIds;
		HashSet<int> mPendingDataChanges;
		HashSet<int> mPendingFailedSlottings;
		Dictionary<String, int> mDeletedTypeNameMap;
		Val128 mVDataHashEx;

	public:
		HotState()
		{			
			mHotProject = NULL;
			mLastStringId = -1;
			mCommittedHotCompileIdx = 0;
			mHasNewTypes = false;
			mHasNewInterfaceTypes = false;
		}

		~HotState();

		bool HasPendingChanges(BfTypeInstance* type);
		void RemovePendingChanges(BfTypeInstance* type);
	};
	HotData* mHotData;
	HotState* mHotState;	
	HotResolveData* mHotResolveData;

	struct StringValueEntry
	{
		int mId;
		BfIRValue mStringVal;
	};		

	struct TestMethod
	{
		String mName;
		BfMethodInstance* mMethodInstance;
	};	

public:	
	BfPassInstance* mPassInstance;	
	FILE* mCompileLogFP;

	CeMachine* mCEMachine;
	BfSystem* mSystem;	
	bool mIsResolveOnly;
	BfResolvePassData* mResolvePassData;
	Dictionary<String, Array<int>> mAttributeTypeOptionMap;
	int mRevision;	
	bool mLastRevisionAborted;
	BfContext* mContext;
	BfCodeGen mCodeGen;
	String mOutputDirectory;
	bool mCanceling;
	bool mFastFinish;
	bool mHasQueuedTypeRebuilds; // Infers we had a fast finish that requires a type rebuild
	bool mHadCancel;
	bool mWantsDeferMethodDecls;		
	bool mInInvalidState;
	float mCompletionPct;
	int mHSPreserveIdx;
	BfModule* mLastAutocompleteModule;		
	CompileState mCompileState;

	Array<BfVDataModule*> mVDataModules;	
		
	BfTypeDef* mChar32TypeDef;
	BfTypeDef* mBfObjectTypeDef;

	BfTypeDef* mArray1TypeDef;
	BfTypeDef* mArray2TypeDef;
	BfTypeDef* mArray3TypeDef;
	BfTypeDef* mArray4TypeDef;
	BfTypeDef* mSpanTypeDef;	
		
	BfTypeDef* mClassVDataTypeDef;	
	
	BfTypeDef* mDbgRawAllocDataTypeDef;
	BfTypeDef* mDeferredCallTypeDef;		
	BfTypeDef* mDelegateTypeDef;
	BfTypeDef* mActionTypeDef;
	BfTypeDef* mEnumTypeDef;
	BfTypeDef* mStringTypeDef;
	BfTypeDef* mStringViewTypeDef;
	BfTypeDef* mTypeTypeDef;
	BfTypeDef* mValueTypeTypeDef;	
	BfTypeDef* mResultTypeDef;
	BfTypeDef* mFunctionTypeDef;
	BfTypeDef* mGCTypeDef;	
	BfTypeDef* mGenericIEnumerableTypeDef;
	BfTypeDef* mGenericIEnumeratorTypeDef;
	BfTypeDef* mGenericIRefEnumeratorTypeDef;
	
	BfTypeDef* mThreadTypeDef;
	BfTypeDef* mInternalTypeDef;
	BfTypeDef* mCompilerTypeDef;
	BfTypeDef* mDiagnosticsDebugTypeDef;
	BfTypeDef* mIDisposableTypeDef;
	BfTypeDef* mIPrintableTypeDef;
	BfTypeDef* mIHashableTypeDef;
	BfTypeDef* mIComptimeTypeApply;
	BfTypeDef* mIComptimeMethodApply;
	
	BfTypeDef* mMethodRefTypeDef;
	BfTypeDef* mNullableTypeDef;
	
	BfTypeDef* mPointerTTypeDef;
	BfTypeDef* mPointerTypeDef;
	BfTypeDef* mReflectArrayType;
	BfTypeDef* mReflectFieldDataDef;
	BfTypeDef* mReflectFieldSplatDataDef;
	BfTypeDef* mReflectMethodDataDef;
	BfTypeDef* mReflectParamDataDef;
	BfTypeDef* mReflectInterfaceDataDef;
	BfTypeDef* mReflectPointerType;
	BfTypeDef* mReflectRefType;
	BfTypeDef* mReflectSizedArrayType;
	BfTypeDef* mReflectSpecializedGenericType;
	BfTypeDef* mReflectTypeInstanceTypeDef;
	BfTypeDef* mReflectUnspecializedGenericType;	
	
	BfTypeDef* mSizedArrayTypeDef;
	BfTypeDef* mAttributeTypeDef;
	BfTypeDef* mAttributeUsageAttributeTypeDef;
	BfTypeDef* mLinkNameAttributeTypeDef;
	BfTypeDef* mCallingConventionAttributeTypeDef;
	BfTypeDef* mOrderedAttributeTypeDef;	
	BfTypeDef* mInlineAttributeTypeDef;	
	BfTypeDef* mCLinkAttributeTypeDef;
	BfTypeDef* mImportAttributeTypeDef;
	BfTypeDef* mExportAttributeTypeDef;
	BfTypeDef* mCReprAttributeTypeDef;
	BfTypeDef* mUnderlyingArrayAttributeTypeDef;
	BfTypeDef* mAlignAttributeTypeDef;
	BfTypeDef* mAllowDuplicatesAttributeTypeDef;
	BfTypeDef* mNoDiscardAttributeTypeDef;
	BfTypeDef* mDisableChecksAttributeTypeDef;
	BfTypeDef* mDisableObjectAccessChecksAttributeTypeDef;
	BfTypeDef* mFriendAttributeTypeDef;
	BfTypeDef* mComptimeAttributeTypeDef;
	BfTypeDef* mConstEvalAttributeTypeDef;
	BfTypeDef* mNoExtensionAttributeTypeDef;
	BfTypeDef* mCheckedAttributeTypeDef;
	BfTypeDef* mUncheckedAttributeTypeDef;	
	BfTypeDef* mStaticInitAfterAttributeTypeDef;
	BfTypeDef* mStaticInitPriorityAttributeTypeDef;	
	BfTypeDef* mTestAttributeTypeDef;
	BfTypeDef* mThreadStaticAttributeTypeDef;	
	BfTypeDef* mUnboundAttributeTypeDef;
	BfTypeDef* mObsoleteAttributeTypeDef;
	BfTypeDef* mErrorAttributeTypeDef;
	BfTypeDef* mWarnAttributeTypeDef;
	BfTypeDef* mIgnoreErrorsAttributeTypeDef;
	BfTypeDef* mReflectAttributeTypeDef;		
	BfTypeDef* mOnCompileAttributeTypeDef;

	int mCurTypeId;	
	int mTypeInitCount;
	String mOutputPath;
	Array<BfType*> mGenericInstancePurgatory;	
	Array<int> mTypeIdFreeList;

	int mMaxInterfaceSlots;
	bool mInterfaceSlotCountChanged;

public:		
	bool IsTypeAccessible(BfType* checkType, BfProject* curProject);
	bool IsTypeUsed(BfType* checkType, BfProject* curProject);
	bool IsModuleAccessible(BfModule* module, BfProject* curProject);
	void FixVDataHash(BfModule* bfModule);
	void CheckModuleStringRefs(BfModule* module, BfVDataModule* vdataModule, int lastModuleRevision, HashSet<int>& foundStringIds, HashSet<int>& dllNameSet, Array<BfMethodInstance*>& dllMethods, Array<BfCompiler::StringValueEntry>& stringValueEntries);
	void HashModuleVData(BfModule* module, HashContext& hash);
	BfIRFunction CreateLoadSharedLibraries(BfVDataModule* bfModule, Array<BfMethodInstance*>& dllMethods);
	void GetTestMethods(BfVDataModule* bfModule, Array<TestMethod>& testMethods, HashContext& vdataHashCtx);
	void EmitTestMethod(BfVDataModule* bfModule, Array<TestMethod>& testMethods, BfIRValue& retValue);
	void CreateVData(BfVDataModule* bfModule);
	void UpdateDependencyMap(bool deleteUnusued, bool& didWork);
	void ProcessPurgatory(bool reifiedOnly);
	bool VerifySlotNums();
	bool QuickGenerateSlotNums();
	bool SlowGenerateSlotNums();
	void GenerateSlotNums();	
	void GenerateDynCastData();
	void UpdateRevisedTypes();
	void VisitAutocompleteExteriorIdentifiers();		
	void VisitSourceExteriorNodes();
	void UpdateCompletion();
	bool DoWorkLoop(bool onlyReifiedTypes = false, bool onlyReifiedMethods = false);
	BfMangler::MangleKind GetMangleKind();
	
	BfTypeDef* GetArrayTypeDef(int dimensions);
	void GenerateAutocompleteInfo();
	void MarkStringPool(BfModule* module);
	void MarkStringPool(BfIRConstHolder* constHolder, BfIRValue irValue);
	void ClearUnusedStringPoolEntries();
	void ClearBuildCache();
	int GetDynCastVDataCount();
	bool IsAutocomplete();
	BfAutoComplete* GetAutoComplete();
	bool IsHotCompile();
	bool IsSkippingExtraResolveChecks();
	int GetVTableMethodOffset();
	BfType* CheckSymbolReferenceTypeRef(BfModule* module, BfTypeReference* typeRef);
	void AddToRebuildTypeList(BfTypeInstance* typeInst, HashSet<BfTypeInstance*>& rebuildTypeInstList);
	void AddDepsToRebuildTypeList(BfTypeInstance* typeInst, HashSet<BfTypeInstance*>& rebuildTypeInstList);
	void CompileReified();
	void PopulateReified();

	void HotCommit();	
	void HotResolve_Start(HotResolveFlags flags);
	void HotResolve_PopulateMethodNameMap();
	bool HotResolve_AddReachableMethod(BfHotMethod* hotMethod, HotTypeFlags flags, bool devirtualized, bool forceProcess = false);
	void HotResolve_AddReachableMethod(const StringImpl& methodName);
	void HotResolve_AddActiveMethod(BfHotMethod* hotMethod);
	void HotResolve_AddActiveMethod(const StringImpl& methodName);
	void HotResolve_AddDelegateMethod(const StringImpl& methodName);
	void HotResolve_ReportType(BfHotTypeVersion* hotTypeVersion, HotTypeFlags flags, BfHotDepData* reason);
	void HotResolve_ReportType(int typeId, HotTypeFlags flags);
	String HotResolve_Finish();
	void ClearOldHotData();

public:
	BfCompiler(BfSystem* bfSystem, bool isResolveOnly);
	~BfCompiler();	

	bool Compile(const StringImpl& outputPath);	
	bool DoCompile(const StringImpl& outputPath);
	void ClearResults();
	void ProcessAutocompleteTempType();	
	void GetSymbolReferences();	
	void Cancel();
	void RequestFastFinish();
	String GetTypeDefList();
	String GetTypeDefMatches(const StringImpl& searchSrc);
	String GetTypeDefInfo(const StringImpl& typeName);	
	int GetEmitSource(const StringImpl& fileName, StringImpl* outBuffer);

	void CompileLog(const char* fmt ...);
	void ReportMemory(MemReporter* memReporter);
};

NS_BF_END

