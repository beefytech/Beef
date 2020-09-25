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
	}
}
