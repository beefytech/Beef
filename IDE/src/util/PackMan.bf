#pragma warning disable 168

using System;
using IDE.Util;
using System.Collections;
using System.Security.Cryptography;
using System.IO;
using Beefy.utils;
using System.Threading;

namespace IDE.util
{
	class PackMan
	{
		public class WorkItem
		{
			public enum Kind
			{
				None,
				FindVersion,
				Clone,
				Checkout,
				Setup
			}

			public Kind mKind;
			public String mProjectName ~ delete _;
			public String mURL ~ delete _;
			public List<String> mConstraints ~ DeleteContainerAndItems!(_);
			public String mTag ~ delete _;
			public String mHash ~ delete _;
			public String mPath ~ delete _;
			public GitManager.GitInstance mGitInstance ~ _?.ReleaseRef();
			public IDEApp.ExecutionInstance mExecInstance ~ _?.Release();

			public ~this()
			{
				mGitInstance?.Cancel();
			}
		}

		public List<WorkItem> mWorkItems = new .() ~ DeleteContainerAndItems!(_);
		public bool mInitialized;
		public String mManagedPath ~ delete _;
		public bool mFailed;
		public HashSet<String> mCleanHashSet = new .() ~ DeleteContainerAndItems!(_);

		public void Fail(StringView error)
		{
			gApp.OutputErrorLine(error);

			if (!mFailed)
			{
				mFailed = true;
				gApp.[Friend]FlushDeferredLoadProjects();
			}
		}

		public bool IsPathManaged(StringView path)
		{
			if (path.IsEmpty)
				return false;
			if (String.IsNullOrEmpty(mManagedPath))
				return false;
			if (path.Length < mManagedPath.Length)
				return false;
			return Path.Equals(mManagedPath, path.Substring(0, mManagedPath.Length));
		}

		public bool GetManagedHash(StringView path, String outHash)
		{
			StringView subView = path.Substring(mManagedPath.Length + 1);
			if (subView.Length < 40)
				return false;
			outHash.Append(subView.Substring(0, 40));
			return true;
		}

		public bool CheckInit()
		{
			if ((gApp.mWorkspace.mProjectLoadState != .Preparing) && (mWorkItems.IsEmpty))
			{
				// Clear failed state
				mFailed = false;
			}

			if (mInitialized)
				return true;

			if (gApp.mBeefConfig.mManagedLibPath.IsEmpty)
				return false;

			mManagedPath = new .(gApp.mBeefConfig.mManagedLibPath);
			mInitialized = true;
			return true;
		}

		public void GetPath(StringView url, StringView hash, String outPath)
		{
			//var urlHash = SHA256.Hash(url.ToRawData()).ToString(.. scope .());
			//outPath.AppendF($"{mManagedPath}/{urlHash}/{hash}");
			outPath.AppendF($"{mManagedPath}/{hash}");
		}

		bool WantsHashClean(StringView hash)
		{
			if (mCleanHashSet.ContainsAlt(hash))
				return true;
			if (mCleanHashSet.Contains("*"))
				return true;
			return false;
		}

		public bool CheckLock(StringView projectName, String outPath, out bool failed)
		{
			failed = false;

			if (!CheckInit())
				return false;

			if (gApp.mWantUpdateVersionLocks != null)
			{
				if ((gApp.mWantUpdateVersionLocks.IsEmpty) || (gApp.mWantUpdateVersionLocks.ContainsAlt(projectName)))
					return false;
			}

			if (!gApp.mWorkspace.mProjectLockMap.TryGetAlt(projectName, ?, var lock))
				return false;

			switch (lock)
			{
			case .Git(let url, let tag, let hash):
				if (WantsHashClean(hash))
					return false;
				var path = GetPath(url, hash, .. scope .());
				var managedFilePath = scope $"{path}/BeefManaged.toml";
				if (File.Exists(managedFilePath))
				{
					StructuredData sd = scope .();
					sd.Load(managedFilePath).IgnoreError();
					outPath.Append(path);
					outPath.Append("/BeefProj.toml");

					if (!sd.GetBool("Setup"))
						gApp.OutputErrorLine(scope $"Project '{projectName}' previous failed setup. Clean managed cache to try again.");
					return true;
				}
			default:
			}

			return false;
		}

		public void CloneCompleted(StringView projectName, StringView url, StringView tag, StringView hash, StringView path, bool writeFile, bool setupComplete)
		{
			if (mCleanHashSet.GetAndRemoveAlt(hash) case .Ok(let val))
				delete val;

			gApp.mWorkspace.SetLock(projectName, .Git(new .(url), new .(tag), new .(hash)));

			if (writeFile)
			{
				StructuredData sd = scope .();
				sd.CreateNew();
				sd.Add("FileVersion", 1);
				sd.Add("Version", tag);
				sd.Add("GitURL", url);
				sd.Add("GitTag", tag);
				sd.Add("GitHash", hash);
				sd.Add("Setup", setupComplete);
				var tomlText = sd.ToTOML(.. scope .());
				var managedFilePath = scope $"{path}/BeefManaged.toml";
				File.WriteAllText(managedFilePath, tomlText).IgnoreError();
			}
		}

		public void RunSetupProject(StringView projectName, StringView url, StringView tag, StringView hash, StringView path)
		{
			if (!CheckInit())
				return;

			String beefBuildPath = scope $"{gApp.mInstallDir}BeefBuild.exe";
			String args = scope $"-run";
			var execInst = gApp.DoRun(beefBuildPath, args, path, .None);
			execInst?.mAutoDelete = false;
			execInst?.mSmartOutput = true;

			WorkItem workItem = new .();
			workItem.mKind = .Setup;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			workItem.mTag = new .(tag);
			workItem.mHash = new .(hash);
			workItem.mPath = new .(path);
			workItem.mExecInstance = execInst;
			mWorkItems.Add(workItem);

			/*cmd.mDoneEvent.Add(new (success) =>
				{
					int a = 123;
				});*/
		}

		public void DeleteDir(StringView path)
		{
			String tempDir;

			if (path.Contains("__DELETE__"))
			{
				tempDir = new .(path);
			}
			else
			{
				tempDir = new $"{path}__DELETE__{(int32)Internal.GetTickCountMicro():X}";
				if (Directory.Move(path, tempDir) case .Err)
				{
					delete tempDir;
					Fail(scope $"Failed to remove directory '{path}'");
					return;
				}
			}

			ThreadPool.QueueUserWorkItem(new () =>
				{
					Directory.DelTree(tempDir);
				}
				~
				{
					delete tempDir;
				});
		}

		public void GetWithHash(StringView projectName, StringView url, StringView tag, StringView hash)
		{
			if (!CheckInit())
				return;

			String destPath = GetPath(url, hash, .. scope .());
			var urlPath = Path.GetDirectoryPath(destPath, .. scope .());
			Directory.CreateDirectory(urlPath).IgnoreError();
			if (Directory.Exists(destPath))
			{
				if (!WantsHashClean(hash))
				{
					var managedFilePath = scope $"{destPath}/BeefManaged.toml";
					if (File.Exists(managedFilePath))
					{
						if (gApp.mVerbosity >= .Normal)
						{
							if (tag.IsEmpty)
								gApp.OutputLine($"Git selecting library '{projectName}' at {hash.Substring(0, 7)}");
							else
								gApp.OutputLine($"Git selecting library '{projectName}' tag '{tag}' at {hash.Substring(0, 7)}");
						}

						CloneCompleted(projectName, url, tag, hash, destPath, false, false);
						ProjectReady(projectName, destPath);
						return;
					}
				}

				DeleteDir(destPath);
			}

			if (gApp.mVerbosity >= .Normal)
			{
				if (tag.IsEmpty)
					gApp.OutputLine($"Git cloning library '{projectName}' at {hash.Substring(0, 7)}...");
				else
					gApp.OutputLine($"Git cloning library '{projectName}' tag '{tag}' at {hash.Substring(0, 7)}");
			}

			WorkItem workItem = new .();
			workItem.mKind = .Clone;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			workItem.mTag = new .(tag);
			workItem.mHash = new .(hash);
			workItem.mPath = new .(destPath);
			mWorkItems.Add(workItem);
		}

		public void GetWithVersion(StringView projectName, StringView url, SemVer semVer)
		{
			if (!CheckInit())
				return;

			bool ignoreLock = false;
			if (gApp.mWantUpdateVersionLocks != null)
			{
				if ((gApp.mWantUpdateVersionLocks.IsEmpty) || (gApp.mWantUpdateVersionLocks.ContainsAlt(projectName)))
					ignoreLock = true;
			}

			if ((!ignoreLock) && (gApp.mWorkspace.mProjectLockMap.TryGetAlt(projectName, ?, var lock)))
			{
				switch (lock)
				{
				case .Git(let checkURL, let tag, let hash):
					if (checkURL == url)
						GetWithHash(projectName, url, tag, hash);
					return;
				default:
				}
			}

			if (gApp.mVerbosity >= .Normal)
				gApp.OutputLine($"Git retrieving version list for '{projectName}'");

			WorkItem workItem = new .();
			workItem.mKind = .FindVersion;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			if (!semVer.IsEmpty)
				workItem.mConstraints = new .() { new String(semVer.mVersion) };
			mWorkItems.Add(workItem);
		}

		public void UpdateGitConstraint(StringView url, SemVer semVer)
		{
			for (var workItem in mWorkItems)
			{
				if ((workItem.mKind == .FindVersion) && (workItem.mURL == url))
				{
					if (workItem.mConstraints == null)
						workItem.mConstraints = new .();
					workItem.mConstraints.Add(new String(semVer.mVersion));
				}
			}
		}

		public void Checkout(StringView projectName, StringView url, StringView path, StringView tag, StringView hash)
		{
			if (!CheckInit())
				return;

			WorkItem workItem = new .();
			workItem.mKind = .Checkout;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			workItem.mTag = new .(tag);
			workItem.mHash = new .(hash);
			workItem.mPath = new .(path);
			mWorkItems.Add(workItem);
		}

		public void ProjectReady(StringView projectName, StringView path)
		{
			if (var project = gApp.mWorkspace.FindProject(projectName))
			{
				String projectPath = scope $"{path}/BeefProj.toml";

				project.mProjectPath.Set(projectPath);
				gApp.RetryProjectLoad(project, false);
			}
		}

		public void Update()
		{
			bool executingGit = false;

			// First handle active git items
			for (var workItem in mWorkItems)
			{
				bool removeItem = false;
				if (workItem.mGitInstance == null)
				{
					switch (workItem.mKind)
					{
					case .Setup:
						if ((workItem.mExecInstance == null) || (workItem.mExecInstance.mDone))
						{
							bool success = workItem.mExecInstance?.mExitCode == 0;
							String projPath = Path.GetAbsolutePath("../", workItem.mPath, .. scope .());
							CloneCompleted(workItem.mProjectName, workItem.mURL, workItem.mTag, workItem.mHash, projPath, true, success);
							if (success)
								ProjectReady(workItem.mProjectName, projPath);
							else
								gApp.OutputErrorLine(scope $"Failed to setup project '{workItem.mProjectName}' located at '{projPath}'");
							removeItem = true;
						}
					default:
					}
				}
				else if (!workItem.mGitInstance.mDone)
				{
					executingGit = true;
				}
				else if (!workItem.mGitInstance.mFailed)
				{
					switch (workItem.mKind)
					{
					case .FindVersion:
						gApp.CompilerLog("");

						StringView bestTag = default;
						StringView bestHash = default;

						for (var tag in workItem.mGitInstance.mTagInfos)
						{
							if ((tag.mTag == "HEAD") &&
								((workItem.mConstraints == null) || (workItem.mConstraints.Contains("HEAD"))))
							{
								bestHash = tag.mHash;
								break;
							}
							else if (workItem.mConstraints != null)
							{
								bool hasMatch = false;
								for (var constraint in workItem.mConstraints)
								{
									if (SemVer.IsVersionMatch(tag.mTag, constraint))
									{
										hasMatch = true;
										break;
									}
								}

								if (hasMatch)
								{
									if ((bestTag.IsEmpty) || (SemVer.Compare(tag.mTag, bestTag) > 0))
									{
										bestTag = tag.mTag;
										bestHash = tag.mHash;
									}
								}
							}
						}

						if (bestHash != default)
						{
							GetWithHash(workItem.mProjectName, workItem.mURL, bestTag, bestHash);
						}
						else
						{
							String constraints = scope .();
							for (var constraint in workItem.mConstraints)
							{
								if (!constraints.IsEmpty)
									constraints.Append(", ");
								constraints.Append('\'');
								constraints.Append(constraint);
								constraints.Append('\'');
							}

							Fail(scope $"Failed to locate version for '{workItem.mProjectName}' with constraints {constraints}");
						}
					case .Clone:
						Checkout(workItem.mProjectName, workItem.mURL, workItem.mPath, workItem.mTag, workItem.mHash);
					case .Checkout:
						if (gApp.mVerbosity >= .Normal)
							gApp.OutputLine($"Git cloning library '{workItem.mProjectName}' done.");

						String setupPath = scope $"{workItem.mPath}/Setup";

						/*if (workItem.mProjectName == "BeefProj0")
						{
							setupPath.Set("C:/proj/BeefProj0/Setup");
						}*/

						if (Directory.Exists(setupPath))
						{
							RunSetupProject(workItem.mProjectName, workItem.mURL, workItem.mTag, workItem.mHash, setupPath);
						}
						else
						{
							CloneCompleted(workItem.mProjectName, workItem.mURL, workItem.mTag, workItem.mHash, workItem.mPath, true, true);
							ProjectReady(workItem.mProjectName, workItem.mPath);
						}
					default:
					}
					removeItem = true;
				}
				else
				{
					Fail(scope $"Failed to retrieve project '{workItem.mProjectName}' at '{workItem.mURL}'");
					removeItem = true;
				}

				if (removeItem)
				{
					@workItem.Remove();
					delete workItem;
				}
			}

			if (!executingGit)
			{
				// First handle active git items
				for (var workItem in mWorkItems)
				{
					if (workItem.mGitInstance != null)
						continue;

					switch (workItem.mKind)
					{
					case .FindVersion:
						workItem.mGitInstance = gApp.mGitManager.GetTags(workItem.mURL)..AddRef();
					case .Checkout:
						workItem.mGitInstance = gApp.mGitManager.Checkout(workItem.mPath, workItem.mHash)..AddRef();
					case .Clone:
						workItem.mGitInstance = gApp.mGitManager.Clone(workItem.mURL, workItem.mPath)..AddRef();
					default:
					}
				}
			}
		}

		public void GetHashFromFilePath(StringView filePath, String path)
		{
			if (mManagedPath == null)
				return;

			if (!filePath.StartsWith(mManagedPath))
				return;

			StringView hashPart = filePath.Substring(mManagedPath.Length);
			if (hashPart.Length < 42)
				return;

			hashPart.RemoveFromStart(1);
			hashPart.Length = 40;
			path.Append(hashPart);
		}

		public void CancelAll()
		{
			if (mWorkItems.IsEmpty)
				return;

			Fail("Aborted project transfer");
			mWorkItems.ClearAndDeleteItems();
		}

		public void CleanCache()
		{
			if (!CheckInit())
				return;

			if (mManagedPath.IsEmpty)
				return;

			for (var entry in Directory.EnumerateDirectories(mManagedPath))
			{
				if (!entry.IsDirectory)
					continue;

				var fileName = entry.GetFileName(.. scope .());
				if (fileName.Length < 40)
					continue;

				var filePath = entry.GetFilePath(.. scope .());
				DeleteDir(filePath);
			}
		}
	}
}
