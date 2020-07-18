using System;
using Beefy.utils;
using System.Diagnostics;

namespace IDE.ui
{
	class DiagnosticsPanel : Panel
	{
		Stopwatch mSampleStopwatch = new .() ~ delete _;
		int mSampleKernelTime;
		int mSampleUserTime;

		public this()
		{
			mSampleStopwatch.Start();
		}

		public override void Update()
		{
			String procInfo = scope .();
			gApp.mDebugger.GetProcessInfo(procInfo);

			int virtualMem = 0;
			int workingMem = 0;
			int kernelTime = 0;
			int userTime = 0;

			for (let line in procInfo.Split('\n'))
			{
				if (line.IsEmpty)
					break;

				var lineEnum = line.Split('\t');
				let category = lineEnum.GetNext().Value;
				let valueSV = lineEnum.GetNext().Value;
				let value = int64.Parse(valueSV).Value;

				switch (category)
				{
				case "WorkingMemory": workingMem = value;
				case "VirtualMemory": virtualMem = value;
				case "KernelTime": kernelTime = value;
				case "UserTime": userTime = value;
				}
			}


		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "DiagnosticsPanel");
		}
	}
}
