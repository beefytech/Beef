using System;

namespace Tests
{
	class Switches
	{
		int Switch0(Result<int> res)
		{
			switch (res)
			{
			case .Ok(let a):
				return 0;
			case .Err(let b):
				 return 1;
			}
		}
	}
}
