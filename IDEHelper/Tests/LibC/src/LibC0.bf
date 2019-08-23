namespace LibC
{
	class LibC0
	{
	}
}

extension LibClassA
{
	public int32 mB = GetVal(13, "LibC.LibClassA.mB");

	public this(int8 i8)
	{
		PrintF("LibC.LibClassA()\n");
		mB += 30000;
	}

	public int LibC_GetB()
	{
		return mB;
	}
}

