#pragma warning disable 168

using System;

namespace IDETest
{
	class Defer
	{
		public int ReturnFromDefer()
		{
			defer
			{
				return 123; //FAIL
			}

			return 0;
		}

		public int BreakFromDefer()
		{
			for (int i < 10)
			{
				defer
				{
					break; //FAIL
				}

				defer
				{
					continue; //FAIL
				}
			}

			Block:
			for (int j < 20)
			{
				for (int i < 10)
				{
					defer
					{
						//break Block;
					}
				}
			}

			return 0;
		}
	}
}
