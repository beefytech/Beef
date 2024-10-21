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
				Checkout
			}

			public Kind mKind;
			public String mProjectName ~ delete _;
			public String mURL ~ delete _;
			public List<String> mConstraints ~ DeleteContainerAndItems!(_);
			public String mTag ~ delete _;
			public String mHash ~ delete _;
			public String mPath ~ delete _;
			public GitManager.GitInstance mGitInstance ~ _?.ReleaseRef();

			public ~this()
			{
				mGitInstance?.Cancel();
			}
		}

		public List<WorkItem> mWorkItems = new .() ~ DeleteContainerAndItems!(_);
		public bool mInitialized;
		public String mManagedPath ~ delete _;
		public bool mFailed;

		public void Fail(StringView error)
		{
			gApp.OutputErrorLine(error);

			if (!mFailed)
			{
				mFailed = true;
				gApp.[Friend]FlushDeferredLoadProjects();
			}
		}

		public bool CheckInit()
		{
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

		public bool CheckLock(StringView projectName, String outPath)
		{
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
				var path = GetPath(url, hash, .. scope .());
				var managedFilePath = scope $"{path}/BeefManaged.toml";
				if (File.Exists(managedFilePath))
				{
					outPath.Append(path);
					outPath.Append("/BeefProj.toml");
					return true;
				}
			default:
			}

			return false;
		}

		public void CloneCompleted(StringView projectName, StringView url, StringView tag, StringView hash, StringView path)
		{
			gApp.mWorkspace.SetLock(projectName, .Git(new .(url), new .(tag), new .(hash)));

			StructuredData sd = scope .();
			sd.CreateNew();
			sd.Add("FileVersion", 1);
			sd.Add("Version", tag);
			sd.Add("GitURL", url);
			sd.Add("GitTag", tag);
			sd.Add("GitHash", hash);
			var tomlText = sd.ToTOML(.. scope .());
			var managedFilePath = scope $"{path}/BeefManaged.toml";
			File.WriteAllText(managedFilePath, tomlText).IgnoreError();
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

					CloneCompleted(projectName, url, tag, hash, destPath);
					ProjectReady(projectName, destPath);
					return;
				}

				String tempDir = new $"{destPath}__{(int32)Internal.GetTickCountMicro():X}";

				//if (Directory.DelTree(destPath) case .Err)
				if (Directory.Move(destPath, tempDir) case .Err)
				{
					delete tempDir;
					Fail(scope $"Failed to remove directory '{destPath}'");
					return;
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
			if (semVer != null)
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
				if (workItem.mGitInstance == null)
					continue;

				if (!workItem.mGitInstance.mDone)
				{
					executingGit = true;
					continue;
				}

				if (!workItem.mGitInstance.mFailed)
				{
					switch (workItem.mKind)
					{
					case .FindVersion:
						gApp.CompilerLog("");

						StringView bestTag = default;
						StringView bestHash = default;

						for (var tag in workItem.mGitInstance.mTagInfos)
						{
							if ((tag.mTag == "HEAD") && (workItem.mConstraints == null))
								bestHash = tag.mHash;
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

							Fail(scope $"Failed to locate version for '{workItem.mProjectName}' with constraints '{constraints}'");
						}
					case .Clone:
						Checkout(workItem.mProjectName, workItem.mURL, workItem.mPath, workItem.mTag, workItem.mHash);
					case .Checkout:
						CloneCompleted(workItem.mProjectName, workItem.mURL, workItem.mTag, workItem.mHash, workItem.mPath);
						ProjectReady(workItem.mProjectName, workItem.mPath);

						if (gApp.mVerbosity >= .Normal)
							gApp.OutputLine($"Git cloning library '{workItem.mProjectName}' done.");
					default:
					}
				}

				@workItem.Remove();
				delete workItem;
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
	}
}
