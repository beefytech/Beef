using System.Collections.Generic;
using System;
using Beefy;
using System.IO;
using System.Diagnostics;
using System.Threading;

namespace IDE
{
	class CompositeFile
	{
		public static String sMainFileName = "@main.bf";

		public Monitor mMonitor = new .() ~ delete _;
		static String sVersionStr = "///@singleFileVersion ";
		static String sSectionStr = "///@embed ";
		public Dictionary<String, String> mData = new .() ~ DeleteDictionyAndKeysAndItems!(_);
		public String mFilePath ~ delete _;
		public bool mPendingSave;
		public bool mHasChanges;

		public this(StringView filePath)
		{
			Debug.Assert(mFilePath == null);
			mFilePath = new String(filePath);
		}

		public ~this()
		{
			if (mHasChanges)
				Save();
		}

		public bool HasUnsavedChanges
		{
			get
			{
				return mHasChanges;
			}
		}

		public bool HasPendingSave
		{
			get
			{
				return mPendingSave;
			}
		}

		public StringView FilePath
		{
			get
			{
				return mFilePath;
			}
		}

		public bool IsIncluded(StringView name)
		{
			return name == sMainFileName;
		}

		public Result<void> Load()
		{
			String source = scope String();
			if (File.ReadAllText(mFilePath, source, false) case .Err)
				return .Err;

			bool isMain = true;
			String embedName = scope .(sMainFileName);
			String content = scope .();

			for (let line in source.Split('\n'))
			{
				if (line.StartsWith(sSectionStr))
				{
					Set(embedName, content);
					embedName.Clear();
					content.Clear();
					embedName.Append(line, sSectionStr.Length);
					isMain = false;

				}
				else if (line.StartsWith(sVersionStr))
				{
					// Ignore
					int version = int.Parse(StringView(line, sVersionStr.Length)).GetValueOrDefault();
					if (version != 1)
					{
						gApp.OutputLineSmart("ERROR: File '{0}' specifies invalid file version '{1}'. Beef upgrade required?", mFilePath, version);
						return .Err;
					}
				}
				else if (isMain)
				{
					if (!content.IsEmpty)
						content.Append("\n");
					content.Append(line);
				}
				else if (line.StartsWith("/// "))
				{
					if (!content.IsEmpty)
						content.Append("\n");
					content.Append(line, 4);
				}
			}
			Set(embedName, content);
			mHasChanges = false;
			mPendingSave = false;
			return .Ok;
		}

		public bool Get(StringView name, String outData)
		{
			using (mMonitor.Enter())
			{
				if (mData.TryGetValue(scope String()..Append(name), var value))
				{
					outData.Append(value);
					return true;
				}
				return false;
			}
		}

		public void Set(StringView name, StringView data)
		{
			using (mMonitor.Enter())
			{
				bool added = mData.TryAdd(scope String()..Append(name), var keyPtr, var valuePtr);
				if (added)
				{
					mHasChanges = true;
					mPendingSave = true;
					*keyPtr = new String(name);
					*valuePtr = new String(data);
				}
				else
				{
					if (*valuePtr != data)
					{
						mHasChanges = true;
						mPendingSave = true;
						(*valuePtr).Set(data);
					}
				}
			}
		}

		public bool Save()
		{
			using (mMonitor.Enter())
			{
				mPendingSave = false;
				if (!mHasChanges)
					return true;

				String str = scope String();

				void Add(StringView name)
				{
					String sectionStr = scope String();
					Get(name, sectionStr);
					if (name == sMainFileName)
					{
						for (let line in sectionStr.Split('\n'))
						{
							if (@line.Pos != 0)
								str.Append("\n");
							str.Append(line);
						}
						return;
					}

					if (!sectionStr.IsEmpty)
					{
						str.Append("\n");
						str.Append(sSectionStr);
						str.Append(name);
						str.Append("\n");

						for (let line in sectionStr.Split('\n'))
						{
							str.Append("/// ");
							str.Append(line);
							str.Append("\n");
						}
					}
				}

				Add(sMainFileName);
				str.Append("\n", sVersionStr, "1\n");
				Add("Workspace");
				Add("Project");

				if (!gApp.SafeWriteTextFile(mFilePath, str))
					return false;
				mHasChanges = true;
			}
			return true;
		}
	}
}
