using System;

namespace SysMSVCRT
{
	class Program
	{
		public static int Main(String[] args)
		{
			return int.Parse(args[0]).GetValueOrDefault() + int.Parse(args[1]).GetValueOrDefault();
		}
	}
}
