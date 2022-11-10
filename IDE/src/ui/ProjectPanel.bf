using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme;
using Beefy.events;
using Beefy.theme.dark;
using System.Threading;
using System.IO;
using Beefy.utils;
using Beefy;
using IDE.Compiler;
using System.Diagnostics;
using IDE.Util;

namespace IDE.ui
{
    public class ProjectListViewItem : DarkListViewItem
    {
        public float mLabelOffset;
        public Object mRefObject;
		public bool mSortDirty;
		public bool mWantsRehup;

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			if (mLabelOffset != 0)
				mLabelOffset = GS!(-16);
		}

        protected override float GetLabelOffset()
        {
            return mLabelOffset;
        }

        public override float ResizeComponents(float xOffset)
        {
            return base.ResizeComponents(xOffset);
        }

        public override void DrawSelect(Graphics g)
        {
			let projectPanel = ((ProjectListView)mListView).mProjectPanel;
            using (g.PushColor((mListView.mParent.mHasFocus || (projectPanel.mMenuWidget != null)) ? 0xFFFFFFFF : 0x80FFFFFF))
                base.DrawSelect(g);
        }

        public override void Draw(Graphics g)
        {
			uint32 color = DarkTheme.COLOR_TEXT;
			let projectPanel = ((ProjectListView)mListView).mProjectPanel;

			ProjectItem projectItem;
			projectPanel.mListViewToProjectMap.TryGetValue(this, out projectItem);
			if ((projectItem != null) && (projectItem.mParentFolder != null))
			{
				if (projectItem.mIncludeKind == .Manual)
					color = Color.Mult(color, gApp.mSettings.mUISettings.mColors.mWorkspaceManualIncludeText);
				else if (projectItem.mIncludeKind == .Ignore)
					color = Color.Mult(color, gApp.mSettings.mUISettings.mColors.mWorkspaceIgnoredText);

				if (let projectFileItem = projectItem as ProjectFileItem)
				{
					if (projectPanel.mClipboardCutQueued != null)
					{
						var path = projectFileItem.mProject.GetProjectFullPath(projectFileItem.mPath, .. scope .());
						IDEUtils.MakeComparableFilePath(path);
						if (projectPanel.mClipboardCutQueued.Contains(path))
							color = Color.Mult(color, gApp.mSettings.mUISettings.mColors.mWorkspaceCutText);
					}
				}

				if (let projectSource = projectItem as ProjectSource)
				{
					if (projectSource.mLoadFailed)
						color = Color.Mult(color, gApp.mSettings.mUISettings.mColors.mWorkspaceFailedText);
				}

				mTextColor = color;
			}

            base.Draw(g);

            if (mRefObject != null)
            {
				float changeX = g.mFont.GetWidth(mLabel) + mLabelOffset + LabelX + GS!(1);

                bool hasChanged = false;
                var workspace = mRefObject as Workspace;
                if (workspace != null)
                    hasChanged = workspace.mHasChanged;
                var project = mRefObject as Project;
                if (project != null)
                {
					hasChanged = project.mHasChanged;
					if (project.mDeferState != .None)
					{
						g.DrawString(scope String()..Append('.', 1 + (mUpdateCnt / 20) % 3), g.mFont.GetWidth(mLabel) + mLabelOffset + LabelX + GS!(3), 0);
						changeX += GS!(12);
					}
					else if (project.mLocked)
					{
						g.Draw(DarkTheme.sDarkTheme.GetImage(.LockIcon), g.mFont.GetWidth(mLabel) + GS!(42), 0);
						changeX += GS!(12);
					}
				}
                if (hasChanged)
                    g.DrawString("*", changeX, 0);
            }
        }

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
		}
    }

    public class ProjectListView : IDEListView
    {
		public ProjectPanel mProjectPanel;

        protected override ListViewItem CreateListViewItem()
        {
            ProjectListViewItem anItem = new ProjectListViewItem();
            return anItem;
        }

		protected override void SetScaleData()
		{
			mIconX = GS!(20);
			mOpenButtonX = GS!(4);
			mLabelX = GS!(44);
			mChildIndent = GS!(16);
			mHiliteOffset = GS!(-2);
		}

    }

    public class ProjectPanel : Panel
    {
        public ProjectListView mListView;
        bool mImportFileDeferred;
        bool mImportFolderDeferred;
        bool mImportProjectDeferred;
		bool mImportInstalledDeferred;
        public Dictionary<ListViewItem, ProjectItem> mListViewToProjectMap = new .() ~ delete _;
        public Dictionary<ProjectItem, ProjectListViewItem> mProjectToListViewMap = new .() ~ delete _;
		public Dictionary<ListViewItem, WorkspaceFolder> mListViewToWorkspaceFolderMap = new .() ~ delete _;
		public Dictionary<ProjectItem, WorkspaceFolder> mProjectToWorkspaceFolderMap = new .() ~ delete _;
		ListViewItem mWorkspaceListViewItem;
        public DarkListViewItem mSelectedParentItem;
		public MenuWidget mMenuWidget;

        public bool mRefreshVisibleViewsDeferred;
		public bool mShowIgnored = true;
		public bool mSortDirty;
		public bool mWantsRehup;
		public HashSet<String> mClipboardCutQueued ~ DeleteContainerAndItems!(_);

        public this()
        {
            mListView = new ProjectListView();
			mListView.mProjectPanel = this;
			mListView.mOnEditDone.Add(new => HandleEditDone);
            
            mListView.SetShowHeader(false);
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
            mListView.UpdateScrollbars();
            mListView.mOnFocusChanged.Add(new => FocusChangedHandler);

            mListView.mOnDragEnd.Add(new => HandleDragEnd);
            mListView.mOnDragUpdate.Add(new => HandleDragUpdate);
            
            AddWidget(mListView);

			mListView.mOnMouseClick.Add(new => ListViewClicked);
            mListView.mOnMouseDown.Add(new => ListViewMouseDown);

            mListView.AddColumn(GS!(100), "Name");

			SetScaleData();
            RebuildUI();
        }        

		void SetScaleData()
		{
			mListView.mColumns[0].mMinWidth = GS!(40);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			mListView.mOpenButtonX = GS!(4);
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

		public override void FocusForKeyboard()
		{
			base.FocusForKeyboard();
			SetFocus();
			let root = mListView.GetRoot();
			if (root.IsOpen && root.FindFocusedItem() == null)
			{
				root.GetChildAtIndex(0).Focused = true;
			}
		}

        void FocusChangedHandler(ListViewItem listViewItem)
        {
            if (listViewItem.Focused)
            {
                ProjectItem aProjectItem;
                mListViewToProjectMap.TryGetValue(listViewItem, out aProjectItem);
                if (aProjectItem != null)
                {
                    IDEApp.sApp.ShowProjectItem(aProjectItem, true, false);
                }
            }
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "ProjectPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

        void HandleDragUpdate(DragEvent theEvent)
        {
            DarkListViewItem source = (DarkListViewItem)theEvent.mSender;
            DarkListViewItem target = (DarkListViewItem)theEvent.mDragTarget;

			theEvent.mDragKind = .None;

			int validCount = 0;
			int invalidCount = 0;
            if (source.mListView == target.mListView)
            {
				if (mListViewToWorkspaceFolderMap.ContainsKey(target) || target == mWorkspaceListViewItem)
				{
					mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
						{
							if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
							{
								if (sourceProjectItem.mParentFolder == null)
									validCount++;
							}
							else if (mListViewToWorkspaceFolderMap.GetValue(selectedItem) case .Ok(let sourceFolderItem))
							{
								validCount++;
							}
						});
				}
				else if (mListViewToProjectMap.GetValue(target) case .Ok(var targetProjectItem))
				{
					mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
						{
							if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
							{
								if ((sourceProjectItem.mParentFolder != null) && (sourceProjectItem.mProject == targetProjectItem.mProject))
									validCount++;
								else
									invalidCount++;
							}
						});
				}
            }

			if ((validCount > 0) && (invalidCount == 0))
				theEvent.mDragKind = .Inside;
        }

        void HandleDragEnd(DragEvent theEvent)
        {
            if (theEvent.mDragKind == .None)
                return;

            if (theEvent.mDragTarget is ProjectListViewItem)
            {
				ProjectListViewItem source = (ProjectListViewItem)theEvent.mSender;
				ProjectListViewItem target = (ProjectListViewItem)theEvent.mDragTarget;

				theEvent.mDragKind = .None;

				if (source.mListView == target.mListView)
				{
					ListViewItem folderListView;
					WorkspaceFolder targetFolder = null;
					if (mListViewToWorkspaceFolderMap.GetValue(target) case .Ok(out targetFolder))
					    folderListView = targetFolder.mListView;
					else if (target == mWorkspaceListViewItem)
					    folderListView = mListView.GetRoot();
					else
					    folderListView = null;

					if (folderListView != null)
					{
					    List<ListViewItem> selectedItems = scope .();
					    mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
					        {
					            selectedItems.Add(selectedItem);
					        });


					    Loop: for (let item in selectedItems)
					    {
					        let parent = item.mParentItem;

					        IsValid: do
					        {
					            if ((mListViewToWorkspaceFolderMap.GetValue(item) case .Ok(let folder)) && (item != folderListView))
					            {
									// Check if target folder is not child of moved directory
									bool IsMoveValid()
									{
										ListViewItem parent = folderListView;
										repeat
										{
											parent = parent.mParentItem;
											if (parent == item)
												return false;
										}
										while(parent != null);

										return true;
									}
									if ((target.mDepth <= item.mDepth) || (IsMoveValid()))
									{
										folder.mParent = targetFolder;
										break IsValid;
									}	
					            }
					            else if ((mListViewToProjectMap.GetValue(item) case .Ok(let projectItem)) && (projectItem.mParentFolder == null))
					            {
									if (targetFolder != null)
									{
										if (mProjectToWorkspaceFolderMap.TryAdd(projectItem, var keyPtr, var valPtr))
										{
											(*valPtr) = targetFolder;
										}
										else
										{
											(*valPtr).mProjects.Remove(projectItem.mProject);
											(*valPtr) = targetFolder;
										}
										targetFolder.mProjects.Add(projectItem.mProject);
									}	
					                break IsValid;
					            }

					            continue Loop;
					        }

					        parent.RemoveChildItem(item, false);
							folderListView.MakeParent();
					        folderListView.AddChild(item);
					        parent.TryUnmakeParent();
					    }

					    if (selectedItems.Count > 0)
					    {
					        target.mOpenButton?.Open(true, false);
					        QueueSortItem((ProjectListViewItem)folderListView);
					        gApp.mWorkspace.SetChanged();
					    }
					    
					}
					else if (mListViewToProjectMap.GetValue(target) case .Ok(var targetProjectItem))
					{
						if (targetProjectItem == null)
						    return;

						if (source == target)
						    return;

						var targetProjectFolder = targetProjectItem as ProjectFolder;
						if (targetProjectFolder == null)
							targetProjectFolder = targetProjectItem.mParentFolder;
						var targetListItem = mProjectToListViewMap[targetProjectFolder];

						int moveCount = 0;
						int selectCount = 0;
						mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
							{
								if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
								{
									if (sourceProjectItem.mParentFolder != targetProjectFolder)
										moveCount++;
								}
								selectCount++;
							});

						if (moveCount == 0)
							return;

						var targetDisplayName = targetProjectFolder.GetFullDisplayName(.. scope .());
						if (targetDisplayName.IsEmpty)
							targetDisplayName = "src";

						Dialog dialog;
						if (selectCount == 1)
						{
							dialog = ThemeFactory.mDefault.CreateDialog("Move file to a new location?",
								scope $"Are you sure you want to move this file to '{targetDisplayName}'?");
						}
						else
						{
							dialog = ThemeFactory.mDefault.CreateDialog("Move files to a new location?",
								scope $"Are you sure you want to move these files to '{targetDisplayName}'?");
						}
						dialog.AddButton("Yes", new (evt) =>
							{
								List<String> fileErrors = scope .();

								List<ListViewItem> selectedItems = scope .();
								mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
									{
										selectedItems.Add(selectedItem);
									});

								for (var selectedItem in selectedItems)
								{
									if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
									{
										var sourceProjectFileItem = sourceProjectItem as ProjectFileItem;
										if (sourceProjectFileItem == null)
											return;

										var sourcePath = sourceProjectFileItem.GetFullImportPath(.. scope .());
										var destPath = targetProjectFolder.GetFullImportPath(.. scope .());
										destPath.AppendF($"{Path.DirectorySeparatorChar}{sourceProjectFileItem.mName}");

										if (!targetProjectFolder.mChildMap.TryAdd(sourceProjectFileItem.mName, sourceProjectFileItem))
										{
											fileErrors.Add(scope:: .(destPath));
										}
										else
										{
											if (File.Move(sourcePath, destPath) case .Ok)
											{
												gApp.FileRenamed(sourceProjectFileItem, sourcePath, destPath);

												if (targetProjectFolder != sourceProjectItem.mParentFolder)
												{
													source.mParentItem.RemoveChildItem(source, false);
													sourceProjectItem.mParentFolder.RemoveChild(sourceProjectItem);

													targetListItem.AddChildAtIndex(0, source);
													targetListItem.mOpenButton.Open(true, false);
													targetProjectFolder.AddChildAtIndex(0, sourceProjectItem);

													sourceProjectFileItem.RecalcPath();
												}
											}
											else
											{
												targetProjectFolder.mChildMap.Remove(sourceProjectFileItem.mName);
												fileErrors.Add(scope:: .(destPath));
											}
										}
									}
								}

								QueueSortItem(targetListItem);
								DoSortItem(targetListItem);

								if (!fileErrors.IsEmpty)
								{
									var errorStr = scope String();
									if (fileErrors.Count == 1)
										errorStr.AppendF("Failed to move file to: {0}", fileErrors[0]);
									else
									{
										errorStr.AppendF("Failed to move {0} files:", fileErrors.Count);
										for (var file in fileErrors)
											errorStr.Append("\n  {}", file);
									}
									gApp.Fail(errorStr);
								}
							});
						dialog.AddButton("No", new (evt) =>
							{
								
							});
						dialog.PopupWindow(gApp.GetActiveWindow());

						
						//RehupFolder(targetProjectFolder.mProject.mRootFolder, .FullTraversal);

						/*if (theEvent.mDragTargetDir == -1) // Before          
						{
						    target.mParentItem.InsertChild(source, target);
						    targetProjectItem.mParentFolder.InsertChild(sourceProjectItem, targetProjectItem);
						}
						else if (theEvent.mDragTargetDir == 0) // Inside
						{
						    target.AddChildAtIndex(0, source);
						    target.mOpenButton.Open(true, false);
						    ((ProjectFolder)targetProjectItem).AddChildAtIndex(0, sourceProjectItem);
						}
						else if (theEvent.mDragTargetDir == 1) // After
						{                        
						    target.mParentItem.AddChild(source, target);
						    targetProjectItem.mParentFolder.AddChild(sourceProjectItem, targetProjectItem);                        
						}*/
					}
				}

                /*DarkListViewItem source = (DarkListViewItem)theEvent.mSender;
                DarkListViewItem target = (DarkListViewItem)theEvent.mDragTarget;

                if (source.mListView == target.mListView)
                {
                    ProjectItem targetProjectItem = mListViewToProjectMap[target];
                    ProjectItem sourceProjectItem = mListViewToProjectMap[source];

                    
                }*/
            }            
        }

        public void InitProject(Project project, WorkspaceFolder workspaceFolder)
        {
            var projectListViewItem = InitProjectItem(project.mRootFolder);
            projectListViewItem.mRefObject = project;
			if (workspaceFolder != null)
			{
				let root = mListView.GetRoot();
				root.RemoveChildItem(projectListViewItem, false);
				workspaceFolder.mListView.MakeParent();
				workspaceFolder.mListView.AddChild(projectListViewItem);
				workspaceFolder.mListView.Open(true);
				mProjectToWorkspaceFolderMap[project.mRootFolder] = workspaceFolder;

				workspaceFolder.mProjects.Add(project);
				QueueSortItem(workspaceFolder.mListView);
			}
        }

        public void RebuildUI()
        {
            mListView.GetRoot().Clear();

			/*if (gApp.mWorkspace.mDebugSession != null)
			{
				var debugSessionItem = (ProjectListViewItem)mListView.GetRoot().CreateChildItem();
				debugSessionItem.Label = "Debug Session";
				debugSessionItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Workspace);            
				debugSessionItem.mLabelOffset = GS!(-16);
				debugSessionItem.mRefObject = IDEApp.sApp.mWorkspace;
				SetupItem(debugSessionItem, true);
			}*/

			if (!gApp.mWorkspace.IsInitialized)
			{
				mListViewToProjectMap.Clear();
				mProjectToListViewMap.Clear();
				mListViewToWorkspaceFolderMap.Clear();
				mProjectToWorkspaceFolderMap.Clear();
				return;
			}	

			if (!gApp.mWorkspace.IsDebugSession)
			{
	            var workspaceItem = (ProjectListViewItem)mListView.GetRoot().CreateChildItem();
	            workspaceItem.Label = "Workspace";
	            workspaceItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Workspace);            
	            workspaceItem.mLabelOffset = GS!(-16);
	            workspaceItem.mRefObject = IDEApp.sApp.mWorkspace;
	            SetupItem(workspaceItem, true);
				mWorkspaceListViewItem = workspaceItem;
			}

            mListViewToProjectMap.Clear();
            mProjectToListViewMap.Clear();
			mListViewToWorkspaceFolderMap.Clear();
			mProjectToWorkspaceFolderMap.Clear();

            for (var project in IDEApp.sApp.mWorkspace.mProjects)
                InitProject(project, null);

			let root = mListView.GetRoot();

			HashSet<String> folderMap = scope .();
			for (var workspaceFolder in IDEApp.sApp.mWorkspace.mWorkspaceFolders)
			{
				void AddFolder(WorkspaceFolder folder)
				{
					if (folder.mListView == null && folderMap.Add(folder.mName))
					{
						ProjectListViewItem parentListViewItem;
						if (folder.mParent == null)
							parentListViewItem = (ProjectListViewItem)mListView.GetRoot();
						else
						{
							if (folder.mParent.mListView == null)
								AddFolder(folder.mParent);

							parentListViewItem = folder.mParent.mListView;
							if (parentListViewItem == null)
								return;
						}

						ProjectListViewItem listViewItem;
						listViewItem = (ProjectListViewItem)parentListViewItem.CreateChildItem();

						folder.mListView = listViewItem;
						listViewItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ProjectFolder);
						listViewItem.Label = folder.mName;
						listViewItem.mRefObject = folder;
						SetupItem(listViewItem, true);
						mListViewToWorkspaceFolderMap.Add(listViewItem, folder);
					}
				}
				AddFolder(workspaceFolder);
				if (workspaceFolder.mProjects == null)
					continue;

				for (let project in workspaceFolder.mProjects)
				{
					if (mProjectToListViewMap.TryGetValue(project.mRootFolder, let viewItem))
					{
						root.RemoveChildItem(viewItem, false);
						workspaceFolder.mListView.MakeParent();
						workspaceFolder.mListView.AddChild(viewItem);
						mProjectToWorkspaceFolderMap[project.mRootFolder] = workspaceFolder;
					}
				}
				QueueSortItem(workspaceFolder.mListView);
			}	

			RehupProjects();
        }

        ProjectListViewItem InitProjectItem(ProjectItem item)
        {
            var listViewItem = AddProjectItem(item);
            if (item is ProjectFolder)
            {
                ProjectFolder aFolder = (ProjectFolder)item;                
                for (ProjectItem anItem in aFolder.mChildItems)
                    InitProjectItem(anItem);
            }
            return listViewItem;
        }

        void AssociateViews(ProjectListViewItem listViewItem, ProjectItem projectItem)
        {
            mListViewToProjectMap[listViewItem] = projectItem;
            mProjectToListViewMap[projectItem] = listViewItem;
        }

        void SetupItem(ListViewItem item, bool selectable)
        {
            if (selectable)
                item.mOnMouseDown.Add(new => ItemClicked);
			item.mOnMouseClick.Add(new => ListViewItemClicked);
            UpdateColors();
            DarkListViewItem listViewItem = (DarkListViewItem)item;
			listViewItem.AllowDragging = true;
            listViewItem.mFocusColor = gApp.mSettings.mUISettings.mColors.mWorkspaceDisabledText;
            listViewItem.mSelectColor = gApp.mSettings.mUISettings.mColors.mWorkspaceDisabledText;
        }

        void UpdateColors()
        {

        }

        public ProjectListViewItem AddProjectItem(ProjectItem projectItem, ProjectListViewItem existingListViewItem = null)
        {
            var projectSource = projectItem as ProjectSource;

            ProjectListViewItem listViewItem = null;
            if ((projectSource != null) && (gApp.IsProjectSourceEnabled(projectSource)))
            {
                var resolveCompiler = gApp.GetProjectCompilerForFile(projectSource.mName);
                if ((resolveCompiler != null) && (gApp.mWorkspace.mProjectLoadState == .Loaded))
                {
                    resolveCompiler.QueueProjectSource(projectSource, .None, false);
                    resolveCompiler.QueueDeferredResolveAll();
                }
				projectSource.mHasChangedSinceLastCompile = true;
                mRefreshVisibleViewsDeferred = true;                
                CheckProjectSource(projectSource);
            }

			if (existingListViewItem != null)
				return existingListViewItem;

            if (projectItem.mParentFolder != null)
            {
                let listViewParent = mProjectToListViewMap[projectItem.mParentFolder];
                listViewItem = (ProjectListViewItem)listViewParent.CreateChildItem();
                if (projectItem is ProjectFolder)
                    listViewItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ProjectFolder);
                else
                    listViewItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Document);
                listViewItem.Label = projectItem.mName;
                if (projectItem is ProjectFolder)
                    listViewItem.MakeParent();

                SetupItem(listViewItem, true);
                AssociateViews(listViewItem, projectItem);
				QueueSortItem(listViewParent);
            }
            else
            {
                var project = projectItem.mProject;
                let listViewParent = (ProjectListViewItem)mListView.GetRoot();
                listViewItem = (ProjectListViewItem)listViewParent.CreateChildItem();
                listViewItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Project);
                listViewItem.Label = project.mProjectName;
                listViewItem.MakeParent();

                listViewItem.mIsBold = project == IDEApp.sApp.mWorkspace.mStartupProject;

                // It's the root
                SetupItem(listViewItem, true);
                AssociateViews(listViewItem, projectItem);
				QueueSortItem(listViewParent);
            }

            return listViewItem;
        }

		bool HasNonAutoItems(ProjectFolder projectFolder)
		{
			for (let child in projectFolder.mChildItems)
			{
				if (var childFolder = child as ProjectFolder)
				{
					if (HasNonAutoItems(childFolder))
						return true;
				}

				if (child.mIncludeKind != .Auto)
				{
					return true;
				}
			}

			return false;
		}

		public enum RehupFlags
		{
			None = 0,
			ApplyAutoToChildren = 1,
			ClearAutoItems = 2,
			FullTraversal = 4,
		}

		public void RehupFolder(ProjectFolder projectFolder, RehupFlags rehupFlags = .None)
		{
			scope AutoBeefPerf("ProjectPanel.RehupFolder");

			if (mProjectToListViewMap.TryGetValue(projectFolder, var lvItem))
			{
				lvItem.mWantsRehup = false;
			}

			var dirPath = scope String();
			projectFolder.GetFullImportPath(dirPath);

			bool clearAutoItems = (!rehupFlags.HasFlag(.ApplyAutoToChildren)) && (projectFolder.mAutoInclude); // && (rehupFlags.HasFlag(.ClearAutoItems));
			HashSet<ProjectItem> foundAutoItems = scope .();

			for (let fileEntry in Directory.EnumerateFiles(dirPath))
			{
				String fileName = scope String();
				fileEntry.GetFileName(fileName);

				ProjectItem projectItem;
				if (projectFolder.mChildMap.TryGetValue(fileName, out projectItem))
				{
					if ((rehupFlags.HasFlag(.ApplyAutoToChildren)) && (projectItem.mIncludeKind == .Manual))
						projectItem.mIncludeKind = .Auto;
					if ((clearAutoItems) && (projectItem.mIncludeKind == .Auto))
						foundAutoItems.Add(projectItem);
                    continue;
				}

				if (!projectFolder.mAutoInclude)
					continue;

				let ext = scope String();
				Path.GetExtension(fileName, ext);
				
				let projectSource = new ProjectSource();
				projectSource.mProject = projectFolder.mProject;
				projectSource.mName.Set(fileName);

				//if (projectFolder.mPath != null)
				{
					projectSource.mPath = new String();
					projectSource.mPath.Append(projectFolder.mPath);
					projectSource.mPath.Append("/");
					projectSource.mPath.Append(fileName);
				}
				projectSource.mIncludeKind = .Auto;
				projectFolder.AddChild(projectSource);

				AddProjectItem(projectSource);

				if ((clearAutoItems) && (projectSource.mIncludeKind == .Auto))
					foundAutoItems.Add(projectSource);
				if (projectSource.mIncludeKind != .Auto)
					projectSource.mProject.SetChanged();
			}

			for (let fileEntry in Directory.EnumerateDirectories(dirPath))
			{
				String dirName = scope String();
				fileEntry.GetFileName(dirName);

				ProjectItem projectItem;
				if (projectFolder.mChildMap.TryGetValue(dirName, out projectItem))
				{
					if (var childProjectFolder = projectItem as ProjectFolder)
					{
						if ((rehupFlags.HasFlag(.ApplyAutoToChildren)) && (projectItem.mIncludeKind == .Manual))
                        	projectItem.mIncludeKind = .Auto;
						if (projectItem.mIncludeKind == .Auto)
							RehupFolder(childProjectFolder, rehupFlags);
						if ((clearAutoItems) && (projectItem.mIncludeKind == .Auto))
							foundAutoItems.Add(projectItem);
					}
				    continue;
				}

				let newRelDir = scope String(projectFolder.mPath);
				if (!newRelDir.IsEmpty)
					newRelDir.Append("/");
				newRelDir.Append(dirName);

				let childProjectFolder = new ProjectFolder();
				childProjectFolder.mProject = projectFolder.mProject;
				childProjectFolder.mName.Set(dirName);

				childProjectFolder.mPath = new String(projectFolder.mPath);
				childProjectFolder.mPath.Append("/");
				childProjectFolder.mPath.Append(dirName);
				childProjectFolder.mAutoInclude = true;
				childProjectFolder.mIncludeKind = .Auto;
				projectFolder.AddChild(childProjectFolder);

				if (clearAutoItems)
				{
					foundAutoItems.Add(childProjectFolder);
				}

				AddProjectItem(childProjectFolder);

				RehupFolder(childProjectFolder, rehupFlags);
			}

			if ((rehupFlags.HasFlag(.ApplyAutoToChildren)) || (clearAutoItems))
			{
				// Any items that were specified as 'manual' but don't actually exist in the file system, remove those now
				//List<ProjectItem> removeList = scope List<ProjectItem>();

				//for (var child in projectFolder.mChildItems)

				for (int childIdx = projectFolder.mChildItems.Count - 1; childIdx >= 0; childIdx--)
				{
					var child = projectFolder.mChildItems[childIdx];
					

					if (((rehupFlags.HasFlag(.ApplyAutoToChildren)) && (child.mIncludeKind == .Manual)) ||
						((clearAutoItems) && (child.mIncludeKind == .Auto) && (!foundAutoItems.Contains(child))))
					{
						var listItem = mProjectToListViewMap[child];
						DoDeleteItem(listItem, null, .ForceRemove);
						continue;
					}

					if ((rehupFlags.HasFlag(.FullTraversal)) && (child.mIncludeKind == .Manual) && (var childProjectFolder = child as ProjectFolder))
					{
						RehupFolder(childProjectFolder, rehupFlags);
					}
				}
			}
		}

		public void QueueRehupFolder(ProjectFolder projectFolder)
		{
			bool hadValue = mProjectToListViewMap.TryGetValue(projectFolder, var lvItem);
			if (!hadValue)
			{
				RehupFolder(projectFolder);
				return;
			}

			mWantsRehup = true;
			lvItem.mWantsRehup = true;
		}

		void Unignore(ProjectItem projectItem)
		{
			var listViewItem = (ProjectListViewItem) mProjectToListViewMap[projectItem];
			if (projectItem.mIncludeKind == .Ignore)
				projectItem.mIncludeKind = .Auto;

			if (let projectFolder = projectItem as ProjectFolder)
			{
				for (let childItem in projectFolder.mChildItems)
				{
					if (childItem.mIncludeKind != .Ignore)
						Unignore(childItem);
				}
			}

			AddProjectItem(projectItem, listViewItem);
			projectItem.mProject.SetChanged();
		}

        public void NewFolder(ProjectFolder folder)
        {
            ProjectFolder projectFolder = new ProjectFolder();

			int checkIdx = 0;
			while (true)
			{
	            projectFolder.mName.Set("New folder");
				if (checkIdx > 0)
					projectFolder.mName.AppendF("{}", checkIdx + 1);
				if (!folder.mChildMap.ContainsKey(projectFolder.mName))
					break;
				checkIdx++;
			}
            projectFolder.mProject = folder.mProject;
			projectFolder.mPath = new String();
			String dirSepStr = scope String()..Append(Path.DirectorySeparatorChar);
			if (folder.mParentFolder == null)
            	projectFolder.mPath.Append("src", dirSepStr, projectFolder.mName);
			else
				projectFolder.mPath.Append(folder.mPath, dirSepStr, projectFolder.mName);
			projectFolder.mIncludeKind = folder.mIncludeKind;
			projectFolder.mAutoInclude = folder.IsAutoInclude();
            folder.AddChild(projectFolder);
            let projectItem = AddProjectItem(projectFolder);
			if (projectItem != null)
			{
				projectItem.Open(true);
				Sort();
				mListView.UpdateAll();
				mListView.GetRoot().SelectItemExclusively(projectItem);
				mListView.EnsureItemVisible(projectItem, false);
				RenameItem(projectFolder);
			}
			if (projectFolder.mIncludeKind != .Auto)
				projectFolder.mProject.SetChanged();

			String fullPath = scope String();
			projectFolder.GetFullImportPath(fullPath);
			if (Directory.CreateDirectory(fullPath) case .Err(let err))
			{
				if (err != .AlreadyExists)
					gApp.Fail(scope String()..AppendF("Failed to create directory '{]'", fullPath));
			}
        }

        public void GenerateCode(ProjectFolder folder)
        {
            /*DarkDialog dialog = (DarkDialog)ThemeFactory.mDefault.CreateDialog("New Class", "Class Name");
            dialog.mMinWidth = GS!(300);
            dialog.mDefaultButton = dialog.AddButton("OK", new (evt) => DoNewClass(folder, evt));
            dialog.mEscButton = dialog.AddButton("Cancel");
            dialog.AddEdit("Unnamed");
            dialog.PopupWindow(gApp.GetActiveWindow());*/

			var dialog = new GenerateDialog(folder);
			dialog.PopupWindow(gApp.GetActiveWindow());
        }

		public void Regenerate(bool allowHashMismatch)
		{
			mListView.GetRoot().WithSelectedItems(scope (selectedItem) =>
				{
					if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
					{
						var dialog = new GenerateDialog(sourceProjectItem, allowHashMismatch);
						dialog.PopupWindow(gApp.GetActiveWindow());
					}
				});
		}

		public void Regenerate(ProjectSource projectSource, StringView fileText)
		{
			var sourceViewPanel = gApp.ShowProjectItem(projectSource, false);
			sourceViewPanel.mEditWidget.SetText(scope .(fileText));
		}

		public void Generate(ProjectFolder folder, StringView fileName, StringView fileText)
		{
			let project = folder.mProject;
			if (project.mNeedsCreate)
				project.FinishCreate();
			String relFileName = scope .(fileName);
			if (!relFileName.Contains('.'))
				relFileName.Append(".bf");

			String fullFilePath = scope String();
			String relPath = scope String();
			folder.GetRelDir(relPath);
			if (relPath.Length > 0)
				relPath.Append("/");
			relPath.Append(relFileName);
			folder.mProject.GetProjectFullPath(relPath, fullFilePath);
			String dirName = scope String();
			Path.GetDirectoryPath(fullFilePath, dirName);
			Directory.CreateDirectory(dirName).IgnoreError();

			if (File.Exists(fullFilePath))
			{
				var error = scope String();
				error.AppendF("File '{0}' already exists", fullFilePath);
				IDEApp.sApp.Fail(error);
				return;
			}

			if (File.WriteAllText(fullFilePath, fileText) case .Err)
			{
				var error = scope String();
				error.AppendF("Failed to create file '{0}'", fullFilePath);
				gApp.Fail(error);
				return;
			}

			ProjectSource projectSource = new ProjectSource();
			projectSource.mIncludeKind = (folder.mIncludeKind == .Auto) ? .Auto : .Manual;
			projectSource.mName.Set(relFileName);
			projectSource.mPath = new String();
			folder.mProject.GetProjectRelPath(fullFilePath, projectSource.mPath);
			projectSource.mProject = folder.mProject;
			projectSource.mParentFolder = folder;
			folder.AddChild(projectSource);
			let projectItem = AddProjectItem(projectSource);
			if (projectItem != null)
			{
				mListView.GetRoot().SelectItemExclusively(projectItem);
				mListView.EnsureItemVisible(projectItem, false);
			}
			Sort();
			if (folder.mIncludeKind != .Auto)
				folder.mProject.SetChanged();

			gApp.RecordHistoryLocation(true);
			gApp.ShowProjectItem(projectSource);
			gApp.RecordHistoryLocation(true);
		}

        void DoNewClass(ProjectFolder folder, DialogEvent evt)
        {
            Dialog dlg = (Dialog)evt.mSender;
            var className = scope String();
            dlg.mDialogEditWidget.GetText(className);
            className.Trim();
            if (className.Length == 0)
                return;

			let project = folder.mProject;
			if (project.mNeedsCreate)
				project.FinishCreate();
            String relFileName = scope String(className, ".bf");
            
            String fullFilePath = scope String();
			String relPath = scope String();
			folder.GetRelDir(relPath);
			if (relPath.Length > 0)
				relPath.Append("/");
            relPath.Append(relFileName);
            folder.mProject.GetProjectFullPath(relPath, fullFilePath);
			String dirName = scope String();
			Path.GetDirectoryPath(fullFilePath, dirName);
            Directory.CreateDirectory(dirName).IgnoreError();

			String fileText = scope String();
			fileText.Append("namespace ");
			String namespaceName = scope String();
            folder.GetRelDir(namespaceName); namespaceName.Replace('/', '.'); namespaceName.Replace('\\', '.'); namespaceName.Replace(" ", "");
			if (namespaceName.StartsWith("src."))
			{
                namespaceName.Remove(0, 4);
				if (!project.mBeefGlobalOptions.mDefaultNamespace.IsWhiteSpace)
				{
					namespaceName.Insert(0, ".");
					namespaceName.Insert(0, project.mBeefGlobalOptions.mDefaultNamespace);
				}
			}
			else if (folder.mParentFolder == null)
			{
				namespaceName.Clear();
				namespaceName.Append(project.mBeefGlobalOptions.mDefaultNamespace);
			}
			else
				namespaceName.Clear();
            fileText.Append(namespaceName, Environment.NewLine);
            fileText.Append("{", Environment.NewLine);
            fileText.Append("\tclass ", className, Environment.NewLine);
            fileText.Append("\t{", Environment.NewLine);
            fileText.Append("\t}", Environment.NewLine);
            fileText.Append("}", Environment.NewLine);

			if (File.Exists(fullFilePath))
			{
				var error = scope String();
				error.AppendF("File '{0}' already exists", fullFilePath);
				IDEApp.sApp.Fail(error);
				return;
			}

			if (File.WriteAllText(fullFilePath, fileText) case .Err)
			{
				var error = scope String();
				error.AppendF("Failed to create file '{0}'", fullFilePath);
				gApp.Fail(error);
				return;
			}

            ProjectSource projectSource = new ProjectSource();
			projectSource.mIncludeKind = (folder.mIncludeKind == .Auto) ? .Auto : .Manual;
            projectSource.mName.Set(relFileName);
			projectSource.mPath = new String();
            folder.mProject.GetProjectRelPath(fullFilePath, projectSource.mPath);
            projectSource.mProject = folder.mProject;
            projectSource.mParentFolder = folder;
            folder.AddChild(projectSource);
            let projectItem = AddProjectItem(projectSource);
			if (projectItem != null)
			{
				mListView.GetRoot().SelectItemExclusively(projectItem);
				mListView.EnsureItemVisible(projectItem, false);
			}
            Sort();
			if (folder.mIncludeKind != .Auto)
            	folder.mProject.SetChanged();

			gApp.RecordHistoryLocation(true);
            gApp.ShowProjectItem(projectSource);
			gApp.RecordHistoryLocation(true);
        }

        int CompareListViewItem(ListViewItem left, ListViewItem right)
        {
            WorkspaceFolder leftFolder;
			mListViewToWorkspaceFolderMap.TryGetValue(left, out leftFolder);
			WorkspaceFolder rightFolder;
			mListViewToWorkspaceFolderMap.TryGetValue(right, out rightFolder);
			if ((leftFolder != null) && (rightFolder != null))
			    return String.Compare(leftFolder.mName, rightFolder.mName, true);

			ProjectItem leftProjectItem;
			mListViewToProjectMap.TryGetValue(left, out leftProjectItem);
			ProjectItem rightProjectItem;
			mListViewToProjectMap.TryGetValue(right, out rightProjectItem);

			if (leftProjectItem == null && leftFolder == null)
			    return -1;
			else if (rightFolder != null)
			    return 1;
			if (rightProjectItem == null && rightFolder == null)
			    return 1;
			else if (leftFolder != null)
			    return -1;

			if ((leftProjectItem.mParentFolder == null) && (rightProjectItem.mParentFolder == null))
			{
				return String.Compare(leftProjectItem.mProject.mProjectName, rightProjectItem.mProject.mProjectName, true);
			}

            return ProjectItem.Compare(leftProjectItem, rightProjectItem);
        }

		void DoSortItem(ProjectListViewItem listViewItem)
		{
			if (listViewItem.mChildItems == null)
			    return;

			if (listViewItem.mSortDirty)
			{
			    if (listViewItem.mChildItems.Count > 1)
			        listViewItem.mChildItems.Sort(scope => CompareListViewItem);
				listViewItem.mSortDirty = false;
			}

			for (var child in listViewItem.mChildItems)
			{
				var childItem = (ProjectListViewItem)child;
			    DoSortItem(childItem);
			}
			mListView.mListSizeDirty = true;
		}

        public void SortItem(ProjectListViewItem listViewItem)
        {
            listViewItem.mSortDirty = true;
			DoSortItem(listViewItem);
        }

		public void QueueSortItem(ProjectListViewItem listViewItem)
		{
			mSortDirty = true;
			listViewItem.mSortDirty = true;
		}

        public void Sort()
        {
            /*for (var project in IDEApp.sApp.mWorkspace.mProjects)
                project.mRootFolder.SortItems();*/
            DoSortItem((ProjectListViewItem)mListView.GetRoot());
            mListView.Resized();
        }

        public bool AlreadyHasPath(ProjectFolder folder, String relPath)
        {
            for (var childItem in folder.mChildItems)
            {
                ProjectSource projectSource = childItem as ProjectSource;
                if (projectSource != null)
                {
                    if (projectSource.mPath == relPath)
                        return true;
                }

                ProjectFolder childFolder = childItem as ProjectFolder;
                if (childFolder != null)
                    if (AlreadyHasPath(childFolder, relPath))
                        return true;
            }
            return false;
        }

        public bool AlreadyHasPath(Project project, String relPath)
        {            
            if (AlreadyHasPath(project.mRootFolder, relPath))
                return true;
            return false;
        }

        void ShowAlreadyHadFileError(List<String> alreadyHadFileList, int totalFileCount)
        {
            if (alreadyHadFileList.Count > 0)
            {
                String errorStr;
                if (alreadyHadFileList.Count == 1)
                    errorStr = StackStringFormat!("Project already contained file: {0}", alreadyHadFileList[0]);
                else
                    errorStr = StackStringFormat!("Project already contained {0} of the {1} files specified", alreadyHadFileList.Count, totalFileCount);
                IDEApp.sApp.Fail(errorStr);
            }
        }

        void CheckProjectSource(ProjectSource projectSource)
        {
            var fullPath = scope String();

            projectSource.GetFullImportPath(fullPath);
            IDEApp.sApp.WithTabs(scope (tab) =>
                {
                    var sourceViewPanel = tab.mContent as SourceViewPanel;
                    if ((sourceViewPanel != null) &&
                        (sourceViewPanel.mProjectSource == null) &&
                        (sourceViewPanel.mFilePath != null) &&
                        (Path.Equals(sourceViewPanel.mFilePath, fullPath)))
                    {
                        sourceViewPanel.AttachToProjectSource(projectSource);
                    }
                });
        }

		bool CheckProjectModify(Project project)
		{
			if (!project.mLocked)
				return true;
			
			let dialog = gApp.Fail(
				"""
				This project is locked because it may be a shared library, and editing shared libraries may have unwanted effects on other programs that use it.

				If you are sure you want to edit this project then you can unlock it by right clicking on the project and deselecting 'Lock Project'
				""",
				null, mWidgetWindow);
			dialog.mWindowFlags |= .Modal;
			return false;
		}

		public void ImportFiles(ProjectFolder folder, String[] fileList)
		{
			String fullDir = scope String();
			folder.GetFullImportPath(fullDir);

			if (mProjectToListViewMap.TryGetValue(folder, var value))
				value.mOpenButton.Open(true, false);

			/*if (mSelectedParentItem.mOpenButton != null)
			    mSelectedParentItem.mOpenButton.Open(true, false);*/

			String dir = scope String(fullDir);

			List<String> alreadyHadFileList = scope List<String>();
			defer ClearAndDeleteItems(alreadyHadFileList);

			for (String fileNameIn in fileList)
			{
			    String fileName = scope String(fileNameIn);
			    String relFilePath = scope String();
			    folder.mProject.GetProjectRelPath(fileName, relFilePath);
			    if (AlreadyHasPath(folder.mProject, relFilePath))
			    {
			        alreadyHadFileList.Add(new String(fileName));
			        continue;
			    }
			    
			    ProjectSource projectSource = new ProjectSource();
			    int32 lastSlashIdx = (int32)fileName.LastIndexOf(IDEUtils.cNativeSlash);
			    if (lastSlashIdx != -1)
			    {
					projectSource.mName.Clear();
			    	projectSource.mName.Append(fileName, lastSlashIdx + 1);
					dir.Clear();
			        dir.Append(fileName, 0, lastSlashIdx);
			    }
				else
					projectSource.mName.Set(fileName);
				projectSource.mIncludeKind = .Manual;
			    projectSource.mProject = folder.mProject;
			    projectSource.mPath = new String(relFilePath);
			    folder.AddChild(projectSource);
			    AddProjectItem(projectSource);

				if (projectSource.mIncludeKind != .Auto)
			    	folder.mProject.SetChanged();
			}
			ShowAlreadyHadFileError(alreadyHadFileList, fileList.Count);

			folder.mProject.GetProjectRelPath(scope String(dir), dir);
			//folder.mLastImportDir.Set(dir);
			//folder.mProject.mLastImportDir.Set(dir);
			Sort();          
		}

        public void ImportFile(ProjectFolder folder)
        {
#if !CLI
            /*String fullDir = scope String();
            var checkFolder = folder;
            while (checkFolder != null)
            {
                if (fullDir.Length > 0)
                    fullDir.Append(Path.DirectorySeparatorChar);
                fullDir.Append(checkFolder.mName);
                checkFolder = checkFolder.mParentFolder;
            }
            fullDir = scope String(folder.mProject.mProjectDir);
            fullDir.Append(Path.DirectorySeparatorChar);
            fullDir.Append(fullDir);

			String oldStr = fullDir;
			fullDir = scope String();
            folder.mProject.GetProjectFullPath(oldStr, fullDir);*/

			if (!CheckProjectModify(folder.mProject))
				return;

			String fullDir = scope String();
			folder.GetFullImportPath(fullDir);
            //fullDir.Replace('/', '\\');
            
			//TODO:
            var fileDialog = scope OpenFileDialog();
			fileDialog.ShowReadOnly = false;
            fileDialog.Title = "Import File";
            fileDialog.Multiselect = true;
            fileDialog.InitialDirectory = fullDir;            
            fileDialog.ValidateNames = true;
            gApp.GetActiveWindow().PreModalChild();
            if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
            {
                ImportFiles(folder, fileDialog.FileNames);
            }
#endif
        }

        public int32 AddFolder(ProjectFolder folder, String folderFileName, List<String> alreadyHadFileList, bool autoInclude)
        {
			int32 totalFileCount = 0;
#if !CLI
			folder.mProject.SetChanged();

            String folderName = scope String();
            Path.GetFileName(folderFileName, folderName);

            ProjectFolder newFolder = new ProjectFolder();
			newFolder.mIncludeKind = .Manual;
            newFolder.mName.Set(folderName);
            newFolder.mProject = folder.mProject;
			newFolder.mPath = new String();
			folder.mProject.GetProjectRelPath(folderFileName, newFolder.mPath);

            folder.AddChild(newFolder);
            AddProjectItem(newFolder);
			
			if (autoInclude)
			{
				newFolder.mAutoInclude = true;
				RehupFolder(newFolder);
			}
			else
			{
				
				for (var fileEntry in Directory.EnumerateFiles(folderFileName))
				{
					var fileName = scope String();
					fileEntry.GetFileName(fileName);

				    totalFileCount++;
				    //String fileName = fileNameIn.Replace('\\', '/');
					IDEUtils.FixFilePath(fileName);
				    String relFilePath = scope String();
				    folder.mProject.GetProjectRelPath(fileName, relFilePath);
				    if (AlreadyHasPath(folder.mProject, relFilePath))
				    {
				        alreadyHadFileList.Add(relFilePath);
				        continue;
				    }

				    ProjectSource projectSource = new ProjectSource();                
				    projectSource.mProject = folder.mProject;
					projectSource.mIncludeKind = .Manual;
				    int32 lastSlashIdx = (int32)fileName.LastIndexOf(IDEUtils.cNativeSlash);
					if (lastSlashIdx != -1)                
						fileName.Remove(0, lastSlashIdx + 1);                  
				    projectSource.mName.Set(fileName);
				    projectSource.mPath = new String(relFilePath);
				    newFolder.AddChild(projectSource);
				    AddProjectItem(projectSource);
				}

	            for (var fileEntry in Directory.EnumerateDirectories(folderFileName))
	            {
					var dirName = scope String();
					fileEntry.GetFileName(dirName);
	                totalFileCount += AddFolder(newFolder, dirName, alreadyHadFileList, autoInclude);
	            }
			}
#endif

            return totalFileCount;
        }

        public void ImportFolder(ProjectFolder folder)
        {
#if !CLI
			if (!CheckProjectModify(folder.mProject))
				return;

			//ThrowUnimplemented();
            String relDir = scope String("");
            var checkFolder = folder;
            while (checkFolder != null)
            {
                if (relDir != "")
                    relDir.Insert(0, "/");
                relDir.Insert(0, checkFolder.mName);
                checkFolder = checkFolder.mParentFolder;
            }
            relDir.Insert(0, "/");
			relDir.Insert(0, folder.mProject.mProjectDir);

            String fullDir = scope String();
            folder.mProject.GetProjectFullPath(relDir, fullDir);
            //fullDir.Replace('/', '\\');

            var folderDialog = scope FolderBrowserDialog();
            folderDialog.SelectedPath = fullDir;
            mWidgetWindow.PreModalChild();
            if (folderDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
            {
                List<String> alreadyHadFileList = scope List<String>();
                if (mSelectedParentItem.mOpenButton != null)
                    mSelectedParentItem.mOpenButton.Open(true, false);

                String dir = scope String(folderDialog.SelectedPath);
                int32 totalFileCount = AddFolder(folder, dir, alreadyHadFileList, true);
				/*int32 totalFileCount = 1;
				if (AlreadyHasPath(folder, dir))
				{
				    alreadyHadFileList.Add(dir);
				}*/
                ShowAlreadyHadFileError(alreadyHadFileList, totalFileCount);

				relDir.Clear();
                folder.mProject.GetProjectRelPath(dir, relDir);
                //folder.mLastImportDir.Set(relDir);
                //folder.mProject.mLastImportDir.Set(relDir);
				if (folder.mIncludeKind != .Auto)
                	folder.mProject.SetChanged();
                Sort();
            }
#endif
        }

		public WorkspaceFolder GetSelectedWorkspaceFolder()
		{
			ListViewItem selectedItem = mListView.GetRoot().FindFirstSelectedItem();
			if (mListViewToWorkspaceFolderMap.TryGetValue(selectedItem, let folder))
				return folder;
			return null;
		}

        ListViewItem GetSelectedParentItem()
        {
            ListViewItem selectedItem = mListView.GetRoot().FindFirstSelectedItem();
            if (selectedItem != null)
            {
                if (selectedItem.IsParent)
                    return selectedItem;
                return selectedItem.mParentItem;
            }
            return mListView.GetRoot();
        }

		ListViewItem GetSelectedItem()
		{
			return mListView.GetRoot().FindFirstSelectedItem();
		}

		ProjectItem GetSelectedProjectItem()
		{
			let listViewItem = GetSelectedItem();
			if (listViewItem == null)
				return null;
			ProjectItem projectItem;
			mListViewToProjectMap.TryGetValue(listViewItem, out projectItem);
			return projectItem;
		}

		ProjectFolder GetSelectedProjectFolder()
		{
			let projectItem = GetSelectedProjectItem();
			if (projectItem == null)
				return null;
			if (let projectFolder = projectItem as ProjectFolder)
				return projectFolder;
			return projectItem.mParentFolder;
		}

        public void SelectItem(ListViewItem item, bool checkKeyStates = false)
        {
            if (item.Focused)
            {
				if (mListView.GetRoot().GetSelectedItemCount() <= 1)
				{
	                // Just rehup focus handler - handles case of closing file then clicking on it again
	                //  even though it's already selected.  We want that to re-open it.
	                FocusChangedHandler(item);
					return;
				}
            }

            mListView.GetRoot().SelectItem(item, checkKeyStates);
            if (item == null)                            
                return;
            SetFocus();
        }

        public override void LostFocus()
        {
            base.LostFocus();
            //SelectItem(null);
        }

        void RemoveProject(Project project)
        {
            var app = IDEApp.sApp;
            //var workspace = IDEApp.sApp.mWorkspace;
            app.RemoveProject(project);
        }

		void ProjectItemUnregister(ProjectItem projectItem, bool isRemovingProjectSource)
		{
			var projectSource = projectItem as ProjectSource;
			if (projectSource != null)
			{
				bool isProjectSourceEnabled = gApp.IsProjectSourceEnabled(projectSource);

			    String fullPath = scope String();
			    projectSource.GetFullImportPath(fullPath);
				gApp.mWorkspace.mForceNextCompile = true;

			    if ((IDEApp.IsBeefFile(fullPath)) && (isProjectSourceEnabled))
			    {
					var compilers = scope List<BfCompiler>();
					IDEApp.sApp.GetBfCompilers(compilers);
			        for (var bfCompiler in compilers)
			        {
			            bfCompiler.QueueProjectSourceRemoved(projectSource);
			        }
					gApp.mBfResolveCompiler?.QueueDeferredResolveAll();
			    }
#if IDE_C_SUPPORT
			    else if (IDEApp.IsClangSourceFile(fullPath))
			    {                    
			        IDEApp.sApp.mDepClang.QueueProjectSourceRemoved(projectSource);
			    }
#endif

			    gApp.WithTabs(scope (tab) =>
			        {
			            var sourceViewPanel = tab.mContent as SourceViewPanel;
			            if ((sourceViewPanel != null) && (sourceViewPanel.mProjectSource == projectSource))
			            {
							if (isRemovingProjectSource)
			                	sourceViewPanel.DetachFromProjectItem(true);
							else
								sourceViewPanel.QueueFullRefresh(true);
			            }
			        });

				if (isProjectSourceEnabled)
				{
					if (isRemovingProjectSource)
					{
				        gApp.mBfResolveHelper.ProjectSourceRemoved(projectSource);
						gApp.mWorkspace.ProjectSourceRemoved(projectSource);
					}
			        gApp.RefreshVisibleViews();
				}
			}
		}

		void ProjectItemRegister(Project projectItem)
		{

		}

		enum DeleteItemKind
		{
			Normal,
			ForceRemove,
			Ignore
		}

        public void DoDeleteItem(ListViewItem listItem, delegate bool(String path) deletePathFunc, DeleteItemKind deleteKind = .Normal)
        {
			var projectItemRoot = mListViewToProjectMap[listItem];
			var projectItem = projectItemRoot;

			if ((projectItem != null) && (projectItem.mParentFolder == null))
			{
				// Removing full project
				mProjectToListViewMap.Remove(projectItem);
				mListViewToProjectMap.Remove(listItem);
				listItem.mParentItem.RemoveChildItem(listItem);
				Project project = projectItem.mProject;

				if (mProjectToWorkspaceFolderMap.GetAndRemove(projectItem) case .Ok((?, let workspaceFolder)))
					workspaceFolder.mProjects.Remove(project);

				gApp.WithTabs(scope (tab) =>
					{
					    var sourceViewPanel = tab.mContent as SourceViewPanel;
					    //if (sourceViewPanel?.mProjectSource?.mProject == project)
						if	((sourceViewPanel != null) &&
							 (sourceViewPanel.mProjectSource != null) &&
							 (sourceViewPanel.mProjectSource.mProject == project))
					    {
					        sourceViewPanel.DetachFromProjectItem(true);
					    }
					});

				project.mRootFolder.Dispose();
			    RemoveProject(project);
				return;
			}

            if (listItem.mChildItems != null)
            {
				for (int childIdx = listItem.mChildItems.Count - 1; childIdx >= 0; childIdx--)
                {
					var childDeleteKind = deleteKind;
					if (childDeleteKind == .Normal)
						childDeleteKind = .ForceRemove;
					DoDeleteItem(listItem.mChildItems[childIdx], deletePathFunc, childDeleteKind);
				}
            }
			
            if (projectItem == null)
                return;

			bool doReleaseRef = false;
			bool didRemove = false;
			if ((deleteKind == .ForceRemove) || (projectItem.mParentFolder == null) || (projectItem.mParentFolder.mIncludeKind == .Manual) || (projectItem.mIncludeKind == .Manual) || (deletePathFunc != null))
			{
				bool doRemove = true;

				if (deletePathFunc != null)
				{
					if (let projectFileItem = projectItem as ProjectFileItem)
					{
                        var path = scope String();
						projectFileItem.GetFullImportPath(path);
						doRemove = deletePathFunc(path);
					}
				}

				if (doRemove)
				{
		            if (projectItem.mParentFolder != null)
		                projectItem.mParentFolder.RemoveChild(projectItem);
					
					mProjectToListViewMap.Remove(projectItem);
					mListViewToProjectMap.Remove(listItem);

					if (projectItem.mIncludeKind != .Auto)
						projectItem.mProject.SetChanged();

					doReleaseRef = true;
					didRemove = true;
				}
			}
			else
			{
				if (let projectSource = projectItem as ProjectSource)
				{
					String path = scope .();
					projectSource.GetFullImportPath(path);
					gApp.mErrorsPanel.ClearParserErrors(path);
				}
			}

			ProjectItemUnregister(projectItem, didRemove);

			if ((didRemove) || (!mShowIgnored))
			{
	            listItem.mParentItem.RemoveChildItem(listItem);
				using (gApp.mMonitor.Enter())
	            	projectItem.Dispose();
			}

			if (doReleaseRef)
			{
				projectItem.mDetached = true;
				projectItem.ReleaseRef();
			}
			//TODO: Defer this, projectItem is needed for a backgrounded QueueProjectSourceRemoved
			//delete projectItem;
        }

		void RemoveSelectedWorkspaceFolder()
		{
			uint32 selectedCount = 0;
			WorkspaceFolder selectedFolder = null;
			mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) => {
			    if (mListViewToWorkspaceFolderMap.GetValue(selectedItem) case .Ok(let folder))
			    {
			        selectedCount++;
					selectedFolder = folder;
			    }
			});

			if (selectedCount == 0)
				return;

			String title;
			String text;
			if (selectedCount > 1)
			{
				title = "Delete Folders";
				text = "Delete selected folders from workspace?";
			}
			else
			{
				title = "Delete Folder";
				text = scope:: $"Delete '{selectedFolder.mName}' from workspace?";
			}

		    Dialog aDialog = ThemeFactory.mDefault.CreateDialog(title, text);                 

		    aDialog.mDefaultButton = aDialog.AddButton("Yes", new (evt) =>
		        {
		            aDialog.Close();
		            let root = mListView.GetRoot();
		            List<ListViewItem> itemsToMove = scope .();
		            List<WorkspaceFolder> foldersToDelete = scope .();

		            root.WithSelectedItems(scope [&] (selectedItem) => {
		                if (mListViewToWorkspaceFolderMap.GetValue(selectedItem) case .Ok(let folder))
		                {
		                    foldersToDelete.Add(folder);
							mListViewToWorkspaceFolderMap.Remove(folder.mListView);
		                    selectedItem.WithItems(scope [&] (item) => {
		                        if (mListViewToProjectMap.GetValue(item) case .Ok(let project))
		                        {
		                            if (project.mParentFolder == null)
		                                itemsToMove.Add(item);
		                        }
								else if (mListViewToWorkspaceFolderMap.GetValue(item) case .Ok(let itemFolder))
								{
									foldersToDelete.Add(itemFolder);
									mListViewToWorkspaceFolderMap.Remove(itemFolder.mListView);
								}
		                    });
		                }
		            });

		            for (let listViewItem in itemsToMove)
		            {
		                listViewItem.mParentItem.RemoveChildItem(listViewItem, false);
		                root.AddChildAtIndex(1, listViewItem);
						if (mListViewToProjectMap.TryGetValue(listViewItem, let projectItem))
							mProjectToWorkspaceFolderMap.Remove(projectItem);
		            }

					bool HasDeletedParent(WorkspaceFolder folder)
					{
						WorkspaceFolder parent = folder;
						repeat
						{
							parent = parent.mParent;

							if (foldersToDelete.Contains(parent))
								return true;
						}
						while (parent != null);

						return false;
					}

					for (let folder in foldersToDelete)
		            {
						if (!HasDeletedParent(folder))
						{
							let folderItem = folder.mListView;
							folderItem.mParentItem.RemoveChildItem(folderItem);
						}

		               	gApp.mWorkspace.mWorkspaceFolders.Remove(folder);
		                delete folder;
		            }

					if ((itemsToMove.Count > 0) || (foldersToDelete.Count > 0))
					{
						QueueSortItem((ProjectListViewItem)root);
						gApp.mWorkspace.SetChanged();
					}	
						
		        });
		    aDialog.mEscButton = aDialog.AddButton("No");
		    aDialog.PopupWindow(gApp.GetActiveWindow());
		}

        void RemoveSelectedItems(bool deleteFiles)
        {
            List<ListViewItem> selectedItems = scope List<ListViewItem>();
            //selectedItems.AddRange(mListView.GetRoot().FindSelectedItems(true));

            mListView.GetRoot().WithSelectedItems(scope (listViewItem) =>
                {
                    selectedItems.Add(listViewItem);
                }, true);

			int fileErrorCount = 0;
			String fileErrors = scope String();
			bool hadOverflow = false;

            for (var selectedItem in selectedItems)
            {
				//var projectItem = mListViewToProjectMap[selectedItem];
				if (!deleteFiles)
				{
					mListViewToProjectMap.TryGetValue(selectedItem, var projectItem);
					if ((projectItem != null) && (projectItem.mIncludeKind == .Auto))
					{
						DoDeleteItem(selectedItem, null, .Ignore);
					    projectItem.mIncludeKind = .Ignore;
						projectItem.mProject.SetChanged();
					}
					else
						DoDeleteItem(selectedItem, null);
				}
				else
				{
	                DoDeleteItem(selectedItem, scope [&] (filePath) =>
						{
							if (File.Delete(filePath) case .Ok)
								return true;
							if (Directory.Delete(filePath) case .Ok)
								return true;

							fileErrorCount++;
							const int maxErrorLen = 4096;
							if ((fileErrors.Length < maxErrorLen) && (!hadOverflow))
							{
								if (!fileErrors.IsEmpty)
									fileErrors.Append(", ");

								if (fileErrors.Length + filePath.Length < maxErrorLen)
									fileErrors.Append(filePath);
								else
								{
									fileErrors.Append("...");
									hadOverflow = true;
								}
							}
							return false;
						});
				}
				/*if (projectItem.mIncludeKind == .Auto)
				{
					projectItem.mIncludeKind = .Ignore;
					projectItem.mProject.SetChanged();
				}*/
            }
			
			if (!fileErrors.IsEmpty)
			{
				var errorStr = scope String();
				if (fileErrorCount == 1)
					errorStr.AppendF("Failed to delete file: {0}", fileErrors);
				else
					errorStr.AppendF("Failed to delete {0} files: {1}", fileErrorCount, fileErrors);
				gApp.Fail(errorStr);
			}
			
        }

		void RemoveSelectedItems()
		{
		    int32 projectCount = 0;
		    int32 fileCount = 0;
		    int32 folderCount = 0;
		    bool hadProjectItemsSelected = false;
			int32 workspaceFolderCount = 0;

		    HashSet<Project> projectsReferenced = scope HashSet<Project>();

		    ProjectItem selectedProjectItem = null;
		    
		    mListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
		        {
		            mListViewToProjectMap.TryGetValue(selectedItem, out selectedProjectItem);
		            if (selectedProjectItem != null)
		            {                           
		                if (selectedProjectItem.mParentFolder == null)
		                {
		                    projectCount++;
		                    hadProjectItemsSelected = selectedItem.FindLastSelectedItem() != selectedItem;
		                }
		                else if (selectedProjectItem is ProjectFolder)
		                {
		                    projectsReferenced.Add(selectedProjectItem.mProject);
		                    folderCount++;
		                }
		                else
		                {
		                    projectsReferenced.Add(selectedProjectItem.mProject);
		                    fileCount++;
		                }
		            }
					else if (mListViewToWorkspaceFolderMap.ContainsKey(selectedItem))
					{
						workspaceFolderCount++;
					}
		        }, true);
		    
		    if (selectedProjectItem != null)
		    {
		        if (projectCount > 0)
		        {
		            if (!hadProjectItemsSelected)
		            {
						if (gApp.IsCompiling)
							return;

		                Dialog aDialog;

		                if (projectCount == 1)
		                {
		                    aDialog = ThemeFactory.mDefault.CreateDialog("Remove Project", StackStringFormat!("Remove project '{0}' from the workspace?", selectedProjectItem.mProject.mProjectName));
		                }
		                else
		                {
		                    aDialog = ThemeFactory.mDefault.CreateDialog("Remove Projects", "Remove the selected projects from the workspace?");
		                }

		                aDialog.mDefaultButton = aDialog.AddButton("Yes", new (evt) => RemoveSelectedItems(false));
		                aDialog.mEscButton = aDialog.AddButton("No");
		                aDialog.PopupWindow(gApp.GetActiveWindow());
		            }
		        }
		        else if((fileCount > 0) || (folderCount > 0))
		        {                                                
		            Dialog aDialog;
		            if (fileCount + folderCount == 1)
		            {
		                aDialog = ThemeFactory.mDefault.CreateDialog((fileCount > 0) ? "Delete File" : "Delete Folder",
		                    StackStringFormat!("Choose Remove to remove '{0}' from '{1}'.\n\nChoose Delete to permanently delete '{0}'.", selectedProjectItem.mName, selectedProjectItem.mProject.mProjectName));
		            }
		            else
		            {
		                String typeDeleting = null; //TODO: Why do we need this assignment?
		                String title = null;
		                if (folderCount == 0)
		                {                                    
		                    title = "Delete Files";
		                    typeDeleting = "files";
		                }
		                else if (fileCount == 0)
		                {                                    
		                    title = "Delete Folders";
		                    typeDeleting = "folders";
		                }
		                else
		                {                                    
		                    title = "Delete Items";
		                    typeDeleting = "items";
		                }

		                if (projectsReferenced.Count == 1)
		                {
		                    aDialog = ThemeFactory.mDefault.CreateDialog(title,
		                        StackStringFormat!("Choose Remove to remove the selected {0} from '{1}'.\n\nChoose Delete to permanently delete the selected items.", typeDeleting, selectedProjectItem.mProject.mProjectName));
		                }
		                else
		                {
		                    aDialog = ThemeFactory.mDefault.CreateDialog(title,
		                        StackStringFormat!("Choose Remove to removed the selected {0}.\n\nChoose Delete to permanently delete the selected items.", typeDeleting));
		                }
		            }                            

		            aDialog.mDefaultButton = aDialog.AddButton("Remove", new (evt) =>
						{
							aDialog.Close();
							RemoveSelectedItems(false);
						});
		            aDialog.AddButton("Delete", new (evt) =>
						{
							aDialog.Close();
							RemoveSelectedItems(true);
						});
		            aDialog.mEscButton = aDialog.AddButton("Cancel");
		            aDialog.PopupWindow(gApp.GetActiveWindow());
		        }                    
		    }
            else if (workspaceFolderCount > 0)
			{
				RemoveSelectedWorkspaceFolder();
			}
		}

		void CopyToClipboard()
		{
			String clipData = scope .();
			mListView.GetRoot().WithSelectedItems(scope (selectedItem) =>
				{
					if (mListViewToProjectMap.GetValue(selectedItem) case .Ok(var sourceProjectItem))
					{
						String path = scope .();
						if (var projectFileItem = sourceProjectItem as ProjectFileItem)
						{
							sourceProjectItem.mProject.GetProjectFullPath(projectFileItem.mPath, path);
							path.Replace('\\', '/');

							if (!clipData.IsEmpty)
								clipData.Append("\n");
							clipData.Append("file:///");
							IDEUtils.URLEncode(path, clipData);
						}
					}
				});
			if (!clipData.IsEmpty)
				gApp.SetClipboardData("code/file-list", clipData.Ptr, (.)clipData.Length, true);
		}

		void CutToClipboard()
		{
			DeleteContainerAndItems!(mClipboardCutQueued);
			mClipboardCutQueued = null;

			CopyToClipboard();
			mClipboardCutQueued = new .();
			ValidateCutClipboard();
		}

		void ValidateCutClipboard()
		{
			if (mClipboardCutQueued == null)
				return;

			void* data = gApp.GetClipboardData("code/file-list", var size, 0);
			if (size == -1)
				return;

			ClearAndDeleteItems!(mClipboardCutQueued);

			if (data != null)
			{
				StringView sv = .((.)data, size);
				for (var line in sv.Split('\n'))
				{
					var uri = IDEUtils.URLDecode(line, .. scope .());
					if (uri.StartsWith("file:///"))
					{
						var srcPath = scope String()..Append(uri.Substring("file:///".Length));
						IDEUtils.MakeComparableFilePath(srcPath);
						if (mClipboardCutQueued.TryAddAlt(srcPath, var entryPtr))
							*entryPtr = new String(srcPath);
					}
				}
			}

			if (mClipboardCutQueued.IsEmpty)
				DeleteAndNullify!(mClipboardCutQueued);
		}

		void PasteFromClipboard()
		{
			ValidateCutClipboard();

			var projectItem = GetSelectedProjectItem();
			var projectFolder = projectItem as ProjectFolder;
			if (projectFolder == null)
				projectFolder = projectItem.mParentFolder;
			if (projectFolder == null)
				return;

			var folderPath = projectFolder.GetFullImportPath(.. scope .());

			void* data = gApp.GetClipboardData("code/file-list", var size);
			if (data == null)
				return;

			bool isCut = mClipboardCutQueued != null;
			Dictionary<String, SourceViewPanel> sourceViewPanelMap = null;
			List<(SourceViewPanel sourceViewPanel, String fromPath, String toPath)> moveList = null;

			HashSet<String> foundDirs = scope .();

			if (isCut)
			{
				sourceViewPanelMap = scope:: .();
				gApp.WithSourceViewPanels(scope (sourceViewPanel) =>
					{
						if (sourceViewPanel.mFilePath === null)
							return;
						if (sourceViewPanel.mProjectSource == null)
							return;

						var path = scope String()..Append(sourceViewPanel.mFilePath);
						IDEUtils.MakeComparableFilePath(path);
						if (sourceViewPanelMap.TryAdd(path, var keyPtr, var valuePtr))
						{
							*keyPtr = new String(path);
							*valuePtr = sourceViewPanel;
						}
					});

				moveList = scope:: .();
			}

			defer
			{
				DeleteContainerAndItems!(mClipboardCutQueued);
				mClipboardCutQueued = null;

				if (sourceViewPanelMap != null)
				{
					for (var key in sourceViewPanelMap.Keys)
						delete key;
				}

				if (moveList != null)
				{
					for (var val in moveList)
					{
						delete val.fromPath;
						delete val.toPath;
					}
				}

				ClearAndDeleteItems!(foundDirs);
			}

			void QueueDirectoryMove(StringView fromDir, StringView toDir)
			{
				var searchStr = scope String();
				searchStr.Append(fromDir);
				searchStr.Append("/*");
				for (var dirEntry in Directory.Enumerate(searchStr, .Directories | .Files))
				{
					var fromChildPath = dirEntry.GetFilePath(.. scope .());

					String toChildPath = scope String()..Append(toDir);
					toChildPath.Append(Path.DirectorySeparatorChar);
					dirEntry.GetFileName(toChildPath);

					if (dirEntry.IsDirectory)
					{
						QueueDirectoryMove(fromChildPath, toChildPath);
						continue;
					}

					var cmpPath = IDEUtils.MakeComparableFilePath(.. scope String()..Append(fromChildPath));
					if (sourceViewPanelMap.TryGet(cmpPath, var matchKey, var sourceViewPanel))
					{
						moveList.Add((sourceViewPanel, new String(fromChildPath), new String(toChildPath)));
					}
				}
			}

			Result<void> CopyDirectory(StringView fromDir, StringView toDir)
			{
				if (Directory.CreateDirectory(toDir) case .Err)
				{
					gApp.Fail(scope $"Failed to create directory '{toDir}'");
					return .Err;
				}

				var searchStr = scope String();
				searchStr.Append(fromDir);
				searchStr.Append("/*");
				for (var dirEntry in Directory.Enumerate(searchStr, .Directories | .Files))
				{
					var fromChildPath = dirEntry.GetFilePath(.. scope .());

					String toChildPath = scope String()..Append(toDir);
					toChildPath.Append("/");
					dirEntry.GetFileName(toChildPath);

					if (dirEntry.IsDirectory)
					{
						CopyDirectory(fromChildPath, toChildPath);
						continue;
					}

					if (File.Copy(fromChildPath, toChildPath) case .Err)
					{
						gApp.Fail(scope $"Failed to copy '{fromChildPath}' to '{toChildPath}'");
						return .Err;
					}
				}

				return .Ok;
			}

			StringView sv = .((.)data, size);
			SrcLoop: for (var line in sv.Split('\n'))
			{
				var uri = IDEUtils.URLDecode(line, .. scope .());
				if (uri.StartsWith("file:///"))
				{
					var srcPath = scope String()..Append(uri.Substring("file:///".Length));

					for (int i < 100)
					{
						var fileName = Path.GetFileNameWithoutExtension(srcPath, .. scope .());
						var destPath = scope String();
						destPath.Append(folderPath);
						destPath.Append("/");
						destPath.Append(fileName);
						if ((i > 0) && (!fileName.Contains(" - Copy")))
							destPath.Append(" - Copy");
						if (i > 1)
							destPath.AppendF($" ({i})");
						Path.GetExtension(srcPath, destPath);

						IDEUtils.FixFilePath(srcPath);
						IDEUtils.FixFilePath(destPath);

						if ((isCut) && (Path.Equals(srcPath, destPath)))
							break;

						if (File.Exists(destPath))
							continue;
						if (Directory.Exists(destPath))
							continue;

						if (Directory.Exists(srcPath))
						{
							if (foundDirs.TryAdd(srcPath, var entryPtr))
								*entryPtr = new String(srcPath);

							if (isCut)
							{
								QueueDirectoryMove(srcPath, destPath);

								if (Directory.Move(srcPath, destPath) case .Err)
								{
									gApp.Fail(scope $"Failed to move '{srcPath}' to '{destPath}'");
									return;
								}

								for (var val in moveList)
								{
									gApp.FileRenamed(val.sourceViewPanel.mProjectSource, val.fromPath, val.toPath);
								}
							}
							else
							{
								if (CopyDirectory(srcPath, destPath) case .Err)
								{
									return;
								}
							}
						}
						else
						{
							var checkPath = scope String()..Append(srcPath);
							while (true)
							{
								String checkDir = scope .();
								if (Path.GetDirectoryPath(checkPath, checkDir) case .Err)
									break;
								if (foundDirs.Contains(checkDir))
								{
									// Already handled
									continue SrcLoop; 
								}
								checkPath.Set(checkDir);
							}

							if (isCut)
							{
								if (File.Move(srcPath, destPath) case .Err)
								{
									gApp.Fail(scope $"Failed to move '{srcPath}' to '{destPath}'");
									return;
								}

								var cmpPath = IDEUtils.MakeComparableFilePath(.. scope String()..Append(srcPath));
								if (sourceViewPanelMap.TryGet(cmpPath, var matchKey, var sourceViewPanel))
								{
									gApp.FileRenamed(sourceViewPanel.mProjectSource, srcPath, destPath);
								}
							}
							else
							{
								if (File.Copy(srcPath, destPath) case .Err)
								{
									gApp.Fail(scope $"Failed to copy '{srcPath}' to '{destPath}'");
									return;
								}
							}
						}

						break;
					}
				}
			}
		}

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            mListView.KeyDown(keyCode, isRepeat);

			if (keyCode == .Apps)
			{
				ShowRightClickMenu(mListView);
				return;
			}

            base.KeyDown(keyCode, isRepeat);

			if (mWidgetWindow.GetKeyFlags() == .Ctrl)
			{
				switch (keyCode)
				{
				case (.)'C':
					CopyToClipboard();
				case (.)'X':
					CutToClipboard();
				case (.)'V':
					PasteFromClipboard();
				default:
				}
			}
			else if (mWidgetWindow.GetKeyFlags() == .None)
			{
				if (keyCode == KeyCode.Delete)
					RemoveSelectedItems();
			}
        }

        void ItemClicked(MouseEvent theEvent)
        {
			SetFocus();

            ListViewItem prevSelectedItem = (ListViewItem)mListView.GetRoot().FindFirstSelectedItem();
            
            DarkListViewItem clickedItem = (DarkListViewItem)theEvent.mSender;
            if (clickedItem.mColumnIdx != 0)
            {
                if ((clickedItem.mColumnIdx == 1) && (prevSelectedItem == clickedItem.GetSubItem(0)))
                {
                    EditListViewItem(clickedItem);
                    return;
                }

                clickedItem = (DarkListViewItem)clickedItem.GetSubItem(0);
            }

			if (theEvent.mBtn != 0)
			{
				if (clickedItem.Selected)
					return;
			}

            SelectItem(clickedItem, true);

            if ((!clickedItem.IsParent) && (theEvent.mBtnCount > 1))
            {
                ProjectItem aProjectItem;
                mListViewToProjectMap.TryGetValue(clickedItem, out aProjectItem);
                if (aProjectItem != null)
                {
                    IDEApp.sApp.ShowProjectItem(aProjectItem, false, true);
                }
            }
        }

        void DeselectFolder(BFWindow window)
        {
            ListViewItem selectedItem = mListView.GetRoot().FindFirstSelectedItem();
            if ((selectedItem != null) && (selectedItem.IsParent))
                selectedItem.Selected = false;
        }        

		void FinishRenameFolder(String newValue, bool doMove)
		{
			bool failed = false;

			var projectFolder = GetSelectedProjectItem() as ProjectFolder;
			if (projectFolder == null)
				return;

			if (doMove)
			{
				String fileName = scope String();
				Path.GetFileName(projectFolder.mPath, fileName);
				if (fileName == projectFolder.mName)
				{
					String importDir = scope String();
					Path.GetDirectoryPath(projectFolder.mPath, importDir);
					importDir.Append("/", newValue);

					String oldFullName = scope String();
					projectFolder.mProject.GetProjectFullPath(projectFolder.mPath, oldFullName);
					String newFullName = scope String();
					projectFolder.mProject.GetProjectFullPath(importDir, newFullName);

					if (Directory.Exists(oldFullName))
					{
						if (Directory.Move(oldFullName, newFullName) case .Err)
							failed = true;
					}

					//if (!failed)
				    //String.NewOrSet!(projectFolder.mPath, importDir);
				}
			}
			else
			{
				//projectFolder.mName.Set(newValue);
			}

			if (failed)
			{
			    //IDEApp.sApp.Fail(ex.Message.Replace("\r", ""));
				IDEApp.sApp.Fail("Failed to rename item");
			    IDEApp.Beep(IDEApp.MessageBeepType.Error);
			    return;
			}

			// Item renamed
			/*listViewItem.Label = newValue;
			if (projectFolder.mIncludeKind != .Auto)
				projectFolder.mProject.SetChanged();
			Sort();*/
		}

        void HandleEditDone(EditWidget editWidget, bool cancelled)
        {
            String newValue = scope String();
            editWidget.GetText(newValue);
            newValue.Trim();

			if (newValue.IsEmpty)
				return;

			/*editWidget.RemoveSelf();
			gApp.DeferDelete(editWidget);*/

            ListViewItem listViewItem = mListView.mEditingItem;
            int32 column = listViewItem.mColumnIdx;

			if (mListViewToWorkspaceFolderMap.GetValue(listViewItem.GetMainItem()) case .Ok(let folder))
			{
				if ((!mListView.mCancelingEdit) && (listViewItem.mLabel != newValue))
				{
					if (WorkspaceFolder.IsNameValid(newValue))
					{
						if (IsWorkspaceFolderNameUnique(newValue, listViewItem.GetMainItem()))
						{
							listViewItem.Label = newValue;
							folder.mName.Set(newValue);
							QueueSortItem((ProjectListViewItem)listViewItem.mParentItem);
							gApp.mWorkspace.SetChanged();
						}
						else
						{
							IDEApp.sApp.Fail(scope $"Workspace already contains folder named '{newValue}'");
							IDEApp.Beep(IDEApp.MessageBeepType.Error);
						}
					}	
					else
					{
						IDEApp.sApp.Fail(scope $"Folder name contains invalid characters!");
						IDEApp.Beep(IDEApp.MessageBeepType.Error);
					}
				}
				return;
			}

            ProjectItem projectItem = mListViewToProjectMap[listViewItem.GetMainItem()];
            if (projectItem == null)
                return;

			bool didRename = true;

            if ((!mListView.mCancelingEdit) && (listViewItem.mLabel != newValue)) 
            {
                if (column == 0)
                {
					bool failed = false;

					RenameBlock: do
                    {
						var projectFolder = projectItem as ProjectFolder;
						if ((projectFolder != null) && (projectFolder.mParentFolder == null))
						{
							gApp.RenameProject(projectFolder.mProject, newValue);
							break;
						}

                        var projectSource = projectItem as ProjectSource;
                        if (projectSource != null)
                        {
                            var prevPath = scope String();
                            projectSource.mProject.GetProjectFullPath(projectSource.mPath, prevPath);
							projectSource.mHasChangedSinceLastCompile = true;
                                                        
                            var nameParts = scope List<StringView>(projectSource.mPath.Split(IDEUtils.cNativeSlash, IDEUtils.cOtherSlash));
                            nameParts[nameParts.Count - 1] = StringView(newValue);

                            String newRelPath = scope String();
                            newRelPath.Join("/", nameParts.GetEnumerator());

                            var newPath = scope String();
                            projectSource.mProject.GetProjectFullPath(newRelPath, newPath);

                            /*if ((IDEApp.IsBeefFile(prevPath) != IDEApp.IsBeefFile(newPath)) ||
                                (IDEApp.IsClangSourceFile(prevPath) != IDEApp.IsClangSourceFile(newPath)))
                            {
                                IDEApp.sApp.Fail(StackStringFormat!("Invalid file extension change, cannot rename '{0}' to '{1}'", prevPath, newPath));
                                return;
                            }*/

							if (File.Exists(prevPath))
							{
	                            if (File.Move(prevPath, newPath) case .Err)
								{
									failed = true;
									break;
								}
							}

                            if (IDEApp.IsBeefFile(prevPath))
                            {
								var bfSystems = scope List<BfSystem>();
								IDEApp.sApp.GetBfSystems(bfSystems);
                                for (var system in bfSystems)
                                    system.FileRenamed(projectSource, prevPath, newPath);
                            }                            

							projectSource.Rename(newValue);
                            //projectSource.mName.Set(newValue);
                            //projectSource.mPath.Set(newRelPath);
                            //Sort();

#if IDE_C_SUPPORT
                            if (IDEApp.IsClangSourceFile(prevPath))
                            {
                                var buildClang = IDEApp.sApp.mDepClang;
                                buildClang.QueueFileRemoved(prevPath);
                                buildClang.QueueProjectSource(projectSource);
                            }
#endif

							IDEApp.sApp.FileRenamed(projectSource, prevPath, newPath);
                        }

                        if (projectFolder != null)
                        {
							if (projectFolder.mIncludeKind == .Auto)
							{
								FinishRenameFolder(newValue, true);
							}
							else
							{
								if (projectFolder.HasAlias())
								{
									FinishRenameFolder(newValue, false);
								}
								else
								{
									Dialog dialog = ThemeFactory.mDefault.CreateDialog("Rename Folder",
	                                    "Choose Alias to change the folder name in just the project.\n\nChoose Move to rename the actual system directory.");

									var oldValue = new String(projectItem.mName);
									var newName = new String(newValue);
									dialog.mDefaultButton = dialog.AddButton("Alias",
                                        new (evt) =>
                                        {
											//FinishRenameFolder(newName, false);
											projectFolder.Rename(newName, false);
											Sort();
                                        }
                                        ~ delete newName);
									dialog.AddButton("Move", new (evt) =>
                                        {
											FinishRenameFolder(newName, true);
											projectFolder.Rename(newName, true);
											Sort();
                                        });
									dialog.mEscButton = dialog.AddButton("Cancel", new (evt) =>
                                        {
											// Item renamed
											listViewItem.Label = oldValue;
											if (projectItem.mIncludeKind != .Auto)
												projectItem.mProject.SetChanged();
											Sort();
                                        }
                                        ~ delete oldValue);
									dialog.PopupWindow(gApp.GetActiveWindow());

									didRename = false;
								}
							}
								
                            //projectFolder.mName.Set(newValue);
                            //IDEApp.sApp.Fail("Cannot rename a folder");                            
                        }

						if ((!failed) && (didRename))
						{
	                        if (var projectFileItem = projectItem as ProjectFileItem)
	                        {
	                            projectFileItem.Rename(newValue);
	                        }
							else if (projectItem != null)
							{
								projectItem.mName.Set(newValue);
							}
						}
                    }
                    
					if (failed)
                    {
                        //IDEApp.sApp.Fail(ex.Message.Replace("\r", ""));
						IDEApp.sApp.Fail("Failed to rename item");
                        IDEApp.Beep(IDEApp.MessageBeepType.Error);
                        return;
                    }                    
                }
                else if (column == 1)
                {

                }

                // Item renamed
                listViewItem.Label = newValue;
				if (projectItem.mIncludeKind != .Auto)
                	projectItem.mProject.SetChanged();

				var parentLvItem = (ProjectListViewItem)listViewItem.mParentItem;
				QueueSortItem(parentLvItem);
				Sort();
            }

			SetFocus();
        }

        void EditListViewItem(ListViewItem listViewItem)
        {
			if (gApp.IsCompiling)
				return;

			mListView.EditListViewItem(listViewItem);

			if (mListView.mEditWidget != null)
			{
				String str = scope .();
				mListView.mEditWidget.GetText(str);
				int lastDot = str.LastIndexOf('.');
				if (lastDot != -1)
				{
					mListView.mEditWidget.mEditWidgetContent.mSelection = .(0, lastDot);
					mListView.mEditWidget.mEditWidgetContent.CursorTextPos = lastDot;
				}
			}
        }

        void RenameItem(ProjectItem projectItem)
        {            
            DarkListViewItem editItem = (DarkListViewItem)mProjectToListViewMap[projectItem];
            EditListViewItem(editItem);
        }

        public void TryRenameItem()
        {
            ListViewItem selectedListViewItem = mListView.GetRoot().FindFocusedItem();           
            if (selectedListViewItem != null)
				EditListViewItem(selectedListViewItem);
        }

		bool IsWorkspaceFolderNameUnique(String name, ListViewItem currentItem = null)
		{
			for (let (k,v) in mListViewToWorkspaceFolderMap)
			{
			    if (v.mListView != currentItem && String.Compare(v.mName, name, true) == 0)
			    {
			        return false;
			    }
			}

			return true;
		}

		public WorkspaceFolder AddWorkspaceFolder(ProjectListViewItem parentListViewItem)
		{
		    ProjectListViewItem listViewItem;
		    listViewItem = (ProjectListViewItem)parentListViewItem.CreateChildItem();
		    parentListViewItem.mOpenButton?.Open(true, false);

			String name = new .(16);
			int32 checkIdx = 0;
			repeat
			{
				name..Clear().AppendF($"New folder");
				if (checkIdx > 0)
					name.AppendF("{}", checkIdx + 1);
				checkIdx++;
			}
			while(!IsWorkspaceFolderNameUnique(name));

		    let folder = new WorkspaceFolder();
		    folder.mName = name;
		    folder.mListView = listViewItem;
		    gApp.mWorkspace.mWorkspaceFolders.Add(folder);
		    
		    listViewItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ProjectFolder);
		    listViewItem.Label = folder.mName;
		    listViewItem.mRefObject = folder;
		    SetupItem(listViewItem, true);
		    mListViewToWorkspaceFolderMap.Add(listViewItem, folder);
			SortItem(parentListViewItem);
		    mListView.UpdateAll();
		    mListView.GetRoot().SelectItemExclusively(listViewItem);
		    EditListViewItem(listViewItem);
		    gApp.mWorkspace.SetChanged();
			return folder;
		}


		public Project ImportProject(String filePath, WorkspaceFolder workspaceFolder, VerSpec verSpec = .None)
		{
			if (gApp.IsCompiling)
				return null;

			if (!File.Exists(filePath))
			{
				gApp.Fail(StackStringFormat!("Project file not found: {0}", filePath));
				return null;
			}

			if (!gApp.mWorkspace.IsInitialized)
			{
				String projPath = scope .();
				Path.GetDirectoryPath(filePath, projPath);
				projPath.Concat(Path.DirectorySeparatorChar, "BeefSpace.toml");
				gApp.OpenWorkspace(projPath);

				for (let project in gApp.mWorkspace.mProjects)
				{
					if (Path.Equals(project.mProjectPath, filePath))
						return project;
				}

				return null;
			}

			bool failed = false;
			String projName = scope String();
			Path.GetFileNameWithoutExtension(filePath, projName);
			if (gApp.mWorkspace.FindProject(projName) != null)
			{
			    gApp.Fail(StackStringFormat!("A project named '{0}' name already exists in the workspace.", projName));
			    return null;
			}

			gApp.CheckLoadConfig();

			//TODO: Protect against importing an existing project
			Project proj = new Project();
			String projFilePath = scope String(filePath);
			IDEUtils.FixFilePath(projFilePath);
			proj.mProjectPath.Set(projFilePath);
			proj.Load(projFilePath);
			gApp.AddNewProjectToWorkspace(proj, verSpec);
			gApp.mWorkspace.FixOptions();
			gApp.[Friend]FlushDeferredLoadProjects(true);
			InitProject(proj, workspaceFolder);
			if (failed)
			{
			    gApp.Fail(StackStringFormat!("Failed to load project: {0}", filePath));
				return proj;
			}

			if ((proj.mGeneralOptions.mTargetType == .BeefLib) && (!gApp.mSettings.mTutorialsFinished.mDependencies))
			{
				Dialog dialog = ThemeFactory.mDefault.CreateDialog("Add Dependencies",
					"""
					A library has been added to the workspace but it won't be accessible until your project is dependent on it.

					Right click on 'Workspace', select 'Properties...', and set dependencies under the 'Projects' tab.
					""", DarkTheme.sDarkTheme.mIconWarning);
				dialog.mDefaultButton = dialog.AddButton("OK");
				dialog.mEscButton = dialog.mDefaultButton;
				dialog.mWindowFlags &= ~.Modal;
				
				dialog.PopupWindow(gApp.mMainWindow);
				gApp.mSettings.mTutorialsFinished.mDependencies = true;
			}

			return proj;
		}

        void ImportProject()
        {
#if !CLI
			gApp.mBeefConfig.Refresh();

            var fileDialog = scope OpenFileDialog();
			fileDialog.ShowReadOnly = false;
            fileDialog.Title = "Import Project";
            fileDialog.Multiselect = false;

			var initialDir = scope String();
			if (gApp.mWorkspace.mDir != null)
				initialDir.Append(gApp.mWorkspace.mDir);
			else
			{
				if (gApp.mInstallDir.Length > 0)
					Path.GetDirectoryPath(.(gApp.mInstallDir, 0, gApp.mInstallDir.Length - 1), initialDir);
				initialDir.Concat(Path.DirectorySeparatorChar, "Samples");
			}

			var workspaceFolder = GetSelectedWorkspaceFolder();

            fileDialog.InitialDirectory = initialDir;
            fileDialog.ValidateNames = true;
            fileDialog.DefaultExt = ".toml";
            fileDialog.SetFilter("Beef projects (BeefProj.toml)|BeefProj.toml|All files (*.*)|*.*");
            gApp.GetActiveWindow().PreModalChild();
            if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
            {
                for (String origProjFilePath in fileDialog.FileNames)
                {
					ImportProject(origProjFilePath, workspaceFolder);
                }
            }
#endif
        }

		void ImportInstalledProject()
		{
#if !CLI
			InstalledProjectDialog dialog = new .();
			dialog.PopupWindow(gApp.mMainWindow);
#endif
		}

        public void ShowProjectProperties(Project project)
        {
            var projectProperties = new ProjectProperties(project);
            projectProperties.PopupWindow(gApp.GetActiveWindow());
        }

        public void ShowWorkspaceProperties()
        {
            var workspaceProperties = new WorkspaceProperties();
            workspaceProperties.PopupWindow(gApp.GetActiveWindow());
        }

        public void RehupProjects()
        {
            for (var checkProject in IDEApp.sApp.mWorkspace.mProjects)
            {
                var listViewItem = (DarkListViewItem)mProjectToListViewMap[checkProject.mRootFolder];
                listViewItem.mIsBold = checkProject == IDEApp.sApp.mWorkspace.mStartupProject;

                var projectOptions = IDEApp.sApp.GetCurProjectOptions(checkProject);
                listViewItem.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, (projectOptions != null) ? gApp.mSettings.mUISettings.mColors.mText : gApp.mSettings.mUISettings.mColors.mWorkspaceDisabledText);
				if (checkProject.mFailed)
					listViewItem.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, gApp.mSettings.mUISettings.mColors.mWorkspaceFailedText);
            }
        }

        void SetAsStartupProject(Project project)
        {
            IDEApp.sApp.mWorkspace.mStartupProject = project;
            IDEApp.sApp.mWorkspace.SetChanged();
            RehupProjects();
        }

        void AddNewProject()
        {
            var newProjectDialog = new NewProjectDialog();
            newProjectDialog.Init();
            newProjectDialog.PopupWindow(gApp.GetActiveWindow());
        }

        protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
        {
     		ProjectItem projectItem = null;
            mSelectedParentItem = (DarkListViewItem)GetSelectedParentItem();
			var focusedItem = mListView.GetRoot().FindFocusedItem();
			if (focusedItem != null)
				mListViewToProjectMap.TryGetValue(focusedItem, out projectItem);
            
            Menu menu = new Menu();
            bool handled = false;

			void AddOpenContainingFolder()
			{
				var item = menu.AddItem("Open Containing Folder");
				item.mOnMenuItemSelected.Add(new (item) =>
				    {
						let projectItem = GetSelectedProjectItem();
						String path = scope String();
						if (projectItem == null)
						{
							path.Set(gApp.mWorkspace.mDir);
						}
						else if (let projectFolder = projectItem as ProjectFolder)
						{
							if (projectFolder.mParentFolder == null)
							{
								path.Set(projectFolder.mProject.mProjectDir);
							}
							else
								projectFolder.GetFullImportPath(path);
						}
						else
							projectItem.mParentFolder.GetFullImportPath(path);

						if (!path.IsWhiteSpace)
						{
							ProcessStartInfo psi = scope ProcessStartInfo();
							psi.SetFileName(path);
							psi.UseShellExecute = true;
							psi.SetVerb("Open");

							var process = scope SpawnedProcess();
							process.Start(psi).IgnoreError();
						}
				    });
			}

            if (projectItem == null)
            {
				Menu anItem;

				void AddWorkspaceMenuItems()
				{
					anItem = menu.AddItem("Add New Project...");
					anItem.mOnMenuItemSelected.Add(new (item) => {
						AddNewProject();
					});
					if (gApp.IsCompiling)
						anItem.SetDisabled(true);

					anItem = menu.AddItem("Add Existing Project...");
					anItem.mOnMenuItemSelected.Add(new (item) => {
						mImportProjectDeferred = true;
					});
					if (gApp.IsCompiling)
						anItem.SetDisabled(true);

					anItem = menu.AddItem("Add From Installed...");
					anItem.mOnMenuItemSelected.Add(new (item) => {
						mImportInstalledDeferred = true;
					});
					if (gApp.IsCompiling)
						anItem.SetDisabled(true);
					anItem = menu.AddItem("New Folder");
					anItem.mOnMenuItemSelected.Add(new (item) => {
						var workspaceFolder = GetSelectedWorkspaceFolder();
						if (workspaceFolder != null)
						{
							let newFolder = AddWorkspaceFolder(workspaceFolder.mListView);
							newFolder.mParent = workspaceFolder;
						}
						else
						{
							AddWorkspaceFolder((ProjectListViewItem)mListView.GetRoot());
						}
					});
				}

				if (mListViewToWorkspaceFolderMap.TryGetValue(focusedItem, let folder))
				{
					anItem = menu.AddItem("Remove");
					anItem.mOnMenuItemSelected.Add(new (item) => { RemoveSelectedWorkspaceFolder(); });
					anItem = menu.AddItem("Rename");
					anItem.mOnMenuItemSelected.Add(new (item) => { EditListViewItem(focusedItem); });
					menu.AddItem();

					AddWorkspaceMenuItems();

					handled = true;
				}
				else if (gApp.mWorkspace.IsInitialized)
				{
					AddOpenContainingFolder();
					menu.AddItem();

	                AddWorkspaceMenuItems();
					menu.AddItem();
	                anItem = menu.AddItem("Properties...");
	                anItem.mOnMenuItemSelected.Add(new (item) => { ShowWorkspaceProperties(); });

	                handled = true;
				}
            }

			bool isProject = false;
			bool isFailedLoad = false;
			if ((projectItem != null) && (!handled))
			{
			    if (projectItem is ProjectFolder)
			    {
			        if (projectItem.mParentFolder == null)
			        {
						isProject = true;

						var project = projectItem.mProject;
						if (project.mFailed)
						{
							var item = menu.AddItem("Retry Load");
							item.mOnMenuItemSelected.Add(new (item) =>
							    {
									var projectItem = GetSelectedProjectItem();
									if (projectItem != null)
							        	gApp.RetryProjectLoad(projectItem.mProject);
							    });
							menu.AddItem();
							//handled = true;
							isFailedLoad = true;
						}
						else if (project.mDeferState != .None)
							isFailedLoad = true;
					}
			    }
			}

            if ((projectItem != null) && (!handled))
            {
				Menu item = null;

				if (isProject)
				{
					if (!isFailedLoad)
					{
						item = menu.AddItem("Set as Startup Project");
						item.mOnMenuItemSelected.Add(new (item) =>
						    {
								var projectItem = GetSelectedProjectItem();
								if (projectItem != null)
									SetAsStartupProject(projectItem.mProject);
						    });

						item = menu.AddItem("Lock Project");
						if (projectItem.mProject.mLocked)
							item.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
						item.mOnMenuItemSelected.Add(new (item) =>
						    {
								var projectItem = GetSelectedProjectItem();
								if (projectItem != null)
								{
						        	projectItem.mProject.mLocked = !projectItem.mProject.mLocked;
									gApp.mWorkspace.SetChanged();
								}
						    });
					}

					item = menu.AddItem("Remove...|Del");
					if (gApp.IsCompiling)
						item.SetDisabled(true);
					item.mOnMenuItemSelected.Add(new (item) =>
						{
							RemoveSelectedItems();
						});

					item = gApp.AddMenuItem(menu, "Rename", "Rename Item");
					if (gApp.IsCompiling)
						item.SetDisabled(true);
					item.mOnMenuItemSelected.Add(new (item) =>
						{
							var projectItem = GetSelectedProjectItem();
							if (projectItem != null)
								RenameItem(projectItem);
						});

					if (!isFailedLoad)
					{
						item = menu.AddItem("Refresh");
						item.mOnMenuItemSelected.Add(new (item) =>
						    {
								var projectItem = GetSelectedProjectItem();
								if (projectItem != null)
								{
									let project = projectItem.mProject;
									if (project.mNeedsCreate)
									{
										project.FinishCreate(false);
										RebuildUI();
									}
									else
									{
										if (project.mRootFolder.mIsWatching)
											project.mRootFolder.StopWatching();
										project.mRootFolder.StartWatching();
										RehupFolder(project.mRootFolder, .FullTraversal);
									}
								}
						    });
					}

					if (!isFailedLoad)
					{
						AddOpenContainingFolder();
						menu.AddItem();
					}
				}

                if ((projectItem != null) && (!isProject))
                {
                    item = menu.AddItem("Remove ...|Del");
                    item.mOnMenuItemSelected.Add(new (item) =>
                        {
							RemoveSelectedItems();
                        });

					item = gApp.AddMenuItem(menu, "Rename", "Rename Item");
					item.mOnMenuItemSelected.Add(new (item) =>
					    {
							var projectItem = GetSelectedProjectItem();
							if (projectItem != null)
					        	RenameItem(projectItem);
					    });

					if (projectItem.mIncludeKind == .Auto)
					{
						item = menu.AddItem("Ignore");
						item.mOnMenuItemSelected.Add(new (item) =>
                            {
								mListView.GetRoot().WithSelectedItems(scope (selectedItem) =>
									{
										ProjectItem projectItem;
	                                    mListViewToProjectMap.TryGetValue(selectedItem, out projectItem);
										DoDeleteItem(selectedItem, null, .Ignore);
										if (projectItem != null)
										{
										    projectItem.mIncludeKind = .Ignore;
											projectItem.mProject.SetChanged();
										}
									});
                            });
					}
					else if (projectItem.mIncludeKind == .Manual)
					{
						/*item = menu.AddItem("Remove");
						item.mOnMenuItemSelected.Add(new (item) =>
							{
								var selectedItem = GetSelectedItem();
								if (selectedItem != null)
							    	DoDeleteItem(selectedItem, null);
							});*/
					}
					else if (projectItem.mIncludeKind == .Ignore)
					{
						item = menu.AddItem("Unignore");
                        item.mOnMenuItemSelected.Add(new (item) =>
                            {
								mListView.GetRoot().WithSelectedItems(scope (selectedItem) =>
									{
										ProjectItem projectItem;
										mListViewToProjectMap.TryGetValue(selectedItem, out projectItem);
										if (projectItem.mIncludeKind == .Ignore)
		                                {
											if (projectItem.mParentFolder.IsIgnored())
												projectItem.mIncludeKind = .Auto;
											else
												Unignore(projectItem);
										}
									});
                            });
					}

					if (projectItem is ProjectSource)
					{
						item = menu.AddItem("Regenerate");
						item.mOnMenuItemSelected.Add(new (item) =>
						    {
								Regenerate(false);
						    });
					}
					else if (let projectFolder = projectItem as ProjectFolder)
					{
						//if (projectFolder.mIncludeKind == .Manual)
						{
							item = menu.AddItem("Auto Include");

							if (projectFolder.mAutoInclude)
								item.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);

							item.mOnMenuItemSelected.Add(new (item) =>
							    {
									mListView.GetRoot().WithSelectedItems(scope (selectedItem) =>
									{
										ProjectItem projectItem;
										mListViewToProjectMap.TryGetValue(selectedItem, out projectItem);
										if (let projectFolder = projectItem as ProjectFolder)
								        {
											projectFolder.mProject.SetChanged();
											projectFolder.mAutoInclude = !projectFolder.mAutoInclude;
											
											if (projectFolder.mAutoInclude)
											{
												if (HasNonAutoItems(projectFolder))
												{
													var dialog = ThemeFactory.mDefault.CreateDialog("Apply to children?",
														"Do you want to apply auto-include to child items?  Selecting 'no' means that current child items will remain manually specified and only new items will be auto-included.");
													dialog.AddButton("Yes", new (evt) =>
														{
															let projectItem = GetSelectedProjectItem();
															if (let projectFolder = projectItem as ProjectFolder)
															{
																RehupFolder(projectFolder, .ApplyAutoToChildren);
															}
														});
													dialog.AddButton("No", new (evt) =>
														{
															let projectItem = GetSelectedProjectItem();
															if (let projectFolder = projectItem as ProjectFolder)
															{
																RehupFolder(projectFolder);
															}
														});
													dialog.PopupWindow(gApp.GetActiveWindow());
												}
												else
													RehupFolder(projectFolder);
											}
											else
											//if ((!projectFolder.mAutoInclude) && (!projectFolder.IsAutoInclude()))
											{
												// Remove any previously auto-added items
												for (int childIdx = projectFolder.mChildItems.Count - 1; childIdx >= 0; childIdx--)
												{
													let childItem = projectFolder.mChildItems[childIdx];
													if (childItem.mIncludeKind == .Auto)
													{
														let listViewItem = (ProjectListViewItem)mProjectToListViewMap[childItem];
														DoDeleteItem(listViewItem, null, .ForceRemove);
													}
												}
											}
										}
									});
							    });
						}
					}

					item = menu.AddItem("Copy Path");
					item.mOnMenuItemSelected.Add(new (item) =>
					    {
							let projectItem = GetSelectedProjectItem();
							String path = scope String();
							if (var projectFileItem = projectItem as ProjectFileItem)
							{
                                projectFileItem.GetFullImportPath(path);
								gApp.SetClipboardText(path);
							}
							
					    });

					AddOpenContainingFolder();

					menu.AddItem();
                }

				if (!isFailedLoad)
				{
					item = menu.AddItem("Copy|Ctrl+C");
					item.mOnMenuItemSelected.Add(new (item) => CopyToClipboard());
					item = menu.AddItem("Cut|Ctrl+X");
					item.mOnMenuItemSelected.Add(new (item) => CutToClipboard());
					item = menu.AddItem("Paste|Ctrl+V");
					item.mOnMenuItemSelected.Add(new (item) => PasteFromClipboard());
					menu.AddItem();

					item = menu.AddItem("New Folder");
					item.mOnMenuItemSelected.Add(new (item) =>
					    {
							var projectFolder = GetSelectedProjectFolder();
							if (projectFolder != null)
					        {
								if (CheckProjectModify(projectFolder.mProject))
									NewFolder(projectFolder);
							}
					    });
	
					item = menu.AddItem("Generate File...");
					item.mOnMenuItemSelected.Add(new (item) =>
					    {
							var projectFolder = GetSelectedProjectFolder();
							if (projectFolder != null)
					        {
								if (CheckProjectModify(projectFolder.mProject))
									GenerateCode(projectFolder);
							}
					    });

					item = menu.AddItem("Import File...");
					item.mOnMenuItemSelected.Add(new (item) => { mImportFileDeferred = true; /* ImportFile();*/ });
	
					item = menu.AddItem("Import Folder...");
					item.mOnMenuItemSelected.Add(new (item) => { mImportFolderDeferred = true; /* ImportFile();*/ });

					if (isProject)
					{
						menu.AddItem();
						item = menu.AddItem("Properties...");
						item.mOnMenuItemSelected.Add(new (item) =>
						    {
								var projectItem = GetSelectedProjectItem();
								if (projectItem != null)
						        	ShowProjectProperties(projectItem.mProject);
						    });
					}
				}
            }
            /*else if (!handled)
            {
                Menu anItem = menu.AddItem("Import Project");
                anItem.mMenuItemSelectedHandler .Add(new (item) => { mImportProjectDeferred = true; };
            }*/

			if (menu.mItems.IsEmpty)
			{
				delete menu;
				return;
			}

            MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);

            menuWidget.Init(mListView.GetRoot(), x, y);
			mMenuWidget = menuWidget;
			mMenuWidget.mOnRemovedFromParent.Add(new (widget, prevParent, widgetWindow) =>
				{
					mMenuWidget = null;
				});

        }

		void ListViewItemClicked(MouseEvent theEvent)
		{
			ListViewItem anItem = (ListViewItem)theEvent.mSender;
			if (anItem.mColumnIdx != 0)
			    anItem = anItem.GetSubItem(0);

			if (theEvent.mBtn == 1)
			{
				Widget widget = (DarkListViewItem)theEvent.mSender;
				float clickX = theEvent.mX;
				float clickY = widget.mHeight + GS!(2);
			    float aX, aY;
			    widget.SelfToOtherTranslate(mListView.GetRoot(), clickX, clickY, out aX, out aY);
			    ShowRightClickMenu(mListView, aX, aY);
			}
			else
			{                
			    //if (anItem.IsParent)
			        //anItem.Selected = false;
			}
		}

		void ListViewClicked(MouseEvent theEvent)
		{
			if (theEvent.mBtn == 1)
			{
			    float aX, aY;
			    theEvent.GetRootCoords(out aX, out aY);
			    ShowRightClickMenu(mListView, aX, aY);
			}
		}

        void ListViewMouseDown(MouseEvent theEvent)
        {
            // We clicked off all items, so deselect
			mListView.GetRoot().WithSelectedItems(scope (item) => { item.Selected = false; } );
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            /*using (g.PushColor(0x80FF0000))
                g.FillRect(0, 0, mWidth, mHeight);*/            
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);
        }

        public override void Update()
        {
            base.Update();

			if (mSortDirty)
			{
				mSortDirty = false;
				Sort();
			}

			if (mWantsRehup)
			{
				mWantsRehup = false;
				mListView.GetRoot().WithItems(scope (item) =>
					{
						let lvItem = (ProjectListViewItem)item;
						if (lvItem.mWantsRehup)
						{
							lvItem.mWantsRehup = false;
							if ((mListViewToProjectMap.TryGetValue(lvItem, var projectItem)) &&
								(let projectFolder = projectItem as ProjectFolder))
							{
								RehupFolder(projectFolder);
							}
						}
					});
			}

            if (mRefreshVisibleViewsDeferred)
            {
                mRefreshVisibleViewsDeferred = false;
                IDEApp.sApp.RefreshVisibleViews();                
            }

            if (mImportFileDeferred)
            {
                mImportFileDeferred = false;
                ImportFile((ProjectFolder)mListViewToProjectMap[mSelectedParentItem]);
            }

            if (mImportFolderDeferred)
            {
                mImportFolderDeferred = false;
                ImportFolder((ProjectFolder)mListViewToProjectMap[mSelectedParentItem]);
            }

            if (mImportProjectDeferred)
            {
                mImportProjectDeferred = false;
                ImportProject();
            }

			if (mImportInstalledDeferred)
			{
				mImportInstalledDeferred = false;
				ImportInstalledProject();
			}

			ValidateCutClipboard();
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mListView.Resize(0, 0, mWidth, mHeight);
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
        }
		
		
    }
}

///////////////////////