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
			public StructA B2 { get; set mut; }
			public ref StructA B3 { get; } //FAIL
			public ref StructA B4 { get mut; }

			int mZ = 9;

			public this() //FAIL
			{
			}

			public void Yoop() mut
			{
				B = .(); //FAIL
				B2.mA = .(); //WARN
			}
		}
	}
}
