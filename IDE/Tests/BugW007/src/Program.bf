#pragma warning disable 168

using System;
using System.Collections;

namespace Bug //Test
{
	struct Zonkle
	{
		int mA;
	}
	
	class Zorp
	{
		Dictionary<int, Zonkle*> mDict;
	}
}

namespace Bug
{
	
	class Program
	{
		public static int Main(String[] args)
		{
			return 0;
		}
	}
}
