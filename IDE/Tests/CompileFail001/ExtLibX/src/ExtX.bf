namespace IDETest
{
	// ExtLibX and ExtLibY do not reference each other, so these two overrides have no defined
	//  order - neither can declare itself as overriding the other with 'new override'
	extension ExtConflictBase
	{
		public override int GetV() //FAIL Conflicting extension override
		{
			return 2;
		}
	}
}
