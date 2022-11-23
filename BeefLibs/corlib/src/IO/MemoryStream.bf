using System.Collections;
using System.Diagnostics;

namespace System.IO
{
	class MemoryStream : Stream
	{
		bool mOwns;
		List<uint8> mMemory ~ { if (mOwns) delete _; }
		int mPosition = 0;

		public override int64 Position
		{
			get
			{
				return mPosition;
			}

			set
			{
				mPosition = (.)value;
			}
		}

		public override int64 Length
		{
			get
			{
				return mMemory.Count;
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

		public this()
		{
			mOwns = true;
			mMemory = new List<uint8>();
		}
		
		public this(int capacity)
        {
			mOwns = true;
			mMemory = new List<uint8>(capacity);
        }

		public this(List<uint8> memory, bool owns = true)
		{
			mOwns = owns;
			mMemory = memory;
		}

		public List<uint8> Memory => mMemory;

		public override Result<int> TryRead(Span<uint8> data)
		{
			let count = data.Length;
			if (count == 0)
				return .Ok(0);
			int readBytes = Math.Min(count, mMemory.Count - mPosition);
			if (readBytes <= 0)
				return .Ok(readBytes);

			Internal.MemCpy(data.Ptr, &mMemory[mPosition], readBytes);
			mPosition += readBytes;
			return .Ok(readBytes);
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			let count = data.Length;
			if (count == 0)
				return .Ok(0);
			int growSize = mPosition + count - mMemory.Count;
			if (growSize > 0)
				mMemory.GrowUnitialized(growSize);
			Internal.MemCpy(&mMemory[mPosition], data.Ptr, count);
			mPosition += count;
			return .Ok(count);
		}

		public override Result<void> Close()
		{
			return .Ok;
		}
		
		public override Result<void> SetLength(int64 length)
        {
			Debug.Assert(mOwns);

			mMemory.Resize((.)length);

			if (Position >= length)
				Position = Length;

			return .Ok;
        }
	}
}
