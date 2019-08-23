using System;
using Beefy.utils;
using IDE;
using Beefy;

namespace IDE.Util
{
	enum VerSpec
	{
		case SemVer(SemVer ver);
		case Path(String path);
		case Git(String url);
	}

	class VerSpecRecord
	{
		public VerSpec mVerSpec;
		public Object mVerObject;

		public ~this()
		{
			/*switch (mVerSpec)
			{
			case .SemVer(let ver): delete ver;
			case .Path(let path): delete path;
			case .Git(let url): delete url;
			}*/
			delete mVerObject;
		}

		public void SetPath(StringView path)
		{
			delete mVerObject;
			String pathStr = new String(path);
			mVerObject = pathStr;

			mVerSpec = .Path(pathStr);
		}

		public void SetSemVer(StringView ver)
		{
			delete mVerObject;
			String pathStr = new String(ver);

			SemVer semVer = new SemVer();
			semVer.mVersion = pathStr;
			mVerObject = semVer;

			mVerSpec = .SemVer(semVer);
		}

		public Result<void> Parse(StructuredData data)
		{
			if (data.IsObject)
			{
				for (var valName in data.Enumerate())
				{
					if (valName == "Path")
					{
						var pathStr = new String();
						data.GetCurString(pathStr);
						mVerObject = pathStr;
						mVerSpec = .Path(pathStr);
					}
					else if (valName == "Git")
					{
						var pathStr = new String();
						data.GetCurString(pathStr);
						mVerObject = pathStr;
						mVerSpec = .Git(pathStr);
					}
					else if (valName == "Ver")
					{
						var pathStr = new String();
						data.GetCurString(pathStr);

						SemVer semVer = new SemVer();
						mVerObject = semVer;

						semVer.mVersion = pathStr;
						mVerSpec = .SemVer(semVer);
					}
					else
					{
						//gApp.Fail("Invalid ver path");
						return .Err;
					}
				}
			}
			else
			{
				let verString = scope String();
				data.GetCurString(verString);
				let semVer = new SemVer();
				mVerSpec = .SemVer(semVer);

				mVerObject = semVer;

				Try!(semVer.Parse(verString));
			}

			return .Ok;
		}

		public void Serialize(String name, StructuredData data)
		{
			switch (mVerSpec)
			{
			case .Git(var path):
				using (data.CreateObject(name))
				{
					data.Add("Git", path);
				}
			case .SemVer(var ver):
				data.Add(name, ver.mVersion);
			case .Path(var path):
				using (data.CreateObject(name))
				{
					data.Add("Path", path);
				}
			}
		}
	}
}
