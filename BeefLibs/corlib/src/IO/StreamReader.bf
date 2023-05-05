// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics.Contracts;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Collections;

namespace System.IO
{
	class StreamReader
	{
		private const int32 DefaultFileStreamBufferSize = 4096;
		private const int32 MinBufferSize = 128;

		Stream mStream ~ if (mOwnsStream) delete _;
		public bool mOwnsStream;
		int mCharLen;
		int mCharPos;

		private int mBytePos;

        // Record the number of valid bytes in the byteBuffer, for a few checks.
		private int mByteLen;
		 // We will support looking for byte order marks in the stream and trying
		// to decide what the encoding might be from the byte order marks, IF they
		// exist.  But that's all we'll do.
		private bool mDetectEncoding;

		// Whether we must still check for the encoding's given preamble at the
		// beginning of this file.
		private bool mCheckPreamble;

		// Whether the stream is most likely not going to give us back as much 
		// data as we want the next time we call it.  We must do the computation
		// before we do any byte order mark handling and save the result.  Note
		// that we need this to allow users to handle streams used for an 
		// interactive protocol, where they block waiting for the remote end 
		// to send a response, like logging in on a Unix machine.
		private bool mIsBlocked;

		private uint8[] mPreamble ~ delete _;   // Encoding's preamble, which identifies this encoding.

		private uint8[] mByteBuffer;
		private char8[] mCharBuffer;
		private bool mOwnsBuffers;

		private int32 mMaxCharsPerBuffer;
		private Encoding mEncoding;
		private bool mPendingNewlineCheck;

		public Stream BaseStream
		{
			get
			{
				return mStream;
			}
		}

		public this()
		{

		}

		public ~this()
		{
			if (mOwnsBuffers)
			{
				delete mByteBuffer;
				delete mCharBuffer;
			}
		}

		public Encoding CurrentEncoding
		{
			get
			{
				return mEncoding;
			}
		}

		public LineReader Lines
		{
			get
			{
				return LineReader(this);
			}
		}

		[AllowAppend]
		public this(Stream stream, Encoding encoding, bool detectEncodingFromByteOrderMarks, int32 bufferSize, bool ownsSteam = false)
		{
			int32 useBufferSize = bufferSize;
			if (useBufferSize < MinBufferSize) useBufferSize = MinBufferSize;
			let maxCharsPerBuffer = (encoding != null) ? encoding.GetMaxCharCount(useBufferSize) : bufferSize;

			let byteBuffer = append uint8[useBufferSize];
			let charBuffer = append char8[maxCharsPerBuffer];
			/*let byteBuffer = new uint8[useBufferSize];
			let charBuffer = new char8[maxCharsPerBuffer];
			mOwnsBuffers = true;*/

			mMaxCharsPerBuffer = (.)maxCharsPerBuffer;
			mByteBuffer = byteBuffer;
			mCharBuffer = charBuffer;
			mOwnsStream = ownsSteam;

			Init(stream, encoding, detectEncodingFromByteOrderMarks, bufferSize);
		}

		[AllowAppend]
		public this(Stream stream) : this(stream, .UTF8, false, 4096)
		{

		}

		int GetMaxCharCount(int32 byteCount)
		{
			if (mEncoding == null)
				return byteCount;
			// UTF-8 to UTF-8
			return mEncoding.GetMaxCharCount(byteCount);
		}

		void Init(Stream stream, Encoding encoding, bool detectEncodingFromByteOrderMarks, int32 bufferSize)
		{
			mStream = stream;
		    this.mEncoding = encoding;
		    //decoder = encoding.GetDecoder();

			if (mByteBuffer == null)
			{
				int32 useBufferSize = bufferSize;
				if (useBufferSize < MinBufferSize) useBufferSize = MinBufferSize;
				mMaxCharsPerBuffer = (.)GetMaxCharCount(useBufferSize);
				mByteBuffer = new uint8[useBufferSize];
				mCharBuffer = new char8[mMaxCharsPerBuffer];
				mOwnsBuffers = true;
			}

			mByteLen = 0;
			mBytePos = 0;
			mDetectEncoding = detectEncodingFromByteOrderMarks;
		    //mPreamble = encoding.GetPreamble();
			mCheckPreamble = false;//(mPreamble.Length > 0);
			mIsBlocked = false;
		}

		public virtual void Dispose()
		{
			delete mStream;
			mStream = null;
		}

		public bool EndOfStream
		{
			get
			{
				if (mCharPos < mCharLen)
					return false;

                // This may block on pipes!
				int numRead = ReadBuffer();
				return numRead == 0;
			}
		}

		public bool CanReadNow
		{
			get
			{
				if (mCharPos < mCharLen)
					return true;
				if (ReadBuffer(true) case .Ok(let count))
					return count > 0;
				return false;
			}
		}

		public Result<void, FileOpenError> Open(StringView fileName)
		{
			Contract.Assert(mStream == null);

			var fileStream = new FileStream();
			Encoding encoding = null;
			Init(fileStream, encoding, true, DefaultFileStreamBufferSize);
			mOwnsStream = true;
			
			if (fileStream.Open(fileName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite) case .Err(let err))
				return .Err(err);

			return .Ok;
		}
		
		public Result<char8> Peek()
		{
			if (mStream == null)
				return .Err;
			if (mCharPos == mCharLen)
			{
				if (Try!(ReadBuffer()) == 0) return .Err;
			}
			return mCharBuffer[mCharPos];
		}

		public Task<String> ReadLineAsync()
		{
		    // If we have been inherited into a subclass, the following implementation could be incorrect
		    // since it does not call through to Read() which a subclass might have overriden.  
		    // To be safe we will only use this implementation in cases where we know it is safe to do so,
		    // and delegate to our base class (which will call into Read) when we are not sure.
			//TODO:
		    //if (this.GetType() != typeof(StreamReader))
		        //return base.ReadLineAsync();

		    //if (mStream == null)
		        //__Error.ReaderClosed();

		    //CheckAsyncTaskInProgress();

		    Task<String> task = ReadLineAsyncInternal();
		    //_asyncReadTask = task;

		    return task;
		}

		class ReadLineTask : Task<String>
		{
			StreamReader mStreamReader;
			WaitEvent mDoneEvent = new WaitEvent() ~ delete _;

			public this(StreamReader streamReader)
			{
				mStreamReader = streamReader;
				ThreadPool.QueueUserWorkItem(new => Proc);
			}

			public ~this()
			{
				//Debug.WriteLine("ReadLineTask ~this waiting {0}", this);
				mDoneEvent.WaitFor();
				//Debug.WriteLine("ReadLineTask ~this done {0}", this);

				delete m_result;
			}

			void Proc()
			{
				//Debug.WriteLine("ReadLineTask Proc start {0}", this);
				m_result = new String();
				var result = mStreamReader.ReadLine(m_result);
				//Debug.WriteLine("ReadLineTask Proc finishing {0}", this);
				Finish(false);
				Ref();
				if (result case .Ok)
					Notify(false);
				mDoneEvent.Set();				
				Deref();
			}
		}

		private Task<String> ReadLineAsyncInternal()
		{
		    /*if (CharPos_Prop == CharLen_Prop && (await ReadBufferAsync().ConfigureAwait(false)) == 0)
		        return null;

		    String sb = null;

		    do
		    {
		        char8[] tmpCharBuffer = CharBuffer_Prop;
		        int tmpCharLen = CharLen_Prop;
		        int tmpCharPos = CharPos_Prop;
		        int i = tmpCharPos;

		        do
		        {
		            char8 ch = tmpCharBuffer[i];

		            // Note the following common line feed chars:
		            // \n - UNIX   \r\n - DOS   \r - Mac
		            if (ch == '\r' || ch == '\n')
		            {
		                String s;

		                if (sb != null)
		                {
		                    sb.Append(tmpCharBuffer, tmpCharPos, i - tmpCharPos);
		                    s = sb;//.ToString();
		                }
		                else
		                {
		                    s = new String(tmpCharBuffer, tmpCharPos, i - tmpCharPos);
		                }

		                CharPos_Prop = tmpCharPos = i + 1;

		                if (ch == '\r' && (tmpCharPos < tmpCharLen || (await ReadBufferAsync().ConfigureAwait(false)) > 0))
		                {
		                    tmpCharPos = CharPos_Prop;
		                    if (CharBuffer_Prop[tmpCharPos] == '\n')
		                        CharPos_Prop = ++tmpCharPos;
		                }

		                return s;
		            }

		            i++;

		        } while (i < tmpCharLen);

		        i = tmpCharLen - tmpCharPos;
		        if (sb == null) sb = new String(i + 80);
		        sb.Append(tmpCharBuffer, tmpCharPos, i);

		    } while (await ReadBufferAsync().ConfigureAwait(false) > 0);

		    return sb.ToString();*/


			ReadLineTask task = new ReadLineTask(this);

			return task;
		}

		public Result<void> ReadToEnd(String outText)
		{
			outText.Reserve((.)mStream.Length + 1);

			while (true)
			{
				Try!(ReadBuffer());
				if (mCharLen == 0)
					break;

				outText.Append(StringView(mCharBuffer, 0, mCharLen));
			}

			return .Ok;
		}

		// Trims the preamble bytes from the byteBuffer. This routine can be called multiple times
		// and we will buffer the bytes read until the preamble is matched or we determine that
		// there is no match. If there is no match, every byte read previously will be available 
		// for further consumption. If there is a match, we will compress the buffer for the 
		// leading preamble bytes
		private bool IsPreamble()
		{
			if (!mCheckPreamble)
				return mCheckPreamble;

		    //Contract.Assert(bytePos <= _preamble.Length, "_compressPreamble was called with the current bytePos greater than the preamble buffer length.  Are two threads using this StreamReader at the same time?");
			int len = (mByteLen >= (mPreamble.Count)) ? (mPreamble.Count - mBytePos) : (mByteLen - mBytePos);

			for (int32 i = 0; i < len; i++,mBytePos++)
			{
				if (mByteBuffer[mBytePos] != mPreamble[mBytePos])
				{
					mBytePos = 0;
					mCheckPreamble = false;
					break;
				}
			}

			Contract.Assert(mBytePos <= mPreamble.Count, "possible bug in _compressPreamble.  Are two threads using this StreamReader at the same time?");

			if (mCheckPreamble)
			{
				if (mBytePos == mPreamble.Count)
				{
		            // We have a match
					CompressBuffer(mPreamble.Count);
					mBytePos = 0;
					mCheckPreamble = false;
					mDetectEncoding = false;
				}
			}

			return mCheckPreamble;
		}

		// Trims n bytes from the front of the buffer.
		private void CompressBuffer(int n)
		{
		    //Contract.Assert(byteLen >= n, "CompressBuffer was called with a number of bytes greater than the current buffer length.  Are two threads using this StreamReader at the same time?");
		    //Buffer.InternalBlockCopy(byteBuffer, n, byteBuffer, 0, byteLen - n);
			mByteBuffer.CopyTo(mByteBuffer, n, 0, mByteLen - n);
			mByteLen -= (int32)n;
		}

		void DetectEncoding()
		{
			mEncoding = Encoding.DetectEncoding(mByteBuffer, var bomSize);
			if (mEncoding != null)
			{
				if (bomSize > 0)
					CompressBuffer(bomSize);
				mDetectEncoding = false;
			}
			else if (mByteLen > 4)
			{
				mDetectEncoding = false;
			}
		}

		protected virtual Result<int> ReadBuffer(bool zeroWait = false)
		{
			mCharLen = 0;
			mCharPos = 0;

			if (!mCheckPreamble)
				mByteLen = 0;
			repeat
			{
				if (mCheckPreamble)
				{
                    //Contract.Assert(bytePos <= _preamble.Length, "possible bug in _compressPreamble.  Are two threads using this StreamReader at the same time?");
					int len = Try!(mStream.TryRead(.(mByteBuffer, mBytePos, mByteBuffer.Count - mBytePos), zeroWait ? 0 : -1));
                    /*switch (mStream.Read(mByteBuffer, mBytePos, mByteBuffer.Length - mBytePos))
					{
					case .Ok(var gotLen):
						len = gotLen;
                        break;
					case .Err: return .Err;
					}*/
                    //Contract.Assert(len >= 0, "Stream.Read returned a negative number!  This is a bug in your stream class.");

					if (len == 0)
					{
                        // EOF but we might have buffered bytes from previous 
                        // attempt to detect preamble that needs to be decoded now
						if (mByteLen > 0)
						{
							//TODO:
                            /*char8Len += decoder.GetChars(byteBuffer, 0, byteLen, char8Buffer, char8Len);

                            // Need to zero out the byteLen after we consume these bytes so that we don't keep infinitely hitting this code path
                            bytePos = byteLen = 0;*/
						}

						return mCharLen - mCharPos;
					}

					mByteLen += len;
				}
				else
				{
                    //Contract.Assert(bytePos == 0, "bytePos can be non zero only when we are trying to _checkPreamble.  Are two threads using this StreamReader at the same time?");

					mByteLen = Try!(mStream.TryRead(.(mByteBuffer, 0, mByteBuffer.Count), zeroWait ? 0 : -1));
					/*switch (mStream.Read(mByteBuffer, 0, mByteBuffer.Length))
					{
					case .Ok(var byteLen):
						mByteLen = byteLen;
                        break;
					case .Err: return .Err;
					}*/
                    //Contract.Assert(byteLen >= 0, "Stream.Read returned a negative number!  This is a bug in your stream class.");

					if (mByteLen == 0)  // We're at EOF
						return mCharLen - mCharPos;
				}

                // _isBlocked == whether we read fewer bytes than we asked for.
                // Note we must check it here because CompressBuffer or 
                // DetectEncoding will change byteLen.
				mIsBlocked = (mByteLen < mByteBuffer.Count);

                // Check for preamble before detect encoding. This is not to override the
                // user suppplied Encoding for the one we implicitly detect. The user could
                // customize the encoding which we will loose, such as ThrowOnError on UTF8
				if (IsPreamble())
					continue;

                // If we're supposed to detect the encoding and haven't done so yet,
                // do it.  Note this may need to be called more than once.
				
                if (mDetectEncoding && mByteLen >= 2)
                    DetectEncoding();
				else if (mEncoding == null)
					mEncoding = Encoding.ASCII;

				switch (mEncoding.DecodeToUTF8(.(mByteBuffer, 0, mByteLen), .(&mCharBuffer[mCharLen], mCharBuffer.Count - mCharLen)))
				{
				case .Ok(let outChars):
					mCharLen += outChars;
					Debug.Assert(outChars <= mCharBuffer.Count);
					break;
				case .Err(let err):
					switch (err)
					{
					case .PartialDecode(let decodedBytes, let outChars):
						//TODO: Handle this partial read...
						Debug.Assert(outChars <= mCharBuffer.Count);
						mCharLen += outChars;
						break;
					default:
					}
				}

				if (mPendingNewlineCheck)
				{
					if (mCharPos == 0 && mCharBuffer[mCharPos] == '\n') mCharPos++;
					mPendingNewlineCheck = false;
				}
			}
			while (mCharLen == mCharPos);

            //Console.WriteLine("ReadBuffer called.  chars: "+char8Len);
			return mCharLen - mCharPos;
		}

		int GetChars(uint8[] byteBuffer, int byteOffset, int byteLength, char8[] char8Buffer, int char8Offset)
		{
			//TODO: This only handles UTF-8 to UTF-8
			for (int32 i = 0; i < byteLength; i++)
				char8Buffer[char8Offset + i] = (char8)byteBuffer[i + byteOffset];
			return byteLength;
		}

		// Reads a line. A line is defined as a sequence of characters followed by
        // a carriage return ('\r'), a line feed ('\n'), or a carriage return
        // immediately followed by a line feed. The resulting string does not
        // contain the terminating carriage return and/or line feed. The returned
        // value is null if the end of the input stream has been reached.
        //
		public Result<void> ReadLine(String strBuffer)
		{
			if (mStream == null)
			{
				return .Err;
                //__Error.ReaderClosed();
			}

#if FEATURE_ASYNC_IO
            CheckAsyncTaskInProgress();
#endif

			if (mCharPos == mCharLen)
			{
				if (Try!(ReadBuffer()) == 0) return .Err;
			}

			repeat
			{
				int i = mCharPos;
				repeat
				{
					char8 ch = mCharBuffer[i];
                    // Note the following common line feed chars:
                    // \n - UNIX   \r\n - DOS   \r - Mac
					if (ch == '\r' || ch == '\n')
					{
						/*String s;
						if (sb != null)
						{
							sb.Append(char8Buffer, char8Pos, i - char8Pos);
							s = sb.ToString();
						}
						else
						{
							s = new String(char8Buffer, char8Pos, i - char8Pos);
						}*/

						strBuffer.Append(mCharBuffer.CArray() + mCharPos, i - mCharPos);

						mCharPos = i + 1;
						if (ch == '\r')
						{
							if (mCharPos < mCharLen)
							{
								if (mCharBuffer[mCharPos] == '\n') mCharPos++;
							}
							else
							{
								mPendingNewlineCheck = true;
							}
						}

						return .Ok;
					}
					i++;
				}
                while (i < mCharLen);
				i = mCharLen - mCharPos;
				//if (sb == null) sb = new StringBuilder(i + 80);
				//sb.Append(char8Buffer, char8Pos, i);
				strBuffer.Append(mCharBuffer.CArray() + mCharPos, i);
			}
            while (Try!(ReadBuffer()) > 0);

			return .Ok;
		}

		public Result<char8> Read()
		{
			if (mStream == null)
				return .Err;
			if (mCharPos == mCharLen)
			{
				if (Try!(ReadBuffer()) == 0) return .Err;
			}
			return mCharBuffer[mCharPos++];
		}

		public struct LineReader : IEnumerator<Result<StringView>>
		{
			StreamReader mStreamReader;
			String mCurrentLine;
			Result<void> mLastReadResult;

			public this(StreamReader streamReader)
			{
				mStreamReader = streamReader;
				mCurrentLine = new String();
				mLastReadResult = default(Result<void>);
			}

			public Result<StringView> Current
			{
				get
				{
					Try!(mLastReadResult);
					return (StringView)mCurrentLine;
				}
			}

			public void Reset()
			{

			}

			public bool MoveNext() mut
			{
				mCurrentLine.Clear();
				mLastReadResult = mStreamReader.ReadLine(mCurrentLine);
				return !(mLastReadResult case .Err);
			}

			public void Dispose() mut
			{
				delete mCurrentLine;
				mCurrentLine = null;
			}

			public Result<Result<StringView>> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
