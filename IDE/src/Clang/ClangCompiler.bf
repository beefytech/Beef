using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.IO;
using Beefy.widgets;
using Beefy.utils;
using IDE.Compiler;
using Beefy;

#if IDE_C_SUPPORT

namespace IDE.Clang
{
    public class ClangCompiler : CompilerBase
    {
		public const bool cEnableClangHelper = false;

        public enum CompilerType
        {
            Resolve,
            Build
        }

        public enum DepCheckerType
        {
            CDep, // Fast - custom in-memory checker
            Clang // Accurate - .dep files written after build
        }

        public class FileEntry
        {
            public ProjectSource mProjectSource;
            public String mFilePath ~ delete _;
            public List<String> mClangFileRefs ~ DeleteContainerAndItems!(_);
            public List<String> mCDepFileRefs ~ DeleteContainerAndItems!(_);
            public String mObjectFilePath ~ delete _;
            public String mBuildStringFilePath ~ delete _;
        }

        public class CacheEntry
        {
            public List<String> mFileRefs = new List<String>();
        }
        [CallingConvention(.Stdcall), CLink]
        static extern void* ClangHelper_Create(bool isForResolve);

        [CallingConvention(.Stdcall), CLink]
        static extern void ClangHelper_Delete(void* clangHelper);

        [CallingConvention(.Stdcall), CLink]
        static extern void ClangHelper_AddTranslationUnit(void* clangHelper, char8* fileName, char8* headerPrefix, char8* clangArgs, void* elementTypeArray, int32 char8Len);

        [CallingConvention(.Stdcall), CLink]
        static extern void ClangHelper_RemoveTranslationUnit(void* clangHelper, char8* fileName);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* ClangHelper_Classify(void* clangHelper, char8* fileName, void* elementTypeArray, int32 char8Len, int32 cursorIdx, int32 errorLookupTextIdx,bool ignoreErrors);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* ClangHelper_Autocomplete(void* clangHelper, char8* fileName, void* elementTypeArray, int32 char8Len, int32 cursorIdx);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* ClangHelper_FindDefinition(void* clangHelper, char8* fileName, int32 defPos, out int32 outDefLine, out int32 outDefColumn);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* ClangHelper_GetNavigationData(void* clangHelper, char8* fileName);

		[CallingConvention(.Stdcall), CLink]
		static extern char8* ClangHelper_GetCurrentLocation(void* clangHelper, char8* fileName, int32 defPos);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* ClangHelper_DetermineFilesReferenced(void* clangHelper, char8* fileName);

        [CallingConvention(.Stdcall), CLink]
        static extern void CDep_ClearCache();

        [CallingConvention(.Stdcall), CLink]
        static extern void CDep_Shutdown();

        [CallingConvention(.Stdcall), CLink]
        static extern char8* CDep_DetermineFilesReferenced(char8* fileName, String cArgs);

        [CallingConvention(.Stdcall), CLink]
        static extern int32 CDep_GetIncludePosition(char8* sourceFileName, char8* sourceContent, char8* headerFileName, String cArgs);

		//static int sRefCount = 0;
        void* mNativeClangHelper;

        public List<ProjectSource> mSourceMRUList = new List<ProjectSource>() ~ delete _;
        public int32 mProjectSourceVersion; // Updated when MRU list changes, or when a CPP is saved

        public Dictionary<ProjectSource, FileEntry> mProjectFileSet = new Dictionary<ProjectSource, FileEntry>() ~ delete _;
        public Dictionary<Project, String> mProjectBuildString = new Dictionary<Project, String>() ~ { for (var val in _.Values) delete val; delete _; }; // Don't delete 'Project', only the String
        public Dictionary<String, String> mHeaderToSourceMap = new Dictionary<String, String>() ~ DeleteDictionaryAndKeysAndItems!(_);

        FileWatcher mFileWatcher = new FileWatcher() ~ delete _;
        public bool mDoDependencyCheck = true;
        public ClangCompiler mPairedCompiler;
        public bool mCompileWaitsForQueueEmpty = true;
        CompilerType mCompilerType;        
        
        protected class ClangProjectSourceCommand : Command
        {
            public ProjectSource mProjectSource;            
        }

        protected class ClangCheckDependencyCommand : Command
        {
            public ProjectSource mProjectSource;
            public DepCheckerType mDepCheckerType;
        }

        protected class ClangReparseSourceCommand : Command
        {
            public ProjectSource mProjectSource;
        }

        protected class FileRemovedCommand : Command
        {
            public String mFileName ~ delete _;
        }        

        public this(bool isForResolve)
        {
            mCompilerType = isForResolve ? CompilerType.Resolve : CompilerType.Build;
			//if (sRefCount == 0)
			if (cEnableClangHelper)
                mNativeClangHelper = ClangHelper_Create(isForResolve);
			//sRefCount++;
        }

        public ~this()
        {
            CancelBackground();
			//--sRefCount;
			//if (sRefCount == 0)
            {
                ClangHelper_Delete(mNativeClangHelper);
                mNativeClangHelper = null;
            }
            CDep_Shutdown();

			for (var fileEntry in mProjectFileSet.Values)
				delete fileEntry;
        }

        public void UpdateMRU(ProjectSource projectSource)
        {
            if (IDEUtils.IsHeaderFile(projectSource.mPath))
                return;

            using (mMonitor.Enter())
            {
                if ((mSourceMRUList.Count > 0) && (mSourceMRUList[mSourceMRUList.Count - 1] == projectSource))
                    return;

                mProjectSourceVersion++;
                mSourceMRUList.Remove(projectSource);
                mSourceMRUList.Add(projectSource);

                while (mSourceMRUList.Count > 10)
                    mSourceMRUList.RemoveAt(0);
            }
        }

        public void QueueFileRemoved(String filePath)
        {
            FileRemovedCommand command = new FileRemovedCommand();
            command.mFileName = new String(filePath);
            QueueCommand(command);
        }

        public override void QueueProjectSource(ProjectSource projectSource)
        {            
            using (mMonitor.Enter())
            {
                for (var command in mCommandQueue)
                {
                    var checkProjectSourceCommand = command as ClangProjectSourceCommand;
                    if ((checkProjectSourceCommand != null) && (checkProjectSourceCommand.mProjectSource == projectSource))
                        return;
                }
                
                ClangProjectSourceCommand clangProjectSourceCommand = new ClangProjectSourceCommand();
                clangProjectSourceCommand.mProjectSource = projectSource;
                QueueCommand(clangProjectSourceCommand);
            }
        }

        public void QueueCheckDependencies(ProjectSource projectSource, DepCheckerType depType)
        {
            for (var command in mCommandQueue)
            {
                var checkCheckDependencyCommand = command as ClangCheckDependencyCommand;
                if ((checkCheckDependencyCommand != null) && (checkCheckDependencyCommand.mProjectSource == projectSource) && 
                    (checkCheckDependencyCommand.mDepCheckerType == depType))
                    return;
            }

            ClangCheckDependencyCommand clangCheckDependencyCommand = new ClangCheckDependencyCommand();
            clangCheckDependencyCommand.mProjectSource = projectSource;
            QueueCommand(clangCheckDependencyCommand);
        }

        public bool Autocomplete(String fileName, EditWidgetContent.CharData[] char8Data, int textLength, int cursorIdx, String outResult)
        {
            EditWidgetContent.CharData* char8DataPtr = char8Data.CArray();
            {
                char8* returnStringPtr = ClangHelper_Autocomplete(mNativeClangHelper, fileName, char8DataPtr, (int32)textLength, (int32)cursorIdx);
                if (returnStringPtr == null)
                    return false;
                outResult.Append(returnStringPtr);
				return true;
            }
        }
        
        public bool Classify(String fileName, EditWidgetContent.CharData[] char8Data, int textLength, int cursorIdx, int errorLookupTextIdx, bool ignoreErrors, String outResult)
        {
            EditWidgetContent.CharData* char8DataPtr = char8Data.CArray();
            {
                char8* returnStringPtr = ClangHelper_Classify(mNativeClangHelper, fileName, char8DataPtr, (int32)textLength, (int32)cursorIdx, (int32)errorLookupTextIdx, ignoreErrors);
                if (returnStringPtr == null)
                    return false;
                outResult.Append(returnStringPtr);
				return true;
            }
        }

        public bool DetermineFilesReferenced(String fileName, String outResult)
        {
            char8* returnStringPtr = ClangHelper_DetermineFilesReferenced(mNativeClangHelper, fileName);
            if (returnStringPtr == null)
                return false;
            outResult.Append(returnStringPtr);
			return true;
        }

        public bool CDepDetermineFilesReferenced(String fileName, String cArgs, String outResult)
        {
            char8* returnStringPtr = CDep_DetermineFilesReferenced(fileName, cArgs);
            if (returnStringPtr == null)
                return false;
            outResult.Append(returnStringPtr);
			return true;
        }

        public int32 CDepGetIncludePosition(String sourceFileName, String sourceContent, String headerFileName, String cArgs)
        {
            return CDep_GetIncludePosition(sourceFileName, sourceContent, headerFileName, cArgs);
        }

        public bool FindDefinition(String fileName, int defPos, String defFileName, out int defLine, out int defColumn)
        {
			String useDefFileName = defFileName;
			int32 defLineOut;
			int32 defColumnOut;
            char8* fileNamePtr = ClangHelper_FindDefinition(mNativeClangHelper, fileName, (int32)defPos, out defLineOut, out defColumnOut);
			defLine = defLineOut;
			defColumn = defColumnOut;
            if (fileNamePtr == null)
            {
                useDefFileName = null;
                defLine = 0;
                defColumn = 0;
                return false;
            }
            useDefFileName.Append(fileNamePtr);
			return true;
        }

        public void GetNavigationData(String fileName, String outResult)
        {
            char8* strPtr = ClangHelper_GetNavigationData(mNativeClangHelper, fileName);
            outResult.Append(strPtr);
        }

		public void GetCurrentLocation(String fileName, String outResult, int defPos)
		{
		    char8* strPtr = ClangHelper_GetCurrentLocation(mNativeClangHelper, fileName, (int32)defPos);
		    outResult.Append(strPtr);
		}
        
        public void GetBuildStringFileName(String buildDir, Project project, String outBuildStr)
        {
            outBuildStr.Append(buildDir, "/", project.mProjectName, ".copt");
        }

        void CheckClangDependencies(ProjectSource projectSource)
        {
            String srcFilePath = scope String();
			/*if (projectSource.mPath == "../../BeefRT/gperftools/src/stacktrace.cc")
			{
				NOP!();
			}*/

            using (projectSource.mProject.mMonitor.Enter())
                projectSource.GetFullImportPath(srcFilePath);
            FileEntry fileEntry;
            mProjectFileSet.TryGetValue(projectSource, out fileEntry);
            if (fileEntry == null)
            {
                fileEntry = new FileEntry();
                using (mMonitor.Enter())
                {
                    mProjectFileSet[projectSource] = fileEntry;
                }
            }
            fileEntry.mProjectSource = projectSource;
            String.NewOrSet!(fileEntry.mFilePath, srcFilePath);

            List<String> oldFileRefs = fileEntry.mClangFileRefs;
            List<String> newFileRefs = new List<String>();

            String depFilePath = scope String();
            IDEApp.sApp.GetClangOutputFilePathWithoutExtension(projectSource, depFilePath);
            depFilePath.Append(".dep");
			// We don't add the .dep since the dep could write after the obj
            //newFileRefs.Add(new String(depFilePath));

			var fileText = scope String();
			var result = File.ReadAllText(depFilePath, fileText);
			if (case .Ok = result)
			{
				// We 
				newFileRefs.Add(new String(srcFilePath));

				int checkIdx = fileText.IndexOf("c:/beef/BeefRT/gc.cpp");
				if (checkIdx != -1)
				{
					NOP!();
				}

				int32 textStart = -1;
				for (int32 i = 0; i < fileText.Length; i++)
				{
					bool lineEnd = false;

					char8 c = fileText[i];
					if (c == ':')
					{
						if (fileText[i + 1] == ' ')
							lineEnd = true;
					}
					else if (c == ' ')
					{
						if ((i > 0) && (fileText[i - 1] != '\\'))
						{
							lineEnd = true;
						}
						/*else if ((i < fileText.Length - 2) && (fileText[i + 1] == '\\') && ((fileText[i + 2] == '\n') || (fileText[i + 2] == '\r')))
						{
							lineEnd = true;
						}*/
					}
					else if (Char8.IsWhiteSpace(c))
					{
						lineEnd = true;
					}
					else if (c != '\\')
					{
						if (textStart == -1)
							textStart = i;
					}

					if ((lineEnd) && (textStart != -1))
					{
						if ((projectSource.mPath == "cpp/sys.cpp") && (newFileRefs.Count == 9))
						{
							NOP!();
						}

						String line = new String();
                        fileText.Substring(textStart, i - textStart, line);
						textStart = -1;

						line.Replace("\\ ", " ");
						if (line.EndsWith(" \\"))
							line.Remove(line.Length - 2);
						IDEUtils.FixFilePath(line);
						newFileRefs.Add(line);
					}
				}
			}

            for (var origRefFilePath in newFileRefs)
            {
                String refFilePath = scope String(origRefFilePath);
                IDEUtils.FixFilePath(refFilePath);
				bool found = false;
				if (oldFileRefs != null)
				{
					int32 idx = oldFileRefs.IndexOf(refFilePath);
					if (idx != -1)
					{
						found = true;
						delete oldFileRefs[idx];
						oldFileRefs.RemoveAtFast(idx);
					}
				}

                if (!found)
                {
					// Don't watch for writes of compiler-generated files, otherwise we get changes triggered
					//  after every compile
                    bool ignoreWrites = refFilePath.EndsWith(".obj") || refFilePath.EndsWith(".dep");
                    mFileWatcher.WatchFile(refFilePath, projectSource, ignoreWrites);
                    mDoDependencyCheck = true;
                }
            }

            fileEntry.mClangFileRefs = newFileRefs;
            if (oldFileRefs != null)
            {
                for (var oldRef in oldFileRefs)
                    mFileWatcher.RemoveWatch(oldRef, projectSource);
				DeleteContainerAndItems!(oldFileRefs);
            }
        }

        void CheckCDepDependencies(ProjectSource projectSource, String clangArgs)
        {
            FileEntry fileEntry;
            mProjectFileSet.TryGetValue(projectSource, out fileEntry);
			if (fileEntry == null)
				return; // Why is this null?

            String srcFilePath = scope String();
            projectSource.GetFullImportPath(srcFilePath);
            String filesReferencedStr = scope String();
            CDepDetermineFilesReferenced(srcFilePath, clangArgs, filesReferencedStr);
			//Debug.Assert(filesReferencedStr != null);

            if (fileEntry.mCDepFileRefs == null)
                fileEntry.mCDepFileRefs = new List<String>();
            else
			{
				for (var str in fileEntry.mCDepFileRefs)
					delete str;
                fileEntry.mCDepFileRefs.Clear();
			}
			var fileViews = scope List<StringView>();
			filesReferencedStr.Split(fileViews, '\n');
            for (var fileView in fileViews)
                if (fileView.Length > 0)
                    fileEntry.mCDepFileRefs.Add(new String(fileView));
        }

        public void GetClangArgs(ProjectSource projectSource, String outClangArgs)
        {
            using (projectSource.mProject.mMonitor.Enter())
            {
                var clangArgList = new List<String>();
                IDEApp.sApp.GetClangResolveArgs(projectSource.mProject, clangArgList);
                if (clangArgList != null)
                    outClangArgs.JoinInto("\n", clangArgList.GetEnumerator());
				DeleteContainerAndItems!(clangArgList);
            }
        }

        public void AddTranslationUnit(ProjectSource projectSource, String headerPrefix)
        {
            String srcFilePath = scope String();
            using (projectSource.mProject.mMonitor.Enter())
                projectSource.GetFullImportPath(srcFilePath);
            String clangArgs = scope String();
            GetClangArgs(projectSource, clangArgs);
            ClangHelper_AddTranslationUnit(mNativeClangHelper, srcFilePath, headerPrefix, clangArgs, null, 0);            
        }

        public void AddTranslationUnit(ProjectSource projectSource, String headerPrefix, EditWidgetContent.CharData[] char8Data, int char8Len)
        {            
            String srcFilePath = scope String();
            using (projectSource.mProject.mMonitor.Enter())
                projectSource.GetFullImportPath(srcFilePath);
            String clangArgs = scope String();
            GetClangArgs(projectSource, clangArgs);

            EditWidgetContent.CharData* char8DataPtr = char8Data.CArray();
            {
                ClangHelper_AddTranslationUnit(mNativeClangHelper, srcFilePath, headerPrefix, clangArgs, char8DataPtr, (int32)char8Len);
            }
        }

        public void AddTranslationUnit(String srcFilePath, String headerPrefix, String clangArgs)
        {
            ClangHelper_AddTranslationUnit(mNativeClangHelper, srcFilePath, headerPrefix, clangArgs, null, 0);
        }

		public static int32 sFileIdx = 0;

        public static int32 sThreadRunningCount = 0;
        protected override void DoProcessQueue()
        {
			// libclang CXIndices aren't current thread safe
            sThreadRunningCount++;
            Debug.Assert(sThreadRunningCount < 2);

            while (mThreadYieldCount == 0)
            {
                Command command = null;
                using (mMonitor.Enter())
                {
                    if (mCommandQueue.Count == 0)
                        break;
                    command = mCommandQueue[0];
                }
                
                if (command is ClangProjectSourceCommand)
                {

					sFileIdx++;

                    var clangProjectSourceCommand = (ClangProjectSourceCommand)command;
					/*if (clangProjectSourceCommand.mProjectSource.mPath == "../../BeefRT/gperftools/src/malloc_extension.cc")
					{
						NOP!();
					}*/

                    var projectSource = clangProjectSourceCommand.mProjectSource;
                    String clangArgs = scope String();
                    GetClangArgs(projectSource, clangArgs);
                    String srcFilePath = scope String();
                    using (projectSource.mProject.mMonitor.Enter())
                    {
                        projectSource.GetFullImportPath(srcFilePath);
                    }

                    if (clangArgs == null)
                    {
						// Don't actually do anything
                    }
                    else if (mCompilerType == CompilerType.Resolve)
                    {
                        ClangHelper_AddTranslationUnit(mNativeClangHelper, srcFilePath, null, clangArgs, null, 0);
                    }
                    else if (mCompilerType == CompilerType.Build)
                    {
                        if (!IDEUtils.IsHeaderFile(srcFilePath))
                        {
                            CheckClangDependencies(projectSource);
                            CheckCDepDependencies(projectSource, clangArgs);
                        }
                        else
                        {
                            
                        }
                    }
                }

                if (command is ClangCheckDependencyCommand)
                {
                    var clangCheckDependencyCommand = (ClangCheckDependencyCommand)command;
                    var projectSource = clangCheckDependencyCommand.mProjectSource;
                    if (clangCheckDependencyCommand.mDepCheckerType == DepCheckerType.Clang)
                    {
                        CheckClangDependencies(projectSource);
                    }
                    else
                    {
                        String clangArgs = scope String();
                        String srcFilePath = scope String();
                        using (projectSource.mProject.mMonitor.Enter())
                        {
                            projectSource.GetFullImportPath(srcFilePath);
                            List<String> clangArgList = new List<String>();
                            IDEApp.sApp.GetClangResolveArgs(projectSource.mProject, clangArgList);
                            if (clangArgList != null)
                                clangArgs.JoinInto("\n", clangArgList.GetEnumerator());
							DeleteContainerAndItems!(clangArgList);
                        }
                        CheckCDepDependencies(projectSource, clangArgs);
                    }
                }

                if (command is FileRemovedCommand)
                {
                    var fileRemovedCommand = (FileRemovedCommand)command;
                    ClangHelper_RemoveTranslationUnit(mNativeClangHelper, fileRemovedCommand.mFileName);
                }

                if (command is ProjectSourceRemovedCommand)
                {
                    var fileRemovedCommand = (ProjectSourceRemovedCommand)command;
                    String srcFileName = scope String();
                    var projectSource = fileRemovedCommand.mProjectSource;
                    
                    using (projectSource.mProject.mMonitor.Enter())
                    {
                        projectSource.GetFullImportPath(srcFileName);
                    }
                    
                    ClangHelper_RemoveTranslationUnit(mNativeClangHelper, srcFileName);

                    using (this.mMonitor.Enter())
                    {
                        mHeaderToSourceMap.Remove(srcFileName);
                    }

                    var fileEntry = GetProjectEntry(projectSource);
                    if (fileEntry != null)
                    {
                        using (this.mMonitor.Enter())
                        {
							mProjectFileSet.Remove(projectSource);
                            mSourceMRUList.Remove(projectSource);
                        }

                        for(var fileRef in fileEntry.mClangFileRefs)                        
                            mFileWatcher.RemoveWatch(fileRef, projectSource);
                        if (fileEntry.mObjectFilePath != null)
                            mFileWatcher.RemoveWatch(fileEntry.mObjectFilePath, null);
                        if (fileEntry.mBuildStringFilePath != null)
                            mFileWatcher.RemoveWatch(fileEntry.mBuildStringFilePath, null);

						delete fileEntry;
                    }                    
                }
                
                using (this.mMonitor.Enter())
                {
					delete command;
                    if (!mShuttingDown)
					{
						var poppedCmd = mCommandQueue.PopFront();
						Debug.Assert(poppedCmd == command);
					}
                }
            }

            if ((mCompilerType == CompilerType.Build) && (mCommandQueue.Count == 0))
            {
				// Clear out cache after we've finished parsing everything
                CDep_ClearCache();
            }

            sThreadRunningCount--;
        }

        public bool HasProjectFile(ProjectSource projectSource)
        {
            using (this.mMonitor.Enter())
            {
                return mProjectFileSet.ContainsKey(projectSource);
            }
        }

        public FileEntry GetProjectEntry(ProjectSource projectSource)
        {
            using (this.mMonitor.Enter())
            {
                FileEntry fileEntry;
                mProjectFileSet.TryGetValue(projectSource, out fileEntry);
                return fileEntry;
            }
        }

        public void FileSaved(String fileName)
        {
            mFileWatcher.FileChanged(fileName);
            ProcessFileChanges();
        }

        public void SetEntryObjFileName(FileEntry fileEntry, String objFilePath)
        {
            using (this.mMonitor.Enter())
            {
                if (objFilePath != fileEntry.mObjectFilePath)
                {
                    if (fileEntry.mObjectFilePath != null)
                        mFileWatcher.RemoveWatch(fileEntry.mObjectFilePath, null);
                    String.NewOrSet!(fileEntry.mObjectFilePath, objFilePath);
                    mFileWatcher.WatchFile(objFilePath, null);
                }
            }
        }

        public void SetEntryBuildStringFileName(FileEntry fileEntry, String buildStringFilePath)
        {
            using (this.mMonitor.Enter())
            {
                if (buildStringFilePath != fileEntry.mBuildStringFilePath)
                {
                    if (fileEntry.mBuildStringFilePath != null)
                        mFileWatcher.RemoveWatch(fileEntry.mBuildStringFilePath, null);
                    String.NewOrSet!(fileEntry.mBuildStringFilePath, buildStringFilePath);
                    mFileWatcher.WatchFile(buildStringFilePath, null);
                }
            }
        }

        public bool DoesEntryNeedRebuild(FileEntry fileEntry)
        {
            if (!mDoDependencyCheck)
                return false;

            if ((fileEntry.mClangFileRefs == null) || (fileEntry.mClangFileRefs.Count == 0))
                return true;

            int64 objFileTime = IDEUtils.GetLastModifiedTime(fileEntry.mObjectFilePath);
            if (objFileTime == 0)
                return true;

            int64 highestRefTime = 0;
            for (var fileRef in fileEntry.mClangFileRefs)
            {
                int64 fileTime = IDEUtils.GetLastModifiedTime(fileRef);
				// Always rebuild if we don't even have a particular dep
                if (fileTime == 0)
                    return true;
                highestRefTime = Math.Max(highestRefTime, fileTime);
            }

            String[] productFileNames = scope String[] { fileEntry.mObjectFilePath, fileEntry.mBuildStringFilePath };
            for (var productFileName in productFileNames)
            {
                int64 productFileTime = IDEUtils.GetLastModifiedTime(fileEntry.mBuildStringFilePath);
                if (productFileTime == 0)
                    return true;
                highestRefTime = Math.Max(highestRefTime, productFileTime);
            }

            return highestRefTime > objFileTime;
        }

        public void ProcessFileChanges()
        {
            while (true)
            {
                ProjectSource depFileChanged = (ProjectSource)mFileWatcher.PopChangedDependency();
                if (depFileChanged == null)
                    break;

                QueueCheckDependencies(depFileChanged, DepCheckerType.CDep);

				//QueueProjectSource(depFileChanged);

				/*ClangReparseSourceCommand command = new ClangReparseSourceCommand();
				command.mProjectSource = depFileChanged;                
				QueueCommand(command);*/

                mDoDependencyCheck = true;
            }
        }        

        public override void Update()
        {
            mAllowThreadStart = (mPairedCompiler == null) || (!mPairedCompiler.mThreadRunning);

            base.Update();

			mFileWatcher.Update();
			String fileChanged = scope String();
            while (true)
            {
                fileChanged.Clear();
                if (!mFileWatcher.PopChangedFile(fileChanged))
					break;
            }

            ProcessFileChanges();			
        }
    }
}

#endif