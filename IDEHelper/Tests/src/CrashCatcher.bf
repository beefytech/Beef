#pragma warning disable 168

using System;
using System.Threading;

namespace Tests
{
	class CrashCatcher
	{
#if BF_PLATFORM_LINUX
		// glibc's struct sigaction on x86_64 and aarch64: the handler, a 1024-bit signal set,
		// the flags, and the restorer.
		[CRepr]
		struct SigAction
		{
			public void* mHandler;
			public uint64[16] mMask;
			public int32 mFlags;
			public void* mRestorer;
		}

		[CLink]
		static extern int32 sigaction(int32 sig, SigAction* action, SigAction* oldAction);

		const int32 SIGTRAP = 5;
		const int32 SIGSEGV = 11;

		// The runtime installs its crash catcher at startup on Linux as on Windows, so a crash
		// reports its signal and a backtrace instead of ending with "Segmentation fault" alone.
		// The test runner installs no SIGSEGV handler of its own, so a handler here is the
		// runtime's.
		[Test]
		public static void TestInstalledOnLinux()
		{
			SigAction current = default;
			Test.Assert(sigaction(SIGSEGV, null, &current) == 0);
			Test.Assert(current.mHandler != null); // SIG_DFL is null

			// Debug.Break() with no debugger attached reports too
			current = default;
			Test.Assert(sigaction(SIGTRAP, null, &current) == 0);
			Test.Assert(current.mHandler != null);
		}

		// glibc's stack_t on x86_64 and aarch64
		[CRepr]
		struct SignalStack
		{
			public void* mSp;
			public int32 mFlags;
			public int mSize;
		}

		[CLink]
		static extern int32 sigaltstack(SignalStack* stack, SignalStack* oldStack);

		const int32 SS_DISABLE = 2;

		static bool HasSignalStack()
		{
			SignalStack current = default;
			return (sigaltstack(null, &current) == 0) && ((current.mFlags & SS_DISABLE) == 0);
		}

		// sigaltstack is per-thread and a new thread starts without one, so the runtime gives each
		// thread it creates its own. Without it, a stack overflow on that thread can't be reported.
		[Test]
		public static void TestThreadSignalStack()
		{
			Test.Assert(HasSignalStack());

			bool threadHasSignalStack = false;
			Thread thread = scope .(new [&] () =>
				{
					threadHasSignalStack = HasSignalStack();
				});
			thread.Start(false);
			thread.Join();
			Test.Assert(threadHasSignalStack);
		}
#endif
	}
}
