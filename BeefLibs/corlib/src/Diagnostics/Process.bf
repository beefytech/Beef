using System.IO;
using System.Threading;
using System.Text;
using System.Collections.Generic;

namespace System.Diagnostics
{
	class Process
	{
		Platform.BfpProcess* mProcess;
		String mProcessName ~ delete _;

		public int32 Id
		{
			get
			{
				return Platform.BfpProcess_GetProcessId(mProcess);
			}
		}

		public static int32 CurrentId
		{
			get
			{
				return (int32)Platform.BfpProcess_GetCurrentId();
			}
		}

		public bool IsAttached
		{
			get
			{
				return mProcess != null;
			}
		}

		public StringView ProcessName
		{
			get
			{
				if (mProcessName == null)
				{
					mProcessName = new String();
					Platform.GetStrHelper(mProcessName, scope (outPtr, outSize, outResult) =>
						{
							Platform.BfpProcess_GetProcessName(mProcess, outPtr, outSize, (Platform.BfpProcessResult*)outResult);
						});
				}
				return mProcessName;
			}
		}

		public ~this()
		{
			Dispose();
		}

		public void Dispose()
		{
			if (mProcess != null)
			{
				Platform.BfpProcess_Release(mProcess);
				mProcess = null;
			}
		}

		public Result<void> GetProcessById(String machineName, int32 processId)
		{
			if (mProcess != null)
			{
				Dispose();
			}
			
			let bfpProcess = Platform.BfpProcess_GetById((machineName != null) ? machineName : null, processId, null);
			if (bfpProcess == null)
				return .Err;
			mProcess = bfpProcess;

			return .Ok;
		}

		public Result<void> GetProcessById(int32 processId)
		{
			return GetProcessById(null, processId);
		}

		public void GetMainWindowTitle(String outTitle)
		{
			Platform.GetStrHelper(outTitle, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpProcess_GetMainWindowTitle(mProcess, outPtr, outSize, (Platform.BfpProcessResult*)outResult);
				});
		}

		public static Result<void> GetProcesses(List<Process> processes)
		{
			let result = Platform.GetSizedHelper<Platform.BfpProcess*>(scope (outPtr, outSize, outResult) =>
                {
					Platform.BfpProcess_Enumerate(null, outPtr, outSize, (Platform.BfpProcessResult*)outResult);
                });

			switch (result)
			{
			case .Err:
				return .Err;
			case .Ok(let bfpProcSpan):
				for (var proc in bfpProcSpan)
				{
					let process = new Process();
					process.mProcess = proc;
					processes.Add(process);
				}
				delete bfpProcSpan.Ptr;
			}

			return .Ok;
		}
	}
}
