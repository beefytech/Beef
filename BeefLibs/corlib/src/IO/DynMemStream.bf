using System.Collections;
using System.Diagnostics;

namespace System.IO
{
	class DynMemStream : Stream
	{
		List<uint8> mData ~ { if (mOwnsData) delete _; };
		int mPosition = 0;
		bool mOwnsData;

		public this()
		{
			mData = new .();
			mOwnsData = true;
		}

		public uint8* Ptr
		{
			get
			{
				return mData.Ptr;
			}
		}
		
		public Span<uint8> Content
		{
			get
			{
				return mData;
			}
		}

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
				return mData.Count;
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

		public List<uint8> TakeOwnership()
		{
			Debug.Assert(mOwnsData);
			mOwnsData = false;
			return mData;
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			if (data.Length == 0)
				return .Ok(0);
			int readBytes = Math.Min(data.Length, mData.Count - mPosition);
			if (readBytes <= 0)
				return .Ok(readBytes);

			Internal.MemCpy(data.Ptr, &mData[mPosition], readBytes);
			mPosition += readBytes;
			return .Ok(readBytes);
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			let count = data.Length;
			if (count == 0)
				return .Ok(0);
			int growSize = mPosition + count - mData.Count;
			if (growSize > 0)
				mData.GrowUnitialized(growSize);
			Internal.MemCpy(&mData[mPosition], data.Ptr, count);
			mPosition += count;
			return .Ok(count);
		}

		public override Result<void> Close()
		{
			return .Ok;
		}

		public void RemoveFromStart(int count)
		{
			mPosition = Math.Max(mPosition - count, 0);
			mData.RemoveRange(0, count);
		}
	}
}
