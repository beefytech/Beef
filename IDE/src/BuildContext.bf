using System.Collections;
using System;
using IDE.Compiler;
using System.IO;
using System.Diagnostics;
using Beefy;
using IDE.util;
using IDE.Util;

namespace IDE
{
	enum CompileKind
	{
		case Normal;
		case RunAfter;
		case DebugAfter;
		case DebugComptime;
		case Test;

		public bool WantsRunAfter
		{
			get
			{
				return (this == .RunAfter) || (this == .DebugAfter);
			}
		}
	}

	class BuildContext
	{
		public int32 mUpdateCnt;
		public Project mHotProject;
		public Workspace.Options mWorkspaceOptions;
		public Dictionary<Project, String> mImpLibMap = new .() ~ DeleteDictionaryAndValues!(_);
		public Dictionary<Project, String> mTargetPathMap = new .() ~ DeleteDictionaryAndValues!(_);
		public ScriptManager.Context mScriptContext = new .() ~ _.ReleaseLastRef();
		public ScriptManager mScriptManager ~ delete _;

		public bool Failed
		{
			get
			{
				if (mScriptManager != null)
					return mScriptManager.mFailed;
				return false;
			}
		}

		public enum CustomBuildCommandResult
		{
			NoCommands,
			HadCommands,
			Failed
		}

		Workspace.PlatformType mPlatformType;
		Workspace.ToolsetType mToolset;
		int mPtrSize;

		public this()
		{
			Workspace.Options workspaceOptions = gApp.GetCurWorkspaceOptions();
			mToolset = workspaceOptions.mToolsetType;
			mPlatformType = Workspace.PlatformType.GetFromName(gApp.mPlatformName, workspaceOptions.mTargetTriple);
			mPtrSize = Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName);
		}

		public CustomBuildCommandResult QueueProjectCustomBuildCommands(Project project, String targetPath, Project.BuildCommandTrigger trigger, List<String> cmdList)
		{
			if (cmdList.IsEmpty)
				return .NoCommands;

			if (trigger == .Never)
				return .NoCommands;

			List<Project> depProjectList = scope .();
			gApp.GetDependentProjectList(project, depProjectList);

			if ((trigger == .IfFilesChanged) && (!project.mForceCustomCommands))
			{
				int64 highestDateTime = 0;

				int64 targetDateTime = File.GetLastWriteTime(targetPath).Get().ToFileTime();

				bool forceRebuild = false;

				for (var depName in depProjectList)
				{
					var depProject = gApp.mWorkspace.FindProject(depName.mProjectName);
					if (depProject != null)
					{
						if (depProject.mLastDidBuild)
							forceRebuild = true;
					}
				}

				project.WithProjectItems(scope [&] (projectItem) =>
					{
						var projectSource = projectItem as ProjectSource;
						var importPath = scope String();
						projectSource.GetFullImportPath(importPath);
						Result<DateTime> fileDateTime = File.GetLastWriteTime(importPath);
						if (fileDateTime case .Ok)
						{
							let date = fileDateTime.Get().ToFileTime();
							/*if (date > targetDateTime)
								Console.WriteLine("Custom build higher time: {0}", importPath);*/
							highestDateTime = Math.Max(highestDateTime, date);
						}
					});

				if ((highestDateTime <= targetDateTime) && (!forceRebuild))
					return .NoCommands;

				project.mLastDidBuild = true;
			}

			Workspace.Options workspaceOptions = gApp.GetCurWorkspaceOptions();
			Project.Options options = gApp.GetCurProjectOptions(project);

			bool didCommands = false;

			//let targetName = scope String("Project ", project.mProjectName);

			//Console.WriteLine("Executing custom command {0} {1} {2}", highestDateTime, targetDateTime, forceRebuild);
			for (let origCustomCmd in cmdList)
			{
				bool isCommand = false;
				for (let c in origCustomCmd.RawChars)
				{
					if ((c == '\"') || (c == '$') || (c == '\n'))
						break;
					if (c == '(')
						isCommand = true;
				}

				String customCmd = scope String();

				if (isCommand)
				{
					customCmd.Append(origCustomCmd);
				}
				else
				{
					customCmd.Append("%exec ");
					gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, origCustomCmd, "custom command", customCmd);

					// For multi-line execs
					customCmd.Replace('\n', '\v');
				}

				if (customCmd.IsWhiteSpace)
					continue;

				if (mScriptManager == null)
				{
					mScriptManager = new .(mScriptContext);
					mScriptManager.mIsBuildScript = true;
					mScriptManager.mSoftFail = true;
					mScriptManager.mVerbosity = gApp.mVerbosity;
					didCommands = true;
				}

				let scriptCmd = new IDEApp.ScriptCmd();
				scriptCmd.mCmd = new String(customCmd);
				scriptCmd.mPath = new $"project {project.mProjectName}";
				gApp.mExecutionQueue.Add(scriptCmd);
				continue;
			}

			return didCommands ? .HadCommands : .NoCommands;
		}

		bool QueueProjectGNUArchive(Project project, String targetPath, Workspace.Options workspaceOptions, Project.Options options, String objectsArg)
		{
#if BF_PLATFORM_WINDOWS
			String llvmDir = scope String(IDEApp.sApp.mInstallDir);
			IDEUtils.FixFilePath(llvmDir);
			llvmDir.Append("llvm/");
#else
		    //String llvmDir = "";
#endif

		    //String error = scope String();
		    bool isTest = options.mBuildOptions.mBuildKind == .Test;
			bool isExe = ((project.mGeneralOptions.mTargetType != Project.TargetType.BeefLib) && (project.mGeneralOptions.mTargetType != Project.TargetType.BeefTest)) || (isTest);
			if ((options.mBuildOptions.mBuildKind == .StaticLib) || (options.mBuildOptions.mBuildKind == .DynamicLib))
			{
				// Okay
			}
			else if (!isExe)
				return true;

			String projectBuildDir = scope String();
			gApp.GetProjectBuildDir(project, projectBuildDir);
			File.WriteAll(scope $"{projectBuildDir}/ObjectArgs.txt", .((.)objectsArg.Ptr, objectsArg.Length)).IgnoreError();

		    String arCmds = null; //-O2 -Rpass=inline 
															 //(doClangCPP ? "-lc++abi " : "") +

			String arArgs = scope .();

			bool useArCmds = false;

			if (useArCmds)
			{
				arCmds = scope:: String("");
			    arCmds.AppendF("CREATE {}\n", targetPath);

				void AddObject(StringView obj)
				{
					if (obj.IsEmpty)
						return;

					if (obj.EndsWith(".lib", .OrdinalIgnoreCase))
						arCmds.AppendF("ADDLIB {}\n", obj);
					else
						arCmds.AppendF("ADDMOD {}\n", obj);
				}

				bool inQuote = false;
				int lastEnd = -1;
				for (int i < objectsArg.Length)
				{
					var c = objectsArg[i];
					if (c == '"')
					{
						if (inQuote)
							AddObject(objectsArg.Substring(lastEnd + 1, i - lastEnd - 1));
						inQuote = !inQuote;
						lastEnd = i;
					}
					else if ((c == ' ') && (!inQuote))
					{
						AddObject(objectsArg.Substring(lastEnd + 1, i - lastEnd - 1));
						lastEnd = i;
					}
				}
				AddObject(objectsArg.Substring(lastEnd + 1));

				for (let obj in objectsArg.Split(' '))
				{
					if (!obj.IsEmpty)
					{
						
					}
				}
				arCmds.AppendF("SAVE\n");
			}
			else
			{
				arArgs.AppendF($"-qc {targetPath}");

				void AddObject(StringView obj)
				{
					if (obj.IsEmpty)
						return;

					arArgs.Append(" ");
					arArgs.Append(obj);
				}

				bool inQuote = false;
				int lastEnd = -1;
				for (int i < objectsArg.Length)
				{
					var c = objectsArg[i];
					if (c == '"')
					{
						if (inQuote)
							AddObject(objectsArg.Substring(lastEnd + 1, i - lastEnd - 1));
						inQuote = !inQuote;
						lastEnd = i;
					}
					else if ((c == ' ') && (!inQuote))
					{
						AddObject(objectsArg.Substring(lastEnd + 1, i - lastEnd - 1));
						lastEnd = i;
					}
				}
				AddObject(objectsArg.Substring(lastEnd + 1));
			}

			UpdateCacheStr(project, "", workspaceOptions, options, null, null);

		    if (project.mNeedsTargetRebuild)
		    {
		        if (File.Delete(targetPath) case .Err)
				{
				    gApp.OutputLine("Failed to delete {0}", targetPath);
				    return false;
				}

				String arPath = scope .();
#if BF_PLATFORM_WINDOWS
				arPath.Clear();
				arPath.Append(gApp.mInstallDir);
				arPath.Append(@"llvm\bin\llvm-ar.exe");
#elif BF_PLATFORM_MACOS
				arPath.Append("llvm/bin/llvm-ar");
#else
				arPath.Append("/usr/bin/ar");
#endif

				String workingDir = scope String();
				workingDir.Append(gApp.mInstallDir);

				String scriptPath = scope .();
				if (Path.GetTempFileName(scriptPath) case .Err)
				{
					return false;
				}
				if (File.WriteAllText(scriptPath, arCmds) case .Err)
				{
					gApp.OutputLine("Failed to write archive script {0}", scriptPath);
					return false;
				}


				if (arCmds != null)
					arArgs.Append("-M");

		        var runCmd = gApp.QueueRun(arPath, arArgs, workingDir, .UTF8);
				runCmd.mReference = new String(project.mProjectName);
		        runCmd.mOnlyIfNotFailed = true;
				if (arCmds != null)
					runCmd.mStdInData = new .(arCmds);
		        var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
		        tagetCompletedCmd.mOnlyIfNotFailed = true;
		        gApp.mExecutionQueue.Add(tagetCompletedCmd);

				project.mLastDidBuild = true;
		    }
			else
			{
				var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
				tagetCompletedCmd.mOnlyIfNotFailed = true;
				gApp.mExecutionQueue.Add(tagetCompletedCmd);
			}

			return true;
		}

		void WSLPathFix(String str)
		{
			for (int i = 1; i < str.Length - 1; i++)
			{
				if (str[i] == ':')
				{
					if (str[i - 1].IsLetter)
					{
						int j = i;
						for ( ; j < str.Length; j++)
						{
							char8 cj = str[j];
							if (cj == '\\')
								str[j] = '/';
							if ((cj.IsWhiteSpace) || (cj == '"'))
								break;
						}

						str.Remove(i);
						str[i - 1] = str[i - 1].ToLower;
						str.Insert(i - 1, "/mnt/");
					}
				}
			}
		}

		bool QueueProjectGNULink(Project project, String targetPath, Workspace.Options workspaceOptions, Project.Options options, String objectsArg)
		{
			if (options.mBuildOptions.mBuildKind == .Intermediate)
				return true;

			bool isDebug = gApp.mConfigName.IndexOf("Debug", true) != -1;

			bool isMinGW = false;

#if BF_PLATFORM_WINDOWS
			bool isWSL = mPlatformType == .Linux;
			String llvmDir = scope String(IDEApp.sApp.mInstallDir);
			IDEUtils.FixFilePath(llvmDir);
			llvmDir.Append("llvm/");
#else
		    String llvmDir = "";
			bool isWSL = false;
#endif

		    //String error = scope String();

		    bool isTest = options.mBuildOptions.mBuildKind == .Test;
			bool isExe = ((project.mGeneralOptions.mTargetType != Project.TargetType.BeefLib) && (project.mGeneralOptions.mTargetType != Project.TargetType.BeefTest)) || (isTest);
			bool isDynLib = (project.mGeneralOptions.mTargetType == Project.TargetType.BeefLib) && (options.mBuildOptions.mBuildKind == .DynamicLib);

			if (options.mBuildOptions.mBuildKind == .StaticLib)
				isExe = false;

			if (isExe || isDynLib)
			{
				CopyLibFiles(targetPath, workspaceOptions, options);

			    String linkLine = scope String(isDebug ? "-g " : "-g -O2 "); //-O2 -Rpass=inline 
																 //(doClangCPP ? "-lc++abi " : "") +
			    
			    linkLine.Append("-o ");
			    IDEUtils.AppendWithOptionalQuotes(linkLine, targetPath);
			    linkLine.Append(" ");

			    /*if (options.mBuildOptions.mLinkerType == Project.LinkerType.GCC)
			    {
					// ...
			    }
			    else
			    {
					linkLine.Append("--target=");
					GetTargetName(workspaceOptions, linkLine);
					linkLine.Append(" ");
			    }*/

				if (isDynLib)
				{
					linkLine.Append("-shared ");
				}

				if ((mPlatformType == .Windows) &&
			    	((project.mGeneralOptions.mTargetType == Project.TargetType.BeefGUIApplication) ||
			        (project.mGeneralOptions.mTargetType == Project.TargetType.C_GUIApplication)))
			    {
			        linkLine.Append("-mwindows ");
			    }

				if (mPlatformType == .Linux)
					linkLine.Append("-no-pie ");

				if (mPlatformType == .macOS)
					linkLine.Append("-Wl,-no_compact_unwind ");

			    linkLine.Append(objectsArg);

				//var destDir = scope String();
				//Path.GetDirectoryName();

				//TODO: Make an option
			    if (options.mBuildOptions.mCLibType == Project.CLibType.Static)
			    {
					if (mPlatformType != .macOS)
						linkLine.Append("-static-libgcc ");
					linkLine.Append("-static-libstdc++ ");
			    }
			    else
			    {
					if (isMinGW)
					{
				        String[] mingwFiles;
				        String fromDir;

				        if (mPtrSize == 4)
				        {
				            fromDir = scope:: String(llvmDir, "i686-w64-mingw32/bin/");
				            mingwFiles = scope:: String[] ( "libgcc_s_dw2-1.dll", "libstdc++-6.dll" );
				        }
				        else
				        {
				            fromDir = scope:: String(llvmDir, "x86_64-w64-mingw32/bin/");
				            mingwFiles = scope:: String[] ( "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll" );
				        }
				        for (var mingwFile in mingwFiles)
				        {
				            String fromPath = scope String(fromDir, mingwFile);
							//string toPath = projectBuildDir + "/" + mingwFile;
				            String toPath = scope String();
				            Path.GetDirectoryPath(targetPath, toPath);
				            toPath.Append("/", mingwFile);
				            if (!File.Exists(toPath))
							{
								if (File.Copy(fromPath, toPath) case .Err)
								{
									gApp.OutputLineSmart("ERROR: Failed to copy mingw file {0}", fromPath);
									return false;
								}
							}
				        }
					}
			    }

			    List<String> libPaths = scope .();
				defer ClearAndDeleteItems(libPaths);
				List<String> depPaths = scope .();
				defer ClearAndDeleteItems(depPaths);
				AddLinkDeps(project, options, workspaceOptions, linkLine, libPaths, depPaths);

				for (var libPath in libPaths)
				{
					IDEUtils.AppendWithOptionalQuotes(linkLine, libPath);
					linkLine.Append(" ");
				}

				String gccExePath;
				String clangExePath;
				if (isMinGW)
				{
				    gccExePath = "c:/mingw/bin/g++.exe";
				    clangExePath = scope String(llvmDir, "bin/clang++.exe");
				}
				else
				{
			        gccExePath = "/usr/bin/c++";
			        clangExePath = scope String("/usr/bin/c++");

			        if (File.Exists("/usr/bin/clang++"))
			        {
						gccExePath = "/usr/bin/clang++";
			        	clangExePath = scope String("/usr/bin/clang++");
			        }
			        else
			        {
						gccExePath = "/usr/bin/c++";
			        	clangExePath = scope String("/usr/bin/c++");
			        }
				}

				UpdateCacheStr(project, linkLine, workspaceOptions, options, depPaths, libPaths);

			    if (project.mNeedsTargetRebuild)
			    {
			        if (File.Delete(targetPath) case .Err)
					{
					    gApp.OutputLine("Failed to delete {0}", targetPath);
					    return false;
					}

					if (workspaceOptions.mToolsetType == .GNU)
					{
			            if (mPtrSize == 4)
			            {
			            }
			            else
			            {
							/*IDEUtils.AppendWithOptionalQuotes(linkLine, scope String("-L", llvmDir, "/x86_64-w64-mingw32/lib"));
							linkLine.Append(" ");
							IDEUtils.AppendWithOptionalQuotes(linkLine, scope String("-L", llvmDir, "/lib/gcc/x86_64-w64-mingw32/5.2.0"));
							linkLine.Append(" ");*/
			            }
					}
					else // Microsoft
					{
						if (mPtrSize == 4)
						{
							//linkLine.Append("-L\"C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.10586.0\\ucrt\\x86\" ");
							for (var libPath in gApp.mSettings.mVSSettings.mLib32Paths)
							{
								linkLine.AppendF("-L\"{0}\" ", libPath);
							}
						}
						else
						{
							/*linkLine.Append("-L\"C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\lib\\amd64\" ");
							linkLine.Append("-L\"C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\atlmfc\\lib\\amd64\" ");
							linkLine.Append("-L\"C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.14393.0\\ucrt\\x64\" ");
							linkLine.Append("-L\"C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.14393.0\\um\\x64\" ");*/
							for (var libPath in gApp.mSettings.mVSSettings.mLib64Paths)
							{
								linkLine.AppendF("-L\"{0}\" ", libPath);
							}
						}
					}

					if (options.mBuildOptions.mOtherLinkFlags.Length != 0)
					{
						var linkFlags = scope String();
						gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, options.mBuildOptions.mOtherLinkFlags, "link flags", linkFlags);
						linkLine.Append(linkFlags, " ");
					}

			        String compilerExePath = (workspaceOptions.mToolsetType == .GNU) ? gccExePath : clangExePath;
					String workingDir = scope String();
					if (!llvmDir.IsEmpty)
					{
						workingDir.Append(llvmDir, "bin");
					}
					else
					{
						workingDir.Append(gApp.mInstallDir);
					}

					if (isWSL)
					{
						linkLine.Insert(0, " ");
						linkLine.Insert(0, compilerExePath);
						compilerExePath = "wsl.exe";
						WSLPathFix(linkLine);
					}

			        var runCmd = gApp.QueueRun(compilerExePath, linkLine, workingDir, .UTF8);
					runCmd.mReference = new .(project.mProjectName);
			        runCmd.mOnlyIfNotFailed = true;
			        var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
			        tagetCompletedCmd.mOnlyIfNotFailed = true;
			        gApp.mExecutionQueue.Add(tagetCompletedCmd);

					String logStr = scope String();
					logStr.AppendF("IDE Process {0}\r\n", Platform.BfpProcess_GetCurrentId());
					logStr.Append(linkLine);
					String targetLogPath = scope String(targetPath, ".build.txt");
					if (Utils.WriteTextFile(targetLogPath, logStr) case .Err)
						gApp.OutputErrorLine("Failed to write {}", targetLogPath);

					project.mLastDidBuild = true;
			    }
			}
			else
			{
				var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
				tagetCompletedCmd.mOnlyIfNotFailed = true;
				gApp.mExecutionQueue.Add(tagetCompletedCmd);
			}

			return true;
		}

		bool QueueProjectWasmLink(Project project, String targetPath, Workspace.Options workspaceOptions, Project.Options options, String objectsArg)
		{
			//bool isDebug = gApp.mConfigName.IndexOf("Debug", true) != -1;

			//bool isMinGW = false;

		    //String error = scope String();

		    bool isTest = options.mBuildOptions.mBuildKind == .Test;
			bool isExe = ((project.mGeneralOptions.mTargetType != Project.TargetType.BeefLib) && (project.mGeneralOptions.mTargetType != Project.TargetType.BeefTest)) || (isTest);
			bool isDynLib = (project.mGeneralOptions.mTargetType == Project.TargetType.BeefLib) && (options.mBuildOptions.mBuildKind == .DynamicLib);

			if (isExe || isDynLib)
			{
				var actualTargetPath = Path.GetActualPathName(targetPath, .. scope .());

			    String linkLine = scope String();
			    linkLine.Append("-o ");
			    IDEUtils.AppendWithOptionalQuotes(linkLine, actualTargetPath);
			    linkLine.Append(" ");

			    linkLine.Append(objectsArg);

			    List<String> libPaths = scope .();
				defer ClearAndDeleteItems(libPaths);
				List<String> depPaths = scope .();
				defer ClearAndDeleteItems(depPaths);
				AddLinkDeps(project, options, workspaceOptions, linkLine, libPaths, depPaths);

				for (var libPath in libPaths)
				{
					IDEUtils.AppendWithOptionalQuotes(linkLine, libPath);
					linkLine.Append(" ");
				}
				
				if (options.mBuildOptions.mOtherLinkFlags.Length != 0)
				{
					var linkFlags = scope String();
					gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, options.mBuildOptions.mOtherLinkFlags, "link flags", linkFlags);
					linkLine.Append(linkFlags, " ");
				}

				UpdateCacheStr(project, linkLine, workspaceOptions, options, depPaths, libPaths);
				
			    if (project.mNeedsTargetRebuild)
			    {
			        if (File.Delete(targetPath) case .Err)
					{
					    gApp.OutputLine("Failed to delete {0}", targetPath);
					    return false;
					}

					String compilerExePath = scope String();
					if (gApp.mSettings.mEmscriptenPath.IsEmpty)
					{
						gApp.OutputErrorLine("Emscripten path not configured. Check Wasm configuration in File\\Preferences\\Settings.");
						return false;
					}
					else
					{
						compilerExePath.Append(gApp.mSettings.mEmscriptenPath);
						if ((!compilerExePath.EndsWith('\\')) && (!compilerExePath.EndsWith('/')))
							compilerExePath.Append("/");
					}

					if (!File.Exists(scope $"{gApp.mInstallDir}/Beef{IDEApp.sRTVersionStr}RT32_wasm.a"))
					{
						gApp.OutputErrorLine("Wasm runtime libraries not found. Build with 'wasm/build_wasm.bat'.");
						return false;
					}

					compilerExePath.Append(@"/upstream/emscripten/emcc.bat");
					//linkLine.Append(" c:\\Beef\\wasm\\BeefRT.a -s STRICT=1 -s USE_PTHREADS=1 -s ALIASING_FUNCTION_POINTERS=1 -s ASSERTIONS=0 -s DISABLE_EXCEPTION_CATCHING=0 -s DEMANGLE_SUPPORT=0 -s EVAL_CTORS=1 -s WASM=1 -s \"EXPORTED_FUNCTIONS=['_BeefMain','_BeefDone','_pthread_mutexattr_init','_pthread_mutex_init','_emscripten_futex_wake','_calloc','_sbrk']\"");
					linkLine.Append("-s DISABLE_EXCEPTION_CATCHING=0 -s DEMANGLE_SUPPORT=0 -s WASM=1");

					if (project.mWasmOptions.mEnableThreads)
						linkLine.Append(" -s USE_PTHREADS=1");

					if (workspaceOptions.mEmitDebugInfo != .No)
						linkLine.Append(" -g");

					if (!workspaceOptions.mRuntimeChecks)
						linkLine.Append(" -s ASSERTIONS=0");

					linkLine.Replace('\\', '/');

					var targetDir = Path.GetDirectoryPath(actualTargetPath, .. scope .());
			        var runCmd = gApp.QueueRun(compilerExePath, linkLine, targetDir, .UTF8);
					runCmd.mReference = new .(project.mProjectName);
			        runCmd.mOnlyIfNotFailed = true;
			        var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
			        tagetCompletedCmd.mOnlyIfNotFailed = true;
			        gApp.mExecutionQueue.Add(tagetCompletedCmd);

					String logStr = scope String();
					logStr.AppendF("IDE Process {0}\r\n", Platform.BfpProcess_GetCurrentId());
					logStr.Append(linkLine);
					String targetLogPath = scope String(targetPath, ".build.txt");
					if (Utils.WriteTextFile(targetLogPath, logStr) case .Err)
						gApp.OutputErrorLine("Failed to write {}", targetLogPath);

					project.mLastDidBuild = true;
			    }
			}
			else
			{
				var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
				tagetCompletedCmd.mOnlyIfNotFailed = true;
				gApp.mExecutionQueue.Add(tagetCompletedCmd);
			}

			return true;
		}

		public static void GetPdbPath(String targetPath, Workspace.Options workspaceOptions, Project.Options options, String outPdbPath)
		{
			int lastDotPos = targetPath.LastIndexOf('.');
			if (lastDotPos == -1)
				return;
			outPdbPath.Append(targetPath, 0, lastDotPos);
			if (workspaceOptions.mToolsetType == .LLVM)
				outPdbPath.Append("_lld");
			outPdbPath.Append(".pdb");
		}

		public static void GetRtLibNames(Workspace.PlatformType platformType, Workspace.Options workspaceOptions, Project.Options options, bool dynName, String outRt, String outDbg, String outAlloc)
		{			
			if ((platformType == .Linux) || (platformType == .macOS) || (platformType == .iOS))
			{
				if (options.mBuildOptions.mBeefLibType == .DynamicDebug)
					outRt.Append("libBeefRT_d.a");
				else
					outRt.Append("libBeefRT.a");
				return;
			}

			if ((!dynName) || (options.mBuildOptions.mBeefLibType != .Static))
			{
				outRt.Append("Beef", IDEApp.sRTVersionStr, "RT");
				outRt.Append((Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 4) ? "32" : "64");
				switch (options.mBuildOptions.mBeefLibType)
				{
				case .Dynamic:
				case .DynamicDebug: outRt.Append("_d");
				case .Static:
					switch (options.mBuildOptions.mCLibType)
					{
					case .None:
					case .Dynamic, .SystemMSVCRT: outRt.Append("_s");
					case .DynamicDebug: outRt.Append("_sd");
					case .Static: outRt.Append("_ss");
					case .StaticDebug: outRt.Append("_ssd");
					}
				}
				outRt.Append(dynName ? ".dll" : ".lib");
			}

			if ((workspaceOptions.mEnableObjectDebugFlags) || (workspaceOptions.mAllocType == .Debug) || (workspaceOptions.mAllocType == .Stomp))
			{
				outDbg.Append("Beef", IDEApp.sRTVersionStr, "Dbg");
				outDbg.Append((Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 4) ? "32" : "64");
				if (options.mBuildOptions.mBeefLibType == .DynamicDebug)
					outDbg.Append("_d");
				outDbg.Append(dynName ? ".dll" : ".lib");
			}

			if ((workspaceOptions.mAllocType == .TCMalloc) || (workspaceOptions.mAllocType == .TCMalloc_Debug) ||
				(workspaceOptions.mAllocType == .JEMalloc) || (workspaceOptions.mAllocType == .JEMalloc_Debug))
			{
				outAlloc.Append(((workspaceOptions.mAllocType == .TCMalloc) || (workspaceOptions.mAllocType == .TCMalloc_Debug)) ? "TCMalloc" : "JEMalloc");
				outAlloc.Append((Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 4) ? "32" : "64");
				if ((workspaceOptions.mAllocType == .TCMalloc_Debug) || (workspaceOptions.mAllocType == .JEMalloc_Debug))
					outAlloc.Append("_d");
				outAlloc.Append(dynName ? ".dll" : ".lib");
			}
		}

		bool CopyLibFiles(String targetPath, Workspace.Options workspaceOptions, Project.Options options)
		{
		    List<String> stdLibFileNames = scope .(2);
		    String fromDir;
		    
		    fromDir = scope String(gApp.mInstallDir);

			bool AddLib(String dllName)
			{
				stdLibFileNames.Add(dllName);

				String fromPath = scope String(fromDir, dllName);
				String toPath = scope String();
				Path.GetDirectoryPath(targetPath, toPath);
				toPath.Append("/", dllName);
				if (File.CopyIfNewer(fromPath, toPath) case .Err(let err))
				{
					gApp.OutputLine("Failed to copy lib file '{0}' to '{1}'", fromPath, toPath);
					return false;
				}
				return true;
			}

			String rtName = scope String();
			String dbgName = scope String();
			String allocName = scope String();
			GetRtLibNames(mPlatformType, workspaceOptions, options, true, rtName, dbgName, allocName);
			if (!rtName.IsEmpty)
				if (!AddLib(rtName))
					return false;
			if (!dbgName.IsEmpty)
				if (!AddLib(dbgName))
					return false;
			if (!allocName.IsEmpty)
				if (!AddLib(allocName))
					return false;
			return true;
		}

		void AddLinkDeps(Project project, Project.Options options, Workspace.Options workspaceOptions, String linkLine, List<String> libPaths, List<String> depPaths)
		{
			void AddLibPath(StringView libPathIn, Project project, Project.Options projectOptions)
			{
				var libPath = new String();
				if (gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, projectOptions, libPathIn, "lib paths", libPath))
				{
					IDEUtils.FixFilePath(libPath);
					libPaths.Add(libPath);
				}
				else
					delete libPath;
			}


			defer ClearAndDeleteItems(depPaths);
			void AddDepPath(StringView depPathIn, Project project, Project.Options projectOptions)
			{
				var depPath = new String();
				if (gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, projectOptions, depPathIn, "dep paths", depPath))
				{
					IDEUtils.FixFilePath(depPath);
					depPaths.Add(depPath);
				}
				else
					delete depPath;
			}

			for (let libPath in options.mBuildOptions.mLibPaths)
				AddLibPath(libPath, project, options);
			for (let depPath in options.mBuildOptions.mLinkDependencies)
				AddDepPath(depPath, project, options);

			List<Project> depProjectList = scope List<Project>();
			gApp.GetDependentProjectList(project, depProjectList);
			if (depProjectList.Count > 0)
			{
			    for (var depProject in depProjectList)
			    {
			        var depOptions = gApp.GetCurProjectOptions(depProject);
					if (depOptions != null)
					{
						if (depOptions.mClangObjectFiles != null)
			            {
							var argBuilder = scope IDEApp.ArgBuilder(linkLine, true);

							for (var fileName in depOptions.mClangObjectFiles)
							{
								argBuilder.AddFileName(fileName);
								argBuilder.AddSep();
							}
						}    
					}

					bool depIsDynLib = (depProject.mGeneralOptions.mTargetType == Project.TargetType.BeefLib) && (depOptions?.mBuildOptions.mBuildKind == .DynamicLib);
					if (depIsDynLib)
					{
						if (mImpLibMap.TryGetValue(depProject, var libPath))
						{
							IDEUtils.AppendWithOptionalQuotes(linkLine, libPath);
							linkLine.Append(" ");
						}
					}

					if (depProject.mGeneralOptions.mTargetType == .BeefLib)
					{
						let depProjectOptions = gApp.GetCurProjectOptions(depProject);
						if (depProjectOptions != null)
						{
							var linkFlags = scope String();
							gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, depProject, depProjectOptions, depProjectOptions.mBuildOptions.mOtherLinkFlags, "link flags", linkFlags);
							if (!linkFlags.IsWhiteSpace)
								linkLine.Append(linkFlags, " ");

							for (let libPath in depProjectOptions.mBuildOptions.mLibPaths)
								AddLibPath(libPath, depProject, depProjectOptions);
							for (let depPath in depProjectOptions.mBuildOptions.mLinkDependencies)
								AddDepPath(depPath, depProject, depProjectOptions);
						}
					}
			    }
			}
		}

		void UpdateCacheStr(Project project, StringView linkLine, Workspace.Options workspaceOptions, Project.Options options, List<String> depPaths, List<String> libPaths)
		{
			String cacheStr = scope String();

			void AddBuildFileDependency(StringView filePath, bool resolveString = false)
			{
				var filePath;

				if ((resolveString) && (filePath.Contains('$')))
				{
					String resolvedFilePath = scope:: String();
					gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, filePath, "link flags", resolvedFilePath);
					filePath = resolvedFilePath;
				}

				int64 fileTime = 0;
				if (!filePath.IsEmpty)
					fileTime = File.GetLastWriteTime(filePath).GetValueOrDefault().ToFileTime();
				cacheStr.AppendF("{}\t{}\n", filePath, fileTime);
			}

			cacheStr.AppendF("Args\t{}\n", linkLine);
			cacheStr.AppendF("Toolset\t{}\n", workspaceOptions.mToolsetType);
			AddBuildFileDependency(project.mWindowsOptions.mIconFile);
			AddBuildFileDependency(project.mWindowsOptions.mManifestFile);

			switch (mPlatformType)
			{
			case .Windows:
				cacheStr.AppendF("Description\t{}\n", project.mWindowsOptions.mDescription);
				cacheStr.AppendF("Comments\t{}\n", project.mWindowsOptions.mComments);
				cacheStr.AppendF("Company\t{}\n", project.mWindowsOptions.mCompany);
				cacheStr.AppendF("Product\t{}\n", project.mWindowsOptions.mProduct);
				cacheStr.AppendF("Copyright\t{}\n", project.mWindowsOptions.mCopyright);
				cacheStr.AppendF("FileVersion\t{}\n", project.mWindowsOptions.mFileVersion);
				cacheStr.AppendF("ProductVersion\t{}\n", project.mWindowsOptions.mProductVersion);
			case .Linux:
				cacheStr.AppendF("Options\t{}\n", project.mLinuxOptions.mOptions);
			case .Wasm:
				cacheStr.AppendF("EnableThreads\t{}\n", project.mWasmOptions.mEnableThreads);
			default:
			}
			if (depPaths != null)
				for (var linkDep in depPaths)
					AddBuildFileDependency(linkDep, true);
			if (libPaths != null)
				for (var linkDep in libPaths)
					AddBuildFileDependency(linkDep, true);

			String projectBuildDir = scope String();
			gApp.GetProjectBuildDir(project, projectBuildDir);
			String prevCacheStr = scope .();
			gApp.mBfBuildCompiler.GetBuildValue(projectBuildDir, "Link", prevCacheStr);
			if (prevCacheStr != cacheStr)
			{
				project.mNeedsTargetRebuild = true;
				gApp.mBfBuildCompiler.SetBuildValue(projectBuildDir, "Link", cacheStr);
				gApp.mBfBuildCompiler.WriteBuildCache(projectBuildDir);
			}
		}

		bool QueueProjectMSLink(Project project, String targetPath, String configName, Workspace.Options workspaceOptions, Project.Options options, String objectsArg)
		{
			if (options.mBuildOptions.mBuildKind == .Intermediate)
				return true;

			bool is64Bit = mPtrSize == 8;

			String llvmDir = scope String(IDEApp.sApp.mInstallDir);
			IDEUtils.FixFilePath(llvmDir);
			llvmDir.Append("llvm/");

			bool isTest = options.mBuildOptions.mBuildKind == .Test;
			bool isExe = ((project.mGeneralOptions.mTargetType != Project.TargetType.BeefLib) && (project.mGeneralOptions.mTargetType != Project.TargetType.BeefTest)) || (isTest);
			if (options.mBuildOptions.mBuildKind == .DynamicLib)
				isExe = true;

			if (isExe)
			{
				String linkLine = scope String();
			    
			    linkLine.Append("-out:");
			    IDEUtils.AppendWithOptionalQuotes(linkLine, targetPath);
			    linkLine.Append(" ");

				if (isTest)
					linkLine.Append("-subsystem:console ");
			    else if (project.mGeneralOptions.mTargetType == .BeefGUIApplication)
					linkLine.Append("-subsystem:windows ");
			    else if (project.mGeneralOptions.mTargetType == .C_GUIApplication)
			    	linkLine.Append("-subsystem:console ");
				else if (project.mGeneralOptions.mTargetType == .BeefLib)
				{
					linkLine.Append("-dll ");

					if (targetPath.EndsWith(".dll", .InvariantCultureIgnoreCase))
					{
						linkLine.Append("-implib:\"");
						linkLine.Append(targetPath, 0, targetPath.Length - 4);
						linkLine.Append(".lib\" ");
					}
				}

			    linkLine.Append(objectsArg);

				CopyLibFiles(targetPath, workspaceOptions, options);

				List<String> libPaths = scope .();
				defer ClearAndDeleteItems(libPaths);
				List<String> depPaths = scope .();
				defer ClearAndDeleteItems(depPaths);
				AddLinkDeps(project, options, workspaceOptions, linkLine, libPaths, depPaths);

		        /*if (File.Delete(targetPath).Failed(true))
				{
				    OutputLine("Failed to delete {0}", targetPath);
				    return false;
				}*/

				switch (options.mBuildOptions.mCLibType)
				{
				case .None:
					linkLine.Append("-nodefaultlib ");
				case .Dynamic:
					//linkLine.Append((workspaceOptions.mMachineType == .x86) ? "-defaultlib:msvcprt " : "-defaultlib:msvcrt ");
					linkLine.Append("-defaultlib:msvcrt ");
				case .Static:
					//linkLine.Append((workspaceOptions.mMachineType == .x86) ? "-defaultlib:libcpmt " : "-defaultlib:libcmt ");
					linkLine.Append("-defaultlib:libcmt ");
				case .DynamicDebug:
					//linkLine.Append((workspaceOptions.mMachineType == .x86) ? "-defaultlib:msvcprtd " : "-defaultlib:msvcrtd ");
					linkLine.Append("-defaultlib:msvcrtd ");
				case .StaticDebug:
					//linkLine.Append((workspaceOptions.mMachineType == .x86) ? "-defaultlib:libcpmtd " : "-defaultlib:libcmtd ");
					linkLine.Append("-defaultlib:libcmtd ");
				case .SystemMSVCRT:
					linkLine.Append("-nodefaultlib ");

					String minRTModName = scope String();
					if ((project.mGeneralOptions.mTargetType == .BeefGUIApplication) ||
						(project.mGeneralOptions.mTargetType == .C_GUIApplication))
						minRTModName.Append("g");
					if (options.mBuildOptions.mBeefLibType == .DynamicDebug)
						minRTModName.Append("d");
					if (!minRTModName.IsEmpty)
						minRTModName.Insert(0, "_");

					if (!is64Bit)
						linkLine.Append("-libpath:\"", gApp.mInstallDir, "lib\\x86\" \"", gApp.mInstallDir, "lib\\x86\\msvcrt.lib\" Beef", IDEApp.sRTVersionStr,"MinRT32", minRTModName, ".lib ");
					else
						linkLine.Append("-libpath:\"", gApp.mInstallDir, "lib\\x64\" \"", gApp.mInstallDir, "lib\\x64\\msvcrt.lib\" Beef", IDEApp.sRTVersionStr,"MinRT64", minRTModName, ".lib ");
					linkLine.Append("ntdll.lib user32.lib kernel32.lib gdi32.lib winmm.lib shell32.lib ole32.lib rpcrt4.lib version.lib comdlg32.lib -ignore:4049 -ignore:4217 ");
				}

				for (var libPath in libPaths)
				{
					IDEUtils.AppendWithOptionalQuotes(linkLine, libPath);
					linkLine.Append(" ");
				}

				linkLine.Append("-nologo ");
				
				if ((project.mGeneralOptions.mTargetType == .BeefLib) && (workspaceOptions.mAllowHotSwapping) && (is64Bit))
				{
					// This helps to ensure that DLLs have enough hot swapping space after them
					int nameHash = targetPath.GetHashCode();
					int64 wantAddress = (((nameHash & 0x3FFFF) + 0x10) << 28);
					linkLine.AppendF("-base:0x{0:X} -dynamicbase:no ", wantAddress);
				}

				// Incremental just seems to be slower for Beef.  Test on larger projects to verify
				linkLine.Append("-incremental:no ");

				if (options.mBuildOptions.mStackSize > 0)
					linkLine.AppendF("-stack:{} ", options.mBuildOptions.mStackSize);

				linkLine.Append("-pdb:");
				let pdbName = scope String();
				GetPdbPath(targetPath, workspaceOptions, options, pdbName);
				IDEUtils.AppendWithOptionalQuotes(linkLine, pdbName);
				linkLine.Append(" ");

				//TODO: Only add -debug if we have some debug info?
				//if (isDebug)
				if (workspaceOptions.mEmitDebugInfo != .No)
					linkLine.Append("-debug ");

				if (workspaceOptions.mBfOptimizationLevel.IsOptimized())
					//linkLine.Append("-opt:ref -verbose ");
					linkLine.Append("-opt:ref ");
				else
					linkLine.Append("-opt:noref ");

				if (!is64Bit)
				{
					for (var libPath in gApp.mSettings.mVSSettings.mLib32Paths)
					{
						linkLine.AppendF("-libpath:\"{0}\" ", libPath);
					}
					linkLine.Append("-libpath:\"", gApp.mInstallDir, "lib\\x86\" ");
				}
				else
				{
					for (var libPath in gApp.mSettings.mVSSettings.mLib64Paths)
					{
						linkLine.AppendF("-libpath:\"{0}\" ", libPath);
					}
					linkLine.Append("-libpath:\"", gApp.mInstallDir, "lib\\x64\" ");
				}

				String targetDir = scope String();
				Path.GetDirectoryPath(targetPath, targetDir);
				linkLine.Append("-libpath:");
				IDEUtils.AppendWithOptionalQuotes(linkLine, targetDir);
				linkLine.Append(" ");

				if (options.mBuildOptions.mOtherLinkFlags.Length != 0)
				{
					var linkFlags = scope String();
					gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, options.mBuildOptions.mOtherLinkFlags, "link flags", linkFlags);
					linkLine.Append(linkFlags, " ");
				}

				let winOptions = project.mWindowsOptions;

				UpdateCacheStr(project, linkLine, workspaceOptions, options, depPaths, libPaths);

				if (project.mNeedsTargetRebuild)
				{
					String projectBuildDir = scope String();
					gApp.GetProjectBuildDir(project, projectBuildDir);

					if ((!String.IsNullOrWhiteSpace(project.mWindowsOptions.mIconFile)) ||
						(!String.IsNullOrWhiteSpace(project.mWindowsOptions.mManifestFile)) ||
		                (winOptions.HasVersionInfo()))
					{
						String resOutPath = scope String();
						resOutPath.Append(projectBuildDir, "\\Resource.res");

						String iconPath = scope String();
						gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, winOptions.mIconFile, "icon file", iconPath);
						
						// Generate resource
						Result<void> CreateResourceFile()
						{
		                    ResourceGen resGen = scope ResourceGen();
							if (resGen.Start(resOutPath) case .Err)
							{
								gApp.OutputErrorLine("Failed to create resource file '{0}'", resOutPath);
								return .Err;
							}
							if (!iconPath.IsWhiteSpace)
							{
								Path.GetAbsolutePath(scope String(iconPath), project.mProjectDir, iconPath..Clear());
								if (resGen.AddIcon(iconPath) case .Err)
								{
									gApp.OutputErrorLine("Failed to add icon");
									return .Err;
								}
							}

							let targetFileName = scope String();
							Path.GetFileName(targetPath, targetFileName);

							if (resGen.AddVersion(winOptions.mDescription, winOptions.mComments, winOptions.mCompany, winOptions.mProduct,
		                        winOptions.mCopyright, winOptions.mFileVersion, winOptions.mProductVersion, targetFileName) case .Err)
							{
								gApp.OutputErrorLine("Failed to add version");
								return .Err;
							}

							String manifestPath = scope String();
							gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, winOptions.mManifestFile, "manifest file", manifestPath);
							if (!manifestPath.IsWhiteSpace)
							{
								Path.GetAbsolutePath(scope String(manifestPath), project.mProjectDir, manifestPath..Clear());
								if (resGen.AddManifest(manifestPath) case .Err)
								{
									gApp.OutputErrorLine("Failed to add manifest file");
									return .Err;
								}
							}

							Try!(resGen.Finish());
							return .Ok;
						}

						if (CreateResourceFile() case .Err)
						{
							gApp.OutputErrorLine("Failed to generate resource file: {0}", resOutPath);
							return false;
						}

						IDEUtils.AppendWithOptionalQuotes(linkLine, resOutPath);
					}

					String linkerPath = scope String();
					if (workspaceOptions.mToolsetType == .LLVM)
					{
						linkerPath.Clear();
						linkerPath.Append(gApp.mInstallDir);
						linkerPath.Append(@"llvm\bin\lld-link.exe");
						//linkerPath = @"C:\Program Files\LLVM\bin\lld-link.exe";

						var ltoType = workspaceOptions.mLTOType;
						if (options.mBeefOptions.mLTOType != null)
							ltoType = options.mBeefOptions.mLTOType.Value;

						if (ltoType == .Thin)
						{
							linkLine.Append(" /lldltocache:");

							String ltoPath = scope String();
							Path.GetDirectoryPath(targetPath, ltoPath);
							ltoPath.Append("/ltocache");
							IDEUtils.AppendWithOptionalQuotes(linkLine, ltoPath);
						}

						if ((mPlatformType == .Windows) && (!is64Bit))
							linkLine.Append(" /safeseh:no");
					}
					else
					{
						let binPath = (!is64Bit) ? gApp.mSettings.mVSSettings.mBin32Path : gApp.mSettings.mVSSettings.mBin64Path;
						if (binPath.IsWhiteSpace)
						{
							gApp.OutputErrorLine("Visual Studio tool path not configured. Check Visual Studio configuration in File\\Preferences\\Settings.");
							return false;
						}
						linkerPath.Append(binPath);
						linkerPath.Append("/link.exe");
					}

					if (options.mBuildOptions.mBeefLibType != .DynamicDebug)
					{
						linkLine.Append(" /ignore:4099");
					}

					int targetDotPos = targetPath.LastIndexOf('.');
					if (targetDotPos != -1)
					{
						var writeEmitCmd = new IDEApp.WriteEmitCmd();
						writeEmitCmd.mPath = new .(targetPath, 0, targetDotPos);
						writeEmitCmd.mPath.Append("__emit.zip");
						writeEmitCmd.mProjectName = new .(project.mProjectName);
						gApp.mExecutionQueue.Add(writeEmitCmd);
					}

			        var runCmd = gApp.QueueRun(linkerPath, linkLine, gApp.mInstallDir, .UTF16WithBom);
					runCmd.mReference = new .(project.mProjectName);
					runCmd.mEnvVars = new .() { (new String("VSLANG"), new String("1033")) };
			        runCmd.mOnlyIfNotFailed = true;

			        var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
			        tagetCompletedCmd.mOnlyIfNotFailed = true;
			        gApp.mExecutionQueue.Add(tagetCompletedCmd);

					String logStr = scope String();
					logStr.AppendF("IDE Process {0}\r\n", Platform.BfpProcess_GetCurrentId());
					logStr.Append(linkLine);
					String targetLogPath = scope String(targetPath, ".build.txt");
					if (Utils.WriteTextFile(targetLogPath, logStr) case .Err)
						gApp.OutputErrorLine("Failed to write {}", targetLogPath);

					project.mLastDidBuild = true;
			    }
			}
			else
			{
				var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
				tagetCompletedCmd.mOnlyIfNotFailed = true;
				gApp.mExecutionQueue.Add(tagetCompletedCmd);
			}

			return true;
		}

		public bool QueueProjectCompile(Project project, Project hotProject, IDEApp.BuildCompletedCmd completedCompileCmd, List<String> hotFileNames, CompileKind compileKind)
		{
			project.mLastDidBuild = false;

			TestManager.ProjectInfo testProjectInfo = null;
			if (gApp.mTestManager != null)
				testProjectInfo = gApp.mTestManager.GetProjectInfo(project);

			var configSelection = gApp.GetCurConfigSelection(project);
		    Project.Options options = gApp.GetCurProjectOptions(project);
		    if (options == null)
		        return true;

		    Workspace.Options workspaceOptions = gApp.GetCurWorkspaceOptions();
		    BfCompiler bfCompiler = gApp.mBfBuildCompiler;
			BfSystem bfSystem = gApp.mBfBuildSystem;

			bfSystem.Lock(0);
		    var bfProject = gApp.mBfBuildSystem.mProjectMap[project];
		    bool bfHadOutputChanges = false;
		    List<String> bfFileNames = scope List<String>();
			if (hotProject == null)
			{
				if (project.mCurBfOutputFileNames == null)
				{
					BfCompiler.UsedOutputFlags usedOutputFlags = .FlushQueuedHotFiles;
					if (options.mBuildOptions.mBuildKind == .StaticLib)
						usedOutputFlags = .SkipImports;

					project.mCurBfOutputFileNames = new .();
					bfCompiler.GetOutputFileNames(bfProject, usedOutputFlags, out bfHadOutputChanges, project.mCurBfOutputFileNames);
				}
				for (var fileName in project.mCurBfOutputFileNames)
					bfFileNames.Add(fileName);
			}
			else
			{
				bfCompiler.GetOutputFileNames(bfProject, .FlushQueuedHotFiles, out bfHadOutputChanges, bfFileNames);
				defer:: ClearAndDeleteItems(bfFileNames);
			}
			bfSystem.Unlock();

		    if (bfHadOutputChanges)
		        project.mNeedsTargetRebuild = true;

		    List<ProjectSource> allFileNames = scope List<ProjectSource>();
		    List<String> clangAllObjNames = scope List<String>();
		    
		    gApp.GetClangFiles(project.mRootFolder, allFileNames);

		    String workspaceBuildDir = scope String();
		    gApp.GetWorkspaceBuildDir(workspaceBuildDir);
		    String projectBuildDir = scope String();
		    gApp.GetProjectBuildDir(project, projectBuildDir);
			if (!projectBuildDir.IsEmpty)
		    	Directory.CreateDirectory(projectBuildDir).IgnoreError();

			String targetPath = scope String();

		    String outputDir = scope String();
			String absOutputDir = scope String();
			
			if (testProjectInfo != null)
			{
				absOutputDir.Append(projectBuildDir);
				outputDir = absOutputDir;
				targetPath.Append(outputDir, "/", project.mProjectName);
				if (mPlatformType == .Windows)
					targetPath.Append(".exe");
				Debug.Assert(testProjectInfo.mTestExePath == null);
				testProjectInfo.mTestExePath = new String(targetPath);
			}
			else
		    {
				gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, options.mBuildOptions.mTargetDirectory, "target directory", outputDir);
				Path.GetAbsolutePath(project.mProjectDir, outputDir, absOutputDir);
				outputDir = absOutputDir;
				gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, project, options, "$(TargetPath)", "target path", targetPath);
			}
			IDEUtils.FixFilePath(targetPath);
		    if (!File.Exists(targetPath))
			{
		        project.mNeedsTargetRebuild = true;

				String targetDir = scope String();
				Path.GetDirectoryPath(targetPath, targetDir).IgnoreError();
				if (!targetDir.IsEmpty)
					Directory.CreateDirectory(targetDir).IgnoreError();
			}

			if (project.mGeneralOptions.mTargetType == .BeefLib)
			{
				if (targetPath.EndsWith(".dll", .InvariantCultureIgnoreCase))
				{
					String libPath = new .();
					libPath.Append(targetPath, 0, targetPath.Length - 4);
					libPath.Append(".lib");
					mImpLibMap.Add(project, libPath);
				}
			}

			mTargetPathMap[project] = new String(targetPath);

			if (hotProject == null)
			{
				switch (QueueProjectCustomBuildCommands(project, targetPath, compileKind.WantsRunAfter ? options.mBuildOptions.mBuildCommandsOnRun : options.mBuildOptions.mBuildCommandsOnCompile, options.mBuildOptions.mPreBuildCmds))
				{
				case .NoCommands:
				case .HadCommands:
				case .Failed:
					completedCompileCmd.mFailed = true;
				}
			}
				
			if (project.mGeneralOptions.mTargetType == .CustomBuild)
			{
				var tagetCompletedCmd = new IDEApp.TargetCompletedCmd(project);
				tagetCompletedCmd.mOnlyIfNotFailed = true;
				gApp.mExecutionQueue.Add(tagetCompletedCmd);

				return true; 
			}

#if IDE_C_SUPPORT
		    bool buildAll = false;
		    String buildStringFilePath = scope String();
		    mDepClang.GetBuildStringFileName(projectBuildDir, project, buildStringFilePath);
		    String newBuildString = scope String();
		    GetClangBuildString(project, options, workspaceOptions, true, newBuildString);
			String clangBuildString = scope String();
			GetClangBuildString(project, options, workspaceOptions, false, clangBuildString);
		    newBuildString.Append("|", clangBuildString);

		    if (mDepClang.mDoDependencyCheck)
		    {   
		     	String prependStr = scope String();
				options.mCOptions.mCompilerType.ToString(prependStr);
				prependStr.Append("|");
		        newBuildString.Insert(0, prependStr);
		        String oldBuildString;
		        mDepClang.mProjectBuildString.TryGetValue(project, out oldBuildString);

				if (oldBuildString == null)
				{
					oldBuildString = new String();
		            File.ReadAllText(buildStringFilePath, oldBuildString).IgnoreError();
					mDepClang.mProjectBuildString[project] = oldBuildString;
				}

		        if (newBuildString != oldBuildString)
		        {
		            buildAll = true;
		            
		            if (case .Err = File.WriteAllText(buildStringFilePath, newBuildString))
						OutputLine("Failed to write {0}", buildStringFilePath);
					
					delete oldBuildString;
		            mDepClang.mProjectBuildString[project] = new String(newBuildString);
		        }
		    }            			

		    using (mDepClang.mMonitor.Enter())
		    {
				if (options.mClangObjectFiles == null)
					options.mClangObjectFiles = new List<String>();
				else
					ClearAndDeleteItems(options.mClangObjectFiles);

		        for (var projectSource in allFileNames)
		        {
		            var fileEntry = mDepClang.GetProjectEntry(projectSource);
		            Debug.Assert((fileEntry != null) || (!mDepClang.mCompileWaitsForQueueEmpty));

		            String filePath = scope String();
		            projectSource.GetFullImportPath(filePath);
		            String baseName = scope String();
		            Path.GetFileNameWithoutExtension(filePath, baseName);
		            String objName = stack String();
		            objName.Append(projectBuildDir, "/", baseName, (options.mCOptions.mGenerateLLVMAsm ? ".ll" : ".obj"));

					if (filePath.Contains("test2.cpp"))
					{
\						NOP!();
					}	

		            bool needsRebuild = true;
		            if ((!buildAll) && (fileEntry != null))
		            {
		                mDepClang.SetEntryObjFileName(fileEntry, objName);
		                mDepClang.SetEntryBuildStringFileName(fileEntry, buildStringFilePath);                        
		                needsRebuild = mDepClang.DoesEntryNeedRebuild(fileEntry);
		            }
		            if (needsRebuild)
		            {
		                if (hotProject != null)
		                {
		                    OutputLine("Hot swap detected disallowed C/C++ change: {0}", filePath);                            
		                    return false;
		                }

		                project.mNeedsTargetRebuild = true;                        
		                var runCmd = CompileSource(project, workspaceOptions, options, filePath);
		                runCmd.mParallelGroup = 1;
		            }

					options.mClangObjectFiles.Add(new String(objName));

					if (hotProject != null)
						continue;

		            clangAllObjNames.Add(objName);

		            IdSpan sourceCharIdData;
		            String sourceCode = scope String();
		            FindProjectSourceContent(projectSource, out sourceCharIdData, true, sourceCode);
		            mWorkspace.ProjectSourceCompiled(projectSource, sourceCode, sourceCharIdData);
					sourceCharIdData.Dispose();

					String* fileEntryPtr;
		            if (completedCompileCmd.mClangCompiledFiles.Add(filePath, out fileEntryPtr))
						*fileEntryPtr = new String(filePath);
		        }
		    }
#endif

		    String llvmDir = scope String(IDEApp.sApp.mInstallDir);
		    IDEUtils.FixFilePath(llvmDir);
		    llvmDir.Append("llvm/");
		    
		    if (hotProject != null)
		    {
		        if ((hotProject == project) || (hotProject.HasDependency(project.mProjectName)))
		        {
		            for (var fileName in bfFileNames)
		                hotFileNames.Add(new String(fileName));
		        }
		    
		        return true;
		    }

		    String objectsArg = scope String();
			var argBuilder = scope IDEApp.ArgBuilder(objectsArg, workspaceOptions.mToolsetType != .GNU);
		    for (var bfFileName in bfFileNames)
		    {
				argBuilder.AddFileName(bfFileName);
				argBuilder.AddSep();
		    }

		    for (var objName in clangAllObjNames)
		    {                
		        IDEUtils.AppendWithOptionalQuotes(objectsArg, objName);
		        objectsArg.Append(" ");
		    }

			if (mPlatformType == .Wasm)
			{
				if (!QueueProjectWasmLink(project, targetPath, workspaceOptions, options, objectsArg))
					return false;
			}
			else if (workspaceOptions.mToolsetType == .GNU)
			{
				if ((options.mBuildOptions.mBuildKind == .StaticLib) || (options.mBuildOptions.mBuildKind == .DynamicLib))
				{
					if (!QueueProjectGNUArchive(project, targetPath, workspaceOptions, options, objectsArg))
						return false;
				}
				else if (!QueueProjectGNULink(project, targetPath, workspaceOptions, options, objectsArg))
					return false;
			}
			else // MS
			{
				if (mPlatformType != .Windows)
				{
					gApp.OutputErrorLine("Project '{}' cannot be linked with the Windows Toolset for platform '{}'", project.mProjectName, mPlatformType);
					return false;
				}
				else
				{
					if (options.mBuildOptions.mBuildKind == .StaticLib)
					{
						if (!QueueProjectGNUArchive(project, targetPath, workspaceOptions, options, objectsArg))
							return false;
					}
					else
					{
						if (!QueueProjectMSLink(project, targetPath, configSelection.mConfig, workspaceOptions, options, objectsArg))
							return false;
					}
				}
			}

		    return true;
		}

		public bool QueueProjectPostBuild(Project project, Project hotProject, IDEApp.BuildCompletedCmd completedCompileCmd, List<String> hotFileNames, CompileKind compileKind)
		{
			if (hotProject != null)
				return true;

			Project.Options options = gApp.GetCurProjectOptions(project);
			if (options == null)
			    return true;

			String targetPath = null;
			mTargetPathMap.TryGetValue(project, out targetPath);
			if (targetPath == null)
				return false;

			switch (QueueProjectCustomBuildCommands(project, targetPath, compileKind.WantsRunAfter ? options.mBuildOptions.mBuildCommandsOnRun : options.mBuildOptions.mBuildCommandsOnCompile, options.mBuildOptions.mPostBuildCmds))
			{
			case .NoCommands:
			case .HadCommands:
			case .Failed:
				completedCompileCmd.mFailed = true;
			}

			return true;
		}
	}
}
