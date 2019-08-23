using System.Text;
using System.Diagnostics;

namespace System.IO
{
	public class StreamWriter
	{
		Stream mStream ~ if (mOwnsStream) delete _;
		public bool mOwnsStream;
		public Encoding mEncoding;

		public bool AutoFlush;

		public this(Stream stream, Encoding encoding, int32 bufferSize, bool ownsStream = false)
		{
			Debug.Assert(encoding != null);
			mStream = stream;
			mEncoding = encoding;
			mOwnsStream = ownsStream;
		}

		public Result<void> Write(Span<uint8> data)
		{
			var spanLeft = data;
			while (!spanLeft.IsEmpty)
			{
				int bytesWritten = Try!(mStream.TryWrite(spanLeft));
				spanLeft.Adjust(bytesWritten);
			}
			if (AutoFlush)
				Flush();
			return .Ok;
		}

		public Result<void> Write(StringView str)
		{
			var curSpan = str;

			Span<uint8> outSpan = scope uint8[4096];

			while (!curSpan.IsEmpty)
			{
				switch (mEncoding.Encode(curSpan, outSpan))
				{
				case .Ok(let encodedBytes):
					Try!(Write(Span<uint8>(outSpan.Ptr, encodedBytes)));
					return .Ok;
				case .Err(let err):
					switch (err)
					{
					case .PartialEncode(int inChars, int encodedBytes):
						Try!(Write(Span<uint8>(outSpan.Ptr, encodedBytes)));
						curSpan.Adjust(inChars);
					}
				}
			}

			return .Ok;
		}

		public Result<void> WriteLine(StringView str)
		{
			Try!(Write(str));
			return Write("\n");
		}

		public Result<void> Write(StringView fmt, params Object[] args)
		{
			return Write(scope String()..AppendF(fmt, params args));
		}

		public Result<void> WriteLine(StringView fmt, params Object[] args)
		{
			Try!(Write(scope String()..AppendF(fmt, params args)));
			return Write("\n");
		}

		public void Flush()
		{
			mStream.Flush();
		}
	}
}
