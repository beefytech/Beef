#pragma once

#include <ppl.h>

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
			class Parallel : public Object
			{
			public:
				BF_DECLARE_CLASS(Parallel, Object);

				typedef void (*PInvokeFunc)();

				BFRT_EXPORT static void InvokeInternal(void* funcs, BF_INT_T count);

				typedef void (*PForFunc)(long long idx);

				BFRT_EXPORT static void ForInternal(long long from, long long to, void* func);

				typedef void (*PForeachFunc)(void* item);

				BFRT_EXPORT static void ForeachInternal(void* arrOfPointers, BF_INT_T count, int elementSize, void* func);
			};
		}
	}
}
