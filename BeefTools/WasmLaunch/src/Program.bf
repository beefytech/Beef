#pragma warning disable 162
#pragma warning disable 168

using System;
using System.IO;
using System.Diagnostics;
using System.Net;
using System.Threading;

namespace WasmLaunch;

class Program
{
	public static void SafeKill(int processId)
	{
		String exePath = Environment.GetExecutableFilePath(.. scope .());
		String exeDir = Path.GetDirectoryPath(exePath, .. scope .());
		var beefConExe = scope $"{exeDir}/BeefCon.exe";

		ProcessStartInfo procInfo = scope ProcessStartInfo();
		procInfo.UseShellExecute = false;
		procInfo.SetFileName(beefConExe);
		procInfo.SetArguments(scope $"{processId} kill");
		procInfo.ActivateWindow = false;

		var process = scope SpawnedProcess();
		process.Start(procInfo).IgnoreError();
	}

	public static void SafeKill(SpawnedProcess process)
	{
		if (process.WaitFor(0))
			return;
		SafeKill(process.ProcessId);
		if (!process.WaitFor(2000))
			process.Kill();
	}

	static void WaitForPort(int32 port)
	{
		Stopwatch sw = scope .();
		sw.Start();

		Socket.Init();
		while (sw.ElapsedMilliseconds < 5000)
		{
			var socket = scope Socket();
			if (socket.Connect("127.0.0.1", port, var sockaddr) case .Ok)
				return;
		}
	}

	public static void OpenURL(StringView url)
	{
		ProcessStartInfo htmlProcInfo = scope ProcessStartInfo();
		htmlProcInfo.UseShellExecute = true;
		htmlProcInfo.SetFileName(url);
		var htmlProcess = scope SpawnedProcess();
		htmlProcess.Start(htmlProcInfo).IgnoreError();
	}

	public static int Main(String[] args)
	{
		String exePath = Environment.GetExecutableFilePath(.. scope .());

		int32 pid = 0;
		String htmlPath = scope .();

		if (args.Count == 1)
		{
			htmlPath.Append(args[0]);

			ProcessStartInfo procInfo = scope ProcessStartInfo();
			procInfo.UseShellExecute = false;
			procInfo.SetFileName(exePath);
			procInfo.SetArguments(scope $"{Process.CurrentId} \"{htmlPath}\"");
			procInfo.ActivateWindow = false;

			var process = scope SpawnedProcess();
			if (process.Start(procInfo) case .Err)
				return 1;

			// Just idle
			while (true)
			{
				Thread.Sleep(1000);
			}

			return 0;
		}
		else if (args.Count == 2)
		{
			pid = int32.Parse(args[0]).GetValueOrDefault();
			htmlPath.Append(args[1]);
		}

		int32 port = 8042;

		String exeDir = Path.GetDirectoryPath(exePath, .. scope .());

		String htmlDir = Path.GetDirectoryPath(htmlPath, .. scope .());
		String htmlFileName = Path.GetFileName(htmlPath, .. scope .());

		String serverPath = scope .();
		Path.GetAbsolutePath("miniserve.exe", exeDir, serverPath..Clear());
		if (!File.Exists(serverPath))
			Path.GetAbsolutePath("../../bin/miniserve.exe", exeDir, serverPath..Clear());

		ProcessStartInfo procInfo = scope ProcessStartInfo();
		procInfo.UseShellExecute = false;
		procInfo.SetFileName(serverPath);
		procInfo.SetArguments(scope $"-p {port} {htmlDir}");
		procInfo.CreateNoWindow = true;

		var process = scope SpawnedProcess();
		process.Start(procInfo).IgnoreError();

		WaitForPort(port);
		OpenURL(scope $"http://127.0.0.1:{port}/{htmlFileName}");

		while (true)
		{
			if (process.WaitFor(100))
				break;

			if (pid != 0)
			{
				bool isProcessOpen = false;
				var srcProcess = Platform.BfpProcess_GetById(null, pid, null);
				if (srcProcess != null)
				{
					if (!Platform.BfpProcess_WaitFor(srcProcess, 0, null, null))
						isProcessOpen = true;
					Platform.BfpProcess_Release(srcProcess);
				}
				if (!isProcessOpen)
					break;
			}
		}

		SafeKill(process);

		return 0;
	}
}