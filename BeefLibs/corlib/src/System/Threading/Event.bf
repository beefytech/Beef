namespace System.Threading
{
	public class WaitEvent
	{
		Platform.BfpEvent* mEvent;

		public this(bool initiallySet = false)
		{
			Platform.BfpEventFlags flags = .AllowAutoReset | .AllowManualReset;
			if (initiallySet)
				flags |= .InitiallySet_Manual;
			mEvent = Platform.BfpEvent_Create(flags);
		}

		public ~this()
		{
			Platform.BfpEvent_Release(mEvent);
		}

		public void Set(bool requireManualReset = false)
		{
			Platform.BfpEvent_Set(mEvent, requireManualReset);
		}

		public void Reset()
		{
			Platform.BfpEvent_Reset(mEvent, null);
		}

		public bool WaitFor(int waitMS = -1)
		{
			return Platform.BfpEvent_WaitFor(mEvent, (int32)waitMS);
		}
	}
}
