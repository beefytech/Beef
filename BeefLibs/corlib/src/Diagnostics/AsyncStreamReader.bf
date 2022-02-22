using System.IO;
using System.Text;
using System.Threading;
using System.Collections;

namespace System.Diagnostics
{
	delegate void UserCallBack(String data);

	class AsyncStreamReader
	{
		private const int32 DefaultBufferSize = 1024;  // Byte buffer size
		private const int32 MinBufferSize = 128;

		private Stream stream;
		private Encoding encoding;
		private Decoder decoder;
		private uint8[] byteBuffer;
		private char8[] char8Buffer;
		// Record the number of valid bytes in the byteBuffer, for a few checks.

		// This is the maximum number of chars we can get from one call to 
		// ReadBuffer.  Used so ReadBuffer can tell when to copy data into
		// a user's char8[] directly, instead of our internal char8[].
		private int32 mMaxCharsPerBuffer;

		// Store a backpointer to the process class, to check for user callbacks
		private Process process;

		// Delegate to call user function.
		private UserCallBack userCallBack;

		// Internal Cancel operation
		private bool cancelOperation;
		private WaitEvent eofEvent = new WaitEvent() ~ delete _;
		private Queue<String> messageQueue ~ delete _;
		private String sb;
		private bool bLastCarriageReturn;
		private Monitor mMonitor = new Monitor();

		// Cache the last position scanned in sb when searching for lines.
		private int currentLinePos;

		this(Process process, Stream stream, UserCallBack callback, Encoding encoding)
		: this(process, stream, callback, encoding, DefaultBufferSize)
		{
		}

		~this()
		{
			for (var msg in messageQueue)
				delete msg;
			Close();
		}

		// Creates a new AsyncStreamReader for the given stream.  The 
		// character encoding is set by encoding and the buffer size, 
		// in number of 16-bit characters, is set by bufferSize.  
		//
		this(Process process, Stream stream, UserCallBack callback, Encoding encoding, int32 bufferSize)
		{
			Debug.Assert(process != null && stream != null && encoding != null && callback != null, "Invalid arguments!");
			Debug.Assert(stream.CanRead, "Stream must be readable!");
			Debug.Assert(bufferSize > 0, "Invalid buffer size!");

			Init(process, stream, callback, encoding, bufferSize);
			messageQueue = new Queue<String>();
		}

		public virtual Encoding CurrentEncoding
		{
			get { return encoding; }
		}

		public virtual Stream BaseStream
		{
			get { return stream; }
		}

		private void Init(Process process, Stream stream, UserCallBack callback, Encoding encoding, int32 bufferSize)
		{
			this.process = process;
			this.stream = stream;
			this.encoding = encoding;
			this.userCallBack = callback;
			int32 useBufferSize = bufferSize;
			if (useBufferSize < MinBufferSize) useBufferSize = MinBufferSize;
			byteBuffer = new uint8[useBufferSize];
			mMaxCharsPerBuffer = (int32)encoding.GetMaxCharCount(useBufferSize);
			char8Buffer = new char8[mMaxCharsPerBuffer];
			cancelOperation = false;
			sb = null;
			this.bLastCarriageReturn = false;
		}

		public virtual void Close()
		{
			Dispose(true);
		}

		void Dispose(bool disposing)
		{
			if (disposing)
			{
				if (stream != null)
					stream.Close();
			}
			if (stream != null)
			{
				stream = null;
				encoding = null;
				decoder = null;
				byteBuffer = null;
				char8Buffer = null;
			}
		}

		// User calls BeginRead to start the asynchronous read
		void BeginReadLine()
		{
			if (cancelOperation)
			{
				cancelOperation = false;
			}

			if (sb == null)
			{
				sb = new String(DefaultBufferSize);
				stream.BeginRead(byteBuffer, 0, byteBuffer.Count, new => ReadBuffer, null);
			}
			else
			{
				FlushMessageQueue();
			}
		}

		void CancelOperation()
		{
			cancelOperation = true;
		}

		// This is the async callback function. Only one thread could/should call this.
		private void ReadBuffer(IAsyncResult ar)
		{
			int byteLen = stream.EndRead(ar);
			
			// We should ideally consume errors from operations getting cancelled
			// so that we don't crash the unsuspecting parent with an unhandled exc. 
			// This seems to come in 2 forms of exceptions (depending on platform and scenario), 
			// namely OperationCanceledException and IOException (for errorcode that we don't 
			// map explicitly).
			byteLen = 0; // Treat this as EOF


			if (byteLen == 0)
			{
				// We're at EOF, we won't call this function again from here on.
				using (mMonitor.Enter())
				{
					if (sb.Length != 0)
					{
						messageQueue.Add(new String(sb));
						sb.Clear();
					}
					messageQueue.Add(null);
				}

				// UserCallback could throw, we should still set the eofEvent
				FlushMessageQueue();

				eofEvent.Set(true);
			}
			else
			{
				int char8Len = decoder.GetChars(byteBuffer, 0, byteLen, char8Buffer, 0);
				sb.Append(char8Buffer, 0, char8Len);
				GetLinesFromStringBuilder();
				stream.BeginRead(byteBuffer, 0, byteBuffer.Count, new => ReadBuffer, null);
			}
		}


		// Read lines stored in StringBuilder and the buffer we just read into. 
		// A line is defined as a sequence of characters followed by
		// a carriage return ('\r'), a line feed ('\n'), or a carriage return
		// immediately followed by a line feed. The resulting string does not
		// contain the terminating carriage return and/or line feed. The returned
		// value is null if the end of the input stream has been reached.
		//

		private void GetLinesFromStringBuilder()
		{
			int currentIndex = currentLinePos;
			int lineStart = 0;
			int len = sb.Length;

			// skip a beginning '\n' character of new block if last block ended 
			// with '\r'
			if (bLastCarriageReturn && (len > 0) && sb[0] == '\n')
			{
				currentIndex = 1;
				lineStart = 1;
				bLastCarriageReturn = false;
			}

			while (currentIndex < len)
			{
				char8 ch = sb[currentIndex];
				// Note the following common line feed chars:
				// \n - UNIX   \r\n - DOS   \r - Mac
				if (ch == '\r' || ch == '\n')
				{
					String s = new String();
					s.Append(sb, lineStart, currentIndex - lineStart);

					lineStart = currentIndex + 1;
					// skip the "\n" character following "\r" character
					if ((ch == '\r') && (lineStart < len) && (sb[lineStart] == '\n'))
					{
						lineStart++;
						currentIndex++;
					}

					using (mMonitor.Enter())
					{
						messageQueue.Add(s);
					}
				}
				currentIndex++;
			}
			if (sb[len - 1] == '\r')
			{
				bLastCarriageReturn = true;
			}
			// Keep the rest characaters which can't form a new line in string builder.
			if (lineStart < len)
			{
				if (lineStart == 0)
				{
					// we found no breaklines, in this case we cache the position
					// so next time we don't have to restart from the beginning
					currentLinePos = currentIndex;
				}
				else
				{
					sb.Remove(0, lineStart);
					currentLinePos = 0;
				}
			}
			else
			{
				sb.Clear();
				currentLinePos = 0;
			}

			FlushMessageQueue();
		}

		private void FlushMessageQueue()
		{
			while (true)
			{
				// When we call BeginReadLine, we also need to flush the queue
				// So there could be a ---- between the ReadBuffer and BeginReadLine
				// We need to take lock before DeQueue.
				if (messageQueue.Count > 0)
				{
					using (mMonitor.Enter())
					{
						if (messageQueue.Count > 0)
						{
							String s = messageQueue.PopFront();
							// skip if the read is the read is cancelled
							// this might happen inside UserCallBack
							// However, continue to drain the queue
							if (!cancelOperation)
							{
								userCallBack(s);
							}
							delete s;
						}
					}
				}
				else
				{
					break;
				}
			}
		}

		// Wait until we hit EOF. This is called from Process.WaitForExit
		// We will lose some information if we don't do this.
		void WaitUtilEOF()
		{
			if (eofEvent != null)
			{
				eofEvent.WaitFor();
			}
		}
	}
}
