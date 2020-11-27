using System;

namespace Beefy.input
{
	class InputDevice
	{
		[CallingConvention(.Stdcall), CLink]
		public static extern void BFInput_Destroy(void* nativeInputDevice);

		[CallingConvention(.Stdcall), CLink]
		public static extern char8* BFInput_GetState(void* nativeInputDevice);

		void* mNativeInputDevice;

		public ~this()
		{
			BFInput_Destroy(mNativeInputDevice);
		}

		public void GetState(String outStr)
		{
			outStr.Append(BFInput_GetState(mNativeInputDevice));
		}
	}

	class InputManager
	{
		[CallingConvention(.Stdcall), CLink]
		public static extern char8* BFApp_EnumerateInputDevices();

		[CallingConvention(.Stdcall), CLink]
		public static extern void* BFApp_CreateInputDevice(char8* guid);

		public void EnumerateInputDevices(String outData)
		{
			outData.Append(BFApp_EnumerateInputDevices());
		}

		public InputDevice CreateInputDevice(StringView guid)
		{
			void* nativeInputDevice = BFApp_CreateInputDevice(guid.ToScopeCStr!());
			if (nativeInputDevice == null)
				return null;
			InputDevice inputDevice = new .();
			inputDevice.[Friend]mNativeInputDevice = nativeInputDevice;
			return inputDevice;
		}
	}
}
