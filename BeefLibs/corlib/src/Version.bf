namespace System
{
	struct Version
	{
		public uint32 Major;
		public uint32 Minor;
		public uint32 Build = 0;
		public uint32 Revision = 0;

		public this(uint32 major, uint32 minor)
		{
			Major = major;
			Minor = minor;
		}

		public this(uint32 major, uint32 minor, uint32 build, uint32 revision)
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

		[Inline]
		public bool Check(uint32 major)
		{
			return Major == major;
		}

		[Inline]
		public bool Check(uint32 major, uint32 minor)
		{
			return (Major > major) || ((Major == major) && (Minor >= minor));
		}

		[Inline]
		public bool Check(uint32 major, uint32 minor, uint32 build)
		{
			return (Major > major) || ((Major == major) && (Minor > minor)) ||
				((Major == major) && (Minor == minor) && (Build >= build));
		}

		[Inline]
		public bool Check(uint32 major, uint32 minor, uint32 build, uint32 revision)
		{
			return (Major > major) || ((Major == major) && (Minor > minor)) ||
				((Major == major) && (Minor == minor) && (Build > build)) ||
				((Major == major) && (Minor == minor) && (Build == build) && (Revision >= revision));
		}
	}
}
