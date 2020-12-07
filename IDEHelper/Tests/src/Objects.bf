using System;

namespace Tests
{
	class Objects
	{
		class ClassA
		{
			public int mA = 1;

			this
			{
				mA *= 11;
			}

			public this()
			{
				mA += 100;
			}

			public virtual void MethodA()
			{

			}
		}

		class ClassB : ClassA
		{
			public override void MethodA()
 			{
				 base.MethodA();
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			Test.Assert(ca.mA == 111);
		}
	}
}
