using System.Threading;
using System;
using System.Collections;
using System.Diagnostics;
using Beefy;

namespace BeefPerf
{
	class BPStateContext
	{
		public enum Event
		{
			case Enter(int64 startTick, int32 strIdx);
			case Leave(int64 endTick);
			case PrevFrameTick(int64 tick);
			case FrameTick(int64 tick);
			case LODSmallEntry(int64 startTick, int64 endTick, int32 stackDepth);
			case Event(int64 tick, char8* name, char8* details);
			case LODEvent(int64 tick, int32 paramsOfs);

			case EndOfStream;
		}

		public BpSession mSession;
		public BpStreamData mStreamData;
		public uint8* mReadPtr;
		public uint8* mReadStart;
		public uint8* mReadEnd;
		public uint8* mParamsPtr;
		public int64 mCurTick;
		public int32 mPendingParamsSize;
		public int32 mSplitCarryoverCount;
		public int mDepth;
		public bool mAutoLeave = true;
		public bool mTimeInferred;

		public this()
		{

		}

		public this(BpSession session, BpStreamData streamData)
		{
			mSession = session;
			mStreamData = streamData;
			mCurTick = streamData.mStartTick;
			if (mStreamData.mBuffer.Count > 0)
				mReadPtr = &mStreamData.mBuffer[0];
			mReadStart = mReadPtr;
			mReadEnd = mReadPtr + mStreamData.mBuffer.Count;
		}

		public int32 ReadPos
		{
			get
			{
				return (int32)(mReadPtr - mReadStart);
			}

			set
			{
				mReadPtr = mReadStart + value;
			}
		}

		[Inline]
		public uint8 Read()
		{
			return *(mReadPtr++);
		}

		public void Read(void* ptr, int32 size)
		{
			Internal.MemCpy(ptr, mReadPtr, size);
			mReadPtr += size;
		}

		public int64 ReadSLEB128()
		{
			int64 value = 0;
			int32 shift = 0;
			int64 curByte;
			repeat
			{
				curByte = *(mReadPtr++);
				value |= ((curByte & 0x7f) << shift);
				shift += 7;
			
			} while (curByte >= 128);
			// Sign extend negative numbers.
			if (((curByte & 0x40) != 0) && (shift < 64))
				value |= ~0L << shift;
			return value;
		}

		void ReadTickDelta()
		{
			int64 tickDelta = ReadSLEB128() * BpClient.cBufTickScale;
			mCurTick += tickDelta;
		}

		public void MoveToParamData()
		{
			if (mPendingParamsSize == -1)
				mPendingParamsSize = (int32)ReadSLEB128();
		}

		public Event GetNextEvent()
		{
			while (true)
			{
				if (mPendingParamsSize == -1)
					mPendingParamsSize = (int32)ReadSLEB128();
				if (mPendingParamsSize != 0)
				{
					Debug.Assert(mPendingParamsSize >= 0);
					mReadPtr += mPendingParamsSize;
					mPendingParamsSize = 0;
				}

				if (mReadPtr == mReadEnd)
				{
					if ((mDepth > 0) && (mAutoLeave))
					{
						--mDepth;
						Debug.Assert(mDepth >= 0); // ??
						mTimeInferred = true;
						return .Leave(mSession.mCurTick);
					}

					return .EndOfStream;
				} 

				BpCmd cmd = (BpCmd)Read();
				switch (cmd)
				{
				case .Enter:
					mDepth++;
					ReadTickDelta();
					int32 zoneNameId = (int32)ReadSLEB128();
					if (zoneNameId < 0)
					{
						mPendingParamsSize = -1;
						mReadPtr += -zoneNameId;
					}
					else
					{
						let zoneName = mSession.mZoneNames[zoneNameId];
						mPendingParamsSize = zoneName.mParamsSize;
					}
					return .Enter(mCurTick, zoneNameId);
				case .Leave:
					mDepth--;
					Debug.Assert(mDepth >= 0);
					ReadTickDelta();
					return .Leave(mCurTick);
				case .LODSmallEntry:
					ReadTickDelta();
					int64 tickStart = mCurTick;
					ReadTickDelta();
					int32 stackDepth = (int32)ReadSLEB128();
					return .LODSmallEntry(tickStart, mCurTick, stackDepth);
				case .FrameTick:
					ReadTickDelta();
					return .FrameTick(mCurTick);
				case .PrevFrameTick:
					ReadTickDelta();
					return .PrevFrameTick(mCurTick);
				case .StreamSplitInfo:
					mSplitCarryoverCount = (int32)ReadSLEB128();
				case .Event:
					ReadTickDelta();
					mParamsPtr = mReadPtr;
					char8* name = (char8*)mReadPtr;
					int32 nameLen = String.StrLen(name);
					mReadPtr += nameLen + 1;
					char8* details = (char8*)mReadPtr;
					int32 detailsLen = String.StrLen(details);
					mReadPtr += detailsLen + 1;
					return .Event(mCurTick, name, details);
				case .LODEvent:
					ReadTickDelta();
					int32 paramsOfs = (int32)ReadSLEB128();
					return .LODEvent(mCurTick, paramsOfs);
				default:
					Runtime.FatalError("Not handled");
				}
			}
		}

		public void FormatStr(int32 paramReadPos, int paramSize, String fmtStr, String outStr)
		{

			int32 prevReadPos = ReadPos;
			ReadPos = paramReadPos;
			defer { ReadPos = prevReadPos; }

			if (paramSize == -1)
				ReadSLEB128();

			for (int idx = 0; idx < fmtStr.Length; idx++)
			{
				let c = fmtStr[idx];
				if (c == '%')
				{
					let cNext = fmtStr[idx + 1];
					idx++;

					if (cNext == '%')
					{
						outStr.Append('%');
					}
					else if (cNext == 'd')
					{
						int32 val = 0;
						Read(&val, 4);
						val.ToString(outStr);
					}
					else if (cNext == 'f')
					{
						float val = 0;
						Read(&val, 4);
						val.ToString(outStr);
					}
					else if (cNext == 's')
					{
						while (true)
						{
							char8 paramC = (char8)Read();
							if (paramC == 0)
								break;
							outStr.Append(paramC);
						}
					}
				}
				else
					outStr.Append(c);
			}
		}
	}

}
