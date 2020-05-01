namespace IDETest
{
	class Properties
	{
		struct StructA
		{
			public int mA = 111;

			public this()
			{
			}

			public this(int a)
			{
				mA = a;
			}
		}

		struct StructB
		{
			public StructA B { get; }

			int mZ = 9;

			public this() //FAIL
			{
			}

			public void Yoop() mut
			{
				B = .(); //FAIL
			}
		}
	}
}
