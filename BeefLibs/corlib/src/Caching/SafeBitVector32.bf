// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	/// This is a multi-thread-safe version of System.Collections.Specialized.BitVector32.
	struct SafeBitVector32
	{
		private volatile int _data;

		public bool this[int bit]
		{
			get
			{
				int data = _data;
				return (data & bit) == bit;
			}

			set mut
			{
				for (;;)
				{
					int oldData = _data;
					int newData = value ? (oldData | bit) : (oldData & ~bit);
					int result = Interlocked.CompareExchange(ref _data, newData, oldData);

					if (result == oldData)
						break;
				}
			}
		}

		public bool ChangeValue(int bit, bool value) mut
		{
			for (;;)
			{
				int oldData = _data;
				int newData = value ? (oldData | bit) : (oldData & ~bit);

				if (oldData == newData)
					return false;

				int result = Interlocked.CompareExchange(ref _data, newData, oldData);

				if (result == oldData)
					return true;
			}
		}
	}
}
