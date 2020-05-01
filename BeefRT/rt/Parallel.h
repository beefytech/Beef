#pragma once

#include <ppl.h>
#include <atomic>
#include <algorithm>
#include <execution>

#include "BeefySysLib/Common.h"
#include "BfObjects.h"

#ifdef BF64
#define BF_INT(a) a##ll
using BF_INT_T = long long;
#else
#define BF_INT(a) a
using BF_INT_T = int;
#endif

namespace bf
{
	namespace System
	{
		namespace Threading
		{
			struct ParallelMetadata
			{
				std::atomic_bool running = true;
				std::atomic_int  taskCount = 0;
			};

			class ParallelState :public Object
			{
			private:
				BFRT_EXPORT static void InitializeMeta(void* meta);

				BFRT_EXPORT static void BreakInternal(void* meta);

				BFRT_EXPORT static void StopInternal(void* meta);

				BFRT_EXPORT static bool StoppedInternal(void* meta);

				BFRT_EXPORT static bool ShouldStopInternal(void* meta);

			public:
				BF_DECLARE_CLASS(ParallelState, Object);
			};

			class Parallel : public Object
			{
			private:

				typedef void (*PInvokeFunc)(void* wr, BF_INT_T idx);
				typedef void (*PForFunc)(void* wr, BF_INT_T idx);

				BFRT_EXPORT static void InvokeInternal(void* wrapper, void* func, BF_INT_T count);
				BFRT_EXPORT static void ForInternal(long long from, long long to, void* wrapper, void* func);
				BFRT_EXPORT static void ForInternal(long long from, long long to, void* meta, void* wrapper, void* func);

			public:
				BF_DECLARE_CLASS(Parallel, Object);
			};
		}
	}
}
