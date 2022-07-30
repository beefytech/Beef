namespace IDETest
{
	class Program
	{
		static void Main()
		{
			Assembly.Test();
			Break.Test();
			Breakpoints.Test();
			Breakpoints02.Test();
			Data01.Test();
			EnumTester.Test();
			HotTester.Test();
			HotSwap_BaseChange.Test();
			HotSwap_Data.Test();
			HotSwap_GetUnusued.Test();
			HotSwap_Interfaces2.Test();
			HotSwap_Lambdas01.Test();
			HotSwap_LocateSym01.Test();
			HotSwap_Reflection.Test();
			HotSwap_TLS.Test();
			InlineTester.Test();
			LambdaTester.Test();
			Methods.Test();
			Mixins.Test();
			Multithread.Test();
			Multithread02.Test();
			MemoryBreakpointTester.Test();
			Properties.Test();
			SplatTester.Test();
			Stepping_Scope.Test();
			TypedPrimitives.Test();
			Unions.Test();
			UsingFields.Test();
			Virtuals.Test();

			Bug001.Test();
			Bug002.Test();
		}
	}
}
