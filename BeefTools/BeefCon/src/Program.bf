using System;
using System.Threading;
using System.IO;
using IDE.util;
using System.Diagnostics;
using Beefy.widgets;

namespace BeefCon;

class Program
{
	BeefConConsoleProvider.Pipe mPipe ~ delete _;
	WinNativeConsoleProvider mProvider ~ delete _;
	int32 mPid;
	int32 mAttachedPid;
	Windows.ProcessHandle mAttachedProcess;
	String mAttachedName = new .() ~ delete _;
	int32 mConid;
	String mExecStr = new .() ~ delete _;
	SpawnedProcess mSpawnedProcess ~ delete _;

	[CLink, CallingConvention(.Stdcall)]
	public static extern Windows.IntBool AttachConsole(int processId);

	[CallingConvention(.Stdcall)]
	function Windows.IntBool ConsoleCtrlHandler(int32 ctrlType);
	[CLink, CallingConvention(.Stdcall)]
	static extern Windows.IntBool SetConsoleCtrlHandler(ConsoleCtrlHandler handler, Windows.IntBool addHandler);

	[CLink, CallingConvention(.Stdcall)]
	static extern Windows.IntBool GenerateConsoleCtrlEvent(uint32 dwCtrlEvent, uint32 dwProcessGroupId);

	public ~this()
	{
		if (mSpawnedProcess != null)
		{
			mSpawnedProcess.Kill();
			mSpawnedProcess.WaitFor();
		}
	}

	static mixin GET<T>(var ptr)
	{
		*((T*)(ptr += sizeof(T)) - 1)
	}

	public void MessageLoop()
	{
		while (true)
		{
			switch (mPipe.ReadMessage(-1))
			{
			case .Ok(let msg):
				uint8* ptr = msg.Ptr + 1;
				switch (*(BeefConConsoleProvider.Message*)msg.Ptr)
				{
				case .GetData:
					mPipe.StartMessage(BeefConConsoleProvider.Message.Data);
					mPipe.Stream.Write((int32)mProvider.Width);
					mPipe.Stream.Write((int32)mProvider.Height);
					mPipe.Stream.Write((int32)mProvider.BufferHeight);
					mPipe.Stream.Write((int32)mProvider.ScrollTop);
					mPipe.Stream.Write(mProvider.CursorVisible);
					mPipe.Stream.Write(mProvider.CursorHeight);
					mPipe.Stream.Write(mProvider.CursorPos);
					for (int i < 16)
						mPipe.Stream.Write(mProvider.GetColor(i));

					for (int row < mProvider.Height)
					{
						for (int col < mProvider.Width)
						{
							var cell = mProvider.GetCell(col, row);
							mPipe.Stream.Write(cell.mChar);
							mPipe.Stream.Write(cell.mAttributes);
						}
					}
					mPipe.EndMessage();
				case .Resize:
					int32 cols = GET!<int32>(ptr);
					int32 rows = GET!<int32>(ptr);
					bool resizeContent = GET!<bool>(ptr);
					mProvider.Resize(cols, rows, resizeContent);
				case .KeyDown:
					KeyCode keyCode = GET!<KeyCode>(ptr);
					KeyFlags keyFlags = GET!<KeyFlags>(ptr);
					mProvider.KeyDown(keyCode, keyFlags);
				case .KeyUp:
					KeyCode keyCode = GET!<KeyCode>(ptr);
					mProvider.KeyUp(keyCode);
				case .InputString:
					int32 strLen = GET!<int32>(ptr);
					StringView str = .((.)ptr, strLen);
					mProvider.SendInput(str);
				case .MouseDown:
					int32 col = GET!<int32>(ptr);
					int32 row = GET!<int32>(ptr);
					int32 btnState = GET!<int32>(ptr);
					int32 btnCount = GET!<int32>(ptr);
					KeyFlags keyFlags = GET!<KeyFlags>(ptr);
					mProvider.MouseDown(col, row, btnState, btnCount, keyFlags);
				case .MouseMove:
					int32 col = GET!<int32>(ptr);
					int32 row = GET!<int32>(ptr);
					int32 btnState = GET!<int32>(ptr);
					KeyFlags keyFlags = GET!<KeyFlags>(ptr);
					mProvider.MouseMove(col, row, btnState, keyFlags);
				case .MouseUp:
					int32 col = GET!<int32>(ptr);
					int32 row = GET!<int32>(ptr);
					int32 btnState = GET!<int32>(ptr);
					KeyFlags keyFlags = GET!<KeyFlags>(ptr);
					mProvider.MouseUp(col, row, btnState, keyFlags);
				case .MouseWheel:
					int32 col = GET!<int32>(ptr);
					int32 row = GET!<int32>(ptr);
					int32 dy = GET!<int32>(ptr);
					mProvider.MouseWheel(col, row, dy);
				case .ScrollTo:
					int32 row = GET!<int32>(ptr);
					mProvider.ScrollTo(row);
				case .Update:
					bool paused = GET!<bool>(ptr);
					mProvider.Update(paused);
				case .Attached:
					int32 pid = GET!<int32>(ptr);
					Process process = scope .();
					if (process.GetProcessById(pid) case .Ok)
					{
						mAttachedPid = pid;
						mAttachedName.Set(process.ProcessName);
						WinNativeConsoleProvider.SetConsoleTitleW(mAttachedName.ToScopedNativeWChar!());
						mAttachedProcess = Windows.OpenProcess(Windows.PROCESS_ALL_ACCESS, false, mAttachedPid);
					}
				default:
				}
			case .Err(let err):
				return;
			}
		}
	}

	public void ClearConsoleTitle()
	{
		WinNativeConsoleProvider.SetConsoleTitleW("BeefIDE Debug Console".ToScopedNativeWChar!());
	}

	public void Run()
	{
		mPipe = new .();
		if (mPipe.Listen(mPid, mConid) case .Err)
			return;

		mProvider = new .();
		mProvider.mHideNativeConsole = !mExecStr.IsEmpty;
		mProvider.Attach();

		if (!mExecStr.IsEmpty)
		{
			ProcessStartInfo procInfo = scope ProcessStartInfo();
			procInfo.UseShellExecute = false;
			procInfo.SetFileName(mExecStr);

			mSpawnedProcess = new SpawnedProcess();
			if (mSpawnedProcess.Start(procInfo) case .Err)
				return;
		}
		else
		{
			ClearConsoleTitle();
			Console.ReopenHandles();
		}

		while (true)
		{
			// Check BeefIDE process
			if ((mPid != 123) || (!Debug.IsDebuggerPresent))
			{
				bool isProcessOpen = false;

				var process = Platform.BfpProcess_GetById(null, mPid, null);
				if (process != null)
				{
					if (!Platform.BfpProcess_WaitFor(process, 0, null, null))
						isProcessOpen = true;
					Platform.BfpProcess_Release(process);
				}

				if (!isProcessOpen)
				{
					Console.Error.WriteLine("Process closed");
					return;
				}
			}

			MessageLoop();

			if (mPipe.mFailed)
			{
				if (mSpawnedProcess == null)
				{
					DeleteAndNullify!(mPipe);
					mPipe = new .();
					mPipe.Listen(mPid, mConid);
				}	
				else
					return;
			}

			if (!mPipe.mConnected)
				Thread.Sleep(20);

			if (mSpawnedProcess != null)
			{
				if (mSpawnedProcess.WaitFor(0))
					return;
			}

			if (mAttachedPid != 0)
			{
				if ((Windows.GetExitCodeProcess(mAttachedProcess, var exitCode)) && (exitCode != 259))
				{
					ClearConsoleTitle();

					Console.WriteLine();
					Console.WriteLine(scope $"{mAttachedName} (process {mAttachedPid}) exited with code {exitCode}.");
					Console.WriteLine("Press any key to close this window...");

					mAttachedProcess.Close();
					mAttachedProcess = default;
					mAttachedPid = 0;
					mAttachedName.Clear();
				}
			}

			if ((mSpawnedProcess == null) && (mAttachedPid == 0))
			{
				if (Console.KeyAvailable)
					return;
			}
		}
	}

	/*public void RunHost()
	{
		mPipe = new .();
		mPipe.Listen(mPid, 0);



		WinNativeConsoleProvider.AllocConsole();
		WinNativeConsoleProvider.SetConsoleTitleW("BeefIDE Debug Console".ToScopedNativeWChar!());

		while (true)
		{
			var process = Platform.BfpProcess_GetById(null, mPid, null);
			if (process == null)
			{
				Console.Error.WriteLine("Process closed");
				return;
			}
			Platform.BfpProcess_Release(process);
			MessageLoop();

			if (mPipe.mFailed)
				return;

			if (!mPipe.mConnected)
				Thread.Sleep(20);
		}
	}*/

	public static int Main(String[] args)
	{
		if (args.Count < 1)
		{
			Console.Error.WriteLine("Usage: BeefCon <pid> [conid] [exe]");
			return 1;
		}

		Program pg = scope .();
		if (args.Count >= 3)
		{
			pg.mPid = int32.Parse(args[0]);
			pg.mConid = int32.Parse(args[1]);
			pg.mExecStr.Set(args[2]);
			pg.Run();
		}
		else if (args.Count >= 2)
		{
			pg.mPid = int32.Parse(args[0]);
			if (args[1] == "kill")
			{
				AttachConsole(pg.mPid);
				SetConsoleCtrlHandler(default, true);
				GenerateConsoleCtrlEvent(/*CTRL_C_EVENT*/0, 0);
			}
		}
		else
		{
			pg.mPid = int32.Parse(args[0]);
			pg.mConid = -Process.CurrentId;
			pg.Run();
		}
		
		return 0;
	}
}