using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy;
using IDE;
using Beefy.theme.dark;
using Beefy.theme;
using Beefy.events;
using System.Diagnostics;

//#define A
//#define B

namespace IDE.ui
{
    class WorkspaceProperties : BuildPropertiesDialog
    {
#if B
		class CreateWorkspacePlatformDialog : DarkDialog
		{
			public this(String title = null, String text = null, Image icon = null) : base(title, text, icon)
			{

			}
		}
#elif A
		class CreateWorkspacePlatformDialog : IDEDialog
		{
			public this(String title = null, String text = null, Image icon = null) : base(title, text, icon)
			{

			}
		}
#endif

        enum CategoryType
        {
			General,
			Beef_Global,

			Targeted,
			Projects,
			Beef_Targeted,
            Build,
            C,

            COUNT
        }

        ConfigDataGroup mCurConfigDataGroup;
        Workspace.Options[] mCurWorkspaceOptions ~ delete _;

        public this()
        {
            var app = IDEApp.sApp;
            mTitle = new String();
            mTitle.AppendF("Workspace '{0}' Properties", app.mWorkspace.mName);

            var root = (DarkListViewItem)mCategorySelector.GetRoot();
			var globalItem = AddCategoryItem(root, "General");
			var item = AddCategoryItem(globalItem, "Beef");
			item.Focused = true;
			globalItem.Open(true, true);

			var targetedItem = AddCategoryItem(root, "Targeted");
			AddCategoryItem(targetedItem, "Projects");
			AddCategoryItem(targetedItem, "Beef");
            item = AddCategoryItem(targetedItem, "Build");
			targetedItem.Open(true, true);
            
            //AddCategoryItem(root, "C/C++");
        }

		public ~this()
		{
			
		}

		protected override bool HasChanges()
		{
			if (mPropPage.mCategoryType == (int)CategoryType.Beef_Global)
			{
				for (let option in gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions)
				{
					if (option.mCreateState != .Normal)
						return true;
				}
			}
			else if (mPropPage.mCategoryType == (int)CategoryType.Beef_Targeted)
			{
				for (var target in mCurPropertiesTargets)
				{
					let options = (Workspace.Options)target;
					for (let option in options.mDistinctBuildOptions)
					{
						if (option.mCreateState != .Normal)
							return true;
					}
				}
			}

			return false;
		}

#if A || B
		protected override void CreateNewPlatform()
		{
			CreateWorkspacePlatformDialog dialog = new CreateWorkspacePlatformDialog("New Platform", "Platform Name");
			dialog.AddEdit("");
			dialog.AddOkCancelButtons(
			    new (evt) => { var text = scope String(); dialog.mDialogEditWidget.GetText(text); CreateNewPlatform(text); },
			    null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}
#endif

        protected override TargetedKind GetCategoryTargetedKind(int32 categoryTypeInt)
		{
			switch ((CategoryType)categoryTypeInt)
			{
			case .General,
				 //.Targeted,
				 .Beef_Global:
				return .None;
			default:
				return .Config;
			}
		}

		public override void GetConfigList(List<String> configNames)
		{
			for (var configName in IDEApp.sApp.mWorkspace.mConfigs.Keys)
				configNames.Add(configName);
			if (mConfigNames.IsEmpty)
				configNames.Add("Debug");
		}

		public override void GetPlatformList(List<String> platformNames)
		{
			/*var configName = mConfigNames[0];
			for (var platformName in gApp.mWorkspace.mConfigs[configName].mPlatforms.Keys)
				platformNames.Add(platformName);
			if (platformNames.IsEmpty)
				platformNames.Add(IDEApp.sPlatform64Name);*/
			gApp.mWorkspace.GetPlatformList(platformNames);
		}
		
        public override bool CreateNewConfig(String name, String copiedFromConfig)
        {
            var workspace = IDEApp.sApp.mWorkspace;

            var curWorkspaceOptions = workspace.mConfigs[copiedFromConfig];

            using (workspace.mMonitor.Enter())
            {
                String useName = scope String(name);
                useName.Trim();
                if (useName.Length > 0)
                {
					if (gApp.mWorkspace.mConfigs.ContainsKey(useName))
					{
						gApp.Fail(scope String()..AppendF("Workspace already contains a config named '{0}'", useName));
						return false;
					}

                    Workspace.Config config = new Workspace.Config();
                    gApp.mWorkspace.mConfigs[new String(useName)] = config;

                    for (var platformKV in curWorkspaceOptions.mPlatforms)
                    {
						Workspace.Options copiedOptions = platformKV.value;
                        Workspace.Options options = new Workspace.Options();
						options.CopyFrom(copiedOptions);

                        config.mPlatforms[new String(platformKV.key)] = options;
                    }
                    gApp.mWorkspace.SetChanged();
                    SelectConfig(useName);
                }
            }
			return true;
        }

		public override void EditConfigs()
		{
		    let dialog = new EditTargetDialog(this, .Config);
			for (var config in gApp.mWorkspace.mConfigs.Keys)
				dialog.Add(config);
			dialog.FinishInit();
			dialog.AddOkCancelButtons(new (dlg) =>
				{
					Dictionary<String, Workspace.Config> newConfigs = new .();
					for (let entry in dialog.mEntries)
					{
						let kv = gApp.mWorkspace.mConfigs.GetAndRemove(entry.mOrigName).Get();
						String matchKey = kv.key;
						Workspace.Config config = kv.value;
						if (entry.mDelete)
						{
							gApp.mWorkspace.mHasChanged = true;
							delete matchKey;
							delete config;
						}
						else
						{
							if (entry.mNewName != null)
							{
								int idx = mConfigNames.IndexOf(entry.mOrigName);
								if (idx != -1)
								{
									mConfigNames[idx].Set(entry.mNewName);
									if (mConfigNames.Count == 1)
										mConfigComboBox.Label = entry.mNewName;
								}
							}
							if ((entry.mNewName != null) && (entry.mNewName != entry.mOrigName))
							{
								gApp.mWorkspace.mHasChanged = true;
							}
							delete matchKey;
							newConfigs[new String(entry.mNewName ?? entry.mOrigName)] = config;
						}
					}
					delete gApp.mWorkspace.mConfigs;
					gApp.mWorkspace.mConfigs = newConfigs;

					for (var window in gApp.mWindows)
					{
						if (var widgetWindow = window as WidgetWindow)
							if (var workspaceProperties = widgetWindow.mRootWidget as WorkspaceProperties)
							{
								for (let entry in dialog.mEntries)
								{
									if (entry.mDelete)
										workspaceProperties.ConfigDeleted(entry.mOrigName);
									else if (entry.mNewName != null)
										workspaceProperties.ConfigRenamed(entry.mOrigName, entry.mNewName);
								}
							}	
					}

					gApp.MarkDirty();
				}, null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

		public override void EditPlatforms()
		{
			var platformList = scope List<String>();
			GetPlatformList(platformList);
		    let dialog = new EditTargetDialog(this, .Platform);
			for (var platformName in platformList)
				dialog.Add(platformName);
			dialog.FinishInit();
			dialog.AddOkCancelButtons(new (dlg) =>
				{
					List<WorkspaceProperties> workspacePropertiesList = scope .();
					for (var window in gApp.mWindows)
					{
						if (var widgetWindow = window as WidgetWindow)
							if (var workspaceProperties = widgetWindow.mRootWidget as WorkspaceProperties)
								workspacePropertiesList.Add(workspaceProperties);
					}

					bool hadChanges = false;
					for (let entry in dialog.mEntries)
					{
						if ((!entry.mDelete) && (entry.mNewName == null))
							continue;

						ConfigLoop: for (var configName in mConfigNames)
						{
							Workspace.Config config;
							if (!gApp.mWorkspace.mConfigs.TryGetValue(configName, out config))
								continue;
							String matchKey;
							Workspace.Options options;
							switch (config.mPlatforms.GetAndRemove(entry.mOrigName))
							{
							case .Ok(let kv):
								matchKey = kv.key;
								options = kv.value;
							case .Err:
								continue ConfigLoop;
							}
							
							if (entry.mDelete)
							{
								hadChanges = true;
								delete matchKey;
								delete options;
							}
							else
							{
								if (entry.mNewName != null)
								{
									int idx = mPlatformNames.IndexOf(entry.mOrigName);
									if (idx != -1)
									{
										mPlatformNames[idx].Set(entry.mNewName);
										if (mPlatformNames.Count == 1)
											mPlatformComboBox.Label = entry.mNewName;
									}
								}
								if ((entry.mNewName != null) && (entry.mNewName != entry.mOrigName))
								{
									hadChanges = true;
								}
								delete matchKey;

								String* newKeyPtr;
								Workspace.Options* newOptionsPtr;
								if (config.mPlatforms.TryAdd(entry.mNewName, out newKeyPtr, out newOptionsPtr))
								{
									*newKeyPtr = new String(entry.mNewName);
									*newOptionsPtr = options;
								}
								else
								{
									delete options;
								}
							}
						}
					}
					
					for (var window in gApp.mWindows)
					{
						if (var widgetWindow = window as WidgetWindow)
						{
							if (var workspaceProperties = widgetWindow.mRootWidget as WorkspaceProperties)
							{
								for (let entry in dialog.mEntries)
								{
									if (entry.mDelete)
										workspaceProperties.PlatformDeleted(entry.mOrigName);
									else if (entry.mNewName != null)
										workspaceProperties.PlatformRenamed(entry.mOrigName, entry.mNewName);
								}
							}	
						}
					}

					gApp.MarkDirty();
				}, null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

        protected override void CreateNewPlatform(String name)
        {
            var workspace = gApp.mWorkspace;
            
            using (workspace.mMonitor.Enter())
            {
                String platformName = scope String(name);
                platformName.Trim();
                if (!platformName.IsEmpty)
                {            
                    /*for (var workspaceConfig in workspace.mConfigs)
                    {
						if (!workspaceConfig.value.mPlatforms.ContainsKey(useName))
						{
	                        Workspace.Options workspaceOptions = new Workspace.Options();
							workspace.SetupDefault(workspaceOptions, workspaceConfig.key, useName);
	                        workspaceConfig.value.mPlatforms[new String(useName)] = workspaceOptions;
						}
                    }

					for (var project in workspace.mProjects)
					{
					    for (var projectConfigKV in project.mConfigs)
					    {
							let projectConfig = projectConfigKV.value;
							if (!projectConfig.mPlatforms.ContainsKey(useName))
					        {
								project.CreateConfig(projectConfigKV.key, useName);
							}
					    }                            
					}*/

                    //IDEApp.sApp.mWorkspace.SetChanged();
					gApp.mWorkspace.FixOptionsForPlatform(platformName);
                    SelectPlatform(platformName);

					gApp.mWorkspace.MarkPlatformNamesDirty();
					if (!gApp.mWorkspace.mUserPlatforms.Contains(platformName))
						gApp.mWorkspace.mUserPlatforms.Add(new String(platformName));
                }
            }
        }

        protected override void ShowPropPage(int32 categoryTypeInt)
        {
            base.ShowPropPage(categoryTypeInt);

			var configName = mConfigNames[0];
			var platformName = mPlatformNames[0];

            gApp.mWorkspace.FixOptions(configName, platformName);

            CategoryType categoryType = (CategoryType)categoryTypeInt;

			int propIdx = 0;
			delete mCurPropertiesTargets;
			mCurPropertiesTargets = new Object[mConfigNames.Count * mPlatformNames.Count];
			delete mCurWorkspaceOptions;
			mCurWorkspaceOptions = new Workspace.Options[mConfigNames.Count * mPlatformNames.Count];

			for (var checkConfigName in mConfigNames)
			{
				for (var checkPlatformName in mPlatformNames)
				{
					var workspace = IDEApp.sApp.mWorkspace;
					var workspaceConfig = workspace.mConfigs[checkConfigName];
					mCurWorkspaceOptions[propIdx] = workspaceConfig.mPlatforms[checkPlatformName];
					mCurPropertiesTargets[propIdx] = mCurWorkspaceOptions[propIdx];
					propIdx++;
				}
			}

            ConfigDataGroup targetedConfigData;
            if ((GetCategoryTargetedKind(categoryTypeInt) != .None) &&
				((mConfigNames.Count == 1) && (mPlatformNames.Count == 1)))
            {
                var key = Tuple<String, String>(configName, platformName);
                mTargetedConfigDatas.TryGetValue(key, out targetedConfigData);
                if (targetedConfigData == null)
                {
					key.Item1 = new String(key.Item1);
					key.Item2 = new String(key.Item2);
                    targetedConfigData = new ConfigDataGroup((int32)CategoryType.COUNT);
                    targetedConfigData.mTarget = key;
                    mConfigDatas.Add(targetedConfigData);
                    mTargetedConfigDatas[key] = targetedConfigData;
                }
            }
            else
            {
                if (mMultiTargetConfigData == null)
                {
                    mMultiTargetConfigData = new ConfigDataGroup((int32)CategoryType.COUNT);
                    mConfigDatas.Add(mMultiTargetConfigData);
					//Debug.WriteLine("Creating ConfigDataGroup {0} in {1}", mUntargetedConfigData, this);
                }
                targetedConfigData = mMultiTargetConfigData;

				if (GetCategoryTargetedKind(categoryTypeInt) != .None)
				{
					DeleteAndNullify!(targetedConfigData.mPropPages[categoryTypeInt]);
				}

				if (categoryType == .Beef_Global)
				{
					mCurPropertiesTargets[0] = gApp.mWorkspace.mBeefGlobalOptions;
				}
            }

            if (targetedConfigData.mPropPages[categoryTypeInt] == null)
            {
				if (categoryType == .Projects)
                	CreatePropPage(categoryTypeInt, .None);
				else
					CreatePropPage(categoryTypeInt, .AllowSearch | .AllowReset);
                targetedConfigData.mPropPages[categoryTypeInt] = mPropPage;

                mPropPage.mPropertiesListView.InitScrollbars(false, true);
                mPropPage.mPropertiesListView.mAutoFocus = true;
                mPropPage.mPropertiesListView.mShowColumnGrid = true;
                mPropPage.mPropertiesListView.mShowGridLines = true;

				if (categoryType == CategoryType.Beef_Global)
					PopulateBeefGlobalOptions();
                else if (categoryType == CategoryType.Build)
                    PopulateBuildOptions();
                else if (categoryType == CategoryType.Projects)
                    PopulateProjectsOptions();
                else if (categoryType == CategoryType.Beef_Targeted)
                    PopulateBeefTargetedOptions();
                else if (categoryType == CategoryType.C)
                    PopulateCOptions();
            }
            mCurConfigDataGroup = targetedConfigData;
            mPropPage = targetedConfigData.mPropPages[(int32)categoryType];
            AddPropPageWidget();
            ResizeComponents();
        }

        void PopulateBuildOptions()
        {
            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            var (category, propEntry) = AddPropertiesItem(root, "General");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
			AddPropertiesItem(category, "Toolset", "mToolsetType");
			AddPropertiesItem(category, "Build Type", "mBuildKind");

            /*var category = AddPropertiesItem(root, "General");            
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;*/

            /*AddPropertiesItem(category, "Target Directory", "mGeneralOptions.mTargetDirectory");
            var parent = AddPropertiesItem(category, "Target Name", "mGeneralOptions.mTargetName");
            parent = AddPropertiesItem(category, "Target Type", "mGeneralOptions.mTargetType");
            AddPropertiesItem(category, "Startup Object", "mGeneralOptions.mStartupObject");
            AddPropertiesItem(category, "Linker Type", "mGeneralOptions.mLinkerType");
            AddPropertiesItem(category, "C Library", "mGeneralOptions.mCLibType");
            //parent.MakeParent();*/
            category.Open(true, true);
        }

        protected void PopulateProjectConfigMenu(Menu menu, ListViewItem listViewItem, Project project, Workspace.ConfigSelection newConfigSelection)
        {
            for (var configName in project.mConfigs.Keys)
            {                
                var item = menu.AddItem(configName);
                item.mOnMenuItemSelected.Add(new (evt) => { listViewItem.Label = configName; newConfigSelection.mConfig.Set(configName); });
            }
        }

        protected void PopulateProjectPlatformMenu(Menu menu, ListViewItem listViewItem, Project project, Workspace.ConfigSelection newConfigSelection)
        {
            if (newConfigSelection.mConfig == null)
                return;

            for (var platformName in project.mConfigs[newConfigSelection.mConfig].mPlatforms.Keys)
            {
                var item = menu.AddItem(platformName);
                item.mOnMenuItemSelected.Add(new (evt) => { listViewItem.Label = platformName; newConfigSelection.mPlatform.Set(platformName); });
            }
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			if ((CategoryType)mPropPage.mCategoryType == .Projects)
				mPropPage.mPropertiesListView.mLabelX = GS!(26);
		}

        void PopulateProjectsOptions()
        {
            mPropPage.mPropertiesListView.mColumns[0].Label = "Project";
            mPropPage.mPropertiesListView.mColumns[0].mMinWidth = GS!(100);
            mPropPage.mPropertiesListView.mColumns[0].mWidth = GS!(180);

            mPropPage.mPropertiesListView.mColumns[1].Label = "Configuration";
            mPropPage.mPropertiesListView.mColumns[1].mMinWidth = GS!(100);
            mPropPage.mPropertiesListView.mColumns[1].mWidth = GS!(180);

            mPropPage.mPropertiesListView.AddColumn(180, "Platform");
            mPropPage.mPropertiesListView.mColumns[2].mMinWidth = GS!(100);
            mPropPage.mPropertiesListView.mLabelX = GS!(26);

            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            var category = root;
            
            for (int32 projectIdx = 0; projectIdx < IDEApp.sApp.mWorkspace.mProjects.Count; projectIdx++)
            {
                var project = IDEApp.sApp.mWorkspace.mProjects[projectIdx];

				PropEntry[] propEntries = new PropEntry[mCurWorkspaceOptions.Count];

				bool multipleEnabled = false;
				bool multipleConfig = false;
				bool multiplePlatform = false;
				DarkCheckBox checkbox = null;
				DarkComboBox configComboBox = null;
				DarkComboBox platformComboBox = null;
				ListViewItem configItem = null;
				ListViewItem platformItem = null;

                Workspace.ConfigSelection firstConfigSelection = null;
				for (int optionsIdx < mCurWorkspaceOptions.Count)
				{
					Workspace.ConfigSelection configSelection;
					var curWorkspaceOptions = mCurWorkspaceOptions[optionsIdx];
	                curWorkspaceOptions.mConfigSelections.TryGetValue(project, out configSelection);
	                if (configSelection == null)
	                    continue;

					if (firstConfigSelection == null)
					{
						firstConfigSelection = configSelection;
					}
					else
					{
						if (firstConfigSelection.mEnabled != configSelection.mEnabled)
							multipleEnabled = true;
						if (firstConfigSelection.mConfig != configSelection.mConfig)
							multipleConfig = true;
						if (firstConfigSelection.mPlatform != configSelection.mPlatform)
							multiplePlatform = true;
					}
				
	                var newConfigSelection = configSelection.Duplicate();
	                var origConfigSelection = configSelection.Duplicate();
	
	                PropEntry propEntry = new PropEntry();                
	                propEntry.mOrigValue = Variant.Create(origConfigSelection, true);
	                propEntry.mCurValue = Variant.Create(newConfigSelection, true);
	                propEntry.mApplyAction = new () =>
	                    {
	                        Workspace.ConfigSelection setConfigSelection;
	                        curWorkspaceOptions.mConfigSelections.TryGetValue(project, out setConfigSelection);
	                        if (setConfigSelection == null)
	                        {
	                            IDEApp.sApp.Fail(StackStringFormat!("Project '{0}' not in workspace", project.mProjectName));
	                            return;
	                        }
	                        setConfigSelection.mEnabled = newConfigSelection.mEnabled;
	                        setConfigSelection.mConfig.Set(newConfigSelection.mConfig);
	                        setConfigSelection.mPlatform.Set(newConfigSelection.mPlatform);
	
	                        origConfigSelection.mEnabled = newConfigSelection.mEnabled;
	                        origConfigSelection.mConfig.Set(newConfigSelection.mConfig);
	                        origConfigSelection.mPlatform.Set(newConfigSelection.mPlatform);
	                    };
					propEntries[optionsIdx] = propEntry;

					if (optionsIdx == 0)
					{
						var (listViewItem, ?) = AddPropertiesItem(category, project.mProjectName);
						propEntry.mListViewItem = listViewItem;

						checkbox = new DarkCheckBox();
						checkbox.Checked = configSelection.mEnabled;
						checkbox.Resize(GS!(4), 0, GS!(20), GS!(20));
						listViewItem.AddWidget(checkbox);

					    propEntry.mComboBoxes = new List<DarkComboBox>();

					    configItem = listViewItem.CreateSubItem(1);                
					    configItem.Label = configSelection.mConfig;
					    configComboBox = new DarkComboBox();
					    configComboBox.mFrameless = true;
					    configComboBox.mPopulateMenuAction.Add(new (menu) => { PopulateProjectConfigMenu(menu, configItem, project, newConfigSelection); });
					    configItem.AddWidget(configComboBox);                
					    configItem.mOnResized.Add(new (evt) => { configComboBox.Resize(0, 0, configItem.mWidth, configItem.mHeight + 1); });
					    propEntry.mComboBoxes.Add(configComboBox);

					    platformItem = listViewItem.CreateSubItem(2);                
					    platformItem.Label = configSelection.mPlatform;
					    platformComboBox = new DarkComboBox();
					    platformComboBox.mFrameless = true;
					    platformComboBox.mPopulateMenuAction.Add(new (menu) => { PopulateProjectPlatformMenu(menu, platformItem, project, newConfigSelection); });
					    platformItem.AddWidget(platformComboBox);
					    platformItem.mOnResized.Add(new (evt) => { platformComboBox.Resize(0, 0, GetValueEditWidth(platformItem), platformItem.mHeight + 1); });
					    propEntry.mComboBoxes.Add(platformComboBox);

					    checkbox.mOnMouseUp.Add(new (evt) => { newConfigSelection.mEnabled = checkbox.Checked; });
						mPropPage.mPropEntries[listViewItem] = propEntries;
					}
				}
				if (multipleEnabled)
					checkbox.State = .Indeterminate;
				if (multipleConfig)
					configItem.Label = "<Multiple Values>";
				if (multiplePlatform)
					platformItem.Label = "<Multiple Values>";
            }
        }

		protected override void ResetSettings()
		{
			var targetDict = scope Dictionary<Object, Object>();
			switch ((CategoryType)mPropPage.mCategoryType)
			{
			case .Beef_Global:
				DeleteDistinctBuildOptions();
				DistinctBuildOptions defaultTypeOptions = scope:: .();
				for (var typeOption in gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions)
					targetDict[typeOption] = defaultTypeOptions;
				var generalOptions = scope Workspace.BeefGlobalOptions;
				targetDict[mCurPropertiesTargets[0]] = generalOptions;
				UpdateFromTarget(targetDict);
			case .Beef_Targeted:
				DeleteDistinctBuildOptions();
				fallthrough;
			case .Build:
				DeleteDistinctBuildOptions();
				int propsIdx = 0;
				for (var configName in mConfigNames)
				{
					for (var platformName in mPlatformNames)
					{
						var curWorkspaceOptions = mCurWorkspaceOptions[propsIdx];
						Workspace.Options options = scope:: .();
						gApp.mWorkspace.SetupDefault(options, configName, platformName);
						targetDict[curWorkspaceOptions] = options;
						propsIdx++;
					}
				}
				UpdateFromTarget(targetDict);
			default:
			}
		}

		protected override Object[] PhysAddNewDistinctBuildOptions()
		{
			if (mPropPage.mCategoryType == (int)CategoryType.Beef_Global)
			{
				let typeOptions = new DistinctBuildOptions();
				typeOptions.mCreateState = .New;
				gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions.Add(typeOptions);
				Object[] typeOptionsTargets = new .(typeOptions);
				return typeOptionsTargets;
			}
			else
			{
				Object[] typeOptionsTargets = new Object[mCurWorkspaceOptions.Count];
				for (int idx < mCurWorkspaceOptions.Count)
				{
					var curWorkspaceOptions = mCurWorkspaceOptions[idx];
					let typeOptions = new DistinctBuildOptions();
					typeOptions.mCreateState = .New;
					curWorkspaceOptions.mDistinctBuildOptions.Add(typeOptions);
					typeOptionsTargets[idx] = typeOptions;
				}
				return typeOptionsTargets;
			}
		}

		void PopulateBeefGlobalOptions()
		{
		    var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
		    /*var (category, ?) = AddPropertiesItem(root, "General");
		    category.mIsBold = true;
		    category.mTextColor = 0xFFE8E8E8;*/
			
			AddPropertiesItem(root, "Preprocessor Macros", "mPreprocessorMacros");
			DistinctOptionBuilder dictinctOptionBuilder = scope .(this);
			dictinctOptionBuilder.Add(gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions);
			dictinctOptionBuilder.Finish();

			//AddPropertiesItem(root, "Target Triple", "mTargetTriple");

			AddNewDistinctBuildOptions();
		    //parent.MakeParent();
		    //category.Open(true, true);
		}

        void PopulateBeefTargetedOptions()
        {
            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();            
            var (category, propEntry) = AddPropertiesItem(root, "General");            
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
			AddPropertiesItem(category, "Preprocessor Macros", "mPreprocessorMacros");
			AddPropertiesItem(category, "Incremental Build", "mIncrementalBuild");
            AddPropertiesItem(category, "Intermediate Type", "mIntermediateType");
			var (allocCategory, allocPropEntry) = AddPropertiesItem(category, "Memory Allocator", "mAllocType");
			var (mallocItem, mallocPropEntry) = AddPropertiesItem(allocCategory, "Malloc", "mAllocMalloc");
			var (freeItem, freePropEntry) = AddPropertiesItem(allocCategory, "Free", "mAllocFree");
			allocPropEntry.mOnUpdate.Add(new () =>
				{
					let mallocSubItem = (DarkListViewItem)mallocItem.GetSubItem(1);
					let freeSubItem = (DarkListViewItem)freeItem.GetSubItem(1);

					let allocType = allocPropEntry.mCurValue.Get<Workspace.AllocType>();
					if (allocType == .Custom)
					{
						mallocSubItem.Label = mallocPropEntry.mCurValue.Get<String>();
						mallocSubItem.mTextColor = 0xFFFFFFFF;
						mallocPropEntry.mDisabled = false;
						freeSubItem.Label = freePropEntry.mCurValue.Get<String>();
						freeSubItem.mTextColor = 0xFFFFFFFF;
						freePropEntry.mDisabled = false;
					}
					else
					{
						if (allocType == .Debug)
						{
							mallocSubItem.Label = "";
							freeSubItem.Label = "";
						}
						else if (allocType == .CRT)
						{
							mallocSubItem.Label = "malloc";
							freeSubItem.Label = "free";
						}
						else if (allocType == .JEMalloc)
						{
							mallocSubItem.Label = "je_malloc";
							freeSubItem.Label = "je_free";
						}
						else if (allocType == .TCMalloc)
						{
							mallocSubItem.Label = "tcmalloc";
							freeSubItem.Label = "tcfree";
						}

						mallocSubItem.mTextColor = 0xFFC0C0C0;
						mallocPropEntry.mDisabled = true;
						freeSubItem.mTextColor = 0xFFC0C0C0;
						freePropEntry.mDisabled = true;
					}
					return false;
				});
			allocPropEntry.mOnUpdate();
            AddPropertiesItem(category, "SIMD Instructions", "mBfSIMDSetting");
            AddPropertiesItem(category, "Optimization Level", "mBfOptimizationLevel",
                scope String[] { "O0", "O1", "O2", "O3", "Og", "Og+"});
			AddPropertiesItem(category, "LTO Type", "mLTOType");
            AddPropertiesItem(category, "No Omit Frame Pointers", "mNoOmitFramePointers");
			AddPropertiesItem(category, "Large Strings", "mLargeStrings");
			AddPropertiesItem(category, "Large Collections", "mLargeCollections");
            category.Open(true, true);

            (category, propEntry) = AddPropertiesItem(root, "Debug");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
            AddPropertiesItem(category, "Debug Info", "mEmitDebugInfo");
            AddPropertiesItem(category, "Runtime Checks", "mRuntimeChecks",
                scope String[] { "No", "Yes" });
            AddPropertiesItem(category, "Dynamic Cast Check", "mEmitDynamicCastCheck",
                scope String[] { "No", "Yes" });
            AddPropertiesItem(category, "Object Debug Flags", "mEnableObjectDebugFlags",
                scope String[] { "No", "Yes" });
            AddPropertiesItem(category, "Object Access Check", "mEmitObjectAccessCheck",
                scope String[] { "No", "Yes" });
            AddPropertiesItem(category, "Realtime Leak Check", "mEnableRealtimeLeakCheck",
                scope String[] { "No", "Yes" });
			AddPropertiesItem(category, "Enable Hot Compilation", "mAllowHotSwapping",
				scope String[] { "No", "Yes" });
			AddPropertiesItem(category, "Alloc Stack Trace Depth", "mAllocStackTraceDepth");
            category.Open(true, true);

			DistinctOptionBuilder dictinctOptionBuilder = scope .(this);
			for (int propIdx < mCurWorkspaceOptions.Count)
			{
				var curWorkspaceOptions = mCurWorkspaceOptions[propIdx];
				dictinctOptionBuilder.Add(curWorkspaceOptions.mDistinctBuildOptions);
			}
			dictinctOptionBuilder.Finish();
			AddNewDistinctBuildOptions();
        }

        void PopulateCOptions()
        {
            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            var (category, propEntry) = AddPropertiesItem(root, "General");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
            AddPropertiesItem(category, "SIMD Instructions", "mCSIMDSetting");
            AddPropertiesItem(category, "Optimization Level", "mCOptimizationLevel");
            category.Open(true, true);
        }

        protected override bool ApplyChanges()
        {
            bool hadChange = false;

            for (var targetedConfigData in mConfigDatas)
            {
				bool configDataHadChange = false;
                for (var propPage in targetedConfigData.mPropPages)
                {
                    if (propPage == null)
                        continue;

                    for (var propEntries in propPage.mPropEntries.Values)
                    {
						for (var propEntry in propEntries)
						{
                            if (propEntry.HasChanged())
                            {
                                configDataHadChange = true;
								propEntry.ApplyValue();
                            }
						}
						if (propPage == mPropPage)
							UpdatePropertyValue(propEntries);
                    }

                    propPage.mHasChanges = false;

					if (configDataHadChange)
					{
						// Try to find any other project properties dialogs that are open
						for (var window in gApp.mWindows)
						{
							if (var widgetWindow = window as WidgetWindow)
							{
								if (var workspaceProperties = widgetWindow.mRootWidget as WorkspaceProperties)
								{
									if (workspaceProperties == this)
										continue;
									
									if (GetCategoryTargetedKind(propPage.mCategoryType) != .None)
									{
										if (mPropPage == propPage)
										{
											for (var configName in mConfigNames)
												for (var platformName in mPlatformNames)
													workspaceProperties.HadExternalChanges(configName, platformName);
										}
										else
											workspaceProperties.HadExternalChanges(targetedConfigData.mTarget.Item1, targetedConfigData.mTarget.Item2);
									}
									else
										workspaceProperties.HadExternalChanges(null, null);
								}
							}
						}
	
						hadChange = true;
					}
				}
            }

            if (hadChange)
            {
                gApp.CurrentWorkspaceConfigChanged();
                gApp.mWorkspace.SetChanged();
            }

			SetWorkspaceData(true);

			if (IsMultiTargeted())
			{
				ClearTargetedData();
			}

            return true;
        }

		void SetWorkspaceData(bool apply)
		{
			if (ApplyDistinctBuildOptions(gApp.mWorkspace.mBeefGlobalOptions.mDistinctBuildOptions, apply))
				gApp.mWorkspace.SetChanged();
			for (let config in gApp.mWorkspace.mConfigs.Values)
			{
				for (let platform in config.mPlatforms.Values)
				{
					if (ApplyDistinctBuildOptions(platform.mDistinctBuildOptions, apply))
						gApp.mWorkspace.SetChanged();
				}
			}
		}

		public override void Close()
		{
			base.Close();
			SetWorkspaceData(false);
		}

        public override void CalcSize()
        {
            mWidth = GS!(660);
            mHeight = GS!(512);
        }
    }
}
