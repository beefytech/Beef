using System;
using Beefy;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.theme;
using System.IO;

namespace MemProfiler
{	
	class MPApp : BFApp
	{
		public new static MPApp sApp;
		public float mTimePerFrame = 1.0f / 120;
		public bool mWantsFullscreen = false;

		WidgetWindow mMainWindow;
		Widget mRootWidget;

		public Widget mMainWidget;

		//public PopupMenuManager mPopupMenuManager;
		//public Trainer mTrainer ~ delete _;

		public Board mBoard;
		public Windows.ProcessInformation mProcessInformation;

		public this()
		{
			sApp = this;
			gApp = this;

			RefreshRate = 120;
		}
		
		static uint32 TimeToUnixTime(DateTime ft)
		{
			// takes the last modified date
			int64 date = ft.ToFileTime();
		
			// 100-nanoseconds = milliseconds * 10000
			int64 adjust = 11644473600000L * 10000;
		
			// removes the diff between 1970 and 1601
			date -= adjust;
		
			// converts back from 100-nanoseconds to seconds
			return (uint32)(date / 10000000L);
		}

		public void Fail(String text)
		{
		    //MessageBeep(MessageBeepType.Error);

		    Dialog aDialog = ThemeFactory.mDefault.CreateDialog("ERROR", text, DarkTheme.sDarkTheme.mIconError);
		    aDialog.mDefaultButton = aDialog.AddButton("OK");
		    aDialog.mEscButton = aDialog.mDefaultButton;
		    aDialog.PopupWindow(mMainWindow);
		}

		public override void Init()
		{		
			/*uint timeNow = GetCurrentUnixTime();

			FitEntry[] fitEntries = new FitEntry[200];
			for (int i = 0; i < fitEntries.Length; i++)
			{				
				fitEntries[i].mElapsedTimeMS = i * 500;
				fitEntries[i].mPower = i;
				fitEntries[i].mCadence = 90;
			}

			int[] lapOffsets = scope int[] {100};

			WriteFit("c:\\temp\\test.fit", UnixTimeToFitTime(timeNow), fitEntries.CArray(), fitEntries.Length, lapOffsets.CArray(), lapOffsets.Length);*/

			base.Init();

			DarkTheme aTheme = new DarkTheme();
			aTheme.Init();
			ThemeFactory.mDefault = aTheme;

			BFWindow.Flags windowFlags = BFWindow.Flags.Border | BFWindow.Flags.SysMenu | //| BFWindow.Flags.CaptureMediaKeys |
			    BFWindow.Flags.Caption | BFWindow.Flags.Minimize | BFWindow.Flags.QuitOnClose | BFWindowBase.Flags.Resizable;
			if (mWantsFullscreen)
				windowFlags |= BFWindowBase.Flags.Fullscreen;

			//mRootWidget = new Widget();
			mBoard = new Board();
			mRootWidget = mBoard;
			mMainWindow = new WidgetWindow(null, "Memory Profiler", 0, 0, 1024, 768, windowFlags, mRootWidget);
			mMainWindow.SetMinimumSize(480, 360);
			mMainWindow.mIsMainWindow = true;
	

			//String injectDllName = "HeapyInject_x64.dll";
			//String launchExeName = @"c:\Beef\IDE\dist\IDE_bf.exe";
			//String argsStr = @"-proddir=c:\beef\ide\";
			String launchExeName = @"c:\Beef\IDE\dist\IDE_bfd.exe";
			String argsStr = @"-proddir=c:\beef\ide";

			String launchStr = scope String(launchExeName, " ", argsStr);

			String launchDir = scope String();
			Path.GetDirectoryName(launchExeName, launchDir);

			mProcessInformation = Windows.ProcessInformation();
			var startupInfo = Windows.StartupInfo();
			Windows.GetStartupInfoA(&startupInfo);

			String dllName = @"C:\temp\Heapy\Debug\HeapyInject_x64.dll";

			int32 flags = Windows.CREATE_SUSPENDED;
			if (!Windows.CreateProcessA(null, launchStr, null, null, 0, flags, null, launchDir, &startupInfo, &mProcessInformation))
			{
				Fail("Failed to create process");
			}
			else
			{
				LoadLibraryInjection(mProcessInformation.mProcess, dllName);
				Windows.ResumeThread(mProcessInformation.mThread);
			}
			//PushMainWidget(mBoard);
		}

		Result<int> LoadLibraryInjection(Windows.ProcessHandle proc, String dllName)
        {
			void* RemoteString, LoadLibAddy;
			LoadLibAddy = Windows.GetProcAddress(Windows.GetModuleHandleW("kernel32.dll".ToScopedNativeWChar!()), "LoadLibraryA");
		
			RemoteString = Windows.VirtualAllocEx(proc, null, (int)dllName.Length, Windows.MEM_RESERVE|Windows.MEM_COMMIT, Windows.PAGE_READWRITE);
			if (RemoteString == null)
            {
				Windows.CloseHandle(proc); // Close the process handle.
				//throw std::runtime_error("LoadLibraryInjection: Error on VirtualAllocEx.");
				return .Err;//new Exception("LoadLibraryInjection: Error on VirtualAllocEx.");
			}
		
			if (Windows.WriteProcessMemory(proc, (char8*)RemoteString, (char8*)dllName, dllName.Length, null) == 0)
            {
				Windows.VirtualFreeEx(proc, RemoteString, 0, Windows.MEM_RELEASE); // Free the memory we were going to use.
				Windows.CloseHandle(proc); // Close the process handle.
				return .Err;//new Exception("LoadLibraryInjection: Error on WriteProcessMemeory.");
			}
		
			Windows.Handle hThread;

			hThread = Windows.CreateRemoteThread(proc, null, 0, LoadLibAddy, (char8*)RemoteString, 0, null);
			if (hThread.IsInvalid)
			{
				Windows.VirtualFreeEx(proc, RemoteString, 0, Windows.MEM_RELEASE); // Free the memory we were going to use.
				Windows.CloseHandle(proc); // Close the process handle.
				return .Err;//new Exception("LoadLibraryInjection: Error on CreateRemoteThread.");
			}
		
			// Wait for the thread to finish.
			Windows.WaitForSingleObject(hThread, -1);
		
			// Lets see what it says...
			int32 dwThreadExitCode=0;
			Windows.GetExitCodeThread(hThread, out dwThreadExitCode);
		
			// No need for this handle anymore, lets get rid of it.
			Windows.CloseHandle(hThread);
		
			// Lets clear up that memory we allocated earlier.
			Windows.VirtualFreeEx(proc, RemoteString, 0, Windows.MEM_RELEASE);
		
			return dwThreadExitCode;
		}

		public override bool HandleCommandLineParam(String key, String value)
		{
			base.HandleCommandLineParam(key, value);
			
			if (key == "-fullscreen")
			{
				mWantsFullscreen = true;
				return true;
			}
			if (key == "-windowed")
			{
				mWantsFullscreen = false;
				return true;
			}
			return false;
		}

		public void PushMainWidget(Widget widget)
		{
			if (mMainWidget != null)
			{
				mMainWidget.RemoveSelf();
				DeferDelete(mMainWidget);
			}

			mMainWidget = widget;

			mRootWidget.AddWidget(widget);
			widget.Resize(0, 0, mRootWidget.mWidth, mRootWidget.mHeight);
			widget.SetFocus();			
		}
	}
}

static
{
	static MemProfiler.MPApp gApp;
}