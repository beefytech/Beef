#pragma warning(disable:4996)
#pragma warning(push)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4146)
#pragma warning(disable:4267)
#pragma warning(disable:4291)

#include "BeefySysLib/util/AllocDebug.h"

#include "llvm/Support/Compiler.h"

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfReducer.h"
#include "BfExprEvaluator.h"
#include "../Backend/BeLibManger.h"
#include <fcntl.h>
#include "BfConstResolver.h"
#include "BfMangler.h"
#include "BfDemangler.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BfSourceClassifier.h"
#include "BfAutoComplete.h"
#include "BfResolvePass.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "../LLVMUtils.h"

#pragma warning(pop)

namespace llvm
{
	extern bool DebugFlag;
}

#define SPLIT_CONTEXTS 

Beefy::BfCompiler* gBfCompiler = NULL;

void pt(llvm::Type* t)
{
	//Beefy::OutputDebugStrF("pv((llvm::Type*)%p)\n", t);

	Beefy::debug_ostream os;
	t->print(os);
	os << "\n";	
	os << " isSized: " << t->isSized() << "\n";
	os.flush();

	if (auto pointerType = llvm::dyn_cast<llvm::PointerType>(t))	
	{
		Beefy::OutputDebugStrF("Element: ");
		pt(pointerType->getElementType());
	}
}

void ppt(llvm::Type* t)
{
	auto pointerType = llvm::dyn_cast<llvm::PointerType>(t);
	if (pointerType == NULL)
	{
		Beefy::OutputDebugStrF("Not a pointer type");
		return;
	}
	pt(pointerType->getElementType());
}

void pt(llvm::DINode* t)
{
	Beefy::debug_ostream os;
	t->print(os);
	os << "\n";	
	os.flush();
}

void pt(llvm::Value* v)
{
	pt(v->getType());
}

void pv(const llvm::Value* v)
{
	Beefy::debug_ostream os;
	v->print(os);
	os << "\n";
	os.flush();
	pt(v->getType());
}

void ppt(llvm::Value* v)
{
	ppt(v->getType());
}

void pmd(llvm::Metadata* md)
{
	Beefy::debug_ostream os;
	md->print(os);
	os << "\n";
	os.flush();
}

void pdl(llvm::DebugLoc& dl)
{
	Beefy::debug_ostream os;
	dl.print(os);
	os << "\n";
	os.flush();
}

void pm(llvm::Module* module)
{
	Beefy::debug_ostream os;
	module->print(os, NULL);
	os << "\n";
	os.flush();
}

void PrintUsers(llvm::MDNode* md)
{
	/*Beefy::debug_ostream os;

	//auto val = llvm::ReplaceableMetadataImpl::get(*md);

	auto val = md->Context.getReplaceableUses();

	if (val == NULL)
	{
		os << "Not replaceable\n";
	}
	else
	{
		//md->print(os);

		typedef std::pair<void *, std::pair<llvm::MetadataTracking::OwnerTy, uint64_t>> UseTy;
		llvm::SmallVector<UseTy, 8> Uses(val->UseMap.begin(), val->UseMap.end());
		std::sort(Uses.begin(), Uses.end(), [](const UseTy &L, const UseTy &R) {
				return L.second.second < R.second.second;
			});
		for (const auto &Pair : Uses)
		{			
			auto Owner = Pair.second.first;
			os << Beefy::StrFormat(" %d %p %d\n", Pair.second.first.isNull(), Pair.first, Pair.second.second, Pair).c_str();
		}

		os << "\n";
	}	
	os.flush();*/
}

void ptbf(Beefy::BfType* bfType)
{
	Beefy::OutputDebugStrF("%s\n", bfType->GetModule()->TypeToString(bfType).c_str());
}

void pt(const Beefy::BfTypedValue& val)
{
	Beefy::OutputDebugStrF("%s\n", val.mType->GetModule()->TypeToString(val.mType).c_str());
}

void pt(llvm::SmallVectorImpl<llvm::Value*>& llvmArgs)
{
	Beefy::debug_ostream os;
	for (int i = 0; i < (int)llvmArgs.size(); i++)
	{
		if (i > 0)
			os << ", ";
		llvmArgs[i]->getType()->print(os);
	}
	os << "\n";
	os.flush();
}

void PrintUsers(llvm::Value* v)
{
	for (auto user : v->users())
	{
		pt(user);
	}
}

/*void PrintFunc(Beefy::BfMethodInstance* methodInstance)
{
	Beefy::debug_ostream os;
	methodInstance->mIRFunction.mLLVMValue->print(os);
	os << "\n";
	os.flush();
}*/

USING_NS_BF;
using namespace llvm;

int Beefy::BfWorkListEntry::sCurReqId = 0;

GlobalVariable* AllocGlobalVariable(Module &M, Type *Ty, bool isConstant,
	GlobalValue::LinkageTypes Linkage, Constant *Initializer,
	const Twine &Name = "", GlobalVariable *InsertBefore = nullptr,
	GlobalValue::ThreadLocalMode tlm = GlobalValue::NotThreadLocal, unsigned AddressSpace = 0,
	bool isExternallyInitialized = false);


#include "BeefySysLib/util/AllocDebug.h"

//////////////////////////////////////////////////////////////////////////

BfCompiler::HotData::~HotData()
{	
	for (auto& kv : mMethodMap)
	{
		auto hotMethod = kv.mValue;
		hotMethod->Clear();
	}
	for (auto& kv : mThisType)
		kv.mValue->Deref();
	for (auto& kv : mAllocation)
		kv.mValue->Deref();
	for (auto& kv : mDevirtualizedMethods)
		kv.mValue->Deref();
	for (auto& kv : mFuncPtrs)
		kv.mValue->Deref();
	for (auto& kv : mVirtualDecls)
		kv.mValue->Deref();
	for (auto& kv : mInnerMethods)
		kv.mValue->Deref();
	for (auto& kv : mMethodMap)		
		kv.mValue->Deref();	
}

template <typename TDict>
static void DeleteUnused(TDict& dict)
{
	auto itr = dict.begin();
	while (itr != dict.end())
	{
		auto val = itr->mValue;
		BF_ASSERT(val->mRefCount >= 1);
		if (val->mRefCount == 1)
		{
			val->Deref();
			itr = dict.Remove(itr);
		}
		else
			++itr;
	}
}

template <typename TDict, typename TElement>
static typename TDict::value_type AllocFromMap(TDict& dict, TElement* elem)
{
 	typename TDict::value_type* valuePtr;
 	if (dict.TryAdd(elem, NULL, &valuePtr))
 	{
 		auto val = new typename std::remove_pointer<typename TDict::value_type>::type(elem); 		
		val->mRefCount++;
 		*valuePtr = val;
 	}
 	return *valuePtr;
}

void BfCompiler::HotData::ClearUnused(bool isHotCompile)
{
	BP_ZONE("BfCompiler::HotData::ClearUnused");

	DeleteUnused(mThisType);
	DeleteUnused(mAllocation);
	DeleteUnused(mDevirtualizedMethods);	
	DeleteUnused(mVirtualDecls);
	DeleteUnused(mInnerMethods);

	if (isHotCompile)
	{
		// We need to keep all function pointer references ever, since we can't tell if we still reference them or not
		DeleteUnused(mFuncPtrs);
	}
}

BfHotThisType* BfCompiler::HotData::GetThisType(BfHotTypeVersion* hotVersion)
{
	return AllocFromMap(mThisType, hotVersion);
}

BfHotAllocation* BfCompiler::HotData::GetAllocation(BfHotTypeVersion* hotVersion)
{
	return AllocFromMap(mAllocation, hotVersion);
}

BfHotDevirtualizedMethod* BfCompiler::HotData::GetDevirtualizedMethod(BfHotMethod* hotMethod)
{
	return AllocFromMap(mDevirtualizedMethods, hotMethod);
}

BfHotFunctionReference* BfCompiler::HotData::GetFunctionReference(BfHotMethod* hotMethod)
{	
	return AllocFromMap(mFuncPtrs, hotMethod);
}

BfHotInnerMethod* BfCompiler::HotData::GetInnerMethod(BfHotMethod* hotMethod)
{
	return AllocFromMap(mInnerMethods, hotMethod);
}

BfHotVirtualDeclaration* BfCompiler::HotData::GetVirtualDeclaration(BfHotMethod* hotMethod)
{
	return AllocFromMap(mVirtualDecls, hotMethod);	
}

BfCompiler::HotState::~HotState()
{
	
}

bool BfCompiler::HotState::HasPendingChanges(BfTypeInstance* type)
{
	return (type->mHotTypeData != NULL) && (type->mHotTypeData->mPendingDataChange);
}

void BfCompiler::HotState::RemovePendingChanges(BfTypeInstance* type)
{
	BF_ASSERT(type->mHotTypeData->mPendingDataChange);
	if (!type->mHotTypeData->mPendingDataChange)
		return;
	type->mHotTypeData->mPendingDataChange = false;	
	bool didRemove = mPendingDataChanges.Remove(type->mTypeId);
	BF_ASSERT(didRemove);
}

BfCompiler::HotResolveData::~HotResolveData()
{
	for (auto hotMethod : mActiveMethods)
		hotMethod->Deref();
	for (auto kv : mReachableMethods)
		kv.mKey->Deref();
}

//////////////////////////////////////////////////////////////////////////

BfCompiler::BfCompiler(BfSystem* bfSystem, bool isResolveOnly)
{
	//llvm::DebugFlag = true;

	memset(&mStats, 0, sizeof(mStats));
	mCompletionPct = 0;	
	mCanceling = false;
	mIsResolveOnly = isResolveOnly;
	mResolvePassData = NULL;
	mPassInstance = NULL;
	mRevision = 0;	
	mLastRevisionAborted = false;
	gBfCompiler = this;
	mSystem = bfSystem;
	mCurTypeId = 1;	
	mTypeInitCount = 0;
	//mMaxInterfaceSlots = 16;
	mMaxInterfaceSlots = -1;
	mInterfaceSlotCountChanged = false;

	mHSPreserveIdx = 0;
	mCompileLogFP = NULL;
	mWantsDeferMethodDecls = false;	
	mHadCancel = false;
	mCompileState = CompileState_None;

	//mMaxInterfaceSlots = 4;	
	mContext = new BfContext(this);	
	mHotData = NULL;
	mHotState = NULL;	
	mHotResolveData = NULL;
		
	mArray1TypeDef = NULL;
	mArray2TypeDef = NULL;
	mArray3TypeDef = NULL;
	mArray4TypeDef = NULL;
	mSpanTypeDef = NULL;
	mAttributeTypeDef = NULL;
	mAttributeUsageAttributeTypeDef = NULL;
	mBfObjectTypeDef = NULL;
	mClassVDataTypeDef = NULL;
	mCLinkAttributeTypeDef = NULL;
	mImportAttributeTypeDef = NULL;
	mCReprAttributeTypeDef = NULL;
	mAlignAttributeTypeDef = NULL;
	mAllowDuplicatesAttributeTypeDef = NULL;
	mNoDiscardAttributeTypeDef = NULL;
	mDisableChecksAttributeTypeDef = NULL;
	mDisableObjectAccessChecksAttributeTypeDef = NULL;
	mDbgRawAllocDataTypeDef = NULL;
	mDeferredCallTypeDef = NULL;
	mDelegateTypeDef = NULL;	
	mActionTypeDef = NULL;
	mEnumTypeDef = NULL;
	mFriendAttributeTypeDef = NULL;
	mCheckedAttributeTypeDef = NULL;
	mUncheckedAttributeTypeDef = NULL;
	mFunctionTypeDef = NULL;
	mGCTypeDef = NULL;
	mGenericIEnumerableTypeDef = NULL;
	mGenericIEnumeratorTypeDef = NULL;
	mGenericIRefEnumeratorTypeDef = NULL;
	mInlineAttributeTypeDef = NULL;
	mInternalTypeDef = NULL;
	mIDisposableTypeDef = NULL;
	mIPrintableTypeDef = NULL;
	mIHashableTypeDef = NULL;
	mLinkNameAttributeTypeDef = NULL;
	mMethodRefTypeDef = NULL;
	mNullableTypeDef = NULL;
	mOrderedAttributeTypeDef = NULL;
	mPointerTTypeDef = NULL;
	mPointerTypeDef = NULL;
	mReflectArrayType = NULL;
	mReflectFieldDataDef = NULL;
	mReflectFieldSplatDataDef = NULL;
	mReflectMethodDataDef = NULL;
	mReflectParamDataDef = NULL;
	mReflectPointerType = NULL;
	mReflectSizedArrayType = NULL;
	mReflectSpecializedGenericType = NULL;
	mReflectTypeInstanceTypeDef = NULL;
	mReflectUnspecializedGenericType = NULL;
	mSizedArrayTypeDef = NULL;
	mStaticInitAfterAttributeTypeDef = NULL;	
	mStaticInitPriorityAttributeTypeDef = NULL;
	mStringTypeDef = NULL;
	mThreadStaticAttributeTypeDef = NULL;
	mTypeTypeDef = NULL;
	mUnboundAttributeTypeDef = NULL;	
	mValueTypeTypeDef = NULL;
	mResultTypeDef = NULL;
	mObsoleteAttributeTypeDef = NULL;
	mErrorAttributeTypeDef = NULL;
	mWarnAttributeTypeDef = NULL;

	mLastAutocompleteModule = NULL;
}

BfCompiler::~BfCompiler()
{		
	delete mContext;
	delete mHotData;
	delete mHotState;	
	delete mHotResolveData;
}

bool BfCompiler::IsTypeAccessible(BfType* checkType, BfProject* curProject)
{
	if (checkType->IsBoxed())
		return IsTypeAccessible(((BfBoxedType*)checkType)->mElementType, curProject);
	
	BfTypeInstance* typeInst = checkType->ToTypeInstance();
	if (typeInst != NULL)
	{
		if (checkType->IsTuple())
		{
			for (auto&& fieldInst : typeInst->mFieldInstances)
			{
				if (!IsTypeAccessible(fieldInst.mResolvedType, curProject))
					return false;
			}
		}

		auto genericTypeInst = typeInst->ToGenericTypeInstance();
		if (genericTypeInst != NULL)
		{
			for (auto genericArg : genericTypeInst->mTypeGenericArguments)
				if (!IsTypeAccessible(genericArg, curProject))
					return false;
		}

		return curProject->ContainsReference(typeInst->mTypeDef->mProject);
	}
	
	if (checkType->IsPointer())
		return IsTypeAccessible(((BfPointerType*)checkType)->mElementType, curProject);
	if (checkType->IsRef())
		return IsTypeAccessible(((BfPointerType*)checkType)->mElementType, curProject);
	
	return true;
}

bool BfCompiler::IsTypeUsed(BfType* checkType, BfProject* curProject)
{	
	if (mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
		return IsTypeAccessible(checkType, curProject);

	BfTypeInstance* typeInst = checkType->ToTypeInstance();
	if (typeInst != NULL)
	{	
		if ((typeInst->mTypeDef->mProject != NULL) && (typeInst->mTypeDef->mProject != curProject))
		{
			if (typeInst->mTypeDef->mProject->mTargetType == BfTargetType_BeefDynLib)
				return false;
		}

		if (checkType->IsInterface())
			return typeInst->mIsReified;
		
		//TODO: We could check to see if this project has any reified specialized instances...
		if (checkType->IsUnspecializedType())
			return typeInst->mIsReified;

		if (checkType->IsTuple())
		{
			for (auto&& fieldInst : typeInst->mFieldInstances)
			{
				if (!IsTypeUsed(fieldInst.mResolvedType, curProject))
					return false;
			}
		}

		auto genericTypeInst = typeInst->ToGenericTypeInstance();
		if (genericTypeInst != NULL)
		{
			for (auto genericArg : genericTypeInst->mTypeGenericArguments)
				if (!IsTypeUsed(genericArg, curProject))
					return false;
		}

		auto module = typeInst->GetModule();
		if (module == NULL)
			return true;

		return curProject->mUsedModules.Contains(module);
	}

	if (checkType->IsPointer())
		return IsTypeUsed(((BfPointerType*)checkType)->mElementType, curProject);
	if (checkType->IsRef())
		return IsTypeUsed(((BfPointerType*)checkType)->mElementType, curProject);

	return true;
}

bool BfCompiler::IsModuleAccessible(BfModule* module, BfProject* curProject)
{
	for (auto checkType : module->mOwnedTypeInstances)
	{
		if (!IsTypeAccessible(checkType, curProject))
			return false;
	}

	return curProject->ContainsReference(module->mProject);
}

void BfCompiler::FixVDataHash(BfModule* bfModule)
{
	// We recreate the actual vdata hash now that we're done creating new string literals
	/*for (auto context : mContexts)
		HASH128_MIXIN(bfModule->mDataHash, bfModule->mHighestUsedStringId);*/
}

void BfCompiler::CheckModuleStringRefs(BfModule* module, BfVDataModule* vdataModule, int lastModuleRevision, HashSet<int>& foundStringIds, HashSet<int>& dllNameSet, Array<BfMethodInstance*>& dllMethods, Array<BfCompiler::StringValueEntry>& stringValueEntries)
{	
	for (int stringId : module->mStringPoolRefs)
	{		
		if (foundStringIds.Add(stringId))
		{			
			BfStringPoolEntry& stringPoolEntry = module->mContext->mStringObjectIdMap[stringId];
			
			if (IsHotCompile())
			{
				if (vdataModule->mDefinedStrings.Contains(stringId))
					continue;				
			}

			StringValueEntry stringEntry;
			stringEntry.mId = stringId;

			vdataModule->mDefinedStrings.Add(stringId);
			stringEntry.mStringVal = vdataModule->CreateStringObjectValue(stringPoolEntry.mString, stringId, true);
			stringValueEntries.Add(stringEntry);

			CompileLog("String %d %s\n", stringId, stringPoolEntry.mString.c_str());
		}
	}

	for (auto dllNameId : module->mImportFileNames)
		dllNameSet.Add(dllNameId);

	for (auto& dllImportEntry : module->mDllImportEntries)
		dllMethods.push_back(dllImportEntry.mMethodInstance);

	auto altModule = module->mNextAltModule;
	while (altModule != NULL)
	{
		CheckModuleStringRefs(altModule, vdataModule, lastModuleRevision, foundStringIds, dllNameSet, dllMethods, stringValueEntries);
		altModule = altModule->mNextAltModule;
	}

	for (auto& specModulePair : module->mSpecializedMethodModules)
		CheckModuleStringRefs(specModulePair.mValue, vdataModule, lastModuleRevision, foundStringIds, dllNameSet, dllMethods, stringValueEntries);
}

void BfCompiler::HashModuleVData(BfModule* module, HashContext& vdataHash)
{
	BP_ZONE("BfCompiler::HashModuleVData");

	if (module->mStringPoolRefs.size() > 0)
	{
		module->mStringPoolRefs.Sort([](int lhs, int rhs) { return lhs < rhs; });
		vdataHash.Mixin(&module->mStringPoolRefs[0], (int)module->mStringPoolRefs.size() * (int)sizeof(int));
	}
	
	if (module->mImportFileNames.size() > 0)
	{
		module->mImportFileNames.Sort([](int lhs, int rhs) { return lhs < rhs; });
		vdataHash.Mixin(&module->mImportFileNames[0], (int)module->mImportFileNames.size() * (int)sizeof(int));
	}
	
	auto altModule = module->mNextAltModule;
	while (altModule != NULL)
	{
		HashModuleVData(altModule, vdataHash);
		altModule = altModule->mNextAltModule;
	}

	for (auto& specModulePair : module->mSpecializedMethodModules)
	{
		HashModuleVData(specModulePair.mValue, vdataHash);
	}
}

BfIRFunction BfCompiler::CreateLoadSharedLibraries(BfVDataModule* bfModule, Array<BfMethodInstance*>& dllMethods)
{
	BfIRType nullPtrType = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_NullPtr));
	BfIRType nullPtrPtrType = bfModule->mBfIRBuilder->MapType(bfModule->CreatePointerType(bfModule->GetPrimitiveType(BfTypeCode_NullPtr)));
	BfIRType voidType = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_None));

	SmallVector<BfIRType, 2> paramTypes;
	auto loadSharedLibrariesFuncType = bfModule->mBfIRBuilder->CreateFunctionType(voidType, paramTypes, false);
	auto loadSharedLibFunc = bfModule->mBfIRBuilder->CreateFunction(loadSharedLibrariesFuncType, BfIRLinkageType_External, "BfLoadSharedLibraries");
	bfModule->SetupIRMethod(NULL, loadSharedLibFunc, false);

	bfModule->mBfIRBuilder->SetActiveFunction(loadSharedLibFunc);
	auto entryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
	bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);

	HashSet<int> dllNameSet;

	auto internalType = bfModule->ResolveTypeDef(mInternalTypeDef);
	bfModule->PopulateType(internalType);
	auto getSharedProcAddressInstance = bfModule->GetMethodByName(internalType->ToTypeInstance(), "GetSharedProcAddressInto");
	auto loadSharedLibraryProc = bfModule->GetMethodByName(internalType->ToTypeInstance(), "LoadSharedLibraryInto");
	BF_ASSERT(getSharedProcAddressInstance);
	BF_ASSERT(loadSharedLibraryProc);

	if (!getSharedProcAddressInstance)
	{
		bfModule->Fail("Missing Internal.GetSharedProcAddressInto");
		return loadSharedLibFunc;
	}

	if (!loadSharedLibraryProc)
	{
		bfModule->Fail("Missing Internal.LoadSharedLibraryInto");
		return loadSharedLibFunc;
	}

	Dictionary<int, BfIRValue> dllHandleMap;
	for (auto methodInstance : dllMethods)
	{
		auto typeInstance = methodInstance->GetOwner();
		auto methodDef = methodInstance->mMethodDef;
		BF_ASSERT(methodInstance->GetCustomAttributes() != NULL);
		for (auto customAttr : methodInstance->GetCustomAttributes()->mAttributes)
		{
			if (customAttr.mType->mTypeDef->mFullName.ToString() == "System.ImportAttribute")
			{
				bool doCLink = false;
				bool undecorated = false;
				BfCallingConvention callingConvention = methodDef->mCallingConvention;
				for (auto fieldSet : customAttr.mSetField)
				{
					BfFieldDef* fieldDef = fieldSet.mFieldRef;

					if (fieldDef->mName == "CLink")
					{
						auto constant = typeInstance->mConstHolder->GetConstant(fieldSet.mParam.mValue);
						if (constant != NULL)
							doCLink = constant->mBool;
					}

					if (fieldDef->mName == "Undecorated")
					{
						auto constant = typeInstance->mConstHolder->GetConstant(fieldSet.mParam.mValue);
						if (constant != NULL)
							undecorated = constant->mBool;
					}

					if (fieldDef->mName == "CallingConvention")
					{
						auto constant = typeInstance->mConstHolder->GetConstant(fieldSet.mParam.mValue);
						if (constant != NULL)
						{
							int callingConventionVal = (int)constant->mInt32;
							if ((callingConventionVal == 3) || (callingConventionVal == 1))
								callingConvention = BfCallingConvention_Stdcall;
							else if (callingConventionVal == 2)
								callingConvention = BfCallingConvention_Cdecl;
						}
					}
				}

				if (customAttr.mCtorArgs.size() == 1)
				{
					auto fileNameArg = customAttr.mCtorArgs[0];
					int strNum = 0;
					auto constant = typeInstance->mConstHolder->GetConstant(fileNameArg);
					if (constant != NULL)
					{
						if (constant->IsNull())
							continue; // Invalid					
						strNum = constant->mInt32;
					}
					else
					{
						strNum = bfModule->GetStringPoolIdx(fileNameArg, typeInstance->mConstHolder);
					}

					BfIRValue dllHandleVar;
					if (!dllHandleMap.TryGetValue(strNum, &dllHandleVar))
					{
						String dllHandleName = StrFormat("bf_hs_preserve@dllHandle%d", strNum);
						dllHandleVar = bfModule->mBfIRBuilder->CreateGlobalVariable(nullPtrType, false, BfIRLinkageType_External,
							bfModule->GetDefaultValue(bfModule->GetPrimitiveType(BfTypeCode_NullPtr)), dllHandleName);

						BfIRValue namePtr = bfModule->GetStringCharPtr(strNum);
						SmallVector<BfIRValue, 1> args;
						args.push_back(namePtr);
						args.push_back(dllHandleVar);
						BfIRValue dllHandleValue = bfModule->mBfIRBuilder->CreateCall(loadSharedLibraryProc.mFunc, args);
						
						dllHandleMap[strNum] = dllHandleVar;						
					}

					String methodImportName;
					if (undecorated)
					{
						methodImportName = methodInstance->mMethodDef->mName;
					}
					else if (doCLink)
					{
						methodImportName = methodInstance->mMethodDef->mName;
						if ((mSystem->mPtrSize == 4) && (callingConvention == BfCallingConvention_Stdcall))
						{
							int argSize = (int)methodDef->mParams.size() * mSystem->mPtrSize;
							methodImportName = StrFormat("_%s$%d", methodImportName.c_str(), argSize);
						}
					}
					else
						BfMangler::Mangle(methodImportName, GetMangleKind(), methodInstance);
					BfIRValue methodNameValue = bfModule->mBfIRBuilder->CreateGlobalStringPtr(methodImportName);

					//auto moduleMethodInstance = bfModule->ReferenceExternalMethodInstance(methodInstance);
					//auto globalVarPtr = bfModule->mBfIRBuilder->CreateBitCast(moduleMethodInstance.mFunc, nullPtrPtrType);

					auto func = bfModule->CreateDllImportGlobalVar(methodInstance, false);
					auto globalVarPtr = bfModule->mBfIRBuilder->CreateBitCast(func, nullPtrPtrType);

					BfSizedVector<BfIRValue, 2> args;
					args.push_back(bfModule->mBfIRBuilder->CreateLoad(dllHandleVar));
					args.push_back(methodNameValue);
					args.push_back(globalVarPtr);
					BfIRValue dllFuncValVoidPtr = bfModule->mBfIRBuilder->CreateCall(getSharedProcAddressInstance.mFunc, args);
				}
			}
		}
	}	

	bfModule->mBfIRBuilder->CreateRetVoid();

	return loadSharedLibFunc;
}

void BfCompiler::GetTestMethods(BfVDataModule* bfModule, Array<TestMethod>& testMethods, HashContext& vdataHashCtx)
{
	vdataHashCtx.Mixin(0xBEEF0001); // Marker

	auto _CheckMethod = [&](BfTypeInstance* typeInstance, BfMethodInstance* methodInstance)
	{
		auto project = typeInstance->mTypeDef->mProject;
		if (project->mTargetType != BfTargetType_BeefTest)
			return;
		if (project != bfModule->mProject)
			return;

		bool isTest = false;
		if ((methodInstance->GetCustomAttributes() != NULL) &&
			(methodInstance->GetCustomAttributes()->Contains(mTestAttributeTypeDef)))
			isTest = true;
		if (!isTest)
			return;

		if (methodInstance->mIsUnspecialized)
		{
			if (!typeInstance->IsSpecializedType())
			{
				bfModule->Fail(StrFormat("Method '%s' cannot be used for testing because it's generic", bfModule->MethodToString(methodInstance).c_str()),
					methodInstance->mMethodDef->GetRefNode());
			}
			bfModule->mHadBuildError = true;
			return;
		}

		if (!methodInstance->mMethodDef->mIsStatic)
		{
			if (!typeInstance->IsSpecializedType())
			{
				bfModule->Fail(StrFormat("Method '%s' cannot be used for testing because it's not static", bfModule->MethodToString(methodInstance).c_str()),
					methodInstance->mMethodDef->GetRefNode());
			}
			bfModule->mHadBuildError = true;
			return;
		}

		if (methodInstance->GetParamCount() > 0)
		{
			if ((methodInstance->GetParamInitializer(0) == NULL) &&
				(methodInstance->GetParamKind(0) != BfParamKind_Params))
			{
				if (!typeInstance->IsSpecializedType())
				{
					bfModule->Fail(StrFormat("Method '%s' cannot be used for testing because it contains parameters without defaults", bfModule->MethodToString(methodInstance).c_str()),
						methodInstance->mMethodDef->GetRefNode());
				}
				bfModule->mHadBuildError = true;
				return;
			}
		}

		BF_ASSERT(typeInstance->IsReified());

		TestMethod testMethod;
		testMethod.mMethodInstance = methodInstance;
		testMethods.Add(testMethod);

		if (!bfModule->mProject->mUsedModules.Contains(typeInstance->mModule))
			bfModule->mProject->mUsedModules.Add(typeInstance->mModule);

		vdataHashCtx.Mixin(methodInstance->GetOwner()->mTypeId);
		vdataHashCtx.Mixin(methodInstance->mMethodDef->mIdx);
	};


	for (auto type : mContext->mResolvedTypes)
	{
		auto typeInstance = type->ToTypeInstance();
		if (typeInstance == NULL)
			continue;

		if (typeInstance->IsUnspecializedType())
			continue;

		for (auto& methodInstanceGroup : typeInstance->mMethodInstanceGroups)
		{
			if (methodInstanceGroup.mDefault != NULL)			
				_CheckMethod(typeInstance, methodInstanceGroup.mDefault);						
		}
	}
}

void BfCompiler::EmitTestMethod(BfVDataModule* bfModule, Array<TestMethod>& testMethods, BfIRValue& retValue)
{
	for (auto& testMethod : testMethods)
	{
		auto methodInstance = testMethod.mMethodInstance;
		auto typeInstance = methodInstance->GetOwner();
		testMethod.mName += bfModule->TypeToString(typeInstance);
		testMethod.mName += ".";
		testMethod.mName += methodInstance->mMethodDef->mName;

		testMethod.mName += "\t";
		auto testAttribute = methodInstance->GetCustomAttributes()->Get(mTestAttributeTypeDef);
		for (auto& field : testAttribute->mSetField)
		{
			auto constant = typeInstance->mConstHolder->GetConstant(field.mParam.mValue);
			if ((constant != NULL) && (constant->mTypeCode == BfTypeCode_Boolean) && (constant->mBool))
			{
				BfFieldDef* fieldDef = field.mFieldRef;
				if (fieldDef->mName == "ShouldFail")
				{
					testMethod.mName += "Sf";
				}
				else if (fieldDef->mName == "Profile")
				{
					testMethod.mName += "Pr";
				}
				else if (fieldDef->mName == "Ignore")
				{
					testMethod.mName += "Ig";
				}
			}
		}

		bfModule->UpdateSrcPos(methodInstance->mMethodDef->GetRefNode(), (BfSrcPosFlags)(BfSrcPosFlag_NoSetDebugLoc | BfSrcPosFlag_Force));
		testMethod.mName += StrFormat("\t%s\t%d\t%d", bfModule->mCurFilePosition.mFileInstance->mParser->mFileName.c_str(), bfModule->mCurFilePosition.mCurLine, bfModule->mCurFilePosition.mCurColumn);
	}

	std::stable_sort(testMethods.begin(), testMethods.end(),
		[](const TestMethod& lhs, const TestMethod& rhs)
	{
		return lhs.mName < rhs.mName;
	});

	String methodData;
	for (int methodIdx = 0; methodIdx < (int)testMethods.size(); methodIdx++)
	{
		String& methodName = testMethods[methodIdx].mName;
		if (!methodData.IsEmpty())
			methodData += "\n";
		methodData += methodName;
	}

	//////////////////////////////////////////////////////////////////////////

	auto testInitMethod = bfModule->GetInternalMethod("Test_Init");
	auto testQueryMethod = bfModule->GetInternalMethod("Test_Query");
	auto testFinishMethod = bfModule->GetInternalMethod("Test_Finish");

	auto char8PtrType = bfModule->CreatePointerType(bfModule->GetPrimitiveType(BfTypeCode_Char8));

	BfIRType strCharType = bfModule->mBfIRBuilder->GetSizedArrayType(bfModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_Char8), (int)methodData.length() + 1);
	BfIRValue strConstant = bfModule->mBfIRBuilder->CreateConstString(methodData);
	BfIRValue gv = bfModule->mBfIRBuilder->CreateGlobalVariable(strCharType,
		true, BfIRLinkageType_External,
		strConstant, "__bfTestData");
	BfIRValue strPtrVal = bfModule->mBfIRBuilder->CreateBitCast(gv, bfModule->mBfIRBuilder->MapType(char8PtrType));

	SizedArray<BfIRValue, 4> irArgs;
	irArgs.Add(strPtrVal);
	bfModule->mBfIRBuilder->CreateCall(testInitMethod.mFunc, irArgs);

	BfIRBlock testHeadBlock = bfModule->mBfIRBuilder->CreateBlock("testHead");
	BfIRBlock testEndBlock = bfModule->mBfIRBuilder->CreateBlock("testEnd");

	bfModule->mBfIRBuilder->CreateBr(testHeadBlock);
	bfModule->mBfIRBuilder->AddBlock(testHeadBlock);
	bfModule->mBfIRBuilder->SetInsertPoint(testHeadBlock);

	irArgs.clear();
	auto testVal = bfModule->mBfIRBuilder->CreateCall(testQueryMethod.mFunc, irArgs);

	auto switchVal = bfModule->mBfIRBuilder->CreateSwitch(testVal, testEndBlock, (int)testMethods.size());

	for (int methodIdx = 0; methodIdx < (int)testMethods.size(); methodIdx++)
	{
		auto methodInstance = testMethods[methodIdx].mMethodInstance;
		String& methodName = testMethods[methodIdx].mName;

		auto testBlock = bfModule->mBfIRBuilder->CreateBlock(StrFormat("test%d", methodIdx));
		bfModule->mBfIRBuilder->AddSwitchCase(switchVal, bfModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, methodIdx), testBlock);

		bfModule->mBfIRBuilder->AddBlock(testBlock);
		bfModule->mBfIRBuilder->SetInsertPoint(testBlock);

		auto moduleMethodInstance = bfModule->ReferenceExternalMethodInstance(methodInstance);
		irArgs.clear();
		if (methodInstance->GetParamCount() > 0)
		{
			if (methodInstance->GetParamKind(0) == BfParamKind_Params)
			{
				auto paramType = methodInstance->GetParamType(0);
				auto paramTypeInst = paramType->ToTypeInstance();
				BfTypedValue paramVal = BfTypedValue(bfModule->mBfIRBuilder->CreateAlloca(bfModule->mBfIRBuilder->MapTypeInst(paramTypeInst)), paramType);
				bfModule->InitTypeInst(paramVal, NULL, false, BfIRValue());

				//TODO: Assert 'length' var is at slot 1
				auto arrayBits = bfModule->mBfIRBuilder->CreateBitCast(paramVal.mValue, bfModule->mBfIRBuilder->MapType(paramTypeInst->mBaseType));
				auto addr = bfModule->mBfIRBuilder->CreateInBoundsGEP(arrayBits, 0, 1);
				auto storeInst = bfModule->mBfIRBuilder->CreateAlignedStore(bfModule->GetConstValue(0), addr, 4);

				irArgs.Add(paramVal.mValue);
			}
			else
			{
				for (int defaultIdx = 0; defaultIdx < (int)methodInstance->mDefaultValues.size(); defaultIdx++)
				{
					irArgs.Add(methodInstance->mDefaultValues[defaultIdx]);
				}
			}
		}

		BfExprEvaluator exprEvaluator(bfModule);
		exprEvaluator.CreateCall(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, irArgs);

		bfModule->mBfIRBuilder->CreateBr(testHeadBlock);
	}

	bfModule->mBfIRBuilder->AddBlock(testEndBlock);
	bfModule->mBfIRBuilder->SetInsertPoint(testEndBlock);

	irArgs.clear();
	bfModule->mBfIRBuilder->CreateCall(testFinishMethod.mFunc, irArgs);

	retValue = bfModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 0);
}

void BfCompiler::CreateVData(BfVDataModule* bfModule)
{
	bool isHotCompile = IsHotCompile();
	if ((isHotCompile) && (bfModule->mProject != mOptions.mHotProject))
		return;	
	
	BP_ZONE("BfCompiler::CreateVData");
	BfLogSysM("CreateVData %s\n", bfModule->mProject->mName.c_str());
	CompileLog("CreateVData %s\n", bfModule->mProject->mName.c_str());
	
	bfModule->mProject->mUsedModules.Add(bfModule);

	auto project = bfModule->mProject;
	auto vdataContext = bfModule->mContext;	
	BF_ASSERT(bfModule->mModuleName == "vdata");	

	//////////////////////////////////////////////////////////////////////////

	// Create types we'll need for vdata, so we won't change the vdata hash afterward
	bfModule->CreatePointerType(bfModule->GetPrimitiveType(BfTypeCode_NullPtr));

	///
	
	auto typeDefType = bfModule->ResolveTypeDef(mTypeTypeDef)->ToTypeInstance();
	if (!typeDefType)
		return;
	BF_ASSERT(typeDefType != NULL);
	vdataContext->mBfTypeType = typeDefType->ToTypeInstance();

	auto typeInstanceDefType = bfModule->ResolveTypeDef(mReflectTypeInstanceTypeDef);
	if (!typeInstanceDefType)
		return;
	auto typeInstanceDefTypeInstance = typeInstanceDefType->ToTypeInstance();

	auto typeDef = mSystem->FindTypeDef("System.ClassVData");
	BF_ASSERT(typeDef != NULL);
	auto bfClassVDataType = bfModule->ResolveTypeDef(typeDef)->ToTypeInstance();	
	vdataContext->mBfClassVDataPtrType = bfModule->CreatePointerType(bfClassVDataType);

	//////////////////////////////////////////////////////////////////////////

	int numEntries = 0;
	int numConcreteTypes = 0;	

	Array<BfType*> orderedTypes;
	for (auto type : mContext->mResolvedTypes)
	{
		numEntries++;		
		BF_ASSERT((type != NULL) || (mPassInstance->HasFailed()));

		if (!type->IsReified())
			continue;

		orderedTypes.Add(type);

		CompileLog("TypeId:%d %s\n", type->mTypeId, bfModule->TypeToString(type).c_str());

		if ((type != NULL) && (type->IsObjectOrInterface()))
		{
			numConcreteTypes++;

			auto typeInst = type->ToTypeInstance();
			if (typeInst->mModule == NULL)
			{
				BF_ASSERT(mPassInstance->HasFailed());
				continue;
			}
		}
	}	

	{
		BP_ZONE("BfCompiler::CreateVData sort orderedTypes");
		orderedTypes.Sort([](BfType* lhs, BfType* rhs)
			{
				return lhs->mTypeId < rhs->mTypeId;
			});
	}

	BfLogSysM("TypeEntries: %d  ConcreteTypes: %d\n", numEntries, numConcreteTypes);
	
	HashContext vdataHashCtx;
	//vdataHashCtx.mDbgViz = true;

	vdataHashCtx.Mixin(bfModule->mProject->mVDataConfigHash);
	
	Array<TestMethod> testMethods;
	if (project->mTargetType == BfTargetType_BeefTest)
		GetTestMethods(bfModule, testMethods, vdataHashCtx);

	Array<BfType*> vdataTypeList;
	std::multimap<String, BfTypeInstance*> sortedStaticInitMap;
	std::multimap<String, BfTypeInstance*> sortedStaticDtorMap;
	std::multimap<String, BfTypeInstance*> sortedStaticMarkMap;
	std::multimap<String, BfTypeInstance*> sortedStaticTLSMap;
	HashSet<BfModule*> usedModuleSet;

	vdataHashCtx.MixinStr(project->mStartupObject);
	vdataHashCtx.Mixin(project->mTargetType);
	
	for (auto type : orderedTypes)
	{
		if (type == NULL)
			continue;

		if (type->IsTemporary())
			continue;

		if ((type->IsGenericParam()) || (type->IsUnspecializedTypeVariation()))
			continue;

		auto typeInst = type->ToTypeInstance();
		if ((typeInst != NULL) && (!typeInst->IsReified()) && (!typeInst->IsUnspecializedType()))
			continue;

		if (!IsTypeUsed(type, project))
			continue;

		vdataTypeList.push_back(type);

		vdataHashCtx.Mixin(type->mTypeId);		

		BF_ASSERT((type != NULL) || (mPassInstance->HasFailed()));
		if ((type != NULL) && (typeInst != NULL))
		{			
			auto module = typeInst->mModule;
			if (module == NULL)
				continue;			
			
			if (type->IsInterface())
				vdataHashCtx.Mixin(typeInst->mSlotNum);

			if (!module->mIsScratchModule)
			{
				BF_ASSERT(module->mIsReified);
				if (usedModuleSet.Add(module))
				{
					CompileLog("UsedModule %p %s\n", module, module->mModuleName.c_str());
					
					HashModuleVData(module, vdataHashCtx);
				}
			}
			
			vdataHashCtx.MixinStr(module->mModuleName);
			vdataHashCtx.Mixin(typeInst->mTypeDef->mSignatureHash);
			vdataHashCtx.Mixin(module->mHasForceLinkMarker);
			
			for (auto iface : typeInst->mInterfaces)
			{
				vdataHashCtx.Mixin(iface.mInterfaceType->mTypeId);
				vdataHashCtx.Mixin(iface.mDeclaringType->mTypeCode);
				vdataHashCtx.Mixin(iface.mDeclaringType->mProject);
			}
						
			if (!typeInst->IsUnspecializedType())
			{
				for (auto& methodInstGroup : typeInst->mMethodInstanceGroups)
				{
					bool isImplementedAndReified = (methodInstGroup.IsImplemented()) && (methodInstGroup.mDefault != NULL) &&
						(methodInstGroup.mDefault->mIsReified) && (!methodInstGroup.mDefault->mIsUnspecialized);
					vdataHashCtx.Mixin(isImplementedAndReified);
				}
			}

			// Could be necessary if a base type in another project adds new virtual methods (for example)

			auto baseType = typeInst->mBaseType;
			while (baseType != NULL)
			{
				vdataHashCtx.Mixin(baseType->mTypeDef->mSignatureHash);				
				baseType = baseType->mBaseType;
			}

			if (module->mProject != bfModule->mProject)
			{
				if ((module->mProject != NULL) && (module->mProject->mTargetType == BfTargetType_BeefDynLib))
					continue;
			}

			if (typeInst->mHasStaticInitMethod)		
				sortedStaticInitMap.insert(std::make_pair(bfModule->TypeToString(type), typeInst));
			else if (typeInst->mHasStaticDtorMethod) // Only store types not already in the static init map
				sortedStaticDtorMap.insert(std::make_pair(bfModule->TypeToString(type), typeInst));

			if ((typeInst->mHasStaticMarkMethod) && (mOptions.mEnableRealtimeLeakCheck))
				sortedStaticMarkMap.insert(std::make_pair(bfModule->TypeToString(type), typeInst));
			if ((typeInst->mHasTLSFindMethod) && (mOptions.mEnableRealtimeLeakCheck))
				sortedStaticTLSMap.insert(std::make_pair(bfModule->TypeToString(type), typeInst));
		}
	}

	int lastModuleRevision = bfModule->mRevision;
	Val128 vdataHash = vdataHashCtx.Finish128();
	bool wantsRebuild = vdataHash != bfModule->mDataHash;	
	if (bfModule->mHadBuildError)
		wantsRebuild = true;
	// If we did one of those 'hot compile' partial vdata builds, now build the whole thing
	if ((!IsHotCompile()) && (bfModule->mHadHotObjectWrites))
		wantsRebuild = true;

	if (mOptions.mHotProject != NULL)	
	{
		HashContext vdataHashCtxEx;
		vdataHashCtxEx.Mixin(mOptions.mHotProject->mName);
	 
		vdataHashCtxEx.Mixin((int)mHotState->mNewlySlottedTypeIds.size());
	 	for (auto typeId : mHotState->mNewlySlottedTypeIds)
			vdataHashCtxEx.Mixin(typeId);
	 
		vdataHashCtxEx.Mixin((int)mHotState->mSlotDefineTypeIds.size());
	 	for (auto typeId : mHotState->mSlotDefineTypeIds)
			vdataHashCtxEx.Mixin(typeId);

		Val128 vdataHashEx = vdataHashCtxEx.Finish128();
		if (mHotState->mVDataHashEx.IsZero())
		{
			if (!mHotState->mNewlySlottedTypeIds.IsEmpty())
				wantsRebuild = true;
			if (!mHotState->mSlotDefineTypeIds.IsEmpty())
				wantsRebuild = true;
		}
		else
		{
			if (vdataHashEx != mHotState->mVDataHashEx)
				wantsRebuild = true;
		}

		mHotState->mVDataHashEx = vdataHashEx;
	}

	if ((wantsRebuild) || (bfModule->mIsModuleMutable))
	{
		bfModule->StartNewRevision();
		if (bfModule->mAwaitingInitFinish)
			bfModule->FinishInit();
	}

	// We add the string hash into vdata hash later
	bfModule->mDataHash = vdataHash;//vdataPreStringHash;

	// This handles "no StartNewRevision" 'else' case, but also handles if vdata failed to complete from a previous compilation
	if (!bfModule->mIsModuleMutable)
	{
		CompileLog("VData unchanged, skipping\n");
		return;
	}
	
	BfTypeInstance* stringType = bfModule->ResolveTypeDef(mStringTypeDef, BfPopulateType_Data)->ToTypeInstance();
	BfTypeInstance* reflectSpecializedTypeInstance = bfModule->ResolveTypeDef(mReflectSpecializedGenericType)->ToTypeInstance();
	BfTypeInstance* reflectUnspecializedTypeInstance = bfModule->ResolveTypeDef(mReflectUnspecializedGenericType)->ToTypeInstance();
	BfTypeInstance* reflectArrayTypeInstance = bfModule->ResolveTypeDef(mReflectArrayType)->ToTypeInstance();

	bool madeBfTypeData = false;
	
	bool needsTypeList = bfModule->IsMethodImplementedAndReified(typeDefType, "GetType");
	bool needsObjectTypeData = needsTypeList || bfModule->IsMethodImplementedAndReified(vdataContext->mBfObjectType, "RawGetType") || bfModule->IsMethodImplementedAndReified(vdataContext->mBfObjectType, "GetType");	
	bool needsTypeNames = bfModule->IsMethodImplementedAndReified(typeDefType, "GetName");
	bool needsStringLiteralList = (mOptions.mAllowHotSwapping) || (bfModule->IsMethodImplementedAndReified(stringType, "Intern"));

	Dictionary<int, int> usedStringIdMap;

	HashSet<BfType*> reflectTypeSet;
	reflectTypeSet.Add(vdataContext->mUnreifiedModule->ResolveTypeDef(mReflectTypeInstanceTypeDef));
	reflectTypeSet.Add(vdataContext->mUnreifiedModule->ResolveTypeDef(mReflectSpecializedGenericType));
	reflectTypeSet.Add(vdataContext->mUnreifiedModule->ResolveTypeDef(mReflectUnspecializedGenericType));
	reflectTypeSet.Add(vdataContext->mUnreifiedModule->ResolveTypeDef(mReflectArrayType));
	
	SmallVector<BfIRValue, 256> typeDataVector;
	for (auto type : vdataTypeList)	
	{
		if (type->IsTypeAlias())
			continue;

		if (type->IsTypeInstance())
			BF_ASSERT(!type->IsIncomplete());

		auto typeInst = type->ToTypeInstance();
		
		if ((typeInst != NULL) && (!typeInst->IsReified()) && (!typeInst->IsUnspecializedType()))
			continue;

		bool needsTypeData = (needsTypeList) || ((type->IsObject()) && (needsObjectTypeData));

		bool forceReflectFields = false;

		if (bfModule->mProject->mReferencedTypeData.Contains(type))
		{
			needsTypeData = true;
			if (type->IsEnum())
				forceReflectFields = true;
		}

		bool needsVData = (type->IsObject()) && (typeInst->mHasBeenInstantiated);
		BfIRValue typeVariable;
		
		if ((needsTypeData) || (needsVData))
		{
			if (reflectTypeSet.Contains(type))
			{
				needsTypeData = true;
				needsVData = true;
			}

			typeVariable = bfModule->CreateTypeData(type, usedStringIdMap, forceReflectFields, needsTypeData, needsTypeNames, needsVData);
		}
		type->mDirty = false;
		
		if (needsTypeList)
		{
			int typeId = type->mTypeId;
			if (typeId == -1)
				continue;
			if (typeId >= (int)typeDataVector.size())
				typeDataVector.resize(typeId + 1);
			typeDataVector[typeId] = typeVariable;
		}
	}

	for (int typeId = 0; typeId < (int)typeDataVector.size(); typeId++)
	{
		if (!typeDataVector[typeId])
			typeDataVector[typeId] = bfModule->GetDefaultValue(typeDefType);
	}

	// We only need 'sTypes' if we actually reference it
	// 
	{
		auto typeDefPtrType = bfModule->CreatePointerType(typeDefType);
		StringT<128> typesVariableName;
		BfMangler::MangleStaticFieldName(typesVariableName, GetMangleKind(), typeDefType->ToTypeInstance(), "sTypes", typeDefPtrType);
		auto arrayType = bfModule->mBfIRBuilder->GetSizedArrayType(bfModule->mBfIRBuilder->MapType(typeDefType), (int)typeDataVector.size());
		auto typeDataConst = bfModule->mBfIRBuilder->CreateConstArray(arrayType, typeDataVector);
		BfIRValue typeDataArray = bfModule->mBfIRBuilder->CreateGlobalVariable(arrayType, true, BfIRLinkageType_External,
			typeDataConst, typesVariableName);
	}
		
	HashSet<int> foundStringIds;
	for (int stringId : bfModule->mStringPoolRefs)
		foundStringIds.Add(stringId);

	Array<BfModule*> orderedUsedModules;
	for (auto module : usedModuleSet)
		orderedUsedModules.push_back(module);
	std::sort(orderedUsedModules.begin(), orderedUsedModules.end(), [] (BfModule* lhs, BfModule* rhs) 
	{ 
		return lhs->mModuleName < rhs->mModuleName;
	});

	Array<BfMethodInstance*> dllMethods;

	Array<BfIRValue> forceLinkValues;

	HashSet<int> dllNameSet;
	Array<BfCompiler::StringValueEntry> stringValueEntries;
	for (auto module : orderedUsedModules)
	{		
		CheckModuleStringRefs(module, bfModule, lastModuleRevision, foundStringIds, dllNameSet, dllMethods, stringValueEntries);

		if ((module->mHasForceLinkMarker) &&
			((!isHotCompile) || (module->mHadHotObjectWrites)))
			forceLinkValues.Add(bfModule->CreateForceLinkMarker(module, NULL));
	}

	if (!forceLinkValues.IsEmpty())
	{
		auto elemType = bfModule->CreatePointerType(bfModule->GetPrimitiveType(BfTypeCode_Int8));
		auto arrayType = bfModule->mBfIRBuilder->GetSizedArrayType(bfModule->mBfIRBuilder->MapType(elemType), (int)forceLinkValues.size());
		auto typeDataConst = bfModule->mBfIRBuilder->CreateConstArray(arrayType, forceLinkValues);
		BfIRValue typeDataArray = bfModule->mBfIRBuilder->CreateGlobalVariable(arrayType, true, BfIRLinkageType_Internal,
			typeDataConst, "FORCELINK_MODULES");
	}
	
	// Generate strings array
	{
		if (!needsStringLiteralList)
		{
			stringValueEntries.Clear();
		}

		std::sort(stringValueEntries.begin(), stringValueEntries.end(),
			[](const StringValueEntry& lhs, const StringValueEntry& rhs)
			{
				return lhs.mId < rhs.mId;
			});
		
		auto stringPtrType = bfModule->CreatePointerType(stringType);
		auto stringPtrIRType = bfModule->mBfIRBuilder->MapTypeInstPtr(stringType);
		
		StringT<128> stringsVariableName;
		BfMangler::MangleStaticFieldName(stringsVariableName, GetMangleKind(), stringType->ToTypeInstance(), "sStringLiterals", stringPtrType);
		Array<BfIRValue> stringList;
		stringList.Add(bfModule->mBfIRBuilder->CreateConstNull(stringPtrIRType));
		for (auto& stringValueEntry : stringValueEntries)
			stringList.Add(stringValueEntry.mStringVal);
		stringList.Add(bfModule->mBfIRBuilder->CreateConstNull(stringPtrIRType));

		BfIRType stringArrayType = bfModule->mBfIRBuilder->GetSizedArrayType(stringPtrIRType, (int)stringList.size());
		auto stringArray = bfModule->mBfIRBuilder->CreateConstArray(stringArrayType, stringList);
		
		auto stringArrayVar = bfModule->mBfIRBuilder->CreateGlobalVariable(stringArrayType, true, BfIRLinkageType_External, stringArray, stringsVariableName);
		
		if (bfModule->mBfIRBuilder->DbgHasInfo())
		{
			auto dbgArrayType = bfModule->mBfIRBuilder->DbgCreateArrayType(stringList.size() * mSystem->mPtrSize * 8, mSystem->mPtrSize * 8, bfModule->mBfIRBuilder->DbgGetType(stringPtrType), (int)stringList.size());
			bfModule->mBfIRBuilder->DbgCreateGlobalVariable(bfModule->mDICompileUnit, stringsVariableName, stringsVariableName, NULL, 0, dbgArrayType, false, stringArrayVar);
		}
	}

	// Generate string ID array
	{		
		auto stringType = bfModule->ResolveTypeDef(mStringTypeDef, BfPopulateType_Data)->ToTypeInstance();
		auto stringPtrType = bfModule->CreatePointerType(stringType);
		auto stringPtrIRType = bfModule->mBfIRBuilder->MapTypeInstPtr(stringType);		

		StringT<128> stringsVariableName;
		BfMangler::MangleStaticFieldName(stringsVariableName, GetMangleKind(), stringType->ToTypeInstance(), "sIdStringLiterals", stringPtrType);
		Array<BfIRValue> stringList;

		stringList.Resize(usedStringIdMap.size());
		for (auto& kv : usedStringIdMap)
		{			
			stringList[kv.mValue] = bfModule->mStringObjectPool[kv.mKey];
		}

		BfIRType stringArrayType = bfModule->mBfIRBuilder->GetSizedArrayType(stringPtrIRType, (int)usedStringIdMap.size());
		auto stringArray = bfModule->mBfIRBuilder->CreateConstArray(stringArrayType, stringList);

		auto stringArrayVar = bfModule->mBfIRBuilder->CreateGlobalVariable(stringArrayType, true, BfIRLinkageType_External, stringArray, stringsVariableName);

		if (bfModule->mBfIRBuilder->DbgHasInfo())
		{
			auto dbgArrayType = bfModule->mBfIRBuilder->DbgCreateArrayType(stringList.size() * mSystem->mPtrSize * 8, mSystem->mPtrSize * 8, bfModule->mBfIRBuilder->DbgGetType(stringPtrType), (int)stringList.size());
			bfModule->mBfIRBuilder->DbgCreateGlobalVariable(bfModule->mDICompileUnit, stringsVariableName, stringsVariableName, NULL, 0, dbgArrayType, false, stringArrayVar);
		}
	}

	BfIRFunction loadSharedLibFunc = CreateLoadSharedLibraries(bfModule, dllMethods);
	
	BfIRType nullPtrType = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_NullPtr));
	BfIRType voidType = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_None));
	BfIRType int32Type = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_Int32));

	struct _StaticInitEntry
	{
		int mPriority;
		BfTypeInstance* mTypeInstance;
	};
	Array<_StaticInitEntry> staticInitList;

	// Populate staticInitList
	{
		Dictionary<int, BfTypeInstance*> pendingIDToInstanceMap;
		HashSet<BfTypeInstance*> handledTypes;
		BfType* staticInitPriorityAttributeType = vdataContext->mUnreifiedModule->ResolveTypeDef(mStaticInitPriorityAttributeTypeDef);
		BfType* staticInitAfterAttributeType = vdataContext->mUnreifiedModule->ResolveTypeDef(mStaticInitAfterAttributeTypeDef);
		bool forceAdd = false;
		for (int pass = 0; true; pass++)
		{
			bool hadAdd = false;
			for (auto& mapEntry : sortedStaticInitMap)
			{
				auto typeInst = mapEntry.second;
				if ((typeInst != NULL) && (!typeInst->IsUnspecializedType()) && (typeInst->mHasStaticInitMethod))
				{
					if (pass == 0)
					{
						int priority = 0;

						bool hadInitAfterAttribute = false;
						if (typeInst->mCustomAttributes != NULL)
						{
							for (auto& customAttr : typeInst->mCustomAttributes->mAttributes)
							{
								if (customAttr.mType == staticInitAfterAttributeType)
									hadInitAfterAttribute = true;
								if (customAttr.mType == staticInitPriorityAttributeType)
								{
									if (customAttr.mCtorArgs.size() == 1)
									{
										auto constant = typeInst->mConstHolder->GetConstant(customAttr.mCtorArgs[0]);
										if (constant != NULL)
											priority = constant->mInt32;
									}
								}
							}
						}

						if (!hadInitAfterAttribute)
						{
							staticInitList.push_back({ priority, typeInst });
							mapEntry.second = NULL;
						}
						else
						{
							pendingIDToInstanceMap.TryAdd(typeInst->mTypeId, typeInst);
						}
					}
					else
					{
						if (pendingIDToInstanceMap.ContainsKey(typeInst->mTypeId))
						{
							bool doAdd = true;
							if (!forceAdd)
							{
								for (auto& customAttr : typeInst->mCustomAttributes->mAttributes)
								{
									if (customAttr.mType == staticInitAfterAttributeType)
									{
										if (customAttr.mCtorArgs.size() == 0)
										{
											doAdd = false;
										}
										else
										{
											auto ctorArg = customAttr.mCtorArgs[0];
											auto constant = typeInst->mConstHolder->GetConstant(ctorArg);
											if (constant != NULL)
											{
												int refTypeId = constant->mInt32;
												if (pendingIDToInstanceMap.ContainsKey(refTypeId))
													doAdd = false;
											}
										}
									}
								}
							}

							if (doAdd)
							{
								staticInitList.push_back({ 0, typeInst });
								pendingIDToInstanceMap.Remove(typeInst->mTypeId);
								hadAdd = true;
							}
						}
					}
				}
			}

			if (pass == 0)
			{
				std::sort(staticInitList.begin(), staticInitList.end(),
					[](const _StaticInitEntry& lhs, const _StaticInitEntry& rhs)
				{
					return lhs.mPriority > rhs.mPriority;
				});
			}

			if ((pass > 0) && (!hadAdd) && (pendingIDToInstanceMap.size() > 0)) // Circular ref?
				forceAdd = true;

			if (pendingIDToInstanceMap.size() == 0)
				break;
		}
	}

	// We want to call DTORS in reverse order from CTORS
	Array<BfTypeInstance*> dtorList;

	for (intptr idx = staticInitList.size() - 1; idx >= 0; idx--)
	{
		auto typeInst = staticInitList[idx].mTypeInstance;
		if (typeInst->mHasStaticDtorMethod)
		{
			dtorList.push_back(typeInst);
		}
	}

	for (auto itr = sortedStaticDtorMap.rbegin(); itr != sortedStaticDtorMap.rend(); itr++)
	{
		auto typeInst = itr->second;
		dtorList.push_back(typeInst);
	}

	///	Generate "BfCallAllStaticDtors"
	BfIRFunction dtorFunc;
	{
		SmallVector<BfIRType, 2> paramTypes;
		auto dtorFuncType = bfModule->mBfIRBuilder->CreateFunctionType(voidType, paramTypes, false);
		dtorFunc = bfModule->mBfIRBuilder->CreateFunction(dtorFuncType, BfIRLinkageType_External, "BfCallAllStaticDtors");
		bfModule->SetupIRMethod(NULL, dtorFunc, false);
		bfModule->mBfIRBuilder->SetActiveFunction(dtorFunc);
		auto entryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
		bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);

		for (auto typeInst : dtorList)
		{
			for (auto& methodGroup : typeInst->mMethodInstanceGroups)
			{
				auto methodInstance = methodGroup.mDefault;
				if ((methodInstance != NULL) &&
					(methodInstance->mMethodDef->mIsStatic) &&
					(methodInstance->mMethodDef->mMethodType == BfMethodType_Dtor) &&
					((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
				{
					if (!typeInst->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, bfModule->mProject))
						continue;
					if (methodInstance->mHotMethod != NULL)
						methodInstance->mHotMethod->mFlags = (BfHotDepDataFlags)(methodInstance->mHotMethod->mFlags | BfHotDepDataFlag_AlwaysCalled);
					auto methodModule = bfModule->GetMethodInstanceAtIdx(typeInst, methodInstance->mMethodDef->mIdx);
					bfModule->mBfIRBuilder->CreateCall(methodModule.mFunc, SmallVector<BfIRValue, 0>());
				}
			}
		}
		bfModule->mBfIRBuilder->CreateRetVoid();
	}

	// Generate "main"
	if (!IsHotCompile())
	{
		BfIRFunctionType mainFuncType;
		BfIRFunction mainFunc;
		if ((project->mTargetType == BfTargetType_BeefConsoleApplication) || (project->mTargetType == BfTargetType_BeefTest))
		{
			SmallVector<BfIRType, 2> paramTypes;
			paramTypes.push_back(int32Type);
			paramTypes.push_back(nullPtrType);
			mainFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);
            mainFunc = bfModule->mBfIRBuilder->CreateFunction(mainFuncType, BfIRLinkageType_External, "main");			
			bfModule->SetupIRMethod(NULL, mainFunc, false);
		}
		else if (project->mTargetType == BfTargetType_BeefDynLib)
		{		
			SmallVector<BfIRType, 4> paramTypes;
			paramTypes.push_back(nullPtrType); // hinstDLL			
			paramTypes.push_back(int32Type); // fdwReason
			paramTypes.push_back(nullPtrType); // lpvReserved			
			mainFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);
			mainFunc = bfModule->mBfIRBuilder->CreateFunction(mainFuncType, BfIRLinkageType_External, "DllMain");
			if (mOptions.mMachineType == BfMachineType_x86)
				bfModule->mBfIRBuilder->SetFuncCallingConv(mainFunc, BfIRCallingConv_StdCall);
			bfModule->SetupIRMethod(NULL, mainFunc, false);
		}
		else if (project->mTargetType == BfTargetType_BeefWindowsApplication)
		{				
			SmallVector<BfIRType, 4> paramTypes;
			paramTypes.push_back(nullPtrType); // hInstance
			paramTypes.push_back(nullPtrType); // hPrevInstance
			paramTypes.push_back(nullPtrType); // lpCmdLine
			paramTypes.push_back(int32Type); // nCmdShow
			mainFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);
			mainFunc = bfModule->mBfIRBuilder->CreateFunction(mainFuncType, BfIRLinkageType_External, "WinMain");
			if (mOptions.mMachineType == BfMachineType_x86)
				bfModule->mBfIRBuilder->SetFuncCallingConv(mainFunc, BfIRCallingConv_StdCall);			
			bfModule->SetupIRMethod(NULL, mainFunc, false);
		}		
		else
		{
			SmallVector<BfIRType, 2> paramTypes;
			paramTypes.push_back(int32Type);
			paramTypes.push_back(nullPtrType);
			mainFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);
			mainFunc = bfModule->mBfIRBuilder->CreateFunction(mainFuncType, BfIRLinkageType_External, "BeefMain");
			bfModule->SetupIRMethod(NULL, mainFunc, false);
		}

		bfModule->mBfIRBuilder->SetActiveFunction(mainFunc);
		auto entryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
		bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);
		
#ifndef BF_PLATFORM_WINDOWS
        {
            SmallVector<BfIRType, 2> paramTypes;
            paramTypes.push_back(int32Type);
            paramTypes.push_back(nullPtrType);
            auto setCmdLineFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);

            auto setCmdLineFunc = bfModule->mBfIRBuilder->CreateFunction(setCmdLineFuncType, BfIRLinkageType_External, "BfpSystem_SetCommandLine");
			bfModule->SetupIRMethod(NULL, setCmdLineFunc, false);

            SmallVector<BfIRValue, 2> args;
            args.push_back(bfModule->mBfIRBuilder->GetArgument(0));
            args.push_back(bfModule->mBfIRBuilder->GetArgument(1));
            bfModule->mBfIRBuilder->CreateCall(setCmdLineFunc, args);
        }
#endif
		
		BfIRBlock initSkipBlock;
		if (project->mTargetType == BfTargetType_BeefDynLib)
		{
			auto initBlock = bfModule->mBfIRBuilder->CreateBlock("doInit", false);
			initSkipBlock = bfModule->mBfIRBuilder->CreateBlock("skipInit", false);
			auto cmpResult = bfModule->mBfIRBuilder->CreateCmpEQ(bfModule->mBfIRBuilder->GetArgument(1), bfModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 1));
			bfModule->mBfIRBuilder->CreateCondBr(cmpResult, initBlock, initSkipBlock);
			bfModule->mBfIRBuilder->AddBlock(initBlock);
			bfModule->mBfIRBuilder->SetInsertPoint(initBlock);

			auto moduleMethodInstance = bfModule->GetInternalMethod("SetModuleHandle", 1);
			if (moduleMethodInstance)
			{
				SmallVector<BfIRValue, 1> args;
				args.push_back(bfModule->mBfIRBuilder->GetArgument(0));
				bfModule->mBfIRBuilder->CreateCall(moduleMethodInstance.mFunc, args);
			}
		}

		// Do the LoadLibrary calls below priority 100
		bool didSharedLibLoad = false;
		auto _CheckSharedLibLoad = [&]()
		{
			if (!didSharedLibLoad)
			{
				bfModule->mBfIRBuilder->CreateCall(loadSharedLibFunc, SmallVector<BfIRValue, 0>());
				didSharedLibLoad = true;
			}
		};

		for (auto& staticInitEntry : staticInitList)
		{
			if (staticInitEntry.mPriority < 100)
				_CheckSharedLibLoad();

			auto typeInst = staticInitEntry.mTypeInstance;
			for (auto& methodGroup : typeInst->mMethodInstanceGroups)
			{				
				auto methodInstance = methodGroup.mDefault;
				if ((methodInstance != NULL) &&
					(methodInstance->mMethodDef->mIsStatic) &&
					(methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor) &&
					((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
				{
					if (!typeInst->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, bfModule->mProject))
						continue;

					auto methodModule = bfModule->GetMethodInstanceAtIdx(typeInst, methodInstance->mMethodDef->mIdx);
					if (methodInstance->mHotMethod != NULL)
						methodInstance->mHotMethod->mFlags = (BfHotDepDataFlags)(methodInstance->mHotMethod->mFlags | BfHotDepDataFlag_AlwaysCalled);
					bfModule->mBfIRBuilder->CreateCall(methodModule.mFunc, SmallVector<BfIRValue, 0>());
				}
			}
		}

		_CheckSharedLibLoad();

		if (initSkipBlock)
		{
			bfModule->mBfIRBuilder->CreateBr(initSkipBlock);
			bfModule->mBfIRBuilder->AddBlock(initSkipBlock);
			bfModule->mBfIRBuilder->SetInsertPoint(initSkipBlock);
		}

		BfIRValue retValue;
		if ((project->mTargetType == BfTargetType_BeefConsoleApplication) || (project->mTargetType == BfTargetType_BeefWindowsApplication) ||
			(project->mTargetType == BfTargetType_BeefApplication_StaticLib) || (project->mTargetType == BfTargetType_BeefApplication_DynamicLib))
		{
			bool hadRet = false;

			String entryClassName = project->mStartupObject;
			typeDef = mSystem->FindTypeDef(entryClassName, 0, bfModule->mProject);
			if (typeDef != NULL)
			{
				auto type = bfModule->ResolveTypeDef(typeDef);
				BF_ASSERT((type != NULL) || (mPassInstance->HasFailed()));
				if (type != NULL)
				{
					BfType* stringType = vdataContext->mUnreifiedModule->ResolveTypeDef(mStringTypeDef);					

					BfType* int32Type = bfModule->GetPrimitiveType(BfTypeCode_Int32);
					BfType* intType = bfModule->GetPrimitiveType(BfTypeCode_IntPtr);
					BfType* voidType = bfModule->GetPrimitiveType(BfTypeCode_None);

					bool hadValidMainMethod = false;
					BfModuleMethodInstance moduleMethodInst;
					for (auto methodDef : typeDef->mMethods)
					{
						if (methodDef->mName == "Main")
						{
							hadValidMainMethod = true;
							moduleMethodInst = bfModule->GetMethodInstanceAtIdx(type->ToTypeInstance(), methodDef->mIdx);

							if (!methodDef->mIsStatic)
							{
								mPassInstance->Fail("Main method must be static", methodDef->GetRefNode());
								hadValidMainMethod = false;
							}
							
							if ((moduleMethodInst.mMethodInstance->mReturnType != int32Type) &&
								(moduleMethodInst.mMethodInstance->mReturnType != intType) &&
								(moduleMethodInst.mMethodInstance->mReturnType != voidType))
							{
								mPassInstance->Fail("Main method must return void, int, or int32", methodDef->GetRefNode());
								hadValidMainMethod = false;
							}
							
							if (moduleMethodInst.mMethodInstance->GetParamCount() == 0)
							{
								// No params
							}
							else
							{
								auto paramType = moduleMethodInst.mMethodInstance->GetParamType(0);
								if ((moduleMethodInst.mMethodInstance->GetParamCount() != 1) || (!paramType->IsArray()) || (paramType->GetUnderlyingType() != stringType))
								{
									mPassInstance->Fail("Main method must be declared with either no parameters or a single String[] parameter", methodDef->GetRefNode());
									hadValidMainMethod = false;
								}
							}
						}
					}
					
					if (moduleMethodInst)
					{
						if (hadValidMainMethod)
						{
							bool hasArgs = moduleMethodInst.mMethodInstance->GetParamCount() != 0;
														
							BfIRType intType = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_IntPtr));
							BfIRType int32Type = bfModule->mBfIRBuilder->MapType(bfModule->GetPrimitiveType(BfTypeCode_Int32));

							// Create BeefEntry thunk
							SmallVector<BfIRType, 1> paramTypes;							
							if (hasArgs)
							{
								paramTypes.push_back(bfModule->mBfIRBuilder->MapType(moduleMethodInst.mMethodInstance->GetParamType(0)));
							}
							BfIRFunctionType thunkFuncType = bfModule->mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, false);
							
							BfIRFunction thunkMainFunc = bfModule->mBfIRBuilder->CreateFunction(thunkFuncType, BfIRLinkageType_External, "BeefStartProgram");
							bfModule->SetupIRMethod(NULL, thunkMainFunc, false);
							bfModule->mBfIRBuilder->SetActiveFunction(thunkMainFunc);

							auto thunkEntryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
							bfModule->mBfIRBuilder->SetInsertPoint(thunkEntryBlock);
							SmallVector<BfIRValue, 1> args;
							if (hasArgs)
								args.push_back(bfModule->mBfIRBuilder->GetArgument(0));
							auto methodInstance = moduleMethodInst.mMethodInstance;
							if (methodInstance->mHotMethod != NULL)
								methodInstance->mHotMethod->mFlags = (BfHotDepDataFlags)(methodInstance->mHotMethod->mFlags | BfHotDepDataFlag_AlwaysCalled);
							auto retVal = bfModule->mBfIRBuilder->CreateCall(moduleMethodInst.mFunc, args);

							if (moduleMethodInst.mMethodInstance->mReturnType->IsVoid())
							{
								bfModule->mBfIRBuilder->CreateRet(bfModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 0));
							}
							else
							{
								retVal = bfModule->mBfIRBuilder->CreateNumericCast(retVal, true, BfTypeCode_Int32);
								bfModule->mBfIRBuilder->CreateRet(retVal);
							}

							hadRet = true;

							auto internalType = bfModule->ResolveTypeDef(mInternalTypeDef);							
														
							args.clear();

							// Call BeefEntry thunk
							bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);
							if (hasArgs)
							{
								auto createParamsMethodInstance = bfModule->GetMethodByName(internalType->ToTypeInstance(), "CreateParamsArray");
								auto callValue = bfModule->mBfIRBuilder->CreateCall(createParamsMethodInstance.mFunc, SmallVector<BfIRValue, 0>());								
								args.push_back(callValue);
							}

							retValue = bfModule->mBfIRBuilder->CreateCall(thunkMainFunc, args);							

							if (hasArgs)
							{
								auto deleteStringArrayMethodInstance = bfModule->GetMethodByName(internalType->ToTypeInstance(), "DeleteStringArray");
								bfModule->mBfIRBuilder->CreateCall(deleteStringArrayMethodInstance.mFunc, args);
							}
						}
					}
					else
					{
						mPassInstance->Fail(StrFormat("Unable to find Main method in class '%s'", entryClassName.c_str()));
					}
				}
			}
			else
			{
				if (entryClassName.empty())
					mPassInstance->Fail(StrFormat("No entry point class specified for executable in project '%s'", project->mName.c_str()));
				else
					mPassInstance->Fail(StrFormat("Unable to find entry point class '%s' in project '%s'", entryClassName.c_str(), project->mName.c_str()));
				bfModule->mHadBuildError = true;
			}

			if (!hadRet)
				retValue = bfModule->GetConstValue32(0);
		}	
		else if (project->mTargetType == BfTargetType_BeefDynLib)
		{
			retValue = bfModule->GetConstValue32(1);
		}

		if (project->mTargetType == BfTargetType_BeefTest)
			EmitTestMethod(bfModule, testMethods, retValue);

		BfIRBlock deinitSkipBlock;
		if (project->mTargetType == BfTargetType_BeefDynLib)
		{			
			auto deinitBlock = bfModule->mBfIRBuilder->CreateBlock("doDeinit", false);
			deinitSkipBlock = bfModule->mBfIRBuilder->CreateBlock("skipDeinit", false);
			auto cmpResult = bfModule->mBfIRBuilder->CreateCmpEQ(bfModule->mBfIRBuilder->GetArgument(1), bfModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 0));
			bfModule->mBfIRBuilder->CreateCondBr(cmpResult, deinitBlock, deinitSkipBlock);
			bfModule->mBfIRBuilder->AddBlock(deinitBlock);
			bfModule->mBfIRBuilder->SetInsertPoint(deinitBlock);
		}

		bfModule->mBfIRBuilder->CreateCall(dtorFunc, SizedArray<BfIRValue, 0>());
		
		BfModuleMethodInstance shutdownMethod = bfModule->GetInternalMethod("Shutdown");
		if (shutdownMethod)
		{
			bfModule->mBfIRBuilder->CreateCall(shutdownMethod.mFunc, SizedArray<BfIRValue, 0>());
		}

		if (deinitSkipBlock)
		{
			bfModule->mBfIRBuilder->CreateBr(deinitSkipBlock);
			bfModule->mBfIRBuilder->AddBlock(deinitSkipBlock);
			bfModule->mBfIRBuilder->SetInsertPoint(deinitSkipBlock);
		}

		if (retValue)
			bfModule->mBfIRBuilder->CreateRet(retValue);
		else
			bfModule->mBfIRBuilder->CreateRetVoid();

		if ((mOptions.mAllowHotSwapping) && (bfModule->mHasFullDebugInfo))
		{
			auto int8Type = bfModule->GetPrimitiveType(BfTypeCode_Int8);
			int dataSize = 16*1024;
			auto irArrType = bfModule->mBfIRBuilder->GetSizedArrayType(bfModule->mBfIRBuilder->MapType(int8Type), dataSize);
			String name = "__BFTLS_EXTRA";
			auto irVal = bfModule->mBfIRBuilder->CreateGlobalVariable(irArrType, false, BfIRLinkageType_External, bfModule->mBfIRBuilder->CreateConstStructZero(irArrType), name, true);
			BfIRMDNode dbgArrayType = bfModule->mBfIRBuilder->DbgCreateArrayType(dataSize * 8, 8, bfModule->mBfIRBuilder->DbgGetType(int8Type), dataSize);
			bfModule->mBfIRBuilder->DbgCreateGlobalVariable(bfModule->mDICompileUnit, name, name, NULL, 0, dbgArrayType, false, irVal);
		}
	}
	
	// Generate "System.GC.MarkAllStaticMembers"	
	auto gcType = vdataContext->mUnreifiedModule->ResolveTypeDef(mGCTypeDef);
	if (bfModule->IsMethodImplementedAndReified(gcType->ToTypeInstance(), "MarkAllStaticMembers"))
	{				
		bfModule->PopulateType(gcType);
		auto moduleMethodInstance = bfModule->GetMethodByName(gcType->ToTypeInstance(), "MarkAllStaticMembers");
		bfModule->mBfIRBuilder->SetActiveFunction(moduleMethodInstance.mFunc);
		if (!moduleMethodInstance)
		{
			bfModule->Fail("Internal error: System.GC doesn't contain MarkAllStaticMembers method");
		}
		else
		{
			auto entryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
			bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);
			for (auto& mapEntry : sortedStaticMarkMap)
			{
				auto typeInst = mapEntry.second;
				if (typeInst->IsUnspecializedType())
					continue;
				
				for (auto& methodGroup : typeInst->mMethodInstanceGroups)
				{
					auto methodInstance = methodGroup.mDefault;
					if ((methodInstance != NULL) &&
						(methodInstance->mMethodDef->mIsStatic) &&
						(methodInstance->mMethodDef->mMethodType == BfMethodType_Normal) &&
						(methodInstance->mMethodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC) &&
						((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
					{
						if (!typeInst->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, bfModule->mProject))
							continue;
						auto methodModule = bfModule->GetMethodInstanceAtIdx(typeInst, methodInstance->mMethodDef->mIdx);
						if (methodInstance->mHotMethod != NULL)
							methodInstance->mHotMethod->mFlags = (BfHotDepDataFlags)(methodInstance->mHotMethod->mFlags | BfHotDepDataFlag_AlwaysCalled);
						bfModule->mBfIRBuilder->CreateCall(methodModule.mFunc, SmallVector<BfIRValue, 0>());
					}
				}
			}
			bfModule->mBfIRBuilder->CreateRetVoid();
		}
	}

	// Generate "System.GC.FindAllTLSMembers"
	if (bfModule->IsMethodImplementedAndReified(gcType->ToTypeInstance(), "FindAllTLSMembers"))
	{
		bfModule->PopulateType(gcType);
		auto moduleMethodInstance = bfModule->GetMethodByName(gcType->ToTypeInstance(), "FindAllTLSMembers");
		bfModule->mBfIRBuilder->SetActiveFunction(moduleMethodInstance.mFunc);
		if (!moduleMethodInstance)
		{
			bfModule->Fail("Internal error: System.GC doesn't contain FindAllTLSMembers method");
		}
		else
		{
			auto entryBlock = bfModule->mBfIRBuilder->CreateBlock("entry", true);
			bfModule->mBfIRBuilder->SetInsertPoint(entryBlock);
			for (auto& mapEntry : sortedStaticTLSMap)
			{
				auto typeInst = mapEntry.second;
				if (typeInst->IsUnspecializedType())
					continue;

				for (auto& methodGroup : typeInst->mMethodInstanceGroups)
				{
					auto methodInstance = methodGroup.mDefault;
					if ((methodInstance != NULL) &&
						(methodInstance->mMethodDef->mIsStatic) &&
						(methodInstance->mMethodDef->mMethodType == BfMethodType_Normal) &&
						(methodInstance->mMethodDef->mName == BF_METHODNAME_FIND_TLS_MEMBERS) &&
						((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
					{
						if (!typeInst->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, bfModule->mProject))
							continue;
						auto methodModule = bfModule->GetMethodInstanceAtIdx(typeInst, methodInstance->mMethodDef->mIdx);
						bfModule->mBfIRBuilder->CreateCall(methodModule.mFunc, SmallVector<BfIRValue, 0>());
					}
				}
			}
			bfModule->mBfIRBuilder->CreateRetVoid();
		}
	}	

	if (bfModule->mHadBuildError)
	{
		bfModule->mDataHash = 0;
	}
}

// This method clears out unused generic types AFTER compilation of reified types has occurred
void BfCompiler::UpdateDependencyMap(bool deleteUnusued, bool& didWork)
{
	BP_ZONE("BfCompiler::UpdateDependencyMap");	
	BfLogSysM("Compiler::UpdateDependencyMap %d\n", deleteUnusued);

	bool madeFullPass = true;
	if (mCanceling)
		madeFullPass = false;
	if ((mResolvePassData != NULL) && (mResolvePassData->mParser != NULL))
		madeFullPass = false;
	
	// Remove old data in dependency maps, and find types which don't have any references (direct or indirect)
	//  to a non-generic type and remove them
	for (int pass = 0; true; pass++)
	{		
		// This assert can fail if we have a dependency error, where deleting a type causes a dependent type
		//  to be rebuilt
		BF_ASSERT(pass < 100);

		bool foundNew = false;		
		
		for (auto type : mContext->mResolvedTypes)
		{
			if (type != NULL)
			{				
				auto depType = type->ToDependedType();					
				auto typeInst = type->ToTypeInstance();
			
				if (depType != NULL)
				{
					extern BfModule* gLastCreatedModule;
						
					for (auto itr = depType->mDependencyMap.begin(); itr != depType->mDependencyMap.end(); ++itr)
					{
						auto dependentType = itr->mKey;							
							
						if (dependentType->IsIncomplete())
						{								
							BF_ASSERT(dependentType->IsDeleting() || dependentType->IsOnDemand() || !dependentType->HasBeenReferenced() || !madeFullPass || dependentType->IsSpecializedByAutoCompleteMethod());
						}
					}

					// Not combined with previous loop because PopulateType could modify typeInst->mDependencyMap
					for (auto itr = depType->mDependencyMap.begin(); itr != depType->mDependencyMap.end();)
					{
						auto dependentType = itr->mKey;
						auto depTypeInst = dependentType->ToTypeInstance();
						auto& depData = itr->mValue;
							
						bool isInvalidVersion = (dependentType->mRevision > depData.mRevision) && (deleteUnusued) && (madeFullPass);
							
						//TODO: Just to cause crash if dependentType is deleted
						bool isIncomplete = dependentType->IsIncomplete();

						if ((isInvalidVersion) && (!dependentType->IsDeleting()))
						{
							if (!dependentType->HasBeenReferenced())
							{										
								BfLogSysM("Skipping remove of old dependent %p from %p\n", dependentType, typeInst);
								//BF_ASSERT(dependentType->IsGenericTypeInstance());
								// We have a pending type rebuild but we're not sure whether we're being deleted or not yet...
								++itr;
								continue;
							}
						}

						if ((dependentType->IsDeleting()) || (isInvalidVersion))
						{							
							// If we're deleting the type, OR the dependency of the type has been removed.
							//  We detect a removed dependency by the dependent type changing but the dependency revision
							//  is older than the dependent type.
							BfLogSysM("Removing old dependent %p from %p\n", dependentType, depType);
  							itr = depType->mDependencyMap.erase(itr);
						}
						else
						{
							// There needs to be more usage than just being used as part of the method specialization's MethodGenericArg.
							//  Keep in mind that actually invoking a generic method creates a DependencyFlag_LocalUsage dependency.  The 
							//  DependencyFlag_MethodGenericArg is just used by the owner during creation of the method specialization
							bool isDependentUsage =
                                (depData.mFlags != BfDependencyMap::DependencyFlag_UnspecializedType) &&
                                (depData.mFlags != BfDependencyMap::DependencyFlag_MethodGenericArg);
								
							// We need to consider specialized generic types separately, to remove unused specializations
							if (typeInst != NULL)
							{
								if ((depTypeInst != NULL) && (typeInst->mLastNonGenericUsedRevision != mRevision) && (isDependentUsage) &&
									((!dependentType->IsGenericTypeInstance()) || (dependentType->IsUnspecializedType()) || (depTypeInst->mLastNonGenericUsedRevision == mRevision)))
								{										
									typeInst->mLastNonGenericUsedRevision = mRevision;
									foundNew = true;
										
									if (!typeInst->HasBeenReferenced())
										mContext->AddTypeToWorkList(typeInst);
								}
							}
								
							++itr;
						}
					}

					if ((!depType->IsGenericTypeInstance() && (!depType->IsBoxed())) || 
						(depType->IsUnspecializedType()) ||
						((typeInst != NULL) && (typeInst->mLastNonGenericUsedRevision == mRevision)))							
					{
						if ((depType->mRebuildFlags & BfTypeRebuildFlag_AwaitingReference) != 0)
						{							
							mContext->MarkAsReferenced(depType);
						}
					}
				}
			}
		}

		if (mCanceling)
			madeFullPass = false;					

		if (!madeFullPass)
		{
			// We can't delete types based on the dependency map when we're canceling, because we may still
			//  have items in the work queues (particularly the mMethodWorkList) that will create
			//  new dependencies -- things may unduly be thought to be deleted.
			return;
		}

		if (foundNew)
		{
			// This will work through generic method specializations for the types referenced above, clearing out AwaitingReference flags for
			//  newly-referenced generics, and queuing up their method specializations as well
			didWork |= DoWorkLoop(false, false);
		}
		else if (deleteUnusued)
		{			
			// Work queues should be empty if we're not canceling				
			BF_ASSERT(mContext->mPopulateTypeWorkList.size() == 0);
			BF_ASSERT(mContext->mMethodWorkList.size() == 0);

			// We need to use a delete queue because we trigger a RebuildType for dependent types,
			//  but we need to make sure we don't rebuild any types that may be next in line for
			//  deletion, so we must set BfTypeRebuildFlag_DeleteQueued first to avoid that
			Array<BfDependedType*> deleteQueue;

			// We bubble out 
			for (auto type : mContext->mResolvedTypes)
			{									
				auto depType = type->ToDependedType();

				// Delete if we're a generic
				if ((depType != NULL) && (!depType->IsDeleting()))
				{
					auto typeInst = depType->ToTypeInstance();
							
					bool wantDelete = false;
					if (typeInst != NULL)
					{
						wantDelete = (typeInst->mLastNonGenericUsedRevision != mRevision) &&
							(typeInst->IsGenericTypeInstance() || typeInst->IsBoxed()) && (!typeInst->IsUnspecializedType());
					}

					wantDelete |= (depType->IsOnDemand()) && (depType->mDependencyMap.IsEmpty());

					if (wantDelete)
					{
						deleteQueue.push_back(depType);
						depType->mRebuildFlags = (BfTypeRebuildFlags)(depType->mRebuildFlags | BfTypeRebuildFlag_DeleteQueued);
						foundNew = true;
					}
				}				
			}

			for (auto depType : deleteQueue)
			{
				BfLogSysM("Deleting type from deleteQueue in UpdateDependencyMap %p\n", depType);
				mContext->DeleteType(depType, true);
			}

			if (deleteQueue.size() != 0)
			{
				mContext->ValidateDependencies();
				mContext->UpdateAfterDeletingTypes();
				mContext->ValidateDependencies();
			}			
		}

		if (!foundNew)
			break;
	}	

#ifdef _DEBUG
	if (deleteUnusued)
	{		
		for (auto type : mContext->mResolvedTypes)
		{
			// This flag should be handled by now
			BF_ASSERT((type->mRebuildFlags & BfTypeRebuildFlag_AwaitingReference) == 0);
		}		
	}
#endif

	
	BP_ZONE("UpdateDependencyMap QueuedSpecializedMethodRebuildTypes");

	HashSet<BfTypeInstance*> specializerSet;

 	for (auto rebuildType : mContext->mQueuedSpecializedMethodRebuildTypes)
 	{
 		if (rebuildType->mRevision != mRevision)
 		{
			mContext->RebuildType(rebuildType);
			rebuildType->mRebuildFlags = (BfTypeRebuildFlags)(rebuildType->mRebuildFlags | BfTypeRebuildFlag_SpecializedMethodRebuild);

			for (auto& dep : rebuildType->mDependencyMap)
			{
				auto depType = dep.mKey;
				auto& depData = dep.mValue;
				auto depTypeInst = depType->ToTypeInstance();
				if (depTypeInst == NULL)
					continue;

				if ((depData.mFlags & BfDependencyMap::DependencyFlag_Calls) != 0)
				{
					specializerSet.Add(depTypeInst);
				}					
			}
 		}
 	}

	for (auto depType : specializerSet)
	{
		mContext->QueueMethodSpecializations(depType, true);
	}

	for (auto rebuildType : mContext->mQueuedSpecializedMethodRebuildTypes)
	{
		rebuildType->mRebuildFlags = (BfTypeRebuildFlags)(rebuildType->mRebuildFlags & ~BfTypeRebuildFlag_SpecializedMethodRebuild);
	}

	mContext->mQueuedSpecializedMethodRebuildTypes.Clear();
}

// When we are unsure of whether an old generic instance will survive, we RebuildType but don't put it in any worklist.
//  One of three things happens:
//   1) It gets built on demand
//   2) It gets deleted in UpdateDependencyMap
//   3) It stays undefined and we need to build it here
void BfCompiler::ProcessPurgatory(bool reifiedOnly)
{
	BP_ZONE("BfCompiler::ProcessPuragory");	

	while (true)
	{		
		mContext->RemoveInvalidWorkItems();		
		
		//for (auto type : mGenericInstancePurgatory)
		for (int i = 0; i < (int)mGenericInstancePurgatory.size(); i++)
		{
			auto type = mGenericInstancePurgatory[i];
			if ((reifiedOnly) && (!type->IsReified()))
				continue;

			if (!type->IsDeleting())
			{
				auto module = type->GetModule();
				if (module != NULL)
					module->PopulateType(type, BfPopulateType_Full);
			}

			if (reifiedOnly)
			{
				mGenericInstancePurgatory.RemoveAtFast(i);
				i--;
			}
		}

		if (!reifiedOnly)
			mGenericInstancePurgatory.Clear();

		int prevPurgatorySize = (int)mGenericInstancePurgatory.size();
		mContext->ProcessWorkList(reifiedOnly, reifiedOnly);
		if (prevPurgatorySize == (int)mGenericInstancePurgatory.size())
			break;
	}
}

bool BfCompiler::VerifySlotNums()
{
	BP_ZONE("BfCompiler::VerifySlotNums");	

	SmallVector<BfTypeInstance*, 16> isSlotUsed;		
	for (auto type : mContext->mResolvedTypes)
	{		
		if (!type->IsReified())
			continue;

		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;

		if (typeInst->IsUnspecializedType())
			continue;

		if (typeInst->IsInterface())
		{
			if (typeInst->mSlotNum == -2)
				continue; // Not used

			if ((typeInst->mVirtualMethodTableSize > 0) && (typeInst->mSlotNum == -1))
			{
				// Slot not assigned yet
				return false;
			}

			continue;
		}

		isSlotUsed.clear();
		isSlotUsed.resize(mMaxInterfaceSlots);

		auto checkType = typeInst;
		while (checkType != NULL)
		{				
			for (auto iface : checkType->mInterfaces)
			{
				int slotNum = iface.mInterfaceType->mSlotNum;
				if (slotNum >= 0)
				{
					if ((isSlotUsed[slotNum] != NULL) && (isSlotUsed[slotNum] != iface.mInterfaceType))
						return false; // Collision
					isSlotUsed[slotNum] = iface.mInterfaceType;
				}
			}
			
			checkType = checkType->mBaseType;
		}
	}	

	return true;
}

bool BfCompiler::QuickGenerateSlotNums()
{	
	/*SmallVector<bool, 16> isSlotUsed;	
	for (auto globalTypeEntry : mResolvedTypes)
	{
		BfType* type = globalTypeEntry->mType;

		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;

		if (typeInst->IsInterface())
		{
			if ((typeInst->mVirtualMethodTableSize > 0) && (typeInst->mSlotNum == -1))
			{
				// Slot not assigned yet
				return false;
			}

			continue;
		}		
	}

	return VerifySlotNums();*/

	// Implement later
	return false;
}

class BfSlotEntry
{
public:
	BfTypeInstance* mTypeInstance;
	int mRefCount;
	Array<BfTypeInstance*> mConcurrentRefs;	
};

typedef std::pair<BfTypeInstance*, BfTypeInstance*> InterfacePair;
typedef Dictionary<BfTypeInstance*, BfSlotEntry*> SlotEntryMap;

static BfSlotEntry* GetSlotEntry(SlotEntryMap& slotEntryMap, BfTypeInstance* typeInst)
{
	BF_ASSERT(typeInst->IsReified());

	BfSlotEntry** slotEntryPtr = NULL;
	if (!slotEntryMap.TryAdd(typeInst, NULL, &slotEntryPtr))
		return *slotEntryPtr;
		
	BfSlotEntry* slotEntry = new BfSlotEntry();
	slotEntry->mTypeInstance = typeInst;
	slotEntry->mRefCount = 0;
	//insertPair.first->second = slotEntry;
	*slotEntryPtr = slotEntry;
	return slotEntry;
}

static InterfacePair MakeInterfacePair(BfTypeInstance* iface1, BfTypeInstance* iface2)
{
	if (iface1->mTypeId < iface2->mTypeId)
		return InterfacePair(iface1, iface2);
	return InterfacePair(iface2, iface1);
}

struct InterfacePairHash
{	
	size_t operator()(const InterfacePair& val) const
	{
		return (((size_t)val.first) >> 2) ^ ((size_t)val.second);
	}
};

bool BfCompiler::SlowGenerateSlotNums()
{
	BP_ZONE("BfCompiler::SlowGenerateSlotNums");	
	
	SlotEntryMap ifaceUseMap;
	
	std::unordered_set<InterfacePair, InterfacePairHash> concurrentInterfaceSet;
	HashSet<BfTypeInstance*> foundIFaces;
	
	if (mMaxInterfaceSlots < 0)
	{
		mMaxInterfaceSlots = 0;
	}

	bool isHotCompile = IsHotCompile();

	for (auto type : mContext->mResolvedTypes)
	{		
		if (!type->IsReified())
			continue;

		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;

		if (typeInst->IsUnspecializedType())
			continue;

		if (typeInst->IsInterface())
		{
			if (typeInst->mSlotNum == -2) // Not needed
				continue;
			if (!isHotCompile) // Hot compiles cannot remap slot numbers
				typeInst->mSlotNum = -1;
			if (typeInst->mVirtualMethodTableSize > 0)
			{
				GetSlotEntry(ifaceUseMap, typeInst);
			}
			continue;
		}

		foundIFaces.Clear();
		auto checkTypeInst = typeInst;			
		while (checkTypeInst != NULL)
		{
			for (auto iface : checkTypeInst->mInterfaces)
			{
				auto interfaceType = iface.mInterfaceType;
				if (interfaceType->mSlotNum == -2)
					continue; // Not needed

				if ((isHotCompile) && (interfaceType->mSlotNum == -1))
					checkTypeInst->mDirty = true; // We're about to slot an interface here

				if (interfaceType->mVirtualMethodTableSize > 0)
				{
					BfSlotEntry* slotEntry = GetSlotEntry(ifaceUseMap, interfaceType);
					slotEntry->mRefCount++;
					foundIFaces.Add(iface.mInterfaceType);
				}
			}

			checkTypeInst = checkTypeInst->mBaseType;
		}

		for (auto itr1 = foundIFaces.begin(); itr1 != foundIFaces.end(); ++itr1)
		{
			auto itr2 = itr1;
			++itr2;
			for ( ; itr2 != foundIFaces.end(); ++itr2)
			{
				auto iface1 = *itr1;
				auto iface2 = *itr2;
					
				InterfacePair ifacePair = MakeInterfacePair(iface1, iface2);
				if (concurrentInterfaceSet.insert(ifacePair).second)
				{
					BfSlotEntry* entry1 = GetSlotEntry(ifaceUseMap, iface1);
					BfSlotEntry* entry2 = GetSlotEntry(ifaceUseMap, iface2);
					entry1->mConcurrentRefs.push_back(iface2);
					entry2->mConcurrentRefs.push_back(iface1);
				}					
			}
		}							
	}	
	
	Array<BfSlotEntry*> sortedIfaceUseMap;	
	for (auto& entry : ifaceUseMap)
	{
		if (!isHotCompile)
			BF_ASSERT(entry.mValue->mTypeInstance->mSlotNum == -1);
		sortedIfaceUseMap.push_back(entry.mValue);
	}	
			
	std::sort(sortedIfaceUseMap.begin(), sortedIfaceUseMap.end(), [] (BfSlotEntry* lhs, BfSlotEntry* rhs) 
	{ 
		if (lhs->mRefCount != rhs->mRefCount)
			return lhs->mRefCount > rhs->mRefCount; 
		return lhs->mTypeInstance->mTypeId < rhs->mTypeInstance->mTypeId;
	});

	bool failed = false;

	SmallVector<bool, 16> isSlotUsed;	
	for (auto slotEntry : sortedIfaceUseMap)
	{
		BfTypeInstance* iface = slotEntry->mTypeInstance;
		if (iface->mSlotNum >= 0)
		{
			BF_ASSERT(isHotCompile);
			continue;
		}

		isSlotUsed.clear();
		if (mMaxInterfaceSlots > 0)
			isSlotUsed.resize(mMaxInterfaceSlots);		

		BF_ASSERT(iface->mSlotNum == -1);

		BF_ASSERT(iface->IsInterface());

		for (auto iface2 : slotEntry->mConcurrentRefs)
		{
			int slotNum2 = iface2->mSlotNum;
			if (slotNum2 != -1)
				isSlotUsed[slotNum2] = true;
		}

		for (int checkSlot = 0; checkSlot < mMaxInterfaceSlots; checkSlot++)
		{
			if (!isSlotUsed[checkSlot])
			{
				iface->mSlotNum = checkSlot;
				break;
			}
		}

		if (iface->mSlotNum == -1)
 		{
			if (isHotCompile)
			{
				failed = true;
  				mPassInstance->Fail("Interface slot numbering overflow. Restart the program or revert changes.");
  				break;
			}

			iface->mSlotNum = mMaxInterfaceSlots;
			if (mOptions.mIncrementalBuild)
			{
				// Allocate more than enough interface slots
				mMaxInterfaceSlots += 3;
			}
			else
				mMaxInterfaceSlots++;

//  			failed = true;
//  			mPassInstance->Fail(StrFormat("Interface slot numbering overflow, increase the maximum slot number from '%d'", mMaxInterfaceSlots));
//  			break;
 		}

// 		if (iface->mSlotNum == -1)
// 		{
// 			failed = true;
// 			mPassInstance->Fail(StrFormat("Interface slot numbering overflow, increase the maximum slot number from '%d'", mMaxInterfaceSlots));
// 			break;
// 		}

		if (isHotCompile)
		{
			mHotState->mNewlySlottedTypeIds.Add(iface->mTypeId);
			mHotState->mSlotDefineTypeIds.Add(iface->mTypeId);
		}
	}

	if (!failed)
	{
		bool success = VerifySlotNums();
		if (!success)
		{
			BF_DBG_FATAL("Failed!");
		}
	}

	for (auto& entry : ifaceUseMap)
		delete entry.mValue;

	return true;
}

void BfCompiler::GenerateSlotNums()
{
	BP_ZONE("BfCompiler::GenerateSlotNums");	
	
	if (mMaxInterfaceSlots < 0)
	{
		if (mOptions.mIncrementalBuild)
			mMaxInterfaceSlots = 3;
		else
			mMaxInterfaceSlots = 0;
	}

	bool isHotCompile = IsHotCompile();
	
	for (auto type : mContext->mResolvedTypes)
	{		
		if (!type->IsInterface())
			continue;

		auto typeInstance = type->ToTypeInstance();
		if ((typeInstance->mSlotNum <= 0) || (!isHotCompile))
		{
			if (mContext->mReferencedIFaceSlots.Contains(typeInstance))
			{
				if (typeInstance->mSlotNum == -2)
					typeInstance->mSlotNum = -1;
			}
			else
				typeInstance->mSlotNum = -2; // Not needed
		}
	}

	if (VerifySlotNums())	
		return;	
	
	if (!QuickGenerateSlotNums())
		SlowGenerateSlotNums();		

	BfLogSysM("GenerateSlotNums mMaxInterfaceSlots: %d\n", mMaxInterfaceSlots);
}

void BfCompiler::GenerateDynCastData()
{
	BP_ZONE("BfCompiler::GenerateDynCastData");

	Array<int> firstDerivedIds;
	Array<int> nextSiblingIds;

	firstDerivedIds.Resize(mCurTypeId);
	nextSiblingIds.Resize(mCurTypeId);

	for (auto type : mContext->mResolvedTypes)
	{		
		if (type->IsBoxed())
			continue;

		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;
		
		if (typeInst->mBaseType == NULL)
			continue;
		
		int baseId = typeInst->mBaseType->mTypeId;
		int firstDerivedId = firstDerivedIds[baseId];

		nextSiblingIds[typeInst->mTypeId] = firstDerivedIds[baseId];
		firstDerivedIds[baseId] = typeInst->mTypeId;
	}

	int curInheritanceId = 1;

	std::function<void(BfTypeInstance*)> _AddTypeInfo = [&](BfTypeInstance* typeInst)
	{
		if (typeInst->mInheritanceId != curInheritanceId)
		{
			typeInst->mInheritanceId = curInheritanceId;
			typeInst->mDirty = true;
		}
		curInheritanceId++;

		int childId = firstDerivedIds[typeInst->mTypeId];
		while (childId != 0)
		{
			auto childType = mContext->mTypes[childId]->ToTypeInstance();
			_AddTypeInfo(childType);
			childId = nextSiblingIds[childId];
		}

		int inheritanceCount = curInheritanceId - typeInst->mInheritanceId - 1;
		if (typeInst->mInheritanceCount != inheritanceCount)
		{
			typeInst->mInheritanceCount = inheritanceCount;
			typeInst->mDirty = true;
		}
	};

	_AddTypeInfo(mContext->mBfObjectType);
	auto valueTypeInst = mContext->mScratchModule->ResolveTypeDef(mValueTypeTypeDef)->ToTypeInstance();
	_AddTypeInfo(valueTypeInst);
}

void BfCompiler::UpdateRevisedTypes()
{
	BfLogSysM("UpdateRevisedTypes\n");
	BP_ZONE("BfCompiler::UpdateRevisedTypes");	
		
	// See if we have any name conflicts and remove those
	auto typeDefItr = mSystem->mTypeDefs.begin();
	while (typeDefItr != mSystem->mTypeDefs.end())
	{
		auto typeDef = *typeDefItr;
		auto origTypeDef = typeDef;
		if (typeDef->mNextRevision != NULL)
			typeDef = typeDef->mNextRevision;		
		if (typeDef->mDupDetectedRevision == mRevision)
		{
			++typeDefItr;
			continue;
		}				
		typeDef->mDupDetectedRevision = -1;

		if ((typeDef->mIsCombinedPartial) || (typeDef->mDefState == BfTypeDef::DefState_Deleted) || (typeDef->mTypeCode == BfTypeCode_Extension))
		{
			++typeDefItr;
			continue;
		}

		if ((!typeDef->IsGlobalsContainer()) && (mSystem->ContainsNamespace(typeDef->mFullName, typeDef->mProject)))
		{
			mPassInstance->Fail(StrFormat("The name '%s' is already defined to be a namespace name", typeDef->mFullName.ToString().c_str()), typeDef->mTypeDeclaration->mNameNode);			
		}
						
		bool removedElement = false;
		auto nextTypeDefItr = typeDefItr;
		nextTypeDefItr.MoveToNextHashMatch();

		while (nextTypeDefItr)
		{
			auto nextTypeDef = *nextTypeDefItr;	
			if (nextTypeDef->mNextRevision != NULL)
				nextTypeDef = nextTypeDef->mNextRevision;
			if ((nextTypeDef->mIsCombinedPartial) || (nextTypeDef->mDefState == BfTypeDef::DefState_Deleted) || (nextTypeDef->mTypeCode == BfTypeCode_Extension) ||
				(typeDef->mFullName != nextTypeDef->mFullName) || (typeDef->mGenericParamDefs.size() != nextTypeDef->mGenericParamDefs.size()))
			{
				nextTypeDefItr.MoveToNextHashMatch();
				continue;
			}			

			if ((typeDef->mIsPartial) && (nextTypeDef->mIsPartial) && 
				(!typeDef->IsGlobalsContainer()) &&
				(typeDef->mProject != nextTypeDef->mProject))
			{
				BfTypeDef* typeA = NULL;
				BfTypeDef* typeB = NULL;
				BfError* error = NULL;

				if (typeDef->mProject->ReferencesOrReferencedBy(nextTypeDef->mProject))
				{
					typeA = typeDef;
					typeB = nextTypeDef;
				}
				else if (nextTypeDef->mProject->ReferencesOrReferencedBy(typeDef->mProject))
				{
					typeA = nextTypeDef;
					typeB = typeDef;
				}

				if (typeA != NULL)
				{
					error = mPassInstance->Fail(StrFormat("Partial type in project '%s' cannot extend a type from a referenced project", typeA->mProject->mName.c_str()).c_str(), 
						typeA->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo(StrFormat("Previous definition in project '%s'", typeB->mProject->mName.c_str()), 
						typeB->mTypeDeclaration->mNameNode);
				}
				if (error != NULL)
					error->mIsPersistent = true;
			}
			
			if (((!typeDef->mIsPartial) || (!nextTypeDef->mIsPartial)) &&
				(!typeDef->IsGlobalsContainer()) && (!nextTypeDef->IsGlobalsContainer()) &&
				(typeDef->mProject->ReferencesOrReferencedBy(nextTypeDef->mProject)))
			{
				nextTypeDef->mDupDetectedRevision = mRevision;

				BfError* error = NULL;
				/*if ((typeDef->mIsPartial) && (typeDef->mTypeCode != BfTypeCode_Extension))
				{					
					error = mPassInstance->Fail("Missing 'partial' modifier; another partial definition of this type exists", nextTypeDef->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo("Previous definition", typeDef->mTypeDeclaration->mNameNode);
				}
				else if ((nextTypeDef->mIsPartial) && (nextTypeDef->mTypeCode != BfTypeCode_Extension))
				{					
					error = mPassInstance->Fail("Missing 'partial' modifier; another partial definition of this type exists", typeDef->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo("Previous definition", nextTypeDef->mTypeDeclaration->mNameNode);					
				}
				else */if (nextTypeDef->mOuterType != NULL)
				{					
					error = mPassInstance->Fail(StrFormat("The type '%s.%s' already has a definition for '%s'", nextTypeDef->mOuterType->mNamespace.ToString().c_str(), nextTypeDef->mOuterType->mName->mString.mPtr,
						nextTypeDef->mName->mString.mPtr), nextTypeDef->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo("Previous definition", typeDef->mTypeDeclaration->mNameNode);
				}
				else if (!nextTypeDef->mNamespace.IsEmpty())
				{
					error = mPassInstance->Fail(StrFormat("The namespace '%s' already has a definition for '%s'", nextTypeDef->mNamespace.ToString().c_str(),
						nextTypeDef->mName->mString.mPtr), nextTypeDef->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo("Previous definition", typeDef->mTypeDeclaration->mNameNode);
				}
				else
				{					
					error = mPassInstance->Fail(StrFormat("The global namespace already has a definition for '%s'", 
						nextTypeDef->mName->mString.mPtr), nextTypeDef->mTypeDeclaration->mNameNode);
					mPassInstance->MoreInfo("Previous definition", typeDef->mTypeDeclaration->mNameNode);
				}
				if (error != NULL)
					error->mIsPersistent = true;
			}
			
			nextTypeDefItr.MoveToNextHashMatch();
		}

		++typeDefItr;
	}
	
	mContext->PreUpdateRevisedTypes();	

	// If we missed out on required types previously, now we should be 'okay'
	mInInvalidState = false;

	// We can't do any yields in here - the compiler state is invalid from the time we inject a new
	//  typedef revision up until we finish the associated RebuildType			
	int compositeBucket = 0;
	
	// These are "extension" defs that were unmatched last run through
	Array<BfTypeDef*> prevSoloExtensions;

	mSystem->mTypeDefs.CheckRehash();

	// Process the typedefs one bucket at a time.  When we are combining extensions or partials (globals) into a single definition then
	//  we will be making multiple passes over the bucket that contains that name
	for (int bucketIdx = 0; bucketIdx < mSystem->mTypeDefs.mHashSize; bucketIdx++)
	{
		bool hadPartials = false;
		bool hadChanges = false;
		
		if (mSystem->mTypeDefs.mHashHeads == NULL)
			break;

		// Partials combiner				
		auto outerTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];
		while (outerTypeDefEntry != NULL)
		{
			auto outerTypeDef = outerTypeDefEntry->mValue;

			if (outerTypeDef->mDefState == BfTypeDef::DefState_Deleted)
			{
				hadChanges = true;
				outerTypeDefEntry = outerTypeDefEntry->mNext;
				continue;
			}

			if (outerTypeDef->mNextRevision != NULL)
				hadChanges = true;			

			BfTypeDefMap::Entry* rootTypeDefEntry = NULL;
			BfTypeDef* rootTypeDef = NULL;
			BfTypeDef* compositeTypeDef = NULL;			
			
			auto latestOuterTypeDef = outerTypeDef->GetLatest();
			if ((outerTypeDef->mTypeCode == BfTypeCode_Extension) && (!outerTypeDef->mIsPartial))
			{
				prevSoloExtensions.Add(outerTypeDef);
				outerTypeDef->mIsPartial = true;
			}
			
			if ((outerTypeDef->mIsPartial) || (outerTypeDef->mIsCombinedPartial))
			{
				// Initialize mPartialUsed flags
				if (!hadPartials)
				{
					auto checkTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];
					while (checkTypeDefEntry != NULL)
					{						
						auto checkTypeDef = checkTypeDefEntry->mValue;
						if ((checkTypeDefEntry->mHash == outerTypeDefEntry->mHash) &&
							(checkTypeDef->NameEquals(outerTypeDef)))
						{
							checkTypeDef->mPartialUsed = false;
						}
						checkTypeDefEntry = checkTypeDefEntry->mNext;
					}
					hadPartials = true;
				}
			}

			if ((outerTypeDef->mTypeCode == BfTypeCode_Extension) && (!outerTypeDef->mPartialUsed))
			{								
				// Find root type, and we assume the composite type follows this
				auto checkTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];
				while (checkTypeDefEntry != NULL)
				{						
					auto checkTypeDef = checkTypeDefEntry->mValue;
					if ((checkTypeDefEntry->mHash != outerTypeDefEntry->mHash) ||
						(checkTypeDef->mIsCombinedPartial) ||
						(checkTypeDef->mTypeCode == BfTypeCode_Extension) ||
						(checkTypeDef->mDefState == BfTypeDef::DefState_Deleted) ||
						(checkTypeDef->mPartialUsed) ||
						(!checkTypeDef->NameEquals(outerTypeDef)) ||
						(checkTypeDef->mGenericParamDefs.size() != outerTypeDef->mGenericParamDefs.size()) ||
						(!outerTypeDef->mProject->ContainsReference(checkTypeDef->mProject)))
					{
						checkTypeDefEntry = checkTypeDefEntry->mNext;
						continue;
					}

					rootTypeDef = checkTypeDef;
					rootTypeDefEntry = checkTypeDefEntry;
					checkTypeDefEntry = checkTypeDefEntry->mNext;
				}
			}
			else if ((outerTypeDef->mIsExplicitPartial) && (!outerTypeDef->mPartialUsed))
			{
				// For explicit partials there is no 'root type' so we just use the first explicit partial
				rootTypeDef = outerTypeDef;
				rootTypeDefEntry = outerTypeDefEntry;

				// Find composite type, there is no explicit position for this
				auto checkTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];
				while (checkTypeDefEntry != NULL)
				{						
					auto checkTypeDef = checkTypeDefEntry->mValue;
					if ((checkTypeDefEntry->mHash != outerTypeDefEntry->mHash) ||
						(!checkTypeDef->mIsCombinedPartial) ||						
						(checkTypeDef->mPartialUsed) ||
						(checkTypeDef->mDefState == BfTypeDef::DefState_Deleted) ||
						(!checkTypeDef->NameEquals(outerTypeDef)) ||
						(checkTypeDef->mGenericParamDefs.size() != outerTypeDef->mGenericParamDefs.size()) ||
						(outerTypeDef->mProject != checkTypeDef->mProject))
					{
						checkTypeDefEntry = checkTypeDefEntry->mNext;
						continue;
					}

					compositeTypeDef = checkTypeDef;
					if (compositeTypeDef->mNextRevision != NULL)
					{
						// This is an old 'next revision'
						delete compositeTypeDef->mNextRevision;
						compositeTypeDef->mNextRevision = NULL;
					}
					checkTypeDefEntry = checkTypeDefEntry->mNext;
				}
			}

			// Now find extensions to apply to the rootTypeDef
			if (rootTypeDef != NULL)
			{
				bool partialsHadChanges = false;
				bool hadSignatureChange = false;
				bool compositeIsNew = false;

				if (compositeTypeDef == NULL)
				{
					if ((rootTypeDef->mIsExplicitPartial) || (rootTypeDefEntry->mNext == NULL) || (!rootTypeDefEntry->mNext->mValue->mIsCombinedPartial))
					{
						compositeTypeDef = new BfTypeDef();
						compositeTypeDef->mSystem = rootTypeDef->mSystem;
						compositeTypeDef->mProject = rootTypeDef->mProject;
						compositeTypeDef->mName = rootTypeDef->mName;
						compositeTypeDef->mName->mRefCount++;
						mSystem->TrackName(compositeTypeDef);
						compositeTypeDef->mNameEx = rootTypeDef->mNameEx;
						compositeTypeDef->mNameEx->Ref();
						compositeTypeDef->mProtection = rootTypeDef->mProtection;
						compositeTypeDef->mNamespace = rootTypeDef->mNamespace;
						compositeTypeDef->mTypeCode = BfTypeCode_Extension;
						compositeTypeDef->mFullName = rootTypeDef->mFullName;
						compositeTypeDef->mFullNameEx = rootTypeDef->mFullNameEx;
						compositeTypeDef->mIsCombinedPartial = true;

						for (auto prevGenericParam : rootTypeDef->mGenericParamDefs)
						{
							BfGenericParamDef* copiedGenericParam = new BfGenericParamDef();
							*copiedGenericParam = *prevGenericParam;
							compositeTypeDef->mGenericParamDefs.Add(copiedGenericParam);
						}

						mSystem->mTypeDefs.AddAfter(compositeTypeDef, rootTypeDefEntry);
						partialsHadChanges = true;
						hadSignatureChange = true;
						compositeIsNew = true;

						BfLogSysM("Creating compositeTypeDef %p\n", compositeTypeDef);
					}
					else
					{
						BF_ASSERT(rootTypeDefEntry->mNext->mValue->NameEquals(rootTypeDef));
						compositeTypeDef = rootTypeDefEntry->mNext->mValue;
						if (compositeTypeDef->mNextRevision != NULL)
						{
							// This is an old 'next revision'
							mSystem->InjectNewRevision(compositeTypeDef);							
							BF_ASSERT(compositeTypeDef->mNextRevision == NULL);	
						}
					}
				}
								
				// Collect the partials
				BfSizedVector<BfTypeDef*, 8> typeParts;
				typeParts.push_back(rootTypeDef);
				auto checkTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];				
				while (checkTypeDefEntry != NULL)
				{
					auto checkTypeDef = checkTypeDefEntry->mValue;

					bool isValidProject;
					if (rootTypeDef->mIsExplicitPartial)
						isValidProject = rootTypeDef->mProject == checkTypeDef->mProject;
					else
						isValidProject = checkTypeDef->mProject->ContainsReference(rootTypeDef->mProject);

					if (checkTypeDef != rootTypeDef)
					{
						if ((checkTypeDef->mIsCombinedPartial) ||
							(!checkTypeDef->mIsPartial) ||
							(checkTypeDef->mPartialUsed) ||
							(!checkTypeDef->NameEquals(rootTypeDef)) ||
							(checkTypeDef->mGenericParamDefs.size() != rootTypeDef->mGenericParamDefs.size()) ||
							(!isValidProject))
						{
							checkTypeDefEntry = checkTypeDefEntry->mNext;
							continue;
						}
					}

					compositeTypeDef->mPartialUsed = true;
					checkTypeDef->mPartialUsed = true;
					
					if (checkTypeDef->mDefState == BfTypeDef::DefState_Deleted)
					{
						partialsHadChanges = true;						
						hadSignatureChange = true;
					}					
					else 
					{
						if (checkTypeDef != rootTypeDef)
							typeParts.push_back(checkTypeDef);
						if (checkTypeDef->mNextRevision != NULL)
						{
							partialsHadChanges = true;
							BF_ASSERT(checkTypeDef->mNextRevision->mGenericParamDefs.size() == rootTypeDef->mGenericParamDefs.size());
							//mSystem->InjectNewRevision(checkTypeDef);
							//BF_ASSERT(checkTypeDef->mGenericParamDefs.size() == rootTypeDef->mGenericParamDefs.size());
						}
						else if (checkTypeDef->mDefState == BfTypeDef::DefState_New)
							partialsHadChanges = true;
					}

					checkTypeDefEntry = checkTypeDefEntry->mNext;
				}
				// Set this down here, because the InjectNewRevision will clear this flag
				rootTypeDef->mIsPartial = true;
								
				if (partialsHadChanges)
				{
					BF_ASSERT(compositeTypeDef->mNextRevision == NULL);

					mSystem->VerifyTypeDef(compositeTypeDef);
					for (auto checkTypeDef : typeParts)
					{
						mSystem->VerifyTypeDef(checkTypeDef);

						// Apply any def state that is more conservative
						if (checkTypeDef->mDefState == BfTypeDef::DefState_Signature_Changed)
							compositeTypeDef->mDefState = BfTypeDef::DefState_Signature_Changed;
						else if (checkTypeDef->mDefState == BfTypeDef::DefState_InlinedInternals_Changed)
						{
							if (compositeTypeDef->mDefState != BfTypeDef::DefState_Signature_Changed)
								compositeTypeDef->mDefState = BfTypeDef::DefState_InlinedInternals_Changed;
						}
						else if (checkTypeDef->mDefState == BfTypeDef::DefState_Internals_Changed)
						{
							if ((compositeTypeDef->mDefState != BfTypeDef::DefState_Signature_Changed) &&
								(compositeTypeDef->mDefState != BfTypeDef::DefState_InlinedInternals_Changed))
								compositeTypeDef->mDefState = BfTypeDef::DefState_Internals_Changed;
						}

						BF_ASSERT(checkTypeDef->mIsPartial);
						if (checkTypeDef->mNextRevision != NULL)
						{
							mSystem->VerifyTypeDef(checkTypeDef->mNextRevision);
							mSystem->InjectNewRevision(checkTypeDef);
						}
						checkTypeDef->mIsPartial = true;
						checkTypeDef->mDefState = BfTypeDef::DefState_Defined;
						mSystem->AddToCompositePartial(mPassInstance, compositeTypeDef, checkTypeDef);
					}
					mSystem->FinishCompositePartial(compositeTypeDef);

					if (!compositeIsNew)
					{
						if (compositeTypeDef->mNextRevision != NULL)
						{
							BF_ASSERT(compositeTypeDef->mPartials.size() != 0);
						}
					}

					// We use the root typedef's namespace search for the composite, but this should only be 
					//  used for cases where we CANNOT specify a typeref on an extension. IE: custom attributes
					//  for a type can only be added on the root typedef. If this changes then we need to make
					//  sure that we attach a definingType to attributes
					for (auto name : compositeTypeDef->mNamespaceSearch)
						mSystem->ReleaseAtomComposite(name);
					compositeTypeDef->mNamespaceSearch = rootTypeDef->mNamespaceSearch;
					for (auto name : compositeTypeDef->mNamespaceSearch)
						mSystem->RefAtomComposite(name);

					if (rootTypeDef != NULL)
						compositeTypeDef->mNamespaceSearch = rootTypeDef->mNamespaceSearch;
					else
						compositeTypeDef->mNamespaceSearch.Clear();					

					//BfLogSysM("Composite type %p updating. isNew: %d\n", compositeTypeDef, compositeIsNew);

					if (compositeIsNew)
					{
						compositeTypeDef->mDefState = BfTypeDef::DefState_New;
						mSystem->InjectNewRevision(compositeTypeDef);
						// Reset 'New' state
						compositeTypeDef->mDefState = BfTypeDef::DefState_New;
					}
					else if (hadSignatureChange)
						compositeTypeDef->mDefState = BfTypeDef::DefState_Signature_Changed;

					if (compositeTypeDef->mDefState == BfTypeDef::DefState_Defined)
					{
						// No changes, just inject
						mSystem->InjectNewRevision(compositeTypeDef);
					}

					/*if (compositeTypeDef->mTypeCode == BfTypeCode_Extension)
					{
						BF_ASSERT(rootTypeDef == NULL);
						compositeTypeDef->mTypeCode = BfTypeCode_Object;
					}*/

					auto latestCompositeTypeDef = compositeTypeDef->GetLatest();					
					if (latestCompositeTypeDef->mTypeCode == BfTypeCode_Extension)
					{
						BF_ASSERT(rootTypeDef == NULL);
						latestCompositeTypeDef->mTypeCode = BfTypeCode_Object;
					}
					
					BfLogSysM("Partial combined type typedef %p updated from parser %p\n", compositeTypeDef, latestCompositeTypeDef->mTypeDeclaration->GetSourceData());
				}				
			}			

			outerTypeDefEntry = outerTypeDefEntry->mNext;
		}

		// Handle unused partials, apply any new revisions, process pending deletes
		
		if ((hadPartials) || (hadChanges))
		{
			BfTypeDef* checkMasterTypeDef = NULL;
			BfTypeDef* deletedCombinedPartial = NULL;

			outerTypeDefEntry = mSystem->mTypeDefs.mHashHeads[bucketIdx];
			while (outerTypeDefEntry != NULL)
			{
				auto outerTypeDef = outerTypeDefEntry->mValue;
				auto nextTypeDefEntry = outerTypeDefEntry->mNext;

				if ((outerTypeDef->mIsPartial) && (!outerTypeDef->mIsExplicitPartial) && (outerTypeDef->mTypeCode != BfTypeCode_Extension) &&
					(nextTypeDefEntry != NULL) && (!nextTypeDefEntry->mValue->mPartialUsed))
				{
					// This is a root type that we've removed all extensions from, so now we go back to treating it as the actual definition
					//  instead of using the composite that immediately follows it
					BF_ASSERT(outerTypeDef->mTypeCode != BfTypeCode_Extension);
					outerTypeDef->mIsPartial = false;
					outerTypeDef->mPartialIdx = -1;
				}

				if (outerTypeDef->mDefState == BfTypeDef::DefState_Deleted)
				{
					BfLogSysM("UpdateRevisedTypes deleting outerTypeDef %p\n", outerTypeDef);
					outerTypeDef->mDefState = BfTypeDef::DefState_Deleted;
					mSystem->RemoveTypeDef(outerTypeDef);
				}
				else if (!outerTypeDef->mPartialUsed)
				{
					if (outerTypeDef->mIsCombinedPartial)
					{
						BfLogSysM("UpdateRevisedTypes deleting combinedPartial type %p\n", outerTypeDef);
						deletedCombinedPartial = outerTypeDef;
						outerTypeDef->mDefState = BfTypeDef::DefState_Deleted;
						mSystem->RemoveTypeDef(outerTypeDef);
					}
					else if (outerTypeDef->mTypeCode == BfTypeCode_Extension)
					{
						auto error = mPassInstance->Fail(StrFormat("Unable to find root type definition for extension '%s'", outerTypeDef->GetLatest()->mFullName.ToString().c_str()),
							outerTypeDef->GetLatest()->mTypeDeclaration->mNameNode);
						if (error != NULL)
							error->mIsPersistent = true;

						if (outerTypeDef->mIsPartial)
						{
							// Allow this typeDef be a full solo type by itself
							outerTypeDef->mIsPartial = false;
							if (outerTypeDef->mNextRevision != NULL)
								outerTypeDef->mNextRevision->mIsPartial = false;
							if (outerTypeDef->mPartialIdx != -1)
							{
								outerTypeDef->mPartialIdx = -1;
								outerTypeDef->mDefState = BfTypeDef::DefState_New;
							}
						}
					}					
				}			

				if (outerTypeDef->mDefState != BfTypeDef::DefState_Deleted)
					checkMasterTypeDef = outerTypeDef;

				if ((deletedCombinedPartial != NULL) && (checkMasterTypeDef != NULL) &&
					(deletedCombinedPartial->NameEquals(checkMasterTypeDef)))
				{
					// Remap nested types to their master typeDef
					for (auto nestedType : deletedCombinedPartial->mNestedTypes)
					{
						nestedType->mOuterType = checkMasterTypeDef;
					}
					deletedCombinedPartial = NULL;
					checkMasterTypeDef = NULL;
				}

				outerTypeDefEntry = nextTypeDefEntry;				
			}
		}
	}

	for (auto typeDef : prevSoloExtensions)
	{
		// If this got added to a composite partial then delete the previous solo type
		if (typeDef->mIsPartial)
		{			
			BfLogSysM("Solo partial going back to normal partial %p\n", typeDef);

			typeDef->mIsPartial = false;
			auto type = mContext->mScratchModule->ResolveTypeDef(typeDef, BfPopulateType_Identity);
			mContext->DeleteType(type);
			typeDef->mIsPartial = true;			
		}
	}
	
	mContext->UpdateRevisedTypes();
	mContext->VerifyTypeLookups();	
	
	mContext->ValidateDependencies();
	if (mStats.mTypesDeleted != 0)
	{
		mContext->UpdateAfterDeletingTypes();
		mContext->ValidateDependencies();
	}
	mContext->RemoveInvalidWorkItems();	
	
	//for (auto typeDef : mSystem->mTypeDefs)
	//{
	//	auto latestTypeDef = typeDef->GetLatest();
	//	if ((latestTypeDef->mOuterType != NULL) && (latestTypeDef->mOuterType->mIsPartial) && (latestTypeDef->mIsCombinedPartial))
	//		//((!latestTypeDef->mIsPartial) || (latestTypeDef->mIsCombinedPartial)))
	//		latestTypeDef->mOuterType = mSystem->GetOuterTypeNonPartial(latestTypeDef);

	//	/*String fullName = typeDef->mFullNameEx.ToString();
	//	if (fullName == "System.Collections.Generic.List`1.Enumerator`1")
	//	{
	//		NOP;
	//	}

	//	if ((typeDef->mOuterType != NULL) && (!typeDef->mIsPartial) && (typeDef->mOuterType->mIsPartial) && (!typeDef->mOuterType->mIsCombinedPartial))
	//	{
	//		NOP;
	//	}*/
	//}

	mSystem->mNeedsTypesHandledByCompiler = false;

	//TODO:
	//Sleep(300);
	//mSystem->CheckLockYield();
}

BfTypeDef* BfCompiler::GetArrayTypeDef(int dimensions)
{
	BF_ASSERT(dimensions <= 4);
	if (dimensions == 1)
		return mArray1TypeDef;
	if (dimensions == 2)
		return mArray2TypeDef;
	if (dimensions == 3)
		return mArray3TypeDef;
	return mArray4TypeDef;
}

void BfCompiler::VisitAutocompleteExteriorIdentifiers()
{
	for (auto checkNode : mResolvePassData->mExteriorAutocompleteCheckNodes)
	{
		bool isUsingDirective = false;
		BfIdentifierNode* checkIdentifier = NULL;
		if (auto usingDirective = BfNodeDynCast<BfUsingDirective>(checkNode))
		{
			checkIdentifier = usingDirective->mNamespace;
		}
		else if (auto usingDirective = BfNodeDynCast<BfUsingStaticDirective>(checkNode))
		{
			if (usingDirective->mTypeRef != NULL)
			{
				BF_ASSERT(mContext->mScratchModule->mCurTypeInstance == NULL);

				SetAndRestoreValue<BfTypeInstance*> prevCurTypeInstance(mContext->mScratchModule->mCurTypeInstance, NULL);
				mContext->mScratchModule->ResolveTypeRef(usingDirective->mTypeRef, NULL);
				if (mResolvePassData->mAutoComplete != NULL)
					mResolvePassData->mAutoComplete->CheckTypeRef(usingDirective->mTypeRef, false, isUsingDirective);
				continue;
			}
		}
		else
			checkIdentifier = BfNodeDynCast<BfIdentifierNode>(checkNode);

		if (checkIdentifier == NULL)
			continue;

		if (mResolvePassData->mAutoComplete != NULL)
			mResolvePassData->mAutoComplete->CheckIdentifier(checkIdentifier, false, isUsingDirective);

		if ((checkIdentifier->IsFromParser(mResolvePassData->mParser)) && (mResolvePassData->mSourceClassifier != NULL))
		{
			if (isUsingDirective)
			{
				while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkIdentifier))
				{
					mResolvePassData->mSourceClassifier->SetElementType(qualifiedNameNode->mRight, BfSourceElementType_Namespace);
					checkIdentifier = qualifiedNameNode->mLeft;
				}

				if (checkIdentifier != NULL)
					mResolvePassData->mSourceClassifier->SetElementType(checkIdentifier, BfSourceElementType_Namespace);
			}
		}
	}
	mResolvePassData->mExteriorAutocompleteCheckNodes.Clear();
}

void BfCompiler::VisitSourceExteriorNodes()
{	
	BP_ZONE("BfCompiler::VisitSourceExteriorNodes");

	String str;
	Array<BfAtom*> namespaceParts;
	Array<BfAstNode*> srcNodes;
	std::function<bool(BfAstNode*)> _AddName = [&](BfAstNode* node)
	{
		if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(node))
		{
			if (!_AddName(qualifiedName->mLeft))
				return false;
			if (!_AddName(qualifiedName->mRight))
				return false;
		}
		else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(node))
		{
			srcNodes.Add(identifier);
			str.Clear();
			identifier->ToString(str);
			auto atom = mSystem->FindAtom(str);
			if (atom == NULL)
			{
				String prevNamespace;
				for (auto part : namespaceParts)
				{
					if (!prevNamespace.IsEmpty())
						prevNamespace += ".";
					prevNamespace += part->mString;
				}

				if (prevNamespace.IsEmpty())
					mPassInstance->Fail(StrFormat("The namespace '%s' does not exist", str.c_str()), identifier);
				else
					mPassInstance->Fail(StrFormat("The namespace '%s' does not exist in the namespace '%s'", str.c_str(), prevNamespace.c_str()), identifier);
				return false;
			}
			namespaceParts.Add(atom);
		}
		return true;
	};

	auto _CheckParser = [&](BfParser* parser)
	{
		while (parser->mNextRevision != NULL)
			parser = parser->mNextRevision;
		if (parser->mAwaitingDelete)
			return;		

		if (parser->mParserData->mExteriorNodesCheckIdx == mSystem->mTypeMapVersion)
			return;

		bool failed = false;
		for (auto node : parser->mParserData->mExteriorNodes)
		{
			if (auto usingDirective = BfNodeDynCast<BfUsingDirective>(node))
			{
				srcNodes.Clear();
				namespaceParts.Clear();
				bool success = _AddName(usingDirective->mNamespace);
				
				for (int i = 0; i < (int)namespaceParts.size(); i++)
				{
					BfAtomComposite checkNamespace;
					checkNamespace.mParts = &namespaceParts[0];
					checkNamespace.mSize = i + 1;

					if (!mSystem->ContainsNamespace(checkNamespace, parser->mProject))
					{
						failed = true;
						BfAtomComposite prevNamespace;
						prevNamespace.mParts = &namespaceParts[0];
						prevNamespace.mSize = i;
						if (i == 0)
							mPassInstance->Fail(StrFormat("The namespace '%s' does not exist", namespaceParts[i]->mString.ToString().c_str()), srcNodes[i]);
						else
							mPassInstance->Fail(StrFormat("The namespace '%s' does not exist in the namespace '%s'", namespaceParts[i]->mString.ToString().c_str(), prevNamespace.ToString().c_str()), srcNodes[i]);
						break;
					}
				}				
			}
			else if (auto usingDirective = BfNodeDynCast<BfUsingStaticDirective>(node))
			{
				if (usingDirective->mTypeRef != NULL)
				{
					BF_ASSERT(mContext->mScratchModule->mCurTypeInstance == NULL);

					SetAndRestoreValue<BfTypeInstance*> prevCurTypeInstance(mContext->mScratchModule->mCurTypeInstance, NULL);
					mContext->mScratchModule->ResolveTypeRef(usingDirective->mTypeRef, NULL);
					if ((mResolvePassData != NULL) && (mResolvePassData->mAutoComplete != NULL))
						mResolvePassData->mAutoComplete->CheckTypeRef(usingDirective->mTypeRef, false, false);
					return;
				}
			}
		}

		if (!failed)
			parser->mParserData->mExteriorNodesCheckIdx = mSystem->mTypeMapVersion;
	};

	if ((mResolvePassData != NULL) && (mResolvePassData->mParser != NULL))
	{
		_CheckParser(mResolvePassData->mParser);
	}
	else		
	{
		for (auto parser : mSystem->mParsers)
		{
			_CheckParser(parser);
		}
	}
}

void BfCompiler::ProcessAutocompleteTempType()
{
	BP_ZONE_F("BfCompiler::ProcessAutocompleteTempType %d", mResolvePassData->mResolveType);	

	String& autoCompleteResultString = *gTLStrReturn.Get();
	autoCompleteResultString.clear();
			
	if (mContext->mBfObjectType == NULL)
		return; // Not initialized yet
	auto module = mContext->mScratchModule;

	auto autoComplete = mResolvePassData->mAutoComplete;
	BfLogSysM("ProcessAutocompleteTempType %d\n", autoComplete->mResolveType);

	SetAndRestoreValue<bool> prevCanceling(mCanceling, false);

	BF_ASSERT(mResolvePassData->mAutoComplete->mDefMethod == NULL);
	if (autoComplete->mResolveType == BfResolveType_GetNavigationData)
	{
		for (auto node : mResolvePassData->mParser->mSidechannelRootNode->mChildArr)
		{
			if (auto preprocNode = BfNodeDynCast<BfPreprocessorNode>(node))
			{
				if (preprocNode->mCommand->Equals("region"))
				{
					if (!autoCompleteResultString.empty())
						autoCompleteResultString += "\n";
					autoCompleteResultString += "#";
					preprocNode->mArgument->ToString(autoCompleteResultString);
					mContext->mScratchModule->UpdateSrcPos(preprocNode, (BfSrcPosFlags)(BfSrcPosFlag_NoSetDebugLoc | BfSrcPosFlag_Force));
					autoCompleteResultString += StrFormat("\tregion\t%d\t%d", module->mCurFilePosition.mCurLine, module->mCurFilePosition.mCurColumn);
				}
			}
		}

		for (auto tempTypeDef : mResolvePassData->mAutoCompleteTempTypes)
		{						
			String typeName = tempTypeDef->ToString();

			BfLogSysM("BfResolveType_GetNavigationData TypeDef:%p %s\n", tempTypeDef, typeName.c_str());

			auto refNode = tempTypeDef->GetRefNode();
			if ((refNode != NULL) && (!tempTypeDef->IsGlobalsContainer()))
			{
				if (!autoCompleteResultString.empty())
					autoCompleteResultString += "\n";

				String typeName = BfTypeUtils::TypeToString(tempTypeDef, BfTypeNameFlag_OmitNamespace);

				module->UpdateSrcPos(refNode, (BfSrcPosFlags)(BfSrcPosFlag_NoSetDebugLoc | BfSrcPosFlag_Force));
				autoCompleteResultString += typeName;
				if (tempTypeDef->mTypeCode == BfTypeCode_Object)
					autoCompleteResultString += "\tclass";
				else if (tempTypeDef->mTypeCode == BfTypeCode_Enum)
					autoCompleteResultString += "\tenum";
				else if (tempTypeDef->mTypeCode == BfTypeCode_Struct)
					autoCompleteResultString += "\tstruct";
				else if (tempTypeDef->mTypeCode == BfTypeCode_TypeAlias)
					autoCompleteResultString += "\ttypealias";
				else
					autoCompleteResultString += "\t";
				autoCompleteResultString += StrFormat("\t%d\t%d", module->mCurFilePosition.mCurLine, module->mCurFilePosition.mCurColumn);
			}

			String methodText;
			for (auto methodDef : tempTypeDef->mMethods)
			{				
				if (((methodDef->mMethodType == BfMethodType_Normal) || (methodDef->mMethodType == BfMethodType_Operator) ||
					 (methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_Dtor) ||
					(methodDef->mMethodType == BfMethodType_Mixin)) &&
					(methodDef->mMethodDeclaration != NULL))
				{
					methodText = methodDef->ToString();
					if (typeName != "@")
						methodText = typeName + "." + methodText;					

					if (!autoCompleteResultString.empty())
						autoCompleteResultString += "\n";

					auto methodDeclaration = methodDef->GetMethodDeclaration();
					BfAstNode* refNode = methodDeclaration;
					if (methodDeclaration->mBody != NULL)
						refNode = methodDeclaration->mBody;
					else if (methodDeclaration->mNameNode != NULL)
						refNode = methodDeclaration->mNameNode;
					module->UpdateSrcPos(refNode, (BfSrcPosFlags)(BfSrcPosFlag_NoSetDebugLoc | BfSrcPosFlag_Force));

					methodText += StrFormat("\tmethod\t%d\t%d", module->mCurFilePosition.mCurLine, module->mCurFilePosition.mCurColumn);

					autoCompleteResultString += methodText;
				}
			}

			for (auto propDef : tempTypeDef->mProperties)
			{
				auto propDeclaration = BfNodeDynCast<BfPropertyDeclaration>(propDef->mFieldDeclaration);
				if ((propDeclaration == NULL) || (propDeclaration->mNameNode == NULL))
					continue;

				String propText = propDef->mName;
				if (typeName != "@")
					propText = typeName + "." + propText;

				if (!autoCompleteResultString.empty())
					autoCompleteResultString += "\n";

				BfAstNode* refNode = propDeclaration->mNameNode;				
				module->UpdateSrcPos(refNode, (BfSrcPosFlags)(BfSrcPosFlag_NoSetDebugLoc | BfSrcPosFlag_Force));

				propText += StrFormat("\tproperty\t%d\t%d", module->mCurFilePosition.mCurLine, module->mCurFilePosition.mCurColumn);

				autoCompleteResultString += propText;
			}
		}

		module->CleanupFileInstances();
		return;
	}

	if (autoComplete->mResolveType == BfResolveType_GetCurrentLocation)
	{
		for (auto tempTypeDef : mResolvePassData->mAutoCompleteTempTypes)
		{						
			String typeName = tempTypeDef->mNamespace.ToString();
			if (!typeName.empty())
				typeName += ".";
			typeName += tempTypeDef->ToString();

			autoCompleteResultString = typeName;

			int cursorPos = mResolvePassData->mParser->mCursorIdx;

			for (auto methodDef : tempTypeDef->mMethods)
			{	
				BfAstNode* defNode = methodDef->mMethodDeclaration;
				if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
					defNode = propertyDeclaration;

				if ((defNode != NULL) && 
					(defNode->Contains(cursorPos)))
				{
					String methodText = methodDef->ToString();
					if (typeName != "@")
						methodText = typeName + "." + methodText;

					autoCompleteResultString = methodText;
					break;
				}
			}			
		}

		module->CleanupFileInstances();
		return;
	}

	// >>> VisitExteriorIdentifiers
	mResolvePassData->mAutoComplete->SetModule(module);
	{		
		SetAndRestoreValue<BfTypeState*> prevTypeState(module->mContext->mCurTypeState, NULL);
		BP_ZONE("VisitExteriorIdentifiers");		
		VisitAutocompleteExteriorIdentifiers();
	}	
	VisitSourceExteriorNodes();

	BfTypeDef* tempTypeDef = NULL;
	for (auto checkTempType : mResolvePassData->mAutoCompleteTempTypes)
	{
		if (mResolvePassData->mAutoComplete->IsAutocompleteNode(checkTempType->mTypeDeclaration))
		{
			tempTypeDef = checkTempType;
			mContext->HandleChangedTypeDef(tempTypeDef, true);
		}
	}
	
	if (tempTypeDef == NULL)
	{
		GenerateAutocompleteInfo();
		BfLogSysM("ProcessAutocompleteTempType - no tempTypeDef\n");
		return;
	}	

	if (tempTypeDef->mProject->mDisabled)
	{
		BfLogSysM("ProcessAutocompleteTempType - project disabled\n");
		return;
	}

	SetAndRestoreValue<BfMethodState*> prevMethodState(module->mCurMethodState, NULL);

	BfTypeState typeState;
	typeState.mCurTypeDef = tempTypeDef;
	SetAndRestoreValue<BfTypeState*> prevTypeState(module->mContext->mCurTypeState, &typeState);	

	BfTypeDef* actualTypeDef = NULL;
	auto typeName = tempTypeDef->mFullName;
	int wantNumGenericParams = (int)tempTypeDef->mGenericParamDefs.size();
	auto actualTypeDefItr = mSystem->mTypeDefs.TryGet(typeName);
	while (actualTypeDefItr)
	{
		auto checkTypeDef = *actualTypeDefItr;
		if ((!checkTypeDef->mIsPartial) /*&& (checkTypeDef->mTypeCode != BfTypeCode_Extension)*/ &&
            ((checkTypeDef->mTypeCode == tempTypeDef->mTypeCode) || (tempTypeDef->mTypeCode == BfTypeCode_Extension)))
		{
			if ((checkTypeDef->NameEquals(tempTypeDef)) && (checkTypeDef->mIsCombinedPartial) && 
				(checkTypeDef->mGenericParamDefs.size() == tempTypeDef->mGenericParamDefs.size()) &&
				(tempTypeDef->mProject->ContainsReference(checkTypeDef->mProject)))
			{
				actualTypeDef = mSystem->FilterDeletedTypeDef(checkTypeDef);
				break;
			}

			if ((checkTypeDef->mGenericParamDefs.size() == wantNumGenericParams) &&
				(FileNameEquals(tempTypeDef->mSource->mSourceData->ToParserData()->mFileName, checkTypeDef->mSource->mSourceData->ToParserData()->mFileName)) &&
				(tempTypeDef->mProject == checkTypeDef->mProject))
			{
				actualTypeDef = mSystem->FilterDeletedTypeDef(checkTypeDef);
				break;
			}
		}
		
		actualTypeDefItr.MoveToNextHashMatch();
	}

	if ((actualTypeDef == NULL) || (actualTypeDef->mTypeDeclaration == NULL))
	{
		auto autoComplete = mResolvePassData->mAutoComplete;
		if (autoComplete->IsAutocompleteNode(tempTypeDef->mTypeDeclaration->mNameNode))
		{
			BfIdentifierNode* nameNode = tempTypeDef->mTypeDeclaration->mNameNode;
			if (tempTypeDef->mTypeCode == BfTypeCode_Extension)
			{
				autoComplete->AddTopLevelNamespaces(nameNode);
				autoComplete->AddTopLevelTypes(nameNode);
				autoComplete->mInsertStartIdx = nameNode->GetSrcStart();
				autoComplete->mInsertEndIdx = nameNode->GetSrcEnd();
			}
		}

		//mResolvePassData->mSourceClassifier->MarkSkipped(tempTypeDef->mTypeDeclaration);

		GenerateAutocompleteInfo();
		return;
	}
	
	if (tempTypeDef->mTypeDeclaration->mAttributes != NULL)
	{
		mResolvePassData->mSourceClassifier->VisitChild(tempTypeDef->mTypeDeclaration->mAttributes);
	}

	BfTypeInstance* typeInst;
	{
		BP_ZONE("ProcessAutocompleteTempType.ResolveTypeDef");		
		typeInst = (BfTypeInstance*)module->ResolveTypeDef(actualTypeDef, BfPopulateType_IdentityNoRemapAlias);

		if ((typeInst != NULL) && (typeInst->IsIncomplete()))
			module->PopulateType(typeInst, BfPopulateType_Full);
	}

	if (typeInst == NULL)
	{				
		return;		
	}

	BF_ASSERT((typeInst->mSize != -1) || (typeInst->IsTypeAlias()));

#ifdef _DEBUG
	if ((typeInst->mModule != NULL) && (!typeInst->mModule->mIsScratchModule))
		mLastAutocompleteModule = typeInst->mModule;
#endif

	SetAndRestoreValue<BfTypeInstance*> prevType(module->mCurTypeInstance, typeInst);
	typeState.mTypeInstance = typeInst;
	
	BfGenericExtensionEntry* genericExEntry = NULL;
	bool hadTempExtensionInfo = false;
	if ((tempTypeDef->IsExtension()) && (actualTypeDef->mIsCombinedPartial) && (typeInst->IsGenericTypeInstance()))
	{
		// Add to our extension info map and then take it out at the end...
		auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
		module->BuildGenericExtensionInfo(genericTypeInst, tempTypeDef);		
		genericTypeInst->mGenericExtensionInfo->mExtensionMap.TryGetValue(tempTypeDef, &genericExEntry);
		BF_ASSERT(genericExEntry != NULL);
		hadTempExtensionInfo = true;
	}

	if ((typeInst->IsUnspecializedType()) || (!typeInst->IsGenericTypeInstance()))
	{
		auto autoComplete = mResolvePassData->mAutoComplete;
		if (autoComplete->IsAutocompleteNode(tempTypeDef->mTypeDeclaration->mNameNode))
		{
			BfIdentifierNode* nameNode;
			nameNode = tempTypeDef->mTypeDeclaration->mNameNode;
			if ((actualTypeDef->mIsCombinedPartial) && (tempTypeDef->mTypeCode == BfTypeCode_Extension))
			{
				autoComplete->AddTopLevelNamespaces(tempTypeDef->mTypeDeclaration->mNameNode);
				autoComplete->AddTopLevelTypes(tempTypeDef->mTypeDeclaration->mNameNode);
				autoComplete->SetDefinitionLocation(actualTypeDef->mTypeDeclaration->mNameNode);
			}
			else				
				autoComplete->SetDefinitionLocation(nameNode);
			autoComplete->mDefType = actualTypeDef;
			autoComplete->mInsertStartIdx = nameNode->GetSrcStart();
			autoComplete->mInsertEndIdx = nameNode->GetSrcEnd();
		}		
	}	
	autoComplete->CheckInterfaceFixit(typeInst, tempTypeDef->mTypeDeclaration->mNameNode);

	if (tempTypeDef->mTypeCode == BfTypeCode_TypeAlias)
	{
		auto typeAliasDecl = (BfTypeAliasDeclaration*)tempTypeDef->mTypeDeclaration;
		if (typeAliasDecl->mAliasToType != NULL)
		{
			autoComplete->CheckTypeRef(typeAliasDecl->mAliasToType, false);
			module->ResolveTypeRef(typeAliasDecl->mAliasToType);
		}
	}

	// Save and restore mFieldResolveReentrys, we could fire off autocomplete while resolving a field
	SetAndRestoreValue<decltype (module->mContext->mFieldResolveReentrys)> prevTypeResolveReentry(module->mContext->mFieldResolveReentrys);
	module->mContext->mFieldResolveReentrys.Clear();

	if (tempTypeDef->mTypeDeclaration->mAttributes != NULL)
	{
		BfAttributeTargets attrTarget;
		if (tempTypeDef->mIsDelegate)
			attrTarget = BfAttributeTargets_Delegate;
		else if (typeInst->IsEnum())
			attrTarget = BfAttributeTargets_Enum;
		else if (typeInst->IsInterface())
			attrTarget = BfAttributeTargets_Interface;
		else if (typeInst->IsStruct())
			attrTarget = BfAttributeTargets_Struct;
		else
			attrTarget = BfAttributeTargets_Class;
		auto customAttrs = module->GetCustomAttributes(tempTypeDef->mTypeDeclaration->mAttributes, attrTarget);
		delete customAttrs;
	}
	
	for (int genericParamIdx = 0; genericParamIdx < (int)tempTypeDef->mGenericParamDefs.size(); genericParamIdx++)
	{	
		auto genericParamDef = tempTypeDef->mGenericParamDefs[genericParamIdx];

		auto genericParamInstance = new BfGenericTypeParamInstance(tempTypeDef, genericParamIdx);
		genericParamInstance->mExternType = module->GetGenericParamType(BfGenericParamKind_Type, genericParamIdx);
		module->ResolveGenericParamConstraints(genericParamInstance, true);
		delete genericParamInstance;

		for (auto nameNode : genericParamDef->mNameNodes)		
			module->HandleTypeGenericParamRef(nameNode, tempTypeDef, genericParamIdx);		
	}

	for (auto fieldDef : tempTypeDef->mFields)
	{
		BP_ZONE("ProcessAutocompleteTempType.CheckField");		

		auto fieldDecl = fieldDef->mFieldDeclaration;
		if (BfNodeIsA<BfPropertyDeclaration>(fieldDecl))
			continue; // Don't process auto-generated property fields

		if (fieldDef->mTypeRef != NULL)
		{
			module->ResolveTypeRef(fieldDef->mTypeRef);			
		}
		mResolvePassData->mAutoComplete->CheckTypeRef(fieldDef->mTypeRef, true);
	
		actualTypeDef->PopulateMemberSets();

		BfFieldDef* actualFieldDef = NULL;		
		BfMemberSetEntry* memberSetEntry = NULL;
		if (actualTypeDef->mFieldSet.TryGetWith(fieldDef->mName, &memberSetEntry))
		{
			auto checkFieldDef = (BfFieldDef*)memberSetEntry->mMemberDef;		
			if ((checkFieldDef->mIsConst == fieldDef->mIsConst) &&
				(checkFieldDef->mIsStatic == fieldDef->mIsStatic))
			{
				actualFieldDef = checkFieldDef;
			}
		}

		if (actualFieldDef != NULL)
		{
			auto fieldInstance = &typeInst->mFieldInstances[actualFieldDef->mIdx];
			autoComplete->CheckVarResolution(fieldDef->mTypeRef, fieldInstance->mResolvedType);
		}

		if (((autoComplete->mIsGetDefinition)  || (autoComplete->mResolveType == BfResolveType_GetResultString)) &&
			(fieldDef->mFieldDeclaration != NULL) && (autoComplete->IsAutocompleteNode(fieldDef->mFieldDeclaration->mNameNode)))
		{
			for (int i = 0; i < (int)actualTypeDef->mFields.size(); i++)
			{
				auto actualFieldDef = actualTypeDef->mFields[i];
				if (actualFieldDef->mName == fieldDef->mName)
				{
					if (autoComplete->mIsGetDefinition)
					{
						autoComplete->mDefType = actualTypeDef;
						autoComplete->mDefField = actualFieldDef;

						autoComplete->SetDefinitionLocation(fieldDef->mFieldDeclaration->mNameNode);
						autoComplete->mInsertStartIdx = fieldDef->mFieldDeclaration->mNameNode->GetSrcStart();
						autoComplete->mInsertEndIdx = fieldDef->mFieldDeclaration->mNameNode->GetSrcEnd();
					}
					else if (autoComplete->mResolveType == BfResolveType_GetResultString)
					{
						auto fieldInstance = &typeInst->mFieldInstances[actualFieldDef->mIdx];						
						if (fieldInstance->mConstIdx != -1)
						{													
							auto constant = typeInst->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
							auto retVal = module->ConstantToCurrent(constant, typeInst->mConstHolder, fieldInstance->mResolvedType);
							BfTypedValue typedValue = BfTypedValue(retVal, fieldInstance->mResolvedType);
							autoComplete->CheckResult(fieldDef->GetRefNode(), typedValue);							
						}
					}
					break;
				}
			}			
		}

		if ((fieldDef->mFieldDeclaration != NULL) && (fieldDef->mFieldDeclaration->mAttributes != NULL))
		{
			auto customAttrs = module->GetCustomAttributes(fieldDef->mFieldDeclaration->mAttributes, BfAttributeTargets_Field);
			delete customAttrs;
		}

		if (fieldDef->mIsConst)
		{			
			module->ResolveConstField(typeInst, NULL, fieldDef);			
		}
		
		if (fieldDef->mInitializer == NULL)
		{
			if (BfNodeIsA<BfVarTypeReference>(fieldDef->mTypeRef))
			{
				if (fieldDef->mInitializer == NULL)
				{
					if ((fieldDef->mTypeRef->IsA<BfVarTypeReference>()) || (fieldDef->mTypeRef->IsA<BfLetTypeReference>()))
						mPassInstance->Fail("Implicitly-typed fields must be initialized", fieldDef->GetRefNode());
				}
			}
		}
	}
	
	auto checkTypeDef = tempTypeDef;
	while (checkTypeDef != NULL)
	{
		for (auto baseType : checkTypeDef->mBaseTypes)
		{
			autoComplete->CheckTypeRef(baseType, false);
			module->ResolveTypeRef(baseType);
		}
		checkTypeDef = checkTypeDef->mOuterType;
	}
	
 	for (auto propDef : tempTypeDef->mProperties)
	{		
		if ((propDef->mFieldDeclaration != NULL) && (propDef->mFieldDeclaration->mAttributes != NULL))
		{
			auto customAttrs = module->GetCustomAttributes(propDef->mFieldDeclaration->mAttributes, BfAttributeTargets_Property);
			delete customAttrs;
		}
		
		auto propDeclaration = BfNodeDynCast<BfPropertyDeclaration>(propDef->mFieldDeclaration);
		if (propDeclaration != NULL)
			autoComplete->CheckProperty(propDeclaration);
		module->ResolveTypeRef(propDef->mTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);

		if ((autoComplete->mIsGetDefinition) && (propDef->mFieldDeclaration != NULL) && (autoComplete->IsAutocompleteNode(propDef->mFieldDeclaration->mNameNode)))
		{
			auto checkType = typeInst;
			while (checkType != NULL)
			{
				for (auto checkProp : checkType->mTypeDef->mProperties)
				{
					if (checkProp->mName == propDef->mName)
					{
						auto checkPropDeclaration = BfNodeDynCast<BfPropertyDeclaration>(checkProp->mFieldDeclaration);
						if ((checkPropDeclaration->mVirtualSpecifier == NULL) || (checkPropDeclaration->mVirtualSpecifier->GetToken() == BfToken_Virtual))
						{
							autoComplete->SetDefinitionLocation(checkPropDeclaration->mNameNode);
							autoComplete->mDefType = checkType->mTypeDef;
							autoComplete->mDefProp = checkProp;
							checkType = NULL;
							break;
						}
					}
				}

				if (checkType != NULL)
					checkType = checkType->mBaseType;
			}			
		}
	}
	
	Array<BfMethodInstance*> methodInstances;	

	for (auto methodDef : tempTypeDef->mMethods)
	{
		auto methodDeclaration = methodDef->GetMethodDeclaration();
		if (methodDeclaration != NULL)
			autoComplete->CheckMethod(methodDeclaration, false);

		if (!methodDef->mWantsBody)
		{			
			if (methodDeclaration != NULL)
			{
				if (methodDeclaration->mAttributes != NULL)
				{
					auto customAttrs = module->GetCustomAttributes(methodDeclaration->mAttributes, (methodDef->mMethodType == BfMethodType_Ctor) ? BfAttributeTargets_Constructor : BfAttributeTargets_Method);
					delete customAttrs;
				}
			}
			else if (auto methodPropertyDeclaration = methodDef->GetPropertyMethodDeclaration())
			{
				if (methodPropertyDeclaration->mAttributes != NULL)
				{					
					auto customAttrs = module->GetCustomAttributes(methodPropertyDeclaration->mAttributes, BfAttributeTargets_Method);
					delete customAttrs;					
				}				
			}
			
			continue;
		}

		BP_ZONE("ProcessAutocompleteTempType.CheckMethod");		
		BfMethodInstanceGroup methodInstanceGroup;
		methodInstanceGroup.mOwner = typeInst;		
		methodInstanceGroup.mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;

		BfMethodInstance* methodInstance = new BfMethodInstance();
		methodInstances.push_back(methodInstance);
		methodInstance->mMethodDef = methodDef;
		methodInstance->mMethodInstanceGroup = &methodInstanceGroup;
		methodInstance->mIsAutocompleteMethod = true;
		
		for (int genericParamIdx = 0; genericParamIdx < (int)methodDef->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParamType = module->GetGenericParamType(BfGenericParamKind_Method, genericParamIdx);
			methodInstance->GetMethodInfoEx()->mMethodGenericArguments.push_back(genericParamType);

			auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, genericParamIdx);			
			methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
		}

		for (int externConstraintIdx = 0; externConstraintIdx < (int)methodDef->mExternalConstraints.size(); externConstraintIdx++)
		{
			auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, externConstraintIdx + (int)methodDef->mGenericParams.size());
			methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
		}

		SetAndRestoreValue<BfFilePosition> prevFilePos(module->mCurFilePosition);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInst(module->mCurMethodInstance, methodInstance);		
 		module->DoMethodDeclaration(methodDeclaration, true);
		module->mIncompleteMethodCount++;		
		module->ProcessMethod(methodInstance);		
		
		if (methodInstance->mIRFunction)
		{
			BfLogSysM("Autocomplete removing IRFunction %d\n", methodInstance->mIRFunction.mId);
			module->mBfIRBuilder->Func_DeleteBody(methodInstance->mIRFunction);
			module->mBfIRBuilder->Func_EraseFromParent(methodInstance->mIRFunction);			
		}
	}
		
	if ((mResolvePassData->mAutoComplete->mDefType == actualTypeDef) && (mResolvePassData->mAutoComplete->mDefMethod != NULL))
	{		
		BfMethodDef* tempDefMethod = NULL;
		for (auto checkMethod : tempTypeDef->mMethods)
		{
			if (checkMethod == mResolvePassData->mAutoComplete->mDefMethod)
				tempDefMethod = checkMethod;
		}

		if (tempDefMethod != NULL)
		{
			BfMethodDef* actualReplaceMethodDef = NULL;
			for (auto checkMethodDef : actualTypeDef->mMethods)
			{
				if ((checkMethodDef->mMethodType == tempDefMethod->mMethodType) &&
					(checkMethodDef->mMethodDeclaration != NULL) && (tempDefMethod->mMethodDeclaration != NULL) &&
					(checkMethodDef->mMethodDeclaration->GetSrcStart() == tempDefMethod->mMethodDeclaration->GetSrcStart()))
					actualReplaceMethodDef = checkMethodDef;
			}

			if (actualReplaceMethodDef == NULL)
			{
				autoComplete->mDefType = NULL;
				autoComplete->mDefField = NULL;
				autoComplete->mDefProp = NULL;
				autoComplete->mReplaceLocalId = -1;
				autoComplete->mDefMethod = NULL;
			}
			else
				autoComplete->mDefMethod = actualReplaceMethodDef;
		}
	}

	if (hadTempExtensionInfo)
	{
		auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
		genericTypeInst->mGenericExtensionInfo->mExtensionMap.Remove(tempTypeDef);
	}

	for (auto checkNode : mResolvePassData->mExteriorAutocompleteCheckNodes)
	{
		BP_ZONE("ProcessAutocompleteTempType.CheckIdentifier");		

		bool isUsingDirective = false;
		BfIdentifierNode* checkIdentifier = NULL;
		if (auto usingDirective = BfNodeDynCast<BfUsingDirective>(checkNode))
		{
			isUsingDirective = true;
			checkIdentifier = usingDirective->mNamespace;
		}
		else
			checkIdentifier = BfNodeDynCast<BfIdentifierNode>(checkNode);
		mResolvePassData->mAutoComplete->CheckIdentifier(checkIdentifier, false, isUsingDirective);
	}		

	GenerateAutocompleteInfo();

	for (auto methodInstance : methodInstances)
		delete methodInstance;
	methodInstances.Clear();		
	
	module->CleanupFileInstances();	
	module->ClearConstData();

	BfLogSysM("ProcessAutocompleteTempType end\n");
}

BfType* BfCompiler::CheckSymbolReferenceTypeRef(BfModule* module, BfTypeReference* typeRef)
{
	//auto resolvedType = module->ResolveTypeRef(typeRef, BfPopulateType_Declaration, 
		//(BfResolveTypeRefFlags)(BfResolveTypeRefFlag_AllowRef | BfResolveTypeRefFlag_AllowGenericMethodParamConstValue | BfResolveTypeRefFlag_AllowGenericTypeParamConstValue));
	auto resolvedType = module->ResolveTypeRef(typeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowRef);
	if ((resolvedType != NULL) && (resolvedType->IsTypeInstance()))
	{
		auto typeInst = resolvedType->ToTypeInstance();

		//TODO: Did we need this?
		//  The ResolveTypeRef call already does mResolvePassData->HandleTypeReference, so we were adding double entries
		//mResolvePassData->HandleTypeReference(typeRef, typeInst->mTypeDef);
	}
	return resolvedType;
}

void BfCompiler::AddToRebuildTypeList(BfTypeInstance* typeInst, HashSet<BfTypeInstance*>& rebuildTypeInstList)
{
	if (mResolvePassData->mParser != NULL)
	{
		// Only find references within the current file
		if (!typeInst->mTypeDef->HasSource(mResolvePassData->mParser))
			return;
	}

	bool allowRebuild = ((!typeInst->IsGenericTypeInstance()) ||
		((typeInst->IsUnspecializedType()) && (!typeInst->IsUnspecializedTypeVariation())));
	if ((typeInst->IsClosure()) || (typeInst->IsConcreteInterfaceType()) || (typeInst->IsRetTypeType()))
		allowRebuild = false;
	if (allowRebuild)
		rebuildTypeInstList.Add(typeInst);
}

void BfCompiler::AddDepsToRebuildTypeList(BfTypeInstance* replaceTypeInst, HashSet<BfTypeInstance*>& rebuildTypeInstList)
{
	for (auto& dep : replaceTypeInst->mDependencyMap)
	{
		auto depType = dep.mKey;
		auto depTypeInst = depType->ToTypeInstance();
		if (depTypeInst == NULL)
			continue;

		AddToRebuildTypeList(depTypeInst, rebuildTypeInstList);		
	}	
}

void BfCompiler::GetSymbolReferences()
{
	BfLogSysM("GetSymbolReferences\n");

	if (mInInvalidState)
		return; // Don't even try

	auto context = mContext;
	if (context->mBfObjectType == NULL)
		return; // Not initialized yet
	auto module = context->mScratchModule;		

	if (mResolvePassData->mAutoComplete != NULL)
		mResolvePassData->mAutoComplete->SetModule(module);
	
	const char* strPtr = mResolvePassData->mQueuedReplaceTypeDef.c_str();	
	BfTypeDef* typeDef = mSystem->FindTypeDefEx(strPtr);
	if ((typeDef == NULL) || (typeDef->mTypeDeclaration == NULL))
		return;
	mResolvePassData->mSymbolReferenceTypeDef = typeDef;
	auto replaceType = module->ResolveTypeDef(typeDef, BfPopulateType_IdentityNoRemapAlias);
	module->PopulateType(replaceType);
	auto replaceTypeInst = replaceType->ToTypeInstance();

	HashSet<BfTypeInstance*> rebuildTypeInstList;
	if (mResolvePassData->mGetSymbolReferenceKind != BfGetSymbolReferenceKind_Local)	
	{
		AddDepsToRebuildTypeList(replaceTypeInst, rebuildTypeInstList);
		// For generic types, add all references from all specialized versions
		if (replaceTypeInst->IsGenericTypeInstance())
		{
			for (auto type : mContext->mResolvedTypes)
			{				
				auto typeInst = type->ToTypeInstance();
				if ((typeInst != replaceTypeInst) && (typeInst != NULL) && (typeInst->mTypeDef == typeDef))
					AddDepsToRebuildTypeList(typeInst, rebuildTypeInstList);
			}			
		}
	}
	
	AddToRebuildTypeList(replaceTypeInst, rebuildTypeInstList);

	//TODO: Did we need this to be rebuildTypeInst->mModule???  Why?
	//auto rebuildModule = rebuildTypeInst->mModule;
	auto rebuildModule = context->mScratchModule;

	auto _CheckAttributes = [&](BfAttributeDirective* attrib, BfTypeDef* declaringType)
	{
		if ((mResolvePassData->mGetSymbolReferenceKind != BfGetSymbolReferenceKind_Type) &&
			(mResolvePassData->mGetSymbolReferenceKind != BfGetSymbolReferenceKind_Field) &&
			(mResolvePassData->mGetSymbolReferenceKind != BfGetSymbolReferenceKind_Property))
			return;

		while (attrib != NULL)
		{
			String attrName = attrib->mAttributeTypeRef->ToString();
			BfType* attrType = NULL;

			BfAtomComposite nameComposite;
			if (mSystem->ParseAtomComposite(attrName + "Attribute", nameComposite))
			{
				BfTypeDef* attrTypeDef = module->FindTypeDefRaw(nameComposite, 0, replaceTypeInst, declaringType, NULL);
				if (attrTypeDef != NULL)
				{
					mResolvePassData->HandleTypeReference(attrib->mAttributeTypeRef, attrTypeDef);

					attrTypeDef->PopulateMemberSets();
					for (auto argExpr : attrib->mArguments)
					{
						if (auto assignExpr = BfNodeDynCast<BfAssignmentExpression>(argExpr))
						{
							auto propName = assignExpr->mLeft->ToString();
							BfMemberSetEntry* propDefEntry;
							if (attrTypeDef->mPropertySet.TryGetWith(propName, &propDefEntry))
							{
								mResolvePassData->HandlePropertyReference(assignExpr->mLeft, attrTypeDef, (BfPropertyDef*)propDefEntry->mMemberDef);
							}
							else if (attrTypeDef->mFieldSet.TryGetWith(propName, &propDefEntry))
							{
								mResolvePassData->HandleFieldReference(assignExpr->mLeft, attrTypeDef, (BfFieldDef*)propDefEntry->mMemberDef);
							}
						}
					}
				}
			}

			attrib = attrib->mNextAttribute;
		}
	};

	for (auto rebuildTypeInst : rebuildTypeInstList)
	{		
		auto context = mContext;
		auto module = context->mScratchModule;
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(module->mCurTypeInstance, rebuildTypeInst);
		SetAndRestoreValue<bool> prevIgnoreErrors(module->mIgnoreErrors, true);

		// Run through base types for type renames
		auto typeDef = rebuildTypeInst->mTypeDef;
		if ((typeDef->mTypeDeclaration != NULL) && (typeDef->mTypeDeclaration->mNameNode != NULL))
		{
			if (typeDef->mIsCombinedPartial)
			{
				for (auto checkTypeDef : typeDef->mPartials)
				{
					auto nameNode = checkTypeDef->mTypeDeclaration->mNameNode;
					if ((mResolvePassData->mParser == NULL) || (nameNode->IsFromParser(mResolvePassData->mParser)))
						mResolvePassData->HandleTypeReference(nameNode, typeDef);

					if (checkTypeDef->IsExtension())
					{
						if (mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Type)
						{
							BfTypeState typeState;
							typeState.mCurTypeDef = checkTypeDef;
							SetAndRestoreValue<BfTypeState*> prevTypeState(module->mContext->mCurTypeState, &typeState);	

							for (auto baseTypeRef : checkTypeDef->mBaseTypes)
								CheckSymbolReferenceTypeRef(module, baseTypeRef);

							for (auto genericParam : checkTypeDef->mGenericParamDefs)
							{
								for (auto constraint : genericParam->mConstraints)
								{
									if (auto constraintTypeRef = BfNodeDynCast<BfTypeReference>(constraint))
									{
										module->ResolveTypeRef(constraintTypeRef, BfPopulateType_Identity);
									}
									else if (auto opConstraint = BfNodeDynCast<BfGenericOperatorConstraint>(constraint))
									{
										module->ResolveTypeRef(opConstraint->mLeftType, BfPopulateType_Identity);
										module->ResolveTypeRef(opConstraint->mRightType, BfPopulateType_Identity);
									}
								}
							}
						}
					}
				}
			}
			else
			{				
				mResolvePassData->HandleTypeReference(typeDef->mTypeDeclaration->mNameNode, typeDef);
			}
		}

		if (!typeDef->mPartials.IsEmpty())
		{
			for (auto partialDef : typeDef->mPartials)
			{
				if ((partialDef->mTypeDeclaration != NULL) && (partialDef->mTypeDeclaration->mAttributes != NULL))
					_CheckAttributes(partialDef->mTypeDeclaration->mAttributes, typeDef);
			}
		}
		else
		{
			if ((typeDef->mTypeDeclaration != NULL) && (typeDef->mTypeDeclaration->mAttributes != NULL))
				_CheckAttributes(typeDef->mTypeDeclaration->mAttributes, typeDef);
		}		

		if (auto typeAliasDeclaration = BfNodeDynCast<BfTypeAliasDeclaration>(typeDef->mTypeDeclaration))
		{
			CheckSymbolReferenceTypeRef(module, typeAliasDeclaration->mAliasToType);
		}
		
		if (mResolvePassData != NULL)
		{
			if (rebuildTypeInst->IsGenericTypeInstance())
			{
				auto genericTypeInstance = (BfGenericTypeInstance*)rebuildTypeInst;

				for (int genericParamIdx = 0; genericParamIdx < (int)genericTypeInstance->mTypeGenericArguments.size(); genericParamIdx++)
				{
					BfGenericTypeParamInstance genericParamInstance(genericTypeInstance->mTypeDef, genericParamIdx);
					auto genericParamDef = typeDef->mGenericParamDefs[genericParamIdx];

					if (mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_TypeGenericParam)
					{
						for (auto nameNode : genericParamDef->mNameNodes)
							if (nameNode != NULL)
								mResolvePassData->HandleTypeGenericParam(nameNode, typeDef, genericParamIdx);
					}

					rebuildModule->ResolveGenericParamConstraints(&genericParamInstance, genericTypeInstance->IsGenericTypeInstance());
				}				
			}
		}
		

		if (mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Type)
		{
			for (auto baseTypeRef : typeDef->mBaseTypes)			
				CheckSymbolReferenceTypeRef(module, baseTypeRef);
		}

		BfTypeState typeState;
		SetAndRestoreValue<BfTypeState*> prevTypeState(module->mContext->mCurTypeState, &typeState);	

		if (mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Property)
		{
			for (auto propDef : typeDef->mProperties)
			{				
				BfPropertyDef* checkPropDef = propDef;
				BfTypeInstance* checkTypeInst = rebuildTypeInst;
				typeState.mCurTypeDef = propDef->mDeclaringType;
				module->GetBasePropertyDef(checkPropDef, checkTypeInst);
				if (propDef->mFieldDeclaration != NULL)
					mResolvePassData->HandlePropertyReference(propDef->mFieldDeclaration->mNameNode, checkTypeInst->mTypeDef, checkPropDef);
			}
		}

		if (mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field)
		{			
			for (auto fieldDef : typeDef->mFields)
			{
				if (fieldDef->mFieldDeclaration != NULL)
				{
					typeState.mCurTypeDef = fieldDef->mDeclaringType;
					mResolvePassData->HandleFieldReference(fieldDef->mFieldDeclaration->mNameNode, typeDef, fieldDef);
				}
			}
		}
		
		for (auto& fieldInst : rebuildTypeInst->mFieldInstances)
		{
			auto fieldDef = fieldInst.GetFieldDef();
			if (fieldDef != NULL)
			{
				typeState.mCurTypeDef = fieldDef->mDeclaringType;
				if (fieldDef->mTypeRef != NULL)
					CheckSymbolReferenceTypeRef(module, fieldDef->mTypeRef);

				if ((fieldDef->mIsConst) && (fieldDef->mInitializer != NULL))
				{
					BfConstResolver constResolver(module); 
					constResolver.Resolve(fieldDef->mInitializer);
				}

				if ((fieldDef->mFieldDeclaration != NULL) && (fieldDef->mFieldDeclaration->mAttributes != NULL))
					_CheckAttributes(fieldDef->mFieldDeclaration->mAttributes, fieldDef->mDeclaringType);
			}
		}

		for (auto& propDef : rebuildTypeInst->mTypeDef->mProperties)
		{
			typeState.mCurTypeDef = propDef->mDeclaringType;
			if (propDef->mTypeRef != NULL)
				CheckSymbolReferenceTypeRef(module, propDef->mTypeRef);
		}		
		
		if (rebuildModule == NULL)
			continue;
		rebuildModule->EnsureIRBuilder();
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance2(rebuildModule->mCurTypeInstance, rebuildTypeInst);

		for (auto& methodInstGroup : rebuildTypeInst->mMethodInstanceGroups)
		{
			// Run through all methods 
			bool isDefault = true;
			
			BfMethodInstanceGroup::MapType::iterator methodItr;
			if (methodInstGroup.mMethodSpecializationMap != NULL)
				methodItr = methodInstGroup.mMethodSpecializationMap->begin();			
			while (true)
			{
				BfMethodInstance* rebuildMethodInstance;
				if (isDefault)
				{
					rebuildMethodInstance = methodInstGroup.mDefault;
					if (rebuildMethodInstance == NULL)
						break;
					isDefault = false;
				}
				else
				{
					//TODO: Why did we process specialized methods?
					//  This caused renaming of types picking up 'T' usage from generic methods
					break;

// 					if (methodInstGroup.mMethodSpecializationMap == NULL)
// 						break;
// 					if (methodItr == methodInstGroup.mMethodSpecializationMap->end())
// 						break;
// 					rebuildMethodInstance = methodItr->mValue;
// 					++methodItr;
				}
				
				if ((rebuildMethodInstance->mIsUnspecializedVariation) || (rebuildMethodInstance->IsSpecializedGenericMethod()))
					continue;				
									
				SetAndRestoreValue<BfMethodInstance*> prevTypeInstance(rebuildModule->mCurMethodInstance, rebuildMethodInstance);
				auto methodDef = rebuildMethodInstance->mMethodDef;
				auto methodDeclaration = methodDef->GetMethodDeclaration();

				typeState.mCurTypeDef = methodDef->mDeclaringType;
								
				if ((methodDeclaration != NULL) && (methodDeclaration->mAttributes != NULL))
					_CheckAttributes(methodDeclaration->mAttributes, methodDef->mDeclaringType);				

				if ((mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Type) ||
					(mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_MethodGenericParam) ||
					(mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_TypeGenericParam))
				{					
					if (methodDef->mExplicitInterface != NULL)
						CheckSymbolReferenceTypeRef(rebuildModule, methodDef->mExplicitInterface);

					for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)
					{
						auto param = methodDef->mParams[paramIdx];
						CheckSymbolReferenceTypeRef(rebuildModule, param->mTypeRef);
					}
					if (methodDef->mReturnTypeRef != NULL)
						CheckSymbolReferenceTypeRef(rebuildModule, methodDef->mReturnTypeRef);
				}

				if (rebuildMethodInstance->mIgnoreBody)
				{
					auto methodDeclaration = methodDef->GetMethodDeclaration();
					if (methodDeclaration != NULL)
						mResolvePassData->HandleMethodReference(methodDeclaration->mNameNode, typeDef, methodDef);

					for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)					
					{
						auto param = methodDef->mParams[paramIdx];
						if (param->mParamDeclaration != NULL)
						{
							if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(param->mParamDeclaration->mNameNode))
								mResolvePassData->HandleLocalReference(identifierNode, rebuildTypeInst->mTypeDef, rebuildMethodInstance->mMethodDef, paramIdx + 1);
							else if (auto tupleExprNode = BfNodeDynCast<BfTupleExpression>(param->mParamDeclaration->mNameNode))
							{
								for (int fieldIdx = 0; fieldIdx < (int)tupleExprNode->mValues.size(); fieldIdx++)
								{
									if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(tupleExprNode->mValues[fieldIdx]))
										mResolvePassData->HandleLocalReference(identifierNode, rebuildTypeInst->mTypeDef, rebuildMethodInstance->mMethodDef, paramIdx + 1);
								}
							}
						}
					}
				}
				else
				{
					for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)					
					{
						auto param = methodDef->mParams[paramIdx];						
						if ((param->mParamDeclaration != NULL) && (param->mParamDeclaration->mInitializer != NULL))
						{
							auto paramType = rebuildMethodInstance->GetParamType(paramIdx);

							BfConstResolver constResolver(rebuildModule); 
							constResolver.Resolve(param->mParamDeclaration->mInitializer, paramType);
						}
					}

					if (rebuildMethodInstance->mHasBeenProcessed)
					{
						if (rebuildMethodInstance->mIRFunction)
							rebuildModule->mBfIRBuilder->Func_DeleteBody(rebuildMethodInstance->mIRFunction);							
						rebuildMethodInstance->mHasBeenProcessed = false;
						rebuildModule->mIncompleteMethodCount++;
					}
					else
					{
						if ((rebuildModule->mIncompleteMethodCount == 0) && (!rebuildModule->mIsScratchModule))
						{
							BF_FATAL("Shouldn't be processing this method");
						}
					}
					
					for (int genericParamIdx = 0; genericParamIdx < (int)rebuildMethodInstance->GetNumGenericArguments(); genericParamIdx++)
					{
						BfGenericMethodParamInstance genericParamInstance(rebuildMethodInstance->mMethodDef, genericParamIdx);
						auto genericParamDef = methodDef->mGenericParams[genericParamIdx];

						if (mResolvePassData != NULL)
						{
							for (auto nameNode : genericParamDef->mNameNodes)
								if (nameNode != NULL)
									mResolvePassData->HandleMethodGenericParam(nameNode, typeDef, methodDef, genericParamIdx);
						}

						rebuildModule->ResolveGenericParamConstraints(&genericParamInstance, rebuildMethodInstance->mIsUnspecialized);
					}

					for (int externConstraintIdx = 0; externConstraintIdx < (int)methodDef->mExternalConstraints.size(); externConstraintIdx++)
					{
						BfGenericMethodParamInstance genericParamInstance(rebuildMethodInstance->mMethodDef, externConstraintIdx + (int)methodDef->mGenericParams.size());
						auto& externConstraintDef = methodDef->mExternalConstraints[externConstraintIdx];
						CheckSymbolReferenceTypeRef(module, externConstraintDef.mTypeRef);
						rebuildModule->ResolveGenericParamConstraints(&genericParamInstance, rebuildMethodInstance->mIsUnspecialized);
					}

					rebuildModule->ProcessMethod(rebuildMethodInstance);
				}
			}
		}
	}
}

void BfCompiler::UpdateCompletion()
{
	if (mIsResolveOnly)
		return;

	float typeScale = 10.0f;
	float methodScale = 1.0f; 
	float queueModuleScale = 50.0f;
	float genModuleScale = 50.0f;

	mCodeGen.UpdateStats();

	BF_ASSERT(mCodeGen.mQueuedCount >= mCodeGen.mCompletionCount);
	BF_ASSERT(mStats.mModulesFinished <= mStats.mModulesStarted);
	BF_ASSERT(mCodeGen.mCompletionCount <= mStats.mModulesStarted);

	float numerator = ((mStats.mQueuedTypesProcessed * typeScale) + //(mStats.mMethodsProcessed * methodScale) + 
		(mStats.mModulesFinished * queueModuleScale) + (mCodeGen.mCompletionCount * genModuleScale));
	float divisor =   ((mStats.mTypesQueued          * typeScale) + //(mStats.mMethodsQueued *    methodScale) + 
		(mStats.mModulesStarted * queueModuleScale) + (mStats.mReifiedModuleCount * genModuleScale));
	float checkPct = 0;
	if (divisor > 0)
	{
		checkPct = numerator / divisor;
		BF_ASSERT(checkPct >= 0);
		if (checkPct > mCompletionPct)
			mCompletionPct = checkPct;
	}
	else
		mCompletionPct = 0;
	if (!mHadCancel)
		BF_ASSERT(mCompletionPct <= 1.0f);
	if (mCompletionPct > 1.0f)
		mCompletionPct = 1.0f;
}

void BfCompiler::MarkStringPool(BfModule* module)
{
	for (int stringId : module->mStringPoolRefs)
	{					
		BfStringPoolEntry& stringPoolEntry = module->mContext->mStringObjectIdMap[stringId];					
		stringPoolEntry.mLastUsedRevision = mRevision;
	}

	/*if (module->mOptModule != NULL)
		MarkStringPool(module->mOptModule);*/
	auto altModule = module->mNextAltModule;
	while (altModule != NULL)
	{
		MarkStringPool(altModule);
		altModule = altModule->mNextAltModule;
	}

	for (auto& specModulePair : module->mSpecializedMethodModules)
		MarkStringPool(specModulePair.mValue);
}

void BfCompiler::ClearUnusedStringPoolEntries()
{
	BF_ASSERT(!IsHotCompile());
	
	for (auto module : mContext->mModules)
	{			
		MarkStringPool(module);
	}

	for (auto itr = mContext->mStringObjectIdMap.begin(); itr != mContext->mStringObjectIdMap.end(); )
	{
		int strId = itr->mKey;
		BfStringPoolEntry& stringPoolEntry = itr->mValue;
		if (stringPoolEntry.mLastUsedRevision != mRevision)
		{				
			CompileLog("Clearing unused string: %d %s\n", itr->mKey, stringPoolEntry.mString.c_str());
			mContext->mStringObjectPool.Remove(stringPoolEntry.mString);
			itr = mContext->mStringObjectIdMap.Remove(itr);
		}
		else
			++itr;
	}		
}

void BfCompiler::ClearBuildCache()
{
	mCodeGen.ClearBuildCache();
	for (auto project : mSystem->mProjects)
	{
		String libPath = mOutputDirectory + "/" + project->mName + "/" + project->mName + "__.lib";
        BfpFile_Delete(libPath.c_str(), NULL);
	}
}

int BfCompiler::GetDynCastVDataCount()
{	
	int dynElements = 1 + mMaxInterfaceSlots;
	return ((dynElements * 4) + mSystem->mPtrSize - 1) / mSystem->mPtrSize;
}

bool BfCompiler::IsAutocomplete()
{
	return (mResolvePassData != NULL) && (mResolvePassData->mAutoComplete != NULL);
}

BfAutoComplete* BfCompiler::GetAutoComplete()
{
	if (mResolvePassData != NULL)
		return mResolvePassData->mAutoComplete;
	return NULL;
}

bool BfCompiler::IsHotCompile()
{
	return mOptions.mHotProject != NULL;
}

bool BfCompiler::IsSkippingExtraResolveChecks()
{
	return mIsResolveOnly && !mOptions.mExtraResolveChecks;
}

int BfCompiler::GetVTableMethodOffset()
{
	if (mOptions.mHasVDataExtender)
		return 1;
	return 0;
}

bool BfCompiler::DoWorkLoop(bool onlyReifiedTypes, bool onlyReifiedMethods)
{
	bool hadAnyWork = false;

	while (true)
	{
		bool didWork = false;		
		didWork |= mContext->ProcessWorkList(onlyReifiedTypes, onlyReifiedMethods);
		if (!didWork)
			break;
		hadAnyWork = true;
	}

	return hadAnyWork;
}

BfMangler::MangleKind BfCompiler::GetMangleKind()
{
	if (mOptions.mToolsetType == BfToolsetType_GNU)
		return BfMangler::MangleKind_GNU;
	return (mSystem->mPtrSize == 8) ? BfMangler::MangleKind_Microsoft_64 : BfMangler::MangleKind_Microsoft_32;
}

//////////////////////////////////////////////////////////////////////////


int ArrTest()
{
	//SizedArray<int, 8> intArr;
	//Array<int> intArr;
	//std::vector<int> intArr;
	BfSizedVector<int, 8> intArr;

	//int val = intArr.GetLastSafe();

	intArr.push_back(123);
	intArr.pop_back();
	intArr.push_back(234);
	
	intArr.push_back(345);
	//intArr.push_back(567);

	//auto itr = std::find(intArr.begin(), intArr.end(), 234);
	//intArr.erase(itr);		

	for (auto itr = intArr.begin(); itr != intArr.end(); )
	{
		if (*itr == 234)
			itr = intArr.erase(itr);
		else
			itr++;
	}

	return (int)intArr.size();
	//intArr.RemoveAt(2);
}

//////////////////////////////////////////////////////////////////////////

void BfCompiler::PopulateReified()
{
	BP_ZONE("PopulateReified");

	BfContext* context = mContext;
	bool hasTests = mSystem->HasTestProjects();

	Array<BfMethodInstance*> impChainHeadMethods;

	// Types can pull in new dependencies, so fully populate types until they stop
	bool reifiedOnly = mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude;
	while (true)
	{
		BP_ZONE("Compile_PopulateTypes");
		
		int startTypeInitCount = mTypeInitCount;

		bool didWork = false;

		BfLogSysM("PopulateReified iteration start\n");

		int typeCount = 0;
		for (auto type : context->mResolvedTypes)
		{			
			auto module = type->GetModule();
			typeCount++;

			if (module == NULL)
				continue;

			if (!type->IsReified())
			{
				// On compiles, only handle reified types in this loop.  This fixes cases where our first instance of a dependent type
				//  is found to be unreified and then we have to reify it later.  It's not an error, just a compile perf issue
				continue;
			}

			// We have to not populate generic type instances because that may force us to populate a type that SHOULD be deleted
			if ((type->IsIncomplete()) && (type->IsTypeInstance()) && (!type->IsGenericTypeInstance()))
			{
				mSystem->CheckLockYield();
				module->PopulateType(type, BfPopulateType_Full);
			}

			auto typeInst = type->ToTypeInstance();
			
			if ((typeInst != NULL) && (typeInst->IsGenericTypeInstance()) && (!typeInst->IsUnspecializedType()))
			{
				auto unspecializedType = module->GetUnspecializedTypeInstance(typeInst);
				if (!unspecializedType->mIsReified)
					unspecializedType->mIsReified = true;
			}
			
			// Check reifications forced by virtuals or interfaces
			if ((!mIsResolveOnly) && (typeInst != NULL) && (typeInst->mIsReified) && (typeInst->IsObject()) && (!typeInst->IsUnspecializedType())
				&& (typeInst->mHasBeenInstantiated) && (!typeInst->IsIncomplete()))
			{				
				// If we have chained methods, make sure we implement the chain members if the chain head is implemented and reified
				if (typeInst->mTypeDef->mIsCombinedPartial)
				{
					bool hasUnimpChainMembers = false;

					impChainHeadMethods.Clear();
					for (auto& methodInstanceGroup : typeInst->mMethodInstanceGroups)
					{
						auto methodInstance = methodInstanceGroup.mDefault;
						if (methodInstance == NULL)
							continue;
						if (methodInstance->mChainType == BfMethodChainType_ChainHead)
						{
							if (methodInstance->IsReifiedAndImplemented())
								impChainHeadMethods.Add(methodInstance);
						}
						else if (methodInstance->mChainType == BfMethodChainType_ChainMember)
						{
							if (!methodInstance->IsReifiedAndImplemented())
								hasUnimpChainMembers = true;
						}
						else if ((methodInstance->mChainType == BfMethodChainType_None) && (methodInstance->mMethodDef->IsDefaultCtor()))
						{
							if (!methodInstance->IsReifiedAndImplemented())
								hasUnimpChainMembers = true;
						}
					}

					if ((hasUnimpChainMembers) && (!impChainHeadMethods.IsEmpty()))
					{
						for (auto& methodInstanceGroup : typeInst->mMethodInstanceGroups)
						{
							auto methodInstance = methodInstanceGroup.mDefault;
							if (methodInstance == NULL)
								continue;

							bool forceMethod = false;
							if (methodInstance->mChainType == BfMethodChainType_ChainMember)
							{
								if (!methodInstance->IsReifiedAndImplemented())
								{
									for (auto impMethodInstance : impChainHeadMethods)
									{
										if (typeInst->mModule->CompareMethodSignatures(methodInstance, impMethodInstance))
										{
											forceMethod = true;
										}
									}
								}
							}
							else if (methodInstance->mMethodDef->IsDefaultCtor())
							{
								if (!methodInstance->IsReifiedAndImplemented())
									forceMethod = true;
							}

							if (forceMethod)
							{
								typeInst->mModule->GetMethodInstance(methodInstance->GetOwner(), methodInstance->mMethodDef, BfTypeVector(),
									(BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_UnspecializedPass));
							}
						}
					}
				}

				// If we have any virtual methods overrides that are unreified but the declaring virtual method is reified then we also need to reify
				for (auto&& vEntry : typeInst->mVirtualMethodTable)
				{
					if ((vEntry.mDeclaringMethod.mTypeInstance == NULL) ||
						(vEntry.mDeclaringMethod.mTypeInstance->IsIncomplete()) ||
						(vEntry.mImplementingMethod.mTypeInstance == NULL) ||
						(vEntry.mImplementingMethod.mTypeInstance->IsIncomplete()))
						continue;

					BfMethodInstance* declaringMethod = vEntry.mDeclaringMethod;
					if (declaringMethod == NULL)
						continue;

					if ((declaringMethod->mIsReified) && (declaringMethod->mMethodInstanceGroup->IsImplemented()))
					{
						BfMethodInstance* implMethod = vEntry.mImplementingMethod;
						if ((implMethod != NULL) && ((!implMethod->mMethodInstanceGroup->IsImplemented()) || (!implMethod->mIsReified)))
						{
							didWork = true;
							typeInst->mModule->GetMethodInstance(implMethod);
						}
					}
				}

				for (auto& ifaceTypeInst : typeInst->mInterfaces)
				{
					auto ifaceInst = ifaceTypeInst.mInterfaceType;
					int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
					int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();
					auto declTypeDef = ifaceTypeInst.mDeclaringType;

					for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)
					{
						auto ifaceMethodInst = ifaceInst->mMethodInstanceGroups[iMethodIdx].mDefault;
						if ((ifaceMethodInst == NULL) || (!ifaceMethodInst->IsReifiedAndImplemented()))
							continue;

						auto implMethodRef = &typeInst->mInterfaceMethodTable[iMethodIdx + startIdx].mMethodRef;
						BfMethodInstance* implMethod = *implMethodRef;
						if (implMethod == NULL)
							continue;
						if (!implMethod->IsReifiedAndImplemented())
						{
							didWork = true;
							typeInst->mModule->GetMethodInstance(implMethod);
						}
					}
				}
			}
		}

		BfLogSysM("PopulateReified iteration done\n");

		didWork |= DoWorkLoop(reifiedOnly, reifiedOnly);
		if (reifiedOnly)
			didWork |= DoWorkLoop(false, reifiedOnly);

		if (startTypeInitCount != mTypeInitCount)
			didWork = true;

		if (didWork)
			continue;

		// We get everything on the first pass through
		if (mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
			break;

		if (mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_SkipUnused)
			break;

		if (startTypeInitCount == mTypeInitCount)
			break;
	}
}

void BfCompiler::HotCommit()
{
	if (mHotState == NULL)
		return;

	mHotState->mCommittedHotCompileIdx = mOptions.mHotCompileIdx;

	for (auto type : mContext->mResolvedTypes)
	{		
		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;
		if (typeInst->mHotTypeData == NULL)
			continue; 

		for (int typeIdx = (int)typeInst->mHotTypeData->mTypeVersions.size() - 1; typeIdx >= 0; typeIdx--)
		{
			auto hotVersion = typeInst->mHotTypeData->mTypeVersions[typeIdx];
			if (hotVersion->mCommittedHotCompileIdx != -1)
				break;
			hotVersion->mCommittedHotCompileIdx = mHotState->mCommittedHotCompileIdx;
			if ((!hotVersion->mInterfaceMapping.IsEmpty()) && (typeIdx > 0))
			{
				auto hotVersionHead = typeInst->mHotTypeData->GetLatestVersionHead();
				if ((hotVersionHead != hotVersion) && (hotVersionHead->mDataHash == hotVersion->mDataHash))
				{
					// When we have a slot failure, the data hash will match but we actually do need to use the new mInterfaceMapping entries
					//  So we copy them over to the 
					hotVersionHead->mInterfaceMapping = hotVersion->mInterfaceMapping;
				}				
			}
		}
	}
}

void BfCompiler::HotResolve_Start(HotResolveFlags flags)
{
	BfLogSysM("BfCompiler::HotResolve_Start\n");

	delete mHotResolveData;
	mHotResolveData = new HotResolveData();
	mHotResolveData->mFlags = flags;
	
	mHotResolveData->mHotTypeIdFlags.Resize(mCurTypeId);
	mHotResolveData->mReasons.Resize(mCurTypeId);

	if ((mHotResolveData->mFlags & HotResolveFlag_HadDataChanges) != 0)
	{
		HotResolve_AddReachableMethod("BfCallAllStaticDtors");

		for (auto& kv : mHotData->mFuncPtrs)
		{
			auto funcRef = kv.mValue;
			HotResolve_AddReachableMethod(funcRef->mMethod, HotTypeFlag_FuncPtr, true);
		}
	}
}

bool BfCompiler::HotResolve_AddReachableMethod(BfHotMethod* hotMethod, HotTypeFlags flags, bool devirtualized, bool forceProcess)
{
	HotReachableData* hotReachableData;
	if (mHotResolveData->mReachableMethods.TryAdd(hotMethod, NULL, &hotReachableData))
	{
		hotMethod->mRefCount++;
	}
	else
	{		
		hotReachableData->mTypeFlags = (HotTypeFlags)(hotReachableData->mTypeFlags | flags);
		if ((!devirtualized) && (!hotReachableData->mHadNonDevirtualizedCall))
		{
			hotReachableData->mHadNonDevirtualizedCall = true;
			if (!forceProcess)
				return true;
		}
		if (!forceProcess)
			return false;
	}
	hotReachableData->mTypeFlags = (HotTypeFlags)(hotReachableData->mTypeFlags | flags);
	if (!devirtualized)
		hotReachableData->mHadNonDevirtualizedCall = true;
	
	for (auto hotDepData : hotMethod->mReferences)
	{
		if (hotDepData->mDataKind == BfHotDepDataKind_ThisType)
		{
			auto hotThisType = (BfHotThisType*)hotDepData;
			auto hotTypeVersion = hotThisType->mTypeVersion;
			
			HotTypeFlags hotTypeFlags = mHotResolveData->mHotTypeIdFlags[hotTypeVersion->mTypeId];
			bool isAllocated = (hotTypeFlags & (HotTypeFlag_Heap | HotTypeFlag_CanAllocate)) != 0;
			if (!isAllocated)
			{
				if (mHotResolveData->mDeferredThisCheckMethods.Add(hotMethod))
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			else
			{
				mHotResolveData->mDeferredThisCheckMethods.Remove(hotMethod);
			}
		}
		else if (hotDepData->mDataKind == BfHotDepDataKind_Allocation)
		{
			auto hotAllocation = (BfHotAllocation*)hotDepData;
			auto hotTypeVersion = hotAllocation->mTypeVersion;
			HotResolve_ReportType(hotTypeVersion, flags, hotMethod);
			HotResolve_ReportType(hotTypeVersion, HotTypeFlag_CanAllocate, hotMethod);			
		}
		else if (hotDepData->mDataKind == BfHotDepDataKind_TypeVersion)
		{
			auto hotTypeVersion = (BfHotTypeVersion*)hotDepData;			
			HotResolve_ReportType(hotTypeVersion, flags, hotMethod);
		}
		else if (hotDepData->mDataKind == BfHotDepDataKind_Method)
		{
			auto checkMethod = (BfHotMethod*)hotDepData;
			HotResolve_AddReachableMethod(checkMethod, flags, false);
		}
		else if (hotDepData->mDataKind == BfHotDepDataKind_DevirtualizedMethod)
		{
			auto checkMethod = (BfHotDevirtualizedMethod*)hotDepData;
			HotResolve_AddReachableMethod(checkMethod->mMethod, flags, true);
		}		
		else if (hotDepData->mDataKind == BfHotDepDataKind_DupMethod)
		{
			auto checkMethod = (BfHotDupMethod*)hotDepData;
			HotResolve_AddReachableMethod(checkMethod->mMethod, flags, true);
		}
	}
	return true;
}

void BfCompiler::HotResolve_AddReachableMethod(const StringImpl& methodName)
{
	BfLogSysM("HotResolve_AddReachableMethod %s\n", methodName.c_str());

	String mangledName = methodName;

	BfHotMethod** hotMethodPtr;
	if (!mHotData->mMethodMap.TryGetValue(mangledName, &hotMethodPtr))
	{
		BfLogSysM("Hot method not found\n");
		return;
	}

	BfHotMethod* hotMethod = *hotMethodPtr;
	while (hotMethod->mPrevVersion != NULL)
	{
		if (hotMethod->mSrcTypeVersion->mCommittedHotCompileIdx != -1)
			break;
		hotMethod = hotMethod->mPrevVersion;
	}

	HotResolve_AddReachableMethod(hotMethod, HotTypeFlag_ActiveFunction, true);
}

void BfCompiler::HotResolve_AddActiveMethod(BfHotMethod* hotMethod)
{
	if (mHotResolveData->mActiveMethods.Add(hotMethod))
	{
		hotMethod->mRefCount++;
	}

	// We don't need to mark reachable methods unless we had data changes
	if ((mHotResolveData->mFlags & HotResolveFlag_HadDataChanges) != 0)
	{
		HotResolve_AddReachableMethod(hotMethod, HotTypeFlag_ActiveFunction, true);
	}

	if ((hotMethod->mFlags & BfHotDepDataFlag_HasDup) != 0)
	{
		for (auto depData : hotMethod->mReferences)
		{			
			if (depData->mDataKind != BfHotDepDataKind_DupMethod)
				continue;
			auto hotDupMethod = (BfHotDupMethod*)depData;
			HotResolve_AddActiveMethod(hotDupMethod->mMethod);
		}
	}	
}

void BfCompiler::HotResolve_AddActiveMethod(const StringImpl& methodName)
{
	BfLogSysM("HotResolve_AddActiveMethod %s\n", methodName.c_str());

	String mangledName;
	int hotCompileIdx = 0;
	
	int tabIdx = (int)methodName.IndexOf('\t');
	if (tabIdx != -1)
	{
		mangledName = methodName.Substring(0, tabIdx);
		hotCompileIdx = atoi(methodName.c_str() + tabIdx + 1);
	}
	else
		mangledName = methodName;
	
	bool isDelegateRef = false;	
	BfHotMethod** hotMethodPtr;
	if (!mHotData->mMethodMap.TryGetValue(mangledName, &hotMethodPtr))
	{
		BfLogSysM("Hot method not found\n");
		return;
	}

	BfHotMethod* hotMethod = *hotMethodPtr;
	while (hotMethod->mPrevVersion != NULL)
	{
		if ((hotMethod->mSrcTypeVersion->mCommittedHotCompileIdx != -1) && (hotCompileIdx < hotMethod->mSrcTypeVersion->mCommittedHotCompileIdx))
			break;
		hotMethod = hotMethod->mPrevVersion;
	}

	HotResolve_AddActiveMethod(hotMethod);
}

void BfCompiler::HotResolve_AddDelegateMethod(const StringImpl& methodName)
{
	BfLogSysM("HotResolve_HotResolve_AddDelegateMethod %s\n", methodName.c_str());

	String mangledName = methodName;

	BfHotMethod** hotMethodPtr;
	if (!mHotData->mMethodMap.TryGetValue(mangledName, &hotMethodPtr))
	{
		BfLogSysM("Hot method not found\n");
		return;
	}
	
	BfHotMethod* hotMethod = *hotMethodPtr;
	HotResolve_AddReachableMethod(hotMethod, HotTypeFlag_Delegate, true);
}

void BfCompiler::HotResolve_ReportType(BfHotTypeVersion* hotTypeVersion, HotTypeFlags flags, BfHotDepData* reason)
{	
	auto& flagsRef = mHotResolveData->mHotTypeFlags[hotTypeVersion];
	if (flagsRef == (flagsRef | flags))
		return;
	flagsRef = (HotTypeFlags)(flags | flagsRef);

	bool applyFlags = true;
	if ((flags & (BfCompiler::HotTypeFlag_ActiveFunction | BfCompiler::HotTypeFlag_Delegate | BfCompiler::HotTypeFlag_FuncPtr)) != 0)
	{		
		applyFlags = (hotTypeVersion->mCommittedHotCompileIdx != -1) && (mHotState->mPendingDataChanges.Contains(hotTypeVersion->mTypeId));

		if ((!applyFlags) && (hotTypeVersion->mCommittedHotCompileIdx != -1))
			applyFlags = mHotState->mPendingFailedSlottings.Contains(hotTypeVersion->mTypeId);

		if (applyFlags)
		{
			if (reason != NULL)
				mHotResolveData->mReasons[hotTypeVersion->mTypeId] = reason;
		}
	}
	if (applyFlags)
	{		
		auto& flagsIdRef = mHotResolveData->mHotTypeIdFlags[hotTypeVersion->mTypeId];
		flagsIdRef = (HotTypeFlags)(flags | flagsIdRef);		
	}
	
	BfLogSysM("HotResolve_ReportType %p %s Flags:%X DeclHotIdx:%d\n", hotTypeVersion, mContext->TypeIdToString(hotTypeVersion->mTypeId).c_str(), flags, hotTypeVersion->mDeclHotCompileIdx);
	
	for (auto member : hotTypeVersion->mMembers)
	{
		HotResolve_ReportType(member, flags, reason);
	}	
}

void BfCompiler::HotResolve_ReportType(int typeId, HotTypeFlags flags)
{	
	if ((uint)typeId >= mHotResolveData->mHotTypeIdFlags.size())
	{
		BF_DBG_FATAL("Invalid typeId");
		return;
	}

	if (mHotResolveData->mHotTypeIdFlags[typeId] == (mHotResolveData->mHotTypeIdFlags[typeId] | flags))
		return;

	auto hotTypeData = mContext->GetHotTypeData(typeId);
	if (hotTypeData != NULL)
	{
		auto hotTypeVersion = hotTypeData->GetTypeVersion(mHotState->mCommittedHotCompileIdx);
		BF_ASSERT(hotTypeVersion != NULL);
		if (hotTypeVersion != NULL)
			HotResolve_ReportType(hotTypeVersion, flags, NULL);
	}	

	mHotResolveData->mHotTypeIdFlags[typeId] = (HotTypeFlags)(flags | mHotResolveData->mHotTypeIdFlags[typeId]);
}

String BfCompiler::HotResolve_Finish()
{
	BfLogSysM("HotResolve_Finish\n");

	if (mHotState == NULL)
	{
		// It's possible we did a HotCompile with no file changes and therefore didn't actually do a compile
		return "";
	}

	String result;

	if ((mHotResolveData->mFlags & HotResolveFlag_HadDataChanges) != 0)
	{
		BF_ASSERT(!mHotState->mPendingDataChanges.IsEmpty() || !mHotState->mPendingFailedSlottings.IsEmpty());
	}
	else
	{
		BF_ASSERT(mHotState->mPendingDataChanges.IsEmpty() && mHotState->mPendingFailedSlottings.IsEmpty());
	}
	
	if ((mHotResolveData->mFlags & HotResolveFlag_HadDataChanges) != 0)
	{
		auto _AddUsedType = [&](BfTypeDef* typeDef)
		{
			auto type = mContext->mUnreifiedModule->ResolveTypeDef(mReflectTypeInstanceTypeDef);
			if (type != NULL)
				HotResolve_ReportType(type->mTypeId, BfCompiler::HotTypeFlag_Heap);
		};		

		// We have some types that can be allocated in a read-only section- pretend they are on the heap
		_AddUsedType(mReflectTypeInstanceTypeDef);
		_AddUsedType(mStringTypeDef);

		// Find any virtual method overrides that may have been called.
		//  These can cause new reachable virtual methods to be called, which may take more than one iteration to fully resolve
		for (int methodPass = 0; true; methodPass++)
		{			
			bool didWork = false;
			
 			for (auto hotMethod : mHotResolveData->mDeferredThisCheckMethods)
 			{
				if (HotResolve_AddReachableMethod(hotMethod, BfCompiler::HotTypeFlag_ActiveFunction, true, true))
					didWork = true;
 			}

			HotTypeFlags typeFlags = HotTypeFlag_None;
			for (auto& kv : mHotData->mMethodMap)
			{
				String& methodName = kv.mKey;
				auto hotMethod = kv.mValue;
				
				bool doCall = false;
				bool forceAdd = false;				
								
				if (mHotResolveData->mReachableMethods.ContainsKey(hotMethod))
					continue;

				for (auto ref : hotMethod->mReferences)
				{
					if (ref->mDataKind == BfHotDepDataKind_ThisType)
						continue;
					if (ref->mDataKind != BfHotDepDataKind_VirtualDecl)
						break;

					auto hotVirtualDecl = (BfHotVirtualDeclaration*)ref;
					HotReachableData* hotReachableData;
					if (mHotResolveData->mReachableMethods.TryGetValue(hotVirtualDecl->mMethod, &hotReachableData))
					{
						if (hotReachableData->mHadNonDevirtualizedCall)
						{
							typeFlags = hotReachableData->mTypeFlags;
							doCall = true;
						}
					}
				}
			
				if (!doCall)
				{
					if ((hotMethod->mFlags & BfHotDepDataFlag_AlwaysCalled) != 0)
					{
						typeFlags = BfCompiler::HotTypeFlag_ActiveFunction;
						doCall = true;
					}
				}

				if (doCall)
				{
					if (HotResolve_AddReachableMethod(hotMethod, typeFlags, true, forceAdd))
						didWork = true;
				}
			}
			if (!didWork)
				break;
		}

		int errorCount = 0;
		for (int typeId = 0; typeId < (int)mHotResolveData->mHotTypeIdFlags.size(); typeId++)
		{
			auto flags = mHotResolveData->mHotTypeIdFlags[typeId];
			if (flags == 0)
				continue;

			auto type = mContext->mTypes[typeId];

			Dictionary<BfHotMethod*, String*> methodNameMap;

			if ((flags > BfCompiler::HotTypeFlag_UserNotUsed) && 
				((mHotState->mPendingDataChanges.Contains(typeId)) || (mHotState->mPendingFailedSlottings.Contains(typeId))))
			{
				bool isBadTypeUsed = false;
				if ((flags & HotTypeFlag_Heap) != 0)
					isBadTypeUsed = true;
				else if ((flags & (HotTypeFlag_ActiveFunction | HotTypeFlag_Delegate | HotTypeFlag_FuncPtr)) != 0)
				{
					// If we detect an old version being used, it's only an issue if this type can actually be allocated
					if ((flags & HotTypeFlag_CanAllocate) != 0)
					{						
						isBadTypeUsed = true;						
					}
				}

				if (isBadTypeUsed)
				{
					bool reasonIsActiveMethod = false;
					String methodReason;
					auto reason = mHotResolveData->mReasons[typeId];
					if ((reason != NULL) && (reason->mDataKind == BfHotDepDataKind_Method))
					{
						auto hotMethod = (BfHotMethod*)reason;
						reasonIsActiveMethod = mHotResolveData->mActiveMethods.Contains(hotMethod);

						if (methodNameMap.IsEmpty())
						{
							for (auto& kv : mHotData->mMethodMap)
							{
								auto hotMethod = kv.mValue;
								while (hotMethod != NULL)
								{
									methodNameMap[hotMethod] = &kv.mKey;
									hotMethod = hotMethod->mPrevVersion;
								}
							}
						}

						String** strPtr;
						if (methodNameMap.TryGetValue(hotMethod, &strPtr))
						{
							methodReason += BfDemangler::Demangle((**strPtr), DbgLanguage_Beef, BfDemangler::Flag_BeefFixed);
						}
					}

					errorCount++;
					if (errorCount >= 1000)
					{						
						result += "\n (more errors)...";
						break;
					}

					if (!result.IsEmpty())
						result += "\n";
					result += "'";
					result += mContext->TypeIdToString(typeId);
					result += "'";
					if ((flags & BfCompiler::HotTypeFlag_Heap) != 0)
						result += " allocated on the heap";					
					else if ((flags & BfCompiler::HotTypeFlag_ActiveFunction) != 0)
					{
						if (reasonIsActiveMethod)
							result += StrFormat(" used by active method '%s'", methodReason.c_str());
						else if (!methodReason.IsEmpty())
							result += StrFormat(" previous data version used by deleted method '%s', reachable by an active method", methodReason.c_str());
						else
							result += " previous data version used by a deleted method reachable by an active method";
					}
					else if ((flags & BfCompiler::HotTypeFlag_Delegate) != 0)
					{
						if (!methodReason.IsEmpty())
							result += StrFormat(" previous data version used by deleted method '%s', reachable by a delegate", methodReason.c_str());
						else
							result += " previous data version used by a deleted method reachable by a delegate";
					}
					else if ((flags & BfCompiler::HotTypeFlag_FuncPtr) != 0)
					{
						if (!methodReason.IsEmpty())
							result += StrFormat(" previous data version used by deleted method '%s', reachable by a function pointer", methodReason.c_str());
						else
							result += " previous data version used by a deleted method reachable by a function pointer";
					}
					else if ((flags & BfCompiler::HotTypeFlag_UserUsed) != 0)
						result += " stated as used by the program";
				}
			}

			String typeName = mContext->TypeIdToString(typeId);
			BfLogSysM("    %d %s %02X\n", typeId, typeName.c_str(), flags);
		}

		if (result.IsEmpty())
		{
			for (auto typeId : mHotState->mPendingDataChanges)
			{
				auto type = mContext->mTypes[typeId];
				auto typeInstance = type->ToTypeInstance();

				BF_ASSERT(typeInstance->mHotTypeData->mPendingDataChange);
				typeInstance->mHotTypeData->mPendingDataChange = false;
				typeInstance->mHotTypeData->mHadDataChange = true;
				typeInstance->mHotTypeData->mVTableOrigLength = -1;
				typeInstance->mHotTypeData->mOrigInterfaceMethodsLength = -1;				

				BfLogSysM("Pending data change applied to type %p\n", typeInstance);
			}

			mHotState->mPendingDataChanges.Clear();
			mHotState->mPendingFailedSlottings.Clear();			
		}		
	}

	ClearOldHotData();

	if ((mHotResolveData->mFlags & HotResolveFlag_HadDataChanges) != 0)
	{
		for (int pass = 0; pass < 2; pass++)
		{
			bool wantsReachable = pass == 0;
			Array<String> methodList;

			for (auto& kv : mHotData->mMethodMap)
			{
				auto hotMethod = kv.mValue;
				bool reachable = mHotResolveData->mReachableMethods.ContainsKey(hotMethod);

				if (reachable != wantsReachable)
					continue;

				String methodName;
				methodName += BfDemangler::Demangle(kv.mKey, DbgLanguage_Beef, BfDemangler::Flag_BeefFixed);
				methodName += " - ";
				methodName += kv.mKey;

				methodList.Add(methodName);
			}

			methodList.Sort([](const String& lhs, const String& rhs) { return lhs < rhs; });

			for (auto& methodName : methodList)
				BfLogSysM("%s: %s\n", wantsReachable ? "Reachable" : "Unreachable", methodName.c_str());
		}
	}

	delete mHotResolveData;
	mHotResolveData = NULL;
		
	return result;
}

void BfCompiler::ClearOldHotData()
{
	if (mHotData == NULL)
		return;

	// TODO: Get rid of old hot data during hot compiles, too
// 	if (IsHotCompile())
// 		return; 

	BP_ZONE("BfCompiler::ClearOldHotData");
	
	bool isHotCompile = IsHotCompile();

	auto itr = mHotData->mMethodMap.begin();
	while (itr != mHotData->mMethodMap.end())
	{
		String& methodName = itr->mKey;
		auto hotMethod = itr->mValue;				

		bool doDelete = false;

		// If a previous version of a method is not currently active then it should be impossible to ever reach it
		while (hotMethod->mPrevVersion != NULL)
		{
			auto prevMethod = hotMethod->mPrevVersion;
			if (prevMethod->mRefCount > 1)
			{
				BF_ASSERT((mHotResolveData != NULL) && (mHotResolveData->mActiveMethods.Contains(prevMethod)));
				break;
			}
			
			hotMethod->mPrevVersion = prevMethod->mPrevVersion;
			prevMethod->mPrevVersion = NULL;
			prevMethod->Deref();
		}

		BF_ASSERT(hotMethod->mRefCount >= 1);
		if (hotMethod->mPrevVersion == NULL)
		{			
			if (hotMethod->mRefCount <= 1)
			{
				doDelete = true;
			}
			else if ((!isHotCompile) && ((hotMethod->mFlags & (BfHotDepDataFlag_IsBound | BfHotDepDataFlag_RetainMethodWithoutBinding)) == 0))
			{
				doDelete = true;
			}
		}

		bool doRemove = doDelete;
		if ((hotMethod->mFlags & BfHotDepDataFlag_HasDup) != 0)
		{			
			bool hasDupMethod = false;			
			for (int idx = 0; idx < (int)hotMethod->mReferences.size(); idx++)
			{
				auto depData = hotMethod->mReferences[idx];
				if (depData->mDataKind == BfHotDepDataKind_DupMethod)
				{
					auto dupMethod = (BfHotDupMethod*)depData;
					if (doDelete)
					{
						doRemove = false;						
						dupMethod->mMethod->mRefCount++;
						itr->mValue = dupMethod->mMethod;
					}
					else
					{
						if ((dupMethod->mMethod->mRefCount == 1) || 
							((!IsHotCompile()) && (dupMethod->mMethod->mFlags & BfHotDepDataFlag_IsBound) == 0))
						{
							dupMethod->Deref();
							hotMethod->mReferences.RemoveAt(idx);
							idx--;
						}
					}
				}
			}
		}

		if (doDelete)
		{			
			BfLogSysM("Deleting hot method %p %s\n", hotMethod, methodName.c_str());
			//BF_ASSERT(hotMethod->mRefCount == 1);
			hotMethod->Clear();
			hotMethod->Deref();			
			if (doRemove)
				itr = mHotData->mMethodMap.Remove(itr);
		}				
		else
			++itr;
	}

	mHotData->ClearUnused(IsHotCompile());

	for (auto type : mContext->mResolvedTypes)
	{
		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;
		if (typeInst->mHotTypeData == NULL)
			continue;

		bool foundCommittedVersion = false;

		auto latestVersionHead = typeInst->mHotTypeData->GetLatestVersionHead();		
		for (int typeIdx = (int)typeInst->mHotTypeData->mTypeVersions.size() - 1; typeIdx >= 0; typeIdx--)
		{
			auto hotVersion = typeInst->mHotTypeData->mTypeVersions[typeIdx];
			if (hotVersion == latestVersionHead)
			{
				// We have to keep the latest version head -- otherwise we would lose vdata and interface mapping data
				continue;
			}

			if ((!foundCommittedVersion) && (mHotState != NULL) && (hotVersion->mDeclHotCompileIdx <= mHotState->mCommittedHotCompileIdx))
			{
				// Don't remove the latest committed version
				foundCommittedVersion = true;
			}
			else if (hotVersion->mRefCount == 1)
			{
				typeInst->mHotTypeData->mTypeVersions.RemoveAt(typeIdx);
				hotVersion->Deref();
				BF_ASSERT(typeInst->mHotTypeData->mTypeVersions.size() > 0);
			}
		}
	}
}

void BfCompiler::CompileReified()
{	
	BP_ZONE("Compile_ResolveTypeDefs");

	for (auto typeDef : mSystem->mTypeDefs)
	{
		mSystem->CheckLockYield();
		if (mCanceling)
		{
			BfLogSysM("Canceling from Compile typeDef loop\n");
			break;
		}

		if (typeDef->mProject->mDisabled)
			continue;

		if (typeDef->mIsPartial)
			continue;

		bool isAlwaysInclude = (typeDef->mIsAlwaysInclude) || (typeDef->mProject->mAlwaysIncludeAll);

		if (typeDef->mProject->IsTestProject())
		{
			for (auto methodDef : typeDef->mMethods)
			{
				auto methodDeclaration = methodDef->GetMethodDeclaration();
				if ((methodDeclaration != NULL) && (methodDeclaration->mAttributes != NULL) &&
					(methodDeclaration->mAttributes->Contains("Test")))
					isAlwaysInclude = true;
			}
		}

		//TODO: Just because the type is required doesn't mean we want to reify it. Why did we have that check?
		if ((mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude) && (!isAlwaysInclude))
			continue;

		auto scratchModule = mContext->mScratchModule;
		scratchModule->ResolveTypeDef(typeDef, BfPopulateType_Full);
	}
	
	if (mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude)
	{
		for (auto project : mSystem->mProjects)
		{
			String entryClassName = project->mStartupObject;
			auto typeDef = mSystem->FindTypeDef(entryClassName, 0, project);
			if (typeDef != NULL)
			{
				typeDef->mIsAlwaysInclude = true;
				auto resolvedType = mContext->mScratchModule->ResolveTypeDef(typeDef);
				if (resolvedType != NULL)
				{
					auto resolvedTypeInst = resolvedType->ToTypeInstance();
					if (resolvedTypeInst != NULL)
					{
						auto module = resolvedTypeInst->GetModule();
						if (!module->mIsReified)
							module->ReifyModule();
						mContext->mScratchModule->PopulateType(resolvedType, BfPopulateType_Full);

						BfMemberSetEntry* memberSetEntry;
						if (resolvedTypeInst->mTypeDef->mMethodSet.TryGetWith(String("Main"), &memberSetEntry))
						{
							BfMethodDef* methodDef = (BfMethodDef*)memberSetEntry->mMemberDef;
							while (methodDef != NULL)
							{
								auto moduleMethodInstance = mContext->mScratchModule->GetMethodInstanceAtIdx(resolvedTypeInst, methodDef->mIdx);
								auto methodInstance = moduleMethodInstance.mMethodInstance;
								if (methodInstance->GetParamCount() != 0)
								{
									mContext->mScratchModule->GetInternalMethod("CreateParamsArray");
									mContext->mScratchModule->GetInternalMethod("DeleteStringArray");
								}
								
								methodDef = methodDef->mNextWithSameName;
							}
						}						
					}
				}
			}
		}
	}

	PopulateReified();	
}

bool BfCompiler::DoCompile(const StringImpl& outputDirectory)
{
	BP_ZONE("BfCompiler::Compile");
	
	if (mSystem->mTypeDefs.mCount == 0)
	{
		// No-source bailout
		return true;
	}

	if (!mOptions.mErrorString.IsEmpty())
	{
		mPassInstance->Fail(mOptions.mErrorString);
		return false;
	}
	
	{
		String hotSwapErrors;
		String toolsetErrors;
		for (auto project : mSystem->mProjects)
		{
			if (project->mDisabled)
				continue;
			if (project->mCodeGenOptions.mLTOType != BfLTOType_None)
			{
				if (mOptions.mAllowHotSwapping)
				{
					if (!hotSwapErrors.IsEmpty())
						hotSwapErrors += ", ";
					hotSwapErrors += project->mName;					
				}
				if (mOptions.mToolsetType != BfToolsetType_LLVM)
				{
					if (!toolsetErrors.IsEmpty())
						toolsetErrors += ", ";
					toolsetErrors += project->mName;					
				}
			}
		}

		if (!hotSwapErrors.IsEmpty())
			mPassInstance->Fail(StrFormat("Hot compilation cannot be used when LTO is enabled in '%s'. Consider setting 'Workspace/Beef/Debug/Enable Hot Compilation' to 'No'.", hotSwapErrors.c_str()));
		if (!toolsetErrors.IsEmpty())
			mPassInstance->Fail(StrFormat("The Workspace Toolset must be set to 'LLVM' in order to use LTO in '%s'. Consider changing 'Workspace/General/Toolset' to 'LLVM'.", toolsetErrors.c_str()));
	}

	//
	{
		String attribName;
		mAttributeTypeOptionMap.Clear();
		for (int typeOptionsIdx = 0; typeOptionsIdx < (int)mSystem->mTypeOptions.size(); typeOptionsIdx++)
		{
			auto& typeOptions = mSystem->mTypeOptions[typeOptionsIdx];
			for (auto& attributeFilter : typeOptions.mAttributeFilters)
			{
				attribName = attributeFilter;
				attribName += "Attribute";
				Array<int>* arrPtr = NULL;
				mAttributeTypeOptionMap.TryAdd(attribName, NULL, &arrPtr);
				arrPtr->Add(typeOptionsIdx);
			}
		}
	}

	// Inc revision for next run through Compile
	mRevision++;
	BfLogSysM("Compile Start. Revision: %d\n", mRevision);

	if (mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
		mContext->mUnreifiedModule->mIsReified = true;
	else
		mContext->mUnreifiedModule->mIsReified = false;

	if (mOptions.mAllowHotSwapping)
	{
		if (mHotData == NULL)
		{
			mHotData = new HotData();
			mHotData->mCompiler = this;
		}
	}
	else
	{
		delete mHotData;
		mHotData = NULL;
	}

	if (IsHotCompile())
	{
		if (!mOptions.mAllowHotSwapping)
		{
			mPassInstance->Fail("Hot Compilation is not enabled");
			return true;
		}

		if (mHotState == NULL)
		{
			mHotState = new HotState();
			mHotState->mHotProject = mOptions.mHotProject;
		}
		else
		{
			// It should be impossible to switch hot projects without a non-hot compile between them
			BF_ASSERT(mHotState->mHotProject == mOptions.mHotProject);
		}
	}
	else
	{
		for (auto& kv : mContext->mSavedTypeDataMap)
		{
			auto savedTypeData = kv.mValue;
			delete savedTypeData->mHotTypeData;
			savedTypeData->mHotTypeData = NULL;
		}

		delete mHotState;
		mHotState = NULL;

		// This will get rid of any old method data so we don't have any more mPrevVersions
		ClearOldHotData();
	}
		
	int prevUnfinishedModules = mStats.mModulesStarted - mStats.mModulesFinished;	
	mCompletionPct = 0;
	memset(&mStats, 0, sizeof(mStats));
	mCodeGen.ClearResults();
	mCodeGen.ResetStats();
	mStats.mModulesStarted = prevUnfinishedModules;

	if ((mLastRevisionAborted) && (!mIsResolveOnly))
	{
		auto _AddCount = [&](BfModule* module)
		{
			if (module->mAddedToCount)
			{
				if (module->mIsReified)
					mStats.mReifiedModuleCount++;				
			}
		};

		for (auto mainModule : mContext->mModules)
		{
			_AddCount(mainModule);
			for (auto specKV : mainModule->mSpecializedMethodModules)
			{
				_AddCount(specKV.mValue);
			}
		}
	}
	

	if (IsHotCompile())
	{		
		mContext->EnsureHotMangledVirtualMethodNames();		
	}

	mOutputDirectory = outputDirectory;
	mSystem->StartYieldSection();

	mCanceling = false;
	mSystem->CheckLockYield();

#ifdef WANT_COMPILE_LOG
	if (!mIsResolveOnly)
	{
		mCompileLogFP = fopen(StrFormat("compile%d.txt", mRevision).c_str(), "wb");
	}
#endif

	BfTypeDef* typeDef;						

	BfLogSysM("UpdateRevisedTypes Revision %d. ResolvePass:%d CursorIdx:%d\n", mRevision, mIsResolveOnly, 
		((mResolvePassData == NULL) || (mResolvePassData->mParser == NULL)) ? - 1 : mResolvePassData->mParser->mCursorIdx);	
	
	mCompileState = CompileState_Normal;
	UpdateRevisedTypes();	
	// We need to defer processing the graveyard until here, because mLookupResults contain atom references so we need to make sure
	//  those aren't deleted until we can properly handle it.
	mSystem->ProcessAtomGraveyard();
	
	BpEnter("Compile_Start");

	bool hasRequiredTypes = true;

	//HashSet<BfTypeDef*> internalTypeDefs;

	auto _GetRequiredType = [&](const StringImpl& typeName, int genericArgCount = 0)
	{
		auto typeDef = mSystem->FindTypeDef(typeName, genericArgCount);		
		if (typeDef == NULL)
		{
			mPassInstance->Fail(StrFormat("Unable to find system type: %s", typeName.c_str()));
			hasRequiredTypes = false;
		}		
		return typeDef;
	};

	mArray1TypeDef = _GetRequiredType("System.Array1");	
	mArray2TypeDef = _GetRequiredType("System.Array2");	
	mArray3TypeDef = _GetRequiredType("System.Array3");
	mArray4TypeDef = _GetRequiredType("System.Array4");
	mSpanTypeDef = _GetRequiredType("System.Span", 1);
	mAttributeTypeDef = _GetRequiredType("System.Attribute");
	mAttributeUsageAttributeTypeDef = _GetRequiredType("System.AttributeUsageAttribute");
	mBfObjectTypeDef = _GetRequiredType("System.Object");	
	mClassVDataTypeDef = _GetRequiredType("System.ClassVData");
	mCLinkAttributeTypeDef = _GetRequiredType("System.CLinkAttribute");
	mImportAttributeTypeDef = _GetRequiredType("System.ImportAttribute");
	mCReprAttributeTypeDef = _GetRequiredType("System.CReprAttribute");
	mAlignAttributeTypeDef = _GetRequiredType("System.AlignAttribute");
	mAllowDuplicatesAttributeTypeDef = _GetRequiredType("System.AllowDuplicatesAttribute");
	mNoDiscardAttributeTypeDef = _GetRequiredType("System.NoDiscardAttribute");
	mDisableChecksAttributeTypeDef = _GetRequiredType("System.DisableChecksAttribute");
	mDisableObjectAccessChecksAttributeTypeDef = _GetRequiredType("System.DisableObjectAccessChecksAttribute");
	mDbgRawAllocDataTypeDef = _GetRequiredType("System.DbgRawAllocData");
	mDeferredCallTypeDef = _GetRequiredType("System.DeferredCall");
	mDelegateTypeDef = _GetRequiredType("System.Delegate");
	mActionTypeDef = _GetRequiredType("System.Action");
	mEnumTypeDef = _GetRequiredType("System.Enum");
	mFriendAttributeTypeDef = _GetRequiredType("System.FriendAttribute");
	mCheckedAttributeTypeDef = _GetRequiredType("System.CheckedAttribute");
	mUncheckedAttributeTypeDef = _GetRequiredType("System.UncheckedAttribute");
	mResultTypeDef = _GetRequiredType("System.Result", 1);
	mFunctionTypeDef = _GetRequiredType("System.Function");
	mGCTypeDef = _GetRequiredType("System.GC");	
	mGenericIEnumerableTypeDef = _GetRequiredType("System.Collections.Generic.IEnumerable");
	mGenericIEnumeratorTypeDef = _GetRequiredType("System.Collections.Generic.IEnumerator");
	mGenericIRefEnumeratorTypeDef = _GetRequiredType("System.Collections.Generic.IRefEnumerator");	
	mInlineAttributeTypeDef = _GetRequiredType("System.InlineAttribute");
	mInternalTypeDef = _GetRequiredType("System.Internal");
	mIDisposableTypeDef = _GetRequiredType("System.IDisposable");
	mIPrintableTypeDef = _GetRequiredType("System.IPrintable");
	mIHashableTypeDef = _GetRequiredType("System.IHashable");
	mLinkNameAttributeTypeDef = _GetRequiredType("System.LinkNameAttribute");
	mMethodRefTypeDef = _GetRequiredType("System.MethodReference", 1);
	mNullableTypeDef = _GetRequiredType("System.Nullable");
	mOrderedAttributeTypeDef = _GetRequiredType("System.OrderedAttribute");
	mPointerTTypeDef = _GetRequiredType("System.Pointer", 1);
	mPointerTypeDef = _GetRequiredType("System.Pointer", 0);
	mReflectArrayType = _GetRequiredType("System.Reflection.ArrayType");
	mReflectFieldDataDef = _GetRequiredType("System.Reflection.TypeInstance.FieldData");
	mReflectFieldSplatDataDef = _GetRequiredType("System.Reflection.TypeInstance.FieldSplatData");
	mReflectMethodDataDef = _GetRequiredType("System.Reflection.TypeInstance.MethodData");
	mReflectParamDataDef = _GetRequiredType("System.Reflection.TypeInstance.ParamData");
	mReflectPointerType = _GetRequiredType("System.Reflection.PointerType");
	mReflectSizedArrayType = _GetRequiredType("System.Reflection.SizedArrayType");
	mReflectSpecializedGenericType = _GetRequiredType("System.Reflection.SpecializedGenericType");
	mReflectTypeInstanceTypeDef = _GetRequiredType("System.Reflection.TypeInstance");
	mReflectUnspecializedGenericType = _GetRequiredType("System.Reflection.UnspecializedGenericType");
	mSizedArrayTypeDef = _GetRequiredType("System.SizedArray", 2);	
	mStaticInitAfterAttributeTypeDef = _GetRequiredType("System.StaticInitAfterAttribute");
	mStaticInitPriorityAttributeTypeDef = _GetRequiredType("System.StaticInitPriorityAttribute");
	mStringTypeDef = _GetRequiredType("System.String");
	mTestAttributeTypeDef = _GetRequiredType("System.TestAttribute");
	mThreadStaticAttributeTypeDef = _GetRequiredType("System.ThreadStaticAttribute");
	mTypeTypeDef = _GetRequiredType("System.Type");
	mUnboundAttributeTypeDef = _GetRequiredType("System.UnboundAttribute");	
	mValueTypeTypeDef = _GetRequiredType("System.ValueType");
	mObsoleteAttributeTypeDef = _GetRequiredType("System.ObsoleteAttribute");
	mErrorAttributeTypeDef = _GetRequiredType("System.ErrorAttribute");
	mWarnAttributeTypeDef = _GetRequiredType("System.WarnAttribute");

	for (int i = 0; i < BfTypeCode_Length; i++)
		mContext->mPrimitiveStructTypes[i] = NULL;

	if (!hasRequiredTypes)
	{
		// Force rebuilding
		mInInvalidState = true;
		mOptions.mForceRebuildIdx++;
		return true;
	}
		
	mSystem->CheckLockYield();
	VisitSourceExteriorNodes();

	//BF_ASSERT(hasRequiredTypes);

	if (!mIsResolveOnly)
	{
		HashSet<BfModule*> foundVDataModuleSet;

		for (auto bfProject : mSystem->mProjects)
		{
			if (bfProject->mDisabled)
				continue;

			if ((mBfObjectTypeDef != NULL) && (!bfProject->ContainsReference(mBfObjectTypeDef->mProject)))
			{
				mPassInstance->Fail(StrFormat("Project '%s' must reference core library '%s'", bfProject->mName.c_str(), mBfObjectTypeDef->mProject->mName.c_str()));
			}

			if ((bfProject->mTargetType != BfTargetType_BeefConsoleApplication) && (bfProject->mTargetType != BfTargetType_BeefWindowsApplication) &&
				(bfProject->mTargetType != BfTargetType_BeefDynLib) &&
				(bfProject->mTargetType != BfTargetType_C_ConsoleApplication) && (bfProject->mTargetType != BfTargetType_C_WindowsApplication) &&
				(bfProject->mTargetType != BfTargetType_BeefTest) &&
				(bfProject->mTargetType != BfTargetType_BeefApplication_StaticLib) && (bfProject->mTargetType != BfTargetType_BeefApplication_DynamicLib))
				continue;

			if (bfProject->mTargetType == BfTargetType_BeefTest)
			{
				// Force internal test methods
				auto bfModule = mContext->mScratchModule;
				bfModule->GetInternalMethod("Test_Init");
				bfModule->GetInternalMethod("Test_Query");
				bfModule->GetInternalMethod("Test_Finish");
			}

			bool found = false;
			for (auto module : mVDataModules)
			{
				if (module->mProject == bfProject)
				{
					found = true;
					foundVDataModuleSet.Add(module);
					//module->StartNewRevision();
				}
			}

			if (!found)
			{
				auto module = new BfVDataModule(mContext);
				module->mProject = bfProject;
				module->Init();
				module->FinishInit();
				module->mIsSpecialModule = true;
				BF_ASSERT(!mContext->mLockModules);
				mContext->mModules.push_back(module);
				mVDataModules.push_back(module);

				foundVDataModuleSet.Add(module);
			}			
		}

		// Remove old vdata
		for (int moduleIdx = 0; moduleIdx < (int) mVDataModules.size(); moduleIdx++)
		{
			auto module = mVDataModules[moduleIdx];
			if (!foundVDataModuleSet.Contains(module))
			{
				delete module;
				mVDataModules.erase(mVDataModules.begin() + moduleIdx);
				moduleIdx--;
				
				mContext->mModules.Remove(module);
			}
		}
	}

	if (mIsResolveOnly)
		VisitAutocompleteExteriorIdentifiers();	

	if (!hasRequiredTypes)
	{
		BfLogSysM("Missing required types\n");		
	}

	mStats.mTypesQueued = 0;
	mStats.mMethodsQueued = 0;
	
	mStats.mTypesQueued += (int)mContext->mPopulateTypeWorkList.size();
	mStats.mMethodsQueued += (int)mContext->mMethodWorkList.size();

	if (hasRequiredTypes)
	{		
		mContext->mScratchModule->ResolveTypeDef(mBfObjectTypeDef, BfPopulateType_Full);
		
		mContext->RemapObject();
				
		mSystem->CheckLockYield();
		
		mWantsDeferMethodDecls = mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude;
		
		CompileReified();
		mWantsDeferMethodDecls = false;	
	}
		
	BpLeave();
	BpEnter("Compile_End");
		
	{
		BP_ZONE("ProcessingLiveness");

		for (auto type : mContext->mResolvedTypes)
		{			
			auto depType = type->ToDependedType();
			if (depType != NULL)
				depType->mRebuildFlags = (BfTypeRebuildFlags)(depType->mRebuildFlags | BfTypeRebuildFlag_AwaitingReference);
		}
			
		bool didWork = false;
		UpdateDependencyMap(mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_ResolveUnused, didWork);

		if (mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude)
		{
			// If UpdateDependencyMap caused methods to be reified, then we need to run PopulateReified again-
			//  because those methods may be virtual and we need to reify overrides (for example).
			// We use the DoWorkLoop result to determine if there were actually any changes from UpdateDependencyMap
			if (didWork)
			{
				PopulateReified();
			}
		}						
	}
	
	if (hasRequiredTypes)
		ProcessPurgatory(true);

	// Mark used modules
	if ((mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude) && (!mCanceling))
	{
		bool hadActualTarget = false;

		if (!mIsResolveOnly)
		{
			SizedArray<BfModule*, 32> requiredModules;

			for (auto typeDef : mSystem->mTypeDefs)
			{
				if ((typeDef->mIsAlwaysInclude) && (!typeDef->mIsPartial))
				{
					auto requiredType = mContext->mScratchModule->ResolveTypeDef(typeDef);
					if (requiredType != NULL)
					{
						auto requiredModule = requiredType->GetModule();
						if (requiredModule != NULL)
							requiredModules.push_back(requiredModule);
					}
				}
			}

			mContext->mReferencedIFaceSlots.Clear();
			bool hasTests = false;
			for (auto project : mSystem->mProjects)
			{
				if (project->mTargetType == BfTargetType_BeefTest)
					hasTests = true;

				project->mUsedModules.Clear();
				project->mReferencedTypeData.Clear();

				if (project->mDisabled)
					continue;
				if (project->mTargetType == BfTargetType_BeefLib)
					continue;

				hadActualTarget = true;

				for (auto requiredModule : requiredModules)
				{
					mContext->MarkUsedModules(project, requiredModule);
				}

				String entryClassName = project->mStartupObject;
				typeDef = mSystem->FindTypeDef(entryClassName, 0, project);
				if (typeDef != NULL)
				{
					auto startupType = mContext->mScratchModule->ResolveTypeDef(typeDef);
					if (startupType != NULL)
					{
						auto startupTypeInst = startupType->ToTypeInstance();
						if (startupTypeInst != NULL)
						{
							mContext->MarkUsedModules(project, startupTypeInst->GetModule());
						}
					}
				}

				if (hasTests)
				{
					for (auto type : mContext->mResolvedTypes)
					{						
						auto typeInstance = type->ToTypeInstance();
						if ((typeInstance != NULL) &&
							(typeInstance->mTypeDef->mProject->mTargetType == BfTargetType_BeefTest))
						{
							bool typeHasTest = false;
							for (auto& methodInstanceGroup : typeInstance->mMethodInstanceGroups)
							{
								if (methodInstanceGroup.mDefault != NULL)
								{
									auto methodInstance = methodInstanceGroup.mDefault;
									if ((methodInstance->GetCustomAttributes() != NULL) &&
										(methodInstance->GetCustomAttributes()->Contains(mTestAttributeTypeDef)))
									{
										typeHasTest = true;
									}
								}
							}
							if (typeHasTest)
								mContext->MarkUsedModules(typeInstance->mTypeDef->mProject, typeInstance->mModule);
						}
					}
				}
			}

			// Leave types reified when hot compiling
			if ((!IsHotCompile()) && (hadActualTarget))
				mContext->TryUnreifyModules();
		}
	}

	// Generate slot nums
	if ((!mIsResolveOnly) && (hasRequiredTypes) && (!mCanceling))
	{
		if ((!IsHotCompile()) || (mHotState->mHasNewInterfaceTypes))
		{	
			int prevSlotCount = mMaxInterfaceSlots;
			GenerateSlotNums();			
			if ((prevSlotCount != -1) && (prevSlotCount != mMaxInterfaceSlots))
			{
				mInterfaceSlotCountChanged = true;
			}
			if (mHotState != NULL)
				mHotState->mHasNewInterfaceTypes = false;
		}
	}

	// Resolve unused types
	if ((mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_ResolveUnused) && (!mCanceling))
	{
		// Finish off any outstanding modules so we can code generate in parallel with handling the unreified stuff		
		for (auto module : mContext->mModules)
		{
			if (!module->mIsSpecialModule)
			{
				if ((module->mIsReified) && (module->mIsModuleMutable))
				{						
					module->Finish();
				}
			}
		}		

		DoWorkLoop();

		BfLogSysM("Compile QueueUnused\n");
		mCompileState = BfCompiler::CompileState_Unreified;

		BpLeave();
		BpEnter("Compile_QueueUnused");

		while (true)
		{
			BP_ZONE("Compile_QueueUnused");

			bool queuedMoreMethods = false;

			int startTypeInitCount = mTypeInitCount;
			
			for (auto typeDef : mSystem->mTypeDefs)
			{
				mSystem->CheckLockYield();
				if (mCanceling)
				{
					BfLogSysM("Canceling from Compile typeDef loop\n");
					break;
				}

				if (typeDef->mProject->mDisabled)
					continue;
				
				if (typeDef->mIsPartial)
					continue;
				if (typeDef->mTypeCode == BfTypeCode_Extension)
					continue;
				
				mContext->mUnreifiedModule->ResolveTypeDef(typeDef, BfPopulateType_Full);
			}

			for (auto type : mContext->mResolvedTypes)
			{				
				auto module = type->GetModule();

				if (module == NULL)
					continue;

				if ((type->IsIncomplete()) && (type->IsTypeInstance()) && (!type->IsSpecializedType()))
				{
					mSystem->CheckLockYield();
					module->PopulateType(type, BfPopulateType_Full);
				}

				auto typeInst = type->ToTypeInstance();
				if (typeInst == NULL)
					continue;
				
 				if (typeInst->IsUnspecializedTypeVariation())
 					continue;

				if (!typeInst->IsSpecializedType())
				{					
					// Find any remaining methods for unreified processing
					for (auto&& methodInstGroup : typeInst->mMethodInstanceGroups)
					{
						if ((methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference) ||
							(methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference))
						{
							queuedMoreMethods = true;
							if ((methodInstGroup.mDefault != NULL) && (methodInstGroup.mDefault->mIsForeignMethodDef))
							{
								mContext->mUnreifiedModule->GetMethodInstance(typeInst, methodInstGroup.mDefault->mMethodDef, BfTypeVector(),
									(BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_ForeignMethodDef | BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_ExplicitResolveOnlyPass));
							}
							else
								mContext->mUnreifiedModule->GetMethodInstance(typeInst, typeInst->mTypeDef->mMethods[methodInstGroup.mMethodIdx], BfTypeVector(),
									(BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_ExplicitResolveOnlyPass));
						}
					}
				}
			}

			if ((!queuedMoreMethods) && (startTypeInitCount == mTypeInitCount))
				break;
			
			DoWorkLoop();			
		}

		bool didWork = false;
		UpdateDependencyMap(true, didWork);
		DoWorkLoop();

		mCompileState = BfCompiler::CompileState_Normal;
	}
	else
	{
		DoWorkLoop();
	}
	
	if (hasRequiredTypes)
		ProcessPurgatory(false);
	
	// Old Mark used modules	

	if ((!mIsResolveOnly) && (hasRequiredTypes))
	{	
// 		if ((!mPassInstance->HasFailed()) && (!mCanceling))
// 		{
// 			if ((!IsHotCompile()) || (mHotState->mHasNewInterfaceTypes))
// 			{
// 				GenerateSlotNums();
// 				if (mHotState != NULL)
// 					mHotState->mHasNewInterfaceTypes = false;
// 			}
// 		}

		if ((!mPassInstance->HasFailed()) && (!mCanceling))
		{
			if (!mOptions.mAllowHotSwapping)
			{
				GenerateDynCastData();				
				mContext->ProcessWorkList(false, false);				
			}

			mCompileState = BfCompiler::CompileState_VData;

			for (auto vdataModule : mVDataModules)
				CreateVData(vdataModule);
			for (auto vdataModule : mVDataModules)
				FixVDataHash(vdataModule);

			mCompileState = BfCompiler::CompileState_Normal;
		}

		// Don't clear out unused string pool entries while we are hot swapping, because we want string literals
		//  to still be the same pointer if it's erased and then put back
		if ((!IsHotCompile()) && (!mCanceling))
			ClearUnusedStringPoolEntries();
		
		mContext->ValidateDependencies();
		mContext->UpdateAfterDeletingTypes();		
	}
		
	// We need to check the specialized errors before writing out modules --
	//  this call is responsible for deleting dead method specializations that contained errors, or for setting
	//  the mHadBuildErrors on the module if there was a method specialization error that didn't die
	mContext->CheckSpecializedErrorData();
	mContext->Finish();
	if ((!mIsResolveOnly) && (!IsHotCompile()))
		ClearOldHotData();
	
	mPassInstance->TryFlushDeferredError();

	BpLeave();
	BpEnter("Compile_Finish");
	
	//TODO:!!
	//mCanceling = true;

	String moduleListStr;
	int numModulesWritten = 0;
	if ((hasRequiredTypes) && (!mCanceling))
	{
		if (!mIsResolveOnly)
		{
			int idx = 0;
			
			BF_ASSERT(mContext->mMethodWorkList.IsEmpty());

			//bfContext->mLockModules = true;
			for (int moduleIdx = 0; moduleIdx < (int)mContext->mModules.size(); moduleIdx++)
			{
				//bool clearModule = false;
				auto mainModule = mContext->mModules[moduleIdx];
				BfModule* bfModule = mainModule;
				if (bfModule->mIsReified)						
				{
					auto itr = mainModule->mSpecializedMethodModules.begin();
					while (true)
					{
						if (bfModule->mIsModuleMutable)
						{
							//clearModule = true;
							// Note that Finish will just return immediately if we have errors, we don't write out modules with errors
							// The 'mLastModuleWrittenRevision' will not be updated in the case.
							bfModule->Finish();
							mainModule->mRevision = std::max(mainModule->mRevision, bfModule->mRevision);
						}
						if (bfModule->mLastModuleWrittenRevision == mRevision)
						{
							if (!moduleListStr.empty())
								moduleListStr += ", ";
							moduleListStr += bfModule->mModuleName;
							numModulesWritten++;
						}

						if (bfModule->mParentModule != NULL)
						{
							for (auto&& fileName : bfModule->mOutFileNames)
							{
								if (!mainModule->mOutFileNames.Contains(fileName))
									mainModule->mOutFileNames.push_back(fileName);
							}
						}

						if (bfModule->mNextAltModule != NULL)
						{
							bfModule = bfModule->mNextAltModule;
						}
						else
						{
							if (itr == mainModule->mSpecializedMethodModules.end())
								break;
							bfModule = itr->mValue;
							++itr;
						}
					}
				}
					
				mainModule->ClearModule();
			}

			//bfContext->mLockModules = false;			
		}
		else
		{
			bool isTargeted = (mResolvePassData != NULL) && (mResolvePassData->mParser != NULL);
			if (!isTargeted)
			{				
				for (auto bfModule : mContext->mModules)
				{
					if (bfModule->mIsModuleMutable)
					{
						bfModule->Finish();							
						bfModule->mRevision = std::max(bfModule->mRevision, bfModule->mRevision);
						bfModule->ClearModuleData();
					}
				}				
			}
		}
	}

	/*if (!moduleListStr.empty())
		mPassInstance->OutputLine(StrFormat("%d modules generated: %s", numModulesWritten, moduleListStr.c_str()));*/
		
	//CompileLog("%d object files written: %s\n", numModulesWritten, moduleListStr.c_str());
	
	//printf("Compile done, waiting for finish\n");

	while (true)
	{
		if (!hasRequiredTypes)
			break;
		if (mCanceling)
			mCodeGen.Cancel();
		bool isDone = mCodeGen.Finish();
		UpdateCompletion();
		if (isDone)
			break;
	}
	mCodeGen.ProcessErrors(mPassInstance, mCanceling);

	// This has to happen after codegen because we may delete modules that are referenced in codegen	
	mContext->Cleanup();	
	if ((!IsHotCompile()) && (!mIsResolveOnly) && (!mCanceling))
	{
		// Only save 'saved type data' for temporarily-deleted types like on-demand types.
		//  If we don't reuse it within a compilation pass then we put those IDs up to be
		//  reused later. We don't do this for hot reloading because there are cases like
		//  a user renaming a type that we want to allow him to be able to undo and then
		//  hot-recompile successfully.
		for (auto& kv : mContext->mSavedTypeDataMap)
		{
			auto savedTypeData = kv.mValue;
			mTypeIdFreeList.Add(savedTypeData->mTypeId);
			delete savedTypeData;
		}
		mContext->mSavedTypeDataMap.Clear();
		mContext->mSavedTypeData.Clear();
	}

#ifdef BF_PLATFORM_WINDOWS
	if (!mIsResolveOnly)
	{				
		for (auto mainModule : mContext->mModules)
		{				
			BfModule* bfModule = mainModule;
			if (bfModule->mIsReified)														
			{
				for (auto outFileName : bfModule->mOutFileNames)					
				{
					if (outFileName.mModuleWritten)
						BeLibManager::Get()->AddUsedFileName(outFileName.mFileName);                        
				}
			}
		}		

		auto libManager = BeLibManager::Get();
		libManager->Finish();
		if (!libManager->mErrors.IsEmpty())
		{
			for (auto& error : libManager->mErrors)
				mPassInstance->Fail(error);
			// We need to rebuild everything just to force that lib to get repopulated
			mOptions.mForceRebuildIdx++;
		}
		libManager->mErrors.Clear();
	}
#endif		

	int numObjFilesWritten = 0;
	for (auto& fileEntry : mCodeGen.mCodeGenFiles)
	{
		if (!fileEntry.mWasCached)
			numObjFilesWritten++;
	}
	mPassInstance->OutputLine(StrFormat(":low %d module%s built, %d object file%s generated", 
		numModulesWritten, (numModulesWritten != 1) ? "s" : "",
		numObjFilesWritten, (numObjFilesWritten != 1) ? "s" : ""));
	
	BpLeave();	
	mPassInstance->WriteErrorSummary();

	if ((mCanceling) && (!mIsResolveOnly))
	{
		mPassInstance->Fail("Build canceled");
		mContext->CancelWorkItems();
		CompileLog("Compile canceled\n");
	}

	BfLogSysM("TypesPopulated:%d MethodsDeclared:%d MethodsProcessed:%d Canceled? %d\n", mStats.mTypesPopulated, mStats.mMethodDeclarations, mStats.mMethodsProcessed, mCanceling);	
	
	UpdateCompletion();
	if ((!mIsResolveOnly) && (!mPassInstance->HasFailed()) && (!mCanceling))
	{
		//BF_ASSERT(mCompletionPct >= 0.99999f);
	}
	
	if (mCompileLogFP != NULL)
	{
		fclose(mCompileLogFP);
		mCompileLogFP = NULL;
	}	

	UpdateCompletion();
	mStats.mTotalTypes = mContext->mResolvedTypes.mCount;

	String compileInfo;
	if (mIsResolveOnly)
		compileInfo += StrFormat("ResolveOnly ResolveType:%d Parser:%d\n", mResolvePassData->mResolveType, mResolvePassData->mParser != NULL);
	compileInfo += StrFormat("TotalTypes:%d\nTypesPopulated:%d\nMethodsDeclared:%d\nMethodsProcessed:%d\nCanceled? %d\n", mStats.mTotalTypes, mStats.mTypesPopulated, mStats.mMethodDeclarations, mStats.mMethodsProcessed, mCanceling);
	compileInfo += StrFormat("TypesPopulated:%d\n", mStats.mTypesPopulated);
	compileInfo += StrFormat("MethodDecls:%d\nMethodsProcessed:%d\nModulesStarted:%d\nModulesFinished:%d\n", mStats.mMethodDeclarations, mStats.mMethodsProcessed, mStats.mModulesFinished);
	BpEvent("CompileDone", compileInfo.c_str());

	if (mHotState != NULL)
	{
		for (auto& fileEntry : mCodeGen.mCodeGenFiles)
		{
			if (fileEntry.mWasCached)
				continue;			
			mHotState->mQueuedOutFiles.Add(fileEntry);
		}

		if (!mPassInstance->HasFailed())
		{
			// Clear these out when we know we've compiled without error
			mHotState->mNewlySlottedTypeIds.Clear();
			mHotState->mSlotDefineTypeIds.Clear();
		}
	}		

	mCompileState = BfCompiler::CompileState_None;

// 	extern MemReporter gBEMemReporter;
// 	extern int gBEMemReporterSize;
// 	gBEMemReporter.Report();
// 	int memReporterSize = gBEMemReporterSize;

	mLastRevisionAborted = mCanceling || !hasRequiredTypes;
	bool didCancel = mCanceling && hasRequiredTypes;
	mCanceling = false;

	mContext->ValidateDependencies();

	return !didCancel;
}

bool BfCompiler::Compile(const StringImpl& outputDirectory)
{
	bool success = DoCompile(outputDirectory);
	if (!success)
		return false;
	if (mPassInstance->HasFailed())
		return true;
	if (!mInterfaceSlotCountChanged)
		return true;
	BfLogSysM("Interface slot count increased. Rebuilding relevant modules.\n");
	mPassInstance->OutputLine("Interface slot count increased. Rebuilding relevant modules.");
	// Recompile with the increased slot count
	success = DoCompile(outputDirectory);	
	BF_ASSERT(!mInterfaceSlotCountChanged);
	return success;
}

void BfCompiler::ClearResults()
{
	BP_ZONE("BfCompiler::ClearResults");

	mCodeGen.ClearResults();
}

// Can should still leave the system in a state such that we when we save as much progress as possible while
//  still leaving the system in a state that the next attempt at compile will resume with a valid state
// Canceling will still process the pending PopulateType calls but may leave items in the method worklist.
// Note that Cancel is an async request to cancel
void BfCompiler::Cancel()
{
	mCanceling = true;
	mHadCancel = true;
	BfLogSysM("BfCompiler::Cancel\n");
	BpEvent("BfCompiler::Cancel", "");
}

//#define WANT_COMPILE_LOG

void BfCompiler::CompileLog(const char* fmt ...)
{
#ifdef WANT_COMPILE_LOG
	if (mCompileLogFP == NULL)	
		return;

	//static int lineNum = 0;
	//lineNum++;
	
	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

	//aResult = StrFormat("%d ", lineNum)  + aResult;

	fwrite(aResult.c_str(), 1, aResult.length(), mCompileLogFP);	
#endif
}

void BfCompiler::ReportMemory(MemReporter* memReporter)
{
	AutoCrit crit(mSystem->mDataLock);

	{
		AutoMemReporter autoMemReporter(memReporter, "Context");
		mContext->ReportMemory(memReporter);
	}

	for (auto type : mContext->mResolvedTypes)
	{
		AutoMemReporter autoMemReporter(memReporter, "Types");
		type->ReportMemory(memReporter);
	}


	for (auto module : mContext->mModules)
	{
		AutoMemReporter autoMemReporter(memReporter, "Modules");
		module->ReportMemory(memReporter);
	}	

	{
		AutoMemReporter autoMemReporter(memReporter, "ScratchModule");
		mContext->mScratchModule->ReportMemory(memReporter);
	}	
	
	for (auto vdataModule : mVDataModules)
	{
		AutoMemReporter autoMemReporter(memReporter, "VDataModules");
		vdataModule->ReportMemory(memReporter);
	}	

	if (mHotData != NULL)
	{
		AutoMemReporter autoMemReporter(memReporter, "HotData");
		memReporter->Add(sizeof(HotData));
		memReporter->AddMap(mHotData->mMethodMap);
		for (auto& kv : mHotData->mMethodMap)
		{
			memReporter->AddStr(kv.mKey);
			memReporter->Add(sizeof(BfHotMethod));
			memReporter->AddVec(kv.mValue->mReferences);
		}
	}

	if (mHotState != NULL)
	{
		AutoMemReporter autoMemReporter(memReporter, "HotState");
		memReporter->Add(sizeof(HotState));
		memReporter->AddVec(mHotState->mQueuedOutFiles, false);
		memReporter->AddHashSet(mHotState->mSlotDefineTypeIds, false);
		memReporter->AddHashSet(mHotState->mPendingDataChanges, false);
		memReporter->AddMap(mHotState->mDeletedTypeNameMap, false);
		for (auto& kv : mHotState->mDeletedTypeNameMap)
		{
			memReporter->AddStr(kv.mKey, false);
		}
	}	
}

//////////////////////////////////////////////////////////////////////////

void BfCompiler::GenerateAutocompleteInfo()
{	
	BP_ZONE("BfCompiler::GetAutocompleteInfo");	
	
 	String& autoCompleteResultString = *gTLStrReturn.Get();
	autoCompleteResultString.Clear();

	auto _GetDocString = [&](BfCommentNode* commentNode, StringImpl& docString)
	{
		commentNode->ToString(docString);
		for (int i = 0; i < (int)docString.length(); i++)
		{
			char c = docString[i];
			if (c == '\n')
				docString[i] = '\x3';
		}
	};


	auto bfModule = mResolvePassData->mAutoComplete->mModule;
 	if (bfModule != NULL)
	{
		auto autoComplete = mResolvePassData->mAutoComplete;
		if (autoComplete->mResolveType == BfResolveType_GetNavigationData)
			return; // Already handled

		if (autoComplete->mResolveType == BfResolveType_GetResultString)
		{
			autoCompleteResultString = autoComplete->mResultString;
			return;
		}

		if (autoComplete->mUncertain)
			autoCompleteResultString += "uncertain\n";

		if (autoComplete->mDefaultSelection.length() != 0)
			autoCompleteResultString += StrFormat("select\t%s\n", autoComplete->mDefaultSelection.c_str());		

		auto _EncodeTypeDef = [] (BfTypeDef* typeDef)
		{
			StringT<128> typeName = typeDef->mProject->mName;
			typeName += ":";
			typeName += typeDef->mFullName.ToString();
			if (!typeDef->mGenericParamDefs.IsEmpty())
				typeName += StrFormat("`%d", (int)typeDef->mGenericParamDefs.size());
			return typeName;
		};

		if (autoComplete->mResolveType == BfResolveType_GetSymbolInfo)
		{
			if (autoComplete->mDefTypeGenericParamIdx != -1)
			{
				autoCompleteResultString += StrFormat("typeGenericParam\t%d\n", autoComplete->mDefTypeGenericParamIdx);
				autoCompleteResultString += StrFormat("typeRef\t%s\n", _EncodeTypeDef(autoComplete->mDefType).c_str());
			}
			else if (autoComplete->mDefMethodGenericParamIdx != -1)
			{
				autoCompleteResultString += StrFormat("methodGenericParam\t%d\n", autoComplete->mDefMethodGenericParamIdx);
				autoCompleteResultString += StrFormat("methodRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefMethod->mIdx);
			}
			else if ((autoComplete->mReplaceLocalId != -1) && (autoComplete->mDefMethod != NULL))
			{
				autoCompleteResultString += StrFormat("localId\t%d\n", autoComplete->mReplaceLocalId);
				autoCompleteResultString += StrFormat("methodRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefMethod->mIdx);
			}
			else if (autoComplete->mDefField != NULL)
			{
				autoCompleteResultString += StrFormat("fieldRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefField->mIdx);
			}
			else if (autoComplete->mDefProp != NULL)
			{
				autoCompleteResultString += StrFormat("propertyRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefProp->mIdx);
			}
			else if (autoComplete->mDefMethod != NULL)
			{
				if (autoComplete->mDefMethod->mMethodType == BfMethodType_Ctor)
					autoCompleteResultString += StrFormat("ctorRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefMethod->mIdx);
				else
					autoCompleteResultString += StrFormat("methodRef\t%s\t%d\n", _EncodeTypeDef(autoComplete->mDefType).c_str(), autoComplete->mDefMethod->mIdx);
			}
			else if (autoComplete->mDefType != NULL)
			{
				autoCompleteResultString += StrFormat("typeRef\t%s\n", _EncodeTypeDef(autoComplete->mDefType).c_str());
			}
			
			if (autoComplete->mInsertEndIdx > 0)
			{
				if (mResolvePassData->mParser->mSrc[autoComplete->mInsertEndIdx - 1] == '!')
					autoComplete->mInsertEndIdx--;
			}
		}

		const char* wantsDocEntry = NULL;
		if (!autoComplete->mDocumentationEntryName.IsEmpty())
			wantsDocEntry = autoComplete->mDocumentationEntryName.c_str();

		if (autoComplete->mInsertStartIdx != -1)
		{
			autoCompleteResultString += StrFormat("insertRange\t%d %d\n", autoComplete->mInsertStartIdx, autoComplete->mInsertEndIdx);
		}

		if ((autoComplete->mDefMethod == NULL) && (autoComplete->mGetDefinitionNode == NULL) && (autoComplete->mIsGetDefinition) && (autoComplete->mMethodMatchInfo != NULL))
		{
			// Take loc from methodMatchInfo
			if (autoComplete->mMethodMatchInfo->mInstanceList.size() > 0)
			{
				int bestIdx = autoComplete->mMethodMatchInfo->mBestIdx;
				auto typeInst = autoComplete->mMethodMatchInfo->mInstanceList[bestIdx].mTypeInstance;
				auto methodDef = autoComplete->mMethodMatchInfo->mInstanceList[bestIdx].mMethodDef;
				if (methodDef->mMethodDeclaration != NULL)
				{
					auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDef->mMethodDeclaration);
					if (ctorDecl != NULL)
						autoComplete->SetDefinitionLocation(ctorDecl->mThisToken);
					else
						autoComplete->SetDefinitionLocation(methodDef->GetMethodDeclaration()->mNameNode);
				}
				else // Just select type then
					autoComplete->SetDefinitionLocation(typeInst->mTypeDef->mTypeDeclaration->mNameNode);
			}
		}

		if (autoComplete->mGetDefinitionNode != NULL)
		{
			auto astNode = autoComplete->mGetDefinitionNode;
			auto bfSource = autoComplete->mGetDefinitionNode->GetSourceData()->ToParserData();
			if (bfSource != NULL)
			{
				int line = 0;
				int lineChar = 0;
				bfSource->GetLineCharAtIdx(astNode->GetSrcStart(), line, lineChar);
				autoCompleteResultString += StrFormat("defLoc\t%s\t%d\t%d\n", bfSource->mFileName.c_str(), line, lineChar);
			}
		}

		auto methodMatchInfo = autoComplete->mMethodMatchInfo;
		if ((methodMatchInfo != NULL) && (wantsDocEntry == NULL))
		{
			if (methodMatchInfo->mInstanceList.size() > 0)
			{
				String invokeInfoText;
				invokeInfoText += StrFormat("%d", methodMatchInfo->mBestIdx);
				for (int srcPosIdx = 0; srcPosIdx < (int) methodMatchInfo->mSrcPositions.size(); srcPosIdx++)
					invokeInfoText += StrFormat(" %d", methodMatchInfo->mSrcPositions[srcPosIdx]);
				autoCompleteResultString += "invokeInfo\t";
				autoCompleteResultString += invokeInfoText;
				autoCompleteResultString += "\n";
			}

			int idx = 0;
			for (auto& methodEntry : methodMatchInfo->mInstanceList)
			{				
				String methodText;
				if (methodEntry.mPayloadEnumField != NULL)
				{
					auto payloadFieldDef = methodEntry.mPayloadEnumField->GetFieldDef();
					methodText += payloadFieldDef->mName;
					methodText += "(\x1";

					auto payloadType = methodEntry.mPayloadEnumField->mResolvedType;
					BF_ASSERT(payloadType->IsTuple());
					if (payloadType->IsTuple())
					{
						auto tupleType = (BfTupleType*)payloadType;
						for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = &tupleType->mFieldInstances[fieldIdx];
							auto fieldDef = fieldInstance->GetFieldDef();
							if (fieldIdx > 0)
								methodText += ",\x1 ";
							methodText += bfModule->TypeToString(fieldInstance->mResolvedType, BfTypeNameFlag_ResolveGenericParamNames);
							if (!fieldDef->IsUnnamedTupleField())
							{
								methodText += " ";
								if (fieldDef->mName.StartsWith("_"))
									methodText += fieldDef->mName.Substring(1);
								else
									methodText += fieldDef->mName;
							}
						}
					}

					methodText += "\x1)";
				}
				else
				{
					BfMethodInstance* methodInstance = NULL;
					if (methodEntry.mMethodDef->mIdx < 0)
					{						
						for (auto localMethod : mContext->mLocalMethodGraveyard)
						{
							if (localMethod->mMethodDef == methodEntry.mMethodDef)
							{
								methodInstance = localMethod->mMethodInstanceGroup->mDefault;
								break;
							}
						}						
					}
					else
						methodInstance = bfModule->GetRawMethodInstanceAtIdx(methodEntry.mTypeInstance, methodEntry.mMethodDef->mIdx);
					auto curMethodInstance = methodInstance;
					curMethodInstance = methodMatchInfo->mCurMethodInstance;

					SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(bfModule->mCurTypeInstance, methodMatchInfo->mCurTypeInstance);
					SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(bfModule->mCurMethodInstance, curMethodInstance);

					Array<String> genericMethodNameOverrides;
					Array<String>* genericMethodNameOverridesPtr = NULL;

					if (methodInstance->GetNumGenericArguments() != 0)
					{
						genericMethodNameOverridesPtr = &genericMethodNameOverrides;

						for (int methodGenericArgIdx = 0; methodGenericArgIdx < (int)methodInstance->GetNumGenericArguments(); methodGenericArgIdx++)
						{
							BfType* methodGenericArg = NULL;
							if (methodEntry.mGenericArguments.size() > 0)
								methodGenericArg = methodEntry.mGenericArguments[methodGenericArgIdx];

							String argName;
							if (methodGenericArg == NULL)
								argName = methodInstance->mMethodDef->mGenericParams[methodGenericArgIdx]->mName;
							else
								argName = bfModule->TypeToString(methodGenericArg, BfTypeNameFlag_ResolveGenericParamNames, NULL);
							genericMethodNameOverrides.push_back(argName);
						}
					}

					if (methodInstance->mMethodDef->mMethodType != BfMethodType_Ctor)
					{
						if (methodInstance->mReturnType != NULL)
							methodText += bfModule->TypeToString(methodInstance->mReturnType, BfTypeNameFlag_ResolveGenericParamNames, genericMethodNameOverridesPtr);
						else
							methodText += BfTypeUtils::TypeToString(methodInstance->mMethodDef->mReturnTypeRef);
						methodText += " ";
					}
					if (methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)
						methodText += "this";
					else
					{
						auto methodName = methodInstance->mMethodDef->mName;
						int splitIdx = (int)methodName.IndexOf('@');
						if (splitIdx != -1)
							methodText += methodName.Substring(0, splitIdx);
						else
							methodText += methodName;
					}
					if (methodInstance->GetNumGenericArguments() != 0)
					{
						methodText += "<";
						for (int methodGenericArgIdx = 0; methodGenericArgIdx < (int)methodInstance->GetNumGenericArguments(); methodGenericArgIdx++)
						{
							if (methodGenericArgIdx > 0)
								methodText += ", ";
							methodText += genericMethodNameOverrides[methodGenericArgIdx];
						}
						methodText += ">";
					}
					
					//TODO: Show default param values also
					methodText += "(\x1";
					if (methodInstance->GetParamCount() == 0)
					{
						// Hm - is this ever useful?  Messes up some cases actually
						// If param resolution failed then we need to print the original param def
						/*for (int paramIdx = 0; paramIdx < (int) methodInstance->mMethodDef->mParams.size(); paramIdx++)
						{
							if (paramIdx > 0)
								methodText += ",\x1 ";
							auto paramDef = methodInstance->mMethodDef->mParams[paramIdx];
							methodText += BfTypeUtils::TypeToString(paramDef->mTypeRef);
							methodText += " ";
							methodText += paramDef->mName;
						}*/
					}

					int dispParamIdx = 0;

					for (int paramIdx = 0; paramIdx < (int)methodInstance->GetParamCount(); paramIdx++)
					{
						auto paramKind = methodInstance->GetParamKind(paramIdx);
						if ((paramKind == BfParamKind_ImplicitCapture) || (paramKind == BfParamKind_AppendIdx))
							continue;

						if (dispParamIdx > 0)
							methodText += ",\x1 ";

						auto type = methodInstance->GetParamType(paramIdx);						
						BfExpression* paramInitializer = methodInstance->GetParamInitializer(paramIdx);

						if (paramInitializer != NULL)
							methodText += "[";

						if (paramKind == BfParamKind_Params)
							methodText += "params ";

						if (type->IsGenericParam())
						{
							auto genericParamType = (BfGenericParamType*)type;
							if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
							{
								BfMethodInstance* checkMethodInstance = methodInstance;
								if (checkMethodInstance->GetNumGenericParams() == 0)
									checkMethodInstance = methodEntry.mCurMethodInstance;

								if (genericParamType->mGenericParamIdx < checkMethodInstance->GetNumGenericParams())
								{
									auto genericParamInstance = checkMethodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
									methodText += genericParamInstance->GetGenericParamDef()->mName;
								}
								else
								{
									methodText += StrFormat("@M%d", genericParamType->mGenericParamIdx);
								}
							}
							else
							{
								BfGenericTypeInstance* genericType = methodEntry.mTypeInstance->ToGenericTypeInstance();
								if (genericType == NULL)
								{
									if (methodEntry.mCurMethodInstance != NULL)
										genericType = methodEntry.mCurMethodInstance->GetOwner()->ToGenericTypeInstance();
								}

								if ((genericType != NULL) && (genericParamType->mGenericParamIdx < (int)genericType->mGenericParams.size()))
								{
									auto genericParamInstance = genericType->mGenericParams[genericParamType->mGenericParamIdx];
									methodText += genericParamInstance->GetGenericParamDef()->mName;
								}
								else
								{
									methodText += StrFormat("@T%d", genericParamType->mGenericParamIdx);
								}
							}
						}
						else
							methodText += bfModule->TypeToString(type, BfTypeNameFlag_ResolveGenericParamNames, genericMethodNameOverridesPtr);
						methodText += " ";
						methodText += methodInstance->GetParamName(paramIdx);

						if (paramInitializer != NULL)
						{
							methodText += " = ";
							methodText += paramInitializer->ToString();
							methodText += "]";
						}
						dispParamIdx++;
					}
					methodText += "\x1)";
				}

				if (methodEntry.mMethodDef != NULL)
				{
					auto methodDeclaration = methodEntry.mMethodDef->GetMethodDeclaration();
					if ((methodDeclaration != NULL) && (methodDeclaration->mDocumentation != NULL))
					{
						String docString;
						_GetDocString(methodDeclaration->mDocumentation, docString);
						methodText += "\x03";
						methodText += docString;
					}
				}

				autoCompleteResultString += "invoke\t" + methodText + "\n";

				idx++;
			}
		}

		Array<AutoCompleteEntry*> entries;

		for (auto& entry : autoComplete->mEntriesSet)
		{
			entries.Add(&entry);
		}
		std::sort(entries.begin(), entries.end(), [](AutoCompleteEntry* lhs, AutoCompleteEntry* rhs)
			{
				return stricmp(lhs->mDisplay, rhs->mDisplay) < 0;
			});
				
		String docString;
		for (auto entry : entries)
		{
			if ((wantsDocEntry != NULL) && (entry->mDocumentation == NULL))
				continue;

			autoCompleteResultString += String(entry->mEntryType);
			autoCompleteResultString += "\t";
			autoCompleteResultString += String(entry->mDisplay);

			if ((entry->mDocumentation != NULL) && (wantsDocEntry != NULL) && (strcmp(wantsDocEntry, entry->mDisplay) == 0))
			{
				docString.Clear();
				_GetDocString(entry->mDocumentation, docString);
				autoCompleteResultString += '\x03';
				autoCompleteResultString += docString;
			}

			autoCompleteResultString += "\n";
		}
	}
}

String BfCompiler::GetTypeDefList()
{
	String result;

	BfProject* curProject = NULL;
	Dictionary<BfProject*, int> projectIds;

	for (auto typeDef : mSystem->mTypeDefs)
	{		
		if (typeDef->mProject != curProject)
		{
			curProject = typeDef->mProject;
			int* projectIdPtr;
			if (projectIds.TryAdd(curProject, NULL, &projectIdPtr))
			{
				*projectIdPtr = (int)projectIds.size() - 1;
				result += "+";
				result += curProject->mName;
				result += "\n";
			}
			else
			{
				char str[32];
				sprintf(str, "=%d\n", *projectIdPtr);
				result += str;
			}
		}
		
		if (((!typeDef->mIsPartial) || (typeDef->mIsCombinedPartial)))
		{
			if (typeDef->IsGlobalsContainer())
			{
				result += "g";
				if (!typeDef->mNamespace.IsEmpty())
				{
					typeDef->mNamespace.ToString(result);
					result += ".";
				}
				result += ":static\n";
				continue;
			}
			else if (typeDef->mTypeCode == BfTypeCode_Interface)
				result += "i";
			else if (typeDef->mTypeCode == BfTypeCode_Object)
				result += "c";
			else
				result += "v";
			result += BfTypeUtils::TypeToString(typeDef, BfTypeNameFlag_InternalName) + "\n";
		}
	}

	return result;
}

struct TypeDefMatchHelper
{
public:
	StringImpl& mResult;
	Array<String> mSearch;
	uint32 mFoundFlags;
	int32 mFoundCount;
	bool mHasDotSearch;

	String mCurTypeName;
	String mTempStr;

public:
	TypeDefMatchHelper(StringImpl& str) : mResult(str)
	{
		mFoundFlags = 0;
		mFoundCount = 0;
		mHasDotSearch = false;
	}

	void Sanitize(StringImpl& str)
	{
		for (int i = 0; i < (int)str.length(); i++)
		{
			char c = str[i];
			if (c < (char)32)
			{
				str[i] = ' ';
			}
		}
	}

	void AddParams(BfMethodDef* methodDef)
	{
		int visParamIdx = 0;
		for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size(); paramIdx++)
		{
			auto paramDef = methodDef->mParams[paramIdx];
			if ((paramDef->mParamKind == BfParamKind_AppendIdx) || (paramDef->mParamKind == BfParamKind_ImplicitCapture))
				continue;
			if (visParamIdx > 0)
				mResult += ", ";

			StringT<64> refName;
			paramDef->mTypeRef->ToString(refName);
			Sanitize(refName);
			mResult += refName;

			mResult += " ";
			mResult += paramDef->mName;
			visParamIdx++;
		}
	}

	void AddLocation(BfAstNode* node)
	{
		if (node == NULL)
			return;
		auto parserData = node->GetSourceData()->ToParserData();
		if (parserData != NULL)
		{
			mResult += parserData->mFileName;

			int lineNum = 0;
			int column = 0;
			parserData->GetLineCharAtIdx(node->GetSrcStart(), lineNum, column);
			mResult += StrFormat("\t%d\t%d", lineNum, column);
		}
	};

	void AddFieldDef(BfFieldDef* fieldDef)
	{
		mResult += "\t";
		AddLocation(fieldDef->GetRefNode());
		mResult += "\n";
	}

	void AddPropertyDef(BfTypeDef* typeDef, BfPropertyDef* propDef)
	{		
		if (propDef->mName == "[]")
		{
			mResult += "[";

			for (auto methodDef : propDef->mMethods)
			{
				if (methodDef->mMethodType == BfMethodType_PropertyGetter)
				{
					AddParams(methodDef);
					break;
				}
			}

			mResult += "]";
		}
		else
			mResult += propDef->mName;
		mResult += "\t";
		auto refNode = propDef->GetRefNode();
		if (refNode == NULL)
			refNode = typeDef->GetRefNode();
		AddLocation(refNode);
		mResult += "\n";
	}

	void AddMethodDef(BfMethodDef* methodDef)
	{
		if (methodDef->mMethodType == BfMethodType_Ctor)
		{
			if (methodDef->mIsStatic)
				mResult += "static ";
			mResult += "this";
		}
		else if (methodDef->mMethodType == BfMethodType_Dtor)
		{
			if (methodDef->mIsStatic)
				mResult += "static ";
			mResult += "~this";
		}
		else
			mResult += methodDef->mName;
		if (methodDef->mMethodType == BfMethodType_Mixin)
			mResult += "!";
		mResult += "(";
		AddParams(methodDef);
		mResult += ")";
		mResult += "\t";
		AddLocation(methodDef->GetRefNode());

		mResult += "\n";
	}

	void ClearResults()
	{
		mFoundFlags = 0;
		mFoundCount = 0;
	}

	bool MergeFlags(uint32 flags)
	{
		int flagIdx = 0;
		while (flags > 0)
		{
			if (((flags & 1) != 0) && ((mFoundFlags & (1 << flagIdx)) == 0))
			{
				mFoundFlags |= (1 << flagIdx);
				mFoundCount++;
			}

			flags >>= 1;
			flagIdx++;
		}
		return mFoundCount == mSearch.mSize;
	}

	uint32 CheckMatch(const StringView& str)
	{
		uint32 matchFlags = 0;
		for (int i = 0; i < mSearch.mSize; i++)
		{
			if (((mFoundFlags & (1 << i)) == 0) && (str.IndexOf(mSearch[i], true) != -1))
			{
				mFoundCount++;
				matchFlags |= (1 << i);
				mFoundFlags |= (1 << i);				
			}
		}
		return matchFlags;
	}

	bool CheckCompletesMatch(BfAtomComposite& name)
	{
		for (int i = 0; i < name.mSize; i++)
		{			
			CheckMatch(name.mParts[i]->mString);
			if (mFoundCount == mSearch.mSize)
				return true;
		}
		return false;		
	}

	bool IsFullMatch()
	{
		return mFoundCount == mSearch.mSize;
	}
	
	bool CheckMemberMatch(BfTypeDef* typeDef, const StringView& str)
	{
		if (CheckMatch(str) == 0)
		{
			if (mHasDotSearch)
			{
				mTempStr.Clear();
				mTempStr += mCurTypeName;
				mTempStr += ".";
				mTempStr += str;
				if (CheckMatch(mTempStr) == 0)
					return false;
			}
			else
				return false;
		}

		if ((IsFullMatch()) || (CheckCompletesMatch(typeDef->mFullName)))
			return true;
		return false;
	}
};

String BfCompiler::GetTypeDefMatches(const StringImpl& searchStr)
{
	String result;
	TypeDefMatchHelper matchHelper(result);

	//
	{
		int searchIdx = 0;
		while (searchIdx < (int)searchStr.length())
		{
			int spacePos = (int)searchStr.IndexOf(' ', searchIdx);
			String str;
			if (spacePos == -1)			
				str = searchStr.Substring(searchIdx);			
			else			
				str = searchStr.Substring(searchIdx, spacePos - searchIdx);			
			str.Trim();
			if (!str.IsEmpty())
				matchHelper.mSearch.Add(str);
			if (str.Contains('.'))
				matchHelper.mHasDotSearch = true;
			if (spacePos == -1)
				break;
			searchIdx = spacePos + 1;
		}

		//// We sort from longest to shortest to make sure longer strings match before shorter, which 
		////  matters when the shorter string is a subset of the longer string
		//matchHelper.mSearch.Sort([](const String& lhs, const String& rhs)
		//	{
		//		int lenCmp = (int)(rhs.length() - lhs.length());
		//		if (lenCmp != 0)
		//			return lenCmp < 0;
		//		return lhs < rhs;
		//	});
	}

	BfProject* curProject = NULL;
	Dictionary<BfProject*, int> projectIds;

	Dictionary<BfAtom*, int> atomMatchMap;

	struct ProjectInfo
	{
		Dictionary<String, int> matchedNames;
	};

	Array<ProjectInfo> projectInfos;
	projectInfos.Resize(mSystem->mProjects.size());
	
	String typeName;
	String foundName;
	int partialIdx = 0;

	for (auto typeDef : mSystem->mTypeDefs)
	{		
		if (typeDef->mIsPartial)
			continue;

		bool fullyMatchesName = false;

		if (matchHelper.mHasDotSearch)
		{
			matchHelper.mCurTypeName.Clear();
			typeDef->mFullName.ToString(matchHelper.mCurTypeName);
			
			matchHelper.ClearResults();
			matchHelper.CheckMatch(matchHelper.mCurTypeName);
			fullyMatchesName = matchHelper.IsFullMatch();				
		}

		int matchIdx = -1;
		//BfAtomComposite foundComposite;

		if (!fullyMatchesName)
		{
			for (auto fieldDef : typeDef->mFields)
			{
				matchHelper.ClearResults();

				bool hasMatch = false;
				if (matchHelper.CheckMemberMatch(typeDef, fieldDef->mName))
				{
					result += "F";
					if (BfTypeUtils::TypeToString(result, typeDef, (BfTypeNameFlags)(BfTypeNameFlag_HideGlobalName | BfTypeNameFlag_InternalName)))
						result += ".";
					result += fieldDef->mName;
					matchHelper.AddFieldDef(fieldDef);
				}
			}

			for (auto propDef : typeDef->mProperties)
			{
				if (propDef->GetRefNode() == NULL)
					continue;

				matchHelper.ClearResults();
				if (matchHelper.CheckMemberMatch(typeDef, propDef->mName))
				{
					result += "P";
					if (BfTypeUtils::TypeToString(result, typeDef, (BfTypeNameFlags)(BfTypeNameFlag_HideGlobalName | BfTypeNameFlag_InternalName)))
						result += ".";
					matchHelper.AddPropertyDef(typeDef, propDef);
				}
			}

			for (auto methodDef : typeDef->mMethods)
			{
				if ((methodDef->mMethodType != BfMethodType_Normal) &&
					(methodDef->mMethodType != BfMethodType_Mixin) &&
					(methodDef->mMethodType != BfMethodType_Ctor) &&
					(methodDef->mMethodType != BfMethodType_Dtor))
					continue;

				if (methodDef->mMethodDeclaration == NULL)
					continue;

				matchHelper.ClearResults();
				if (matchHelper.CheckMemberMatch(typeDef, methodDef->mName))
				{
					result += "M";
					if (BfTypeUtils::TypeToString(result, typeDef, (BfTypeNameFlags)(BfTypeNameFlag_HideGlobalName | BfTypeNameFlag_InternalName)))
						result += ".";
					matchHelper.AddMethodDef(methodDef);
				}
			}
			
			uint32 matchFlags = 0;
			for (int atomIdx = typeDef->mFullName.mSize - 1; atomIdx >= 0; atomIdx--)
			{
				auto atom = typeDef->mFullName.mParts[atomIdx];
				int* matchesPtr = NULL;
				if (atomMatchMap.TryAdd(atom, NULL, &matchesPtr))
				{
					matchHelper.ClearResults();
					*matchesPtr = matchHelper.CheckMatch(atom->mString);
				}

				if (*matchesPtr != 0)
				{
					if (matchIdx == -1)
						matchIdx = atomIdx;
					matchFlags |= *matchesPtr;
				}
			}

			matchHelper.ClearResults();
			if (!matchHelper.MergeFlags(matchFlags))
			{
				continue;
			}			
		}
		
		if (typeDef->mProject != curProject)
		{
			curProject = typeDef->mProject;
			int* projectIdPtr;
			if (projectIds.TryAdd(curProject, NULL, &projectIdPtr))
			{
				*projectIdPtr = (int)projectIds.size() - 1;
				result += "+";
				result += curProject->mName;
				result += "\n";
			}
			else
			{
				char str[32];
				sprintf(str, "=%d\n", *projectIdPtr);
				result += str;
			}
		}

		typeName = BfTypeUtils::TypeToString(typeDef, BfTypeNameFlag_InternalName);

		if (matchIdx != -1)
		{
			int* matchIdxPtr = 0;
			auto projectInfo = &projectInfos[typeDef->mProject->mIdx];

			int dotCount = 0;
			foundName = typeName;
			for (int i = 0; i < (int)typeName.length(); i++)
			{
				if (typeName[i] == '.')
				{
					if (dotCount == matchIdx)
					{
						foundName.Clear();
						foundName.Append(typeName.c_str(), i);
						break;
					}

					dotCount++;					
				}
			}

			if (projectInfo->matchedNames.TryAdd(foundName, NULL, &matchIdxPtr))
			{
				*matchIdxPtr = partialIdx++;
				result += StrFormat(">%d@", matchIdx);
			}
			else
			{
				result += StrFormat("<%d@", *matchIdxPtr);
			} 
		}
		else
		{
			result += ":";
		}
		
		if (typeDef->IsGlobalsContainer())
		{
			result += "g";
			if (!typeDef->mNamespace.IsEmpty())
			{
				typeDef->mNamespace.ToString(result);
				result += ".";
			}
			result += ":static\n";
			continue;
		}
		else if (typeDef->mTypeCode == BfTypeCode_Interface)
			result += "i";
		else if (typeDef->mTypeCode == BfTypeCode_Object)
			result += "c";
		else
			result += "v";
		result += typeName + "\n";		
	}

	return result;
}

String BfCompiler::GetTypeDefInfo(const StringImpl& inTypeName)
{
	BfProject* project = NULL;	
	int idx = 0;

	int sep = (int)inTypeName.IndexOf(':');
	if (sep != -1)
	{
		idx = sep + 1;
		project = mSystem->GetProject(inTypeName.Substring(0, sep));
	}

	String typeName;
	int genericCount = 0;
	int pendingGenericCount = 0;
	for ( ; idx < (int)inTypeName.length(); idx++)
	{
		char c = inTypeName[idx];
		if (c == '<')
			genericCount = 1;
		else if (genericCount > 0)
		{
			if (c == ',')
				genericCount++;
			else if (c == '>')
			{
				pendingGenericCount = genericCount;				
				genericCount = 0;
			}
		}
		else
		{
			if (pendingGenericCount != 0)
			{
				typeName += StrFormat("`%d", pendingGenericCount);
				pendingGenericCount = 0;
			}
			typeName += c;
		}
	}
	
	bool isGlobals = false;
	if (typeName == ":static")
	{		
		typeName.clear();
		isGlobals = true;
	}
	if (typeName.EndsWith(".:static"))
	{
		typeName.RemoveToEnd(typeName.length() - 8);
		isGlobals = true;
	}

	for (int i = 0; i < (int)typeName.length(); i++)
		if (typeName[i] == '+')
			typeName[i] = '.';
	
	String result;
	TypeDefMatchHelper matchHelper(result);
	
	BfAtomComposite nameComposite;
	if ((typeName.IsEmpty()) || (mSystem->ParseAtomComposite(typeName, nameComposite)))
	{		
		auto itr = mSystem->mTypeDefs.TryGet(nameComposite);
		while (itr)
		{			
			auto typeDef = *itr;
			if ((!typeDef->mIsPartial) &&
				(typeDef->mProject == project) &&
				(typeDef->mFullName == nameComposite) &&
				(typeDef->IsGlobalsContainer() == isGlobals) &&
				(typeDef->GetSelfGenericParamCount() == pendingGenericCount))
			{				
				auto refNode = typeDef->GetRefNode();
				result += "S";
				matchHelper.AddLocation(refNode);
				result += "\n";

				for (auto fieldDef : typeDef->mFields)
				{
					result += "F";
					result += fieldDef->mName;
					matchHelper.AddFieldDef(fieldDef);
				}

				for (auto propDef : typeDef->mProperties)
				{	
					if (propDef->GetRefNode() == NULL)
						continue;

					result += "P";
					matchHelper.AddPropertyDef(typeDef, propDef);					
				}

				for (auto methodDef : typeDef->mMethods)
				{
					if ((methodDef->mMethodType != BfMethodType_Normal) &&
						(methodDef->mMethodType != BfMethodType_Mixin) &&
						(methodDef->mMethodType != BfMethodType_Ctor) &&
						(methodDef->mMethodType != BfMethodType_Dtor))
						continue;

					if (methodDef->mMethodDeclaration == NULL)
						continue;					

					result += "M";
					matchHelper.AddMethodDef(methodDef);					
				}
			}

			itr.MoveToNextHashMatch();
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////

PerfManager* BfGetPerfManager(BfParser* bfParser);

/*BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetDefaultTargetTriple(BfCompiler* bfCompiler)
{
	String& autoCompleteResultString = *gTLStrReturn.Get();
	return autoCompleteResultString.c_str();
}*/

BF_EXPORT bool BF_CALLTYPE BfCompiler_Compile(BfCompiler* bfCompiler, BfPassInstance* bfPassInstance, const char* outputPath)
{
	BP_ZONE("BfCompiler_Compile");
	
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(bfCompiler->mPassInstance, bfPassInstance);
	bfCompiler->mPassInstance = bfPassInstance;
	bfCompiler->Compile(outputPath);
	return !bfCompiler->mPassInstance->HasFailed();
}

BF_EXPORT void BF_CALLTYPE BfCompiler_ClearResults(BfCompiler* bfCompiler)
{
	bfCompiler->ClearResults();
}

BF_EXPORT bool BF_CALLTYPE BfCompiler_ClassifySource(BfCompiler* bfCompiler, BfPassInstance* bfPassInstance, BfParser* bfParser, BfResolvePassData* resolvePassData, BfSourceClassifier::CharData* charData)
{
	BP_ZONE("BfCompiler_ClassifySource");	
	BfSourceClassifier bfSourceClassifier(bfParser, charData);
	bfSourceClassifier.mClassifierPassId = bfPassInstance->mClassifierPassId;
	
	String& autoCompleteResultString = *gTLStrReturn.Get();
	autoCompleteResultString.clear();

	bool doClassifyPass = (charData != NULL) && (resolvePassData->mResolveType <= BfResolveType_Autocomplete_HighPri); 
	bfSourceClassifier.mEnabled = doClassifyPass;

	// Full classifier pass?
	
	bfSourceClassifier.mSkipMethodInternals = true;	
	bfSourceClassifier.mSkipTypeDeclarations = true;	
	if ((charData != NULL) && (doClassifyPass))
		bfSourceClassifier.Visit(bfParser->mRootNode);
	bfSourceClassifier.mSkipTypeDeclarations = false;
	bfSourceClassifier.mSkipMethodInternals = false;		
	
	
	if (charData != NULL)
		resolvePassData->mSourceClassifier = &bfSourceClassifier;	

	bfPassInstance->mFilterErrorsTo = bfParser;
	bfPassInstance->mTrimMessagesToCursor = true;
	SetAndRestoreValue<BfResolvePassData*> prevCompilerResolvePassData(bfCompiler->mResolvePassData, resolvePassData);
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(bfCompiler->mPassInstance, bfPassInstance);
	bool canceled = false;
	if (resolvePassData->mAutoComplete != NULL)
	{		
		bfCompiler->ProcessAutocompleteTempType();
	}
	else
		canceled = !bfCompiler->Compile("");	
	resolvePassData->mSourceClassifier = NULL;
		
	if ((charData != NULL) && (doClassifyPass))
	{
		bfSourceClassifier.mIsSideChannel = false;
		bfSourceClassifier.Visit(bfParser->mErrorRootNode);

		bfSourceClassifier.mIsSideChannel = true;
		bfSourceClassifier.Visit(bfParser->mSidechannelRootNode);				
	}

	return !canceled;
}

BF_EXPORT bool BF_CALLTYPE BfCompiler_VerifyTypeName(BfCompiler* bfCompiler, char* name, int cursorPos)
{
	String typeName = name;

	auto system = bfCompiler->mSystem;
	AutoCrit autoCrit(system->mSystemLock);

	String& autoCompleteResultString = *gTLStrReturn.Get();
	autoCompleteResultString.Clear();

	BfPassInstance passInstance(bfCompiler->mSystem);

	BfParser parser(bfCompiler->mSystem);
	parser.SetSource(typeName.c_str(), (int)typeName.length());	
	parser.Parse(&passInstance);
	parser.mCursorIdx = cursorPos;
	parser.mCursorCheckIdx = cursorPos;

	BfReducer reducer;
	reducer.mAlloc = parser.mAlloc;
	reducer.mPassInstance = &passInstance;
	reducer.mAllowTypeWildcard = true;
		
	if (parser.mRootNode->mChildArr.mSize == 0)
		return false;

	bool attribWasClosed = false;
	bool isAttributeRef = false;
	auto firstNode = parser.mRootNode->mChildArr[0];
	auto endIdx = parser.mRootNode->mSrcEnd;
	reducer.mVisitorPos = BfReducer::BfVisitorPos(parser.mRootNode);
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(firstNode))
	{
		if (tokenNode->mToken == BfToken_LBracket)
		{
			if (auto lastToken = BfNodeDynCast<BfTokenNode>(parser.mRootNode->mChildArr.back()))
			{
				if (lastToken->mToken == BfToken_RBracket)
				{
					attribWasClosed = true;
					endIdx = lastToken->mSrcStart;
				}
			}

			isAttributeRef = true;
			if (parser.mRootNode->mChildArr.mSize < 2)
				return false;
			firstNode = parser.mRootNode->mChildArr[1];
			reducer.mVisitorPos.MoveNext();
		}
	}
	
	reducer.mVisitorPos.MoveNext();
	auto typeRef = reducer.CreateTypeRef(firstNode);
	if (typeRef == NULL)
		return false;

	BfResolvePassData resolvePassData;
	if (cursorPos != -1)
	{
		resolvePassData.mResolveType = BfResolveType_Autocomplete;
		parser.mParserFlags = (BfParserFlag)(parser.mParserFlags | ParserFlag_Autocomplete);		
		resolvePassData.mAutoComplete = new BfAutoComplete();		
		resolvePassData.mAutoComplete->mSystem = bfCompiler->mSystem;
		resolvePassData.mAutoComplete->mCompiler = bfCompiler;
		resolvePassData.mAutoComplete->mModule = bfCompiler->mContext->mScratchModule;
	}
	resolvePassData.mParser = &parser;
			
	SetAndRestoreValue<BfResolvePassData*> prevCompilerResolvePassData(bfCompiler->mResolvePassData, &resolvePassData);
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(bfCompiler->mPassInstance, &passInstance);

	if (resolvePassData.mAutoComplete != NULL)
	{
		if (isAttributeRef)
			resolvePassData.mAutoComplete->CheckAttributeTypeRef(typeRef);
		else
			resolvePassData.mAutoComplete->CheckTypeRef(typeRef, false);
		bfCompiler->GenerateAutocompleteInfo();
	}

	if (passInstance.HasFailed())
		return false;

	if (typeRef->mSrcEnd != endIdx)
		return false;

	if (!bfCompiler->mContext->mScratchModule->ValidateTypeWildcard(typeRef, isAttributeRef))
		return false;

	if ((isAttributeRef) && (!attribWasClosed))
		return false;

	return true;
}

BF_EXPORT void BF_CALLTYPE BfCompiler_ClearCompletionPercentage(BfCompiler* bfCompiler)
{
	bfCompiler->mCompletionPct = 0;
}

BF_EXPORT float BF_CALLTYPE BfCompiler_GetCompletionPercentage(BfCompiler* bfCompiler)
{
	return bfCompiler->mCompletionPct;
}

BF_EXPORT int BF_CALLTYPE BfCompiler_GetCompileRevision(BfCompiler* bfCompiler)
{
	return bfCompiler->mRevision;
}

BF_EXPORT void BF_CALLTYPE BfCompiler_Cancel(BfCompiler* bfCompiler)
{
	bfCompiler->Cancel();
}

BF_EXPORT void BF_CALLTYPE BfCompiler_ClearBuildCache(BfCompiler* bfCompiler)
{
	bfCompiler->ClearBuildCache();
}

BF_EXPORT void BF_CALLTYPE BfCompiler_SetBuildValue(BfCompiler* bfCompiler, char* cacheDir, char* key, char* value)
{
	bfCompiler->mCodeGen.SetBuildValue(cacheDir, key, value);
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetBuildValue(BfCompiler* bfCompiler, char* cacheDir, char* key)
{
	String& outString = *gTLStrReturn.Get();
	outString = bfCompiler->mCodeGen.GetBuildValue(cacheDir, key);
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE BfCompiler_WriteBuildCache(BfCompiler* bfCompiler, char* cacheDir)
{
	bfCompiler->mCodeGen.WriteBuildCache(cacheDir);
}

BF_EXPORT void BF_CALLTYPE BfCompiler_Delete(BfCompiler* bfCompiler)
{		
	delete bfCompiler;	
}

BF_EXPORT void BF_CALLTYPE BfCompiler_ProgramDone()
{
#ifdef BF_PLATFORM_WINDOWS	
	BeLibManager::Get()->Clear();
#endif
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetTypeDefList(BfCompiler* bfCompiler)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	outString = bfCompiler->GetTypeDefList();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetTypeDefMatches(BfCompiler* bfCompiler, const char* searchStr)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	outString = bfCompiler->GetTypeDefMatches(searchStr);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetTypeDefInfo(BfCompiler* bfCompiler, const char* typeDefName)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	outString = bfCompiler->GetTypeDefInfo(typeDefName);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetOutputFileNames(BfCompiler* bfCompiler, BfProject* bfProject, bool* hadOutputChanges)
{	
	BF_FATAL("not used ?");

	*hadOutputChanges = false;
	String& outString = *gTLStrReturn.Get();
	outString.clear();	
	for (auto mainModule : bfCompiler->mContext->mModules)
	{	
		if (!mainModule->mIsReified)
			continue;
		if (mainModule->mProject != bfProject)
			continue;			
		if (bfCompiler->mOptions.mHotProject != NULL)
			continue; // Only add new objs from mCodeGen.mCodeGenFiles during hot reload
			
		for (auto&& moduleFileName : mainModule->mOutFileNames)
		{
			if (!moduleFileName.mModuleWritten)
				continue;
			if (!outString.empty())
				outString += "\n";
			outString += moduleFileName.mFileName;
		}
	}	
	
	if (bfCompiler->mHotState != NULL)
	{
		Array<String> outPaths;

		for (int i = 0; i < (int)bfCompiler->mHotState->mQueuedOutFiles.size(); i++)
		{
			auto& fileEntry = bfCompiler->mHotState->mQueuedOutFiles[i];
			if (fileEntry.mProject != bfProject)
				continue;			
			outPaths.Add(fileEntry.mFileName);
			bfCompiler->mHotState->mQueuedOutFiles.RemoveAtFast(i);
			i--;
		}

		//outPaths.Sort();
		std::sort(outPaths.begin(), outPaths.end(), [](const String& lhs, const String& rhs) { return lhs < rhs; });

		for (auto& path : outPaths)
		{
			if (!outString.empty())
				outString += "\n";
			outString += path;
			outString += BF_OBJ_EXT;
		}
	}

	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetUsedOutputFileNames(BfCompiler* bfCompiler, BfProject* bfProject, bool flushQueuedHotFiles, bool* hadOutputChanges)
{	
	BP_ZONE("BfCompiler_GetUsedOutputFileNames");

	*hadOutputChanges = false;
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	
	Array<BfModule*> moduleList;
	moduleList.Reserve(bfProject->mUsedModules.size());

	if (bfCompiler->mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
	{
		for (auto mainModule : bfCompiler->mContext->mModules)
		{			
			if ((!mainModule->mIsReified) || (mainModule->mIsScratchModule))
				continue;
			if (bfCompiler->mOptions.mHotProject != NULL)
				continue; // Only add new objs from mCodeGen.mCodeGenFiles during hot reload
			if (!bfCompiler->IsModuleAccessible(mainModule, bfProject))
				continue;
			
			moduleList.push_back(mainModule);
		}
	}
	else
	{
		for (auto mainModule : bfProject->mUsedModules)
		{		
			if ((!mainModule->mIsReified) || (mainModule->mIsScratchModule))
				continue;
			if (bfCompiler->mOptions.mHotProject != NULL)
				continue; // Only add new objs from mCodeGen.mCodeGenFiles during hot reload			

			moduleList.push_back(mainModule);
		}
	}

	std::sort(moduleList.begin(), moduleList.end(), [&](BfModule* moduleA, BfModule* moduleB) { return moduleA->mModuleName < moduleB->mModuleName; } );

	HashSet<String> usedFileNames;
	usedFileNames.Reserve(moduleList.size());

	for (auto mainModule : moduleList)
	{	
		for (auto fileNameIdx : mainModule->mImportFileNames)
		{
			auto fileName = bfCompiler->mContext->mStringObjectIdMap[fileNameIdx].mString;
			if (!usedFileNames.TryAdd(fileName, NULL))
				continue;
			if (!outString.empty())
				outString += "\n";
			outString += fileName;
		}

		for (auto&& moduleFileName : mainModule->mOutFileNames)
		{
			if (!moduleFileName.mModuleWritten)
				continue;

			bool canReference = true;
			for (auto project : moduleFileName.mProjects)
			{
				if (!bfProject->ContainsReference(project))
					canReference = false;
 				if (bfProject != project)
 				{
 					if (project->mTargetType == BfTargetType_BeefDynLib)
 						canReference = false;
 				}
			}
			if (!canReference)
				continue;
			
			String fileName = moduleFileName.mFileName;

#ifdef BF_PLATFORM_WINDOWS
			if (moduleFileName.mWroteToLib)
				fileName = BeLibManager::GetLibFilePath(fileName);
#endif			
			if (!usedFileNames.TryAdd(fileName, NULL))
				continue;
			if (!outString.empty())
				outString += "\n";
			outString += fileName;

			if (mainModule->mWroteToLib)
				break;
		}
	}

	if (bfCompiler->mHotState != NULL)
	{
		Array<String> outPaths;

		for (int i = 0; i < (int)bfCompiler->mHotState->mQueuedOutFiles.size(); i++)
		{
			auto& fileEntry = bfCompiler->mHotState->mQueuedOutFiles[i];
			if (fileEntry.mProject != bfProject)
				continue;
			if (!bfCompiler->mHotState->mHotProject->mUsedModules.Contains(fileEntry.mModule))
				continue;
			outPaths.Add(fileEntry.mFileName);

			if (flushQueuedHotFiles)
			{
				bfCompiler->mHotState->mQueuedOutFiles.RemoveAtFast(i);
				i--;
			}
		}

		std::sort(outPaths.begin(), outPaths.end(), [](const String& lhs, const String& rhs) { return lhs < rhs; });

		for (auto& path : outPaths)
		{
			if (!outString.empty())
				outString += "\n";
			outString += path;
			outString += BF_OBJ_EXT;
		}
	}

	for (auto& fileEntry : bfCompiler->mCodeGen.mCodeGenFiles)
	{
		if (fileEntry.mWasCached)
		 	continue;
		if (!bfProject->ContainsReference(fileEntry.mProject))
			continue;
		*hadOutputChanges = true;
	}

	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetAutocompleteInfo(BfCompiler* bfCompiler)
{
	String& autoCompleteResultString = *gTLStrReturn.Get();
	return autoCompleteResultString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_GetSymbolReferences(BfCompiler* bfCompiler, BfPassInstance* bfPassInstance, BfResolvePassData* resolvePassData)
{
	BP_ZONE("BfCompiler_GetSymbolReferences");

	String& outString = *gTLStrReturn.Get();
	outString.clear();
	SetAndRestoreValue<BfResolvePassData*> prevCompilerResolvePassData(bfCompiler->mResolvePassData, resolvePassData);
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(bfCompiler->mPassInstance, bfPassInstance);
	bfCompiler->GetSymbolReferences();

	std::map<String, String*> sortedParserMap;	
	for (auto& parserDataPair : resolvePassData->mFoundSymbolReferencesParserData)
	{
		sortedParserMap.insert(std::make_pair(parserDataPair.mKey->mFileName, &parserDataPair.mValue));
	}

	for (auto& parserData : sortedParserMap)
	{
		if (!outString.empty())
			outString += "\n";
		outString += parserData.first + "\t" + *(parserData.second);
	}
	return outString.c_str();
}

BF_EXPORT bool BF_CALLTYPE BfCompiler_GetHasHotPendingDataChanges(BfCompiler* bfCompiler)
{
	return (bfCompiler->mHotState != NULL) && 
		((!bfCompiler->mHotState->mPendingDataChanges.IsEmpty()) || (!bfCompiler->mHotState->mPendingFailedSlottings.IsEmpty()));
}

BF_EXPORT void BF_CALLTYPE BfCompiler_HotCommit(BfCompiler* bfCompiler)
{
	bfCompiler->HotCommit();
}

BF_EXPORT void BF_CALLTYPE BfCompiler_HotResolve_Start(BfCompiler* bfCompiler, int flags)
{
	bfCompiler->HotResolve_Start((BfCompiler::HotResolveFlags)flags);
}

BF_EXPORT void BF_CALLTYPE BfCompiler_HotResolve_AddActiveMethod(BfCompiler* bfCompiler, const char* methodName)
{
	bfCompiler->HotResolve_AddActiveMethod(methodName);
}

BF_EXPORT void BF_CALLTYPE BfCompiler_HotResolve_AddDelegateMethod(BfCompiler* bfCompiler, const char* methodName)
{
	bfCompiler->HotResolve_AddDelegateMethod(methodName);
}

// 0: heap, 1: user stated 'not used', 2: user stated 'used'
BF_EXPORT void BF_CALLTYPE BfCompiler_HotResolve_ReportType(BfCompiler* bfCompiler, int typeId, int usageKind)
{
	bfCompiler->HotResolve_ReportType(typeId, (BfCompiler::HotTypeFlags)usageKind);
}

// 0: heap, 1: user stated 'used', 2: user stated 'not used'
BF_EXPORT void BF_CALLTYPE BfCompiler_HotResolve_ReportTypeRange(BfCompiler* bfCompiler, const char* typeName, int usageKind)
{
	//TODO: Implement
}

BF_EXPORT const char* BF_CALLTYPE BfCompiler_HotResolve_Finish(BfCompiler* bfCompiler)
{
	String& outString = *gTLStrReturn.Get();
	outString = bfCompiler->HotResolve_Finish();
	return outString.c_str();
}

static BfPlatformType GetPlatform(StringView str)
{	
	while (!str.IsEmpty())
	{
		char c = str[str.mLength - 1];
		if (((c >= '0') && (c <= '9')) || (c == '.'))
			str.RemoveFromEnd(1);
		else
			break;
	}

	bool hasLinux = false;

	for (auto elem : str.Split('-'))
	{
		if (elem == "linux")
			hasLinux = true;
		else if (elem == "windows")
			return BfPlatformType_Windows;
		else if (elem == "macosx")
			return BfPlatformType_macOS;
		else if (elem == "ios")
			return BfPlatformType_iOS;
		else if ((elem == "android") || (elem == "androideabi"))
			return BfPlatformType_Android;
	}

	if (hasLinux)
		return BfPlatformType_Linux;
	return BfPlatformType_Unknown;
}

BF_EXPORT void BF_CALLTYPE BfCompiler_SetOptions(BfCompiler* bfCompiler, BfProject* hotProject, int hotIdx,
	const char* targetTriple, int toolsetType, int simdSetting, int allocStackCount, int maxWorkerThreads,
	BfCompilerOptionFlags optionFlags, char* mallocLinkName, char* freeLinkName)
{
	BfLogSys(bfCompiler->mSystem, "BfCompiler_SetOptions\n");

	//printf("BfCompiler_SetOptions Threads:%d\n", maxWorkerThreads);
	
	auto options = &bfCompiler->mOptions;	
	
	options->mErrorString.Clear();
	options->mHotProject = hotProject;
	options->mHotCompileIdx = hotIdx;
	options->mTargetTriple = targetTriple;

	if (options->mTargetTriple.StartsWith("x86_64-"))
		options->mMachineType = BfMachineType_x64;
	else if (options->mTargetTriple.StartsWith("i686-"))
		options->mMachineType = BfMachineType_x86;
	else if ((options->mTargetTriple.StartsWith("arm64")) || (options->mTargetTriple.StartsWith("aarch64")))
		options->mMachineType = BfMachineType_AArch64;
	else if (options->mTargetTriple.StartsWith("armv"))
		options->mMachineType = BfMachineType_ARM;
	else
		options->mMachineType = BfMachineType_x64; // Default

	options->mPlatformType = GetPlatform(options->mTargetTriple);

	options->mCLongSize = 4;
	if ((options->mMachineType == BfMachineType_AArch64) || (options->mMachineType == BfMachineType_x64))
	{
		if ((options->mPlatformType == BfPlatformType_macOS) || (options->mPlatformType == BfPlatformType_iOS) || 
			(options->mPlatformType == BfPlatformType_Linux) || (options->mPlatformType == BfPlatformType_Android))
			options->mCLongSize = 8;
	}	
	
	bfCompiler->mCodeGen.SetMaxThreads(maxWorkerThreads);

	if (!bfCompiler->mIsResolveOnly)
	{
		bool allowHotSwapping = (optionFlags & BfCompilerOptionFlag_EnableHotSwapping) != 0;
		bool emitDebugInfo = (optionFlags & BfCompilerOptionFlag_EmitDebugInfo) != 0;

		// These settings only matter for code generation, they are not applicable for resolveOnly
		options->mCompileOnDemandKind = BfCompileOnDemandKind_ResolveUnused;
		//options->mCompileOnDemandKind = BfCompileOnDemandKind_AlwaysInclude;			
		options->mToolsetType = (BfToolsetType)toolsetType;
		options->mSIMDSetting = (BfSIMDSetting)simdSetting;
		options->mIncrementalBuild = (optionFlags & BfCompilerOptionFlag_IncrementalBuild) != 0;		
		options->mWriteIR = (optionFlags & BfCompilerOptionFlag_WriteIR) != 0;
		options->mGenerateObj = (optionFlags & BfCompilerOptionFlag_GenerateOBJ) != 0;
		options->mGenerateBitcode = (optionFlags & BfCompilerOptionFlag_GenerateBitcode) != 0;
		options->mNoFramePointerElim = (optionFlags & BfCompilerOptionFlag_NoFramePointerElim) != 0;
		options->mInitLocalVariables = (optionFlags & BfCompilerOptionFlag_ClearLocalVars) != 0;
		options->mRuntimeChecks = (optionFlags & BfCompilerOptionFlag_RuntimeChecks) != 0;
		options->mEmitDynamicCastCheck = (optionFlags & BfCompilerOptionFlag_EmitDynamicCastCheck) != 0;
		options->mObjectHasDebugFlags = (optionFlags & BfCompilerOptionFlag_EnableObjectDebugFlags) != 0;
		options->mEnableRealtimeLeakCheck = ((optionFlags & BfCompilerOptionFlag_EnableRealtimeLeakCheck) != 0) && options->mObjectHasDebugFlags;
		options->mDebugAlloc = ((optionFlags & BfCompilerOptionFlag_DebugAlloc) != 0) || options->mEnableRealtimeLeakCheck;
		options->mOmitDebugHelpers = (optionFlags & BfCompilerOptionFlag_OmitDebugHelpers) != 0;

#ifdef _WINDOWS
// 		if (options->mToolsetType == BfToolsetType_GNU)
// 		{
// 			options->mErrorString = "Toolset 'GNU' is not available on this platform. Consider changing 'Workspace/General/Toolset'.";
// 		}
#else
// 		if (options->mToolsetType == BfToolsetType_Microsoft)
// 		{
// 			options->mErrorString = "Toolset 'Microsoft' is not available on this platform. Consider changing 'Workspace/General/Toolset'.";
// 		}
		BF_ASSERT(!options->mEnableRealtimeLeakCheck);		
#endif
		options->mEmitObjectAccessCheck = (optionFlags & BfCompilerOptionFlag_EmitDebugInfo) != 0;
		options->mAllocStackCount = allocStackCount;
		
		if (hotProject != NULL)
		{
			String errorName;
			if (options->mAllowHotSwapping != allowHotSwapping)
				errorName = "Hot Compilation Enabled";
			else if (options->mMallocLinkName != mallocLinkName)
				errorName = "Malloc";
			else if (options->mFreeLinkName != freeLinkName)
				errorName = "Free";

			if (!options->mEmitDebugInfo)
			{
				options->mErrorString = "Hot compilation cannot be used when the target is not built with debug information. Consider setting 'Workspace/Beef/Debug/Debug Information' to 'Yes'.";
			}
			else if (!errorName.IsEmpty())
			{
				options->mErrorString = StrFormat("Unable to change option '%s' during hot compilation", errorName.c_str());
			}
		}
		else
		{
			options->mAllowHotSwapping = allowHotSwapping;
			options->mHasVDataExtender = options->mAllowHotSwapping;			
			options->mMallocLinkName = mallocLinkName;
			options->mFreeLinkName = freeLinkName;
			options->mEmitDebugInfo = emitDebugInfo;
			options->mEmitLineInfo = (optionFlags & BfCompilerOptionFlag_EmitLineInfo) != 0;;
			options->mEnableCustodian = (optionFlags & BfCompilerOptionFlag_EnableCustodian) != 0;
			options->mEnableSideStack = (optionFlags & BfCompilerOptionFlag_EnableSideStack) != 0;
		}		
	}
	else
	{
		options->mCompileOnDemandKind = BfCompileOnDemandKind_AlwaysInclude;
		options->mAllowHotSwapping = false;
		options->mObjectHasDebugFlags = false;
		options->mEnableRealtimeLeakCheck = false;
		options->mEmitObjectAccessCheck = false;		
		options->mEmitDynamicCastCheck = false;
		options->mRuntimeChecks = (optionFlags & BfCompilerOptionFlag_RuntimeChecks) != 0;
	}	
}

BF_EXPORT void BF_CALLTYPE BfCompiler_ForceRebuild(BfCompiler* bfCompiler)
{
	bfCompiler->mOptions.mForceRebuildIdx++;
}


