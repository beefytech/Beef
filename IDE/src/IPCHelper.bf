using System;
using System.Collections;

namespace IDE
{
	public class IPCHelper
	{
#if BF_PLATFORM_WINDOWS
		public const int32 cBufferSize = 1024;

		public Windows.Handle mPipeHandle ~ _.Close();
		public bool mHasConnection;
		public String mBuffer = new String() ~ delete _;
		public List<String> mMessages = new List<String>() ~ DeleteContainerAndItems!(_);

		public ~this()
		{			
		}

		public bool Init(String name)
		{
			String pipeName = scope String(@"\\.\pipe\", name);
			mPipeHandle = Windows.CreateNamedPipeA(pipeName, Windows.PIPE_ACCESS_DUPLEX,       // read/write access 
          		Windows.PIPE_TYPE_MESSAGE |       // message type pipe 
          		Windows.PIPE_READMODE_MESSAGE |   // message-read mode 
          		Windows.PIPE_NOWAIT,
				1/*Windows.PIPE_UNLIMITED_INSTANCES*/, // max. instances  
				cBufferSize,              // output buffer size 
				cBufferSize,              // input buffer size 
				0 /*NMPWAIT_USE_DEFAULT_WAIT*/, // client time-out 
				null);
			
			if (mPipeHandle.IsInvalid)
			{
				// Someone else has the IPC pipe open, ask them to shut it down (asserting that WE are the app that wants to host it now)
#unwarn
				int32 lastError = Windows.GetLastError();

				Windows.FileHandle fileHandle = Windows.CreateFileA(pipeName, Windows.FILE_WRITE_DATA, default, null, System.IO.FileMode.Open, 0, 0);

				if (fileHandle.IsInvalid)
					return false;

				String closeCmd = "StopIPC\n";

				int32 bytesWritten;
				Windows.WriteFile(fileHandle, (uint8*)closeCmd.CStr(), (int32)closeCmd.Length, out bytesWritten, null);

				fileHandle.Close();				
				return false;
			}
			
			return true;
		}

		public void CloseConnection()
		{
			if (mHasConnection)
			{
				mHasConnection = false;
				Windows.DisconnectNamedPipe(mPipeHandle);
				mBuffer.Clear();
			}
		}

		public void Update()
		{
			if (!mHasConnection)
			{
				if (!Windows.ConnectNamedPipe(mPipeHandle, null))
				{
					int32 lastError = Windows.GetLastError();
					/*if (lastError == Windows.ERROR_NO_DATA)
					{
						Windows.DisconnectNamedPipe();
					}*/

					if ((lastError != Windows.ERROR_PIPE_CONNECTED) && (lastError != Windows.ERROR_NO_DATA))
						return;
				}
				mHasConnection = true;
			}

			uint8* buffer = scope uint8[1024]*;
			int32 bytesRead;
			int32 result = Windows.ReadFile(mPipeHandle, buffer, 1024, out bytesRead, null);
			if ((result <= 0) || (bytesRead == 0))
			{
				int32 lastError = Windows.GetLastError();
				if (lastError == Windows.ERROR_BROKEN_PIPE)
				{
					CloseConnection();
				}
			}
			else
			{
				for (int32 i = 0; i < bytesRead; i++)
				{
					mBuffer.Append((char8)buffer[i]);
				}
			}
			
			int crPos = mBuffer.IndexOf('\n');
			if (crPos > 0)
			{
				String msg = new String(mBuffer, 0, crPos);
				mBuffer.Remove(0, crPos + 1);
				mMessages.Add(msg);
			}
		}

		public void Send(String str)
		{
			int32 bytesWritten;
			Windows.WriteFile(mPipeHandle, (uint8*)str.CStr(), (int32)str.Length, out bytesWritten, null);
		}

		public String PopMessage()
		{
			if (mMessages.Count == 0)
				return null;
			return mMessages.PopFront();
		}
#endif //BF_PLATFORM_WINDOWS
	}
}
