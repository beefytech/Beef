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
			// "none" may be the vendor (aarch64-none-linux-android) as well as the OS of a
			// bare-metal triple (thumbv7em-none-eabi), so it names the platform only when no other
			// part of the triple does.
			bool hasNone = false;

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
				case "wasm32",
					 "wasm64":
					return .Wasm;
				case "none":
					hasNone = true;
				}
			}

			if (hasLinux)
				return .Linux;
			if (hasNone)
				return .None;
			return .Unknown;
		}
	}
}
