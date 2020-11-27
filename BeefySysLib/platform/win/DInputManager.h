#pragma once

#include "../../BeefySysLib/Common.h"
#include "../../BeefySysLib/BFApp.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

NS_BF_BEGIN

class DInputDevice : public BFInputDevice
{
public:
	LPDIRECTINPUTDEVICE8 mDirectInputDevice;

public:
	~DInputDevice();	
	virtual String GetState() override;
};

class DInputManager
{
public:
	LPDIRECTINPUT8 mDirectInput;
	String mEnumData;

public:
	DInputManager();
	~DInputManager();

	String EnumerateDevices();
	DInputDevice* CreateInputDevice(const StringImpl& guid);
};

NS_BF_END