namespace System
{
	public class EventArgs
	{
		public static readonly EventArgs Empty = new EventArgs() ~ delete _;

		public this()
		{
		}
	}
}
