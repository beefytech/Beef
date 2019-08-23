#pragma warning disable 168

using System;

class Bug002
{
	public static void Parse(String[] strs)
	{
		for (let val in strs)
		{

		}

		int val2 = 999;
	}

	public static void Test()
	{
		var strs = scope String[] {"aaa", "bbb", "ccc"};
		//Bug002_DoTest
		Parse(strs);
	};
}