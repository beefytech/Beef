#pragma warning disable 168

class TypedPrimitives
{
	struct StructA : float
	{

	}

	public static void Test()
	{
		//Test_Start
		StructA sa = (.)1.2f;
	}
}