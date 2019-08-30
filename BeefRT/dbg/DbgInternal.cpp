#pragma warning(disable:4996)
#define HEAPHOOK

#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

//#define OBJECT_GUARD_END_SIZE 8
#define OBJECT_GUARD_END_SIZE 0

//#define BF_USE_STOMP_ALLOC 1

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReturnAddress) 
#define BF_RETURN_ADDRESS _ReturnAddress()
#else
#define BF_RETURN_ADDRESS __builtin_return_address(0)
#endif

#include "BeefySysLib/Common.h"
#include "../rt/BfObjects.h"
#include "gc.h"
#include "../rt/StompAlloc.h"
#include "BeefySysLib/platform/PlatformHelper.h"

USING_NS_BF;

#ifdef BF_PLATFORM_WINDOWS
bf::System::Runtime::BfRtCallbacks gBfRtDbgCallbacks;
BfRtFlags gBfRtDbgFlags = (BfRtFlags)0;
#endif

namespace bf
{
	namespace System
	{
		class Object;
		class Exception;

		class Internal
		{		
		public:						
			BFRT_EXPORT static intptr Dbg_PrepareStackTrace(intptr baseAllocSize, intptr maxStackTraceDepth);
			BFRT_EXPORT void Dbg_ReserveMetadataBytes(intptr metadataBytes, intptr& curAllocBytes);
			BFRT_EXPORT void* Dbg_GetMetadata(System::Object* obj);
			BFRT_EXPORT static void Dbg_ObjectCreated(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData);
			BFRT_EXPORT static void Dbg_ObjectCreatedEx(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData);
			BFRT_EXPORT static void Dbg_ObjectAllocated(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData);
			BFRT_EXPORT static void Dbg_ObjectAllocatedEx(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData);
			BFRT_EXPORT static Object* Dbg_ObjectAlloc(bf::System::Reflection::TypeInstance* typeInst, intptr size);			
			BFRT_EXPORT static Object* Dbg_ObjectAlloc(bf::System::ClassVData* classVData, intptr size, intptr align, intptr maxStackTraceDept);						
			BFRT_EXPORT static void Dbg_MarkObjectDeleted(bf::System::Object* obj);
			BFRT_EXPORT static void Dbg_ObjectStackInit(bf::System::Object* result, bf::System::ClassVData* classVData);			
			BFRT_EXPORT static void Dbg_ObjectPreDelete(bf::System::Object* obj);
			BFRT_EXPORT static void Dbg_ObjectPreCustomDelete(bf::System::Object* obj);
			
			BFRT_EXPORT static void* Dbg_RawMarkedAlloc(intptr size, void* markFunc);
			BFRT_EXPORT static void* Dbg_RawMarkedArrayAlloc(intptr elemCount, intptr elemSize, void* markFunc);
			BFRT_EXPORT static void* Dbg_RawAlloc(intptr size);
			BFRT_EXPORT static void* Dbg_RawObjectAlloc(intptr size);
			
			BFRT_EXPORT static void* Dbg_RawAlloc(intptr size, DbgRawAllocData* rawAllocData);
			BFRT_EXPORT static void Dbg_RawFree(void* ptr);
		};

		namespace IO
		{
			class File
			{
			private:
				BFRT_EXPORT static bool Exists(char* fileName);
			};

			class Directory
			{
			private:
				BFRT_EXPORT static bool Exists(char* fileName);
			};
		}

		namespace Diagnostics
		{
			namespace Contracts
			{
				class Contract
				{
				public:
					enum ContractFailureKind : uint8
					{
						ContractFailureKind_Precondition,
						//[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]  
						ContractFailureKind_Postcondition,
						//[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]
						ContractFailureKind_PostconditionOnException,
						ContractFailureKind_Invariant,
						ContractFailureKind_Assert,
						ContractFailureKind_Assume,
					};

				private:
					BFRT_EXPORT static void ReportFailure(ContractFailureKind failureKind, char* userMessage, int userMessageLen, char* conditionText, int conditionTextLen);
				};
			}

			class Debug
			{
			private:
				BFRT_EXPORT static void Write(char* str, intptr strLen);			
			};
		}

		namespace FFI
		{
			enum FFIABI : int32;
			enum FFIResult : int32;

			struct FFIType;

			struct FFILIB
			{
				struct FFICIF;

				BFRT_EXPORT static void* ClosureAlloc(intptr size, void** outFunc);
				BFRT_EXPORT static FFIResult PrepCif(FFICIF* cif, FFIABI abi, int32 nargs, FFIType* rtype, FFIType** argTypes);
				BFRT_EXPORT static void Call(FFICIF* cif, void* funcPtr, void* rvalue, void** args);
			};
		}
	}
}

//#define BF_TRACK_SIZES 1

#if BF_TRACK_SIZES
static int sAllocSizes[1024 * 1024];
static int sHighestId = 0;
#endif

using namespace bf::System;

/*static void* MallocHook(size_t size, const void *caller)
{
printf("MallocHook\n");
return NULL;
}*/


/*static int __cdecl HeapHook(int a, size_t b, void* c, void** d)
{
printf("Heap Hook\n");
return 0;
}*/

//////////////////////////////////////////////////////////////////////////

Beefy::StringT<0> gDbgErrorString;

extern DbgRawAllocData sEmptyAllocData;
extern DbgRawAllocData sObjectAllocData;

#define SETUP_ERROR(str, skip) gDbgErrorString = str; BFRTCALLBACKS.DebugMessageData_SetupError(str, skip)

#ifdef BF_PLATFORM_WINDOWS
#define BF_CAPTURE_STACK(skipCount, outFrames, wantCount) (int)RtlCaptureStackBackTrace(skipCount, wantCount, (void**)outFrames, NULL)
#else
#define BF_CAPTURE_STACK(skipCount, outFrames, wantCount) BfpStack_CaptureBackTrace(skipCount, outFrames, wantCount)
#endif


static void GetCrashInfo()
{
	if (!gDbgErrorString.IsEmpty())
	{
		Beefy::String debugStr;
		debugStr += "Beef Error: ";
		debugStr += gDbgErrorString;
		BfpSystem_AddCrashInfo(debugStr.c_str());
	}
}

void bf::System::Runtime::Dbg_Init(int version, int flags, BfRtCallbacks* callbacks)
{		
#ifndef BFRTMERGED

	if (version != BFRT_VERSION)
	{
		BfpSystem_FatalError(StrFormat("BeefDbg build version '%d' does not match requested version '%d'", BFRT_VERSION, version).c_str(), "BEEF FATAL ERROR");
	}

	if (gBfRtDbgCallbacks.Alloc != NULL)
	{
		BfpSystem_FatalError(StrFormat("BeefDbg already initialized. Multiple executable modules in the same process cannot dynamically link to the Beef debug runtime.").c_str(), "BEEF FATAL ERROR");
	}

	gBfRtDbgCallbacks = *callbacks;
	gBfRtDbgFlags = (BfRtFlags)flags;
#ifdef BF_GC_SUPPORTED	
	gGCDbgData.mDbgFlags = gBfRtDbgFlags;
#endif

#endif
}

void* bf::System::Runtime::Dbg_GetCrashInfoFunc()
{
	return *(void**)&GetCrashInfo;
}

//////////////////////////////////////////////////////////////////////////


void Internal::Dbg_MarkObjectDeleted(bf::System::Object* object)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
	if ((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0)
		object->mObjectFlags = (BfObjectFlags)((object->mObjectFlags & ~BfObjectFlag_StackAlloc) | BfObjectFlag_Deleted);
#ifdef BF_GC_SUPPORTED
	gBFGC.ObjectDeleteRequested(object);
#endif
}

int GetStackTrace(void **result, int max_depth, int skip_count);

void BfLog(const char* fmt ...);

static const int cMaxStackTraceCount = 1024;
struct PendingAllocState
{
	bool mHasData;
	void* mStackTrace[cMaxStackTraceCount];
	int mStackTraceCount;
	int mMetadataBytes;
	bool mIsLargeAlloc;

	bool IsSmall(intptr curAllocBytes)
	{
		if ((mStackTraceCount > 255) || (mMetadataBytes > 255))
			return false;

		const intptr maxSmallObjectSize = ((intptr)1 << ((sizeof(intptr) - 2) * 8)) - 1;
		if (curAllocBytes <= maxSmallObjectSize)
			return true;

		intptr objBytes = curAllocBytes - mStackTraceCount * sizeof(intptr) - mMetadataBytes;
		return (objBytes < maxSmallObjectSize);
	}
};

static __thread PendingAllocState gPendingAllocState = { 0 };

void Internal::Dbg_ReserveMetadataBytes(intptr metadataBytes, intptr& curAllocBytes)
{
	bool isSmall = gPendingAllocState.IsSmall(curAllocBytes);
	gPendingAllocState.mMetadataBytes += (int)metadataBytes;
	curAllocBytes += metadataBytes;
	if ((isSmall) && (gPendingAllocState.mMetadataBytes > 255))
	{
		// We just went to 'small' to not small
		curAllocBytes += sizeof(intptr);
	}
}

void* Internal::Dbg_GetMetadata(bf::System::Object* obj)
{
	return NULL;
}

intptr Internal::Dbg_PrepareStackTrace(intptr baseAllocSize, intptr maxStackTraceDepth)
{
	int allocSize = 0;
	if (maxStackTraceDepth > 1)
	{
		int capturedTraceCount = BF_CAPTURE_STACK(1, (intptr*)gPendingAllocState.mStackTrace, min((int)maxStackTraceDepth, 1024));
		gPendingAllocState.mStackTraceCount = capturedTraceCount;
		const intptr maxSmallObjectSize = ((intptr)1 << ((sizeof(intptr) - 2) * 8)) - 1;
		if ((capturedTraceCount > 255) || (baseAllocSize >= maxSmallObjectSize))
		{
			gPendingAllocState.mIsLargeAlloc = true;			
			allocSize += (1 + capturedTraceCount) * sizeof(intptr);
		}
		else
		{
			gPendingAllocState.mIsLargeAlloc = false;
			allocSize += capturedTraceCount * sizeof(intptr);
		}
	}
	return allocSize;
}

bf::System::Object* Internal::Dbg_ObjectAlloc(bf::System::Reflection::TypeInstance* typeInst, intptr size)
{	
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
	Object* result;	
	int allocSize = BF_ALIGN(size, typeInst->mInstAlign);
	uint8* allocBytes = (uint8*)BfObjectAllocate(allocSize, typeInst->_GetType());
// 	int dataOffset = (int)(sizeof(intptr) * 2);
// 	memset(allocBytes + dataOffset, 0, size - dataOffset);
	result = (bf::System::Object*)allocBytes;
	auto classVData = typeInst->mTypeClassVData;	

#ifndef BFRT_NODBGFLAGS
	intptr dbgAllocInfo = (intptr)BF_RETURN_ADDRESS;
	result->mClassVData = (intptr)classVData | (intptr)BfObjectFlag_Allocated /*| BFGC::sAllocFlags*/;
	BF_FULL_MEMORY_FENCE(); // Since we depend on mDbAllocInfo to determine if we are allocated, we need to set this last after we're set up
	result->mDbgAllocInfo = dbgAllocInfo;
	BF_FULL_MEMORY_FENCE();
	result->mClassVData = (result->mClassVData & ~BF_OBJECTFLAG_MARK_ID_MASK) | BFGC::sAllocFlags;
#endif
	return result;
}

//#define DBG_OBJECTEND

bf::System::Object* Internal::Dbg_ObjectAlloc(bf::System::ClassVData* classVData, intptr size, intptr align, intptr maxStackTraceDepth)
{
	void* stackTrace[1024];
	int capturedTraceCount = 0;
	intptr allocSize = size;
	bool largeAllocInfo = false;

	if ((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0)
	{
		if (maxStackTraceDepth > 1)
		{
			capturedTraceCount = BF_CAPTURE_STACK(1, (intptr*)stackTrace, min((int)maxStackTraceDepth, 1024));
			const intptr maxSmallObjectSize = ((intptr)1 << ((sizeof(intptr) - 2) * 8)) - 1;
			if ((capturedTraceCount > 255) || (size >= maxSmallObjectSize))
			{
				largeAllocInfo = true;
				allocSize += (1 + capturedTraceCount) * sizeof(intptr);
			}
			else
				allocSize += capturedTraceCount * sizeof(intptr);
		}
	}

#ifdef DBG_OBJECTEND
	allocSize += 4;
#endif

	bf::System::Object* result;
	if ((BFRTFLAGS & BfRtFlags_LeakCheck) != 0)
	{
		allocSize = BF_ALIGN(allocSize, align);
		uint8* allocBytes = (uint8*)BfObjectAllocate(allocSize, classVData->mType);
// 		int dataOffset = (int)(sizeof(intptr) * 2);
// 		memset(allocBytes + dataOffset, 0, size - dataOffset);
		result = (bf::System::Object*)(allocBytes);
	}
	else
	{
#if BF_USE_STOMP_ALLOC
		result = (bf::System::Object*)StompAlloc(allocSize);
#elif BF_TRACK_SIZES
		sHighestId = BF_MAX(sHighestId, classVData->mType->mTypeId);
		uint8* allocPtr = (uint8*)malloc(size + 16);
		*((int*)allocPtr) = size;
		sAllocSizes[classVData->mType->mTypeId] += size;
		result = (bf::System::Object*)(allocPtr + 16);
#else
		if ((BFRTFLAGS & BfRtFlags_DebugAlloc) != 0)
		{			
			uint8* allocBytes = (uint8*)BfRawAllocate(allocSize, &sObjectAllocData, NULL, 0);
			result = (bf::System::Object*)allocBytes;
		}
		else
		{
			uint8* allocBytes = (uint8*)BFRTCALLBACKS.Alloc(allocSize);
			result = (bf::System::Object*)allocBytes;
		}		
#endif
	}

#ifndef BFRT_NODBGFLAGS
	if ((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0)
	{
		// The order is very important here-
		//  Once we set mDbgAllocInfo, the memory will be recognized by the GC as being a valid object.
		//  There's a race condition with the alloc flags, however-- if the GC pauses our thread after we write
		//  the mClassVData but before mDbgAllocInfo is set, then the object won't be marked with the new mark id
		//  and it will appear as a leak during the Sweep. Thus, we leave the sAllocFlags off until AFTER
		//  mDbgAllocInfo is set, so that the object will be ignored during sweeping unless we get all the way
		//  through.

		intptr dbgAllocInfo;
		auto classVDataVal = (intptr)classVData | (intptr)BfObjectFlag_Allocated;
		result->mClassVData = classVDataVal;
		if (maxStackTraceDepth <= 1)
			dbgAllocInfo = (intptr)BF_RETURN_ADDRESS;
		else
		{
			if (largeAllocInfo)
			{
				result->mClassVData |= (intptr)BfObjectFlag_AllocInfo;
				dbgAllocInfo = size;
				*(intptr*)((uint8*)result + size) = capturedTraceCount;
				memcpy((uint8*)result + size + sizeof(intptr), stackTrace, capturedTraceCount * sizeof(intptr));
			}
			else
			{
				result->mClassVData |= (intptr)BfObjectFlag_AllocInfo_Short;
				dbgAllocInfo = (size << 16) | capturedTraceCount;
				memcpy((uint8*)result + size, stackTrace, capturedTraceCount * sizeof(intptr));
			}
		}		
		BF_FULL_MEMORY_FENCE(); // Since we depend on mDbAllocInfo to determine if we are allocated, we need to set this last after we're set up
		result->mDbgAllocInfo = dbgAllocInfo;
		BF_FULL_MEMORY_FENCE();
		// If the GC has already set the correct mark id then we don't need want to overwrite it - we could have an old value
		BfpSystem_InterlockedCompareExchangePtr((uintptr*)&result->mClassVData, (uintptr)classVDataVal, classVDataVal | BFGC::sAllocFlags);		
		//result->mClassVData = (result->mClassVData & ~BF_OBJECTFLAG_MARK_ID_MASK) | BFGC::sAllocFlags;
	}
	else
#endif
		result->mClassVData = (intptr)classVData;

	//OutputDebugStrF("Object %@ ClassVData %@\n", result, classVData);

#ifdef DBG_OBJECTEND
	*(uint32*)((uint8*)result + size) = 0xBFBFBFBF;
#endif

	return result;
}

void Internal::Dbg_ObjectStackInit(bf::System::Object* result, bf::System::ClassVData* classVData)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);

	result->mClassVData = (intptr)classVData | (intptr)BfObjectFlag_StackAlloc;
#ifndef BFRT_NODBGFLAGS
	result->mDbgAllocInfo = (intptr)BF_RETURN_ADDRESS;	
#endif
}

static void SetupDbgAllocInfo(bf::System::Object* result, intptr origSize)
{
#ifndef BFRT_NODBGFLAGS
	if (gPendingAllocState.mStackTraceCount == 0)
	{
		result->mDbgAllocInfo = 1; // Must have a value
		return;
	}
	if (gPendingAllocState.mStackTraceCount == 1)
	{
		result->mDbgAllocInfo = (intptr)gPendingAllocState.mStackTrace[0];
		return;
	}
	if (gPendingAllocState.mIsLargeAlloc)
	{
		result->mClassVData |= (intptr)BfObjectFlag_AllocInfo;
		result->mDbgAllocInfo = origSize;
		*(intptr*)((uint8*)result + origSize) = gPendingAllocState.mStackTraceCount;
		memcpy((uint8*)result + origSize + sizeof(intptr), gPendingAllocState.mStackTrace, gPendingAllocState.mStackTraceCount * sizeof(intptr));
	}
	else
	{
		result->mClassVData |= (intptr)BfObjectFlag_AllocInfo_Short;
		result->mDbgAllocInfo = (origSize << 16) | gPendingAllocState.mStackTraceCount;
		memcpy((uint8*)result + origSize, gPendingAllocState.mStackTrace, gPendingAllocState.mStackTraceCount * sizeof(intptr));
	}
#endif
}

void Internal::Dbg_ObjectCreated(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
#ifndef BFRT_NODBGFLAGS	
	BF_ASSERT_REL((result->mClassVData & ~(BfObjectFlag_Allocated | BfObjectFlag_Mark3)) == (intptr)classVData);
	result->mDbgAllocInfo = (intptr)BF_RETURN_ADDRESS;
#endif
}

void Internal::Dbg_ObjectCreatedEx(bf::System::Object* result, intptr origSize, bf::System::ClassVData* classVData)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
#ifndef BFRT_NODBGFLAGS	
	BF_ASSERT_REL((result->mClassVData & ~(BfObjectFlag_Allocated | BfObjectFlag_Mark3)) == (intptr)classVData);
	SetupDbgAllocInfo(result, origSize);
#endif
}

void Internal::Dbg_ObjectAllocated(bf::System::Object* result, intptr size, bf::System::ClassVData* classVData)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
	result->mClassVData = (intptr)classVData;
#ifndef BFRT_NODBGFLAGS	
	result->mDbgAllocInfo = (intptr)BF_RETURN_ADDRESS;	
#endif
}

void Internal::Dbg_ObjectAllocatedEx(bf::System::Object* result, intptr origSize, bf::System::ClassVData* classVData)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);
	result->mClassVData = (intptr)classVData;
	SetupDbgAllocInfo(result, origSize);
}

void Internal::Dbg_ObjectPreDelete(bf::System::Object* object)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);

#ifndef BFRT_NODBGFLAGS
	const char* errorPtr = NULL;

	if ((object->mObjectFlags & BfObjectFlag_StackAlloc) != 0)
	{
		if ((object->mObjectFlags & BfObjectFlag_Allocated) == 0)
			errorPtr = "Attempting to delete stack-allocated object";
		else
			errorPtr = "Deleting an object that was detected as leaked (internal error)";
	}
	else if ((object->mObjectFlags & BfObjectFlag_AppendAlloc) != 0)
		errorPtr = "Attempting to delete append-allocated object, use 'delete append' statement instead of 'delete'";
	else if ((object->mObjectFlags & BfObjectFlag_Allocated) == 0)
	{
		errorPtr = "Attempting to delete custom-allocated object without specifying allocator";
#if _WIN32		
		MEMORY_BASIC_INFORMATION stackInfo = { 0 };				
		VirtualQuery(object, &stackInfo, sizeof(MEMORY_BASIC_INFORMATION));
		if ((stackInfo.Protect & PAGE_READONLY) != 0)
			errorPtr = "Attempting to delete read-only object";
#endif
	}
	else if ((object->mObjectFlags & BfObjectFlag_Deleted) != 0)
		errorPtr = "Attempting second delete on object";	

	if (errorPtr != NULL)
	{
		Beefy::String errorStr = errorPtr;

		Beefy::String typeName = object->GetTypeName();
		errorStr += "\x1";
		errorStr += StrFormat("LEAK\t0x%@\n", object);
		errorStr += StrFormat("   (%s)0x%@\n", typeName.c_str(), object);
		SETUP_ERROR(errorStr.c_str(), 2);
		BF_DEBUG_BREAK();
		BFRTCALLBACKS.DebugMessageData_Fatal();
		return;
	}
#endif
}

void Internal::Dbg_ObjectPreCustomDelete(bf::System::Object* object)
{
	BF_ASSERT((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0);

	const char* errorPtr = NULL;

	if ((object->mObjectFlags & BfObjectFlag_StackAlloc) != 0)
		errorPtr = "Attempting to delete stack-allocated object";
	if ((object->mObjectFlags & BfObjectFlag_Deleted) != 0)
		errorPtr = "Attempting second delete on object";

	if (errorPtr != NULL)
	{
		Beefy::String errorStr = errorPtr;

		Beefy::String typeName = object->GetTypeName();
		errorStr += "\x1";
		errorStr += StrFormat("LEAK\t0x%@\n", object);
		errorStr += StrFormat("   (%s)0x%@\n", typeName.c_str(), object);
		SETUP_ERROR(errorStr.c_str(), 2);
		BF_DEBUG_BREAK();
		BFRTCALLBACKS.DebugMessageData_Fatal();
		return;
	}
}

void* Internal::Dbg_RawAlloc(intptr size, DbgRawAllocData* rawAllocData)
{	
	void* stackTrace[1024];
	int capturedTraceCount = 0;
#ifndef BFRT_NODBGFLAGS	
	if (rawAllocData->mMaxStackTrace == 1)
	{
		stackTrace[0] = BF_RETURN_ADDRESS;
		capturedTraceCount = 1;
	}
	else if (rawAllocData->mMaxStackTrace > 1)
	{
		capturedTraceCount = BF_CAPTURE_STACK(1, (intptr*)stackTrace, min(rawAllocData->mMaxStackTrace, 1024));
	}	
#endif
	return BfRawAllocate(size, rawAllocData, stackTrace, capturedTraceCount);
}

void* Internal::Dbg_RawMarkedAlloc(intptr size, void* markFunc)
{	
	return BfRawAllocate(size, &sEmptyAllocData, NULL, 0);
}

void* Internal::Dbg_RawMarkedArrayAlloc(intptr elemCount, intptr elemStride, void* markFunc)
{	
	return BfRawAllocate(elemCount * elemStride, &sEmptyAllocData, NULL, 0);
}

void* Internal::Dbg_RawAlloc(intptr size)
{
	return BfRawAllocate(size, &sEmptyAllocData, NULL, 0);
}

void* Internal::Dbg_RawObjectAlloc(intptr size)
{
	return BfRawAllocate(size, &sObjectAllocData, NULL, 0);
}

void Internal::Dbg_RawFree(void* ptr)
{
	BfRawFree(ptr);
}
