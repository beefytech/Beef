#pragma warning disable 168
//#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;

struct Blurg
{
	public static int MethodB<T>(T foo) where T : struct
	{
	    return 2;
	}

	public static int MethodB<K, V>((K key, V value) foo) where K : var where V : var
	{
	    return 3;
	}

	public static void Hey()
	{
		Debug.Assert(MethodB(11) == 2);
		Debug.Assert(MethodB(("A", "B")) == 3);
	}
}
