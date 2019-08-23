#pragma once

#include "../rt/Thread.h"
#include "gc.h"

namespace tcmalloc_obj
{
	class ThreadCache;
}

class BfDbgInternalThread : public BfInternalThread
{
public:		
	BfDbgInternalThread();
	virtual ~BfDbgInternalThread();	
	virtual void ThreadStarted() override;
	virtual void ThreadStopped() override;
}; 
