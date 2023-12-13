namespace System
{
	struct Guid : IHashable, IParseable<Guid>
	{
		public static readonly Guid Empty = Guid();
        
        private uint32 mA;
        private uint16 mB;
        private uint16 mC;
        private uint8 mD;
        private uint8 mE;
        private uint8 mF;
        private uint8 mG;
        private uint8 mH;
        private uint8 mI;
        private uint8 mJ;
        private uint8 mK;

		public this()
		{
			this = default;
		}

		public this(uint32 a, uint16 b, uint16 c, uint8 d, uint8 e, uint8 f, uint8 g, uint8 h, uint8 i, uint8 j, uint8 k)
		{
			mA = a;
			mB = b;
			mC = c;
			mD = d;
			mE = e;
			mF = f;
			mG = g;
			mH = h;
			mI = i;
			mJ = j;
			mK = k;
		}

		public int GetHashCode()
		{
			return (int)mA ^ (int)mK;
		}

		[Commutable]
		public static bool operator==(Guid val1, Guid val2)
		{
			return 
                (val1.mA == val2.mA) &&
				(val1.mB == val2.mB) &&
				(val1.mC == val2.mC) &&
				(val1.mD == val2.mD) &&
				(val1.mE == val2.mE) &&
				(val1.mF == val2.mF) &&
				(val1.mG == val2.mG) &&
				(val1.mH == val2.mH) &&
				(val1.mI == val2.mI) &&
				(val1.mJ == val2.mJ) &&
				(val1.mK == val2.mK);
		}

		public static Result<Guid> Parse(StringView val)
		{
			return .Err;
		}

		public static Guid Create()
		{
			Guid guid = ?;
			Platform.BfpSystem_CreateGUID(&guid);
			return guid;
		}
	}
}
