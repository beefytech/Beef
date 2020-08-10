#pragma once

#include "../Common.h"

NS_BF_BEGIN

class WorkThread
{
public:
	BfpThread* mThread;
	int mStackSize;

public:
	WorkThread();
	virtual ~WorkThread();

	virtual void Start();
	virtual void Stop();
	virtual void WaitForFinish();
	virtual bool WaitForFinish(int waitMS);

	virtual void Run() = 0;
};

class WorkThreadFunc : public WorkThread
{
public:
	void (*mFunc)(void*);
	void* mParam;		

public:
	// Note: this startProc signature does not match BfpThreadStartProc -- here we abstract out the calling convention to be default 
	//  on all platforms (cdecl)
	void Start(void (*func)(void*), void* param);
	virtual void Run() override;
};

NS_BF_END
