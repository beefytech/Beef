namespace System.Threading
{
	struct SpinWait
	{
		internal const int YIELD_THRESHOLD = 10; // When to switch over to a true yield.
		internal const int SLEEP_0_EVERY_HOW_MANY_TIMES = 5; // After how many yields should we Sleep(0)?
		internal const int SLEEP_1_EVERY_HOW_MANY_TIMES = 20; // After how many yields should we Sleep(1)?

		private int m_count;

		public int Count
		{
		    get { return m_count; }
		}

		public void SpinOnce() mut
		{
			//TODO: Implement
		}

		public void Reset() mut
		{
		    m_count = 0;
		}
	}
}
