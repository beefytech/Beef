namespace LibB
{
	class LibB0
	{
	}
}

extension LibClassA
{
	public int32 mB = GetVal(8, "LibB.LibClassA.mB");

	public this()
	{
		PrintF("LibB.LibClassA()\n");
		mB += 100;
	}

	public this(int32 a)
	{
		PrintF("LibB.LibClassA(int32)\n");
		mB += 1000;
	}

	public int32 LibB_GetB()
	{
		return mB;
	}
}
