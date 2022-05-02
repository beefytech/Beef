using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Diagnostics;
using Beefy.widgets;
using Beefy;
using Beefy.utils;
using IDE.Util;
using IDE.ui;
using IDE.util;
using System.IO;

namespace IDE.Compiler
{
    //public class Bf

    public class BfCompiler : CompilerBase
    {
		enum OptionFlags : int32
		{
			EmitDebugInfo = 1,
			EmitLineInfo = 2,
			WriteIR = 4,
			GenerateOBJ = 8,
			GenerateBitcode = 0x10,
			ClearLocalVars = 0x20,
			RuntimeChecks = 0x40,
			EmitDynamicCastCheck = 0x80,
			EnableObjectDebugFlags = 0x100,
			EmitObjectAccessCheck = 0x200,
			EnableCustodian = 0x400,
			EnableRealtimeLeakCheck = 0x800,
			EnableSideStack = 0x1000,
			EnableHotSwapping = 0x2000,
			IncrementalBuild = 0x4000,
			DebugAlloc = 0x8000,
			OmitDebugHelpers = 0x10000,
			NoFramePointerElim = 0x20000,
			ArithmeticChecks = 0x40000
		}

		public enum UsedOutputFlags : int32
		{
			None = 0,
			FlushQueuedHotFiles = 1,
			SkipImports = 2,
		}

        [CallingConvention(.Stdcall), CLink]
        static extern bool BfCompiler_Compile(void* bfCompiler, void* bfPassInstance, char8* outputDirectory);

		[CallingConvention(.Stdcall), CLink]
		static extern bool BfCompiler_ClearResults(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern bool BfCompiler_VerifyTypeName(void* bfCompiler, char8* typeName, int32 cursorPos);

        [CallingConvention(.Stdcall), CLink]
        static extern bool BfCompiler_ClassifySource(void* bfCompiler, void* bfPassInstance, void* bfResolvePassData);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetCollapseRegions(void* bfCompiler, void* bfParser, void* bfResolvePassData, char8* explicitEmitTypeNames);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* BfCompiler_GetAutocompleteInfo(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetRebuildFileSet(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_FileChanged(void* bfCompiler, char8* str);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* BfCompiler_GetSymbolReferences(void* bfCompiler, void* bfPassInstance, void* bfResolvePassData);        

        [CallingConvention(.Stdcall), CLink]
        static extern void BfCompiler_Cancel(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_RequestFastFinish(void* bfCompiler);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfCompiler_ClearCompletionPercentage(void* bfCompiler);

        [CallingConvention(.Stdcall), CLink]
        static extern float BfCompiler_GetCompletionPercentage(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 BfCompiler_GetCompileRevision(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 BfCompiler_GetCurConstEvalExecuteId(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern bool BfCompiler_GetLastHadComptimeRebuilds(void* bfCompiler);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfCompiler_Delete(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_ClearBuildCache(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_SetBuildValue(void* bfCompiler, char8* cacheDir, char8* key, char8* value);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetBuildValue(void* bfCompiler, char8* cacheDir, char8* key);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_WriteBuildCache(void* bfCompiler, char8* cacheDir);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetUsedOutputFileNames(void* bfCompiler, void* bfProject, UsedOutputFlags flags, out bool hadOutputChanges);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetGeneratorTypeDefList(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetGeneratorInitData(void* bfCompiler, char8* typeDefName, char8* args);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetGeneratorGenData(void* bfCompiler, char8* typeDefName, char8* args);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetTypeDefList(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetTypeDefMatches(void* bfCompiler, char8* searchStr);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetTypeDefInfo(void* bfCompiler, char8* typeDefName);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetTypeInfo(void* bfCompiler, char8* typeName);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetGenericTypeInstances(void* bfCompiler, char8* typeName);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 BfCompiler_GetTypeId(void* bfCompiler, char8* typeName);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfCompiler_SetOptions(void* bfCompiler,
            void* hotProject, int32 hotIdx, char8* targetTriple, char8* targetCPU, int32 toolsetType, int32 simdSetting, int32 allocStackCount, int32 maxWorkerThreads,
            OptionFlags optionsFlags, char8* mallocName, char8* freeName);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_ForceRebuild(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetEmitSource(void* bfCompiler, char8* fileName);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 BfCompiler_GetEmitSourceVersion(void* bfCompiler, char8* fileName);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_GetEmitLocation(void* bfCompiler, char8* typeName, int32 line, out int32 embedLine, out int32 embedLineChar);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_WriteEmitData(void* bfCompiler, char8* filePath, void* bfProject);
		
		public enum HotTypeFlags
		{
			None		= 0,
			UserNotUsed = 1,
			UserUsed	= 2,
			Heap		= 4,
		};

		[CallingConvention(.Stdcall), CLink]
		static extern bool BfCompiler_GetHasHotPendingDataChanges(void* bfCompiler);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotCommit(void* bfCompiler);

		public enum HotResolveFlags
		{
			None = 0,
			HadDataChanges = 1
		}

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotResolve_Start(void* bfCompiler, int32 flags);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotResolve_AddActiveMethod(void* bfCompiler, char8* methodName);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotResolve_AddDelegateMethod(void* bfCompiler, char8* methodName);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotResolve_ReportType(void* bfCompiler, int typeId, int usageKind);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfCompiler_HotResolve_ReportTypeRange(void* bfCompiler, char8* typeName, int usageKind);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfCompiler_HotResolve_Finish(void* bfCompiler);

        class SetPassInstanceCommand : Command
        {
            public BfPassInstance mPassInstance;
        }

		class DeletePassInstanceCommand : Command
		{
		    public BfPassInstance mPassInstance;
		}

        class SetupProjectSettingsCommand : Command
        {
            public Project mProject;
        }

        class DeleteBfProjectCommand : Command
        {
            public BfProject mBfProject;
        }

        class RefreshViewCommand : Command
        {
			public ViewRefreshKind mRefreshKind;

			public this(ViewRefreshKind refreshKind)
			{
				mRefreshKind = refreshKind;
			}
        }

        class SetWorkspaceOptionsCommand : Command
        {
            public BfProject mHotBfProject;
			public int32 mHotIdx;
        }

		class RebuildFileChangedCommand : Command
		{
			public String mDir = new .() ~ delete _;
		}


        public void* mNativeBfCompiler;
        public bool mIsResolveOnly;
		public bool mWantsResolveAllCollapseRefresh;
        public BfSystem mBfSystem;
		bool mWantsRemoveOldData;
		public Dictionary<String, String> mRebuildWatchingFiles = new .() ~ delete _;

        public this(void* nativeBfCompiler)
        {
            mNativeBfCompiler = nativeBfCompiler;
        }

		public ~this()
		{
		    BfCompiler_Delete(mNativeBfCompiler);
		    mNativeBfCompiler = null;

			for (var kv in mRebuildWatchingFiles)
			{
				gApp.mFileWatcher.RemoveWatch(kv.key, kv.value);
				delete kv.key;
				delete kv.value;
			}
		}

        public bool Compile(BfPassInstance passInstance, String outputDirectory)
        {
            bool success = BfCompiler_Compile(mNativeBfCompiler, passInstance.mNativeBfPassInstance, outputDirectory);
            passInstance.mCompileSucceeded = success;
            passInstance.mDidCompile = true;
            return success;
        }

		public void ClearResults()
		{
			BfCompiler_ClearResults(mNativeBfCompiler);
		}

        public bool ClassifySource(BfPassInstance bfPassInstance, BfResolvePassData resolvePassData)
        {
            void* nativeResolvePassData = null;
            if (resolvePassData != null)
                nativeResolvePassData = resolvePassData.mNativeResolvePassData;
            return BfCompiler_ClassifySource(mNativeBfCompiler, bfPassInstance.mNativeBfPassInstance, nativeResolvePassData);
        }

		public void GetCollapseRegions(BfParser parser, BfResolvePassData resolvePassData, String explicitEmitTypeNames, String outData)
		{
			outData.Append(BfCompiler_GetCollapseRegions(mNativeBfCompiler, (parser != null) ? parser.mNativeBfParser : null, resolvePassData.mNativeResolvePassData, explicitEmitTypeNames));
		}

		public bool VerifyTypeName(String typeName, int cursorPos)
		{
			return BfCompiler_VerifyTypeName(mNativeBfCompiler, typeName, (.)cursorPos);
		}

        public void GetAutocompleteInfo(String outAutocompleteInfo)
        {
            char8* result = BfCompiler_GetAutocompleteInfo(mNativeBfCompiler);
			outAutocompleteInfo.Append(StringView(result));
        }

		public void GetRebuildFileSet(String outDirInfo)
		{
		    char8* result = BfCompiler_GetRebuildFileSet(mNativeBfCompiler);
			outDirInfo.Append(StringView(result));
		}

        public void GetSymbolReferences(BfPassInstance passInstance, BfResolvePassData resolvePassData, String outSymbolReferences)
        {
            char8* result = BfCompiler_GetSymbolReferences(mNativeBfCompiler, passInstance.mNativeBfPassInstance, resolvePassData.mNativeResolvePassData);
            outSymbolReferences.Append(StringView(result));
        }

        /*public void UpdateRenameSymbols(BfPassInstance passInstance, BfResolvePassData resolvePassData)
        {
            BfCompiler_UpdateRenameSymbols(mNativeBfCompiler, passInstance.mNativeBfPassInstance, resolvePassData.mNativeResolvePassData);
        }*/

        public void GetOutputFileNames(BfProject project, UsedOutputFlags flags, out bool hadOutputChanges, List<String> outFileNames)
        {
            char8* result = BfCompiler_GetUsedOutputFileNames(mNativeBfCompiler, project.mNativeBfProject, flags, out hadOutputChanges);
            if (result == null)
                return;
			String fileNamesStr = scope String();
			fileNamesStr.Append(result);
            if (fileNamesStr.Length == 0)
                return;
			List<StringView> stringViews = scope List<StringView>(fileNamesStr.Split('\n'));
			for (var strView in stringViews)
				outFileNames.Add(new String(strView));
        }

        public void SetOptions(BfProject hotProject, int32 hotIdx,
            String targetTriple, String targetCPU, int32 toolsetType, int32 simdSetting, int32 allocStackCount, int32 maxWorkerThreads,
			OptionFlags optionFlags, String mallocFuncName, String freeFuncName)
        {
            BfCompiler_SetOptions(mNativeBfCompiler,
                (hotProject != null) ? hotProject.mNativeBfProject : null, hotIdx,
                targetTriple, targetCPU, toolsetType, simdSetting, allocStackCount, maxWorkerThreads, optionFlags, mallocFuncName, freeFuncName);
        }

		public void ForceRebuild()
		{
			BfCompiler_ForceRebuild(mNativeBfCompiler);
		}

		public bool GetEmitSource(StringView fileName, String outText)
		{
			char8* str = BfCompiler_GetEmitSource(mNativeBfCompiler, fileName.ToScopeCStr!());
			if (str == null)
				return false;
			outText.Append(str);
			return true;
		}

		public int32 GetEmitVersion(StringView fileName)
		{
			return BfCompiler_GetEmitSourceVersion(mNativeBfCompiler, fileName.ToScopeCStr!());
		}

		public void GetEmitLocation(StringView typeName, int line, String outFilePath, out int embedLine, out int embedLineChar)
		{
			int32 embedLine32;
			int32 embedLineChar32;
			outFilePath.Append(BfCompiler_GetEmitLocation(mNativeBfCompiler, typeName.ToScopeCStr!(), (.)line, out embedLine32, out embedLineChar32));
			embedLine = embedLine32;
			embedLineChar = embedLineChar32;
		}

        public void QueueSetPassInstance(BfPassInstance passInstance)
        {
            SetPassInstanceCommand command = new SetPassInstanceCommand();
            command.mPassInstance = passInstance;            
            QueueCommand(command);
        }

		public void QueueDeletePassInstance(BfPassInstance passInstance)
		{
		    DeletePassInstanceCommand command = new DeletePassInstanceCommand();
		    command.mPassInstance = passInstance;            
		    QueueCommand(command);
		}

        public void QueueSetupProjectSettings(Project project)
        {
            SetupProjectSettingsCommand command = new SetupProjectSettingsCommand();
            command.mProject = project;
            QueueCommand(command);
        }

        public void QueueDeleteBfProject(BfProject bfProject)
        {
            DeleteBfProjectCommand command = new DeleteBfProjectCommand();
            command.mBfProject = bfProject;
            QueueCommand(command);
        }

        public void QueueRefreshViewCommand(ViewRefreshKind viewRefreshKind = .FullRefresh)
        {
            QueueCommand(new RefreshViewCommand(viewRefreshKind));
        }

        public void QueueSetWorkspaceOptions(Project hotProject, int32 hotIdx)
        {
            BfProject hotBfProject = null;
            if (hotProject != null)
                hotBfProject = mBfSystem.GetBfProject(hotProject);
            var command = new SetWorkspaceOptionsCommand();
            command.mHotBfProject = hotBfProject;
			command.mHotIdx = hotIdx;
            QueueCommand(command);
        }

        protected override void DoProcessQueue()
        {
			BfPassInstance.PassKind passKind = .None;

            BfPassInstance passInstance = null;
            bool didPassInstanceAlloc = false;
			bool wantsRemoveOldData = false;

            mBfSystem.Lock(0);

			//Debug.WriteLine("Starting Beef Thread {0}", Thread.CurrentThread.Id);

            while (true)
            {
                Command command = null;
                using (mMonitor.Enter())
                {
                    if (mCommandQueue.Count == 0)
                        break;
                    command = mCommandQueue[0];
                }

				bool commandProcessed = true;
				defer
				{
					if (commandProcessed)
					{
						using (mMonitor.Enter())
						{
						    delete command;
						    if (!mShuttingDown)
							{
								var poppedCmd = mCommandQueue.PopFront();
								Debug.Assert(poppedCmd == command);
							}
						}
					}
				}

                if (command is SetPassInstanceCommand)
                {
                    var setPassInstanceCommand = (SetPassInstanceCommand)command;
                    passInstance = setPassInstanceCommand.mPassInstance;
                }
				else if (command is DeletePassInstanceCommand)
                {
                    var deletePassInstanceCommand = (DeletePassInstanceCommand)command;
					if (passInstance == deletePassInstanceCommand.mPassInstance)
						passInstance = null;
                    delete deletePassInstanceCommand.mPassInstance;
                }
                else if (passInstance == null)
                {
                    passInstance = mBfSystem.CreatePassInstance("ProcessQueue");
                    didPassInstanceAlloc = true;
                }

                if (command is SetupProjectSettingsCommand)
                {
                    var setupProjectSettingsCommand = (SetupProjectSettingsCommand)command;
					if (setupProjectSettingsCommand.mProject.mDeleted)
						continue;
                    gApp.SetupBeefProjectSettings(mBfSystem, this, setupProjectSettingsCommand.mProject);
                }

                if (command is DeleteBfProjectCommand)
                {
                    var deleteBfProjectCommand = (DeleteBfProjectCommand)command;
                    deleteBfProjectCommand.mBfProject.Dispose();
					delete deleteBfProjectCommand.mBfProject;
                }

                if (command is ProjectSourceCommand)
				ProjectSourceCommandBlock:
                {
                    var projectSourceCommand = (ProjectSourceCommand)command;
					if (projectSourceCommand.mProjectSource.mProject.mDeleted)
						continue;
                    bool worked = true;
                    String sourceFilePath = scope String();
                    var projectSource = projectSourceCommand.mProjectSource;
					if (projectSource.mIncludeKind != .Ignore)
					{
						BfProject bfProject = null;
	                    using (projectSource.mProject.mMonitor.Enter())
	                    {
	                        projectSourceCommand.mProjectSource.GetFullImportPath(sourceFilePath);
							bfProject = mBfSystem.GetBfProject(projectSource.mProject);
	                    }

						bool wantsHash = !mIsResolveOnly;

						bool canMoveSourceString = true;
	                    IdSpan char8IdData = projectSourceCommand.mSourceCharIdData;
	                    String data = projectSourceCommand.mSourceString;
						SourceHash hash = .None;
						if (wantsHash)
							hash = projectSourceCommand.mSourceHash;
	                    if (char8IdData.IsEmpty)
	                    {
	                        data = scope:ProjectSourceCommandBlock String();

	                        if (gApp.LoadTextFile(sourceFilePath, data, true, scope [&] () => { if (wantsHash) hash = SourceHash.Create(.MD5, data); } ) case .Err)
								data = null;
	                        if (data != null)
	                        {
								data.EnsureNullTerminator();
	                            char8IdData = IdSpan.GetDefault((int32)data.Length);
								defer:ProjectSourceCommandBlock char8IdData.Dispose();
								var editData = gApp.GetEditData(projectSource, false);
								using (gApp.mMonitor.Enter())
								{
									editData.GetFileTime();
									editData.SetSavedData(data, char8IdData);
									if (hash case .MD5(let md5Hash))
										editData.mMD5Hash = md5Hash;
								}
								canMoveSourceString = false;
							}                        
	                    }

	                    if (data == null)
	                    {
							String msg = new String();
							msg.AppendF("ERROR: FAILED TO LOAD FILE '{0}' in project '{1}'", sourceFilePath, projectSource.mProject.mProjectName);
	                        mQueuedOutput.Add(msg);
							passInstance.mFailed = true;
							projectSourceCommand.mProjectSource.mLoadFailed = true;
	                    }
						else
							projectSourceCommand.mProjectSource.mLoadFailed = false;
						
						if (mIsResolveOnly)
							projectSourceCommand.mProjectSource.mLoadFailed = data == null;

	                    if ((!mIsResolveOnly) && (data != null))
	                        IDEApp.sApp.mWorkspace.ProjectSourceCompiled(projectSource, data, hash, char8IdData, canMoveSourceString);

	                    var bfParser = mBfSystem.CreateParser(projectSourceCommand.mProjectSource);
						if (data != null)
	                        bfParser.SetSource(data, sourceFilePath);
						else
							bfParser.SetSource("", sourceFilePath);
						bfParser.SetCharIdData(ref char8IdData);

						if (hash case .MD5(let md5Hash))
							bfParser.SetHashMD5(md5Hash);

						worked &= bfParser.Parse(passInstance, false);
						worked &= bfParser.Reduce(passInstance);
	                    worked &= bfParser.BuildDefs(passInstance, null, false);

						passKind = .Parse;

						// Do this to make sure we re-trigger errors in parse/reduce/builddefs
						if (!worked)
							projectSource.HasChangedSinceLastCompile = true;
					}
                }

                if (command is ProjectSourceRemovedCommand)
                {
                    var fileRemovedCommand = (ProjectSourceRemovedCommand)command;
					let projectSource = fileRemovedCommand.mProjectSource;

					using (projectSource.mProject.mMonitor.Enter())
					{
						String sourceFilePath = scope String();
					    projectSource.GetFullImportPath(sourceFilePath);
						gApp.mErrorsPanel.ClearParserErrors(sourceFilePath);
					}

                    var bfParser = mBfSystem.FileRemoved(fileRemovedCommand.mProjectSource);
                    if (bfParser != null)
                    {
                        bfParser.RemoveDefs();
                        delete bfParser;
						mWantsRemoveOldData = true;
                    }
                }

                if (command is CompileCommand)
                {
                    var compileCommand = (CompileCommand)command;
                    Compile(passInstance, compileCommand.mOutputDirectory);
					UpdateRebuildFileWatches();
                    mBfSystem.RemoveOldParsers();
                    mBfSystem.RemoveOldData();
                }

                if (command is ResolveAllCommand)
                {
					if (passKind != .None)
					{
						commandProcessed = false;
						break;
					}

                    var resolvePassData = BfResolvePassData.Create(ResolveType.Classify);
                    // If we get canceled then try again after waiting a couple updates

					bool wantsCollapseRefresh = false;

                    if (ClassifySource(passInstance, resolvePassData))
					{
						if (mWantsResolveAllCollapseRefresh)
						{
							mWantsResolveAllCollapseRefresh = false;
							wantsCollapseRefresh = true;
						}
					}
					else
					{
                        QueueDeferredResolveAll();
					}

					if (resolvePassData.HadEmits)
						wantsCollapseRefresh = true;

					if (wantsCollapseRefresh)
						QueueRefreshViewCommand(.Collapse);

					UpdateRebuildFileWatches();

                    delete resolvePassData;
					wantsRemoveOldData = true;
					passKind = .Classify;

					// End after resolveAll
					break;
                }

                if (command is SetWorkspaceOptionsCommand)
                {
                    var setWorkspaceOptionsCommand = (SetWorkspaceOptionsCommand)command;
                    var workspace = IDEApp.sApp.mWorkspace;
                    using (workspace.mMonitor.Enter())
                    {
						HandleOptions(setWorkspaceOptionsCommand.mHotBfProject, setWorkspaceOptionsCommand.mHotIdx);
                    }
                }

                if (command is RefreshViewCommand)
                {
					var refreshViewCommand = (RefreshViewCommand)command;
                    mWantsActiveViewRefresh = Math.Max(mWantsActiveViewRefresh, refreshViewCommand.mRefreshKind);
                }

				if (var dirChangedCommand = command as RebuildFileChangedCommand)
				{
					BfCompiler_FileChanged(mNativeBfCompiler, dirChangedCommand.mDir);
				}
            }

            mBfSystem.Unlock();

            if (didPassInstanceAlloc)
            {
				if ((passKind != .None) && (mIsResolveOnly))
					gApp.mErrorsPanel.ProcessPassInstance(passInstance, passKind);
				delete passInstance;
			}

			if (wantsRemoveOldData)
			{
				mBfSystem.RemoveOldParsers();
				mBfSystem.RemoveOldData();
			}
        }

		void HandleOptions(BfProject hotBfProject, int32 hotIdx)
		{
			//Debug.WriteLine("HandleOptions");

			var options = IDEApp.sApp.GetCurWorkspaceOptions();
			String targetTriple = scope .();
			if (TargetTriple.IsTargetTriple(gApp.mPlatformName))
				targetTriple.Set(gApp.mPlatformName);
			else
				Workspace.PlatformType.GetTargetTripleByName(gApp.mPlatformName, options.mToolsetType, targetTriple);

			bool enableObjectDebugFlags = options.mEnableObjectDebugFlags;
			bool emitObjectAccessCheck = options.mEmitObjectAccessCheck && enableObjectDebugFlags;

			OptionFlags optionFlags = default;
			void SetOpt(bool val, OptionFlags optionFlag)
			{
				if (val)
					optionFlags |= optionFlag;
				//Debug.WriteLine(" SetOpt {0:X} {1:X}", (int)optionFlags, (int)optionFlag);
			}

			SetOpt(options.mIncrementalBuild, .IncrementalBuild);
			SetOpt(options.mEmitDebugInfo == .Yes, .EmitDebugInfo);
			SetOpt((options.mEmitDebugInfo == .Yes) || (options.mEmitDebugInfo == .LinesOnly), .EmitLineInfo);
			if (gApp.mConfig_NoIR)
			{
				SetOpt(true, .GenerateOBJ);
			}	
			else
			{
				SetOpt((options.mIntermediateType == .IRCode) || (options.mIntermediateType == .ObjectAndIRCode) || (options.mIntermediateType == .BitcodeAndIRCode), .WriteIR);
				SetOpt((options.mIntermediateType == .Object) || (options.mIntermediateType == .ObjectAndIRCode) ||
					(options.mIntermediateType == .Bitcode) || (options.mIntermediateType == .BitcodeAndIRCode), .GenerateOBJ);
				SetOpt((options.mIntermediateType == .Bitcode) || (options.mIntermediateType == .BitcodeAndIRCode), .GenerateBitcode);
			}
			SetOpt(options.mNoOmitFramePointers, .NoFramePointerElim);
			SetOpt(options.mInitLocalVariables, .ClearLocalVars);
			SetOpt(options.mRuntimeChecks, .RuntimeChecks);
			SetOpt(options.mEmitDynamicCastCheck, .EmitDynamicCastCheck);
			SetOpt(enableObjectDebugFlags, .EnableObjectDebugFlags);
			SetOpt(emitObjectAccessCheck, .EmitObjectAccessCheck);
			SetOpt(options.mArithmeticCheck, .ArithmeticChecks);

			if (options.LeakCheckingEnabled)
				SetOpt(options.mEnableRealtimeLeakCheck, .EnableRealtimeLeakCheck);

			SetOpt(options.mEnableSideStack, .EnableSideStack);
#if !CLI
			SetOpt(options.mAllowHotSwapping, .EnableHotSwapping);
#endif

			String mallocLinkName;
			String freeLinkName;
			switch (options.mAllocType)
			{
			case .Debug:
				optionFlags |= .DebugAlloc;
				mallocLinkName = "";
				freeLinkName = "";
			case .CRT:
				mallocLinkName = "malloc";
				freeLinkName = "free";
			case .JEMalloc:
				mallocLinkName = "je_malloc";
				freeLinkName = "je_free";
			case .TCMalloc:
				mallocLinkName = "tcmalloc";
				freeLinkName = "tcfree";
			case .Custom:
				mallocLinkName = options.mAllocMalloc;
				freeLinkName = options.mAllocFree;
			}

			//Debug.WriteLine("HandleOptions SetOptions:{0:X}", (int)optionFlags);
			if (!options.mTargetTriple.IsWhiteSpace)
				targetTriple.Set(options.mTargetTriple);

			SetOptions(hotBfProject, hotIdx,
			    targetTriple, options.mTargetCPU, (int32)options.mToolsetType, (int32)options.mBfSIMDSetting, (int32)options.mAllocStackTraceDepth,
				(int32)gApp.mSettings.mCompilerSettings.mWorkerThreads, optionFlags, mallocLinkName, freeLinkName);

			if (!mIsResolveOnly)
			{
				for (let typeOption in gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions)
					mBfSystem.AddTypeOptions(typeOption);
				for (let typeOption in options.mDistinctBuildOptions)
					mBfSystem.AddTypeOptions(typeOption);
			}
		}

        public override void StartQueueProcessThread()
        {
            // This causes the current view to do a full refresh every keystroke.
            //  I think it's not needed...
            //mWantsActiveViewRefresh = true;
            base.StartQueueProcessThread();
        }

        public override void RequestCancelBackground()
        {
            if (mThreadWorker.mThreadRunning)
            {
				if ((mNativeBfCompiler != null) && (!gApp.mDeterministic))
                	BfCompiler_Cancel(mNativeBfCompiler);
            }
        }

		public override void RequestFastFinish(bool force = false)
		{
		    if (mThreadWorker.mThreadRunning || mThreadWorkerHi.mThreadRunning)
		    {
				if ((!force) &&
					((!mThreadWorker.mAllowFastFinish) || (!mThreadWorkerHi.mAllowFastFinish)))
					return;

				if ((mNativeBfCompiler != null) && (!gApp.mDeterministic))
		        	BfCompiler_RequestFastFinish(mNativeBfCompiler);
		    }
		}

        public void ClearCompletionPercentage()
        {
            BfCompiler_ClearCompletionPercentage(mNativeBfCompiler);
        }

        public float GetCompletionPercentage()
        {
            return BfCompiler_GetCompletionPercentage(mNativeBfCompiler);
        }

		public int32 GetCompileRevision()
		{
			return BfCompiler_GetCompileRevision(mNativeBfCompiler);
		}

		public int32 GetCurConstEvalExecuteId()
		{
			return BfCompiler_GetCurConstEvalExecuteId(mNativeBfCompiler);
		}

		public void GetGeneratorTypeDefList(String outStr)
		{
			outStr.Append(BfCompiler_GetGeneratorTypeDefList(mNativeBfCompiler));
		}

		public void GetGeneratorInitData(String typeDefName, String args, String outStr)
		{
			outStr.Append(BfCompiler_GetGeneratorInitData(mNativeBfCompiler, typeDefName, args));
		}

		public void GetGeneratorGenData(String typeDefName, String args, String outStr)
		{
			outStr.Append(BfCompiler_GetGeneratorGenData(mNativeBfCompiler, typeDefName, args));
		}

		public void GetTypeDefList(String outStr)
		{
			outStr.Append(BfCompiler_GetTypeDefList(mNativeBfCompiler));
		}

		public void GetTypeDefMatches(String searchStr, String outStr)
		{
			outStr.Append(BfCompiler_GetTypeDefMatches(mNativeBfCompiler, searchStr));
		}

		public void GetTypeDefInfo(String typeDefName, String outStr)
		{
			outStr.Append(BfCompiler_GetTypeDefInfo(mNativeBfCompiler, typeDefName));
		}

		public void GetGenericTypeInstances(String typeName, String outStr)
		{
			outStr.Append(BfCompiler_GetGenericTypeInstances(mNativeBfCompiler, typeName));
		}

		public void GetTypeInfo(String typeDefName, String outStr)
		{
			outStr.Append(BfCompiler_GetTypeInfo(mNativeBfCompiler, typeDefName));
		}

		public int GetTypeId(String typeName)
		{
			return BfCompiler_GetTypeId(mNativeBfCompiler, typeName);
		}

		public void ClearBuildCache()
		{
			BfCompiler_ClearBuildCache(mNativeBfCompiler);
		}

		public void SetBuildValue(String cacheDir, String key, String value)
		{
			BfCompiler_SetBuildValue(mNativeBfCompiler, cacheDir, key, value);
		}

		public void GetBuildValue(String cacheDir, String key, String outValue)
		{
			char8* cStr = BfCompiler_GetBuildValue(mNativeBfCompiler, cacheDir, key);
			outValue.Append(cStr);
		}

		public void WriteBuildCache(String cacheDir)
		{
			BfCompiler_WriteBuildCache(mNativeBfCompiler, cacheDir);
		}

		public bool GetHasHotPendingDataChanges()
		{
			return BfCompiler_GetHasHotPendingDataChanges(mNativeBfCompiler);
		}

		public void HotCommit()
		{
			BfCompiler_HotCommit(mNativeBfCompiler);
		}

		public void HotResolve_Start(HotResolveFlags flags)
		{
			BfCompiler_HotResolve_Start(mNativeBfCompiler, (.)flags);
		}

		public void HotResolve_AddActiveMethod(char8* methodName)
		{
			BfCompiler_HotResolve_AddActiveMethod(mNativeBfCompiler, methodName);
		}

		public void HotResolve_AddDelegateMethod(char8* methodName)
		{
			BfCompiler_HotResolve_AddDelegateMethod(mNativeBfCompiler, methodName);
		}

		public void HotResolve_ReportType(int typeId, HotTypeFlags flags)
		{
			BfCompiler_HotResolve_ReportType(mNativeBfCompiler, typeId, (int32)flags);
		}

		public void HotResolve_ReportTypeRange(char8* typeName, HotTypeFlags flags)
		{
			BfCompiler_HotResolve_ReportTypeRange(mNativeBfCompiler, typeName, (int32)flags);
		}

		public void HotResolve_Finish(String result)
		{
			char8* resultCStr = BfCompiler_HotResolve_Finish(mNativeBfCompiler);
			result.Append(resultCStr);
		}

		public override void Update()
		{
			base.Update();

			if (!ThreadRunning)
			{
                if (mWantsRemoveOldData)
				{
					mBfSystem.RemoveOldParsers();
					mBfSystem.RemoveOldData();
					mWantsRemoveOldData = false;
				}
			}
		}

		public bool GetLastHadComptimeRebuilds()
		{
			return BfCompiler_GetLastHadComptimeRebuilds(mNativeBfCompiler);
		}

		void UpdateRebuildFileWatches()
		{
			HashSet<StringView> curWatches = scope .();

			var rebuildDirStr = GetRebuildFileSet(.. scope .());
			for (var dir in rebuildDirStr.Split('\n', .RemoveEmptyEntries))
			{
				curWatches.Add(dir);
				if (mRebuildWatchingFiles.TryAddAlt(dir, var keyPtr, var valuePtr))
				{
					*keyPtr = new String(dir);
					String watchFile = *valuePtr = new .();
					watchFile.Append(dir);
					if ((watchFile.EndsWith(Path.DirectorySeparatorChar)) || (watchFile.EndsWith(Path.AltDirectorySeparatorChar)))
						watchFile.Append("*");
					gApp.mFileWatcher.WatchFile(watchFile, watchFile);
				}
			}

			List<String> oldKeys = scope .();
			for (var kv in mRebuildWatchingFiles)
			{
				if (!curWatches.Contains(kv.key))
				{
					gApp.mFileWatcher.RemoveWatch(kv.key, kv.value);
					oldKeys.Add(kv.key);
				}
			}

			for (var key in oldKeys)
			{
				var kv = mRebuildWatchingFiles.GetAndRemove(key).Value;
				delete kv.key;
				delete kv.value;
			}
		}

		public bool HasRebuildFileWatches()
		{
			return !mRebuildWatchingFiles.IsEmpty;
		}

		public void AddChangedDirectory(StringView str)
		{
			var dirChangedCommand = new RebuildFileChangedCommand();
			dirChangedCommand.mDir.Set(str);
			QueueCommand(dirChangedCommand);
		}

		public void WriteEmitData(String filePath, BfProject bfProject)
		{
			BfCompiler_WriteEmitData(mNativeBfCompiler, filePath, bfProject.mNativeBfProject);
		}
    }
}
