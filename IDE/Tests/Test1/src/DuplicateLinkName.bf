#pragma warning disable 168

using System;

namespace IDETest
{
	class DuplicateLinkName
	{
		// Two extern declarations share a symbol with different signatures, and are bound to function
		// pointers. With hot swapping on, the Beef backend (Win64 Debug's OgPlus) gave each its own
		// 'bf_hs_preserve@' variable of the same name, which failed to link
		[LinkName("strlen")]
		static extern int StrLenA(char8* str);
		[LinkName("strlen")]
		static extern int StrLenB(void* str);

		public static void Test()
		{
			function int(char8*) funcA = => StrLenA;
			function int(void*) funcB = => StrLenB;
			Runtime.Assert(funcA("abc") == 3);
			Runtime.Assert(funcB((char8*)"abcd") == 4);
		}
	}
}
