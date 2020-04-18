#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections.Generic;

struct Smibbs
{
	public int16 mX = 11;
	public int16 mY = 22;
	public int16 mZ = 33;
}

namespace PropertyStructCrash
{
	struct B
	{
		public int c;
	}

	struct A
	{
		public B Prop { get; set; }
	}

	class Program
	{
		public static void Main()
		{
			var c = '¥';
		}
	}
}

struct Blurg
{
	public static void Hey()
	{
		PropertyStructCrash.Program.Main();
	}
}

