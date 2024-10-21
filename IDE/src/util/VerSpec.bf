using System;
using Beefy.utils;
using IDE;
using Beefy;

namespace IDE.Util
{
	enum VerSpec
	{
		case None;
		case SemVer(SemVer ver);
		case Path(String path);
		case Git(String url, SemVer ver);

		public void Dispose() mut
		{
			switch (this)
			{
			case .None:
			case .SemVer(let ver):
				delete ver;
			case .Path(let path):
				delete path;
			case .Git(let url, let ver):
				delete url;
				delete ver;
			}
			this = .None;
		}

		public VerSpec Duplicate()
		{
			switch (this)
			{
			case .None:
				return .None;
			case .SemVer(let ver):
				return .SemVer(new SemVer(ver));
			case .Path(let path):
				return .Path(new String(path));
			case .Git(let url, let ver):
				if (ver == null)
					return .Git(new String(url), null);
				return .Git(new String(url), new SemVer(ver));
			}
		}

		public Result<void> Parse(StructuredData data) mut
		{
			Dispose();
			if (data.IsObject)
			{
				for (var valName in data.Enumerate())
				{
					if (valName == "Path")
					{
						var pathStr = new String();
						data.GetCurString(pathStr);
						this = .Path(pathStr);
					}
					else if (valName == "Git")
					{
						var pathStr = new String();
						data.GetCurString(pathStr);
						this = .Git(pathStr, null);
					}
					else if (valName == "Version")
					{
						if (this case .Git(var url, var prevVer))
						{
							if (prevVer == null)
							{
								var pathStr = new String();
								data.GetCurString(pathStr);
								SemVer semVer = new SemVer();
								semVer.mVersion = pathStr;
								this = .Git(url, semVer);
							}
						}
						else
						{
							var pathStr = new String();
							data.GetCurString(pathStr);
							SemVer semVer = new SemVer();
							semVer.mVersion = pathStr;
							this = .SemVer(semVer);
						}
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
				this = .SemVer(semVer);

				Try!(semVer.Parse(verString));
			}

			return .Ok;
		}

		public void Serialize(String name, StructuredData data)
		{
			switch (this)
			{
			case .None:
			case .Git(var path, var ver):
				using (data.CreateObject(name))
				{
					data.Add("Git", path);
					if ((ver != null) && (!ver.mVersion.IsEmpty))
						data.Add("Version", ver.mVersion);
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

	class VerSpecRecord
	{
		public VerSpec mVerSpec;
		public Object mVerObject;
		public Object mVerObject2;

		public ~this()
		{
			/*switch (mVerSpec)
			{
			case .SemVer(let ver): delete ver;
			case .Path(let path): delete path;
			case .Git(let url): delete url;
			}*/
			delete mVerObject;
			delete mVerObject2;
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
						mVerSpec = .Git(pathStr, null);
					}
					else if (valName == "Version")
					{
						if (mVerSpec case .Git(var url, var prevVer))
						{
							if (prevVer == null)
							{
								var pathStr = new String();
								data.GetCurString(pathStr);
								SemVer semVer = new SemVer();
								semVer.mVersion = pathStr;
								mVerSpec = .Git(url, semVer);
								mVerObject2 = semVer;
							}
						}
						else
						{
							var pathStr = new String();
							data.GetCurString(pathStr);
							SemVer semVer = new SemVer();
							semVer.mVersion = pathStr;
							mVerSpec = .SemVer(semVer);
							mVerObject = semVer;
						}
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
			case .None:
			case .Git(var path, var ver):
				using (data.CreateObject(name))
				{
					data.Add("Git", path);
					if (ver != null)
						data.Add("Version", ver.mVersion);
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
