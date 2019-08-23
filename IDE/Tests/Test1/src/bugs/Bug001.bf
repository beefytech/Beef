#pragma warning disable 168

using System;

class Bug001
{
	class ClassA
	{
		public String mStrA;
		public String mStrB;
		public bool mCheck;
	
		public void DoTest()
		{
			int a = 123;
			int b = 234;
			if (mCheck)
				return;

			//Bug001_DoTest
			while (mCheck)
			{
			}
		}
	}

	public static void Test()
	{
		ClassA ca = scope .();
		ca.DoTest();
	}
}