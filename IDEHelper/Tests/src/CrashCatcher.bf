#pragma warning disable 168

using System;

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
		}
#endif
	}
}
