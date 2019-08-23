using System;
using System.Diagnostics;
using System.IO;

namespace IDE
{
	class SourceControl
	{
		void DoP4Cmd(StringView fileName)
		{
			String actualFileName = scope String();
			Path.GetActualPathName(fileName, actualFileName);

			ProcessStartInfo psi = scope ProcessStartInfo();
			psi.SetFileName("p4.exe");
			var args = scope String();
			args.AppendF("edit -c default \"{0}\"", actualFileName);
			Debug.WriteLine("P4: {0}", args);
			psi.SetArguments(args);
			psi.UseShellExecute = false;
			psi.CreateNoWindow = true;

			var process = scope SpawnedProcess();
			process.Start(psi).IgnoreError();
			process.WaitFor(-1);
		}

		public void Checkout(StringView fileName)
		{
			DoP4Cmd(fileName);
		}
	}
}