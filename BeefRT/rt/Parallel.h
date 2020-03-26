#pragma once

#include <ppl.h>

#include "BeefySysLib/Common.h"
#include "BfObjects.h"

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

				BFRT_EXPORT static void InvokeInternal(void* funcs, int count);

				typedef void (*PForFuncLong)(long long idx);
				typedef void (*PForFuncInt)(int idx);

				BFRT_EXPORT static void ForInternal(long long from, long long to, void* func);
				BFRT_EXPORT static void ForInternal(int from, int to, void* func);

				typedef void (*PForeachFunc)(void* item);

				BFRT_EXPORT static void ForeachInternal(void* arrOfPointers, int count, void* func);
			};
		}
	}
}
