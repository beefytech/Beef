using System;

#pragma warning disable 168

namespace IDETest
{
	class HotSwap_StringLiterals
	{
		// A separate module lets the literal become unused when its call is removed.
		class StringSource
		{
			public static String GetValue() => "hot-swap string";
		}

		static int GetLength()
		{
			int length = 0;
			/*UseString
			length = StringSource.GetValue().Length;
			*/
			return length;
		}

		public static void Test()
		{
			//StringPool_Start
			int length = GetLength();
		}
	}
}
