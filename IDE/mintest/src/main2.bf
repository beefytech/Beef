using System;

#define SMz

#if SM
typealias int_test = int32;
#else
typealias int_test =  int64;
#endif

struct Floof
{
	public static int32 Hey()
	{
		Result<int> res = 123;
		switch (res)
		{
		case .Ok(let val):
		default:
		}

		return 123;
	}
}
