#pragma warning disable 168

namespace IDETest
{
	class Properties
	{
		struct StructA
		{
			public float mA;
			public float mB;
		}

		struct StructB
		{
			public float mA;
			public float mB;
			public float mC;

			public StructA A0
			{
				get
				{
					StructA sa;
					sa.mA = mA + 1000;
					sa.mB = mB + 2000;
					return sa;
				}
			}

			public StructA A1
			{
				get mut
				{
					StructA sa;
					sa.mA = mA + 2000;
					sa.mB = mB + 3000;
					return sa;
				}
			}
		}

		struct StructC
		{
			public float mA;
			public float mB;
			public float mC;
			public float mD;

			public StructA A0
			{
				get
				{
					StructA sa;
					sa.mA = mA + 4000;
					sa.mB = mB + 5000;
					return sa;
				}
			}

			public StructA A1
			{
				get mut
				{
					StructA sa;
					sa.mA = mA + 6000;
					sa.mB = mB + 7000;
					return sa;
				}
			}

			public StructB B0
			{
				get
				{
					StructB sb;
					sb.mA = mA + 4000;
					sb.mB = mB + 5000;
					sb.mC = mC + 6000;
					return sb;
				}
			}

			public StructB B1
			{
				get mut
				{
					StructB sb;
					sb.mA = mA + 7000;
					sb.mB = mB + 8000;
					sb.mC = mC + 9000;
					return sb;
				}
			}

			public StructC C0
			{
				get
				{
					StructC sc;
					sc.mA = mA + 10000;
					sc.mB = mB + 11000;
					sc.mC = mC + 12000;
					sc.mD = mD + 13000;
					return sc;
				}
			}

			public StructC C1
			{
				get mut
				{
					StructC sc;
					sc.mA = mA + 14000;
					sc.mB = mB + 15000;
					sc.mC = mC + 16000;
					sc.mD = mD + 17000;
					return sc;
				}
			}
		}

		public static void Test()
		{
			StructB sb = .();
			sb.mA = 100;
			sb.mB = 101;
			sb.mC = 102;
			var sb_a0 = sb.A0;
			var sb_a1 = sb.A1;

			StructC sc = .();
			sc.mA = 200;
			sc.mB = 201;
			sc.mC = 202;
			sc.mD = 203;
			var sc_a0 = sc.A0;
			var sc_a1 = sc.A1;
			var sc_b0 = sc.B0;
			var sc_b1 = sc.B1;
			var sc_c0 = sc.C0;
			var sc_c1 = sc.C1;

			//Test_Break
			int a = 123;
		}
	}
}
