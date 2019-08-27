using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Beefy;

namespace IDE
{
    public class WakaTime
    {
        String mApiKey ~ delete _;

        String mPythonLocation ~ delete _;
        String mCurFilePath = new String() ~ delete _;
        int32 mLastSendTicks;
		Monitor mMonitor = new Monitor() ~ delete _;

        public this(String key)
        {
            mApiKey = new String(key);            
        }

        public void Dispose()
        {            
        }

        public void QueueFile(String filePath, String projectName, bool isWrite)
        {
			String useFilePath = filePath;
			String useProjectName = projectName;

            bool doSend = false;
            if (mCurFilePath != useFilePath)
            {
                doSend = true;                
            }
            else if (mLastSendTicks > 120*60)
            {                
                doSend = true;
            }

            if (isWrite)
                doSend = true;
            mCurFilePath.Set(useFilePath);

            if (doSend)
            {
				useFilePath = new String(useFilePath);
				useProjectName = new String(useProjectName);
				var me = this;
                ThreadPool.QueueUserWorkItem(new () => me.SendFile(useFilePath, useProjectName, isWrite) ~ { delete useFilePath; delete useProjectName; });
                mLastSendTicks = 0;
            }
        }

        public void Update()
        {
            mLastSendTicks++;
        }
        
        public void getPythonDir(String pyDir)
        {
			getCurrentDirectory(pyDir);
            pyDir.Append("\\Python");
        }

        public void getPython(String outPath)
        {

			doesPythonExist();
			//return;


            if (mPythonLocation != null)
			{
                outPath.Append(mPythonLocation);
				return;
			}

            String[] locations = scope
            	.(
					//"c:\\Python27\\python.exe",

                    "pythonw.exe",
                    "python",
                    "\\Python37\\pythonw",
                    "\\Python36\\pythonw",
                    "\\Python35\\pythonw",
                    "\\Python34\\pythonw",
                    "\\Python33\\pythonw",
                    "\\Python32\\pythonw",
                    "\\Python31\\pythonw",
                    "\\Python30\\pythonw",
                    "\\Python27\\pythonw",
                    "\\Python26\\pythonw",
                    "\\python37\\pythonw",
                    "\\python36\\pythonw",
                    "\\python35\\pythonw",
                    "\\python34\\pythonw",
                    "\\python33\\pythonw",
                    "\\python32\\pythonw",
                    "\\python31\\pythonw",
                    "\\python30\\pythonw",
                    "\\python27\\pythonw",
                    "\\python26\\pythonw",
                    "\\Python37\\python",
                    "\\Python36\\python",
                    "\\Python35\\python",
                    "\\Python34\\python",
                    "\\Python33\\python",
                    "\\Python32\\python",
                    "\\Python31\\python",
                    "\\Python30\\python",
                    "\\Python27\\python",
                    "\\Python26\\python",
                    "\\python37\\python",
                    "\\python36\\python",
                    "\\python35\\python",
                    "\\python34\\python",
                    "\\python33\\python",
                    "\\python32\\python",
                    "\\python31\\python",
                    "\\python30\\python",
                    "\\python27\\python",
                    "\\python26\\python"
                );
            for (String location in locations)
            {
                if (location.Contains('\\'))
				{
					String dirName = scope String();
					Path.GetDirectoryPath(location, dirName);
                    if (!Directory.Exists(dirName))
                        continue;
				}

                ProcessStartInfo procInfo = scope ProcessStartInfo();
                procInfo.UseShellExecute = false;
                procInfo.RedirectStandardError = true;
				procInfo.RedirectStandardOutput = true;
                procInfo.SetFileName(location);
                procInfo.CreateNoWindow = true;
                procInfo.SetArguments("--version");

				Debug.WriteLine("ProcStartInfo {0} Verb: {1}", procInfo, procInfo.mVerb);

				/*Process process = null;
				if (!case .Ok(out process) = Process.Start(procInfo))
					continue;
				defer(scope) delete process;
                String errors = scope String();
                if (case .Err = process.StandardError.ReadToEnd(errors))
					continue;*/

				String resultStr = scope String();
				SpawnedProcess process = scope SpawnedProcess();
				if (process.Start(procInfo) case .Err)
					continue;

				FileStream fileStream = scope FileStream();
				process.AttachStandardError(fileStream);
				StreamReader streamReader = scope StreamReader(fileStream, null, false, 4096);
				streamReader.ReadToEnd(resultStr).IgnoreError();

				if (resultStr.IsEmpty)
				{
					FileStream fileStreamOut = scope FileStream();
					process.AttachStandardOutput(fileStreamOut);
					StreamReader streamReaderOut = scope StreamReader(fileStreamOut, null, false, 4096);
					streamReaderOut.ReadToEnd(resultStr).IgnoreError();
				}

				//TODO: This 'errors' check is not correct, but also it seems that we're getting stdout data in stderr... (?)
                if ((resultStr != null) && (resultStr != ""))
                {
                    mPythonLocation = new String(location);
					outPath.Append(mPythonLocation);
                    return;
                }
            }

			mPythonLocation = new String("");// Give up

            return;
        }

        void GetCLI(String cliPath)
        {
            cliPath.Append(BFApp.sApp.mInstallDir, "wakatime-master/wakatime/cli.py");
        }

		SpawnedProcess mProcess ~ delete _;

		public void SendFile(String fileName, String projectName, bool isWrite)
		{
			using (mMonitor.Enter())
			{
                DoSendFile(fileName, projectName, isWrite);
			}
		}

        public void DoSendFile(String fileName, String projectName, bool isWrite)
        {
            Debug.WriteLine("WakaTime: {0} proj: {1} isWrite: {2} ThreadId: {3} Tick: {4}", fileName, projectName, isWrite, Thread.CurrentThread.Id, (int32)Platform.BfpSystem_TickCount());

            String arguments = scope String();
            arguments.Append("\"");
            GetCLI(arguments);
            arguments.Append("\" --key=\"", mApiKey, "\"",
                                " --file=\"", fileName, "\"",
                                " --plugin=\"Beef\"");

            if (!String.IsNullOrWhiteSpace(projectName))
                arguments.Append(" --project=\"", projectName, "\"");

            if (isWrite)
                arguments.Append(" --write");

            ProcessStartInfo procInfo = scope ProcessStartInfo();
            procInfo.UseShellExecute = false;
			String pyPath = scope String();
			getPython(pyPath);
			if (String.IsNullOrEmpty(pyPath))
				return;
            procInfo.SetFileName(pyPath);
            procInfo.CreateNoWindow = true;
            procInfo.SetArguments(arguments);



			//for (int i = 0; i < 10; i++)
			{
				//Debug.WriteLine("ProcStartInfo {0} Dir: {1} Verb: {2}", procInfo, procInfo.mDirectory, procInfo.mVerb);

				delete mProcess;
				mProcess = new SpawnedProcess();
				if (mProcess.Start(procInfo) case .Err)
				{
					delete mProcess;																																													    
					mProcess = null;
				}
			}
        }

        /// Check if wakatime command line exists or not
        bool doesCLIExist()
        {
			String path = scope String();
			GetCLI(path);
            if (File.Exists(path))
            {
                return true;
            }
            return false;
        }

        /// Check if bundled python installation exists
        bool doesPythonExist()
        {
			String path = scope String();
			getPythonDir(path);
			path.Append("\\pythonw.exe");
            if (File.Exists(path))
            {
                return true;
            }
            return false;
        }


        /// Check if python is installed
        bool isPythonInstalled()
        {
			String pyPath = scope String();
			getPython(pyPath);
            if (String.IsNullOrEmpty(pyPath))
            {
                return true;
            }
            return false;
        }

        /// Returns current working dir
        static public void getCurrentDirectory(String outDir)
        {
            var exePath = scope String();
            Environment.GetExecutableFilePath(exePath);
            Path.GetDirectoryPath(exePath, outDir);
        }
    }
}
