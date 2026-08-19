#pragma warning disable 168

namespace IDETest
{
	class HotSwap_ExtensionOverride
	{
		public class ClassA
		{
			public virtual int MethodA()
			{
				return 10;
			}
		}

		public class ClassB : ClassA
		{
			public override int MethodA()
			{
				// This is a devirtualized 'base' call. Once the extension below adds an override for
				//  ClassA.MethodA it has to start resolving to that override instead.
				return base.MethodA() + 1;
			}
		}

		/*ExtClassA_MethodA
		public extension ClassA
		{
			public override int MethodA()
			{
				return 100;
			}
		}
		*/

		static void DoTest()
		{
			ClassA ca = scope ClassA();
			ClassB cb = scope ClassB();

			//HotSwap_ExtensionOverride_Start
			int a = ca.MethodA();
			int b = cb.MethodA();
		}

		public static void Test()
		{
			DoTest();
			DoTest();
		}
	}
}
