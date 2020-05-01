namespace Tests
{
	class StructInit
	{
		struct StructA
		{
			public int mA0;
		}

		struct StructB
		{
			public int mB0;
			public int mB1;
		}

		struct StructC
		{
			public StructA mSA;
			public StructB mSB;

			public this()
			{
				mSA.mA0 = 1;
				mSB.mB0 = 2;
				mSB.mB1 = 3;
			}
		}

		struct StructD
		{
			public StructC mSC;
			public int mD0;

			public this()
			{
				mSC.mSA.mA0 = 1;
				mSC.mSB.mB0 = 2;
				mSC.mSB.mB1 = 3;
				mD0 = 4;
			}
		}

		struct StructE
		{
			public StructD mSD;
			public int[3] mE0;

			public this()
			{
				mSD.mSC.mSA.mA0 = 1;
				mSD.mSC.mSB.mB0 = 2;
				mSD.mSC.mSB.mB1 = 3;
				mSD.mD0 = 4;
				mE0[0] = 5;
				mE0[1] = 6;
				mE0[2] = 7;
			}
		}
	}
}
