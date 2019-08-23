#pragma warning disable 168

namespace IDETest
{
	enum EnumA
	{
		case A(int a);
		case B(float b);
	}

	enum EnumB
	{
		case A;
		case B;
		case C;
	}	

	class EnumTester
	{
		public static void Test()
		{
			EnumA ea = .B(1.2f);

			let z = (aa:123, bb:345);
			var q = (a:234, 567, c:999);

			var qRef = ref q;

			EnumB eb = .B;

			//EnumTester_Test
		}
	}
}
