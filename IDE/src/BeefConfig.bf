using IDE.Util;
using System;
using System.Collections.Generic;
using System.IO;
using Beefy.utils;

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
		public List<RegistryEntry> mRegistry = new List<RegistryEntry>() ~ DeleteContainerAndItems!(_);
		List<String> mConfigPathQueue = new List<String>() ~ DeleteContainerAndItems!(_);
		List<LibDirectory> mLibDirectories = new List<LibDirectory>() ~ DeleteContainerAndItems!(_);

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
				mRegistry.Add(regEntry);
				
				regEntry.mConfigFile = configFile;

				var verString = scope String();
				data.GetString("Version", verString);
				regEntry.mVersion = new SemVer();
				regEntry.mVersion.Parse(verString).IgnoreError();

				regEntry.mLocation = new VerSpecRecord();
				using (data.Open("Location"))
					regEntry.mLocation.Parse(data).IgnoreError();
			}

			for (data.Enumerate("LibDirs"))
			{
				String dirStr = scope .();
				data.GetCurString(dirStr);

				if (!dirStr.IsWhiteSpace)
				{
					LibDirectory libDir = new .();
					libDir.mPath = new String(dirStr);
					libDir.mConfigFile = configFile;
					mLibDirectories.Add(libDir);
				}
			}

			mConfigFiles.Add(configFile);

			return .Ok;
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
						break; // We have this and everthing under it already
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
			for (int i = mConfigPathQueue.Count - 1; i >= 0; i--)
			{
				let path = mConfigPathQueue[i];
				Try!(Load(path));
			}
			return .Ok;
		}
	}
}
