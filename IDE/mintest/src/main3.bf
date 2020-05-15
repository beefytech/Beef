#pragma warning disable 168

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;


struct Blurg
{

	
	public static void Hey()
	{
		String str = new String();
		delete str;

		//Internal.Malloc(123);
		Internal.StdMalloc(123);
	}

}
