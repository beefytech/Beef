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
	int32 mConid;
	String mExecStr = new .() ~ delete _;
	SpawnedProcess mSpawnedProcess ~ delete _;

	public ~this()
	{
		mSpawnedProcess.Kill();
		mSpawnedProcess.WaitFor();
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
				default:
				}
			case .Err(let err):
				return;
			}
		}
	}

	public void Run()
	{
		mPipe = new .();
		mPipe.Listen(mPid, mConid);

		mProvider = new .();
		//mProvider.mHideNativeConsole = false;
		mProvider.Attach();

		ProcessStartInfo procInfo = scope ProcessStartInfo();
		procInfo.UseShellExecute = false;
		procInfo.SetFileName(mExecStr);

		mSpawnedProcess = new SpawnedProcess();
		if (mSpawnedProcess.Start(procInfo) case .Err)
			return;

		while (true)
		{
			mProvider.Update();

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

			if (mSpawnedProcess.WaitFor(0))
				return;
		}
	}

	public static int Main(String[] args)
	{
		if (args.Count < 2)
		{
			Console.Error.WriteLine("Usage: BeefCon <pid> <conid> <exe>");
			return 1;
		}

		Program pg = scope .();
		pg.mPid = int32.Parse(args[0]);
		pg.mConid = int32.Parse(args[1]);
		pg.mExecStr.Set(args[2]);
		pg.Run();

		return 0;
	}
}