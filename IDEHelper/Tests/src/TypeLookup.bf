#pragma warning disable 168

using System.Collections;

namespace Tests
{
	class ClassE
	{
		public int mGlobal_E;
	}

	class TypeLookup
	{
		class ClassA
		{
			public int mA;
		}

		class ClassB
		{
			private class ClassA
			{
				public int mClassB_A2;

				public void Hey()
				{
					ClassC cc = scope .();
					cc.mClassB_C = 1;
				}
			}

			public class ClassC
			{
				public int mClassB_C;

				public void Hey()
				{
					ClassA ca = scope .();
					ca.mClassB_A2 = 1;
				}
			}
		}

		class ClassC
		{
			public int mC;
		}

		private class ClassE
		{
			public int mE;
		}

		class ClassD : ClassB
		{
			public void Hey()
			{
				// Shouldn't be able to find ClassB.ClassB since it's private
				ClassA ca = scope .();
				ca.mA = 1;

				// We should find ClassB.ClassC since it's public
				ClassC cc = scope .();
				cc.mClassB_C = 1;

				ClassE ce = scope .();
				ce.mE = 2;
			}
		}

		class ClassF
		{
			private class ClassA
			{
				public int mClassF_A;
			}
		}

		extension ClassF
		{
			public void Hey()
			{
				ClassA ca = scope .();
				ca.mClassF_A = 1;
			}
		}

		class ClassG
		{
			public class InnerG
			{
				
			}

			public class InnerG2<T>
			{
				
			}
		}

		class ClassH
		{
			public class InnerH
			{

			}
		}

		class ClassI : ClassG
		{
			class InnerI : ClassH
			{
				class InnerInI : InnerG
				{

					public void UseIt()
					{
						InnerG2<int> ig2 = default;
						InnerH ih = default;
					}
				}
			}
		}
	}

	class DictExt : Dictionary<int, float>
	{
		Entry mEntry;
	}
}
