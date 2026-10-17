#pragma warning disable 168

using System;

namespace Tests
{
	// A lambda passed for a generic delegate parameter is a value whose data is its captures. Stored where the GC
	// traces it - a heap lambda's capture, or a field - its captured references must be marked as well, or a
	// collection reports objects that are still in use as leaked
	class LambdaCaptureMarking
	{
		class Holder<F> where F : delegate void()
		{
			public F mAction;

			public this(F action)
			{
				mAction = action;
			}
		}

		struct Pair<F> where F : delegate void()
		{
			public int mValue;
			public F mAction;
		}

		class PairHolder<F> where F : delegate void()
		{
			public Pair<F> mPair;

			public this(F action)
			{
				mPair.mValue = 1;
				mPair.mAction = action;
			}
		}

		static int sCalls;

		static Action Wrap<F>(F action) where F : delegate void()
		{
			return new () => { action(); };
		}

		static Holder<F> Hold<F>(F action) where F : delegate void()
		{
			return new Holder<F>(action);
		}

		static PairHolder<F> HoldPair<F>(F action) where F : delegate void()
		{
			return new PairHolder<F>(action);
		}

		[NoInline]
		static Action CreateWrapped()
		{
			Action inner = new () => { sCalls++; };
			return Wrap(() =>
				{
					inner();
					delete inner;
				});
		}

		[NoInline]
		static Object CreateHeld(out Action invoke)
		{
			Action inner = new () => { sCalls++; };
			let holder = Hold(() =>
				{
					inner();
					delete inner;
				});
			invoke = new () => { holder.mAction(); };
			return holder;
		}

		[NoInline]
		static Object CreatePairHeld(out Action invoke)
		{
			Action inner = new () => { sCalls++; };
			let holder = HoldPair(() =>
				{
					inner();
					delete inner;
				});
			invoke = new () => { holder.mPair.mAction(); };
			return holder;
		}

		[NoInline]
		static Action CreateWrappedTuple()
		{
			// A composite capture: the object is inside the tuple
			(int, Action) pair = (7, new () => { sCalls++; });
			return Wrap(() =>
				{
					pair.1();
					delete pair.1;
				});
		}

		[Test]
		public static void TestHeapLambdaCapture()
		{
			sCalls = 0;
			let outer = CreateWrapped();
			GC.Collect(false);
			outer();
			delete outer;
			Test.Assert(sCalls == 1);
		}

		[Test]
		public static void TestFieldCapture()
		{
			sCalls = 0;
			Action invoke;
			let holder = CreateHeld(out invoke);
			GC.Collect(false);
			invoke();
			delete invoke;
			delete holder;
			Test.Assert(sCalls == 1);
		}

		[Test]
		public static void TestStructFieldCapture()
		{
			sCalls = 0;
			Action invoke;
			let holder = CreatePairHeld(out invoke);
			GC.Collect(false);
			invoke();
			delete invoke;
			delete holder;
			Test.Assert(sCalls == 1);
		}

		[Test]
		public static void TestCompositeCapture()
		{
			sCalls = 0;
			let outer = CreateWrappedTuple();
			GC.Collect(false);
			outer();
			delete outer;
			Test.Assert(sCalls == 1);
		}
	}
}
