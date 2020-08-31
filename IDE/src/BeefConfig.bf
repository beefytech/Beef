using IDE.Util;
using System;
using System.Collections;
using System.IO;
using Beefy.utils;
using System.Threading;
using System.Diagnostics;

namespace IDE
{
	class BeefConfig
	{
		public class RegistryEntry
		{
			public String mProjName ~ delete _;
			public SemVer mVersion ~ delete _;
			public VerSpecRecord mLocation ~ delete _;
			public ConfigFile mConfigFile;

			public bool mParsedConfig;
		}

		public class Registry
		{
			public List<RegistryEntry> mEntries = new List<RegistryEntry>() ~ DeleteContainerAndItems!(_);
			public Monitor mMonitor = new .() ~ delete _;
			public WaitEvent mEvent = new .() ~ delete _;
			
			public ~this()
			{
				mEvent.WaitFor();
			}

			public void ParseConfig(RegistryEntry entry)
			{
				entry.mParsedConfig = true;

				if (entry.mLocation.mVerSpec case .Path(let path))
				{
					String configPath = scope String()..AppendF("{}/BeefProj.toml", path);
					StructuredData sd = scope .();
					if (sd.Load(configPath) case .Ok)
					{
						using (sd.Open("Project"))
						{
							var projName = scope String();
							sd.GetString("Name", projName);
							if (!projName.IsEmpty)
							{
								using (mMonitor.Enter())
									entry.mProjName.Set(projName);
							}
						}
					}
				}
			}

			public void WaitFor()
			{
				mEvent.WaitFor();
			}

			void Resolve()
			{
				// NOTE: We allow a race condition where ParseConfig can possibly occur on multiple threads
				// at the same time
				for (var entry in mEntries)
				{
					if (!entry.mParsedConfig)
						ParseConfig(entry);
				}

				mEvent.Set(true);
			}

			public void StartResolve()
			{
				var thread = new Thread(new => Resolve);
				thread.Start(true);
			}
		}

		public class LibDirectory
		{
			public String mPath ~ delete _;
			public ConfigFile mConfigFile;
		}

		public class ConfigFile
		{
			public String mFilePath  ~ delete _;
			public String mConfigDir ~ delete _;
		}

		List<ConfigFile> mConfigFiles = new List<ConfigFile>() ~ DeleteContainerAndItems!(_);
		public Registry mRegistry ~ delete _;
		List<String> mConfigPathQueue = new List<String>() ~ DeleteContainerAndItems!(_);
		List<LibDirectory> mLibDirectories = new List<LibDirectory>() ~ DeleteContainerAndItems!(_);
		List<FileSystemWatcher> mWatchers = new .() ~ DeleteContainerAndItems!(_);
		public bool mLibsChanged;

		void LibsChanged()
		{
			mLibsChanged = true;
		}

		Result<void> Load(StringView path)
		{
			let data = scope StructuredData();
			if (data.Load(path) case .Err)
				return .Err;

			let configFile = new ConfigFile();
			configFile.mFilePath = new String(path);

			configFile.mConfigDir = new String();
			Path.GetDirectoryPath(configFile.mFilePath, configFile.mConfigDir);

			for (let projName in data.Enumerate("Registry"))
			{
				RegistryEntry regEntry = new RegistryEntry();
				regEntry.mProjName = new String(projName);
				mRegistry.mEntries.Add(regEntry);
				
				regEntry.mConfigFile = configFile;

				var verString = scope String();
				data.GetString("Version", verString);
				regEntry.mVersion = new SemVer();
				regEntry.mVersion.Parse(verString).IgnoreError();

				regEntry.mLocation = new VerSpecRecord();
				using (data.Open("Location"))
					regEntry.mLocation.Parse(data).IgnoreError();
			}

			void AddFromLibraryPath(String absPath)
			{
				for (var entry in Directory.EnumerateDirectories(absPath))
				{
					String projName = scope .();
					entry.GetFileName(projName);
					
					String filePath = scope .();
					entry.GetFilePath(filePath);

					String projFilePath = scope .();
					projFilePath.Concat(filePath, "/BeefProj.toml");

					if (File.Exists(projFilePath))
					{
						RegistryEntry regEntry = new RegistryEntry();
						regEntry.mProjName = new String(projName);
						mRegistry.mEntries.Add(regEntry);

						regEntry.mConfigFile = configFile;

						regEntry.mVersion = new SemVer();
						regEntry.mVersion.Parse("0.0.0");

						regEntry.mLocation = new VerSpecRecord();
						regEntry.mLocation.SetPath(filePath);
					}
					else
					{
						AddFromLibraryPath(filePath);
					}
				}
			}

			for (data.Enumerate("UnversionedLibDirs"))
			{
				String dirStr = scope .();
				data.GetCurString(dirStr);

				if (!dirStr.IsWhiteSpace)
				{
					LibDirectory libDir = new .();
					libDir.mPath = new String(dirStr);
					libDir.mConfigFile = configFile;
					mLibDirectories.Add(libDir);

					String absPath = scope .();
					Path.GetAbsolutePath(libDir.mPath, configFile.mConfigDir, absPath);

					AddFromLibraryPath(absPath);
					absPath.Append(Path.DirectorySeparatorChar);

					FileSystemWatcher watcher = new FileSystemWatcher(absPath);
					watcher.OnChanged.Add(new (fileName) => LibsChanged());
					watcher.OnCreated.Add(new (fileName) => LibsChanged());
					watcher.OnDeleted.Add(new (fileName) => LibsChanged());
					watcher.OnRenamed.Add(new (newName, oldName) => LibsChanged());
					watcher.OnError.Add(new () => LibsChanged());
					watcher.StartRaisingEvents();
					mWatchers.Add(watcher);
				}
			}

			mConfigFiles.Add(configFile);

			return .Ok;
		}

		public void Refresh()
		{
			Load().IgnoreError();
		}

		public void QueuePaths(StringView topLevelDir)
		{
			let dir = scope String(topLevelDir);
			if (!dir.EndsWith(IDEUtils.cNativeSlash))
				dir.Append(IDEUtils.cNativeSlash);

			while (true)
			{
				let path = scope String(dir);
				path.Append("BeefConfig.toml");

				if (File.Exists(path))
				{
					if (mConfigPathQueue.Contains(path))
						break; // We have this and everything under it already
					mConfigPathQueue.Add(new String(path));
				}

				// We had logic to check parent directories, but this seems unsound.  Revisit this decision when we have
				//  better usage cases in mind.
				break;

				/*if (dir.Length < 2)
					break;
				int slashPos = dir.LastIndexOf(IDEUtils.cNativeSlash, dir.Length - 2);
				if (slashPos == -1)
					break;

				dir.RemoveToEnd(slashPos + 1);*/
			}
		}

		public Result<void> Load()
		{
			mLibsChanged = false;
			delete mRegistry;
			mRegistry = new Registry();
			ClearAndDeleteItems(mConfigFiles);
			ClearAndDeleteItems(mWatchers);

			for (int i = mConfigPathQueue.Count - 1; i >= 0; i--)
			{
				let path = mConfigPathQueue[i];
				Try!(Load(path));
			}
			mRegistry.StartResolve();
			return .Ok;
		}
	}
}
