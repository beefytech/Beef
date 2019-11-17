using System;

namespace Tests
{
	class Generics
	{
		class ClassA : IDisposable
		{
			public void Dispose()
			{

			}
		}

		static void DoDispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		struct Disposer<T>
		{
			static void UseDispose(IDisposable disp)
			{

			}

			static void DoDisposeA(mut T val) where T : IDisposable
			{
				val.Dispose();
				UseDispose(val);
			}

			static void DoDisposeB(mut T val) where T : IDisposable
			{
				val.Dispose();
			}
		}
		
		[Test]
		public static void TestBasics()
		{

		}
	}
}
