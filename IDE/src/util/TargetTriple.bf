using System;

namespace IDE.Util
{
	class TargetTriple
	{
		public static bool IsTargetTriple(StringView str)
		{
			int dashCount = 0;
			for (let c in str.RawChars)
				if (c == '-')
					dashCount++;
			return dashCount >= 2;
		}

		public static Workspace.PlatformType GetPlatformType(StringView str)
		{
			var str;

			// Remove version from the end
			while (!str.IsEmpty)
			{
				char8 c = str[str.Length - 1];
				if ((c.IsDigit) || (c == '.'))
					str.RemoveFromEnd(1);
				else
					break;
			}

			bool hasLinux = false;

			for (let elem in str.Split('-'))
			{
				switch (elem)
				{
				case "linux":
					hasLinux = true;
				case "windows":
					return .Windows;
				case "macosx":
					return .macOS;
				case "ios":
					return .iOS;
				case "android",
					 "androideabi":
					return .Android;
				}
			}

			if (hasLinux)
				return .Linux;
			return .Unknown;
		}
	}
}
