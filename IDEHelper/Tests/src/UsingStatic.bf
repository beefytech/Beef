#pragma warning disable 168

using static Tests.USOuter;
using static Tests.USOuter.USInnerC<int>;

namespace Tests
{
	class USOuter
	{
		public static int sVal0 = 123;

		public class USInnerA
		{
			public class USInnerB<T>
			{

			}
		}

		public class USInnerC<T>
		{
			public class USInnerD
			{

			}

			public class USInnerE<T2>
			{

			}
		}
	}

	class UsingStatic
	{
		public static void TestBasics()
		{
			USInnerA innerA;
			USInnerA.USInnerB<int> innerB;
			int val = sVal0;
			USInnerD id;
			USInnerE<float> ie;
		}
	}
}
