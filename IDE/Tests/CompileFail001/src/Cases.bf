#pragma warning disable 168

using System;

namespace IDETest
{
	class Cases
	{
		public void Test()
		{
			Result<int> iResult = .Err;

			if ((iResult case .Ok(var val0)) || (true)) //FAIL
			{

			}

			int val1;
			if ((true) || (iResult case .Ok(out val1)))
			{
				int a = val1; //FAIL
			}
		}
	}
}
