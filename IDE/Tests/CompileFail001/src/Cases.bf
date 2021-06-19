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

		int Switch1(Result<int> res)
		{
			switch (res)
			{
			case .Ok(let a):
				fallthrough;
			case .Err(let b): //FAIL
				 return 1;
			}
		}

		int Switch2(Result<int> res)
		{
			switch (res)
			{
			case .Ok(let a):
				if (a > 0)
					break;
				fallthrough;
			case .Err:
				 return 1;
			}
		} //FAIL
	}
}
