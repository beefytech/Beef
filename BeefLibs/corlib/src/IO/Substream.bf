using System.IO;

namespace System.IO
{
	class Substream : Stream
	{
		public bool mOwnsStream;
		Stream mChildStream ~ { if (mOwnsStream) delete _; };
		int64 mOffset;
		int64 mLength;
		int64 mPosition;

		public override int64 Position
		{
			get
			{
				return mPosition;
			}

			set
			{
				mPosition = value;
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
			mChildStream.Position = offset;
			mOffset = offset;
			mLength = length;
			mOwnsStream = ownsStream;
		}

		public override Result<int> TryRead(Span<uint8> data)
		{
			var tryData = data;
			if (mPosition + data.Length > mLength)
				tryData.Length = (.)(mLength - mPosition);

			switch (mChildStream.TryRead(tryData))
			{
			case .Ok(let len):
				mPosition += len;
				return .Ok(len);
			case .Err(let err):
				return .Err(err);
			}
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			var tryData = data;
			if (mPosition + data.Length > mLength)
				tryData.Length = (.)(mLength - mPosition);

			switch (mChildStream.TryWrite(tryData))
			{
			case .Ok(let len):
				mPosition += len;
				return .Ok(len);
			case .Err(let err):
				return .Err(err);
			}
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
