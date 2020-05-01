using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.utils;
using System.Diagnostics;
using System.Threading;
using IDE.Util;
using IDE.util;

namespace IDE
{
	[Reflect(.StaticFields | .NonStaticFields | .ApplyToInnerTypes)]
    public class Workspace
    {
        public enum IntermediateType
        {
            Object,
            IRCode,
            ObjectAndIRCode,
			Bitcode,
			BitcodeAndIRCode,
        }

		public enum PlatformType
		{
			case Unknown;
			case Windows;
			case Linux;
			case macOS;
			case iOS;
			case Android;

			public static PlatformType GetFromName(String name)
			{
				switch (name)
				{
				case "Win32", "Win64": return .Windows;
				case "Linux32", "Linux64": return .Linux;
				case "macOS": return .macOS;
				case "iOS": return .iOS;
				default:
					return TargetTriple.GetPlatformType(name);
				}
			}

			public static PlatformType GetHostPlatform()
			{
#if BF_PLATFORM_WINDOWS
				return .Windows;
#endif

#if BF_PLATFORM_LINUX
				return .Linux;
#endif

#if BF_PLATFORM_MACOS
				return .Linux;
#endif

#unwarn
				return .Unknown;
			}

			public static int GetPtrSizeByName(String name)
			{
				if ((name.EndsWith("32")) && (!TargetTriple.IsTargetTriple(name)))
					return 4;
				if (name.StartsWith("armv"))
					return 4;
				if (name.StartsWith("i686-"))
					return 4;
				return 8;
			}

			public static bool GetTargetTripleByName(String name, ToolsetType toolsetType, String outTriple)
			{
				switch (name)
				{
				case "Win32":
					outTriple.Append((toolsetType == .GNU) ? "i686-pc-windows-gnu" : "i686-pc-windows-msvc");
				case "Win64":
					outTriple.Append((toolsetType == .GNU) ? "x86_64-pc-windows-gnu" : "x86_64-pc-windows-msvc");
				case "Linux32":
					outTriple.Append("i686-unknown-linux-gnu");
				case "Linux64":
					outTriple.Append("x86_64-unknown-linux-gnu");
				case "macOS":
					outTriple.Append("x86_64-apple-macosx10.8.0");
				case "iOS":
					outTriple.Append("arm64-apple-ios");
				default:
					return false;
				}
				return true;
			}
		}

		public enum ToolsetType
		{
			case GNU;
			case Microsoft;
			case LLVM;

			public static ToolsetType Default
			{
				get
				{
#if BF_PLATFORM_WINDOWS
					return Microsoft;
#else
					return .GNU;
#endif
				}
			}
		}

		public enum BuildKind
		{
			case Normal;
			case Test;
		}

        public enum COptimizationLevel
        {
            O0,
            O1,
            O2,
            O3,
            Ofast,
            Og,            
        }

        public class ConfigSelection : IHashable, IEquatable
        {
            public bool mEnabled = true;
            public String mConfig ~ delete _;
            public String mPlatform ~ delete _;

            public ConfigSelection Duplicate()
            {
                ConfigSelection cs = new ConfigSelection();
                cs.mEnabled = mEnabled;
                cs.mConfig = new String(mConfig);
                cs.mPlatform = new String(mPlatform);
                return cs;
            }

            public bool Equals(Object value)
            {
                ConfigSelection other = value as ConfigSelection;
                if (other == null)
                    return false;
                return (mEnabled == other.mEnabled) &&
                    (mConfig == other.mConfig) &&
                    (mPlatform == other.mPlatform);                  
            }

            public int GetHashCode()
            {
                return mConfig.GetHashCode();
            }
        }

		public enum AllocType
		{
			Debug,
			CRT,
			JEMalloc,
			TCMalloc,
			Custom
		}

		public class BeefGlobalOptions
		{
			[Reflect]
			public List<String> mPreprocessorMacros = new List<String>() ~ DeleteContainerAndItems!(_);
			/*[Reflect]
			public String mTargetTriple = new .() ~ delete _;*/
			[Reflect]
			public List<DistinctBuildOptions> mDistinctBuildOptions = new List<DistinctBuildOptions>() ~ DeleteContainerAndItems!(_);
		}

        public class Options
        {
			[Reflect]
			public ToolsetType mToolsetType;
			[Reflect]
			public BuildKind mBuildKind;

			[Reflect]
			public bool mIncrementalBuild = true;
			[Reflect]
			public IntermediateType mIntermediateType;
			[Reflect]
			public BuildOptions.SIMDSetting mBfSIMDSetting = .SSE2;
			[Reflect]
			public BuildOptions.BfOptimizationLevel mBfOptimizationLevel;
			[Reflect]
			public BuildOptions.SIMDSetting mCSIMDSetting = .SSE2;
			[Reflect]
			public COptimizationLevel mCOptimizationLevel;
			[Reflect]
			public BuildOptions.LTOType mLTOType;
			[Reflect]
			public bool mNoOmitFramePointers;
			[Reflect]
			public bool mLargeStrings;
			[Reflect]
			public bool mLargeCollections;
			[Reflect]
			public AllocType mAllocType = .CRT;
			[Reflect]
			public String mAllocMalloc = new String() ~ delete _;
			[Reflect]
			public String mAllocFree = new String() ~ delete _;

			[Reflect]
            public BuildOptions.EmitDebugInfo mEmitDebugInfo;
			[Reflect]
            public bool mInitLocalVariables;
            //public bool mAllowStructByVal;
			[Reflect]
            public bool mRuntimeChecks;
			[Reflect]
            public bool mEmitDynamicCastCheck;
			[Reflect]
            public bool mEnableObjectDebugFlags;
			[Reflect]
            public bool mEmitObjectAccessCheck; // Only valid with mObjectHasDebugFlags
			[Reflect]
            public bool mEnableRealtimeLeakCheck;
			[Reflect]
            public bool mEnableSideStack;
			[Reflect]
			public bool mAllowHotSwapping;
			[Reflect]
			public int32 mAllocStackTraceDepth;
			[Reflect]
			public List<String> mPreprocessorMacros = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<DistinctBuildOptions> mDistinctBuildOptions = new List<DistinctBuildOptions>() ~ DeleteContainerAndItems!(_);

            public Dictionary<Project, ConfigSelection> mConfigSelections = new Dictionary<Project, ConfigSelection>() ~ delete _;

			public ~this()
			{
				for (var configSel in mConfigSelections.Values)
					delete configSel;
			}

			public bool IsTestProject(Project project)
			{
				return ((mBuildKind == .Test) &&
					((project.mGeneralOptions.mTargetType == .BeefConsoleApplication) ||
					 (project.mGeneralOptions.mTargetType == .BeefGUIApplication) ||
					 (project.mGeneralOptions.mTargetType == .BeefConsoleApplication)));
			}
			
			public void CopyFrom(Workspace.Options prev)
			{
				mToolsetType = prev.mToolsetType;
				mBuildKind = prev.mBuildKind;

				mIncrementalBuild = prev.mIncrementalBuild;
				mIntermediateType = prev.mIntermediateType;
				mBfSIMDSetting = prev.mBfSIMDSetting;
				mBfOptimizationLevel = prev.mBfOptimizationLevel;
				mCSIMDSetting = prev.mCSIMDSetting;
				mCOptimizationLevel = prev.mCOptimizationLevel;
				mLTOType = prev.mLTOType;
				mNoOmitFramePointers = prev.mNoOmitFramePointers;
				mLargeStrings = prev.mLargeStrings;
				mLargeCollections = prev.mLargeCollections;
				mAllocType = prev.mAllocType;
				mAllocMalloc.Set(prev.mAllocMalloc);
				mAllocFree.Set(prev.mAllocFree);

				mEmitDebugInfo = prev.mEmitDebugInfo;
				mInitLocalVariables = prev.mInitLocalVariables;
				mRuntimeChecks = prev.mRuntimeChecks;
				mEmitDynamicCastCheck = prev.mEmitDynamicCastCheck;
				mEnableObjectDebugFlags = prev.mEnableObjectDebugFlags;
				mEmitObjectAccessCheck = prev.mEmitObjectAccessCheck;
				mEnableRealtimeLeakCheck = prev.mEnableRealtimeLeakCheck;
				mEnableSideStack = prev.mEnableSideStack;
				mAllowHotSwapping = prev.mAllowHotSwapping;
				mAllocStackTraceDepth = prev.mAllocStackTraceDepth;

				for (var preProc in prev.mPreprocessorMacros)
					mPreprocessorMacros.Add(new String(preProc));

				for (var typeOptionKV in prev.mConfigSelections)
				{
					let prevConfig = typeOptionKV.value;
					let newConfig = prevConfig.Duplicate();
					mConfigSelections[typeOptionKV.key] = newConfig;
				}

				for (var typeOption in prev.mDistinctBuildOptions)
				{
					var newTypeOption = typeOption.Duplicate();
					mDistinctBuildOptions.Add(newTypeOption);
				}
			}
        }

        public class Config
        {
            public Dictionary<String, Options> mPlatforms = new Dictionary<String, Options>() ~ DeleteDictionaryAndKeysAndItems!(_);
        }

		public BeefGlobalOptions mBeefGlobalOptions = new BeefGlobalOptions() ~ delete _;
        public Dictionary<String, Config> mConfigs = new Dictionary<String, Config>() ~ DeleteDictionaryAndKeysAndItems!(_);

        public class ProjectSourceCompileInstance
        {
            public String mSource ~ delete _;
			public SourceHash mSourceHash;
            public IdSpan mSourceCharIdData ~ _.Dispose();
			public int32 mRefCount = 1;

			public this()
			{
			}

			public void Deref()
			{
				if (--mRefCount == 0)
					delete this;
			}

			public ~this()
			{
			}
        }

        public class CompileInstance
        {
			public enum CompileResult
			{
				Compiling,
				Success,
				PendingHotLoad,
				Failure,
			}

            public Dictionary<ProjectItem, ProjectSourceCompileInstance> mProjectItemCompileInstances = new Dictionary<ProjectItem, ProjectSourceCompileInstance>() ~ delete _;
            public CompileResult mCompileResult = .Compiling;
			public bool mIsReused;

			public ~this()
			{
				for (var compileInstance in mProjectItemCompileInstances.Values)
					if (compileInstance != null)
						compileInstance.Deref();
			}
        }

		public class ProjectSpec
		{
			public String mProjectName ~ delete _;
			public VerSpecRecord mVerSpec ~ delete _;
		}

		public Monitor mMonitor = new Monitor() ~ delete _;
        public String mName ~ delete _;
        public String mDir ~ delete _;
		public CompositeFile mCompositeFile ~ delete _;
        public List<Project> mProjects = new List<Project>() ~ DeleteContainerAndItems!(_);
		public List<ProjectSpec> mProjectSpecs = new .() ~ DeleteContainerAndItems!(_);
		public Dictionary<String, Project> mProjectNameMap = new .() ~ DeleteDictionaryAndKeys!(_);
        public Project mStartupProject;
		public bool mNeedsCreate;
        public bool mHasChanged;
		public bool mHadHotCompileSinceLastFullCompile;
		public bool mForceNextCompile;
        public List<CompileInstance> mCompileInstanceList = new List<CompileInstance>() ~ DeleteContainerAndItems!(_); // First item is primary compile, secondaries are hot reloads
		public List<String> mPlatforms = new List<String>() ~ DeleteContainerAndItems!(_);
		public List<String> mUserPlatforms = new List<String>() ~ DeleteContainerAndItems!(_);
		public bool mIsDebugSession;

		public int32 HotCompileIdx
		{
			get
			{
				return (.)mCompileInstanceList.Count - 1;
			}
		}

		public bool IsSingleFileWorkspace
		{
			get
			{
				return mCompositeFile != null;
			}
		}

		public bool IsDebugSession
		{
			get
			{
				return mIsDebugSession;
			}
		}

        public this()
        {
			
        }

		public bool IsInitialized
		{
			get
			{
				return (mName != null) || (IsDebugSession);
			}
		}

        public void SetChanged()
        {
            mHasChanged = true;
        }

		public void MarkPlatformNamesDirty()
		{
			ClearAndDeleteItems(mPlatforms);
		}

		public void GetPlatformList(List<String> outList)
		{
			if (mPlatforms.IsEmpty)
			{
				HashSet<String> platformSet = scope .();

				void Add(String str)
				{
					if (platformSet.Add(str))
						mPlatforms.Add(new String(str));
				}

				Add(IDEApp.sPlatform32Name);
				Add(IDEApp.sPlatform64Name);

				for (let config in mConfigs.Values)
				{
					for (let platform in config.mPlatforms.Keys)
						Add(platform);
				}

				/*for (let project in mProjects)
				{
					for (let config in project.mConfigs.Values)
					{
						for (let platform in config.mPlatforms.Keys)
							Add(platform);
					}
				}*/

				mPlatforms.Sort(scope (a, b) => String.Compare(a, b, true));
			}

			for (let str in mPlatforms)
				outList.Add(str);
		}


        public Options GetOptions(String configName, String platformName)
        {
            Config config;
            mConfigs.TryGetValue(configName, out config);
            if (config == null)
                return null;
            Options options;
            config.mPlatforms.TryGetValue(platformName, out options);
            return options;
        }

        public void Serialize(StructuredData data)
        {
			void WriteStrings(String name, List<String> strs)
			{
				if (!strs.IsEmpty)
				{
				    using (data.CreateArray(name))
				    {
				        for (var str in strs)
				            data.Add(str);
				    }
				}
			}

			void WriteDistinctOptions(List<DistinctBuildOptions> distinctBuildOptions)
			{
				if (distinctBuildOptions.IsEmpty)
					return;
				using (data.CreateArray("DistinctOptions"))
				{
					for (let typeOptions in distinctBuildOptions)
					{
						// This '.Deleted' can only happen if we're editing the properties but haven't committed yet
						if (typeOptions.mCreateState == .Deleted)
							continue;
						using (data.CreateObject())
							typeOptions.Serialize(data);
					}
					data.RemoveIfEmpty();
				}
			}

			if (!IsSingleFileWorkspace)
				data.Add("FileVersion", 1);
			
			using (data.CreateObject("Workspace"))
			{
				if (mStartupProject != null)
					data.Add("StartupProject", mStartupProject.mProjectName);
				WriteStrings("PreprocessorMacros", mBeefGlobalOptions.mPreprocessorMacros);
				//data.ConditionalAdd("TargetTriple", mBeefGlobalOptions.mTargetTriple, "");
				WriteDistinctOptions(mBeefGlobalOptions.mDistinctBuildOptions);
				data.RemoveIfEmpty();
			}

			if (!mProjectSpecs.IsEmpty)
			{
				using (data.CreateObject("Projects", true))
				{
					for (var projSpec in mProjectSpecs)
					{
						projSpec.mVerSpec.Serialize(projSpec.mProjectName, data);
					}
				}
			}

			//
			{
				List<Project> unlockedProject = scope .();
				List<Project> lockedProject = scope .();
				for (var project in mProjects)
				{
					if (project.mLocked != project.mLockedDefault)
					{
						if (project.mLocked)
							lockedProject.Add(project);
						else
							unlockedProject.Add(project);
					}
				}

				if (!lockedProject.IsEmpty)
				{
					using (data.CreateArray("Locked"))
					{
						for (let project in lockedProject)
							data.Add(project.mProjectName);
					}
				}

				if (!unlockedProject.IsEmpty)
				{
					using (data.CreateArray("Unlocked"))
					{
						for (let project in unlockedProject)
							data.Add(project.mProjectName);
					}
				}
			}

            using (data.CreateObject("Configs"))
            {
                for (var configKeyValue in mConfigs)
                {
					var configName = configKeyValue.key;
					//bool isRelease = configName.Contains("Release");
#unwarn
					bool isDebug = configName.Contains("Debug");
					bool isRelease = configName.Contains("Release");
					bool isParanoid = configName.Contains("Paranoid");
					bool isTest = configName.Contains("Test");

                    var config = configKeyValue.value;
                    using (data.CreateObject(configName))
                    {
                        for (var platformKeyValue in config.mPlatforms)
                        {
                            var options = platformKeyValue.value;
							var platformName = platformKeyValue.key;

							let platformType = PlatformType.GetFromName(platformName);

                            using (data.CreateObject(platformName))
                            {
								using (data.CreateArray("PreprocessorMacros"))
								{
								    for (var macro in options.mPreprocessorMacros)
								        data.Add(macro);
									data.RemoveIfEmpty();
								}

								data.ConditionalAdd("Toolset", options.mToolsetType, ToolsetType.Default);
								data.ConditionalAdd("BuildKind", options.mBuildKind, isTest ? .Test : .Normal);
                                data.ConditionalAdd("BfSIMDSetting", options.mBfSIMDSetting, .SSE2);
								if (platformType == .Windows)
                                	data.ConditionalAdd("BfOptimizationLevel", options.mBfOptimizationLevel, isRelease ? .O2 : (platformName == "Win64") ? .OgPlus : .O0);
								else
									data.ConditionalAdd("BfOptimizationLevel", options.mBfOptimizationLevel, isRelease ? .O2 : .O0);
								data.ConditionalAdd("LTOType", options.mLTOType, .None);
								data.ConditionalAdd("AllocType", options.mAllocType, isRelease ? .CRT : .Debug);
								data.ConditionalAdd("AllocMalloc", options.mAllocMalloc, "");
								data.ConditionalAdd("AllocFree", options.mAllocFree, "");

                                data.ConditionalAdd("EmitDebugInfo", options.mEmitDebugInfo, .Yes);
                                data.ConditionalAdd("NoOmitFramePointers", options.mNoOmitFramePointers, false);
								data.ConditionalAdd("LargeStrings", options.mLargeStrings, false);
								data.ConditionalAdd("LargeCollections", options.mLargeCollections, false);
                                data.ConditionalAdd("InitLocalVariables", options.mInitLocalVariables, false);
                                data.ConditionalAdd("RuntimeChecks", options.mRuntimeChecks, !isRelease);
                                data.ConditionalAdd("EmitDynamicCastCheck", options.mEmitDynamicCastCheck, !isRelease);
                                data.ConditionalAdd("EnableObjectDebugFlags", options.mEnableObjectDebugFlags, !isRelease);
                                data.ConditionalAdd("EmitObjectAccessCheck", options.mEmitObjectAccessCheck, !isRelease);
                                data.ConditionalAdd("EnableRealtimeLeakCheck", options.mEnableRealtimeLeakCheck, !isRelease);
                                data.ConditionalAdd("EnableSideStack", options.mEnableSideStack, isParanoid);
								data.ConditionalAdd("AllowHotSwapping", options.mAllowHotSwapping, !isRelease);
								data.ConditionalAdd("AllocStackTraceDepth", options.mAllocStackTraceDepth, 1);

								data.ConditionalAdd("IncrementalBuild", options.mIncrementalBuild, !isRelease);
                                data.ConditionalAdd("IntermediateType", options.mIntermediateType, .Object);

                                data.ConditionalAdd("CSIMDSetting", options.mCSIMDSetting, .SSE2);
                                data.ConditionalAdd("COptimizationLevel", options.mCOptimizationLevel, isRelease ? .O2 : .O0);

                                using (data.CreateObject("ConfigSelections", true))
                                {
                                    for (var configPair in options.mConfigSelections)
                                    {
										let projectName = configPair.key.mProjectName;
                                        using (data.CreateObject(projectName))
                                        {
											String expectConfig = configName;
											if (isTest)
											{
												if (projectName != mProjects[0].mProjectName)
													expectConfig = "Debug";
											}

                                            var configSelection = configPair.value;
                                            data.ConditionalAdd("Enabled", configSelection.mEnabled, true);
                                            data.ConditionalAdd("Config", configSelection.mConfig, expectConfig);
                                            data.ConditionalAdd("Platform", configSelection.mPlatform, platformName);
											data.RemoveIfEmpty();
                                        }
                                    }
									data.RemoveIfEmpty();
                                }

								WriteDistinctOptions(options.mDistinctBuildOptions);
                            }
                        }
                    }
                }
            }
        }

		public void ClearProjectNameCache()
		{
			for (var key in mProjectNameMap.Keys)
				delete key;
			mProjectNameMap.Clear();
		}

		public void AddProjectToCache(Project project)
		{
			void Add(String name, Project project)
			{
				bool added = mProjectNameMap.TryAdd(name, var keyPtr, var valuePtr);
				if (!added)
					return;
				*keyPtr = new String(name);
				*valuePtr = project;
			}

			Add(project.mProjectName, project);

			for (var alias in project.mGeneralOptions.mAliases)
				Add(alias, project);
		}

        public Project FindProject(StringView projectName)
        {
			if (mProjectNameMap.IsEmpty)
			{
				void Add(String name, Project project)
				{
					bool added = mProjectNameMap.TryAdd(name, var keyPtr, var valuePtr);
					if (!added)
						return;
					*keyPtr = new String(name);
					*valuePtr = project;
				}

				for (var project in mProjects)
					Add(project.mProjectName, project);
				
				for (var project in mProjects)
				{
					for (var alias in project.mGeneralOptions.mAliases)
						Add(alias, project);
				}
			}

            if (mProjectNameMap.TryGetWith(projectName, var matchKey, var value))
			{
				return value;
			}
			return null;
        }

		public void SetupDefault(Options options, String configName, String platformName)
		{
#unwarn
			//bool isDebug = configName.Contains("Debug");
#unwarn
			bool isRelease = configName.Contains("Release");
#unwarn
			bool isParanoid = configName.Contains("Paranoid");
			bool isTest = configName.Contains("Test");
			let platformType = PlatformType.GetFromName(platformName);

			/*if (TargetTriple.IsTargetTriple(platformName))
			{
				options.mToolsetType = .None;
			}*/

			options.mBfOptimizationLevel = isRelease ? .O2 : .O0;
			options.mBfSIMDSetting = .SSE2;
			if (platformType == .Windows)
			{
				options.mBfOptimizationLevel = isRelease ? .O2 : (platformName == "Win64") ? .OgPlus : .O0;
				options.mToolsetType = .Microsoft;
			}
			else if ((platformType == .macOS) == (platformType == .Linux))
			{
				options.mToolsetType = .GNU;
			}

			options.mAllocType = isRelease ? .CRT : .Debug;
			options.mEmitDebugInfo = .Yes;
			options.mNoOmitFramePointers = false;
			options.mLargeStrings = false;
			options.mLargeCollections = false;
			options.mInitLocalVariables = false;
			options.mRuntimeChecks = !isRelease;
			options.mEmitDynamicCastCheck = !isRelease;
			options.mEnableObjectDebugFlags = !isRelease;
			options.mEmitObjectAccessCheck = !isRelease;

			if (platformType == .Windows)
			{
				options.mEnableRealtimeLeakCheck = !isRelease;
				options.mEnableSideStack = isParanoid;
			}
			else
			{
	            options.mEnableRealtimeLeakCheck = false;
	            options.mEnableSideStack = false;
			}
			options.mAllowHotSwapping = !isRelease;
			options.mIncrementalBuild = !isRelease;

            options.mAllocStackTraceDepth = 1;
			options.mIntermediateType = (platformType == .iOS) ? .Bitcode : .Object;
			options.mCSIMDSetting = .SSE2;
			options.mCOptimizationLevel = isRelease ? .O2 : .O0;

			options.mBuildKind = isTest ? .Test : .Normal;

			//TODO:
			//options.mIntermediateType = .ObjectAndIRCode;
		}

        public void Deserialize(StructuredData data)
        {
			DeleteDictionaryAndKeysAndItems!(mConfigs);
			mConfigs = new Dictionary<String, Config>();

			using (data.Open("Workspace"))
			{
				for (data.Enumerate("PreprocessorMacros"))
				{
					var str = new String();
					data.GetCurString(str);
				    mBeefGlobalOptions.mPreprocessorMacros.Add(str);
				}
				//data.GetString("TargetTriple", mBeefGlobalOptions.mTargetTriple);

				for (data.Enumerate("DistinctOptions"))
				{
					var typeOptions = new DistinctBuildOptions();
					typeOptions.Deserialize(data);
					mBeefGlobalOptions.mDistinctBuildOptions.Add(typeOptions);
				}
			}

            for (var configNameKey in data.Enumerate("Configs"))
            {
                Config config = new Config();
				//let configName = new String(data.Keys[configIdx]);
				let configName = new String(configNameKey);
				bool isRelease = configName.Contains("Release");
				bool isParanoid = configName.Contains("Paranoid");
				bool isTest = configName.Contains("Test");
				//bool isDebug = configName.Contains("Debug");
                mConfigs[configName] = config;
                
				for (var platformNameKey in data.Enumerate())
                {
                    Options options = new Options();
					let platformName = new String(platformNameKey);
					let platformType = PlatformType.GetFromName(platformName);
                    config.mPlatforms[platformName] = options;
                    
					SetupDefault(options, configName, platformName);

					for (data.Enumerate("PreprocessorMacros"))
					{
						var str = new String();
						data.GetCurString(str);
				        options.mPreprocessorMacros.Add(str);
					}

					options.mToolsetType = data.GetEnum<ToolsetType>("Toolset", ToolsetType.Default);
					options.mBuildKind = data.GetEnum<BuildKind>("BuildKind", isTest ? .Test : .Normal);
					options.mBfSIMDSetting = data.GetEnum<BuildOptions.SIMDSetting>("BfSIMDSetting", .SSE2);
					if (platformType == .Windows)
                    	options.mBfOptimizationLevel = data.GetEnum<BuildOptions.BfOptimizationLevel>("BfOptimizationLevel", isRelease ? .O2 : (platformName == "Win64") ? .OgPlus : .O0);
					else
						options.mBfOptimizationLevel = data.GetEnum<BuildOptions.BfOptimizationLevel>("BfOptimizationLevel", isRelease ? .O2 : .O0);

					options.mLTOType = data.GetEnum<BuildOptions.LTOType>("LTOType", .None);
					options.mAllocType = data.GetEnum<AllocType>("AllocType", isRelease ? .CRT : .Debug);
					data.GetString("AllocMalloc", options.mAllocMalloc);
					data.GetString("AllocFree", options.mAllocFree);

                    options.mEmitDebugInfo = data.GetEnum<BuildOptions.EmitDebugInfo>("EmitDebugInfo", .Yes);
                    options.mNoOmitFramePointers = data.GetBool("NoOmitFramePointers", false);
					options.mLargeStrings = data.GetBool("LargeStrings", false);
					options.mLargeCollections = data.GetBool("LargeCollections", false);
					options.mInitLocalVariables = data.GetBool("InitLocalVariables", false);
                    options.mRuntimeChecks = data.GetBool("RuntimeChecks", !isRelease);
                    options.mEmitDynamicCastCheck = data.GetBool("EmitDynamicCastCheck", !isRelease);
                    options.mEnableObjectDebugFlags = data.GetBool("EnableObjectDebugFlags", !isRelease);
                    options.mEmitObjectAccessCheck = data.GetBool("EmitObjectAccessCheck", !isRelease);
                    options.mEnableRealtimeLeakCheck = data.GetBool("EnableRealtimeLeakCheck", !isRelease);
                    options.mEnableSideStack = data.GetBool("EnableSideStack", isParanoid);
					options.mAllowHotSwapping = data.GetBool("AllowHotSwapping", !isRelease);
					options.mAllocStackTraceDepth = data.GetInt("AllocStackTraceDepth", 1);

					options.mIncrementalBuild = data.GetBool("IncrementalBuild", !isRelease);
                    options.mIntermediateType = data.GetEnum<IntermediateType>("IntermediateType", .Object);

                    options.mCSIMDSetting = data.GetEnum<BuildOptions.SIMDSetting>("CSIMDSetting", .SSE2);
                    options.mCOptimizationLevel = data.GetEnum<COptimizationLevel>("COptimizationLevel", isRelease ? .O2 : .O0);

                    for (var projectName in data.Enumerate("ConfigSelections"))
                    {
						Project project = FindProject(scope String(projectName));
                        if (project != null)
                        {
							String expectConfig = configName;
							if (isTest)
							{
								if (project != mProjects[0])
									expectConfig = "Debug";
							}
                            
                            var configSelection = new ConfigSelection();
                            configSelection.mEnabled = data.GetBool("Enabled", true);
                            configSelection.mConfig = new String();
                            data.GetString("Config", configSelection.mConfig, expectConfig);
                            configSelection.mPlatform = new String();
                            data.GetString("Platform", configSelection.mPlatform, platformName);
                            options.mConfigSelections[project] = configSelection;
                        }
                    }

					for (data.Enumerate("DistinctOptions"))
					{
						var typeOptions = new DistinctBuildOptions();
						typeOptions.Deserialize(data);
						options.mDistinctBuildOptions.Add(typeOptions);
					}
                }
            }
        }

		public void FinishDeserialize(StructuredData data)
		{
			for (data.Enumerate("Locked"))
			{
				String projName = scope .();
				data.GetCurString(projName);
				let project = FindProject(projName);
				if (project != null)
					project.mLocked = true;
			}

			for (data.Enumerate("Unlocked"))
			{
				String projName = scope .();
				data.GetCurString(projName);
				let project = FindProject(projName);
				if (project != null)
					project.mLocked = false;
			}
		}

        public void FixOptions()
        {
            for (var configKV in mConfigs)
            {
                for (var platformName in configKV.value.mPlatforms.Keys)
                {
                    FixOptions(configKV.key, platformName);
                }
            }
        }

		public void FixOptionsForPlatform(String platformName)
		{
		    for (var configKV in mConfigs)
		    {
		        FixOptions(configKV.key, platformName);
		    }
		}

        public void FixOptions(String configName, String platformName)
        {
			bool isTest = configName.Contains("Test");

			Config configVal;
			switch (mConfigs.TryAdd(configName))
			{
			case .Added(let keyPtr, let configPtr):
				*keyPtr = new String(configName);
				configVal = new Config();
				*configPtr = configVal;

			case .Exists(?, let configPtr):
				configVal = *configPtr;
			}

			Options options;
			switch (configVal.mPlatforms.TryAdd(platformName))
			{
			case .Added(let keyPtr, let optionsPtr):
				*keyPtr = new String(platformName);
				options = new Options();
				*optionsPtr = options;
				SetupDefault(options, configName, platformName);
			case .Exists(?, let optionsPtr):
				options = *optionsPtr;
			}
			
			var removeList = scope List<Project>();
            for (var project in options.mConfigSelections.Keys)
            {
                if (!mProjects.Contains(project))
                {
                    // This project is no longer in the workspace
					removeList.Add(project);
                }
            }

			for (var project in removeList)
			{
				var value = options.mConfigSelections.GetValue(project);
				if (value case .Ok)
				{
					delete value.Get();
					options.mConfigSelections.Remove(project);
				}
			}

            for (var project in mProjects)
            {
                ConfigSelection configSelection;
                options.mConfigSelections.TryGetValue(project, out configSelection);

                String findConfig = configName;
                String findPlatform = platformName;

				bool hadConfig = false;
				bool hadPlatform = false;

				if (isTest)
				{
					if (project != mProjects[0])
					{
						findConfig = "Debug";
					}
				}

                if (configSelection != null)
                {
					hadConfig = project.mConfigs.ContainsKey(configSelection.mConfig);
                    if (hadConfig)
                    {
                        findConfig = configSelection.mConfig;
                        hadPlatform = project.mConfigs[configSelection.mConfig].mPlatforms.ContainsKey(configSelection.mPlatform);
                    }
                }

                if ((!hadConfig) || (!hadPlatform))
                {
					if (configSelection == null)
                    {
                        configSelection = new ConfigSelection();
						configSelection.mConfig = new String(findConfig);
						configSelection.mPlatform = new String(findPlatform);
						options.mConfigSelections[project] = configSelection;
					}

					project.CreateConfig(findConfig, findPlatform);
					configSelection.mEnabled = true;
                }
            }
        }

        public void WithProjectItems(Action<ProjectItem> func)
        {
            for (var project in mProjects)
            {
                project.WithProjectItems(scope (projectItem) =>
                    {
                        func(projectItem);
                    });                
            }
        }

		void FlattenCompileInstanceList()
		{
			if (mCompileInstanceList.Count == 0)
				return;

			var headCompileInstance = mCompileInstanceList[0];
			headCompileInstance.mIsReused = true;
			if (mCompileInstanceList.Count > 1)
			{
				for (var pair in headCompileInstance.mProjectItemCompileInstances)
				{
					var projectSource = pair.key;
					ProjectSourceCompileInstance bestEntry = null;
					for (int checkIdx = mCompileInstanceList.Count - 1; checkIdx >= 1; checkIdx--)
					{
						mCompileInstanceList[checkIdx].mProjectItemCompileInstances.TryGetValue(projectSource, out bestEntry);
						if (bestEntry != null)
							break;
					}
					if (bestEntry != null)
					{
						pair.value.Deref();
						bestEntry.mRefCount++;
						@pair.SetValue(bestEntry);
					}
				}

				while (mCompileInstanceList.Count > 1)
				{
					var compileInstanceList = mCompileInstanceList.PopBack();
					delete compileInstanceList;
				}

				for (var pair in headCompileInstance.mProjectItemCompileInstances)
				{
					Debug.Assert(pair.value.mRefCount == 1);
				}
			}
		}

        public void SetupProjectCompileInstance(bool isHotReload)
        {
            using (mMonitor.Enter())
            {                
                if ((!isHotReload) && (mCompileInstanceList.Count > 0))
				{
					// Only keep the most recent one if we're just doing a normal compile
					FlattenCompileInstanceList();
					return;
				}
                CompileInstance compileInstance = new CompileInstance();
                mCompileInstanceList.Add(compileInstance);                
            }
        }

        public int GetHighestSuccessfulCompileIdx()
        {
            using (mMonitor.Enter())
            {
                int checkIdx = mCompileInstanceList.Count - 1;
                while (checkIdx >= 0)
                {
                    CompileInstance compileInstance = mCompileInstanceList[checkIdx];
                    if (compileInstance.mCompileResult == .Success)
                        return checkIdx;
                    checkIdx--;
                }
                return -1;
            }
        }

        public int GetHighestCompileIdx()
        {
            using (mMonitor.Enter())
            {
                return mCompileInstanceList.Count - 1;
            }            
        }

		public void ProjectSourceRemoved(ProjectSource projectSource)
		{
			for (var compileInstance in mCompileInstanceList)
			{
				if (compileInstance.mProjectItemCompileInstances.GetAndRemove(projectSource) case .Ok((?, let compileInstance)))
				{
					compileInstance.Deref();
				}
			}
		}

        public void ProjectSourceCompiled(ProjectSource projectSource, String source, SourceHash sourceHash, IdSpan sourceCharIdData, bool canMoveSourceString = false)
        {
            using (mMonitor.Enter())
            {
                if (mCompileInstanceList.Count == 0)
                    return;
                var compileInstance = mCompileInstanceList[mCompileInstanceList.Count - 1];

				Debug.Assert(sourceCharIdData.mLength > 0);
                var projectSourceCompileInstance = new ProjectSourceCompileInstance();
                projectSourceCompileInstance.mSource = new String();
				if (canMoveSourceString)
					source.MoveTo(projectSourceCompileInstance.mSource, true);
				else
					projectSourceCompileInstance.mSource.Set(source);
				projectSourceCompileInstance.mSourceHash = sourceHash;
                projectSourceCompileInstance.mSourceCharIdData = sourceCharIdData.Duplicate();

				ProjectItem* keyPtr;
				ProjectSourceCompileInstance* compileInstancePtr;
				if (compileInstance.mProjectItemCompileInstances.TryAdd(projectSource, out keyPtr, out compileInstancePtr))
				{
					*keyPtr = projectSource;
					*compileInstancePtr = projectSourceCompileInstance;
				}
				else
				{
					(*compileInstancePtr).Deref();
					*compileInstancePtr = projectSourceCompileInstance;
				}
            }
        }

        public ProjectSourceCompileInstance GetProjectSourceCompileInstance(ProjectSource projectSource, int compileInstanceIdx)
        {
			int useCompileInstanceIdx = compileInstanceIdx;

            using (mMonitor.Enter())
            {
                if (mCompileInstanceList.Count == 0)
                    return null;
                if (useCompileInstanceIdx == -1)
                    useCompileInstanceIdx = GetHighestCompileIdx();
                var compileInstance = mCompileInstanceList[useCompileInstanceIdx];

                ProjectSourceCompileInstance projectSourceCompileInstance;
                if ((!compileInstance.mProjectItemCompileInstances.TryGetValue(projectSource, out projectSourceCompileInstance)) && (useCompileInstanceIdx > 0))
				{
					for (int checkIdx = useCompileInstanceIdx - 1; checkIdx >= 0; checkIdx--)
					{
						var checkCompileInstance = mCompileInstanceList[checkIdx];
						if (checkCompileInstance.mProjectItemCompileInstances.TryGetValue(projectSource, out projectSourceCompileInstance))
							break;
					}

					// Add it to the index we were looking at for faster lookup next time (even if nothing was found)
					if (projectSourceCompileInstance != null)
						projectSourceCompileInstance.mRefCount++;
					compileInstance.mProjectItemCompileInstances[projectSource] = projectSourceCompileInstance;
				}
                return projectSourceCompileInstance;
            }
        }

        public int32 GetProjectSourceCharId(ProjectSource projectSource, int compileInstanceIdx, int line, int column)
        {
            using (mMonitor.Enter())
            {
                if (mCompileInstanceList.Count == 0)
                    return -1;                

                ProjectSourceCompileInstance projectSourceCompileInstance = GetProjectSourceCompileInstance(projectSource, compileInstanceIdx);
                if (projectSourceCompileInstance != null)
                {
					projectSourceCompileInstance.mSourceCharIdData.Prepare();
                    int encodeIdx = 0;
                    int spanLeft = 0;
                    int32 charId = 1;

                    int curLine = 0;
                    int curColumn = 0;
                    int charIdx = 0;
                    while (true)
                    {     
                        while (spanLeft == 0)
                        {
                            if (projectSourceCompileInstance.mSourceCharIdData.IsEmpty)
                            {
                                spanLeft = (int32)projectSourceCompileInstance.mSource.Length;
                                continue;
                            }

                            int32 cmd = Utils.DecodeInt(projectSourceCompileInstance.mSourceCharIdData.mData, ref encodeIdx);
                            if (cmd > 0)
                                charId = cmd;
                            else
                                spanLeft = -cmd;
                            if (cmd == 0)
                                return 0;
                        }
                                                
                        char8 c = projectSourceCompileInstance.mSource[charIdx++];

                        if (curLine == line)
                        {
                            if (curColumn == column)
                                return charId;
                            // For column == -1, use first non-whitespace char8
                            if ((column == -1) && (!c.IsWhiteSpace))
                                return charId;
                        }

                        if (c == '\n')
                        {
                            if (curLine == line)
                                return charId;
                            curLine++;
                            curColumn = 0;
                        }
                        else
                            curColumn++;
                        spanLeft--;
                        charId++;
                    }
                }

                return -1;
            }
        }

        public IdSpan GetProjectSourceSelectionCharIds(ProjectSource projectSource, int compileInstanceIdx, int defLineStart, int defLineEnd, out int32 charIdStart, out int32 charIdEnd)
        {
            using (mMonitor.Enter())
            {
				int useCompileInstanceIdx = compileInstanceIdx;

                charIdStart = -1;
                charIdEnd = -1;

                if (mCompileInstanceList.Count == 0)
                    return IdSpan();

                if (useCompileInstanceIdx == -1)
                    useCompileInstanceIdx = GetHighestCompileIdx();

                ProjectSourceCompileInstance projectSourceCompileInstance = GetProjectSourceCompileInstance(projectSource, compileInstanceIdx);
                if (projectSourceCompileInstance != null)
                {
					projectSourceCompileInstance.mSourceCharIdData.Prepare();
                    int encodeIdx = 0;
                    int spanLeft = 0;
                    int32 charId = 0;

                    int curLine = 0;                    
                    int charIdx = 0;
                    while (true)
                    {
                        while (spanLeft == 0)
                        {
                            if (projectSourceCompileInstance.mSourceCharIdData.mData == null)
                            {
                                spanLeft = (int32)projectSourceCompileInstance.mSource.Length;
                                continue;
                            }

                            int32 cmd = Utils.DecodeInt(projectSourceCompileInstance.mSourceCharIdData.mData, ref encodeIdx);
                            if (cmd > 0)
                                charId = cmd;
                            else
                                spanLeft = -cmd;
                            if (cmd == 0)
                                break;
                        }

                        if (charIdx >= projectSourceCompileInstance.mSource.Length)
                            return IdSpan();

                        char8 c = projectSourceCompileInstance.mSource[charIdx++];

                        if (curLine == defLineStart)
                        {
                            if (!c.IsWhiteSpace)
                            {
                                if (charIdStart == -1)
                                    charIdStart = charId;
                            }
                        }
                        else if (curLine == defLineEnd)
                        {
                            charIdEnd = charId;
                        }
                        else if (curLine > defLineEnd)
                        {
                            break;                            
                        }

                        if (c == '\n')
                            curLine++;
                        spanLeft--;
                        charId++;
                    }
                }

                if ((charIdStart != -1) && (charIdEnd != -1))
                    return projectSourceCompileInstance.mSourceCharIdData;
                return IdSpan();
            }
        }

        public Result<void> GetProjectSourceSection(ProjectSource projectSource, int32 compileInstanceIdx, int32 defLineStart, int32 defLineEnd, String outSourceSection)
        {
            var projectSourceCompileInstance = GetProjectSourceCompileInstance(projectSource, compileInstanceIdx);
            if (projectSourceCompileInstance == null)
                return .Err;

            int32 startIdx = -1;            
            
            int32 curLine = 0;            
            int32 charIdx = 0;
            while (charIdx < projectSourceCompileInstance.mSource.Length)
            {
                char8 c = projectSourceCompileInstance.mSource[charIdx++];
                
                if (c == '\n')
                {
                    if (curLine == defLineStart)
                        startIdx = charIdx;
                    if (curLine == defLineEnd + 1)
                    {
                        charIdx--;
                        break;
                    }

                    curLine++;                    
                }
            }

            if (startIdx == -1)
                return .Err;

            outSourceSection.Append(projectSourceCompileInstance.mSource, startIdx, charIdx - startIdx);
			return .Ok;
        }    

        public bool GetProjectSourceCharIdPosition(ProjectSource projectSource, int compileInstanceIdx, int findCharId, ref int line, ref int column)
        {
            using (mMonitor.Enter())
            {                
                if (mCompileInstanceList.Count == 0)
                    return true; // Allow it through - for the run-without-compiling case
                
                ProjectSourceCompileInstance projectSourceCompileInstance = GetProjectSourceCompileInstance(projectSource, compileInstanceIdx);
                if (projectSourceCompileInstance != null)
                {
					projectSourceCompileInstance.mSourceCharIdData.Prepare();

                    int curLine = 0;
                    int curColumn = 0;

                    int encodeIdx = 0;
                    int spanLeft = 0;
                    int charId = 0;
                    
                    int charIdx = 0;
                    while (true)
                    {
                        while (spanLeft == 0)
                        {
                            if (projectSourceCompileInstance.mSourceCharIdData.mData == null)
                            {
                                spanLeft = (int32)projectSourceCompileInstance.mSource.Length;
                                continue;
                            }

                            int cmd = Utils.DecodeInt(projectSourceCompileInstance.mSourceCharIdData.mData, ref encodeIdx);
                            if (cmd > 0)
                                charId = cmd;
                            else
                                spanLeft = -cmd;
                            if (cmd == 0)
                                return false;
                        }

                        if (charId == findCharId)
                        {
                            line = curLine;
                            column = curColumn;
                            return true;
                        }
                        
                        char8 c = projectSourceCompileInstance.mSource[charIdx++];
                        if (c == '\n')
                        {                            
                            curLine++;
                            curColumn = 0;
                        }
                        else
                            curColumn++;
                        spanLeft--;
                        charId++;
                    }
                }

                return false;                
            }
        }

        public void StoppedRunning()
        {
            FlattenCompileInstanceList();
        }        
    }
}
