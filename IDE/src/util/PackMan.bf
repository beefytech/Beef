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
				CloneShallow,
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
		HashSet<String> mCleanedHashSet = new .() ~ DeleteContainerAndItems!(_);

		public void Fail(StringView error)
		{
			gApp.OutputErrorLine(error);

			if (!mFailed)
			{
				mFailed = true;
				gApp.[Friend]FlushDeferredLoadProjects();
			}
		}

		/// True when 'path' lies inside a commit directory of the managed cache.
		public bool IsPathManaged(StringView path)
		{
			return GetManagedHash(path, scope .());
		}

		/// Extracts the commit hash directory from a path inside the managed cache.
		/// Normalizes first: Path.Equals asserts in debug builds on forward slashes.
		public bool GetManagedHash(StringView path, String outHash)
		{
			if (String.IsNullOrEmpty(mManagedPath))
				return false;

			String normalizedPath = scope .(path);
			IDEUtils.FixFilePath(normalizedPath);
			if (normalizedPath.Length <= mManagedPath.Length)
				return false;
			if (!Path.Equals(mManagedPath, normalizedPath.Substring(0, mManagedPath.Length)))
				return false;
			if (normalizedPath[mManagedPath.Length] != IDEUtils.cNativeSlash)
				return false;

			// The first directory below the managed cache root is the commit hash.
			StringView hashDirectory = normalizedPath.Substring(mManagedPath.Length + 1);
			int separatorIdx = hashDirectory.IndexOf(IDEUtils.cNativeSlash);
			if (separatorIdx != -1)
				hashDirectory = hashDirectory.Substring(0, separatorIdx);

			// Git SHA-1 commit hashes contain exactly 40 hexadecimal digits.
			const int gitHashLength = 40;
			if (hashDirectory.Length != gitHashLength)
				return false;
			for (let c in hashDirectory.RawChars)
			{
				if (!IDEUtils.IsHexDigit(c))
					return false;
			}

			outHash.Append(hashDirectory);
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
			IDEUtils.FixFilePath(mManagedPath);
			while ((mManagedPath.Length > 1) && (mManagedPath.EndsWith(IDEUtils.cNativeSlash)))
				mManagedPath.RemoveFromEnd(1);
			mInitialized = true;
			return true;
		}

		/// Every dependency at the same commit shares one clone directory, whatever its ?path= selects.
		public void GetClonePath(StringView hash, String outPath)
		{
			outPath.AppendF($"{mManagedPath}/{hash}");
			IDEUtils.FixFilePath(outPath);
		}

		/// Directory of the selected project inside the clone, or the clone root when projectSubPath is empty.
		void GetProjectPath(StringView clonePath, StringView projectSubPath, String outPath)
		{
			outPath.Append(clonePath);

			if (!projectSubPath.IsEmpty)
			{
				outPath.Append('/');
				outPath.Append(projectSubPath);
			}
			IDEUtils.FixFilePath(outPath);
		}

		/// Splits a dependency URL into the repository URL handed to Git and the normalized project subfolder.
		/// Reports a malformed URL through Fail, so load-time callers use IDEUtils.ParseGitProjectURL directly.
		bool ParseProjectURL(StringView url, String outRepoURL, String outSubPath)
		{
			if (IDEUtils.ParseGitProjectURL(url, outRepoURL, outSubPath))
				return true;
			Fail(scope $"Invalid git project path in '{url}'");
			return false;
		}

		bool CheckProjectPath(StringView projectName, StringView url, StringView projectPath, bool requireProjectFile, String outError)
		{
			if (!Directory.Exists(projectPath))
			{
				outError.AppendF($"Git project '{projectName}' at '{url}' does not contain subfolder '{projectPath}'");
				return false;
			}

			if (requireProjectFile)
			{
				String projectFilePath = scope $"{projectPath}/BeefProj.toml";
				if (!File.Exists(projectFilePath))
				{
					outError.AppendF($"Git project '{projectName}' at '{url}' does not contain BeefProj.toml at '{projectPath}'");
					return false;
				}
			}

			return true;
		}

		bool IsClonePendingForHash(StringView hash, WorkItem ignoreItem)
		{
			// The clone directory is a pure function of the commit hash,
			// so matching hashes is equivalent to matching clone paths.
			for (var workItem in mWorkItems)
			{
				if (workItem == ignoreItem)
					continue;
				if ((workItem.mHash == null) || (workItem.mHash != hash))
					continue;

				switch (workItem.mKind)
				{
				case .Checkout, .Setup:
					return true;
				case .Clone, .CloneShallow:
					if (workItem.mGitInstance != null)
						return true;
				default:
				}
			}

			return false;
		}

		/// Initializes one project after the clone for 'hash' exists on disk, running its Setup if needed.
		void CompleteProjectFromClone(StringView projectName, StringView url, StringView repoURL, StringView subPath, StringView tag, StringView hash)
		{
			String clonePath = GetClonePath(hash, .. scope .());
			String projectPath = GetProjectPath(clonePath, subPath, .. scope .());

			StructuredData sd = scope .();
			if (sd.Load(scope $"{clonePath}/BeefManaged.toml") case .Ok)
			{
				if (FindProjectEntry(sd, subPath, var setup))
				{
					if (!setup)
						Fail(scope $"Project '{projectName}' previously failed setup. Clean managed cache to try again.");
					else
					{
						String pathError = scope .();
						if (!CheckProjectPath(projectName, url, projectPath, true, pathError))
							Fail(pathError);
						else
						{
							CloneCompleted(projectName, url, repoURL, subPath, tag, hash, false, true);
							ProjectReady(projectName, projectPath);
						}
					}
					return;
				}
			}

			// No setup ran, so record nothing and let the next load re-check the folder
			String pathError = scope .();
			if (!CheckProjectPath(projectName, url, projectPath, false, pathError))
			{
				Fail(pathError);
				return;
			}

			String setupPath = scope $"{projectPath}/Setup";
			if (Directory.Exists(setupPath))
			{
				RunSetupProject(projectName, url, tag, hash, setupPath);
				return;
			}

			if (!CheckProjectPath(projectName, url, projectPath, true, pathError))
			{
				Fail(pathError);
				return;
			}
			if (CloneCompleted(projectName, url, repoURL, subPath, tag, hash, true, true))
				ProjectReady(projectName, projectPath);
		}

		bool WantsHashClean(StringView hash)
		{
			if (mCleanHashSet.ContainsAlt(hash))
				return true;
			if ((mCleanHashSet.Contains("*")) && (!mCleanedHashSet.ContainsAlt(hash)))
				return true;
			return false;
		}

		/// Looks up the recorded result for one project. Version 1 manifests predate ?path= and
		/// describe only the root; version 2 carries one [[Projects]] entry per subfolder, "." for the root.
		bool FindProjectEntry(StructuredData sd, StringView subPath, out bool outSetup)
		{
			outSetup = false;
			if (sd.GetInt("FileVersion") == 1)
			{
				outSetup = sd.GetBool("Setup");
				return (subPath.IsEmpty) && (sd.Get("Setup") != null);
			}
			if (sd.GetInt("FileVersion") != 2)
				return false;

			StringView entryPath = subPath.IsEmpty ? StringView(".") : subPath;
			// Case sensitivity is a property of the file system, not the OS, and Windows 10+ can even set it
			// per directory: https://learn.microsoft.com/en-us/windows/wsl/case-sensitivity
			let fsCaseMode = Environment.IsFileSystemCaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
			for (sd.Enumerate("Projects"))
			{
				String path = scope .();
				sd.GetString("Path", path);
				if (path.Equals(entryPath, fsCaseMode))
				{
					outSetup = sd.GetBool("Setup");
					return true;
				}
			}
			return false;
		}

		public bool TryGetManagedInfoPath(StringView projectDir, String outManifestPath)
		{
			String hash = scope .();
			if (!GetManagedHash(projectDir, hash))
			{
				var localPath = scope $"{projectDir}/BeefManaged.toml";
				if (!File.Exists(localPath))
					return false;
				outManifestPath.Append(localPath);
				return true;
			}

			String cloneRoot = scope $"{mManagedPath}/{hash}";
			String subPath = scope .();
			if (projectDir.Length > cloneRoot.Length)
				subPath.Append(projectDir.Substring(cloneRoot.Length + 1));
			if (!IDEUtils.NormalizeGitProjectSubPath(subPath))
				return false;

			var manifestPath = scope $"{cloneRoot}/BeefManaged.toml";
			StructuredData sd = scope .();
			if (sd.Load(manifestPath) case .Err)
				return false;
			if (!FindProjectEntry(sd, subPath, var setup))
				return false;
			outManifestPath.Append(manifestPath);
			return true;
		}

		void WriteProjectEntry(StructuredData sd, StringView path, bool setup)
		{
			using (sd.CreateObject())
			{
				sd.Add("Path", path.IsEmpty ? StringView(".") : path);
				sd.Add("Setup", setup);
			}
		}

		/// In a shared clone the repository-level Version/GitTag fields describe whichever project wrote
		/// last. Per-project state lives in [[Projects]]; Project.Load overrides the displayed version
		/// from the workspace lock.
		bool WriteManifestFile(StringView clonePath, StructuredData sd)
		{
			var manifestPath = scope $"{clonePath}/BeefManaged.toml";
			if (File.WriteAllText(manifestPath, sd.ToTOML(.. scope .())) case .Err)
			{
				Fail(scope $"Failed to write managed library metadata at '{manifestPath}'");
				return false;
			}
			return true;
		}

		/// A manifest with no project entries marks a completed checkout awaiting setup,
		/// so an interrupted first setup reuses the clone instead of fetching it again.
		bool WriteCheckoutMarker(StringView repoURL, StringView tag, StringView hash, StringView clonePath)
		{
			StructuredData sd = scope .();
			sd.CreateNew();
			sd.Add("FileVersion", 2);
			sd.Add("Version", tag);
			sd.Add("GitURL", repoURL);
			sd.Add("GitTag", tag);
			sd.Add("GitHash", hash);
			using (sd.CreateArray("Projects"))
			{
			}

			return WriteManifestFile(clonePath, sd);
		}

		/// Records the initialization result for one project, preserving the entries of every other
		/// project in this clone.
		bool WriteProjectResult(StringView repoURL, StringView subPath, StringView tag, StringView hash, StringView clonePath, bool setupComplete)
		{
			var manifestPath = scope $"{clonePath}/BeefManaged.toml";
			StructuredData oldSd = scope .();
			oldSd.Load(manifestPath).IgnoreError();

			// Mirror the root entry for older readers, which only understand the
			// top-level Setup key. They still cannot resolve subfolder dependencies.
			bool hasRootSetup = FindProjectEntry(oldSd, "", var rootSetup);
			if (subPath.IsEmpty)
			{
				hasRootSetup = true;
				rootSetup = setupComplete;
			}

			StructuredData sd = scope .();
			sd.CreateNew();
			sd.Add("FileVersion", 2);
			sd.Add("Version", tag);
			sd.Add("GitURL", repoURL);
			sd.Add("GitTag", tag);
			sd.Add("GitHash", hash);
			if (hasRootSetup)
				sd.Add("Setup", rootSetup);
			using (sd.CreateArray("Projects"))
			{
				// Preserve the migrated v1 root result when recording a subfolder.
				// A root result replaces it with the new value below.
				if ((oldSd.GetInt("FileVersion") == 1) && (hasRootSetup) && (!subPath.IsEmpty))
					WriteProjectEntry(sd, ".", rootSetup);
				if (oldSd.GetInt("FileVersion") == 2)
				{
					StringView entryPath = subPath.IsEmpty ? StringView(".") : subPath;
					let fsCaseMode = Environment.IsFileSystemCaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
					for (oldSd.Enumerate("Projects"))
					{
						String path = scope .();
						oldSd.GetString("Path", path);
						if (path.Equals(entryPath, fsCaseMode))
							continue;
						WriteProjectEntry(sd, path, oldSd.GetBool("Setup"));
					}
				}
				WriteProjectEntry(sd, subPath, setupComplete);
			}

			return WriteManifestFile(clonePath, sd);
		}

		/// Fast path for a locked dependency whose commit is already cached and initialized.
		/// Sets 'failed' and outError when the lock cannot be used, so the caller reports it
		/// instead of this method failing synchronously inside the project loop.
		public bool CheckLock(StringView projectName, StringView projectURL, String outPath, String outError, out bool failed)
		{
			failed = false;
			outError.Clear();

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
				if (url != projectURL)
					return false;

				String projectSubPath = scope .();
				{
					String repoURL = scope .();
					if (!IDEUtils.ParseGitProjectURL(url, repoURL, projectSubPath))
					{
						outError.AppendF($"Invalid git project path in '{url}'");
						failed = true;
						return false;
					}
				}
				if (WantsHashClean(hash))
					return false;

				var clonePath = GetClonePath(hash, .. scope .());
				var rootManagedFilePath = scope $"{clonePath}/BeefManaged.toml";
				if (!File.Exists(rootManagedFilePath))
					return false;

				String projectPath = scope .();
				GetProjectPath(clonePath, projectSubPath, projectPath);

				StructuredData sd = scope .();
				if (sd.Load(rootManagedFilePath) case .Err)
					return false;

				bool entrySetup;
				if (!FindProjectEntry(sd, projectSubPath, out entrySetup))
					return false;

				outPath.Append(projectPath);
				outPath.Append("/BeefProj.toml");

				if (!entrySetup)
				{
					outError.AppendF($"Project '{projectName}' previously failed setup. Clean managed cache to try again.");
					failed = true;
					return false;
				}
				if (!CheckProjectPath(projectName, url, projectPath, true, outError))
				{
					failed = true;
					return false;
				}
				return true;
			default:
			}

			return false;
		}

		/// Locks the project to this commit and, when writeFile is set, records its result in the clone's manifest.
		bool CloneCompleted(StringView projectName, StringView url, StringView repoURL, StringView subPath, StringView tag, StringView hash, bool writeFile, bool setupComplete)
		{
			if (writeFile)
			{
				String clonePath = GetClonePath(hash, .. scope .());
				if (!WriteProjectResult(repoURL, subPath, tag, hash, clonePath, setupComplete))
					return false;
			}
			gApp.mWorkspace.SetLock(projectName, .Git(new .(url), new .(tag), new .(hash)));
			return true;
		}

		public void RunSetupProject(StringView projectName, StringView url, StringView tag, StringView hash, StringView path)
		{
			if (!CheckInit())
				return;

#if BF_PLATFORM_WINDOWS
			let ext = ".exe";
#else
			let ext = "";
#endif
			String beefBuildPath = scope $"{gApp.mInstallDir}BeefBuild{ext}";
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
			IDEUtils.FixFilePath(workItem.mPath);
			workItem.mExecInstance = execInst;
			mWorkItems.Add(workItem);
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

		/// Called only after GetWithVersion has validated the URL.
		void GetWithHash(StringView projectName, StringView url, StringView tag, StringView hash)
		{
			if (!CheckInit())
				return;

			String destPath = GetClonePath(hash, .. scope .());

			bool hasShallowClone = false;
			if (GitManager.GetGitVersion() case .Ok(let ver))
				hasShallowClone = (ver.Major > 2) || ((ver.Major == 2) && (ver.Minor >= 49));

			WorkItem workItem = new .();
			workItem.mKind = hasShallowClone ? .CloneShallow : .Clone;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			workItem.mTag = new .(tag);
			workItem.mHash = new .(hash);
			workItem.mPath = new .(destPath);
			mWorkItems.Add(workItem);
		}

		public Result<void> GetWithVersion(StringView projectName, StringView url, SemVer semVer, String outError)
		{
			if (!CheckInit())
				return .Err;
			{
				String repoURL = scope .();
				String projectSubPath = scope .();
				if (!IDEUtils.ParseGitProjectURL(url, repoURL, projectSubPath))
				{
					outError.AppendF($"Invalid git project path in '{url}'");
					return .Err;
				}
			}

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
					{
						GetWithHash(projectName, url, tag, hash);
						return .Ok;
					}
				default:
				}
			}

			if (gApp.mVerbosity >= .Normal)
				gApp.OutputLine($"Git retrieving version list for '{projectName}'");

			WorkItem workItem = new .();
			workItem.mKind = .FindVersion;
			workItem.mProjectName = new .(projectName);
			workItem.mURL = new .(url);
			if (semVer?.IsEmpty == false)
				workItem.mConstraints = new .() { new String(semVer.mVersion) };
			mWorkItems.Add(workItem);
			return .Ok;
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
							removeItem = true;

							String repoURL = scope .();
							String subPath = scope .();
							if (!ParseProjectURL(workItem.mURL, repoURL, subPath))
								break;

							String clonePath = GetClonePath(workItem.mHash, .. scope .());
							String projPath = GetProjectPath(clonePath, subPath, .. scope .());
							bool success = workItem.mExecInstance?.mExitCode == 0;
							String setupError = scope .();
							if (success)
								success = CheckProjectPath(workItem.mProjectName, workItem.mURL, projPath, true, setupError);
							else
								setupError.AppendF($"Failed to setup project '{workItem.mProjectName}' located at '{projPath}'");
							// Record the result and lock before Fail can flush deferred loads.
							if (CloneCompleted(workItem.mProjectName, workItem.mURL, repoURL, subPath, workItem.mTag, workItem.mHash, true, success))
							{
								if (success)
									ProjectReady(workItem.mProjectName, projPath);
								else
									Fail(setupError);
							}
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
					case .Checkout, .CloneShallow:
						if (gApp.mVerbosity >= .Normal)
							gApp.OutputLine($"Git cloning library '{workItem.mProjectName}' done.");

						String repoURL = scope .();
						String subPath = scope .();
						if ((ParseProjectURL(workItem.mURL, repoURL, subPath)) &&
							(WriteCheckoutMarker(repoURL, workItem.mTag, workItem.mHash, workItem.mPath)))
						{
							CompleteProjectFromClone(workItem.mProjectName, workItem.mURL, repoURL, subPath, workItem.mTag, workItem.mHash);
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
				// Start queued work after active Git operations finish.
				for (var workItem in mWorkItems)
				{
					if (workItem.mGitInstance != null)
						continue;

					// After any failure, drop queued work that has not started so one broken
					// dependency does not start a cascade of new clones. An already-running
					// Setup still completes and records its own result above.
					if ((mFailed) && (workItem.mKind != .Setup))
					{
						@workItem.Remove();
						delete workItem;
						continue;
					}
					bool removeItem = false;
					switch (workItem.mKind)
					{
					case .FindVersion:
						String repoURL = scope .();
						if (ParseProjectURL(workItem.mURL, repoURL, scope .()))
							workItem.mGitInstance = gApp.mGitManager.GetTags(repoURL)..AddRef();
						else
							removeItem = true;
					case .Checkout:
						workItem.mGitInstance = gApp.mGitManager.Checkout(workItem.mPath, workItem.mHash)..AddRef();
					case .Clone, .CloneShallow:
						// Several dependencies can select the same commit. Only one clones it;
						// the others wait here until that clone and any setup in it finish.
						if (IsClonePendingForHash(workItem.mHash, workItem))
							continue;

						String repoURL = scope .();
						String subPath = scope .();
						if (!ParseProjectURL(workItem.mURL, repoURL, subPath))
						{
							removeItem = true;
							break;
						}

						String manifestPath = scope $"{workItem.mPath}/BeefManaged.toml";
						bool canReuseClone = false;
						if (!WantsHashClean(workItem.mHash))
						{
							StructuredData sd = scope .();
							if (sd.Load(manifestPath) case .Ok)
							{
								int fileVersion = sd.GetInt("FileVersion");
								canReuseClone = (fileVersion == 1) || (fileVersion == 2);
								if ((!canReuseClone) && (gApp.mVerbosity >= .Normal))
									gApp.OutputLine($"Unsupported managed metadata (FileVersion {fileVersion}) at '{manifestPath}', rebuilding");
							}
							else if ((File.Exists(manifestPath)) && (gApp.mVerbosity >= .Normal))
								gApp.OutputLine($"Unreadable managed metadata at '{manifestPath}', rebuilding");
						}

						if (canReuseClone)
						{
							CompleteProjectFromClone(workItem.mProjectName, workItem.mURL, repoURL, subPath, workItem.mTag, workItem.mHash);
							removeItem = true;
						}
						else
						{
							// No usable metadata: the directory is missing, an interrupted checkout,
							// explicitly cleaned, or written in a format this build does not read.
							if (Directory.Exists(workItem.mPath))
								DeleteDir(workItem.mPath);
							if (mFailed)
								continue;
							if (mCleanHashSet.GetAndRemoveAlt(workItem.mHash) case .Ok(let val))
								delete val;
							if (mCleanedHashSet.TryAddAlt(workItem.mHash, var entryPtr))
								*entryPtr = new .(workItem.mHash);
							Directory.CreateDirectory(mManagedPath).IgnoreError();
							if (gApp.mVerbosity >= .Normal)
								gApp.OutputLine($"Git cloning library '{workItem.mProjectName}' at {workItem.mHash.Substring(0, 7)}...");
							if (workItem.mKind == .CloneShallow)
								workItem.mGitInstance = gApp.mGitManager.CloneShallow(repoURL, workItem.mPath, workItem.mHash)..AddRef();
							else
								workItem.mGitInstance = gApp.mGitManager.Clone(repoURL, workItem.mPath)..AddRef();
						}

					default:
					}

					if (removeItem)
					{
						@workItem.Remove();
						delete workItem;
					}
				}
			}
		}

		public void GetHashFromFilePath(StringView filePath, String path)
		{
			GetManagedHash(filePath, path);
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
