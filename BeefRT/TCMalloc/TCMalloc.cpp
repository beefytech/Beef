#define TCMALLOC_NO_MALLOCGUARD
#define TCMALLOC_NAMESPACE tcmalloc
#include "gperftools/src/tcmalloc.cc"

#include "gperftools/src/central_freelist.cc"
#include "gperftools/src/internal_logging.cc"
//#include "malloc_extension.cc"
#include "gperftools/src/page_heap.cc"
#include "gperftools/src/sampler.cc"
#include "gperftools/src/span.cc"
#include "gperftools/src/stack_trace_table.cc"
#include "gperftools/src/static_vars.cc"
#include "gperftools/src/tc_common.cc"
#include "gperftools/src/thread_cache.cc"

namespace tcmalloc
{
	extern "C" int RunningOnValgrind(void)
	{
		return 0;
	}
}

void TCMalloc_RecordAlloc(void* ptr, int size)
{
	
}