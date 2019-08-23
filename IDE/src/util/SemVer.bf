using System;

namespace IDE.Util
{
	class SemVer
	{
		public String mVersion ~ delete _;

		public Result<void> Parse(StringView ver)
		{
			mVersion = new String(ver);
			return .Ok;
		}
	}
}
