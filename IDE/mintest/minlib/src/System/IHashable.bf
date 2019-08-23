namespace System
{
	interface IHashable
	{
		int GetHashCode();
	}
	
    /*extension IHashable where Self : class
	{

	}*/

	static class HashHelper
	{
		public static int GetHashCode<T>(T val) where T : class
		{
			return (int)(void*)(val);
		}

		public static int GetHashCode<T>(T val) where T : IHashable
		{
			return val.GetHashCode();
		}
	}
}
