using System;
using System.Diagnostics;
using System.Collections.Generic;

namespace BeefPerf
{
	class CircularBuffer
	{
		public class View
		{
			public uint8* mPtr;			
			public int32 mSrcIdx;
			public int32 mSrcSize;
			public uint8* mTempBuf ~ delete _;
			public int32 mTempBufSize;
		
			public CircularBuffer mCircularBuffer;
		
			//void Commit(int size = -1);
		};

		uint8* mBuffer;	
		int32 mTail;
		int32 mDataSize;
		int32 mBufSize;

		public this()
		{	
			mTail = 0;
			mBufSize = 0;
			mDataSize = 0;
			mBuffer = null;
		}
		
		public ~this()
		{
			delete mBuffer;
		}
		
		public void Resize(int32 newSize)
		{	
			uint8* newBuffer = new uint8[newSize]*;
			Read(newBuffer, 0, mDataSize);
		
			delete mBuffer;
			mBuffer = newBuffer;
			mBufSize = newSize;
			mTail = 0;		
		}
		
		public void GrowReserve(int32 addSize)
		{	
			if (mDataSize + addSize <= mBufSize)
				return;
			Debug.WriteLine("GrowReserve");
			Resize(Math.Max(mDataSize + addSize, mDataSize + mDataSize/2));
		}
		
		public void Grow(int32 addSize)
		{	
			GrowReserve(addSize);
			mDataSize += addSize;
		}

		/// This ensures that we have a span that does not wrap around. This ensures we can decode
		/// an entire block without wrap checks.
		public void EnsureSpan(int32 spanSize)
		{
			//Debug.WriteLine("Span Size:{0} Tail:{1} DataSize:{2} BufSize:{3}", spanSize, mTail, mDataSize, mBufSize);

			if (mDataSize + spanSize > mBufSize)
			{
				int32 newSize = (int32)Math.Max(mDataSize + spanSize, (int32)mBufSize * 1.5);
				//Debug.WriteLine("Resizing {0}", newSize);
				Resize(newSize);
				return;
			}

			int head = mTail + mDataSize;
			if (head + spanSize > mBufSize)
			{
				// If we are sufficiently full then keep the buffer at the same size
				if (mDataSize + spanSize < mBufSize / 2)
				{
					// If our buffer doesn't wrap around then we can just pull back inplace
					if (mTail + mDataSize <= mBufSize)
					{
						if (mTail != 0)
						{
							//Debug.WriteLine("MemMove");
							Internal.MemMove(mBuffer, mBuffer + mTail, mDataSize);
							mTail = 0;
						}
						return;
					}
					//Debug.WriteLine("MakeLinear");
					MakeLinear();
					return;
				}

				int32 newSize = (int32)Math.Max(mDataSize + spanSize, (int32)mBufSize * 1.5);
				//Debug.WriteLine("Resizing2 {0}", newSize);
				Resize(newSize);
				return;
			}

			/*if (mTail + spanSize > mBufSize)
			{
				/*Debug.WriteLine("EnsureSpan {0} {1} {2} {3}", mTail, spanSize, mBufSize, mDataSize);

				// If we are sufficiently full then keep the buffer at the same size
				if (mDataSize + spanSize < mBufSize / 2)
				{
					// If our buffer doesn't wrap around then we can just pull back inplace
					if (mTail + mDataSize <= mBufSize)
					{
						if (mTail != 0)
						{
							Internal.MemMove(mBuffer, mBuffer + mTail, mDataSize);
							mTail = 0;
						}
						return;
					}
					MakeLinear();
					return;
				}*/

				int32 newBufSize = (int32)((mTail + spanSize) * 1.5);
				Resize(newBufSize);
			}*/
		}

		public void MakeLinear()
		{
			if (mDataSize == 0)
			{
				mTail = 0;
				return;
			}
			Resize(mBufSize);
		}
		
		public void GrowFront(int32 addSize)
		{
			if (mDataSize + addSize > mBufSize)
			{
				Resize(mDataSize + addSize);
			}
			mDataSize += addSize;
			mTail = (mTail + mBufSize - addSize) % mBufSize;
		}
		
		public int32 GetSize()
		{
			return mDataSize;
		}

		public int32 GetBufSize()
		{
			return mBufSize;
		}

		public uint8* GetPtr()
		{
			return mBuffer + mTail;
		}

		public void SetPtr(uint8* ptr)
		{
			int32 newTail = (int32)(ptr - mBuffer);
			mDataSize -= newTail - mTail;
			Debug.Assert(mDataSize >= 0);
			if (newTail == mBufSize)
				newTail = 0;
			mTail = newTail;
			Debug.Assert(mTail < mBufSize);
		}
		
		public void MapView(int32 idx, int32 len, View view)
		{
			view.mCircularBuffer = this;
			view.mSrcIdx = idx;
			view.mSrcSize = len;
			if (mTail + idx + len <= mBufSize)
			{
				view.mPtr = mBuffer + mTail + idx;
			}
			else
			{
				if (view.mTempBufSize < len)
				{
					delete view.mTempBuf;
					view.mTempBuf = new uint8[len]*;
					view.mTempBufSize = len;
				}
				view.mPtr = view.mTempBuf;		
				Read(view.mTempBuf, idx, len);
			}
		}
		
		public void Read(void* ptr, int32 idx, int32 len)
		{
			Debug.Assert(len <= mBufSize);
		
			if (len == 0)
				return;
		
			int absIdx = (mTail + idx) % mBufSize;
			if (absIdx + len > mBufSize)
			{		
				int lowSize = mBufSize - absIdx;
				Internal.MemCpy(ptr, mBuffer + absIdx, lowSize);
				Internal.MemCpy((uint8*)ptr + lowSize, mBuffer, len - lowSize);
			}
			else
			{
				Internal.MemCpy(ptr, mBuffer + absIdx, len);
			}
		}

		public uint8 Read()
		{
			uint8 val = mBuffer[mTail];
			mTail = (mTail + 1) % mBufSize;
			mDataSize--;
			return val;
		}

		public int64 ReadSLEB128()
		{
			int64 value = 0;
			int32 shift = 0;
			int64 curByte;
			repeat
			{
				curByte = Read();
				value |= ((curByte & 0x7f) << shift);
				shift += 7;
			
			} while (curByte >= 128);
			// Sign extend negative numbers.
			if (((curByte & 0x40) != 0) && (shift < 64))
				value |= ~0L << shift;
			return value;
		}
		
		public void Write(void* ptr, int32 idx, int32 len)
		{
			Debug.Assert(len <= mBufSize);
		
			if (len == 0)
				return;
		
			int absIdx = (mTail + idx) % mBufSize;
			if (absIdx + len > mBufSize)
			{		
				int lowSize = mBufSize - absIdx;
				Internal.MemCpy(mBuffer + absIdx, ptr, lowSize);
				Internal.MemCpy(mBuffer, (uint8*)ptr + lowSize, len - lowSize);
			}
			else
			{
				Internal.MemCpy(mBuffer + absIdx, ptr, len);
			}
		}
		
		public void RemoveFront(int32 len)
		{
			mTail = (mTail + len) % mBufSize;
			mDataSize -= len;
		}

		public void RemoveBack(int32 len)
		{
			mDataSize -= len;
		}
	}
}
