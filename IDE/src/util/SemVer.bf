using System;

namespace IDE.Util
{
	class SemVer
	{
		public String mVersion ~ delete _;

		public this()
		{

		}

		public this(SemVer semVer)
		{
			if (semVer.mVersion != null)
				mVersion = new String(semVer.mVersion);
		}

		public this(StringView version)
		{
			mVersion = new String(version);
		}

		public Result<void> Parse(StringView ver)
		{
			mVersion = new String(ver);
			return .Ok;
		}
	}
}
