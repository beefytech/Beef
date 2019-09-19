using System.Collections.Generic;
using System.Diagnostics;

namespace System.IO
{
	/// The NullStream will allow ignore all writes (returning success) and return 'no data' on all reads.
	class NullStream : Stream
	{				   
		public this()
		{
		}

		public override int64 Position
		{
			get
			{
				return 0;
			}

			set
			{
			}
		}

		public override int64 Length
		{
			get
			{
				return 0;
			}
		}

		public override bool CanRead
		{
			get
			{
				return true;
			}
		}

		public override bool CanWrite
		{
			get
			{
				return true;
			}
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			return .Ok(0);
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			return .Ok(data.Length);
		}

		public override void Close()
		{
			
		}
	}
}
