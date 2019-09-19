namespace System
{
	class OperatingSystem
	{
		public Version Version;
		public PlatformID Platform = PlatformID.Win32NT;

		public this()
		{
			Version.Major = 5; //TODO:
		}
	}
}
