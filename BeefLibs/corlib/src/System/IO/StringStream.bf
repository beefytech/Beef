using System.Collections.Generic;
using System.Diagnostics;

namespace System.IO
{
	class StringStream : Stream
	{
		enum StringKind
		{
			Append,
			Reference,
			Owned
		}

		StringKind mStringKind;
		String mString;
		int mPosition;

		[AllowAppend]
		public this()
		{
			let appendStr = append String();
			mString = appendStr;
		}

		public ~this()
		{
			if (mStringKind == .Append)
				delete:append mString;
			else if (mStringKind == .Owned)
				DeleteOwned();
		}

		protected virtual void DeleteOwned()
		{
			delete mString;
		}

		public enum StringViewInitKind
		{
			Copy,
			Reference,
		}

		[AllowAppend]
		public this(StringView str, StringViewInitKind initKind)
		{
			let appendStr = append String();
			mString = appendStr;
			mStringKind = .Append;

			switch (initKind)
			{
			case .Copy:
				mString.Set(str);
			case .Reference:
				mString = new String();
				mString.Reference(str.Ptr, str.Length);
			}
		}

		public enum StringInitKind
		{
			Copy,
			Reference,
			TakeOwnership
		}

		[AllowAppend]
		public this(String str, StringInitKind initKind)
		{
			let appendStr = append String();
			mString = appendStr;
			
			switch (initKind)
			{
			case .Copy:
				mString.Set(str);
				mStringKind = .Append;
			case .Reference:
				mString = str;
				mStringKind = .Reference;
			case .TakeOwnership:
				mString = str;
				mStringKind = .Owned;
			}
		}

		public StringView Content
		{
			get
			{
				return mString;
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
				return mString.Length;
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
			if (data.Length == 0)
				return .Ok(0);
			int readBytes = Math.Min(data.Length, mString.Length - mPosition);
			if (readBytes <= 0)
				return .Ok(readBytes);

			Internal.MemCpy(data.Ptr, &mString[mPosition], readBytes);
			mPosition += readBytes;
			return .Ok(readBytes);
		}

		public override Result<int> TryWrite(Span<uint8> data)
		{
			let count = data.Length;
			if (count == 0)
				return .Ok(0);
			int growSize = mPosition + count - mString.Length;
			if (growSize > 0)
				mString.PrepareBuffer(growSize);
			Internal.MemCpy(&mString[mPosition], data.Ptr, count);
			mPosition += count;
			return .Ok(count);
		}

		public override void Close()
		{
			
		}
	}
}
