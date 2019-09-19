using System.IO;

namespace System.IO
{
	class Substream : Stream
	{
		public bool mOwnsStream;
		Stream mChildStream ~ { if (mOwnsStream) delete _; };
		int64 mOffset;
		int64 mLength;

		public override int64 Position
		{
			get
			{
				return mChildStream.Position + mOffset;
			}

			set
			{
				mChildStream.Position = value + mOffset;
			}
		}

		public override int64 Length
		{
			get
			{
				return mLength;
			}
		}

		public override bool CanRead
		{
			get
			{
				return mChildStream.CanRead;
			}
		}

		public override bool CanWrite
		{
			get
			{
				return mChildStream.CanWrite;
			}
		}


		public this(Stream childStream, int64 offset, int64 length, bool ownsStream = false)
		{
			mChildStream = childStream;
			mOffset = offset;
			mLength = length;
			mOwnsStream = ownsStream;
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			return mChildStream.TryRead(data);
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			return mChildStream.TryWrite(data);
		}

		public override void Close()
		{
			mChildStream.Close();
		}

		public override void Flush()
		{
			mChildStream.Flush();
		}
	}
}
