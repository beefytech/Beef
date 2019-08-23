namespace System
{
	struct Version
	{
		public int32 Major;
		public int32 Minor;
		public int32 Build = -1;
		public int32 Revision = -1;

		public this(int32 major, int32 minor)
		{
			Major = major;
			Minor = minor;
		}

		public this(int32 major, int32 minor, int32 build, int32 revision)
		{
			Major = major;
			Minor = minor;
			Build = build;
			Revision = revision;
		}

		public static int operator<=>(Version lhs, Version rhs)
		{
			if (lhs.Major != rhs.Major)
			    if (lhs.Major > rhs.Major)
			        return 1;
			    else
			        return -1;

			if (lhs.Minor != rhs.Minor)
			    if (lhs.Minor > rhs.Minor)
			        return 1;
			    else
			        return -1;

			if (lhs.Build != rhs.Build)
			    if (lhs.Build > rhs.Build)
			        return 1;
			    else
			        return -1;

			if (lhs.Revision != rhs.Revision)
			    if (lhs.Revision > rhs.Revision)
			        return 1;
			    else
			        return -1;

			return 0;
		}
	}
}
