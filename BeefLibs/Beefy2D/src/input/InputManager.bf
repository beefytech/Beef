using System;
using System.Threading;

namespace Beefy.input
{
	class InputDevice
	{
		[CallingConvention(.Stdcall), CLink]
		public static extern void BFInput_Destroy(void* nativeInputDevice);

		[CallingConvention(.Stdcall), CLink]
		public static extern char8* BFInput_GetState(void* nativeInputDevice);

		public String mProdName ~ delete _;
		public String mGUID ~ delete _;
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

		public Monitor mMonitor = new .() ~ delete _;
		public String mEnumerateString = new .() ~ delete _;
		public bool mEnumerating;

		public void CachedEnumerateInputDevices(String outData)
		{
			using (mMonitor.Enter())
			{
				outData.Append(mEnumerateString);

				if (!mEnumerating)
				{
					mEnumerating = true;
					ThreadPool.QueueUserWorkItem(new () =>
						{
							String data = scope .();
							data.Append(BFApp_EnumerateInputDevices());
							using (mMonitor.Enter())
							{
								mEnumerateString.Set(data);
								mEnumerating = false;
							}
						});
				}
			}
		}

		public void EnumerateInputDevices(String outData)
		{
			outData.Append(BFApp_EnumerateInputDevices());
		}

		public InputDevice CreateInputDevice(StringView prodName, StringView guid)
		{
			void* nativeInputDevice = BFApp_CreateInputDevice(guid.ToScopeCStr!());
			if (nativeInputDevice == null)
				return null;
			InputDevice inputDevice = new .();
			inputDevice.mProdName = new String(prodName);
			inputDevice.mGUID = new String(guid);
			inputDevice.[Friend]mNativeInputDevice = nativeInputDevice;
			return inputDevice;
		}
	}
}
