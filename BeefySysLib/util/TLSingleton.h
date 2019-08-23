#pragma once

#include "../Common.h"
#include "CritSect.h"

NS_BF_BEGIN;

#ifdef BF_PLATFORM_WINDOWS

template <typename T>
class TLSingleton
{
public:	
	uint32 mFlsValue;

public:	
	TLSingleton()
	{
		this->mFlsValue = FlsAlloc(&this->FlsFreeFunc);
	}

	~TLSingleton()
	{		
		FlsFree(this->mFlsValue);
	}

	static void NTAPI FlsFreeFunc(void* ptr)
	{
		delete (T*)ptr;
	}

	T* Get()
	{
		T* val = (T*)FlsGetValue(this->mFlsValue);
		if (val == NULL)
		{
			val = new T();
			FlsSetValue(this->mFlsValue, val);
		}
		return val;
	}
};

class TLSDtor
{
public:
	typedef void (NTAPI *CallbackFunction)(void* lpFlsData);

public:
	DWORD mFlsValue;
	
	TLSDtor(CallbackFunction callbackFunction)
	{
		mFlsValue = FlsAlloc(callbackFunction);
	}

	~TLSDtor()
	{
		FlsFree(mFlsValue);
	}

	void Add(void* data)
	{
		FlsSetValue(mFlsValue, data);
	}	
};

#else

template <typename T>
class TLSingleton
{
public:	
	pthread_key_t mTlsKey;

public:	
	TLSingleton()
	{
		mTlsKey = 0;
		pthread_key_create(&mTlsKey, TlsFreeFunc);
	}

	~TLSingleton()
	{		
		pthread_key_delete(mTlsKey);
	}

	static void NTAPI TlsFreeFunc(void* ptr)
	{
		delete (T*)ptr;
	}

	T* Get()
	{
		T* val = (T*)pthread_getspecific(this->mTlsKey);
		if (val == NULL)
		{
			val = new T();
			pthread_setspecific(this->mTlsKey, (void*)val);
		}
		return val;
	}
};

class TLSDtor
{
public:
	typedef void (*CallbackFunction)(void* lpFlsData);

public:
	pthread_key_t mTlsKey;
	
	TLSDtor(CallbackFunction callbackFunction)
	{
		mTlsKey = 0;
		pthread_key_create(&mTlsKey, callbackFunction);
	}

	~TLSDtor()
	{
		pthread_key_delete(mTlsKey);
	}

	void Add(void* data)
	{
		pthread_setspecific(mTlsKey, data);
	}	
};


#endif

NS_BF_END;