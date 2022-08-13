#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/String.h"

#define BFRT_VERSION 10

#ifdef BFRT_DYNAMIC
#define BFRT_EXPORT __declspec(dllexport)
#else
#define BFRT_EXPORT
#endif

class BfType;
class BfInternalThread;

#define BF_DECLARE_CLASS(ClassName, BaseClassName) static System::ClassVData sBfClassVData;

enum BfObjectFlags : uint8
{
	BfObjectFlag_None			= 0,
	BfObjectFlag_Mark1			= 0x01,
	BfObjectFlag_Mark2			= 0x02,
	BfObjectFlag_Mark3			= 0x03,
	BfObjectFlag_Allocated		= 0x04,
	BfObjectFlag_StackAlloc		= 0x08,
	BfObjectFlag_AppendAlloc	= 0x10,
	BfObjectFlag_AllocInfo		= 0x20,
	BfObjectFlag_AllocInfo_Short= 0x40,	
	BfObjectFlag_Deleted		= 0x80
};

enum BfRtFlags
{
	BfRtFlags_ObjectHasDebugFlags = 1,
	BfRtFlags_LeakCheck = 2,
	BfRtFlags_SilentCrash = 4,
	BfRtFlags_DebugAlloc = 8,
	BfRtFlags_NoThreadExitWait = 0x10,	
};

namespace bf
{
	namespace System
	{
		struct ClassVData;
		class Type;
		class String;
		class Object;

		namespace Threading
		{
			class Thread;
		}
	}
}

namespace bf
{
	namespace System
	{
		struct DbgRawAllocData
		{
			Type* mType;
			void* mMarkFunc;
			int32 mMaxStackTrace; // Only 0, 1, >1 matters
		};

		class Runtime
		{
		public:
			enum RtCrashReportKind : int32
			{
				RtCrashReportKind_Default,
				RtCrashReportKind_GUI,
				RtCrashReportKind_Console,
				RtCrashReportKind_PrintOnly,
				RtCrashReportKind_None
			};

		public:
			struct BfRtCallbacks
			{
				void*(*Alloc)(intptr size);
				void(*Free)(void* ptr);
				void(*Object_Delete)(bf::System::Object* obj);
				void* mUnused0;
				bf::System::Type* (*Object_GetType)(bf::System::Object* obj);
				void(*Object_GCMarkMembers)(bf::System::Object* obj);
				bf::System::Object* (*Object_DynamicCastToTypeId)(bf::System::Object* obj, int typeId);
				void(*Type_GetFullName)(System::Type* type, bf::System::String* str);
				bf::System::String* (*String_Alloc)();
				const char* (*String_ToCStr)(bf::System::String* str);
				bf::System::Threading::Thread* (*Thread_Alloc)();
				bf::System::Threading::Thread* (*Thread_GetMainThread)();
				void(*Thread_ThreadProc)(bf::System::Threading::Thread* thread);
				BfInternalThread* (*Thread_GetInternalThread)(bf::System::Threading::Thread* thread);
				void(*Thread_SetInternalThread)(bf::System::Threading::Thread* thread, BfInternalThread* internalThread);
				bool(*Thread_IsAutoDelete)(bf::System::Threading::Thread* thread);
				void(*Thread_AutoDelete)(bf::System::Threading::Thread* thread);
				int32(*Thread_GetMaxStackSize)(bf::System::Threading::Thread* thread);				
				void(*Thread_Exiting)();
				void(*GC_MarkAllStaticMembers)();
				bool(*GC_CallRootCallbacks)();
				void(*GC_Shutdown)();
				void(*SetErrorString)(const char* str);
				void(*DebugMessageData_SetupError)(const char* str, int32 stackWindbackCount);
				void(*DebugMessageData_SetupProfilerCmd)(const char* str);				
				void(*DebugMessageData_Fatal)();
				void(*DebugMessageData_Clear)();
				int(*CheckErrorHandler)(const char* kind, const char* arg1, const char* arg2, intptr arg3);				
			};

		public:
			BFRT_EXPORT static void SetCrashReportKind(RtCrashReportKind crashReportKind);			

		private:
			BFRT_EXPORT static void Init(int version, int flags, BfRtCallbacks* callbacks);			
			BFRT_EXPORT static void AddCrashInfoFunc(void* func);
			BFRT_EXPORT static void SetErrorString(char* errorStr);
			BFRT_EXPORT static void Dbg_Init(int version, int flags, BfRtCallbacks* callbacks);
			BFRT_EXPORT static void* Dbg_GetCrashInfoFunc();
		};
	}
}

#ifdef BFRTDBG
#define BFRTCALLBACKS gBfRtDbgCallbacks
#define BFRTFLAGS gBfRtDbgFlags
#else
#define BFRTCALLBACKS gBfRtCallbacks
#define BFRTFLAGS gBfRtFlags
#endif

extern bf::System::Runtime::BfRtCallbacks BFRTCALLBACKS;
extern BfRtFlags BFRTFLAGS;

namespace bf
{
	namespace System
	{
		struct ClassVData
		{
			Type* mType;
			void* mInterfaceSlots[16];
		};

		class Object
		{
		public:
			union
			{
				intptr mClassVData;				
				struct
				{
					BfObjectFlags mObjectFlags;
					uint8 mClassVDataBytes[sizeof(intptr) - 1];
				};
			};

#ifndef BFRT_NODBGFLAGS
			union
			{
				void* mAllocCheckPtr;
				intptr mDbgAllocInfo; // Meaning depends on object flags- could be PC at allocation
			};
#endif

			Type* _GetType()
			{
				return BFRTCALLBACKS.Object_GetType(this);
			}

			Type* GetTypeSafe()
			{
				return NULL;
			}

			Beefy::String GetTypeName();
		};

		class Exception : public Object
		{

		};

		typedef int32 TypeId;

		class Type : public Object
		{
		public:
			int32 mSize;
			TypeId mTypeId;
			TypeId mBoxedId;
			uint16 mTypeFlags;
			int32 mMemberDataOffset;			
			uint8 mTypeCode;
			uint8 mAlign;

			Beefy::String GetFullName();
		};

		class Type_NOFLAGS
		{
		public:
			intptr mClassVData;
			int32 mSize;
			TypeId mTypeId;
			TypeId mBoxedId;
			uint16 mTypeFlags;
			int32 mMemberDataOffset;
			uint8 mTypeCode;
			uint8 mAlign;
		};

		namespace Reflection
		{
			typedef int16 TypeId;
			typedef uint8 FieldFlags;

			class TypeInstance : public Type
			{
			public:
				ClassVData* mTypeClassVData;
				String* mName;
				String* mNamespace;
				int32 mInstSize;
				int32 mInstAlign;				
			};
		}

		struct IntPtr
		{
			intptr mValue;
		};

		struct Delegate
		{
		};

		class String : public Object
		{
		public:
			BF_DECLARE_CLASS(String, Object);

		private:
			BFRT_EXPORT static intptr UTF8GetAllocSize(char* str, intptr strlen, int32 options);
			BFRT_EXPORT static intptr UTF8Map(char* str, intptr strlen, char* outStr, intptr outSize, int32 options);

		public:
			int mLength;
			uint mAllocSizeAndFlags;
			char* mPtr;

			const char* CStr()
			{
				return BFRTCALLBACKS.String_ToCStr(this);
			}
		};
	}
}

/*struct BfDebugMessageData
{
	enum MessageType
	{
		MessageType_None = 0,
		MessageType_Error = 1,
		MessageType_ProfilerCmd = 2
	};

	int mMessageType; // 0 = none, 1 = error
	int mStackWindbackCount;
	int mStrParamLen;
	const char* mStrParam;
	void* mPCOverride;

	////
	String mStrBuffer;

	void SetupError(const Beefy::String& str, int stackWindbackCount = 0)
	{
		mMessageType = MessageType_Error;
		mStackWindbackCount = stackWindbackCount;
		mStrBuffer = str;
		mStrParam = mStrBuffer.c_str();
		mStrParamLen = (int) mStrBuffer.length();
		mPCOverride = NULL;
	}

	void SetupProfilerCmd(const String& str)
	{
		mMessageType = MessageType_ProfilerCmd;
		mStackWindbackCount = 0;
		mStrBuffer = str;
		mStrParam = mStrBuffer.c_str();
		mStrParamLen = (int) mStrBuffer.length();
		mPCOverride = NULL;
	}

	void Clear()
	{
		mMessageType = 0;
		mStrBuffer.clear();
		mStrParamLen = 0;
	}
};*/

namespace Beefy
{
	String PointerToString(void* ptr);
}

//extern "C" BfDebugMessageData gBfDebugMessageData;

//extern "C" void* BfObjectNew(System::ClassVData* classVData, intptr size, uint8 flags);
//extern "C" void* BfObjectStackInit(System::Object* result, System::ClassVData* classVData);
