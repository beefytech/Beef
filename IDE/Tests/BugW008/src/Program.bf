#pragma warning disable 168

using System;
using System.Collections;

namespace Bug
{
	class Zonk<T>
	{
		public int Call(float val)
		{
			return 1;
		}
	}

	extension Zonk<T> where comptype(Gen.Get()) : String
	{
		public int Call(int val)
		{
			return 2;
		}
	}
	
	class Program
	{
		public static int Main(String[] args)
		{
			Zonk<int> zk = scope .();
			int val = zk.Call(1);
			//End
			return 0;
		}
	}
}
