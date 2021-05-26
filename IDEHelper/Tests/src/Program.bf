namespace Tests
{
	typealias DDDDAttribute = System.InlineAttribute;

	class Program
	{
		
		public static void Main()
		{
			A();
		}

		[DDDD]
		public static void A()
		{
			int i = 1 + 2;
		}
	}
}
