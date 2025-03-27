using System;
using System.Collections;
using System.IO;
using System.Reflection;
using Beefy.utils;

namespace IDE
{
	class CustomBuildProperties
	{
		static String[?] BUILT_IN_PROPERTIES = .(
			"Slash",
			"Var",
			"Arguments",
			"BuildDir",
			"LinkFlags",
			"ProjectDir",
			"ProjectName",
			"TargetDir",
			"TargetPath",
			"WorkingDir",
			"ProjectName",
			"VSToolPath",
			"VSToolPath_x86",
			"VSToolPath_x64",
			"EmccPath",
			"ScriptDir",
			"Configuration",
			"Platform",
			"WorkspaceDir",
			"BeefPath"
		);

		static Dictionary<String, String> mProperties = new .() ~ DeleteDictionaryAndKeysAndValues!(_);
		static Dictionary<Project, Project> mProjects = new .() ~ DeleteDictionaryAndValues!(_); 

		static public void GetFileName(String outResult)
		{
			String workspaceDir = scope String();

			if (gApp.mWorkspace.mDir == null)
				Directory.GetCurrentDirectory(workspaceDir);
			else
				workspaceDir = gApp.mWorkspace.mDir;

			outResult.Append(workspaceDir, "/BeefProperties.toml");
		}

		static public void Clear()
		{
			for (var entry in mProperties)
			{
				delete entry.key;
				delete entry.value;
			}

			for (var entry in mProjects)
				delete entry.value;

			mProperties.Clear();
			mProjects.Clear();
		}

		static public void Load()
		{
			const char8* PROPERTIES_STR = "Properties";

			Clear();

			String propertiesFileName = scope String();
			GetFileName(propertiesFileName);

			if (!File.Exists(propertiesFileName))
				return;

			StructuredData data = scope StructuredData();
			if (gApp.StructuredLoad(data, propertiesFileName) case .Err(let err))
			{
				gApp.OutputErrorLine("Failed to load properties from: '{0}'", propertiesFileName);
				return;
			}

			if (!data.Contains(PROPERTIES_STR))
				return;

			for (var property in data.Enumerate(PROPERTIES_STR))
			{
				String propertyStr = new String();
				property.ToString(propertyStr);

				if (Contains(propertyStr))
				{
					delete propertyStr;
					continue;
				}

				String propertyValue = new String();
				data.GetCurString(propertyValue);

				mProperties.Add(propertyStr, propertyValue);
			}
		}

		static public bool Contains(String property)
		{
			return mProperties.ContainsKey(property);
		}

		static public String Get(String property)
		{
			if (!Contains(property))
				return null;

			return mProperties[property];
		}

		static public void ResolveString(StringView configString, String outResult)
		{
			outResult.Set(configString);

			// Find the property to resolve (if any).
			int i = 0;
			for (; i < outResult.Length - 2; i++)
			{
				if ((outResult[i] == '$') && (outResult[i + 1] == '('))
				{
					int parenPos = -1;
					int openCount = 1;
					bool inString = false;
					char8 prevC = 0;
					for (int checkIdx = i + 2; checkIdx < outResult.Length; checkIdx++)
					{
						char8 c = outResult[checkIdx];
						if (inString)
						{
							if (prevC == '\\')
							{
								// Slashed char
								prevC = 0;
								continue;
							}

							if (c == '"')
								inString = false;
						}
						else
						{
							if (c == '"')
								inString = true;
							else if (c == '(')
								openCount++;
							else if (c == ')')
							{
								openCount--;
								if (openCount == 0)
								{
									parenPos = checkIdx;
									break;
								}
							}
						}

						prevC = c;
					}

					if (parenPos != -1)
						ReplaceBlock:
							do
						{
							// If we reach here, a property has been found.
							String replaceStr = scope String(outResult, i + 2, parenPos - i - 2);

							// Ignore built-in properties because they are resolved elsewhere.
							if (BUILT_IN_PROPERTIES.Contains(replaceStr))
								continue;

							String newString = Get(replaceStr);

							// Resolve custom property value.
							if (newString != null)
							{
								outResult.Remove(i, parenPos - i + 1);
								outResult.Insert(i, newString);
								i--;
							}
						}
				}
			}
		}

		static public void ResolveWorkspaceProperties(Workspace workspace)
		{

		}

		static public void ResolveProjectProperties(Project project)
		{
			if (project == null)
				return;

			Project unresolvedProject = new Project();
			mProjects[project] = unresolvedProject;

			// Deep copy the project so unresolved custom build properties can be restored later,
			// and resolve any custom build properties in place in the existing project.
			if (project.mNamespace != null)
			{
				unresolvedProject.mNamespace.Set(project.mNamespace);
				ResolveString(unresolvedProject.mNamespace, project.mNamespace);
			}

			if (project.mProjectDir != null)
			{
				unresolvedProject.mProjectDir.Set(project.mProjectDir);
				ResolveString(unresolvedProject.mProjectDir, project.mProjectDir);
			}

			if (project.mProjectName != null)
			{
				unresolvedProject.mProjectName.Set(project.mProjectName);
				ResolveString(unresolvedProject.mProjectName, project.mProjectName);
			}

			if (project.mProjectPath != null)
			{
				unresolvedProject.mProjectPath.Set(project.mProjectPath);
				ResolveString(unresolvedProject.mProjectPath, project.mProjectPath);
			}

			if (project.mManagedInfo != null)
			{
				unresolvedProject.mManagedInfo = new Project.ManagedInfo();
				unresolvedProject.mManagedInfo.mVersion.mVersion.Set(project.mManagedInfo.mVersion.mVersion);
				unresolvedProject.mManagedInfo.mInfo.Set(project.mManagedInfo.mInfo);

				ResolveString(unresolvedProject.mManagedInfo.mVersion.mVersion, project.mManagedInfo.mVersion.mVersion);
				ResolveString(unresolvedProject.mManagedInfo.mInfo, project.mManagedInfo.mInfo);
			}

			if (project.mConfigs != null)
			{
				DeleteDictionaryAndKeysAndValues!(unresolvedProject.mConfigs);
				unresolvedProject.mConfigs = new Dictionary<String, Project.Config>();

				for (var config in project.mConfigs)
				{
					Project.Config unresolvedConfig = new Project.Config();

					for (var platform in config.value.mPlatforms)
					{
						Project.Options unresolvedOptions = platform.value.Duplicate();
						unresolvedConfig.mPlatforms[new String(platform.key)] = unresolvedOptions;

						ResolveString(unresolvedOptions.mBuildOptions.mTargetDirectory, platform.value.mBuildOptions.mTargetDirectory);
						ResolveString(unresolvedOptions.mBuildOptions.mTargetName, platform.value.mBuildOptions.mTargetName);
						ResolveString(unresolvedOptions.mBuildOptions.mOtherLinkFlags, platform.value.mBuildOptions.mOtherLinkFlags);

						for (int32 i = 0; i < unresolvedOptions.mBuildOptions.mLibPaths.Count; i++)
							ResolveString(unresolvedOptions.mBuildOptions.mLibPaths[i], platform.value.mBuildOptions.mLibPaths[i]);

						for (int32 i = 0; i < unresolvedOptions.mBuildOptions.mLinkDependencies.Count; i++)
							ResolveString(unresolvedOptions.mBuildOptions.mLinkDependencies[i], platform.value.mBuildOptions.mLinkDependencies[i]);

						for (int32 i = 0; i < unresolvedOptions.mBuildOptions.mPreBuildCmds.Count; i++)
							ResolveString(unresolvedOptions.mBuildOptions.mPreBuildCmds[i], platform.value.mBuildOptions.mPreBuildCmds[i]);
						
						for (int32 i = 0; i < unresolvedOptions.mBuildOptions.mPostBuildCmds.Count; i++)
							ResolveString(unresolvedOptions.mBuildOptions.mPostBuildCmds[i], platform.value.mBuildOptions.mPostBuildCmds[i]);

						for (int32 i = 0; i < unresolvedOptions.mBuildOptions.mCleanCmds.Count; i++)
							ResolveString(unresolvedOptions.mBuildOptions.mCleanCmds[i], platform.value.mBuildOptions.mCleanCmds[i]);

						for (int32 i = 0; i < unresolvedOptions.mBeefOptions.mPreprocessorMacros.Count; i++)
							ResolveString(unresolvedOptions.mBeefOptions.mPreprocessorMacros[i], platform.value.mBeefOptions.mPreprocessorMacros[i]);

						for (int32 i = 0; i < unresolvedOptions.mBeefOptions.mDistinctBuildOptions.Count; i++)
						{
							ResolveString(unresolvedOptions.mBeefOptions.mDistinctBuildOptions[i].mFilter, platform.value.mBeefOptions.mDistinctBuildOptions[i].mFilter);
							ResolveString(unresolvedOptions.mBeefOptions.mDistinctBuildOptions[i].mReflectMethodFilter, platform.value.mBeefOptions.mDistinctBuildOptions[i].mReflectMethodFilter);
						}

						ResolveString(unresolvedOptions.mCOptions.mOtherCFlags, platform.value.mCOptions.mOtherCFlags);
						ResolveString(unresolvedOptions.mCOptions.mOtherCPPFlags, platform.value.mCOptions.mOtherCPPFlags);
						
						for (int32 i = 0; i < unresolvedOptions.mCOptions.mIncludePaths.Count; i++)
							ResolveString(unresolvedOptions.mCOptions.mIncludePaths[i], platform.value.mCOptions.mIncludePaths[i]);

						for (int32 i = 0; i < unresolvedOptions.mCOptions.mPreprocessorMacros.Count; i++)
							ResolveString(unresolvedOptions.mCOptions.mPreprocessorMacros[i], platform.value.mCOptions.mPreprocessorMacros[i]);

						ResolveString(unresolvedOptions.mCOptions.mAddressSanitizer, platform.value.mCOptions.mAddressSanitizer);

						for (int32 i = 0; i < unresolvedOptions.mCOptions.mSpecificWarningsAsErrors.Count; i++)
							ResolveString(unresolvedOptions.mCOptions.mSpecificWarningsAsErrors[i], platform.value.mCOptions.mSpecificWarningsAsErrors[i]);

						for (int32 i = 0; i < unresolvedOptions.mCOptions.mDisableSpecificWarnings.Count; i++)
							ResolveString(unresolvedOptions.mCOptions.mDisableSpecificWarnings[i], platform.value.mCOptions.mDisableSpecificWarnings[i]);

						ResolveString(unresolvedOptions.mDebugOptions.mCommand, platform.value.mDebugOptions.mCommand);
						ResolveString(unresolvedOptions.mDebugOptions.mCommandArguments, platform.value.mDebugOptions.mCommandArguments);
						ResolveString(unresolvedOptions.mDebugOptions.mWorkingDirectory, platform.value.mDebugOptions.mWorkingDirectory);

						for (int32 i = 0; i < unresolvedOptions.mDebugOptions.mEnvironmentVars.Count; i++)
							ResolveString(unresolvedOptions.mDebugOptions.mEnvironmentVars[i], platform.value.mDebugOptions.mEnvironmentVars[i]);
					}

					unresolvedProject.mConfigs[new String(config.key)] = unresolvedConfig;
				}
			}

			if (project.mGeneralOptions != null)
			{
				unresolvedProject.mGeneralOptions.mProjectNameDecl.Set(project.mGeneralOptions.mProjectNameDecl);
				ResolveString(unresolvedProject.mGeneralOptions.mProjectNameDecl, project.mGeneralOptions.mProjectNameDecl);

				unresolvedProject.mGeneralOptions.mVersion.mVersion.Set(project.mGeneralOptions.mVersion.mVersion);
				ResolveString(unresolvedProject.mGeneralOptions.mVersion.mVersion, project.mGeneralOptions.mVersion.mVersion);

				unresolvedProject.mGeneralOptions.mAliases.Clear();
				for (String alias in project.mGeneralOptions.mAliases)
				{
					String unresolvedAlias = new String(alias);
					unresolvedProject.mGeneralOptions.mAliases.Add(unresolvedAlias);
					ResolveString(unresolvedAlias, alias);
				}
			}

			if (project.mBeefGlobalOptions != null)
			{
				unresolvedProject.mBeefGlobalOptions.mStartupObject.Set(project.mBeefGlobalOptions.mStartupObject);
				ResolveString(unresolvedProject.mBeefGlobalOptions.mStartupObject, project.mBeefGlobalOptions.mStartupObject);

				unresolvedProject.mBeefGlobalOptions.mDefaultNamespace.Set(project.mBeefGlobalOptions.mDefaultNamespace);
				ResolveString(unresolvedProject.mBeefGlobalOptions.mDefaultNamespace, project.mBeefGlobalOptions.mDefaultNamespace);

				unresolvedProject.mBeefGlobalOptions.mPreprocessorMacros.Clear();
				for (String macro in project.mBeefGlobalOptions.mPreprocessorMacros)
				{
					String unresolvedMacro = new String(macro);
					unresolvedProject.mBeefGlobalOptions.mPreprocessorMacros.Add(unresolvedMacro);
					ResolveString(unresolvedMacro, macro);
				}

				unresolvedProject.mBeefGlobalOptions.mDistinctBuildOptions.Clear();
				for (DistinctBuildOptions options in project.mBeefGlobalOptions.mDistinctBuildOptions)
				{
					DistinctBuildOptions unresolvedOptions = new DistinctBuildOptions();

					unresolvedOptions.mFilter.Set(options.mFilter);
					unresolvedOptions.mReflectMethodFilter.Set(options.mReflectMethodFilter);
					unresolvedProject.mBeefGlobalOptions.mDistinctBuildOptions.Add(unresolvedOptions);

					ResolveString(unresolvedOptions.mFilter, options.mFilter);
					ResolveString(unresolvedOptions.mReflectMethodFilter, options.mReflectMethodFilter);
				}
			}

			if (project.mWindowsOptions != null)
			{
				unresolvedProject.mWindowsOptions.mIconFile.Set(project.mWindowsOptions.mIconFile);
				unresolvedProject.mWindowsOptions.mManifestFile.Set(project.mWindowsOptions.mManifestFile);
				unresolvedProject.mWindowsOptions.mDescription.Set(project.mWindowsOptions.mDescription);
				unresolvedProject.mWindowsOptions.mComments.Set(project.mWindowsOptions.mComments);
				unresolvedProject.mWindowsOptions.mCompany.Set(project.mWindowsOptions.mCompany);
				unresolvedProject.mWindowsOptions.mProduct.Set(project.mWindowsOptions.mProduct);
				unresolvedProject.mWindowsOptions.mCopyright.Set(project.mWindowsOptions.mCopyright);
				unresolvedProject.mWindowsOptions.mFileVersion.Set(project.mWindowsOptions.mFileVersion);
				unresolvedProject.mWindowsOptions.mProductVersion.Set(project.mWindowsOptions.mProductVersion);

				ResolveString(unresolvedProject.mWindowsOptions.mIconFile, project.mWindowsOptions.mIconFile);
				ResolveString(unresolvedProject.mWindowsOptions.mManifestFile, project.mWindowsOptions.mManifestFile);
				ResolveString(unresolvedProject.mWindowsOptions.mDescription, project.mWindowsOptions.mDescription);
				ResolveString(unresolvedProject.mWindowsOptions.mComments, project.mWindowsOptions.mComments);
				ResolveString(unresolvedProject.mWindowsOptions.mCompany, project.mWindowsOptions.mCompany);
				ResolveString(unresolvedProject.mWindowsOptions.mProduct, project.mWindowsOptions.mProduct);
				ResolveString(unresolvedProject.mWindowsOptions.mCopyright, project.mWindowsOptions.mCopyright);
				ResolveString(unresolvedProject.mWindowsOptions.mFileVersion, project.mWindowsOptions.mFileVersion);
				ResolveString(unresolvedProject.mWindowsOptions.mProductVersion, project.mWindowsOptions.mProductVersion);
			}

			if (project.mLinuxOptions != null)
			{
				unresolvedProject.mLinuxOptions.mOptions.Set(project.mLinuxOptions.mOptions);
				ResolveString(unresolvedProject.mLinuxOptions.mOptions, project.mLinuxOptions.mOptions);
			}

			if (project.mDependencies != null)
			{
				unresolvedProject.mDependencies.Clear();
				for (Project.Dependency dependency in project.mDependencies)
				{
					Project.Dependency unresolvedDependency = new Project.Dependency();

					unresolvedDependency.mVerSpec = dependency.mVerSpec.Duplicate();

					if (dependency.mProjectName != null)
					{
						unresolvedDependency.mProjectName = new String();
						unresolvedDependency.mProjectName.Set(dependency.mProjectName);
						ResolveString(unresolvedDependency.mProjectName, dependency.mProjectName);
					}

					unresolvedProject.mDependencies.Add(unresolvedDependency);
				}
			}
		}

		static public void UnresolveProjectProperties(Project project)
		{
			if (project == null)
				return;

			if (!mProjects.ContainsKey(project))
				return;

			Project unresolvedProject = mProjects[project];

			// Unpack unresolved strings into the project.
			if (project.mNamespace != null)
				project.mNamespace.Set(unresolvedProject.mNamespace);

			if (project.mProjectDir != null)
				project.mProjectDir.Set(unresolvedProject.mProjectDir);

			if (project.mProjectName != null)
				project.mProjectName.Set(unresolvedProject.mProjectName);

			if (project.mProjectPath != null)
				project.mProjectPath.Set(unresolvedProject.mProjectPath);

			if (project.mManagedInfo != null)
			{
				project.mManagedInfo.mVersion.mVersion.Set(unresolvedProject.mManagedInfo.mVersion.mVersion);
				project.mManagedInfo.mInfo.Set(unresolvedProject.mManagedInfo.mInfo);
			}

			if (project.mConfigs != null)
			{
				for (var config in project.mConfigs)
				{
					Project.Config unresolvedConfig = null;

					for (var c in unresolvedProject.mConfigs)
					{
						if (config.key.Equals(c.key))
						{
							unresolvedConfig = c.value;
							break;
						}
					}

					if (unresolvedConfig == null)
						continue;

					for (var platform in config.value.mPlatforms)
					{
						Project.Options unresolvedOptions = null;

						for (var p in unresolvedConfig.mPlatforms)
						{
							if (platform.key.Equals(p.key))
							{
								unresolvedOptions = p.value;
								break;
							}
						}

						if (unresolvedOptions == null)
							continue;

						platform.value.mBuildOptions.mTargetDirectory.Set(unresolvedOptions.mBuildOptions.mTargetDirectory);
						platform.value.mBuildOptions.mTargetName.Set(unresolvedOptions.mBuildOptions.mTargetName);
						platform.value.mBuildOptions.mOtherLinkFlags.Set(unresolvedOptions.mBuildOptions.mOtherLinkFlags);

						for (int32 i = 0; i < platform.value.mBuildOptions.mLibPaths.Count; i++)
							platform.value.mBuildOptions.mLibPaths[i].Set(unresolvedOptions.mBuildOptions.mLibPaths[i]);

						for (int32 i = 0; i < platform.value.mBuildOptions.mLinkDependencies.Count; i++)
							platform.value.mBuildOptions.mLinkDependencies[i].Set(unresolvedOptions.mBuildOptions.mLinkDependencies[i]);

						for (int32 i = 0; i < platform.value.mBuildOptions.mPreBuildCmds.Count; i++)
							platform.value.mBuildOptions.mPreBuildCmds[i].Set(unresolvedOptions.mBuildOptions.mPreBuildCmds[i]);

						for (int32 i = 0; i < platform.value.mBuildOptions.mPostBuildCmds.Count; i++)
							platform.value.mBuildOptions.mPostBuildCmds[i].Set(unresolvedOptions.mBuildOptions.mPostBuildCmds[i]);

						for (int32 i = 0; i < platform.value.mBuildOptions.mCleanCmds.Count; i++)
							platform.value.mBuildOptions.mCleanCmds[i].Set(unresolvedOptions.mBuildOptions.mCleanCmds[i]);

						for (int32 i = 0; i < platform.value.mBeefOptions.mPreprocessorMacros.Count; i++)
							platform.value.mBeefOptions.mPreprocessorMacros[i].Set(unresolvedOptions.mBeefOptions.mPreprocessorMacros[i]);

						for (int32 i = 0; i < platform.value.mBeefOptions.mDistinctBuildOptions.Count; i++)
						{
							platform.value.mBeefOptions.mDistinctBuildOptions[i].mFilter.Set(unresolvedOptions.mBeefOptions.mDistinctBuildOptions[i].mFilter);
							platform.value.mBeefOptions.mDistinctBuildOptions[i].mReflectMethodFilter.Set(unresolvedOptions.mBeefOptions.mDistinctBuildOptions[i].mReflectMethodFilter);
						}

						platform.value.mCOptions.mOtherCFlags.Set(unresolvedOptions.mCOptions.mOtherCFlags);
						platform.value.mCOptions.mOtherCPPFlags.Set(unresolvedOptions.mCOptions.mOtherCPPFlags);

						for (int32 i = 0; i < platform.value.mCOptions.mIncludePaths.Count; i++)
							platform.value.mCOptions.mIncludePaths[i].Set(unresolvedOptions.mCOptions.mIncludePaths[i]);

						for (int32 i = 0; i < platform.value.mCOptions.mPreprocessorMacros.Count; i++)
							platform.value.mCOptions.mPreprocessorMacros[i].Set(unresolvedOptions.mCOptions.mPreprocessorMacros[i]);
						
						platform.value.mCOptions.mAddressSanitizer.Set(unresolvedOptions.mCOptions.mAddressSanitizer);

						for (int32 i = 0; i < platform.value.mCOptions.mSpecificWarningsAsErrors.Count; i++)
							platform.value.mCOptions.mSpecificWarningsAsErrors[i].Set(unresolvedOptions.mCOptions.mSpecificWarningsAsErrors[i]);

						for (int32 i = 0; i < platform.value.mCOptions.mDisableSpecificWarnings.Count; i++)
							platform.value.mCOptions.mDisableSpecificWarnings[i].Set(unresolvedOptions.mCOptions.mDisableSpecificWarnings[i]);

						platform.value.mDebugOptions.mCommand.Set(unresolvedOptions.mDebugOptions.mCommand);
						platform.value.mDebugOptions.mCommandArguments.Set(unresolvedOptions.mDebugOptions.mCommandArguments);
						platform.value.mDebugOptions.mWorkingDirectory.Set(unresolvedOptions.mDebugOptions.mWorkingDirectory);

						for (int32 i = 0; i < platform.value.mDebugOptions.mEnvironmentVars.Count; i++)
							platform.value.mDebugOptions.mEnvironmentVars[i].Set(unresolvedOptions.mDebugOptions.mEnvironmentVars[i]);
					}
				}
			}

			if (project.mGeneralOptions != null)
			{
				project.mGeneralOptions.mProjectNameDecl.Set(unresolvedProject.mGeneralOptions.mProjectNameDecl);
				project.mGeneralOptions.mVersion.mVersion.Set(unresolvedProject.mGeneralOptions.mVersion.mVersion);

				for (int32 i = 0; i < project.mGeneralOptions.mAliases.Count; i++)
					project.mGeneralOptions.mAliases[i].Set(unresolvedProject.mGeneralOptions.mAliases[i]);
			}

			if (project.mBeefGlobalOptions != null)
			{
				project.mBeefGlobalOptions.mStartupObject.Set(unresolvedProject.mBeefGlobalOptions.mStartupObject);
				project.mBeefGlobalOptions.mDefaultNamespace.Set(unresolvedProject.mBeefGlobalOptions.mDefaultNamespace);

				for (int32 i = 0; i < project.mBeefGlobalOptions.mPreprocessorMacros.Count; i++)
					project.mBeefGlobalOptions.mPreprocessorMacros[i].Set(unresolvedProject.mBeefGlobalOptions.mPreprocessorMacros[i]);

				for (int32 i = 0; i < project.mBeefGlobalOptions.mDistinctBuildOptions.Count; i++)
				{
					project.mBeefGlobalOptions.mDistinctBuildOptions[i].mFilter.Set(unresolvedProject.mBeefGlobalOptions.mDistinctBuildOptions[i].mFilter);
					project.mBeefGlobalOptions.mDistinctBuildOptions[i].mReflectMethodFilter.Set(unresolvedProject.mBeefGlobalOptions.mDistinctBuildOptions[i].mReflectMethodFilter);
				}
			}

			if (project.mWindowsOptions != null)
			{
				project.mWindowsOptions.mIconFile.Set(unresolvedProject.mWindowsOptions.mIconFile);
				project.mWindowsOptions.mManifestFile.Set(unresolvedProject.mWindowsOptions.mManifestFile);
				project.mWindowsOptions.mDescription.Set(unresolvedProject.mWindowsOptions.mDescription);
				project.mWindowsOptions.mComments.Set(unresolvedProject.mWindowsOptions.mComments);
				project.mWindowsOptions.mCompany.Set(unresolvedProject.mWindowsOptions.mCompany);
				project.mWindowsOptions.mProduct.Set(unresolvedProject.mWindowsOptions.mProduct);
				project.mWindowsOptions.mCopyright.Set(unresolvedProject.mWindowsOptions.mCopyright);
				project.mWindowsOptions.mFileVersion.Set(unresolvedProject.mWindowsOptions.mFileVersion);
				project.mWindowsOptions.mProductVersion.Set(unresolvedProject.mWindowsOptions.mProductVersion);
			}

			if (project.mLinuxOptions != null)
				project.mLinuxOptions.mOptions.Set(unresolvedProject.mLinuxOptions.mOptions);

			if (project.mDependencies != null)
			{
				for (int32 i = 0; i < project.mDependencies.Count; i++)
				{
					if (project.mDependencies[i].mProjectName != null)
						project.mDependencies[i].mProjectName.Set(unresolvedProject.mDependencies[i].mProjectName);
				}
			}

			// Clean up unresolved project.
			delete mProjects[project];
			mProjects.Remove(project);
		}
	}
}
