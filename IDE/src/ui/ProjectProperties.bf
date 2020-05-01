 using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Reflection;
using Beefy;
using Beefy.widgets;
using Beefy.events;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.geom;

namespace IDE.ui
{
    public class ProjectProperties : BuildPropertiesDialog
    {   
		ValueContainer<String> mVC;

        enum CategoryType
        {
			General, ///
            Project,
			/*Platform,
			Platform_Windows,
			Platform_Linux,*/
            Dependencies,
			Beef_Global,
			Platform,

			Targeted, ///
			Beef_Targeted,
			Build,
            Debugging,
            C,

            COUNT
        }
        
        public Project mProject;
        Dictionary<String, ValueContainer<bool>> mDependencyValuesMap ~ DeleteDictionaryAndKeysAndItems!(_);
		Project.Options[] mCurProjectOptions ~ delete _;
		float mLockFlashPct;
		public int32 mNewDebugSessionCountdown;
        
        public this(Project project)
        {
            mActiveConfigName.Clear();
            mActivePlatformName.Clear();

            var app = IDEApp.sApp;            
            Workspace.ConfigSelection configSelection;
            var options = app.mWorkspace.mConfigs[app.mConfigName].mPlatforms[app.mPlatformName];            
            options.mConfigSelections.TryGetValue(project, out configSelection);            
            if (configSelection != null)
            {
				ClearAndDeleteItems(mConfigNames);
                mConfigNames.Add(new String(configSelection.mConfig));
				ClearAndDeleteItems(mPlatformNames);
                mPlatformNames.Add(new String(configSelection.mPlatform));
                if ((configSelection.mEnabled) && (project.GetOptions(mConfigNames[0], mPlatformNames[0]) != null))
                {
                    mActiveConfigName.Set(mConfigNames[0]);
                    mActivePlatformName.Set(mPlatformNames[0]);
                }
                else
                    configSelection = null;
            }
            if (configSelection == null)
            {
                //mConfigNames.Set("");
                //mPlatformNames.Set("");

				List<String> sortedConfigNames = scope List<String>(project.mConfigs.Keys);
				//TODO: sortedConfigNames();
				ClearAndDeleteItems(mConfigNames);
                mConfigNames.Add(new String(sortedConfigNames[0]));
				List<String> sortedPlatformNames = scope List<String>(project.mConfigs[mConfigNames[0]].mPlatforms.Keys);
				//TODO: sortedPlatformNames.Sort();
				ClearAndDeleteItems(mPlatformNames);
                mPlatformNames.Add(new String(sortedPlatformNames[0]));
            }

            //mConfigComboBox.Label = (mActiveConfigName == mConfigName) ? String.Format("Active({0})", mConfigName) : mConfigName;
            //mPlatformComboBox.Label = (mActivePlatformName == mPlatformName) ? String.Format("Active({0})", mPlatformName) : mPlatformName;

            mTitle = new String(project.mProjectName, " Properties");
            mProject = project;                        
            
            var root = (DarkListViewItem)mCategorySelector.GetRoot();

			var globalItem = AddCategoryItem(root, "General");
            var item = AddCategoryItem(globalItem, "Project");
			if (!project.IsDebugSession)
            	item.Focused = true;
			AddCategoryItem(globalItem, "Dependencies");
			AddCategoryItem(globalItem, "Beef");
			AddCategoryItem(globalItem, "Platform");
			globalItem.Open(true, true);

			var targetedItem = AddCategoryItem(root, "Targeted");
			AddCategoryItem(targetedItem, "Beef");
			AddCategoryItem(targetedItem, "Build");
            item = AddCategoryItem(targetedItem, "Debugging");
			if (project.IsDebugSession)
				item.Focused = true;
			targetedItem.Open(true, true);
            //AddCategoryItem(root, "C/C++");

			if (project.IsDebugSession)
				mHideSelector = true;
        }

		public ~this()
		{
			
		}

		protected override bool HasChanges()
		{
			if (mPropPage.mCategoryType == (int)CategoryType.Beef_Global)
			{
				for (let option in mProject.mBeefGlobalOptions.mDistinctBuildOptions)
				{
					if (option.mCreateState != .Normal)
						return true;
				}
			}
			else if (mPropPage.mCategoryType == (int)CategoryType.Beef_Targeted)
			{
				for (var target in mCurPropertiesTargets)
				{
					let options = (Project.Options)target;
					for (let option in options.mBeefOptions.mDistinctBuildOptions)
					{
						if (option.mCreateState != .Normal)
							return true;
					}
				}
			}

			return false;
		}

        protected override TargetedKind GetCategoryTargetedKind(int32 categoryTypeInt)
        {
        	switch ((CategoryType)categoryTypeInt)
			{
			case .General,
				 .Project,
				 .Dependencies,
				 .Beef_Global:
				return .None;
			case .Platform:
				return .Platform;
			default:
				return .Config;
			}
        }

		public override void GetConfigList(List<String> configNames)
		{
			for (var configName in mProject.mConfigs.Keys)
				configNames.Add(configName);
		}

		public override void GetPlatformList(List<String> platformNames)
		{
			/*var configName = mConfigNames[0];
			for (var platformName in mProject.mConfigs[configName].mPlatforms.Keys)
				platformNames.Add(platformName);*/
			
			HashSet<String> platformSet = scope .();
			for (var config in mProject.mConfigs.Values)
			{
				for (var platform in config.mPlatforms.Keys)
					if (platformSet.Add(platform))
						platformNames.Add(platform);
			}
		}

        public override bool CreateNewConfig(String name, String copiedFromConfig)
        {
			var useName = scope String(name);
			useName.Trim();

			if (mProject.mConfigs.ContainsKey(useName))
			{
				gApp.Fail(scope String()..AppendF("Project already contains a config named '{0}'", useName));
				return false;
			}

			var oldConfig = mProject.mConfigs[copiedFromConfig];
            Project.Config newConfig = new Project.Config();
			for (var platformKV in oldConfig.mPlatforms)
			{
				var oldOptions = platformKV.value;
				var newOptions = oldOptions.Duplicate();
				newConfig.mPlatforms[new String(platformKV.key)] = newOptions;
			}

			mProject.mConfigs[new String(name)] = newConfig;
			mProject.SetChanged();

			SelectConfig(name);
			return true;
        }

		public override void EditConfigs()
		{
		    let dialog = new EditTargetDialog(this, .Config);
			for (var config in mProject.mConfigs.Keys)
				dialog.Add(config);
			dialog.FinishInit();
			dialog.AddOkCancelButtons(new (dlg) =>
				{
					Dictionary<String, String> configNameMap = scope Dictionary<String, String>();
					bool hadChanges = false;

					Dictionary<String, Project.Config> newConfigs = new .();
					for (let entry in dialog.mEntries)
					{
						let kv = mProject.mConfigs.GetAndRemove(entry.mOrigName).Get();
						String matchKey = kv.key;
						Project.Config config = kv.value;
						if (entry.mDelete)
						{
							hadChanges = true;
							delete matchKey;
							delete config;
						}
						else
						{
							if ((mConfigNames.Contains(entry.mOrigName)) && (entry.mNewName != null))
							{
								mConfigComboBox.Label = entry.mNewName;
								ClearAndDeleteItems(mConfigNames);
								mConfigNames.Add(new String(entry.mNewName));
							}
							if ((entry.mNewName != null) && (entry.mNewName != entry.mOrigName))
							{
								hadChanges = true;
								configNameMap[entry.mOrigName] = entry.mNewName;
							}
							delete matchKey;
							newConfigs[new String(entry.mNewName ?? entry.mOrigName)] = config;
						}
					}

					if (!configNameMap.IsEmpty)
					{
						for (var workspaceConfig in gApp.mWorkspace.mConfigs.Values)
						{
							for (let platform in workspaceConfig.mPlatforms.Values)
							{
								Workspace.ConfigSelection configSelection;
								if (platform.mConfigSelections.TryGetValue(mProject, out configSelection))
								{
									String newConfig;
									if (configNameMap.TryGetValue(configSelection.mConfig, out newConfig))
									{
										configSelection.mConfig.Set(newConfig);
									}
								}
							}
						}
					}

					delete mProject.mConfigs;
					mProject.mConfigs = newConfigs;
					if (hadChanges)
						mProject.mHasChanged = true;

					for (var window in gApp.mWindows)
					{
						if (var widgetWindow = window as WidgetWindow)
						{
							if (var projectProperties = widgetWindow.mRootWidget as ProjectProperties)
							{
								for (let entry in dialog.mEntries)
								{
									if (entry.mDelete)
										projectProperties.ConfigDeleted(entry.mOrigName);
									if (entry.mNewName != null)
										projectProperties.ConfigRenamed(entry.mOrigName, entry.mNewName);
								}
							}
						}
					}
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
					bool hadChanges = false;
					for (let entry in dialog.mEntries)
					{
						if ((!entry.mDelete) && (entry.mNewName == null))
							continue;

						ConfigLoop: for (var configName in mConfigNames)
						{
							Project.Config config;
							if (!mProject.mConfigs.TryGetValue(configName, out config))
								continue;
							String matchKey;
							Project.Options options;
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
								Project.Options* newOptionsPtr;
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
							if (var projectProperties = widgetWindow.mRootWidget as ProjectProperties)
							{
								for (let entry in dialog.mEntries)
								{
									if (entry.mDelete)
										projectProperties.PlatformDeleted(entry.mOrigName);
									if (entry.mNewName != null)
										projectProperties.PlatformRenamed(entry.mOrigName, entry.mNewName);
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
            var workspace = IDEApp.sApp.mWorkspace;

            using (workspace.mMonitor.Enter())
            {
                name.Trim();
                if (name.Length > 0)
                {
                    for (var projectConfig in mProject.mConfigs.Values)
                    {
                        Project.Options projectOptions = new Project.Options();
                        projectConfig.mPlatforms[new String(name)] = projectOptions;
                    }

                    mProject.SetChanged();                    
                    SelectPlatform(name);
					gApp.mWorkspace.MarkPlatformNamesDirty();
                }
            }
        }

		protected override void ResetSettings()
		{
			var targetDict = scope Dictionary<Object, Object>();
			switch ((CategoryType)mPropPage.mCategoryType)
			{
			case .Project:
				var generalOptions = scope Project.GeneralOptions;
				mProject.SetupDefault(generalOptions);
				targetDict[mCurPropertiesTargets[0]] = generalOptions;
				UpdateFromTarget(targetDict);
			case .Beef_Global:
				DeleteDistinctBuildOptions();
				DistinctBuildOptions defaultTypeOptions = scope:: .();
				for (var typeOption in mProject.mBeefGlobalOptions.mDistinctBuildOptions)
					targetDict[typeOption] = defaultTypeOptions;
				var generalOptions = scope Project.BeefGlobalOptions;
				mProject.SetupDefault(generalOptions);
				targetDict[mCurPropertiesTargets[0]] = generalOptions;
				UpdateFromTarget(targetDict);
			case .Platform:
				for (var platformName in mPlatformNames)
				{
					let platform = Workspace.PlatformType.GetFromName(platformName);
					switch (platform)
					{
					case .Windows:
						var windowsOptions = scope Project.WindowsOptions();
						targetDict[mCurPropertiesTargets[0]] = windowsOptions;
						UpdateFromTarget(targetDict);
					case .Linux:
						var linuxOptions = scope Project.LinuxOptions();
						targetDict[mCurPropertiesTargets[0]] = linuxOptions;
						UpdateFromTarget(targetDict);
					default:
					}
				}
			case .Build, .Debugging, .Beef_Targeted:
				DeleteDistinctBuildOptions();
				DistinctBuildOptions defaultTypeOptions = scope:: .();
				int propIdx = 0;
				for (var configName in mConfigNames)
				{
					for (var platformName in mPlatformNames)
					{
						Project.Options defaultOptions = scope:: .();
						mProject.SetupDefault(defaultOptions, configName, platformName);
						var curOptions = (Project.Options)mCurPropertiesTargets[propIdx];
						targetDict[curOptions] = defaultOptions;
						targetDict[curOptions.mBuildOptions] = defaultOptions.mBuildOptions;
						targetDict[curOptions.mDebugOptions] = defaultOptions.mDebugOptions;
						targetDict[curOptions.mBeefOptions] = defaultOptions.mBeefOptions;
						for (var typeOption in curOptions.mBeefOptions.mDistinctBuildOptions)
							targetDict[typeOption] = defaultTypeOptions;
						propIdx++;
					}
				}
				UpdateFromTarget(targetDict);
			default:
			}
		}

        protected override void ShowPropPage(int32 categoryTypeInt)
        {
            CategoryType categoryType = (CategoryType)categoryTypeInt;            

			/*switch (categoryType)
			{
			case .Platform,
				 .Global,
				 .Targeted:
				if (mPropPage != null)
				{
					
				}
				return;// Not an actual category
			default:
			}*/

			base.ShowPropPage(categoryTypeInt);

			var configName = mConfigNames[0];
			var platformName = mPlatformNames[0];

			int propIdx = 0;
			delete mCurPropertiesTargets;
			mCurPropertiesTargets = null;

			DeleteAndNullify!(mCurProjectOptions);

			let targetKind = GetCategoryTargetedKind(categoryTypeInt);

			bool areSamePlatforms = true;

			if (targetKind == .Platform)
			{
				let platformKind = Workspace.PlatformType.GetFromName(platformName);

				for (var checkPlatformName in mPlatformNames)
				{
					let checkPlatformKind = Workspace.PlatformType.GetFromName(checkPlatformName);
					if (checkPlatformKind != platformKind)
						areSamePlatforms = false;
				}

				if (areSamePlatforms)
				{
					mCurPropertiesTargets = new Object[1];
					switch (platformKind)
					{
					case .Windows: mCurPropertiesTargets[0] = mProject.mWindowsOptions;
					case .Linux: mCurPropertiesTargets[0] = mProject.mLinuxOptions;
					default:
					}
				}
			}
			else if (targetKind == .Config)
			{
				mCurPropertiesTargets = new Object[mConfigNames.Count * mPlatformNames.Count];
				mCurProjectOptions = new Project.Options[mConfigNames.Count * mPlatformNames.Count];
				for (var checkConfigName in mConfigNames)
				{
					for (var checkPlatformName in mPlatformNames)
					{
						let projectOptions = mProject.GetOptions(checkConfigName, checkPlatformName, true);
						mCurProjectOptions[propIdx] = projectOptions;
						mCurPropertiesTargets[propIdx] = projectOptions;
						propIdx++;
					}
				}
			}
			else
			{
				mCurPropertiesTargets = new Object[1];
				if (categoryType == .Project)
					mCurPropertiesTargets[0] = mProject.mGeneralOptions;
				else if (categoryType == .Beef_Global)
					mCurPropertiesTargets[0] = mProject.mBeefGlobalOptions;
			}

            ConfigDataGroup targetedConfigData;
            if ((GetCategoryTargetedKind(categoryTypeInt) == .Config) &&
				((mConfigNames.Count == 1) && (mPlatformNames.Count == 1)))
            {
                var key = Tuple<String, String>(configName, platformName);
                var targetedConfigDataResult = mTargetedConfigDatas.GetValue(key);
                if (!(targetedConfigDataResult case .Ok(out targetedConfigData)))
                {
					key.Item1 = new String(key.Item1);
					key.Item2 = new String(key.Item2);
                    targetedConfigData = new ConfigDataGroup((int32)CategoryType.COUNT);
                    targetedConfigData.mTarget = key;
                    mTargetedConfigDatas[key] = targetedConfigData;
                }
            }
            else
            {
                if (mMultiTargetConfigData == null)
                {
                    mMultiTargetConfigData = new ConfigDataGroup((int32)CategoryType.COUNT);
					mMultiTargetConfigData.mIsMultiTargeted = true;
                }
                targetedConfigData = mMultiTargetConfigData;

				if (GetCategoryTargetedKind(categoryTypeInt) != .None)
				{
					DeleteAndNullify!(targetedConfigData.mPropPages[categoryTypeInt]);
				}
            }

			// Always add the current to the back so the most recently viewed one will apply changes last.
			//  This matters when we have both project-specific and multiply-selected config data
			mConfigDatas.Remove(targetedConfigData);
			mConfigDatas.Add(targetedConfigData);

            if (targetedConfigData.mPropPages[(int32)categoryType] == null)
            {
                CreatePropPage(categoryTypeInt, .AllowSearch | .AllowReset);
                targetedConfigData.mPropPages[categoryTypeInt] = mPropPage;

                //mProperties.SetShowHeader(false);
                mPropPage.mPropertiesListView.InitScrollbars(false, true);
                //mPropPage.mPropertiesListView.mAutoFocus = true;
                mPropPage.mPropertiesListView.mShowColumnGrid = true;
                mPropPage.mPropertiesListView.mShowGridLines = true;

                if (categoryType == CategoryType.Project)
                    PopulateGeneralOptions();
                else if (categoryType == CategoryType.Dependencies)
                    PopulateDependencyOptions();
				else if (categoryType == .Platform)
				{
					if (areSamePlatforms)
					{
						let platformKind = Workspace.PlatformType.GetFromName(platformName);
						switch (platformKind)
						{
						case .Windows: PopulateWindowsOptions();
						case .Linux: PopulateLinuxOptions();
						default:
						}
					}
				}
				else if (categoryType == CategoryType.Build)
					PopulateBuildOptions();
				else if (categoryType == CategoryType.Beef_Global				)
					PopulateBeefSharedOptions();
                else if (categoryType == CategoryType.Beef_Targeted)
                    PopulateBeefTargetedOptions();
                else if (categoryType == CategoryType.C)
                    PopulateCOptions();
                else if (categoryType == CategoryType.Debugging)
                    PopulateDebuggingOptions();
            }
            mPropPage = targetedConfigData.mPropPages[(int32)categoryType];
            AddPropPageWidget();
            ResizeComponents();
        }

        void PopulateGeneralOptions()
        {
            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			AddPropertiesItem(root, "Target Type", "mTargetType", scope String[]
				{
					"Console Application",
					"GUI Application",
					"Library",
					"Dynamic Library",
					"Custom Build"
				});
			AddPropertiesItem(root, "Project Name Aliases", "mAliases");
        }

		void PopulateWindowsOptions()
		{
			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (category, ?) = AddPropertiesItem(root, "Resources");
			category.mIsBold = true;
			category.mTextColor = 0xFFE8E8E8;
			var (listViewItem, propEntry) = AddPropertiesItem(category, "Icon File", "mIconFile");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Manifest File", "mManifestFile");
			category.Open(true, true);

			(category, ?) = AddPropertiesItem(root, "Version");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Description", "mDescription");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Comments", "mComments");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Company", "mCompany");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Product", "mProduct");
			(listViewItem, propEntry) = AddPropertiesItem(category, "Copyright", "mCopyright");
			(listViewItem, propEntry) = AddPropertiesItem(category, "FileVersion", "mFileVersion");
			(listViewItem, propEntry) = AddPropertiesItem(category, "ProductVersion", "mProductVersion");
			//parent.MakeParent();
			category.Open(true, true);
		}

		void PopulateLinuxOptions()
		{
		    var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
		    var (category, ?) = AddPropertiesItem(root, "General");
		    category.mIsBold = true;
		    category.mTextColor = 0xFFE8E8E8;
			AddPropertiesItem(category, "Options", "mOptions");
		    //parent.MakeParent();
		    category.Open(true, true);
		}

		void PopulateBuildOptions()
		{
		    var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (listViewItem, propEntry) = AddPropertiesItem(root, "Build Type", "mBuildOptions.mBuildKind");
		    (listViewItem, propEntry) = AddPropertiesItem(root, "Target Directory", "mBuildOptions.mTargetDirectory");
		    (listViewItem, propEntry) = AddPropertiesItem(root, "Target Name", "mBuildOptions.mTargetName");
		    (listViewItem, propEntry) = AddPropertiesItem(root, "Other Linker Flags", "mBuildOptions.mOtherLinkFlags");
		    (listViewItem, propEntry) = AddPropertiesItem(root, "C Library", "mBuildOptions.mCLibType");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Beef Library", "mBuildOptions.mBeefLibType");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Stack Size", "mBuildOptions.mStackSize");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Additional Lib Paths", "mBuildOptions.mLibPaths");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Rebuild Dependencies", "mBuildOptions.mLinkDependencies");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Prebuild Commands", "mBuildOptions.mPreBuildCmds");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Postbuild Commands", "mBuildOptions.mPostBuildCmds");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Clean Commands", "mBuildOptions.mCleanCmds");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Build Commands on Compile", "mBuildOptions.mBuildCommandsOnCompile");
			(listViewItem, propEntry) = AddPropertiesItem(root, "Build Commands on Run", "mBuildOptions.mBuildCommandsOnRun");
		}

        void PopulateDependencyOptions()
        {
            mDependencyValuesMap = new Dictionary<String, ValueContainer<bool>>();

            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            var category = root;

            List<String> projectNames = scope List<String>();
            for (int32 projectIdx = 0; projectIdx < IDEApp.sApp.mWorkspace.mProjects.Count; projectIdx++)
            {
                var project = IDEApp.sApp.mWorkspace.mProjects[projectIdx];
                if (project == mProject)
                    continue;
                projectNames.Add(project.mProjectName);
            }

            for (var dep in mProject.mDependencies)
            {
                if (!projectNames.Contains(dep.mProjectName))
                    projectNames.Add(dep.mProjectName);
            }

            projectNames.Sort(scope (a, b) => String.Compare(a, b, true));

            for (var projectName in projectNames)
            {                
                var dependencyContainer = new ValueContainer<bool>();
                dependencyContainer.mValue = mProject.HasDependency(projectName);
                mDependencyValuesMap[new String(projectName)] = dependencyContainer;
                
                var (listViewItem, propItem) = AddPropertiesItem(category, projectName);
                if (IDEApp.sApp.mWorkspace.FindProject(projectName) == null)
                    listViewItem.mTextColor = 0xFFFF6060;

                var subItem = listViewItem.CreateSubItem(1);

                var checkbox = new DarkCheckBox();
                checkbox.Checked = dependencyContainer.mValue;
                checkbox.Resize(0, 0, DarkTheme.sUnitSize, DarkTheme.sUnitSize);
                subItem.AddWidget(checkbox);

				PropEntry[] propEntries = new PropEntry[1];

                PropEntry propEntry = new PropEntry();
                propEntry.mTarget = dependencyContainer;
                //propEntry.mFieldInfo = dependencyContainer.GetType().GetField("mValue").Value;
                propEntry.mOrigValue = Variant.Create(dependencyContainer.mValue);
                propEntry.mCurValue = propEntry.mOrigValue;
				
                propEntry.mListViewItem = listViewItem;
                propEntry.mCheckBox = checkbox;
				propEntry.mApplyAction = new () =>
					{
						if (propEntry.mCurValue.Get<bool>())
						{
							if (!mProject.HasDependency(listViewItem.mLabel))
							{
								var dep = new Project.Dependency();
								dep.mProjectName = new String(listViewItem.mLabel);
								dep.mVerSpec = new .();
								dep.mVerSpec.SetSemVer("*");
								mProject.mDependencies.Add(dep);
							}
						}
						else
						{
							int idx = mProject.mDependencies.FindIndex(scope (dep) => dep.mProjectName == listViewItem.mLabel);
							if (idx != -1)
							{
								delete mProject.mDependencies[idx];
								mProject.mDependencies.RemoveAt(idx);
							}							
						}
						propEntry.mOrigValue = propEntry.mCurValue;
					};

                checkbox.mOnMouseUp.Add(new (evt) => { PropEntry.DisposeVariant(ref propEntry.mCurValue); propEntry.mCurValue = Variant.Create(checkbox.Checked); });

				propEntries[0] = propEntry;
                mPropPage.mPropEntries[listViewItem] = propEntries;
            }            
        }

		protected override Object[] PhysAddNewDistinctBuildOptions()
		{
			if (mCurProjectOptions == null)
			{
				let typeOptions = new DistinctBuildOptions();
				typeOptions.mCreateState = .New;
				mProject.mBeefGlobalOptions.mDistinctBuildOptions.Add(typeOptions);
				Object[] typeOptionsTargets = new .(typeOptions);
				return typeOptionsTargets;
			}
			else
			{
				Object[] typeOptionsTargets = new Object[mCurProjectOptions.Count];
				for (int idx < mCurProjectOptions.Count)
				{
					var curWorkspaceOptions = mCurProjectOptions[idx];
					let typeOptions = new DistinctBuildOptions();
					typeOptions.mCreateState = .New;
					curWorkspaceOptions.mBeefOptions.mDistinctBuildOptions.Add(typeOptions);
					typeOptionsTargets[idx] = typeOptions;
				}
				return typeOptionsTargets;
			}
		}

		void PopulateBeefSharedOptions()
		{
		    var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
		    var (category, ?) = AddPropertiesItem(root, "General");
		    category.mIsBold = true;
		    category.mTextColor = 0xFFE8E8E8;
			
			AddPropertiesItem(category, "Startup Object", "mStartupObject");
			AddPropertiesItem(category, "Default Namespace", "mDefaultNamespace");
			AddPropertiesItem(category, "Preprocessor Macros", "mPreprocessorMacros");
			DistinctOptionBuilder dictinctOptionBuilder = scope .(this);
			dictinctOptionBuilder.Add(mProject.mBeefGlobalOptions.mDistinctBuildOptions);
			dictinctOptionBuilder.Finish();
			AddNewDistinctBuildOptions();
		    //parent.MakeParent();
		    category.Open(true, true);
		}

        void PopulateBeefTargetedOptions()
        {
            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            var (category, propEntry) = AddPropertiesItem(root, "General");
            category.mIsBold = true;
            category.mTextColor = 0xFFE8E8E8;
            AddPropertiesItem(category, "Preprocessor Macros", "mBeefOptions.mPreprocessorMacros");
            category.Open(true, true);

            (category, propEntry) = AddPropertiesItem(root, "Code Generation");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
			AddPropertiesItem(category, "Reloc Model", "mBeefOptions.mRelocType");
			AddPropertiesItem(category, "PIC Level", "mBeefOptions.mPICLevel");
            AddPropertiesItem(category, "Optimization Level", "mBeefOptions.mOptimizationLevel",
                scope String[] { "O0", "O1", "O2", "O3", "Og", "Og+" }); // -O0 .. -O3,  -Os, -Ofast, -Og
			AddPropertiesItem(category, "LTO", "mBeefOptions.mLTOType");
            AddPropertiesItem(category, "Vectorize Loops", "mBeefOptions.mVectorizeLoops");
            AddPropertiesItem(category, "Vectorize SLP", "mBeefOptions.mVectorizeSLP");
            category.Open(true, true);

			DistinctOptionBuilder dictinctOptionBuilder = scope .(this);
			for (int propIdx < mCurProjectOptions.Count)
			{
				var curWorkspaceOptions = mCurProjectOptions[propIdx];
				dictinctOptionBuilder.Add(curWorkspaceOptions.mBeefOptions.mDistinctBuildOptions);
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
            AddPropertiesItem(category, "Compiler", "mCOptions.mCompilerType");            
            AddPropertiesItem(category, "Other C Flags", "mCOptions.mOtherCFlags");
            AddPropertiesItem(category, "Other C++ Flags", "mCOptions.mOtherCPPFlags");
            AddPropertiesItem(category, "Enable Beef Interop", "mCOptions.mEnableBeefInterop",
                scope String[] { "No", "Yes" });
            var parent = AddPropertiesItem(category, "Include Paths", "mCOptions.mIncludePaths");
            parent = AddPropertiesItem(category, "Preprocessor Macros", "mCOptions.mPreprocessorMacros");
            //parent.MakeParent();
            category.Open(true, true);

            (category, propEntry) = AddPropertiesItem(root, "Code Generation", "");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
            AddPropertiesItem(category, "Disable C++ Exceptions", "mCOptions.mDisableExceptions",
                scope String[] { "No", "Yes (-fno-exceptions)" }); // -fno-exceptions
            AddPropertiesItem(category, "SIMD Instructions", "mCOptions.mSIMD"); // -msse, -msse2, -msse4.1, -mno-sse
            AddPropertiesItem(category, "Generate LLVM IR", "mCOptions.mGenerateLLVMAsm",
                scope String[] { "No", "Yes (-emit-llvm)" });
            AddPropertiesItem(category, "No Omit Frame Pointers", "mCOptions.mNoOmitFramePointers",
                scope String[] { "No", "Yes (-fno-omit-frame-pointer)" }); //-fno-omit-frame-pointer
            AddPropertiesItem(category, "Disable Inlining", "mCOptions.mDisableInlining",
                scope String[] { "No", "Yes (-fno-inline)" }); // -fno-inline
            AddPropertiesItem(category, "Strict Aliasing", "mCOptions.mStrictAliasing",
                scope String[] { "No", "Yes (-fstrict-aliasing)" }); // -fstrict-aliasing
            AddPropertiesItem(category, "Fast Math", "mCOptions.mFastMath",
                scope String[] { "No", "Yes (-ffast-math)" }); // -ffast-math
            AddPropertiesItem(category, "Disable RTTI", "mCOptions.mDisableRTTI",
                scope String[] { "No", "Yes (-fno-rtti)" }); // -fno-rtti
            AddPropertiesItem(category, "Optimization Level", "mCOptions.mOptimizationLevel"); // -O0 .. -O3,  -Os, -Ofast, -Og
            AddPropertiesItem(category, "Debug Info", "mCOptions.mEmitDebugInfo",
                scope String[] { "None", "DWARF (-g)" });
            AddPropertiesItem(category, "Address Sanitizer", "");
            category.Open(true, true);

            (category, propEntry) = AddPropertiesItem(root, "Warnings", "");
            category.mIsBold = true;
            category.mTextColor = cHeaderColor;
            AddPropertiesItem(category, "All warnings", "mCOptions.mAllWarnings",
                scope String[] { "No", "Yes (-Wall)" }); // -Wall
            AddPropertiesItem(category, "Effective C++ Violations", "mCOptions.mEffectiveCPPViolations",
                scope String[] { "No", "Yes (-Weffc++)" }); //-Weffc++
            AddPropertiesItem(category, "Pedantic", "mCOptions.mPedantic",
                scope String[] { "No", "Yes (-pedantic)" }); //-pedantic
            AddPropertiesItem(category, "Warnings as errors", "mCOptions.mWarningsAsErrors",
                scope String[] { "No", "Yes (-Werror)" });  //-Werror
            AddPropertiesItem(category, "Specific Warnings As Errors", "",
                scope String[] { "No", "Yes (-Werror=)" }); // -Werror=
            AddPropertiesItem(category, "Disable Specific Warnings", "",
                scope String[] { "No", "Yes (-Wno-)" }); //-Wno-
            category.Open(true, true);
        }

        void PopulateDebuggingOptions()
        {
			if (mProject.IsDebugSession)
			{
				PopulateDebuggingSessionOptions();
				return;
			}	

            var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
            //var category = AddPropertiesItem(root, "General");
            //category.mIsBold = true;
            //category.mTextColor = 0xFFE8E8E8;
            var (category, propEntry) = AddPropertiesItem(root, "Command", "mDebugOptions.mCommand", null, .BrowseForFile);
			propEntry.mRelPath = new String(mProject.mProjectDir);
            AddPropertiesItem(root, "Command Arguments", "mDebugOptions.mCommandArguments");
            (category, propEntry) = AddPropertiesItem(root, "Working Directory", "mDebugOptions.mWorkingDirectory", null, .BrowseForFolder);
			propEntry.mRelPath = new String(mProject.mProjectDir);
            AddPropertiesItem(root, "Environment Variables", "mDebugOptions.mEnvironmentVars");
            //parent.MakeParent();
            root.Open(true, true);
        }

		void PopulateDebuggingSessionOptions()
		{
			mPropPage.mFlags = .None;

		    var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
		    //var category = AddPropertiesItem(root, "General");
		    //category.mIsBold = true;
		    //category.mTextColor = 0xFFE8E8E8;
			var (category, propEntry) = AddPropertiesItem(root, "Debug Target", "mBuildOptions.mTargetName", null, .BrowseForFile);
			propEntry.mRelPath = new String(mProject.mProjectDir);
		    (category, propEntry) = AddPropertiesItem(root, "Debug Command", "mDebugOptions.mCommand", null, .BrowseForFile);
			propEntry.mRelPath = new String(mProject.mProjectDir);
		    AddPropertiesItem(root, "Command Arguments", "mDebugOptions.mCommandArguments");
		    (category, propEntry) = AddPropertiesItem(root, "Working Directory", "mDebugOptions.mWorkingDirectory", null, .BrowseForFolder);
			propEntry.mRelPath = new String(mProject.mProjectDir);
		    AddPropertiesItem(root, "Environment Variables", "mDebugOptions.mEnvironmentVars");
			AddPropertiesItem(root, "Build Commands", "mBuildOptions.mPostBuildCmds");
			AddPropertiesItem(root, "Build Commands on Compile", "mBuildOptions.mBuildCommandsOnCompile");
			AddPropertiesItem(root, "Build Commands on Run", "mBuildOptions.mBuildCommandsOnRun");
			AddPropertiesItem(root, "Clean Commands", "mBuildOptions.mCleanCmds");
		    //parent.MakeParent();
		    root.Open(true, true);
		}

        protected override bool ApplyChanges()
        {
			if (mApplyButton.mDisabled)
				return true;

			if (mProject.mLocked)
			{
				let dialog = gApp.Fail(
					"""
					This project is locked because it may be a shared library, and editing shared libraries may have unwanted effects on other programs that use it.

					If you are sure you want to edit this project then you can unlock it with the lock icon in the lower left of the properties dialog.
					""",
					null, mWidgetWindow);
				dialog.mWindowFlags |= .Modal;
				if (dialog != null)
				{
					dialog.mOnClosed.Add(new () =>
						{
							mLockFlashPct = 0.00001f;
						});
				}
				mLockFlashPct = 0.00001f;
				return false;
			}

            bool hadChange = false;

            /*if (!AssertNotCompilingOrRunning())
                return false;*/

            using (mProject.mMonitor.Enter())
            {
                for (var targetedConfigData in mConfigDatas)
                {
                    for (var propPage in targetedConfigData.mPropPages)
                    {
                        if (propPage == null)
                            continue;

						bool configDataHadChange = false;
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
									if (var projectProperties = widgetWindow.mRootWidget as ProjectProperties)
									{
										if (projectProperties == this)
											continue;
										if (projectProperties.mProject != mProject)
											continue;
										if (GetCategoryTargetedKind(propPage.mCategoryType) != .None)
										{
											if (mPropPage == propPage)
											{
												for (var configName in mConfigNames)
													for (var platformName in mPlatformNames)
														projectProperties.HadExternalChanges(configName, platformName);
											}
											else
												projectProperties.HadExternalChanges(targetedConfigData.mTarget.Item1, targetedConfigData.mTarget.Item2);
										}
										else
											projectProperties.HadExternalChanges(null, null);
									}
								}
							}

							hadChange = true;
						}
                    }
                }
            }

            if (hadChange)
            {
                mProject.SetChanged();
                IDEApp.sApp.ProjectOptionsChanged(mProject);
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
			if (ApplyDistinctBuildOptions(mProject.mBeefGlobalOptions.mDistinctBuildOptions, apply))
				mProject.SetChanged();
			for (let config in mProject.mConfigs.Values)
			{
				for (let platform in config.mPlatforms.Values)
				{
					if (ApplyDistinctBuildOptions(platform.mBeefOptions.mDistinctBuildOptions, apply))
						mProject.SetChanged();
				}
			}
		}

		public override void Close()
		{
			base.Close();
			SetWorkspaceData(false);
		}

        public override void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
        {
            base.PopupWindow(parentWindow, offsetX, offsetY);
            mWidgetWindow.SetMinimumSize(GS!(480), GS!(320));
        }

        public override void CalcSize()
        {
			if (mProject.IsDebugSession)
			{
				mWidth = GS!(512);
				mHeight = GS!(380);
			}
			else
			{
	            mWidth = GS!(660);
	            mHeight = GS!(512);
			}
        }

		public override void Update()
		{
			base.Update();
			if (mLockFlashPct != 0)
			{
				mLockFlashPct += 0.01f;
				if (mLockFlashPct >= 1.0f)
					mLockFlashPct = 0;
				MarkDirty();
			}

			if ((mNewDebugSessionCountdown > 0) && (--mNewDebugSessionCountdown == 0))
			{
				if (mEditingListViewItem == null)
				{
					// Show edit for "Command"
					let lvItem = mPropPage.mPropertiesListView.GetRoot().GetChildAtIndex(0);
					EditValue(lvItem, mPropPage.mPropEntries[lvItem]);
				}
			}
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);
			IDEUtils.DrawLock(g, GS!(6), mHeight - GS!(24), mProject.mLocked, mLockFlashPct);
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			float lockX = GS!(6);
			float lockY = mHeight - GS!(24);
			if (Rect(lockX, lockY, GS!(20), GS!(20)).Contains(x, y))
			{
				Menu menu = new Menu();
				var menuItem = menu.AddItem("Lock Project");
				menuItem.mOnMenuItemSelected.Add(new (dlg) =>
					{
						mProject.mLocked = !mProject.mLocked;
						gApp.mWorkspace.SetChanged();
						gApp.mProjectPanel.MarkDirty();
					});
				if (mProject.mLocked)
					menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
				MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
				menuWidget.Init(this, x, y, true);
			}
		}
    }
}
