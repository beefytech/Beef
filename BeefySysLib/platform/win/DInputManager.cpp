#include "DInputManager.h"
#include "WinBFApp.h"

#pragma comment (lib, "dinput8.lib")
#pragma comment (lib, "dxguid.lib")

USING_NS_BF;

static BOOL DIEnumDevicesCallback(LPCDIDEVICEINSTANCE lpddi, LPVOID pvRef)
{
	String& outStr = *(String*)pvRef;

	auto AddStr = [&](const StringImpl& str)
	{
		for (auto c : str)
		{
			if ((c >= 32) && (c < 128))
				outStr += c;
		}
	};

	AddStr(UTF8Encode(lpddi->tszInstanceName));
	outStr += "\t";
	AddStr(UTF8Encode(lpddi->tszProductName));

	auto guid = lpddi->guidInstance;
	outStr += StrFormat("\t%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	//OutputDebugStrF("Device: %s %s\n", lpddi->tszInstanceName, lpddi->tszProductName);

	outStr += "\n";

	return TRUE;
}

DInputManager::DInputManager()
{	
	mDirectInput = NULL;
	//TODO: This doesn't work for DLLs
	DirectInput8Create(::GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&mDirectInput, NULL);	

	if (mDirectInput == NULL)
		return;	
}

DInputManager::~DInputManager()
{
	mDirectInput->Release();
}

DInputDevice* DInputManager::CreateInputDevice(const StringImpl& guidStr)
{
	UUID guid = { 0 };
	UuidFromStringA((RPC_CSTR)guidStr.c_str(), &guid);

	LPDIRECTINPUTDEVICE8 device = NULL;
	mDirectInput->CreateDevice(guid, &device, NULL);

	if (device == NULL)
		return NULL;

	HRESULT result = 0;

	DInputDevice* inputDevice = new DInputDevice();
	inputDevice->mDirectInputDevice = device;

	WinBFWindow* window = NULL;
	for (auto checkWindow : gBFApp->mWindowList)
	{
		window = (WinBFWindow*)checkWindow;
		break;
	}

	if (window != NULL)
	{
		result = device->SetCooperativeLevel(window->mHWnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	}

	result = device->SetDataFormat(&c_dfDIJoystick2);
	
	return inputDevice;
}

String DInputManager::EnumerateDevices()
{
	String str;
	if (mDirectInput != NULL)
		mDirectInput->EnumDevices(DI8DEVCLASS_ALL, DIEnumDevicesCallback, &str, DIEDFL_ATTACHEDONLY);
	return str;
}

DInputDevice::~DInputDevice()
{
	mDirectInputDevice->Release();
}

String DInputDevice::GetState()
{
	DIJOYSTATE2 joyState = { 0 };
	HRESULT result = mDirectInputDevice->Poll();
	if (FAILED(result))
	{
		result = mDirectInputDevice->Acquire();
		while (result == DIERR_INPUTLOST)
			result = mDirectInputDevice->Acquire();
	}

	result = mDirectInputDevice->GetDeviceState(sizeof(joyState), &joyState);

	String str;
	if (!FAILED(result))
	{
		if (joyState.lX != 0)
			str += StrFormat("X\t%d\n", joyState.lX);
		if (joyState.lY != 0)
			str += StrFormat("Y\t%d\n", joyState.lY);
		if (joyState.lZ != 0)
			str += StrFormat("Z\t%d\n", joyState.lZ);

		//str += StrFormat("X\t%d\nY\t%d\nZ\t%d\n", joyState.lX, joyState.lY, joyState.lZ);

		for (int i = 0; i < 128; i++)
		{
			if (joyState.rgbButtons[i] != 0)
			{
				str += StrFormat("Btn\t%d\n", i);
			}
		}
	}
	else
	{
		str += "!FAILED";
	}

	return str;
}
