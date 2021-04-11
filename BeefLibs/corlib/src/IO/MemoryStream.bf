using System.Collections;

namespace System.IO
{
	class MemoryStream : Stream
	{
		List<uint8> mMemory = new List<uint8>() ~ delete _;
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
	}
}
