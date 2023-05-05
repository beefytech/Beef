using System;
using System.Collections;
using System.Text;
using System.IO;
using System.Diagnostics;
using Beefy;
using Beefy.utils;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.res;
using IDE.Compiler;
using IDE.ui;
using IDE.Util;
using System.Threading;
using System.Diagnostics;
using IDE.util;

namespace IDE
{    
    public class ProjectItem : ISerializable, RefCounted
    {
		public enum IncludeKind
		{
			Auto,
			Manual,
			Ignore
		}

		public enum CategoryKind
		{
			None,
			SimpleSource,
		}

		public IncludeKind mIncludeKind;
        public Project mProject;
        public ProjectFolder mParentFolder;
        public String mName = new String() ~ delete _;
        public String mComment = new String() ~ delete _;
		public bool mDetached;

		public virtual bool IncludeInMap
		{
			get
			{
				return true;
			}
		}

		public ~this()
		{
			Debug.Assert(mRefCount == 0);
		}

        public virtual void Serialize(StructuredData data)
        {
			if (!String.IsNullOrEmpty(mName))
            	data.Add("Name", mName);
            if (!String.IsNullOrEmpty(mComment))
                data.Add("Comment", mComment);
        }

        public virtual void Deserialize(StructuredData data)
        {
            data.GetString("Name", mName);
            data.GetString("Comment", mComment);
        }

        public static int NameSort(ProjectItem item1, ProjectItem item2)
        {
            return item1.mName.CompareTo(item2.mName);
        }

        public static int Compare(ProjectItem left, ProjectItem right)
        {            
            int32 leftType = (left is ProjectFolder) ? 0 : 1;
            int32 rightType = (right is ProjectFolder) ? 0 : 1;
            if (leftType - rightType != 0)
                return leftType - rightType;
            return String.Compare(left.mName, right.mName, true);
        }

        public virtual void Dispose()
        {

        }

		public virtual bool HasNonAuto()
		{
			return mIncludeKind != .Auto;
		}

		public bool IsIgnored()
		{
			if (mIncludeKind == .Ignore)
				return true;
			if (mParentFolder == null)
				return false;
			return mParentFolder.IsIgnored();
		}

		public virtual CategoryKind GetCategoryKind()
		{
			return .None;
		}

		public virtual void Detach()
		{
			mDetached = true;
			ReleaseRef();
		}
    }

	public class ProjectFileItem : ProjectItem
	{
		public String mPath ~ delete _;
		public bool mIsWatching;

		public override bool IncludeInMap
		{
			get
			{
				if ((mPath == null) || (mParentFolder.mPath == null))
					return true;
				String dirPath = scope .();
				Path.GetDirectoryPath(mPath, dirPath);
				return Path.Equals(dirPath, mParentFolder.mPath);
			}
		}

		public this()
		{

		}

		public ~this()
		{
			if ((mIsWatching) && (!IDEApp.sApp.mShuttingDown))
				StopWatching();
		}

		public void GetFullImportPath(String outFullPath)
		{
			if (mProject.IsSingleFile)
			{
				if (gApp.mWorkspace.mCompositeFile.IsIncluded(mName))
				{
					outFullPath.Append(mName);
					return;
				}
			}

			if (mPath != null)
			{
				mProject.GetProjectFullPath(mPath, outFullPath);
				IDEUtils.FixFilePath(outFullPath);
			}
			else if (mParentFolder != null)
			{
				mParentFolder.GetFullImportPath(outFullPath);
				outFullPath.Append(Path.DirectorySeparatorChar);
				outFullPath.Append(mName);
			}
		}

		public override void Deserialize(StructuredData data)
		{
		    base.Deserialize(data);

			if (mPath == null)
			{
				mPath = new String();
			    data.GetString("Path", mPath);
				if (mPath.IsEmpty) //TODO: Temporary
					data.GetString("ImportPath", mPath);
				IDEUtils.FixFilePath(mPath);
				if ((mPath.IsEmpty) && (mParentFolder != null))
				{
					mPath.Append(mParentFolder.mPath);
					mPath.Append(Path.DirectorySeparatorChar);
					mPath.Append(mName);
				}

				if (mName.IsEmpty)
					Path.GetFileName(mPath, mName);
			}
		    //AddToResourceManager();
		}

		public override void Serialize(StructuredData data)
		{
			bool wantName = true;

			if (mPath != null)
			{
				String predictedPath = scope String();
				if (mParentFolder != null)
				{
					predictedPath.Append(mParentFolder.mPath);
					predictedPath.Append(Path.DirectorySeparatorChar);
					predictedPath.Append(mName);
				}

				if (mPath != predictedPath)
				{
					String writePath = scope .(mPath);
					IDEUtils.CanonicalizeFilePath(writePath);
			    	data.Add("Path", writePath);

					String fileName = scope .();
					Path.GetFileName(mPath, fileName);
					wantName = fileName != mName;
				}
			}

			if (wantName)
				data.Add("Name", mName);
		}

		public virtual void StartWatching()
		{
#if !CLI
			Debug.Assert(!mIsWatching);

			var fullPath = scope String();
			GetFullImportPath(fullPath);
			IDEApp.sApp.mFileWatcher.WatchFile(fullPath, this);
			mIsWatching = true;
#endif
		}

		public virtual void StopWatching()
		{
#if !CLI
			Debug.Assert(mIsWatching);

			var fullPath = scope String();
			GetFullImportPath(fullPath);
			IDEApp.sApp.mFileWatcher.RemoveWatch(fullPath, this);
			mIsWatching = false;
#endif
		}

		public virtual void OnRename(String oldPath, String newPath)
		{

		}

		public virtual void Rename(String newName, bool changePath = true)
		{
			bool wasWatching = mIsWatching;
			if ((wasWatching) && (changePath))
				StopWatching();

			String dirName = scope String();
			String fileName = scope String();
			Path.GetDirectoryPath(mPath, dirName);
			Path.GetFileName(mPath, fileName);

			bool didNameMatch = mName == fileName;

			if (IncludeInMap)
			{
				mParentFolder.mChildMap.Remove(mName);
				mName.Set(newName);
				bool added = mParentFolder.mChildMap.TryAdd(mName, this);
				Debug.Assert(added);
			}

			if ((didNameMatch) && (changePath))
			{
				String newPath = scope String();
				newPath..Append(dirName)
					..Append(Path.DirectorySeparatorChar)
					..Append(newName);

				OnRename(mPath, newPath);

				mPath.Set(newPath);
			}

			if ((wasWatching) && (changePath))
				StartWatching();
		}

		public void RecalcPath()
		{
			mPath.Clear();
			mPath.Append(mParentFolder.mPath);
			mPath.Append(Path.DirectorySeparatorChar);
			mPath.Append(mName);
		}
	}

    public class ProjectSource : ProjectFileItem
    {
        // 'Unsaved content' is for when we compile from an unsaved file
        //public string mUnsavedContent;
        //public IdSpan mUnsavedCharIdData;
        public FileEditData mEditData;

		public bool mHasChangedSinceLastCompile = true;
		public bool mWasBuiltWithOldHash;
		public bool mHasChangedSinceLastSuccessfulCompile = true;
		public bool mLoadFailed;
		public bool HasChangedSinceLastCompile
		{
			get
			{
				return mHasChangedSinceLastCompile;
			}

			set
			{
				mHasChangedSinceLastCompile = value;
				if (value)
					mHasChangedSinceLastSuccessfulCompile = true;
			}
		}

        public bool IsBeefFile
        {
            get
            {
                return IDEApp.IsBeefFile(mPath);
            }
        }

		public this()
		{

		}

		public ~this()
		{
			Dispose();
		}

		public override CategoryKind GetCategoryKind()
		{
			if (mIncludeKind != .Manual)
				return .None;

			if (mParentFolder.mPath == null)
				return .SimpleSource;

			var expectPath = scope String();
			expectPath.Append(mParentFolder.mPath);
			expectPath.Append(Path.DirectorySeparatorChar);
			expectPath.Append(mName);

			if (mPath != expectPath)
				return .None;

			return .SimpleSource;
		}

		public void ClearEditData()
		{
			if (mEditData != null)
			{
				mEditData.mProjectSources.Remove(this);
			    mEditData.Deref();
				mEditData = null;
			}
		}

		public override void Dispose()
		{
			ClearEditData();
		}

		public override void Detach()
		{
			Dispose();
			base.Detach();
		}

		public override void Serialize(StructuredData data)
		{
			if ((mProject.IsSingleFile) && (mName == CompositeFile.sMainFileName))
				return;

			data.Add("Type", (mIncludeKind == .Ignore) ? "IgnoreSource" : "Source");
		    base.Serialize(data);
		}

        /*public void AddToResourceManager()
        {
            ResImageInfo resInfo = new ResImageInfo();
            resInfo.mFilePath = mPath;
            resInfo.mResId = mResId;
            resInfo.mName = mName;
            BFApp.sApp.mResourceManager.mIdToResInfoMap[mResId] = resInfo;
        }*/
		
        /*public override void Dispose()
        {
            if (mEditData != null)
            {
                //for (var entry in mEditData.mEditWidget.mEditWidgetContent.mUndoManager.)
                //mEditData.mEditWidget.mEditWidgetContent.mUndoManager.Dispose();
                mEditData.mEditWidget.mEditWidgetContent.mData.mUndoManager.WithActions(scope (undoAction) =>
                    {
                        if (undoAction is GlobalUndoAction)
                        {
                            var globalUndoAction = (GlobalUndoAction)undoAction;
                            globalUndoAction.Detach();
                        }
                    });
            }
        }*/
    }

    public class ProjectComposition : ProjectItem
    {
        public Composition mComposition;

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);
            using (data.CreateObject("Composition"))
            {
                
            }
        }

        public override void Deserialize(StructuredData data)
        {
            base.Deserialize(data);

            using (data.Open("Composition"))
            {

            }
        }
    }

    public class ProjectFolder : ProjectFileItem
    {
        public List<ProjectItem> mChildItems = new List<ProjectItem>() ~
			{
				for (var projectItem in mChildItems)
				{
					projectItem.Detach();
				}
				delete _;
			};
		public Dictionary<String, ProjectItem> mChildMap = new Dictionary<String, ProjectItem>() ~ delete _;
        //public String mLastImportDir = new String() ~ delete _;
		public bool mAutoInclude;
		public bool mSortDirty;

		public this()
		{

		}

		public ~this()
		{
			if ((mIsWatching) && (!gApp.mShuttingDown))
				StopWatching();
		}

		public override void Dispose()
		{
			for (var item in mChildItems)
			{
				item.Dispose();
			}
		}

		public void GetFullDisplayName(String displayName)
		{
			if (mParentFolder == null)
				return;

			if (mParentFolder.mParentFolder != null)
			{
				mParentFolder.mParentFolder.GetFullDisplayName(displayName);
				displayName.Append("/");
			}
			displayName.Append(mName);
		}

        public void GetRelDir(String path)
        {
			if (mPath != null)
			{
				path.Append(mPath);
				return;
			}

            if ((mParentFolder != null) && (mParentFolder.mName.Length > 0))
			{
				var relDirStr = scope String();
				mParentFolder.GetRelDir(relDirStr);
                path..Append(relDirStr)
					..Append(Path.DirectorySeparatorChar)
					..Append(mName);
			}
			else
            	path.Append(mName);
        }

        public virtual void SortItems()
        {
			if (mSortDirty)
			{
	            mChildItems.Sort(scope => ProjectItem.Compare);
				mSortDirty = false;
			}

            for (var item in mChildItems)
            {
                var projectFolder = item as ProjectFolder;
                if (projectFolder != null)                
                    projectFolder.SortItems();
            }
        }

		public void MarkAsUnsorted()
		{
			mSortDirty = true;
		}

		public bool IsAutoInclude()
		{
			//return (mAutoInclude != .Never) &&
				//((mAutoInclude == .Always) || (mIncludeKind == .Auto));

			return mAutoInclude;
		}

        public virtual void AddChildAtIndex(int index, ProjectItem item)
        {
			item.mParentFolder = this;

			if ((mIsWatching) && (var projectFileItem = item as ProjectFileItem))
            {
				projectFileItem.StartWatching();
            }
			
            mChildItems.Insert(index, item);

			if (item.IncludeInMap)
			{
				bool added = mChildMap.TryAdd(item.mName, item);
				Debug.Assert(added);
			}
        }

        public virtual void InsertChild(ProjectItem item, ProjectItem insertBefore)
        {
            int pos = (insertBefore != null) ? mChildItems.IndexOf(insertBefore) : mChildItems.Count;
            AddChildAtIndex(pos, item);
        }

        public virtual void AddChild(ProjectItem item, ProjectItem addAfter = null)
        {
            int pos = (addAfter != null) ? (mChildItems.IndexOf(addAfter) + 1) : mChildItems.Count;
            AddChildAtIndex(pos, item);
        }

        public virtual void RemoveChild(ProjectItem item)
        {
            var projectFileItem = item as ProjectFileItem;
            if (projectFileItem != null)
            {
                if (projectFileItem.mIsWatching)
					projectFileItem.StopWatching();
            }

			if (item.IncludeInMap)
				mChildMap.Remove(item.mName);
            mChildItems.Remove(item);
            item.mParentFolder = null;
        }

		public override bool HasNonAuto()
		{
			if (mAutoInclude != (mIncludeKind == .Auto))
				return true;

			if (mIncludeKind != .Auto)
				return true;

			for (let child in mChildItems)
			{
				if (child.HasNonAuto())
					return true;
			}
			return false;
		}

		public bool HasAlias()
		{
			if (mPath == null)
				return false;
			if (!mPath.EndsWith(mName))
				return true;
			if (mPath.Length > mName.Length)
			{
				// Make sure it's not just a partial match 
                char8 slashChar = mPath[mPath.Length - mName.Length];
				return (slashChar == '\\') || (slashChar == '/');
			}
			return false;
		}

        public override void Serialize(StructuredData data)
        {
			if (!HasNonAuto())
				return;

			if (mParentFolder != null)
			{
				data.Add("Type", (mIncludeKind == .Ignore) ? "IgnoreFolder" : (mIncludeKind == .Auto) ? "AutoFolder" : "Folder");
	            base.Serialize(data);
				if (mAutoInclude != (mIncludeKind == .Auto))
					data.ConditionalAdd("AutoInclude", mAutoInclude, mIncludeKind == .Auto);
			}
			if (!mChildItems.IsEmpty)
			{
				for (CategoryKind categoryKind = .None; categoryKind <= .SimpleSource; categoryKind++)
				{
					IDisposable arrayCloseItem = null;
					
				    for (ProjectItem item in mChildItems)
				    {
						if (!item.HasNonAuto())
							continue;
						if (item.GetCategoryKind() != categoryKind)
							continue;
						
						if (categoryKind == .SimpleSource)
						{
							if ((mProject.IsSingleFile) && (item.mName == CompositeFile.sMainFileName))
								continue;
							if (arrayCloseItem == null)
								arrayCloseItem = data.CreateArray("Source", true);
							data.Add(item.mName);
						}
						else
						{
							if (arrayCloseItem == null)
								arrayCloseItem = data.CreateArray("Items");
							using (data.CreateObject())
								item.Serialize(data);
						}
				    }

					if (arrayCloseItem != null)
						arrayCloseItem.Dispose();
				}
			}
        }

        public override void Deserialize(StructuredData data)
        {
            base.Deserialize(data);

            //mLastImportDir.Clear();
            //data.GetString("LastImportDir", mLastImportDir);

			//var outer = data.Current;

			bool doPopulate = false;

			bool autoInclude = data.GetBool("AutoInclude", mIncludeKind == .Auto);
			if ((autoInclude) && (!mAutoInclude))
				doPopulate = true;
			mAutoInclude = autoInclude;

            for (data.Enumerate("Items"))
            {
                ProjectItem projectItem = null;
													
                String type = scope String();
                data.GetString("Type", type);
				String name = scope String();
				data.GetString("Name", name);

				mChildMap.TryGetValue(name, out projectItem);

				if (projectItem == null)
				{
                    if (type == "Source")
					{
                        projectItem = new ProjectSource();
						projectItem.mIncludeKind = .Manual;
					}
					else if (type == "IgnoreSource")
					{
						projectItem = new ProjectSource();
						projectItem.mIncludeKind = .Ignore;
					}
                    else if (type == "Folder")
					{
                        projectItem = new ProjectFolder();
						projectItem.mIncludeKind = .Manual;
					}
					else if (type == "AutoFolder")
					{
					    projectItem = new ProjectFolder();
						projectItem.mIncludeKind = .Auto;
					}
					else if (type == "IgnoreFolder")
					{
						projectItem = new ProjectFolder();
						projectItem.mIncludeKind = .Ignore;
					}
					else
						continue;
					
                    projectItem.mProject = mProject;
					projectItem.mParentFolder = this;
					projectItem.Deserialize(data);
                    AddChild(projectItem);
				}
				else
				{
					if ((type == "IgnoreSource") ||
						(type == "IgnoreFolder"))
						projectItem.mIncludeKind = .Ignore;

					projectItem.Deserialize(data);
				}
            }

			for (data.Enumerate("Source"))
			{
				let projectItem = new ProjectSource();
				data.GetCurString(projectItem.mName);
				projectItem.mIncludeKind = .Manual;
				projectItem.mProject = mProject;
				projectItem.mParentFolder = this;
				var path = new String()
					..Append(mPath)
					..Append(Path.DirectorySeparatorChar)
					..Append(projectItem.mName);
				projectItem.mPath = path;
				AddChild(projectItem);
			}

			if (doPopulate)
				Populate(mPath);
        }
		
		public void Populate(String relDir)
		{
			mAutoInclude = true;

			var dirPath = scope String(mProject.mProjectDir);
            dirPath.Append(Path.DirectorySeparatorChar);
			dirPath.Append(relDir);

			for (let fileEntry in Directory.EnumerateFiles(dirPath))
			{
				String fileName = scope String();
				fileEntry.GetFileName(fileName);

				if ((!gApp.IsFilteredOut(fileName)) && (!mChildMap.ContainsKey(fileName)))
				{
					let projectItem = new ProjectSource();
					projectItem.mProject = mProject;
					projectItem.mName.Set(fileName);

					projectItem.mPath = new String(relDir);
					projectItem.mPath.Append(Path.DirectorySeparatorChar);
					projectItem.mPath.Append(fileName);
					AddChild(projectItem);
				}
			}

			for (let fileEntry in Directory.EnumerateDirectories(dirPath))
			{
				String dirName = scope String();
				fileEntry.GetFileName(dirName);

				// Why was this here?
				/*if (dirName == "build")
					continue;*/

				if (mChildMap.ContainsKey(dirName))
					continue;

				let newRelDir = scope String(relDir);
				if (!newRelDir.IsEmpty)
					newRelDir.Append(Path.DirectorySeparatorChar);
				newRelDir.Append(dirName);

				let projectFolder = new ProjectFolder();
				projectFolder.mProject = mProject;
				projectFolder.mName.Set(dirName);

				projectFolder.mPath = new String(relDir)
					..Append(Path.DirectorySeparatorChar)
					..Append(dirName);

				AddChild(projectFolder);

				projectFolder.Populate(newRelDir);
			}
		}

		public override void StartWatching()
		{
			//base.StartWatching();
			Debug.Assert(!mIsWatching);
			mIsWatching = true;

			// Add self as a watch
			var fullPath = scope String();
			GetFullImportPath(fullPath);
			fullPath.Append(Path.DirectorySeparatorChar);
			IDEApp.sApp.mFileWatcher.WatchFile(fullPath, this);

			for (var child in mChildItems)
				if (let childFileItem = child as ProjectFileItem)
					if (!childFileItem.mIsWatching)
						childFileItem.StartWatching();
		}

		public override void StopWatching()
		{
			base.StopWatching();

			var fullPath = scope String();
			GetFullImportPath(fullPath);
			fullPath.Append(Path.DirectorySeparatorChar);
			IDEApp.sApp.mFileWatcher.RemoveWatch(fullPath, this);

			for (var child in mChildItems)
				if (let childFileItem = child as ProjectFileItem)
					if (childFileItem.mIsWatching)
						childFileItem.StopWatching();
		}

		public override void OnRename(String oldPath, String newPath)
		{
			for (var child in mChildItems)
				if (let childFileItem = child as ProjectFileItem)
				{
					if (childFileItem.mPath.StartsWith(oldPath, .OrdinalIgnoreCase))
					{
						var newChildPath = scope String();
						newChildPath.Append(newPath);
						newChildPath.Append(childFileItem.mPath, oldPath.Length);

						childFileItem.OnRename(childFileItem.mPath, newChildPath);

						String oldFullName = scope String();
						mProject.GetProjectFullPath(childFileItem.mPath, oldFullName);
						String newFullName = scope String();
						mProject.GetProjectFullPath(newChildPath, newFullName);

						IDEApp.sApp.FileRenamed(childFileItem, oldFullName, newFullName);

						childFileItem.mPath.Set(newChildPath);
					}
				}
		}
    }

	[Reflect(.StaticFields | .NonStaticFields | .ApplyToInnerTypes)]
    public class Project
    {
        class ConfigWriteData
        {
            public String mPlatform;
            public String mOutputDir;
            public StructuredData mData;
        }

		public enum BuildKind
		{
			case Normal;
			case Test;
			case StaticLib;
			case DynamicLib;
			case Intermediate;
			case NotSupported;
		}

        public enum COptimizationLevel
        {
            O0,
            O1,
            O2,
            O3,            
            Ofast,
            Og,

            FromWorkspace
        }

        public enum CCompilerType
        {
            Clang,
            GCC          
        }

        public enum CLibType
        {
			None,
            Dynamic,
			Static,
			DynamicDebug,
			StaticDebug,
			SystemMSVCRT,
        }

		public enum BeefLibType
		{
			Dynamic,
			DynamicDebug,
			Static,
		}

        public enum TargetType
        {
			case BeefConsoleApplication,
				 BeefGUIApplication,
				 BeefLib,
				 CustomBuild,
				 BeefTest,
				 C_ConsoleApplication,
				 C_GUIApplication,
				 BeefApplication_StaticLib,
				 BeefApplication_DynamicLib,
				 BeefLib_Static,
				 BeefLib_Dynamic;

		 	public bool IsBeef
		 	{
				get
				{
					switch (this)
					{
					case BeefConsoleApplication,
						 BeefGUIApplication,
						 BeefLib,
						 BeefTest:
						return true;
					default:
						return false;
					}
				}
			}

			public bool IsBeefApplication
			{
				get
				{
					switch (this)
					{
					case BeefConsoleApplication,
						 BeefGUIApplication:
						return true;
					default:
						return false;
					}
				}
			}

			public bool IsBeefApplicationOrLib
			{
				get
				{
					switch (this)
					{
					case BeefConsoleApplication,
						 BeefGUIApplication,
						 BeefLib:
						return true;
					default:
						return false;
					}
				}
			}

			public bool IsLib
			{
				get
				{
					switch (this)
					{
					case BeefLib:
						return true;
					default:
						return false;
					}
				}
			}
        }

		public class WindowsOptions
		{
			[Reflect]
			public String mIconFile = new String() ~ delete _;
			[Reflect]
			public String mManifestFile = new String() ~ delete _;

			[Reflect]
			public String mDescription = new String() ~ delete _;
			[Reflect]
			public String mComments = new String() ~ delete _;
			[Reflect]
			public String mCompany = new String() ~ delete _;
			[Reflect]
			public String mProduct = new String() ~ delete _;
			[Reflect]
			public String mCopyright = new String() ~ delete _;
			[Reflect]
			public String mFileVersion = new String() ~ delete _;
			[Reflect]
			public String mProductVersion = new String() ~ delete _;

			public bool HasVersionInfo()
			{
				return 
					!mDescription.IsWhiteSpace ||
					!mComments.IsWhiteSpace ||
					!mCompany.IsWhiteSpace ||
					!mProduct.IsWhiteSpace ||
					!mCopyright.IsWhiteSpace ||
					!mFileVersion.IsWhiteSpace ||
					!mProductVersion.IsWhiteSpace;
			}

			public void Deserialize(StructuredData data)
			{
				data.GetString("IconFile", mIconFile);
				data.GetString("ManifestFile", mManifestFile);
				data.GetString("Description", mDescription);
				data.GetString("Comments", mComments);
				data.GetString("Company", mCompany);
				data.GetString("Product", mProduct);
				data.GetString("Copyright", mCopyright);
				data.GetString("FileVersion", mFileVersion);
				data.GetString("ProductVersion", mProductVersion);
			}

			public void Serialize(StructuredData data)
			{
				data.ConditionalAdd("IconFile", mIconFile);
				data.ConditionalAdd("ManifestFile", mManifestFile);
				data.ConditionalAdd("Description", mDescription);
				data.ConditionalAdd("Comments", mComments);
				data.ConditionalAdd("Company", mCompany);
				data.ConditionalAdd("Product", mProduct);
				data.ConditionalAdd("Copyright", mCopyright);
				data.ConditionalAdd("FileVersion", mFileVersion);
				data.ConditionalAdd("ProductVersion", mProductVersion);
			}			
		}

		public class LinuxOptions
		{
			[Reflect]
			public String mOptions = new String() ~ delete _;

			public void Deserialize(StructuredData data)
			{
				data.GetString("Options", mOptions);
			}

			public void Serialize(StructuredData data)
			{
				data.ConditionalAdd("Options", mOptions);
			}
		}

		public class WasmOptions
		{
			[Reflect]
			public bool mEnableThreads = false;

			public void Deserialize(StructuredData data)
			{
				mEnableThreads = data.GetBool("EnableThreads", false);
			}

			public void Serialize(StructuredData data)
			{
				data.ConditionalAdd("EnableThreads", mEnableThreads, false);
			}
		}

        public class GeneralOptions
        {
			[Reflect]
            public TargetType mTargetType;
			[Reflect]
			public List<String> mAliases = new .() ~ DeleteContainerAndItems!(_);
        }

		public class BeefGlobalOptions
		{
			[Reflect]
			public String mStartupObject = new String("") ~ delete _;
			[Reflect]
			public String mDefaultNamespace = new String() ~ delete _;
			[Reflect]
			public List<String> mPreprocessorMacros = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<DistinctBuildOptions> mDistinctBuildOptions = new List<DistinctBuildOptions>() ~ DeleteContainerAndItems!(_);
		}

		public enum BuildCommandTrigger
		{
			Never,
			IfFilesChanged,
			Always,
		}

		public class ProjectBuildOptions
		{
			[Reflect]
			public BuildKind mBuildKind;
			[Reflect]
		    public String mTargetDirectory = new String("$(BuildDir)") ~ delete _;
			[Reflect]
		    public String mTargetName = new String("$(ProjectName)") ~ delete _;
			[Reflect]
		    public String mOtherLinkFlags = new String("") ~ delete _;
			[Reflect]
		    public CLibType mCLibType = .Static;
			[Reflect]
			public BeefLibType mBeefLibType = .Static;
			[Reflect]
			public int32 mStackSize;

			[Reflect]
			public BuildCommandTrigger mBuildCommandsOnCompile = .Always;
			[Reflect]
			public BuildCommandTrigger mBuildCommandsOnRun = .Always;
			[Reflect]
			public List<String> mLibPaths = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<String> mLinkDependencies = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<String> mPreBuildCmds = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<String> mPostBuildCmds = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public List<String> mCleanCmds = new List<String>() ~ DeleteContainerAndItems!(_);
		}

        public class DebugOptions
        {
			[Reflect]
            public String mCommand = new String("$(TargetPath)") ~ delete _;
			[Reflect]
            public String mCommandArguments = new String("") ~ delete _;
			[Reflect]
            public String mWorkingDirectory = new String("$(ProjectDir)") ~ delete _;
			[Reflect]
            public List<String> mEnvironmentVars = new List<String>() ~ DeleteContainerAndItems!(_);
        }

        public class BeefOptions
        {
			[Reflect]
            public List<String> mPreprocessorMacros = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
			public BuildOptions.RelocType mRelocType;
			[Reflect]
			public BuildOptions.PICLevel mPICLevel;
			[Reflect]
            public BuildOptions.BfOptimizationLevel? mOptimizationLevel;
			[Reflect]
			public BuildOptions.LTOType? mLTOType;
			[Reflect]
            public bool mMergeFunctions;
			[Reflect]
            public bool mCombineLoads;
			[Reflect]
            public bool mVectorizeLoops;
			[Reflect]
            public bool mVectorizeSLP;
			[Reflect]
			public List<DistinctBuildOptions> mDistinctBuildOptions = new List<DistinctBuildOptions>() ~ DeleteContainerAndItems!(_);
        }

        public class COptions
        {
			[Reflect]
            public CCompilerType mCompilerType = CCompilerType.Clang;
			[Reflect]
            public String mOtherCFlags = new String("") ~ delete _;
			[Reflect]
            public String mOtherCPPFlags = new String("") ~ delete _;
			[Reflect]
            public List<String> mIncludePaths = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
            public bool mEnableBeefInterop;
			[Reflect]
            public List<String> mPreprocessorMacros = new List<String>() ~ DeleteContainerAndItems!(_);

			[Reflect]
            public bool mDisableExceptions;
			[Reflect]
            public BuildOptions.SIMDSetting? mSIMD;
			[Reflect]
            public bool mGenerateLLVMAsm;
			[Reflect]
            public bool mNoOmitFramePointers;
			[Reflect]
            public bool mDisableInlining;
			[Reflect]
            public bool mStrictAliasing;
			[Reflect]
            public bool mFastMath;
			[Reflect]
            public bool mDisableRTTI;
			[Reflect]
            public COptimizationLevel mOptimizationLevel = .FromWorkspace;
			[Reflect]
            public bool mEmitDebugInfo;
			[Reflect]
            public String mAddressSanitizer = new String("") ~ delete _; // TODO: Add proper options            

			[Reflect]
            public bool mAllWarnings;
			[Reflect]
            public bool mEffectiveCPPViolations;
			[Reflect]
            public bool mPedantic;
			[Reflect]
            public bool mWarningsAsErrors;
			[Reflect]
            public List<String> mSpecificWarningsAsErrors = new List<String>() ~ DeleteContainerAndItems!(_);
			[Reflect]
            public List<String> mDisableSpecificWarnings = new List<String>() ~ DeleteContainerAndItems!(_);

			public ~this()
			{
			}
        }
        
        public class Options
        {
			[Reflect]
            public ProjectBuildOptions mBuildOptions = new ProjectBuildOptions() ~ delete _;
			[Reflect]
            public BeefOptions mBeefOptions = new BeefOptions() ~ delete _;
			[Reflect]
            public COptions mCOptions = new COptions() ~ delete _;
			[Reflect]
            public DebugOptions mDebugOptions = new DebugOptions() ~ delete _;

			public List<String> mClangObjectFiles ~ DeleteContainerAndItems!(_);

			public Options Duplicate()
			{
				var newOptions = new Options();

				Set!(newOptions.mBuildOptions.mTargetDirectory, mBuildOptions.mTargetDirectory);
				Set!(newOptions.mBuildOptions.mTargetName, mBuildOptions.mTargetName);
				Set!(newOptions.mBuildOptions.mOtherLinkFlags, mBuildOptions.mOtherLinkFlags);
				Set!(newOptions.mBuildOptions.mCLibType, mBuildOptions.mCLibType);
				Set!(newOptions.mBuildOptions.mLibPaths, mBuildOptions.mLibPaths);
				Set!(newOptions.mBuildOptions.mLinkDependencies, mBuildOptions.mLinkDependencies);
				Set!(newOptions.mBuildOptions.mPreBuildCmds, mBuildOptions.mPreBuildCmds);
				Set!(newOptions.mBuildOptions.mPostBuildCmds, mBuildOptions.mPostBuildCmds);
				Set!(newOptions.mBuildOptions.mCleanCmds, mBuildOptions.mCleanCmds);

				Set!(newOptions.mBeefOptions.mPreprocessorMacros, mBeefOptions.mPreprocessorMacros);
				Set!(newOptions.mBeefOptions.mOptimizationLevel, mBeefOptions.mOptimizationLevel);
				Set!(newOptions.mBeefOptions.mLTOType, mBeefOptions.mLTOType);
				Set!(newOptions.mBeefOptions.mRelocType, mBeefOptions.mRelocType);
				Set!(newOptions.mBeefOptions.mPICLevel, mBeefOptions.mPICLevel);
				Set!(newOptions.mBeefOptions.mMergeFunctions, mBeefOptions.mMergeFunctions);
				Set!(newOptions.mBeefOptions.mCombineLoads, mBeefOptions.mCombineLoads);
				Set!(newOptions.mBeefOptions.mVectorizeLoops, mBeefOptions.mVectorizeLoops);
				Set!(newOptions.mBeefOptions.mVectorizeSLP, mBeefOptions.mVectorizeSLP);
				for (var prev in mBeefOptions.mDistinctBuildOptions)
					newOptions.mBeefOptions.mDistinctBuildOptions.Add(prev.Duplicate());

				Set!(newOptions.mCOptions.mCompilerType, mCOptions.mCompilerType);
				Set!(newOptions.mCOptions.mOtherCFlags, mCOptions.mOtherCFlags);
				Set!(newOptions.mCOptions.mOtherCPPFlags, mCOptions.mOtherCPPFlags);
				Set!(newOptions.mCOptions.mIncludePaths, mCOptions.mIncludePaths);
				Set!(newOptions.mCOptions.mEnableBeefInterop, mCOptions.mEnableBeefInterop);
				Set!(newOptions.mCOptions.mPreprocessorMacros, mCOptions.mPreprocessorMacros);
				Set!(newOptions.mCOptions.mDisableExceptions, mCOptions.mDisableExceptions);
				Set!(newOptions.mCOptions.mSIMD, mCOptions.mSIMD);
				Set!(newOptions.mCOptions.mGenerateLLVMAsm, mCOptions.mGenerateLLVMAsm);
				Set!(newOptions.mCOptions.mNoOmitFramePointers, mCOptions.mNoOmitFramePointers);
				Set!(newOptions.mCOptions.mDisableInlining, mCOptions.mDisableInlining);
				Set!(newOptions.mCOptions.mStrictAliasing, mCOptions.mStrictAliasing);
				Set!(newOptions.mCOptions.mFastMath, mCOptions.mFastMath);
				Set!(newOptions.mCOptions.mDisableRTTI, mCOptions.mDisableRTTI);
				Set!(newOptions.mCOptions.mOptimizationLevel, mCOptions.mOptimizationLevel);
				Set!(newOptions.mCOptions.mEmitDebugInfo, mCOptions.mEmitDebugInfo);
				Set!(newOptions.mCOptions.mAddressSanitizer, mCOptions.mAddressSanitizer);
				Set!(newOptions.mCOptions.mAllWarnings, mCOptions.mAllWarnings);
				Set!(newOptions.mCOptions.mEffectiveCPPViolations, mCOptions.mEffectiveCPPViolations);
				Set!(newOptions.mCOptions.mPedantic, mCOptions.mPedantic);
				Set!(newOptions.mCOptions.mWarningsAsErrors, mCOptions.mWarningsAsErrors);
				Set!(newOptions.mCOptions.mSpecificWarningsAsErrors, mCOptions.mSpecificWarningsAsErrors);
				Set!(newOptions.mCOptions.mDisableSpecificWarnings, mCOptions.mDisableSpecificWarnings);

				Set!(newOptions.mDebugOptions.mCommand, mDebugOptions.mCommand);
				Set!(newOptions.mDebugOptions.mCommandArguments, mDebugOptions.mCommandArguments);
				Set!(newOptions.mDebugOptions.mWorkingDirectory, mDebugOptions.mWorkingDirectory);
				Set!(newOptions.mDebugOptions.mEnvironmentVars, mDebugOptions.mEnvironmentVars);

				return newOptions;
			}
        }

        public class Config
        {
            public Dictionary<String, Options> mPlatforms = new Dictionary<String, Options>() ~ DeleteDictionaryAndKeysAndValues!(_);
        }

		public class Dependency
		{
			public VerSpec mVerSpec ~ _.Dispose();
			public String mProjectName ~ delete _;
		}

		public enum DeferState
		{
			None,
			ReadyToLoad,
			Pending,
			Searching
		}

		public class VerReference
		{
			public String mSrcProjectName ~ delete _;
			public VerSpec mVerSpec ~ _.Dispose();
		}

		public Monitor mMonitor = new Monitor() ~ delete _;
        public String mNamespace = new String() ~ delete _;
        public String mProjectDir = new String() ~ delete _;
        public String mProjectName = new String() ~ delete _;
        public String mProjectPath = new String() ~ delete _;
		public DeferState mDeferState;
		public List<VerReference> mVerReferences = new .() ~ DeleteContainerAndItems!(_);

        //public String mLastImportDir = new String() ~ delete _;
        public bool mHasChanged;
        public bool mNeedsTargetRebuild;
		public bool mForceCustomCommands;
		public bool mEnabled = true;
		public bool mLocked;
		public bool mLockedDefault;
		public bool mDeleted;

        public int32 [] mColorDialogCustomColors;

        public ProjectFolder mRootFolder ~ mRootFolder.ReleaseRef();

        public int32 mCurResVer;
        public int32 mLastGeneratedResVer;

        public Dictionary<String, Config> mConfigs = new Dictionary<String, Config>() ~ DeleteDictionaryAndKeysAndValues!(_);
		public GeneralOptions mGeneralOptions = new GeneralOptions() ~ delete _;
		public BeefGlobalOptions mBeefGlobalOptions = new BeefGlobalOptions() ~ delete _;

		// Platform-dependent options
		public WindowsOptions mWindowsOptions = new WindowsOptions() ~ delete _;
		public LinuxOptions mLinuxOptions = new LinuxOptions() ~ delete _;
		public WasmOptions mWasmOptions = new WasmOptions() ~ delete _;

        public List<Dependency> mDependencies = new List<Dependency>() ~ DeleteContainerAndItems!(_);
		public bool mLastDidBuild;
		public bool mFailed;
		public bool mNeedsCreate;

		public List<String> mCurBfOutputFileNames ~ DeleteContainerAndItems!(_);

        public String ProjectFileName
        {
            get
            {
				Debug.Assert(!mProjectPath.IsEmpty);
                return mProjectPath;
            }
        }

		public bool IsSingleFile
		{
			get
			{
				return (mProjectPath.IsEmpty) && (!mProjectDir.IsEmpty);
			}
		}

		public bool IsDebugSession
		{
			get
			{
				return gApp.mWorkspace.IsDebugSession;
			}
		}

		public bool IsEmpty
		{
			get
			{
				return mRootFolder.mChildItems.IsEmpty;
			}
		}

		void SetupDefaultOptions(Options options)
		{
			options.mBuildOptions.mOtherLinkFlags.Set("$(LinkFlags)");
		}

		public static void GetSanitizedName(String projectName, String outName, bool allowDot = false)
		{
			for (let c in projectName.RawChars)
			{
				if ((c.IsLetterOrDigit) || (c == '_'))
					outName.Append(c);
				else if (c == '-')
					outName.Append('_');
				else if ((c == '.') && (allowDot))
					outName.Append(c);
			}
		}

        public this()
        {
            mRootFolder = new ProjectFolder();
            mRootFolder.mProject = this;

			if (gApp.mWorkspace.IsDebugSession)
				mGeneralOptions.mTargetType = .CustomBuild;
			SetupDefaultConfigs();

			mBeefGlobalOptions.mStartupObject.Set("Program");
        }

		public void SetupDefaultConfigs()
		{
			List<String> platforms = scope List<String>();
			if (IDEApp.sPlatform32Name != null)
				platforms.Add(IDEApp.sPlatform32Name);
			if (IDEApp.sPlatform64Name != null)
				platforms.Add(IDEApp.sPlatform64Name);

			List<String> configs = scope List<String>();
			configs.Add("Debug");
			configs.Add("Release");
			configs.Add("Paranoid");
			configs.Add("Test");

			for (let platformName in platforms)
			{
				for (let configName in configs)
				{
					CreateConfig(configName, platformName);
				}
			}
		}

		public ~this()
		{
		}

        public void GetProjectRelPath(String fullPath, String outRelPath)
        {
			Debug.Assert((Object)fullPath != outRelPath);
            Path.GetRelativePath(fullPath, mProjectDir, outRelPath);
			IDEUtils.FixFilePath(outRelPath);
        }

        public void GetProjectFullPath(String relPath, String outAbsPath)
        {
			Debug.Assert((Object)relPath != outAbsPath);
            Path.GetAbsolutePath(relPath, mProjectDir, outAbsPath);
        }

		public void DeferLoad(StringView path)
		{
			if (!path.IsEmpty)
			{
				mProjectPath.Set(path);
				mDeferState = .ReadyToLoad;
			}
			else
				mDeferState = .Pending;
		}

        public bool Load(StringView path)
        {
			scope AutoBeefPerf("Project.Load");

			mDeferState = .None;

            mLastGeneratedResVer = 0;
            mCurResVer = 0;

			StructuredData structuredData = scope StructuredData();
			if (!mProjectPath.IsEmpty)
			{
				mProjectDir.Clear();
				mProjectPath.Clear();

				if (!Environment.IsFileSystemCaseSensitive)
				{
					Path.GetActualPathName(path, mProjectPath);
				}

				if (mProjectPath.IsEmpty)
				{
					mProjectPath.Set(path);
					IDEUtils.FixFilePath(mProjectPath);
				}

				Path.GetDirectoryPath(mProjectPath, mProjectDir);
	            if (structuredData.Load(ProjectFileName) case .Err)
	                return false;
			}
			else
			{
				if (gApp.StructuredLoad(structuredData, "Project") case .Err)
					return false;
			}
            //Console.WriteLine(structuredData.ToJSON(true));
            Deserialize(structuredData);

			/*if (!mIsLegacyProject)
			{
				Save();
			}*/

			if (mProjectName != null)
			{
				gApp.mWorkspace.mProjectFileEntries.Add(new .(path, mProjectName));
			}

            return true;
        }

        public void Serialize(StructuredData data)
        {
			mRootFolder.SortItems();

            //data.Add("LastImportDir", mLastImportDir);

			String deferredCreateObject = scope String();
			bool isOpened = false;

			void DeferCreateObject(String name)
			{
				deferredCreateObject.Set(name);
			}

			void FinishCreate()
			{
				if (!deferredCreateObject.IsEmpty)
				{
					data.CreateObject("General");
					isOpened = true;
				}
			}

			void TryClose()
			{
				if (isOpened)
				{
					isOpened = false;
				}
			}

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

			if (!IsSingleFile)
				data.Add("FileVersion", 1);

			using (data.CreateObject("Project"))
			{
				if (!IsSingleFile)
					data.Add("Name", mProjectName);
				data.ConditionalAdd("TargetType", mGeneralOptions.mTargetType, GetDefaultTargetType());
				data.ConditionalAdd("StartupObject", mBeefGlobalOptions.mStartupObject, IsSingleFile ? "Program" : "");
				var defaultNamespace = scope String();
				GetSanitizedName(mProjectName, defaultNamespace, true);
				data.ConditionalAdd("DefaultNamespace", mBeefGlobalOptions.mDefaultNamespace, defaultNamespace);
				WriteStrings("Aliases", mGeneralOptions.mAliases);
				WriteStrings("ProcessorMacros", mBeefGlobalOptions.mPreprocessorMacros);
				WriteDistinctOptions(mBeefGlobalOptions.mDistinctBuildOptions);
				data.RemoveIfEmpty();
			}

			bool isDefaultDependencies = false;
			if (mDependencies.Count == 1)
			{
				var dep = mDependencies[0];
				if ((dep.mProjectName == "corlib") &&
					(dep.mVerSpec case .SemVer(let semVer)) &&
					(semVer.mVersion == "*"))
				{
					isDefaultDependencies = true;
				}
			}

			if (!isDefaultDependencies)
			{
				using (data.CreateObject("Dependencies", true))
				{
				    for (var dependency in mDependencies)
				    {
						//let verSpecStr = scope String();
						//dependency.mVerSpec.Serialize(data);
						//data.Add(dependency.mProjectName, verSpecStr);
						dependency.mVerSpec.Serialize(dependency.mProjectName, data);
					}
				}
			}

			using (data.CreateObject("Platform"))
			{
				using (data.CreateObject("Windows"))
				{
                    mWindowsOptions.Serialize(data);
					data.RemoveIfEmpty();
				}

				using (data.CreateObject("Linux"))
				{
				    mLinuxOptions.Serialize(data);
					data.RemoveIfEmpty();
				}

				using (data.CreateObject("Wasm"))
				{
				    mWasmOptions.Serialize(data);
					data.RemoveIfEmpty();
				}

				data.RemoveIfEmpty();
			}

            using (data.CreateObject("Configs"))
            {
                for (var configKeyValue in mConfigs)
                {
                    var config = configKeyValue.value;
					let configName = configKeyValue.key;
#unwarn
					bool isRelease = configName.Contains("Release");
					bool isParanoid = configName.Contains("Paranoid");
					bool isDebug = isParanoid || configName.Contains("Debug");
					bool isTest = configName.Contains("Test");
                    using (data.CreateObject(configName))
                    {
                        for (var platformKeyValue in config.mPlatforms)
                        {
							//let platformName = platformKeyValue.Key;

                            var options = platformKeyValue.value;
                            using (data.CreateObject(platformKeyValue.key))
                            {
								BuildKind defaultBuildKind = .Normal;
								BuildOptions.RelocType defaultRelocType = .NotSet;
								let platformType = Workspace.PlatformType.GetFromName(platformKeyValue.key);
								switch (platformType)
								{
								case .Linux,
									 .Windows,
									 .macOS,
									 .Wasm:
									defaultBuildKind = isTest ? .Test : .Normal;
								default:
									defaultBuildKind = .StaticLib;
								}
								if (platformType == .Android)
									defaultRelocType = .PIC;

								// Build
								data.ConditionalAdd("BuildKind", options.mBuildOptions.mBuildKind, defaultBuildKind);
							    data.ConditionalAdd("TargetDirectory", options.mBuildOptions.mTargetDirectory, "$(BuildDir)");
							    data.ConditionalAdd("TargetName", options.mBuildOptions.mTargetName, "$(ProjectName)");
							    data.ConditionalAdd("OtherLinkFlags", options.mBuildOptions.mOtherLinkFlags, "$(LinkFlags)");
							    data.ConditionalAdd("CLibType", options.mBuildOptions.mCLibType, isRelease ? .Static : .StaticDebug);
								data.ConditionalAdd("BeefLibType", options.mBuildOptions.mBeefLibType, isRelease ? .Static : .Dynamic);
								data.ConditionalAdd("StackSize", options.mBuildOptions.mStackSize, 0);
								data.ConditionalAdd("BuildCommandsOnCompile", options.mBuildOptions.mBuildCommandsOnCompile, .Always);
								data.ConditionalAdd("BuildCommandsOnRun", options.mBuildOptions.mBuildCommandsOnRun, .Always);
								WriteStrings("LibPaths", options.mBuildOptions.mLibPaths);
								WriteStrings("LinkDependencies", options.mBuildOptions.mLinkDependencies);
								WriteStrings("PreBuildCmds", options.mBuildOptions.mPreBuildCmds);
								WriteStrings("PostBuildCmds", options.mBuildOptions.mPostBuildCmds);
								WriteStrings("CleanCmds", options.mBuildOptions.mCleanCmds);
								
								// DebugOptions
							    data.ConditionalAdd("DebugCommand", options.mDebugOptions.mCommand, "$(TargetPath)");
							    data.ConditionalAdd("DebugCommandArguments", options.mDebugOptions.mCommandArguments);
							    data.ConditionalAdd("DebugWorkingDirectory", options.mDebugOptions.mWorkingDirectory, "$(ProjectDir)");
								WriteStrings("EnvironmentVars", options.mDebugOptions.mEnvironmentVars);
								
								// BeefOptions
								bool hasDefaultPreprocs = false;
								if (isRelease)
								{
									if ((options.mBeefOptions.mPreprocessorMacros.Count == 1) &&
										(options.mBeefOptions.mPreprocessorMacros[0] == "RELEASE"))
										hasDefaultPreprocs = true;
								}
								else if (isParanoid)
								{
									if ((options.mBeefOptions.mPreprocessorMacros.Count == 2) &&
										(options.mBeefOptions.mPreprocessorMacros[0] == "DEBUG") &&
										(options.mBeefOptions.mPreprocessorMacros[1] == "PARANOID"))
										hasDefaultPreprocs = true;
								}
								else if (isDebug)
								{
									if ((options.mBeefOptions.mPreprocessorMacros.Count == 1) &&
										(options.mBeefOptions.mPreprocessorMacros[0] == "DEBUG"))
										hasDefaultPreprocs = true;
								}
								else if (isTest)
								{
									if ((options.mBeefOptions.mPreprocessorMacros.Count == 1) &&
										(options.mBeefOptions.mPreprocessorMacros[0] == "TEST"))
										hasDefaultPreprocs = true;
								}
								else
									hasDefaultPreprocs = options.mBeefOptions.mPreprocessorMacros.Count == 0;

								if (!hasDefaultPreprocs)
								{
							        using (data.CreateArray("PreprocessorMacros"))
							        {
							            for (var macro in options.mBeefOptions.mPreprocessorMacros)
							                data.Add(macro);
							        }
								}
								data.ConditionalAdd("RelocType", options.mBeefOptions.mRelocType, defaultRelocType);
								data.ConditionalAdd("PICLevel", options.mBeefOptions.mPICLevel, .NotSet);
							    data.ConditionalAdd("OptimizationLevel", options.mBeefOptions.mOptimizationLevel);
								data.ConditionalAdd("LTOType", options.mBeefOptions.mLTOType);
							    data.ConditionalAdd("MergeFunctions", options.mBeefOptions.mMergeFunctions);
							    data.ConditionalAdd("CombineLoads", options.mBeefOptions.mCombineLoads);
							    data.ConditionalAdd("VectorizeLoops", options.mBeefOptions.mVectorizeLoops);
							    data.ConditionalAdd("VectorizeSLP", options.mBeefOptions.mVectorizeSLP);
								WriteDistinctOptions(options.mBeefOptions.mDistinctBuildOptions);

#if IDE_C_SUPPORT
                                using (data.CreateObject("COptions"))
                                {
                                    data.ConditionalAdd("CompilerType", options.mCOptions.mCompilerType);
                                    data.ConditionalAdd("OtherCFlags", options.mCOptions.mOtherCFlags);
                                    data.ConditionalAdd("OtherCPPFlags", options.mCOptions.mOtherCPPFlags);
									if (options.mCOptions.mIncludePaths.Count > 0)
									{
	                                    using (data.CreateArray("IncludePaths"))
	                                    {
	                                        for (var includePath in options.mCOptions.mIncludePaths)
	                                            data.Add(includePath);
	                                    }
									}
                                    data.ConditionalAdd("EnableBeefInterop", options.mCOptions.mEnableBeefInterop);
									if (options.mCOptions.mPreprocessorMacros.Count > 0)
									{
	                                    using (data.CreateArray("PreprocessorMacros"))
	                                    {
	                                        for (var macro in options.mCOptions.mPreprocessorMacros)
	                                            data.Add(macro);
	                                    }
									}
                                    data.ConditionalAdd("DisableExceptions", options.mCOptions.mDisableExceptions);
                                	data.ConditionalAdd("SIMD", options.mCOptions.mSIMD, .FromWorkspace);
                                	data.ConditionalAdd("GenerateLLVMAsm", options.mCOptions.mGenerateLLVMAsm);
                                	data.ConditionalAdd("NoOmitFramePointers", options.mCOptions.mNoOmitFramePointers);
                                	data.ConditionalAdd("DisableInlining", options.mCOptions.mDisableInlining);
                                	data.ConditionalAdd("StrictAliasing", options.mCOptions.mStrictAliasing);
                                	data.ConditionalAdd("FastMath", options.mCOptions.mFastMath);
                                	data.ConditionalAdd("DisableRTTI", options.mCOptions.mDisableRTTI);
                                	data.ConditionalAdd("OptimizationLevel", options.mCOptions.mOptimizationLevel, .FromWorkspace);
                                	data.ConditionalAdd("EmitDebugInfo", options.mCOptions.mEmitDebugInfo);
									//if (options.mCOptions.mAddressSanitizer)
                                    	//data.Add("AddressSanitizer", options.mCOptions.mAddressSanitizer);
                                    data.ConditionalAdd("AllWarnings", options.mCOptions.mAllWarnings);
                                    data.ConditionalAdd("EffectiveCPPViolations", options.mCOptions.mEffectiveCPPViolations);
                                    data.ConditionalAdd("Pedantic", options.mCOptions.mPedantic);
                                    data.ConditionalAdd("WarningsAsErrors", options.mCOptions.mWarningsAsErrors);
									if (options.mCOptions.mSpecificWarningsAsErrors.Count > 0)
									{
	                                    using (data.CreateArray("SpecificWarningsAsErrors"))
	                                    {
	                                        for (var warning in options.mCOptions.mSpecificWarningsAsErrors)
	                                            data.Add(warning);
	                                    }
									}
									if (options.mCOptions.mDisableSpecificWarnings.Count > 0)
									{
	                                    using (data.CreateArray("DisableSpecificWarnings"))
	                                    {
	                                        for (var warning in options.mCOptions.mDisableSpecificWarnings)
	                                            data.Add(warning);
	                                    }
									}
                                }
#endif

								data.RemoveIfEmpty();
                            }
                        }

						data.RemoveIfEmpty();
                    }
                }

				data.RemoveIfEmpty();
            }

            /*data.Add("LinkFlags", mLinkFlags);
            if (mCompileFlags != null)
            {
                using (data.CreateArray("CompileFlags"))
                {
                    foreach (var str in mCompileFlags)
                        data.Add(str);
                }
            }*/

			bool isDefaultProjectFolder = true;

			if (!IsSingleFile)
			{
				if (mRootFolder.mPath != "src")
					isDefaultProjectFolder = false;
				if (!mRootFolder.mAutoInclude)
					isDefaultProjectFolder = false;
			}

			bool IsDefaultFolder(ProjectFolder folder)
			{
				if (folder.mParentFolder != null)
				{
					if (!folder.mAutoInclude)
						return false;
					if (folder.mIncludeKind != .Auto)
						return false;
				}

				for (var childItem in folder.mChildItems)
				{
					if (var childFolder = childItem as ProjectFolder)
					{
						if (!IsDefaultFolder(childFolder))
							return false;
					}
					if (var childSource = childItem as ProjectSource)
					{
						if ((IsSingleFile) && (childSource.mName == CompositeFile.sMainFileName))
						{
							// Ignore
						}
						else
						{
							if (childSource.mIncludeKind != .Auto)
								return false;
							if (!childSource.mComment.IsEmpty)
								return false;
						}
					}
				}
				return true;
			}

			if (!IsDefaultFolder(mRootFolder))
				isDefaultProjectFolder = false;

			if (!isDefaultProjectFolder)
			{
	            using (data.CreateObject("ProjectFolder"))
	            {
	                mRootFolder.Serialize(data);
					data.RemoveIfEmpty();
	            }            
			}
        }

		TargetType GetDefaultTargetType()
		{
			if (mBeefGlobalOptions.mStartupObject.IsEmpty)
				return TargetType.BeefLib;
			return TargetType.BeefConsoleApplication;
		}

        public void Deserialize(StructuredData data)
        {
			//mLastImportDir.Clear();
            //data.GetString("LastImportDir", mLastImportDir);
            /*mLinkFlags = data.GetString("LinkFlags");
            using (data.Open("CompileFlags"))
            {
                if (data.IsArray)
                {
                    mCompileFlags = new List<string>();
                    for (int i = 0; i < data.Count; i++)
                        mCompileFlags.Add(data.GetString(i));
                }
            }*/

			void ReadStrings(String name, List<String> strs)
			{
				for (data.Enumerate(name))
				{
					String cmd = new String();
					data.GetCurString(cmd);
					strs.Add(cmd);
				}
			}

			bool isBeefDynLib = false;
			using (data.Open("Project"))
			{
				if (!IsSingleFile)
					data.GetString("Name", mProjectName);
				ReadStrings("Aliases", mGeneralOptions.mAliases);
				data.GetString("StartupObject", mBeefGlobalOptions.mStartupObject, IsSingleFile ? "Program" : "");
				var defaultNamespace = scope String();
				GetSanitizedName(mProjectName, defaultNamespace, true);
				data.GetString("DefaultNamespace", mBeefGlobalOptions.mDefaultNamespace, defaultNamespace);
				ReadStrings("ProcessorMacros", mBeefGlobalOptions.mPreprocessorMacros);
				for (data.Enumerate("DistinctOptions"))
				{
					var typeOptions = new DistinctBuildOptions();
					typeOptions.Deserialize(data);
					mBeefGlobalOptions.mDistinctBuildOptions.Add(typeOptions);
				}

				var targetTypeName = scope String();
				data.GetString("TargetType", targetTypeName);
				switch (targetTypeName)
				{ // Handle Legacy names first
				case "BeefWindowsApplication":
					mGeneralOptions.mTargetType = .BeefGUIApplication;
				case "C_WindowsApplication":
					mGeneralOptions.mTargetType = .C_GUIApplication;
				case "BeefDynLib":
					mGeneralOptions.mTargetType = .BeefLib;
					isBeefDynLib = true;
				default:
					mGeneralOptions.mTargetType = data.GetEnum<TargetType>("TargetType", GetDefaultTargetType());
				}
			}

			using (data.Open("Platform"))
			{
				using (data.Open("Windows"))
					mWindowsOptions.Deserialize(data);
				using (data.Open("Linux"))
					mLinuxOptions.Deserialize(data);
				using (data.Open("Wasm"))
					mWasmOptions.Deserialize(data);
			}

			if (!data.Contains("Dependencies"))
			{
				var dep = new Project.Dependency();
				dep.mProjectName = new .("corlib");
				dep.mVerSpec = .SemVer(new .("*"));
				mDependencies.Add(dep);
			}
			else
			{
				for (let depName in data.Enumerate("Dependencies"))
				{
					var dep = new Dependency();
					defer { delete dep; }

					if (dep.mVerSpec.Parse(data) case .Err)
					{
						var err = scope String();
						err.AppendF("Unable to parse version specifier for {0} in {1}", depName, mProjectPath);
						gApp.Fail(err);
						continue;
					}

					dep.mProjectName = new String(depName);
					mDependencies.Add(dep);
					dep = null;
				}
			}

			for (var dep in mDependencies)
			{
				switch (gApp.AddProject(dep.mProjectName, dep.mVerSpec))
				{
				case .Ok(let project):
				case .Err(let err):
					gApp.OutputLineSmart("ERROR: Unable to load project '{0}' specified in project '{1}'", dep.mProjectName, mProjectName);
				}
			}

			DeleteDictionaryAndKeysAndValues!(mConfigs);
			mConfigs = new Dictionary<String, Config>();

			for (var configNameSV in data.Enumerate("Configs"))
            {
				let configName = scope String(configNameSV);

                Config config = new Config();
#unwarn
				bool isRelease = configName.Contains("Release");
				bool isParanoid = configName.Contains("Paranoid");
				bool isDebug = isParanoid || configName.Contains("Debug");
				bool isTest = configName.Contains("Test");
                mConfigs[new String(configName)] = config;                    
                
				for (var platformName in data.Enumerate())
                {
                    Options options = new Options();
					config.mPlatforms[new String(platformName)] = options;

					BuildKind defaultBuildKind = .Normal;
					BuildOptions.RelocType defaultRelocType = .NotSet;
					let platformType = Workspace.PlatformType.GetFromName(platformName);
					switch (platformType)
					{
					case .Linux,
						 .Windows,
						 .macOS,
						 .Wasm:
						defaultBuildKind = isTest ? .Test : .Normal;
					default:
						defaultBuildKind = .StaticLib;
					}
					if (platformType == .Android)
						defaultRelocType = .PIC;

					// Build
					options.mBuildOptions.mBuildKind = data.GetEnum<BuildKind>("BuildKind", defaultBuildKind);
					if ((isBeefDynLib) && (options.mBuildOptions.mBuildKind == .Normal))
						options.mBuildOptions.mBuildKind = .DynamicLib;
				    data.GetString("TargetDirectory", options.mBuildOptions.mTargetDirectory, "$(BuildDir)");
				    data.GetString("TargetName", options.mBuildOptions.mTargetName, "$(ProjectName)");
				    data.GetString("OtherLinkFlags", options.mBuildOptions.mOtherLinkFlags, "$(LinkFlags)");
					options.mBuildOptions.mCLibType = data.GetEnum<CLibType>("CLibType", isRelease ? .Static : .StaticDebug);
					options.mBuildOptions.mBeefLibType = data.GetEnum<BeefLibType>("BeefLibType", isRelease ? .Static : .Dynamic);
					options.mBuildOptions.mStackSize = data.GetInt("StackSize");
					options.mBuildOptions.mBuildCommandsOnCompile = data.GetEnum<BuildCommandTrigger>("BuildCommandsOnCompile", .Always);
					options.mBuildOptions.mBuildCommandsOnRun = data.GetEnum<BuildCommandTrigger>("BuildCommandsOnRun", .Always);
					ReadStrings("LibPaths", options.mBuildOptions.mLibPaths);
					ReadStrings("LinkDependencies", options.mBuildOptions.mLinkDependencies);
					ReadStrings("PreBuildCmds", options.mBuildOptions.mPreBuildCmds);
					ReadStrings("PostBuildCmds", options.mBuildOptions.mPostBuildCmds);
					ReadStrings("CleanCmds", options.mBuildOptions.mCleanCmds);
					
			    	// DebugOptions
			        data.GetString("DebugCommand", options.mDebugOptions.mCommand, "$(TargetPath)");
			        data.GetString("DebugCommandArguments", options.mDebugOptions.mCommandArguments);
			        data.GetString("DebugWorkingDirectory", options.mDebugOptions.mWorkingDirectory, "$(ProjectDir)");
					ReadStrings("EnvironmentVars", options.mDebugOptions.mEnvironmentVars);
			        
			    	// BeefOptions
					ClearAndDeleteItems(options.mBeefOptions.mPreprocessorMacros);
					if (data.Contains("PreprocessorMacros"))
					{
			            for (var _preproc in data.Enumerate("PreprocessorMacros"))
			            {
							var str = new String();
							data.GetCurString(str);
		                    options.mBeefOptions.mPreprocessorMacros.Add(str);
			            }
					}
					else
					{
						if (isRelease)
							options.mBeefOptions.mPreprocessorMacros.Add(new String("RELEASE"));
						if (isDebug)
							options.mBeefOptions.mPreprocessorMacros.Add(new String("DEBUG"));
						if (isParanoid)
							options.mBeefOptions.mPreprocessorMacros.Add(new String("PARANOID"));
						if (isTest)
							options.mBeefOptions.mPreprocessorMacros.Add(new String("TEST"));
					}

					options.mBeefOptions.mRelocType = data.GetEnum<BuildOptions.RelocType>("RelocType", defaultRelocType);
					options.mBeefOptions.mPICLevel = data.GetEnum<BuildOptions.PICLevel>("PICLevel");
					if (data.Contains("OptimizationLevel"))
			        	options.mBeefOptions.mOptimizationLevel = data.GetEnum<BuildOptions.BfOptimizationLevel>("OptimizationLevel");
					if (data.Contains("LTOType"))
						options.mBeefOptions.mLTOType = data.GetEnum<BuildOptions.LTOType>("LTOType");
			        options.mBeefOptions.mMergeFunctions = data.GetBool("MergeFunctions");
			        options.mBeefOptions.mCombineLoads = data.GetBool("CombineLoads");
			        options.mBeefOptions.mVectorizeLoops = data.GetBool("VectorizeLoops");
			        options.mBeefOptions.mVectorizeSLP = data.GetBool("VectorizeSLP");
					for (data.Enumerate("DistinctOptions"))
					{
						var typeOptions = new DistinctBuildOptions();
						typeOptions.Deserialize(data);
						options.mBeefOptions.mDistinctBuildOptions.Add(typeOptions);
					}

#if IDE_C_SUPPORT
                    using (data.Open("COptions"))
                    {
                        options.mCOptions.mCompilerType = data.GetEnum<CCompilerType>("CompilerType", .Clang);
                        data.GetString("OtherCFlags", options.mCOptions.mOtherCFlags);
                        data.GetString("OtherCPPFlags", options.mCOptions.mOtherCPPFlags);
                        DeleteAndClearItems!(options.mCOptions.mIncludePaths);
                        using (data.Open("IncludePaths"))
                        {
                            for (int32 i = 0; i < data.Count; i++)
							{
								var str = new String();
								data.GetString(i, str);
                                options.mCOptions.mIncludePaths.Add(str);
							}
                        }
                        options.mCOptions.mEnableBeefInterop = data.GetBool("EnableBeefInterop");
                        DeleteAndClearItems!(options.mCOptions.mPreprocessorMacros);
                        using (data.Open("PreprocessorMacros"))
                        {                                        
                            for (int32 i = 0; i < data.Count; i++)
							{
								var str = new String();
								data.GetString(i, str);
                                options.mCOptions.mPreprocessorMacros.Add(str);
							}
                        }
                        data.GetBool("DisableExceptions", options.mCOptions.mDisableExceptions);
                        options.mCOptions.mSIMD = data.GetEnum<SIMDSetting>("SIMD", SIMDSetting.FromWorkspace);
                        options.mCOptions.mGenerateLLVMAsm = data.GetBool("GenerateLLVMAsm");
                        options.mCOptions.mNoOmitFramePointers = data.GetBool("NoOmitFramePointers");
                        options.mCOptions.mDisableInlining = data.GetBool("DisableInlining");
                        options.mCOptions.mStrictAliasing = data.GetBool("StrictAliasing");
                        options.mCOptions.mFastMath = data.GetBool("FastMath");
                        options.mCOptions.mDisableRTTI = data.GetBool("DisableRTTI");
                        options.mCOptions.mOptimizationLevel = data.GetEnum<COptimizationLevel>("OptimizationLevel", .FromWorkspace);
                        options.mCOptions.mEmitDebugInfo = data.GetBool("EmitDebugInfo", isRelease ? false : true);
                        data.GetString("AddressSanitizer", options.mCOptions.mAddressSanitizer);

                        options.mCOptions.mAllWarnings = data.GetBool("AllWarnings");
                        options.mCOptions.mEffectiveCPPViolations = data.GetBool("EffectiveCPPViolations");
                        options.mCOptions.mPedantic = data.GetBool("Pedantic");
                        options.mCOptions.mWarningsAsErrors = data.GetBool("WarningsAsErrors");
                        using (data.Open("SpecificWarningsAsErrors"))
                        {                                        
                            for (int32 i = 0; i < data.Count; i++)
							{
								var str = new String();
								data.GetString(i, str);
                                options.mCOptions.mSpecificWarningsAsErrors.Add(str);
							}
                        }
                        using (data.Open("DisableSpecificWarnings"))
                        {
                            for (int32 i = 0; i < data.Count; i++)
							{
								var str = new String();
								data.GetString(i, str);
                                options.mCOptions.mDisableSpecificWarnings.Add(str);
							}
                        }
                    }
#endif
                }
            }

			SetupDefaultConfigs();

			if (!IsSingleFile)
			{
				mRootFolder.mPath = new String("src");
				using (data.Open("ProjectFolder"))
					mRootFolder.Deserialize(data);
				/*if (Directory.Exists(scope String(mProjectDir, "/src")))
				{
					mRootFolder.Populate("src");
				}*/
			}
			else
			{
				mRootFolder.mPath = new String();
				Path.GetDirectoryPath(gApp.mWorkspace.mCompositeFile.FilePath, mRootFolder.mPath);
				mRootFolder.mIncludeKind = .Manual;

				let srcFile = new ProjectSource();
				srcFile.mIncludeKind = .Manual;
				srcFile.mProject = this;
				srcFile.mParentFolder = mRootFolder;
				srcFile.mPath = new String(CompositeFile.sMainFileName);
				srcFile.mName.Set(CompositeFile.sMainFileName);
				mRootFolder.AddChild(srcFile);

				using (data.Open("ProjectFolder"))
					mRootFolder.Deserialize(data);
			}

			mRootFolder.StartWatching();
        }

		public void FinishCreate(bool allowCreateDir = true)
		{
			if (!mRootFolder.mIsWatching)
			{
				String fullPath = scope String();
				mRootFolder.GetFullImportPath(fullPath);
				if (Directory.Exists(fullPath))
				{
					mRootFolder.Populate("src");
				}
				else if (!allowCreateDir)
				{
					return;
				}
				else
					Directory.CreateDirectory(fullPath).IgnoreError();
				mRootFolder.StartWatching();
			}
			mNeedsCreate = false;
		}

		public void SetupDefault()
		{
			mRootFolder.mPath = new String("src");
			mRootFolder.mAutoInclude = true;
			if (Directory.Exists(scope String(mProjectDir, "/src")))
			{
				mRootFolder.Populate("src");
				mRootFolder.StartWatching();
			}
			SetupDefault(mBeefGlobalOptions);
		}

        public void Save()
        {
			if (mNeedsCreate)
				FinishCreate();

            StructuredData aData = scope StructuredData();
			aData.CreateNew();
            Serialize(aData);

			String tomlString = scope String();
			aData.ToTOML(tomlString);
			if (!mProjectPath.IsEmpty)
			{
				if (!gApp.SafeWriteTextFile(ProjectFileName, tomlString))
					return;
			}
			else
			{
				// If it's just the FileVersion then don't save anything...
				/*if (tomlString.Count('\n') < 2)
					tomlString.Clear();*/
				gApp.StructuredSave("Project", tomlString);
			}
            mHasChanged = false;
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

        public Options GetOptions(String configName, String platformName, bool createOnDemand)
        {
            var options = GetOptions(configName, platformName);
			if ((options == null) && (createOnDemand))
				options = CreateConfig(configName, platformName);
			return options;
        }

        public void SetChanged()
        {
			gApp.MarkDirty();
            mHasChanged = true;
        }
                
        delegate String ProjectItemStringDelegate<T>(T projectItem);
        
        void SourceIterate<T>(String sourceText, ProjectItem projectItem, ProjectItemStringDelegate<T> theDelegate) where T : ProjectItem
        {
            if (projectItem is T)
                sourceText.Append(theDelegate((T) projectItem), Environment.NewLine);
            
            if (projectItem is ProjectFolder)
            {
                var projectFolder = (ProjectFolder)projectItem;
                for (var childItem in projectFolder.mChildItems)
                    SourceIterate<T>(sourceText, childItem, theDelegate);
            }
        }
        
        delegate void ProjectItemStructuredDataDelegate<T>(ConfigWriteData data, T projectItem);

        void ConfigIterate<T>(ConfigWriteData data, ProjectItem projectItem, ProjectItemStructuredDataDelegate<T> theDelegate) where T : ProjectItem
        {
            if (projectItem is T)
                theDelegate(data, (T)projectItem);

            data.mData = null;
            data.mOutputDir = null;
            data.mPlatform = null;

            if (projectItem is ProjectFolder)
            {
                var projectFolder = (ProjectFolder)projectItem;
                for (var childItem in projectFolder.mChildItems)
                    ConfigIterate<T>(data, childItem, theDelegate);
            }
        }

        void WriteItemConfigData(ConfigWriteData data, ProjectItem item)
        {
            StructuredData aData = data.mData;

            if (!(item is ProjectFolder))
            {
                ProjectSource aProjectImage = item as ProjectSource;
                if (aProjectImage != null)
                {
                    using (aData.CreateObject())
                    {
                        aData.Add("Type", "Image");                     
						aData.Add("Name", aProjectImage.mName);
                        String imageFullPath = scope String();
                        GetProjectFullPath(aProjectImage.mPath, imageFullPath);
						String imageRelPath = scope String();
						Path.GetRelativePath(imageFullPath, data.mOutputDir, imageRelPath);
						IDEUtils.CanonicalizeFilePath(imageRelPath);
                        aData.Add("Path", imageRelPath);
                    }
                }
            }
        }

        public void UpdateResConfigData()
        {            
        }
        
        public void Update()
        {
            
        }

        public void WithProjectItems(delegate void(ProjectItem) func)
        {
            List<int32> idxStack = scope List<int32>();
            List<ProjectFolder> folderStack = scope List<ProjectFolder>();

            folderStack.Add(mRootFolder);
            idxStack.Add(0);

            while (folderStack.Count > 0)
            {
                int32 curIdx = idxStack[idxStack.Count - 1]++;
                ProjectFolder curFolder = folderStack[folderStack.Count - 1];
                if (curIdx >= curFolder.mChildItems.Count)
                {
                    folderStack.RemoveAt(folderStack.Count - 1);
                    idxStack.RemoveAt(idxStack.Count - 1);
                    continue;
                }

                var projectItem = curFolder.mChildItems[curIdx];
                if (projectItem is ProjectFolder)
                {
                    folderStack.Add((ProjectFolder)projectItem);
                    idxStack.Add(0);
                    continue;
                }

                func(projectItem);
            }
        }

        public bool HasDependency(String projectName, bool checkRecursively = true)
        {
			HashSet<Project> checkedProject = scope .();

			bool CheckDependency(Project project)
			{
				if (!checkedProject.Add(project))
					return false;

				for (var dependency in project.mDependencies)
				{
				    if (dependency.mProjectName == projectName)
				        return true;
					let depProject = gApp.mWorkspace.FindProject(dependency.mProjectName);
					if ((depProject != null) && (checkRecursively) && (CheckDependency(depProject)))
						return true;
				}
				return false;
			}

            return CheckDependency(this);
        }

		public void SetupDefault(Options options, String configName, String platformName)
		{
			bool isRelease = configName.Contains("Release");
			bool isParanoid = configName.Contains("Paranoid");
			bool isDebug = isParanoid || configName.Contains("Debug");
			bool isTest = configName.Contains("Test");
			let platformType = Workspace.PlatformType.GetFromName(platformName);

			if (isRelease)
				options.mBeefOptions.mPreprocessorMacros.Add(new String("RELEASE"));
			if (isDebug)
				options.mBeefOptions.mPreprocessorMacros.Add(new String("DEBUG"));
			if (isParanoid)
				options.mBeefOptions.mPreprocessorMacros.Add(new String("PARANOID"));
			if (isTest)
				options.mBeefOptions.mPreprocessorMacros.Add(new String("TEST"));

			options.mBuildOptions.mCLibType = isRelease ? .Static : .StaticDebug;
			options.mBuildOptions.mBeefLibType = isRelease ? .Static : .Dynamic;
			options.mBuildOptions.mStackSize = 0;

			switch (platformType)
			{
			case .Linux,
				 .Windows,
				 .macOS,
				 .Wasm:
				options.mBuildOptions.mBuildKind = isTest ? .Test : .Normal;
			default:
				options.mBuildOptions.mBuildKind = .StaticLib;
			}

			if (platformType == .Android)
			{
				options.mBeefOptions.mRelocType = .PIC;
			}

			options.mBuildOptions.mOtherLinkFlags.Set("$(LinkFlags)");

			if (gApp.mWorkspace.IsDebugSession)
			{
				options.mBuildOptions.mTargetName.Clear();
				options.mDebugOptions.mWorkingDirectory.Clear();
				options.mBuildOptions.mBuildCommandsOnCompile = .Always;
				options.mBuildOptions.mBuildCommandsOnRun = .Never;
			}
		}

		public void SetupDefault(GeneralOptions generalOptions)
		{
			
		}

		public void SetupDefault(BeefGlobalOptions generalOptions)
		{
			generalOptions.mDefaultNamespace.Clear();
			GetSanitizedName(mProjectName, generalOptions.mDefaultNamespace, true);
			generalOptions.mStartupObject.Set(scope String()..AppendF("{}.Program", generalOptions.mDefaultNamespace));
		}

		public Options CreateConfig(String configName, String platformName)
		{
			String* configKeyPtr = null;
			Config* configPtr = null;
			Config config = null;
			if (mConfigs.TryAdd(configName, out configKeyPtr, out configPtr))
			{
				config = new Config();
				*configPtr = config;
				*configKeyPtr = new String(configName);
			}
			else
				config = *configPtr;

			String* platformKeyPtr = null;
			Options* optionsPtr = null;
			Options options = null;
			if (config.mPlatforms.TryAdd(platformName, out platformKeyPtr, out optionsPtr))
			{
				options = new Options();
				*optionsPtr = options;
				*platformKeyPtr = new String(platformName);
				SetupDefault(options, configName, platformName);
			}
			return *optionsPtr;
		}
    }
}
