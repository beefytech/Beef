#pragma warning disable 168

using System;
using System.Collections;

namespace Bug
{
	class ClassA
	{
		public int Call()
		{
			return 1;
		}
	}

	class ClassB
	{
		public int Call()
		{
			return 2;
		}
	}

	typealias Alias1 = comptype(Gen.Get());
	typealias Alias2 = Alias1;

	class Zonk<T> : Alias2
	{
		
	}

	class Program
	{
		public static int Main(String[] args)
		{
			Zonk<int> zk = scope .();
			int val = zk.Call();
			//End
			return 0;
		}
	}
}
