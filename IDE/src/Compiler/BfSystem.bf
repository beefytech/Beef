using System;
using System.Collections;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;

namespace IDE.Compiler
{
    public class BfSystem
    {
		[CallingConvention(.Stdcall), CLink]
		static extern void BfSystem_CheckLock(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_Create();

        [CallingConvention(.Stdcall), CLink]
        static extern void BfSystem_Delete(void* bfSystem);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfSystem_ReportMemory(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        static extern void BfSystem_Update(void* bfSystem);        

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_CreatePassInstance(void* bfSystem);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* BfSystem_GetNamespaceSearch(void* bfSystem, char8* typeName, void* project);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_CreateProject(void* bfSystem, char8* projectName);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfSystem_ClearTypeOptions(void* bfSystem);

		[CallingConvention(.Stdcall), CLink]
		static extern void BfSystem_AddTypeOptions(void* bfSystem, char8* filter, int32 simdSetting, int32 optimizationLevel, int32 emitDebugInfo, int32 runtimeChecks,
            int32 initLocalVariables, int32 emitDynamicCastCheck, int32 emitObjectAccessCheck, int32 allocStackTraceDepth);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_CreateParser(void* bfSystem, void* bfProject);
        
        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_CreateCompiler(void* bfSystem, bool isResolveOnly = false);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_RemoveDeletedParsers(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_RemoveOldParsers(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_RemoveOldData(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_NotifyWillRequestLock(void* bfSystem, int32 priority);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_Lock(void* bfSystem, int32 priority);

        [CallingConvention(.Stdcall), CLink]
        extern static void BfSystem_PerfZoneStart(char8* name);

        [CallingConvention(.Stdcall), CLink]
        extern static void BfSystem_PerfZoneEnd();

        [CallingConvention(.Stdcall), CLink]
        static extern void* BfSystem_Unlock(void* bfSystem);

        [CallingConvention(.Stdcall), CLink]
        extern static void BfSystem_StartTiming();

        [CallingConvention(.Stdcall), CLink]
        extern static void BfSystem_StopTiming();

        [CallingConvention(.Stdcall), CLink]
        extern static void BfSystem_DbgPrintTimings();

		[CallingConvention(.Stdcall), CLink]
		extern static void BfSystem_Log(void* bfSystem, char8* str);

        public void* mNativeBfSystem;
        public bool mIsTiming;
		public Monitor mMonitor = new Monitor() ~ delete _;

        public Dictionary<ProjectSource, BfParser> mParserMap = new Dictionary<ProjectSource, BfParser>() ~ delete _;
        public Dictionary<Project, BfProject> mProjectMap = new Dictionary<Project, BfProject>() ~ delete _;

        public this()
        {
            mNativeBfSystem = BfSystem_Create();
        }        

		public ~this()
		{
			for	(var parser in mParserMap.Values)
				delete parser;
			for (var bfProject in mProjectMap.Values)
				delete bfProject;
			BfSystem_Delete(mNativeBfSystem);
		}

		public void CheckLock()
		{
			BfSystem_CheckLock(mNativeBfSystem);
		}

        public void Update()
        {
            BfSystem_Update(mNativeBfSystem);
        }

		public void ReportMemory()
		{
		    BfSystem_ReportMemory(mNativeBfSystem);
		}

        public void AddProject(Project project)
        {
            using (mMonitor.Enter())
            {
                var bfProject = CreateProject(project.mProjectName);
                mProjectMap[project] = bfProject;
            }
        }

        public BfProject GetBfProject(Project project)
        {
            using (mMonitor.Enter())
            {
                return mProjectMap[project];
            }
        }

        public void RemoveBfProject(Project project)
        {
            using (mMonitor.Enter())
            {
                mProjectMap.Remove(project);                
            }
        }

        public BfPassInstance CreatePassInstance(String dbgStr = null)
        {
            void* nativePassInstance = BfSystem_CreatePassInstance(mNativeBfSystem);
            BfPassInstance passInstance = new BfPassInstance(nativePassInstance);
			if (dbgStr != null)
            	passInstance.mDbgStr = new String(dbgStr);
            return passInstance;
        }

        public BfParser CreateEmptyParser(BfProject bfProject)
        {
            void* nativeBfProject = (bfProject == null) ? null : bfProject.mNativeBfProject;
            void* nativeBfParser = BfSystem_CreateParser(mNativeBfSystem, nativeBfProject);
            BfParser parser = new BfParser(nativeBfParser);
			parser.mSystem = this;
			return parser;
        }

		public void GetNamespaceSearch(String typeName, String outNamespaceSearch, BfProject project)
		{
			char8* namespaceSearch = BfSystem_GetNamespaceSearch(mNativeBfSystem, typeName, project.mNativeBfProject);
			if (namespaceSearch != null)
				outNamespaceSearch.Append(namespaceSearch);
		}

        public BfProject CreateProject(String projectName)
        {
            BfProject project = new BfProject();
            project.mNativeBfProject = BfSystem_CreateProject(mNativeBfSystem, projectName);
            return project;
        }

        /*public bool HasParser(string fileName)
        {
            lock (this)
            {
                return mParserMap.ContainsKey(fileName);
            }
        }*/

        public BfParser CreateParser(ProjectSource projectSource, bool useMap = true)
        {
            using (mMonitor.Enter())
            {                
                BfParser parser;
                if (!useMap)
                {
                    parser = CreateEmptyParser(mProjectMap[projectSource.mProject]);
                    parser.mProjectSource = projectSource;
                    parser.mFileName = new String();
                    projectSource.GetFullImportPath(parser.mFileName);
                    return parser;
                }
            
                BfParser prevParser;
                mParserMap.TryGetValue(projectSource, out prevParser);

                parser = CreateEmptyParser(mProjectMap[projectSource.mProject]);
				parser.mSystem = this;
                parser.mProjectSource = projectSource;
                parser.mFileName = new String();
                projectSource.GetFullImportPath(parser.mFileName);
                mParserMap[projectSource] = parser;
                if (prevParser != null)
				{
                    prevParser.SetNextRevision(parser);
					prevParser.Detach();
					delete prevParser;
				}

                return parser;
            }
        }

        public BfParser CreateNewParserRevision(BfParser prevParser)
        {
            using (mMonitor.Enter())
            {   
                BfParser parser = CreateEmptyParser(mProjectMap[prevParser.mProjectSource.mProject]);
                parser.mFileName = new String(prevParser.mFileName);
                parser.mProjectSource = prevParser.mProjectSource;
                mParserMap[parser.mProjectSource] = parser;
                if (prevParser != null)
				{
                    prevParser.SetNextRevision(parser);
					prevParser.Detach();
					delete prevParser;
				}

                return parser;
            }
        }

        public void FileRenamed(ProjectSource projectSource, String oldFileName, String newFileName)
        {
            using (mMonitor.Enter())
            {
                BfParser prevParser;
                if (mParserMap.TryGetValue(projectSource, out prevParser))
                {                    
                    prevParser.mFileName.Set(newFileName);
                }
            }
        }

        public BfParser FileRemoved(ProjectSource projectSource)
        {
            using (mMonitor.Enter())
            {
                BfParser prevParser = null;
                if (mParserMap.TryGetValue(projectSource, out prevParser))
                {
                    //DeleteParser(prevParser);
                    mParserMap.Remove(projectSource);
                }
                return prevParser;
            }            
        }

        public BfParser FindParser(ProjectSource projectSource)
        {
            using (mMonitor.Enter())
            {
                BfParser prevParser;
                mParserMap.TryGetValue(projectSource, out prevParser);
                return prevParser;
            }
        }

        public BfCompiler CreateCompiler(bool isResolveOnly)
        {
            void* nativeBfCompiler = BfSystem_CreateCompiler(mNativeBfSystem, isResolveOnly);
            var bfCompiler = new BfCompiler(nativeBfCompiler);
            bfCompiler.mIsResolveOnly = isResolveOnly;
            bfCompiler.mBfSystem = this;
            return bfCompiler;
        }

        public void RemoveDeletedParsers()
        {
            BfSystem_RemoveDeletedParsers(mNativeBfSystem);
        }

        public void RemoveOldParsers()
        {
            BfSystem_RemoveOldParsers(mNativeBfSystem);
        }

        public void RemoveOldData()
        {
            BfSystem_RemoveOldData(mNativeBfSystem);
        }        

        public void NotifyWillRequestLock(int32 priority)
        {
            BfSystem_NotifyWillRequestLock(mNativeBfSystem, priority);
        }

        public void Lock(int32 priority)
        {
            BfSystem_Lock(mNativeBfSystem, priority);
        }

        public void Unlock()
        {
            BfSystem_Unlock(mNativeBfSystem);
        }

        public void StartTiming()
        {
            mIsTiming = true;
            BfSystem_StartTiming();
        }

        public void PerfZoneStart(String name)
        {
            BfSystem_PerfZoneStart(name);
        }

        public void PerfZoneEnd()
        {            
            BfSystem_PerfZoneEnd();
        }

        public void StopTiming()
        {
            mIsTiming = false;
            BfSystem_StopTiming();
        }

        public void DbgPrintTimings()
        {
            BfSystem_DbgPrintTimings();
        }

		public void ClearTypeOptions()
		{
			BfSystem_ClearTypeOptions(mNativeBfSystem);
		}

		public void AddTypeOptions(String filter, BuildOptions.SIMDSetting? simdSetting, BuildOptions.BfOptimizationLevel? optimizationLevel, BuildOptions.EmitDebugInfo? emitDebugInfo, bool? runtimeChecks,
            bool? initLocalVariables, bool? emitDynamicCastCheck, bool? emitObjectAccessCheck, int32? allocStackTraceDepth)
		{
			int32 simdSettingInt = (simdSetting == null) ? -1 : (int32)simdSetting.Value;
			int32 optimizationLevelInt = (optimizationLevel == null) ? -1 : (int32)optimizationLevel.Value;
			int32 emitDebugInfoInt = (emitDebugInfo == null) ? -1 : (int32)emitDebugInfo.Value;
			int32 runtimeChecksInt = (runtimeChecks == null) ? -1 : runtimeChecks.Value ? 1 : 0;
			int32 initLocalVariablesInt = (initLocalVariables == null) ? -1 : initLocalVariables.Value ? 1 : 0;
			int32 emitDynamicCastCheckInt = (emitDynamicCastCheck == null) ? -1 : emitDynamicCastCheck.Value ? 1 : 0;
			int32 emitObjectAccessCheckInt = (emitObjectAccessCheck == null) ? -1 : emitObjectAccessCheck.Value ? 1 : 0;
			int32 allocStackTraceDepthInt = (allocStackTraceDepth == null) ? -1 : allocStackTraceDepth.Value;
			BfSystem_AddTypeOptions(mNativeBfSystem, filter, simdSettingInt, optimizationLevelInt, emitDebugInfoInt, runtimeChecksInt,
                initLocalVariablesInt, emitDynamicCastCheckInt, emitObjectAccessCheckInt, allocStackTraceDepthInt);
		}

		public void AddTypeOptions(DistinctBuildOptions typeOption)
		{
			AddTypeOptions(typeOption.mFilter, typeOption.mBfSIMDSetting, typeOption.mBfOptimizationLevel, typeOption.mEmitDebugInfo, typeOption.mRuntimeChecks,
				typeOption.mInitLocalVariables, typeOption.mEmitDynamicCastCheck, typeOption.mEmitObjectAccessCheck, typeOption.mAllocStackTraceDepth);
		}

		public void Log(String str)
		{
			BfSystem_Log(mNativeBfSystem, str);
		}
    }
}
