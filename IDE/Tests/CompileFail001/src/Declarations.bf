namespace IDETest
{
	class Declarations
	{
		class ClassA : InnerA //FAIL
		{
			class InnerA
			{
			}
		}

		class ClassB : InnerB.Zop //FAIL
		{
			class InnerB
			{
			}
		}

		class ClassC : InnerC.Zop //FAIL
		{
			class InnerC
			{
				public class Zop
				{
				}
			}
		}

		public class ClassD
		{
			public int mA;
			public int mB;
		}

		public struct StructA
		{
			public int32 mA;
			public int32 mB;
		}

		public struct StructB
		{
			ClassD parent;
			StructA mSA;
			int mInnerInt;

			public this(ClassD test)
			{
				parent = test;
				mInnerInt = parent.mA;

				mSA.mA = 123;
				int a = mSA.mA;
				int b = mSA.mB; //FAIL
				mSA.mB = 234;
			}
		}
	}
}
