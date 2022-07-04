#pragma once

#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/Dictionary.h"

namespace bf
{
	namespace System
	{
		namespace Threading
		{
			class Thread;
		}
	}
}

bf::System::Threading::Thread* BfGetCurrentThread();

class BfTLSEntry
{
public:
    BfpTLS* mThreadKey;
};

class BfTLSDatumRoot
{
public:
	virtual void Finalize() = 0;
};

class BfTLSManager
{
public:
    static BfpTLS* sInternalThreadKey;
	Beefy::CritSect mCritSect;

    BfpTLS** mAllocatedKeys;
	BfTLSDatumRoot** mAssociatedTLSDatums;
	int mAllocSize;
	int mAllocIdx;
    Beefy::Array<int> mFreeIndices;

	BfTLSManager()
	{
        sInternalThreadKey = BfpTLS_Create(NULL);
		mAssociatedTLSDatums = NULL;
		mAllocSize = 0;
		mAllocIdx = 1;
		mAllocatedKeys = NULL;
	}

	uint32 Alloc();
	void RegisterTlsDatum(uint32 key, BfTLSDatumRoot* tlsDatum);
	void Free(uint32 idx);

    BfpTLS* Lookup(uint32 idx)
	{
		return mAllocatedKeys[idx];
	}
};

extern BfTLSManager gBfTLSManager;

//#define DECLARE_THREAD_LOCAL_STORAGE(memberType) __thread memberType
#define DECLARE_THREAD_LOCAL_STORAGE(memberType) BfTLSStatic<memberType>

class BfTLSStaticBase
{
};

template <class T>
class BFMemClear
{
public:
	static void Clear(T& valPtr)
	{
		memset(&valPtr, 0, sizeof(T));
	}
};

template <class T>
class BFNoMemClear
{
public:
	static void Clear(T& valPtr)
	{
	}
};

/*template <class T, class TClear = BFMemClear<T> >
class BfTLSStatic : public BfTLSStaticBase
{
public:
	typedef BfTLSStatic<T, TClear> TLSType;
	typedef T TLSInnerType;
	typedef T InnerType;
    typedef Beefy::Dictionary<bf::System::Threading::Thread*, T*> ValueMapType;
	typedef void BFBoxedType;
	mutable Beefy::CritSect mCritSect;
	mutable uint32 mKey;
	mutable ValueMapType mValueMap;

public:
	BfTLSStatic()
	{
        mKey = BfpTLS_Create();
	}

	T* getSafe() const
	{
        T* valPtr = (T*) BfpTLS_GetValue(mKey);
		if (valPtr == NULL)
		{
			Beefy::AutoCrit autoCrit(mCritSect);
			valPtr = new T();
			TClear::Clear(*valPtr);
			mValueMap[::BfGetCurrentThread()] = valPtr;
            BfpTLS_SetValue(mKey, valPtr);
		}
		return valPtr;
	}

	const BfTLSStatic<T>& operator=(const T& rhs) const
	{
		*getSafe() = rhs;
		return *this;
	}

	T& operator->() const
	{
		return *getSafe();
	}

	T& operator*() const
	{
		return *getSafe();
	}

	operator T&() const
	{
		return *getSafe();
	}

	ValueMapType* GetValueMap()
	{
		return &mValueMap;
	}
};
*/