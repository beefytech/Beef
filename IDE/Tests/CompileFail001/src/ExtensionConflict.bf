namespace IDETest
{
	// ExtConflictBase is declared in ExtLibBase and overridden by extensions in both ExtLibX and
	//  ExtLibY, which do not reference each other. Their order is undefined, so the two overrides
	//  collide - see ExtLibX/src/ExtX.bf and ExtLibY/src/ExtY.bf
	class ExtensionConflict
	{
		public static int Use()
		{
			ExtConflictBase b = scope ExtConflictBase();
			return b.GetV();
		}
	}
}
