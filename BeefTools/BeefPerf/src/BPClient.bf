#define LINEAR_OPT

using System.Threading;
using System;
using System.Collections;
using System.Diagnostics;
using System.Net;
using Beefy;
using System.IO;

namespace BeefPerf
{
	enum BpCmd : uint8
	{
		Init,
		Enter,
		EnterDyn,
		Leave,
		LODSmallEntry, 
		StrEntry,
		ThreadAdd,
		ThreadRemove,
		SetThread,
		ThreadName,
		FrameTick,
		PrevFrameTick,
		KeepAlive,
		ClockInfo,
		StreamSplitInfo,
		Event,
		LODEvent,
		Cmd,
	}

	class BpStreamData
	{
		public List<uint8> mBuffer ~ delete _;

		public int64 mStartTick;
		public int64 mEndTick;
		public int64 mSplitTick;
		public int64 mSizeOverride = -1; // To reserve size to close out entries
		public int32 mPendingLeaveDepth;
		public int32 mNumZones;
		public int64 mMinTicks;

		public void Write(uint8 val)
		{
			mBuffer.Add(val);
		}

		public void Write(void* ptr, int size)
		{
			int curSize = mBuffer.Count;
			mBuffer.GrowUnitialized((int32)size);
			var destPtr = &mBuffer[curSize];
			Internal.MemCpy(destPtr, ptr, size);
		}

		public void WriteTickDelta(int64 tick)
		{
			int64 encodedDelta = (tick - mEndTick) / BpClient.cBufTickScale;
			WriteSLEB128(encodedDelta);
			mEndTick += encodedDelta * BpClient.cBufTickScale;
		}

		public void WriteSLEB128(int64 value)
		{
			int64 valLeft = value;
			bool hasMore;
			repeat
			{
				uint8 curByte = (uint8)(valLeft & 0x7f);    
				valLeft >>= 7;
				hasMore = !((((valLeft == 0) && ((curByte & 0x40) == 0)) ||
					((valLeft == -1) && ((curByte & 0x40) != 0))));
				if (hasMore)
					curByte |= 0x80;
				mBuffer.Add(curByte);
			}
			while (hasMore);        
		}
	}

	class BpStreamLOD
	{
		struct LODEntry
		{
			public int32 mZoneNameId;
			public int32 mParamsReadPos;
			public int64 mStartTick;
			public bool mWritten;
		}

		struct LODSmallEntry
		{
			public int64 mStartTick;
			public int64 mEndTick;
		}

		public int64 mMinTicks;
		public List<BpStreamData> mStreamDataList = new List<BpStreamData>() ~ DeleteContainerAndItems!(_);

		void FlushSmallEntry(BpStreamData destStreamData, List<LODSmallEntry> smallEntryStack, int32 stackDepth)
		{
			if (stackDepth >= smallEntryStack.Count)
				return;
			var smallEntry = ref smallEntryStack[stackDepth];
			if (smallEntry.mStartTick == 0)
				return;

			destStreamData.Write((uint8)BpCmd.LODSmallEntry);
			destStreamData.WriteTickDelta(smallEntry.mStartTick);
			destStreamData.WriteTickDelta(smallEntry.mEndTick);
			destStreamData.WriteSLEB128(stackDepth);

			smallEntry.mStartTick = 0;
		}

		void FlushLODEntry(BpCmdTarget cmdTarget, BpStreamData srcStreamData, BpStreamData destStreamData, List<LODEntry> entryStack, List<LODSmallEntry> smallEntryList)
		{
			int32 stackDepth;
			for (stackDepth = (int32)entryStack.Count - 1; stackDepth >= 0; stackDepth--)
			{
				var entry = ref entryStack[stackDepth];
				if (entry.mWritten)
					break;
			}
			stackDepth++;

			while (stackDepth < entryStack.Count)
			{
				FlushSmallEntry(destStreamData, smallEntryList, stackDepth);
				var entry = ref entryStack[stackDepth];
				destStreamData.mNumZones++;

				destStreamData.Write((uint8)BpCmd.Enter);
				destStreamData.WriteTickDelta(entry.mStartTick);
				destStreamData.WriteSLEB128(entry.mZoneNameId);

				int32 paramSize = 0;
				
				if (entry.mZoneNameId >= 0)
				{
					let zoneName = cmdTarget.mSession.mZoneNames[entry.mZoneNameId];
					paramSize = zoneName.mParamsSize;
				}
				else if (entry.mZoneNameId < 0)
				{
					int32 nameLen = -entry.mZoneNameId;
					destStreamData.Write(&srcStreamData.mBuffer[entry.mParamsReadPos - nameLen], nameLen);
					paramSize = -1;
				}

				int paramReadPos = entry.mParamsReadPos;
				if (paramSize == -1)
				{
					uint8* paramPtr = &srcStreamData.mBuffer[paramReadPos];
					uint8* readPtr = paramPtr;
					paramSize = (int32)Utils.DecodeInt64(ref readPtr);
					destStreamData.WriteSLEB128(paramSize);
					paramReadPos += readPtr - paramPtr;
				}

				if (paramSize != 0)
				{
					destStreamData.Write(&srcStreamData.mBuffer[paramReadPos], paramSize);
				}

				entry.mWritten = true;
				stackDepth++;
			}
		}

		public BpStreamData DoGenerate(BpCmdTarget cmdTarget, List<BpStreamData> srcStreamDataList, int32 idx, int64 overrideMinTicks = -1)
		{
			int64 minTicks = (overrideMinTicks != -1) ? overrideMinTicks : mMinTicks;
			if (minTicks == -1)
				minTicks = mMinTicks;

			if (idx <= mStreamDataList.Count)
			{
				if (mStreamDataList.Capacity == 0)
					mStreamDataList.Capacity = srcStreamDataList.Count;
				else
					mStreamDataList.EnsureCapacity(srcStreamDataList.Count, true);
				mStreamDataList.GrowUnitialized(idx - (int32)mStreamDataList.Count + 1);
			}

			let tempData = scope List<uint8>(srcStreamDataList.Count);

			let srcStreamData = srcStreamDataList[idx];
			let destStreamData = new BpStreamData();
			destStreamData.mMinTicks = minTicks;
			destStreamData.mBuffer = tempData;
			destStreamData.mStartTick = srcStreamData.mStartTick;
			destStreamData.mEndTick = destStreamData.mStartTick; // Init to startTick for proper encoding of tick deltas
			destStreamData.mSplitTick = srcStreamData.mSplitTick;
			mStreamDataList[idx] = destStreamData;

			BPStateContext stateCtx = scope BPStateContext(cmdTarget.mSession, srcStreamData);
			stateCtx.mAutoLeave = false;

			List<LODEntry> entryStack = scope List<LODEntry>();
			List<LODSmallEntry> smallEntryStack = scope List<LODSmallEntry>();

			int readCount = 0;

			int64 lastEventTick = 0;

			CmdLoop: while (true)
			{
				switch (stateCtx.GetNextEvent())
				{
				case let .Enter(startTick, strIdx):
					readCount++;
					LODEntry entry;
					entry.mStartTick = startTick;
					entry.mZoneNameId = strIdx;
					entry.mParamsReadPos = stateCtx.ReadPos;
					entry.mWritten = false;
					entryStack.Add(entry);
				case let .Leave(endTick):
					int32 stackDepth = (int32)entryStack.Count - 1;
					var entry = ref entryStack[stackDepth];

					int64 ticks = endTick - entry.mStartTick;
					// Keep either long or spanning-from-previous entries
					if ((ticks >= minTicks) || (entry.mStartTick <= srcStreamData.mStartTick))
					{
						FlushLODEntry(cmdTarget, srcStreamData, destStreamData, entryStack, smallEntryStack);
					}
					else
					{
						while (stackDepth >= smallEntryStack.Count)
							smallEntryStack.Add(LODSmallEntry());
						var smallEntry = ref smallEntryStack[stackDepth];
						if (smallEntry.mStartTick != 0)
						{
							int64 spaceTicks = entry.mStartTick - smallEntry.mEndTick;
							Debug.Assert(spaceTicks >= 0);
							if (spaceTicks > minTicks)
							{
                                FlushSmallEntry(destStreamData, smallEntryStack, stackDepth);
							}
						}
						if (smallEntry.mStartTick == 0)
							smallEntry.mStartTick = entry.mStartTick;
						Debug.Assert(endTick >= smallEntry.mEndTick);
						smallEntry.mEndTick = endTick;
					}

					if (entry.mWritten)
					{
						destStreamData.Write((uint8)BpCmd.Leave);
						destStreamData.WriteTickDelta(endTick);

						/*int readPos = stateCtx.ReadPos;
						int writePos = destStreamData.mBuffer.Count;
						Debug.Assert(readPos == writePos);*/
					}

					entryStack.RemoveAt(stackDepth);
				case .EndOfStream:
					break CmdLoop;
				case .FrameTick(int64 tick):
					destStreamData.Write((uint8)BpCmd.FrameTick);
					destStreamData.WriteTickDelta(tick);
				case .PrevFrameTick(int64 tick):
					destStreamData.Write((uint8)BpCmd.PrevFrameTick);
					destStreamData.WriteTickDelta(tick);
				case let .LODSmallEntry(startTick, endTick, stackDepth):
					while (stackDepth >= smallEntryStack.Count)
						smallEntryStack.Add(LODSmallEntry());
					var smallEntry = ref smallEntryStack[stackDepth];
					if (smallEntry.mStartTick != 0)
					{
						int64 spaceTicks = startTick - smallEntry.mEndTick;
						Debug.Assert(spaceTicks >= 0);
						if (spaceTicks > minTicks)
						{
					        FlushSmallEntry(destStreamData, smallEntryStack, stackDepth);
						}
					}
					if (smallEntry.mStartTick == 0)
						smallEntry.mStartTick = startTick;
					Debug.Assert(endTick >= smallEntry.mEndTick);
					smallEntry.mEndTick = endTick;
				case let .Event(tick, name, details):
					if (tick - lastEventTick >= minTicks)
					{
						destStreamData.Write((uint8)BpCmd.LODEvent);
						destStreamData.WriteTickDelta(tick);
						destStreamData.WriteSLEB128(stateCtx.mParamsPtr - stateCtx.mReadStart);
						lastEventTick = tick;
					}
				case let .LODEvent(tick, paramsOfs):
					if (tick - lastEventTick >= minTicks)
					{
						destStreamData.Write((uint8)BpCmd.LODEvent);
						destStreamData.WriteTickDelta(tick);
						destStreamData.WriteSLEB128(paramsOfs);
						lastEventTick = tick;
					}
				default:
					Runtime.FatalError();
				}
			}

			FlushLODEntry(cmdTarget, srcStreamData, destStreamData, entryStack, smallEntryStack);
			for (int32 stackDepth < (int32)smallEntryStack.Count)
				FlushSmallEntry(destStreamData, smallEntryStack, stackDepth);

			int sizeAdd = (int)(srcStreamData.mSizeOverride - srcStreamData.mBuffer.Count);
			int wantSize = destStreamData.mBuffer.Count + sizeAdd;
			destStreamData.mSizeOverride = wantSize;

			let outData = new List<uint8>((int32)wantSize);
			tempData.CopyTo(outData);
			destStreamData.mBuffer = outData;

			// Validate stream
			/*if (true)
			{
				BPStateContext validateStateCtx = scope BPStateContext(cmdTarget.mClient, destStreamData);
				CmdLoop: while (true)
				{
					switch (validateStateCtx.GetNextEvent())
					{
					case .EndOfStream:
						break CmdLoop;
					default:
					}
				}

				//let srcStreamData = srcStreamDataList[idx];
				if (validateStateCtx.mCurTick != destStreamData.mEndTick)
				{
					Runtime.FatalError();
				}
			}*/

			return destStreamData;
		}

		public BpStreamData Generate(BpCmdTarget cmdTarget, List<BpStreamData> srcStreamDataList, int32 idx, int64 overrideMinTicks = -1)
		{
			var streamData = DoGenerate(cmdTarget, srcStreamDataList, idx, overrideMinTicks);

			

			return streamData;
		}
	}

	class BpCmdTarget
	{
		public BpSession mSession;
		public int32 mNativeThreadId;
		public int64 mCreatedTick;
		public int64 mRemoveTick;

		//public List<BPEntry> mEntries = new List<BPEntry>() ~ delete _;
		//public List<int> mStack = new List<int>();

		public List<BpStreamLOD> mStreamLODs = new List<BpStreamLOD>() ~ DeleteContainerAndItems!(_);
		public List<BpStreamData> mStreamDataList = new List<BpStreamData>() ~ DeleteContainerAndItems!(_);
		public BpStreamData mStreamData;

		public List<BPEntry> mEntryStack = new List<BPEntry>() ~ delete _;
		public int64 mLastFrameTick;

		public this(BpSession session, bool defaultInit)
		{
			mSession = session;

			if (defaultInit)
			{
				mStreamData = new BpStreamData();
				mStreamData.mBuffer = new List<uint8>();
				mStreamDataList.Add(mStreamData);

				// Idx 0 is the 'overview'
				var streamLOD = new BpStreamLOD();
				mStreamLODs.Add(streamLOD);

				streamLOD = new BpStreamLOD();
				mStreamLODs.Add(streamLOD);
			}
		}
		
		public void SplitStreamData()
		{
			var prevStreamData = mStreamData;
			prevStreamData.mSplitTick = prevStreamData.mEndTick;
			// Leave room to add the .Leave commands
			prevStreamData.mSizeOverride = prevStreamData.mBuffer.Count + (mEntryStack.Count * (1 + 8+1));

			int64 numTicks = mStreamData.mEndTick - mStreamData.mStartTick;
			int64 avgZoneDist = 0;
			if (mStreamData.mNumZones > 0)
				avgZoneDist = numTicks / mStreamData.mNumZones;
			let streamDataLOD0 = mStreamLODs[0].Generate(this, mStreamDataList, (int32)mStreamDataList.Count - 1, avgZoneDist * 10);

			numTicks = streamDataLOD0.mEndTick - streamDataLOD0.mStartTick;
			avgZoneDist = 0;
			if (streamDataLOD0.mNumZones > 0)
				avgZoneDist = numTicks / streamDataLOD0.mNumZones;
#unwarn
			let streamDataLOD1 = mStreamLODs[1].Generate(this, mStreamLODs[0].mStreamDataList, (int32)mStreamLODs[0].mStreamDataList.Count - 1, avgZoneDist * 10);

			mStreamData = new BpStreamData();
			mStreamDataList.Add(mStreamData);

			// We actually copy the buffer data to place in the old stream data and we keep around the 'scratch buffer'
			//  for the newest stream data.  This is to avoid keeping around unused bytes in a closed buffer
			var oldBuffer = new List<uint8>();
			oldBuffer.Capacity = (int32)prevStreamData.mSizeOverride;
			prevStreamData.mBuffer.CopyTo(oldBuffer);
			mStreamData.mBuffer = prevStreamData.mBuffer;
			mStreamData.mBuffer.Clear();
			prevStreamData.mBuffer = oldBuffer;
			prevStreamData.mPendingLeaveDepth = (int32)mEntryStack.Count;

			mStreamData.mStartTick = prevStreamData.mEndTick;
			mStreamData.mEndTick = prevStreamData.mEndTick;

			if (mEntryStack.Count > 0)
			{
				mStreamData.Write((uint8)BpCmd.StreamSplitInfo);
				mStreamData.WriteSLEB128(mEntryStack.Count);
			}	

			for (var entry in ref mEntryStack)
			{
				mStreamData.Write((uint8)BpCmd.Enter);
				mStreamData.WriteTickDelta(entry.mStartTick);
				mStreamData.WriteSLEB128(entry.mZoneNameId);

				int32 newParamsPos = (int32)mStreamData.mBuffer.Count;

				int32 paramSize = 0;
				if (entry.mZoneNameId < 0)
				{
					paramSize = -1;
					int32 nameLen = -entry.mZoneNameId;
					mStreamData.Write(&prevStreamData.mBuffer[entry.mParamsReadPos - nameLen], nameLen);
				}
				else
				{
					var zoneName = mSession.mZoneNames[entry.mZoneNameId];
					paramSize = zoneName.mParamsSize;
				}
				
				if (paramSize != 0)
				{
					BPStateContext stateCtx = scope BPStateContext(mSession, prevStreamData);
					stateCtx.ReadPos = entry.mParamsReadPos;
					if (paramSize == -1)
					{
						paramSize = (int32)stateCtx.ReadSLEB128();
						mStreamData.WriteSLEB128(paramSize);
					}
					if (paramSize > 0)
					{
						int32 prevSize = (int32)mStreamData.mBuffer.Count;
						mStreamData.mBuffer.GrowUnitialized(paramSize);
						stateCtx.Read(&mStreamData.mBuffer[prevSize], paramSize);
					}
					entry.mParamsReadPos = newParamsPos;
				}
			}

			if (mLastFrameTick != 0)
			{
				mStreamData.Write((uint8)BpCmd.PrevFrameTick);
				mStreamData.WriteTickDelta(mLastFrameTick);
			}
		}
		
		public void MaybeSplitStreamData()
		{
			if (mStreamData.mBuffer.Count >= 64*1024)
			{
				SplitStreamData();

				/*if (mStreamDataList.Count == 2)
				{
					Debug.WriteLine("Split Tick: {0}, BufferSize={1}", mStreamDataList[0].mSplitTick, mStreamDataList[0].mBuffer.Count);
					for (var entry in mEntryStack)
					{
						Debug.WriteLine("mEntryStack[{0}] mStartTick={1}", @entry.Index, entry.mStartTick);
					}
				}*/
			}
		}
	}

	class BpTrack : BpCmdTarget
	{
		public String mName ~ delete _;

		public this(BpSession session) : base(session, false)
		{

		}

		public this(BpSession session, int32 nativeThreadId) : base(session, true)
		{
			mNativeThreadId = nativeThreadId;
		}
		
		public void GetName(String str)
		{
			if (mName != null)
				str.Append(mName);
			else
				str.AppendF("Thread {0}", mNativeThreadId);
		}

		public static int Compare(BpTrack lhs, BpTrack rhs)
		{
			String lhsName = lhs.mName;
			String rhsName = rhs.mName;
			if ((lhsName == null) && (rhsName == null))
				return lhs.mNativeThreadId - rhs.mNativeThreadId;
			if ((lhsName != null) && (rhsName != null))
				return String.Compare(lhsName, rhsName, true);
			if (lhsName == null)
				return String.Compare("Thread ", rhsName, true);
			return String.Compare(lhsName, "Thread ", true);
		}
	}

	class BpZoneName
	{
		public String mName ~ delete _;
		public int32 mParamsSize; // -1 = variable sized (string)
	}

	class BpSession 
	{
		public enum LoadError
		{
			FileNotFound,
			FileError,
			InvalidData,
			InvalidVersion
		}

		public Stream mStream ~ delete _;

		public const int cBufTickScale = 100;
		public const uint32 cFileMarker = 0xBEEF193A;

		public BpCmdTarget mCurCmdTarget;
		public DateTime mConnectTime;
		public DateTime mDisconnectTime;
		public int64 mNumZones;
		public String mClientEnv ~ delete _;
		public String mClientName ~ delete _;
		public String mSessionId ~ delete _;
		public String mSessionName ~ delete _;

		public List<BpTrack> mThreads = new List<BpTrack>() ~ DeleteContainerAndItems!(_);
		public BpCmdTarget mRootCmdTarget ~ delete _;
		public List<BpZoneName> mZoneNames = new List<BpZoneName>() ~ DeleteContainerAndItems!(_);
		public int64 mStatTotalBytesReceived;
		public int64 mFirstTick;
		public int64 mCurTick;

		public int32 mThreadDataVersion;
		public int32 mDispBPS;
		public bool mSessionOver;
		public double mTicksToUSScale;

		/*public int64 mFirstTimeStamp = -1;
		public uint32 mFirstTickMS;*/

		private this()
		{

		}

		public this(bool defaultInit)
		{
			mRootCmdTarget = new BpCmdTarget(this, defaultInit);
		}

		protected void ProcessClientEnv()
		{
			for (var line in mClientEnv.Split('\n'))
			{
				var lineEnum = line.Split('\t');
				if ((StringView key = lineEnum.GetNext()) &&
					(StringView value = lineEnum.GetNext()))
				{
					switch (key)
					{
					case "ClientName": mClientName = new String(value);
					case "SessionId": mSessionId = new String(value);
					case "SessionName": mSessionName = new String(value);
					}
				}
			}
		}

		public double GetTicksToUSScale()
		{
			return mTicksToUSScale;
		}

		public double TicksToUS(int64 ticks)
		{
			return ticks * mTicksToUSScale;
		}

		public void CalcBPS()
		{
			TimeSpan elapsed;
			if (mSessionOver)
				elapsed = mDisconnectTime - mConnectTime;
			else
				elapsed = DateTime.Now - mConnectTime;

			mDispBPS = 0;
			if (elapsed.Milliseconds > 0)
				mDispBPS = (int32)(mStatTotalBytesReceived * 1000 / elapsed.TotalMilliseconds / 1024);
		}

		public void ElapsedTicksToStr(int64 ticks, String str)
		{
			double timeUS = (double)(ticks * GetTicksToUSScale());
			ElapsedTimeToStr(timeUS, str);
		}

		public Result<int64> ParseElapsedTicks(String str)
		{
			double timeUS = 0;

			int numStart = -1;
			int numEnd = -1;
			double curNumber = 0;
			char8 prevChar = 0;
			bool hasNumber = false;
			for (int i < str.Length)
			{
				char8 c = str[i];

				if (((c >= '0') && (c <= '9')) || (c == '.'))
				{
					if (numStart == -1)
						numStart = i;
					numEnd = i;
					hasNumber = true;
					continue;
				}

				if (hasNumber)
				{
					if (float.Parse(scope String(str, numStart, numEnd - numStart + 1)) case .Ok(let num))
						curNumber = num;
					else
						return .Err;
					numStart = -1;
					numEnd = -1;
					hasNumber = false;
				}

				if (c.IsWhiteSpace)
					continue;
				if (c == ':')
					continue;

				if ((c == 'm') || (c == 'u'))
				{
					if (prevChar != 0)
						return .Err;
					prevChar = c;
				}
				else if (c == 's')
				{
					double secScale;
					if (prevChar == 'u')
						secScale = 1.0;
					else if (prevChar == 'm')
						secScale = 0.001;
					else
						secScale = 0.000001;
					timeUS += curNumber * secScale;
					hasNumber = false;
				}
				else // Invalid char
					return .Err;
			}

			// Shouldn't have a leftover non-terminated number
			if (hasNumber)
				return .Err;

			return .Ok((int64)(timeUS / GetTicksToUSScale()));
		}

		public static void ElapsedTimeToStr(double timeUS, String str)
		{
			int64 intTimeUS = (int64)timeUS;

			if (intTimeUS < 1000) // < 1ms
			{
				int32 timeNS = (int32)((timeUS - intTimeUS) * 1000);
				str.AppendF("{0}.{1:00}us", intTimeUS, timeNS / 10);
			}
			else if (intTimeUS < 1000*1000) // < 1s
			{
				str.AppendF("{0}.{1:00}ms", (int32)(intTimeUS / 1000), (int32)((intTimeUS / 10) % 100));
			}
			else if (intTimeUS < 10*1000*1000) // < 10s
			{
				str.AppendF("{0}s:{1:000}.{2:00}ms", (int32)(intTimeUS / 1000 / 1000), (int32)((intTimeUS / 1000) % 1000), (int32)((intTimeUS / 100) % 10));
			}
			else if (intTimeUS < 60*1000*1000) // < 1m
			{
				str.AppendF("{0}s:{1:000}ms", (int32)(intTimeUS / 1000 / 1000), (int32)((intTimeUS / 1000) % 1000));
			}
			else
			{
				str.AppendF("{0}m:{1}s:{2}ms", (int32)(intTimeUS / 60 / 60 / 1000 / 1000), (int32)((intTimeUS / 60 / 1000 / 1000) % 60), (int32)((intTimeUS / 1000) % 60));
			}
		}

		public static void TimeToStr(double timeUS, String str, bool highPrecision = true)
		{
			int64 intTimeUS = (int64)timeUS;
			str.AppendF("{0}:{1:00}:", (int32)((intTimeUS / 60 / 60 / 1000000)), (int32)((intTimeUS / 60 / 1000000) % 60));
			if (highPrecision)
				str.AppendF("{0:00}.{1:000000}", (int32)((intTimeUS / 1000000) % 60), (int32)((intTimeUS % 1000000)));
			else
				str.AppendF("{0:00}.{1:00}", (int32)((intTimeUS / 1000000) % 60), (int32)((intTimeUS / 10000 % 100)));
		}

		public Result<int64> ParseTime(String str)
		{
			double timeUS = 0;

			int numStart = -1;
			int numEnd = -1;
			double curNumber = 0;
			bool hasParsedNumber = false;
			bool hasNumberChars = false;

			List<double> sections = scope List<double>(4);

			for (int i < str.Length)
			{
				char8 c = str[i];
				
				bool ending = false;
				if (((c >= '0') && (c <= '9')) || (c == '.'))
				{
					if (hasParsedNumber)
						return .Err;
					if (numStart == -1)
						numStart = i;
					numEnd = i;
					hasNumberChars = true;

					if (i < str.Length - 1)
						continue;
					ending = true;
				}

				if (hasNumberChars)
				{
					if (double.Parse(scope String(str, numStart, numEnd - numStart + 1)) case .Ok(let num))
						curNumber = num;
					else
						return .Err;
					numStart = -1;
					numEnd = -1;
					hasNumberChars = false;
					hasParsedNumber = true;
				}

				if (ending)
					break;

				if (c.IsWhiteSpace)
					continue;
				if (c == ':')
				{
					if (hasParsedNumber)
					{
						sections.Add(curNumber);
						hasParsedNumber = false;
					}

					continue;
				}
				else
					return .Err;
			}

			double seconds = curNumber;
			if (sections.Count == 2)
			{
				seconds += sections[0]*60*60 + sections[1]*60;
			}
			else
				return .Err;

			timeUS += seconds * 1000000.0;

			return .Ok((int64)(timeUS / GetTicksToUSScale()) + mFirstTick);
		}

		public void TicksToStr(int64 tick, String str)
		{
			TimeToStr((tick - mFirstTick) * GetTicksToUSScale(), str);
		}

		public void ClearData()
		{
			mFirstTick = mCurTick;
			mNumZones = 0;
			ClearCommandTarget(mRootCmdTarget);
			for (var thread in mThreads)
				ClearCommandTarget(thread);
		}

		void ClearCommandTarget(BpCmdTarget cmdTarget)
		{
			cmdTarget.SplitStreamData(); // Start a new, clear streamData
			while (cmdTarget.mStreamDataList.Count > 1)
			{
				var secondToLastIdx = cmdTarget.mStreamDataList.Count - 2;
				delete cmdTarget.mStreamDataList[secondToLastIdx];
				cmdTarget.mStreamDataList.RemoveAt(secondToLastIdx);
			}
			for (var lod in cmdTarget.mStreamLODs)
			{
				ClearAndDeleteItems(lod.mStreamDataList);
			}
			cmdTarget.mEntryStack.Clear();
		}

		public Result<void> Save(StringView filePath)
		{
			var fs = scope FileStream();

			void SaveStreamData(BpStreamData streamData)
			{
				fs.Write((int32)streamData.mBuffer.Count);
				fs.Write((Span<uint8>)streamData.mBuffer);

				fs.Write(streamData.mStartTick);
				fs.Write(streamData.mEndTick);
				fs.Write(streamData.mSplitTick);
				fs.Write(streamData.mSizeOverride);
				fs.Write(streamData.mPendingLeaveDepth);
				fs.Write(streamData.mNumZones);
				fs.Write(streamData.mMinTicks);
			}

			void SaveCmdTarget(BpCmdTarget cmdTarget)
			{
				fs.Write(cmdTarget.mNativeThreadId);
				fs.Write(cmdTarget.mCreatedTick);
				fs.Write(cmdTarget.mRemoveTick);

				fs.Write((int32)cmdTarget.mStreamLODs.Count);
				for (var streamLod in cmdTarget.mStreamLODs)
				{
					fs.Write(streamLod.mMinTicks);
					fs.Write((int32)streamLod.mStreamDataList.Count);
					for (var streamData in streamLod.mStreamDataList)
						SaveStreamData(streamData);
				}

				fs.Write((int32)cmdTarget.mStreamDataList.Count);
				for (var streamData in cmdTarget.mStreamDataList)
					SaveStreamData(streamData);

				fs.Write((int32)cmdTarget.mEntryStack.Count);
				if (!cmdTarget.mEntryStack.IsEmpty)
					fs.Write(Span<uint8>((uint8*)&cmdTarget.mEntryStack[0], cmdTarget.mEntryStack.Count * sizeof(BPEntry)));

				fs.Write(cmdTarget.mLastFrameTick);
			}

			switch (fs.Create(filePath))
			{
			case .Ok:
			case .Err:
				return .Err;
			}

			fs.Write((int32)cFileMarker);
			fs.Write((int32)2); // Version

			fs.Write(mConnectTime.ToBinaryRaw());
			fs.Write(mDisconnectTime.ToBinaryRaw());
			fs.Write(mNumZones);
			fs.WriteStrSized32(mClientEnv);

			fs.Write((int32)mThreads.Count);
			for (var thread in mThreads)
			{
				fs.WriteStrSized32(thread.mName ?? "");
				SaveCmdTarget(thread);
			}
			SaveCmdTarget(mRootCmdTarget);
			fs.Write((int32)mZoneNames.Count);
			for (var cmdName in mZoneNames)
			{
				fs.WriteStrSized32(cmdName.mName);
				fs.Write(cmdName.mParamsSize);
			}
			fs.Write(mStatTotalBytesReceived);
			fs.Write(mFirstTick);
			fs.Write(mCurTick);
			fs.Write(mClientEnv);

			return .Ok;
		}

		public Result<void, LoadError> Load(StringView filePath)
		{
			var fs = scope FileStream();
			bool readFailed = false;

			switch (fs.Open(filePath, .Read))
			{
			case .Ok:
			case .Err(let err):
				switch (err)
				{
				case .NotFound:
					return .Err(.FileNotFound);
				default:
					return .Err(.FileError);
				}
			}

			T Read<T>() where T : struct
			{
				switch (fs.Read<T>())
				{
				case .Ok(let setVal):
					return setVal;
				case .Err:
					readFailed = true;
				}
				return default;
			}

			void Read<T>(out T val) where T : struct
			{
				switch (fs.Read<T>())
				{
				case .Ok(let setVal):
					val = setVal;
					return;
				case .Err:
					val = default;
					readFailed = true;
				}
			}

			void ReadStrSized32(String s)
			{
				switch (fs.Read<int32>())
				{
				case .Ok(let len):
					char8* ptr = s.PrepareBuffer(len);
					switch (fs.TryRead(Span<uint8>((uint8*)ptr, len)))
					{
					case .Ok:
					case .Err:
						readFailed = true;
						return;
					}
				case .Err:
					readFailed = true;
				}
			}

			mixin LoadTry(var val)
			{
				if (val case .Err(var err))
					return .Err(.InvalidData);
				val.Get()
			}

			void Read(Span<uint8> data)
			{
				if (fs.TryRead(data) case .Err)
					readFailed = true;
			}

			void ReadStreamData(BpStreamData streamData)
			{
				int dataSize = Read<int32>();
				streamData.mBuffer = new List<uint8>();
				streamData.mBuffer.GrowUnitialized(dataSize);
				Read(streamData.mBuffer);

				Read(out streamData.mStartTick);
				Read(out streamData.mEndTick);
				Read(out streamData.mSplitTick);
				Read(out streamData.mSizeOverride);
				Read(out streamData.mPendingLeaveDepth);
				Read(out streamData.mNumZones);
				Read(out streamData.mMinTicks);
			}

			void ReadCmdTarget(BpCmdTarget cmdTarget)
			{
				Read(out cmdTarget.mNativeThreadId);
				Read(out cmdTarget.mCreatedTick);
				Read(out cmdTarget.mRemoveTick);

				int lodCount = Read<int32>();
				for (int lodIdx < lodCount)
				{
					var streamLod = new BpStreamLOD();
					cmdTarget.mStreamLODs.Add(streamLod);
					Read(out streamLod.mMinTicks);
					int dataCount = Read<int32>();
					for (int dataIdx < dataCount)
					{
						var streamData = new BpStreamData();
						ReadStreamData(streamData);
						streamLod.mStreamDataList.Add(streamData);
					}
				}

				int dataCount = Read<int32>();
				for (int dataIdx < dataCount)
				{
					var streamData = new BpStreamData();
					ReadStreamData(streamData);
					cmdTarget.mStreamDataList.Add(streamData);
				}

				int stackSize = Read<int32>();
				if (stackSize > 0)
				{
					cmdTarget.mEntryStack.GrowUnitialized(stackSize);
					Read(Span<uint8>((uint8*)&cmdTarget.mEntryStack[0], cmdTarget.mEntryStack.Count * sizeof(BPEntry)));
				}	
				
				Read(out cmdTarget.mLastFrameTick);
			}

			uint32 fileMarker = Read<uint32>();
			if (fileMarker != cFileMarker)
				return .Err(.InvalidData);
			int32 version = Read<int32>();
			if ((version < 1) || (version > 2))
				return .Err(.InvalidVersion);
			mConnectTime = LoadTry!(DateTime.FromBinaryRaw(Read<int64>()));
			mDisconnectTime = LoadTry!(DateTime.FromBinaryRaw(Read<int64>()));
			Read(out mNumZones);
			mClientEnv = new String();
			ReadStrSized32(mClientEnv);
			ProcessClientEnv();

			int threadCount = Read<int32>();
			for (int threadIdx < threadCount)
			{
				var thread = new BpTrack(this);
				thread.mName = new String();
				ReadStrSized32(thread.mName);
				if (thread.mName.IsEmpty)
				{
					DeleteAndNullify!(thread.mName);
				}
				ReadCmdTarget(thread);
				mThreads.Add(thread);
			}
			ReadCmdTarget(mRootCmdTarget);
			int zoneCount = Read<int32>();
			for (int zoneIdx < zoneCount)
			{
				var zoneName = new BpZoneName();
				zoneName.mName = new String();
				ReadStrSized32(zoneName.mName);
				zoneName.mParamsSize = Read<int32>();
				mZoneNames.Add(zoneName);
			}
			Read(out mStatTotalBytesReceived);
			Read(out mFirstTick);
			Read(out mCurTick);
			if (version > 1)
				Read(out mTicksToUSScale);
			else
				mTicksToUSScale = 1 / 4500.0;

			if (fs.Position != fs.Length)
				return .Err(.InvalidData);

			if (readFailed)
				return .Err(.InvalidData);

			mThreadDataVersion = 1;
			mSessionOver = true;
			CalcBPS();

			return .Ok;
		}
	}

	class BpClient : BpSession
	{
		public Socket mSocket ~ delete _;
		public Monitor mDataMonitor = new Monitor() ~ delete _;
		public CircularBuffer mCircularBuffer = new CircularBuffer() ~ delete _;
		public int32 mBlockRecvLeft;
		public int32 mCurBlockSize;
		public bool mGettingBlockSize = true;
		public ProfileInstance mProfileId;
		public int32 mValidDataSize;
		
		public int32 mUpdateCnt;
		public bool mSessionInitialized;

		public this() : base(true)
		{
			//mCircularBuffer.Resize(128*1024*1024);

			mCurCmdTarget = mRootCmdTarget;
		}

		public ~this()
		{
			if (mProfileId != 0)
				mProfileId.Dispose();
		}

		public bool IsSessionInitialized
		{
			get
			{
				return mSessionInitialized;
			}
		}

		public void TryRecv()
		{
			if (!mSocket.IsOpen)
			{
				return;
			}

			if (mSessionOver)
			{
				if (mProfileId != 0)
				{
					mProfileId.Dispose();
					mProfileId = 0;
				}
				mSocket.Close();
				return;
			}

			let recvSize = 0x10000;
			uint8* data = scope uint8[recvSize]*;

			while (true)												   
			{
				if (mGettingBlockSize)
				{
					int32 wantsRecvSize;
					using (mDataMonitor.Enter())
					{
						int32 extraData = mCircularBuffer.GetSize() - mValidDataSize;
						wantsRecvSize = 4 - extraData;
					}
					int32 result = -1;
					if (mSocket.Recv(data, wantsRecvSize) case .Ok(let recvBytes))
					{
						result = (int32)recvBytes;
					}
					else
					{
						if (!mSocket.IsConnected)
						{
							Disconnect();
						}
					}
					using (mDataMonitor.Enter())
					{
						if (result > 0)
						{
							int32 pos = mCircularBuffer.GetSize();
							mCircularBuffer.Grow(result);
							mCircularBuffer.Write(data, pos, result);
							gApp.ReportBytesReceived(result);
							mStatTotalBytesReceived += result;
						}
	
						if (result == wantsRecvSize)
						{
							mCircularBuffer.Read(&mCurBlockSize, mValidDataSize, 4);
							mBlockRecvLeft = mCurBlockSize;
							mCircularBuffer.RemoveBack(4);

#if LINEAR_OPT
							mCircularBuffer.EnsureSpan(mBlockRecvLeft);
#endif							
							mGettingBlockSize = false;

							//Debug.WriteLine("Recv block size {0}", mCurBlockSize);
							Debug.Assert(mCurBlockSize < 32*1024*1024); // Just ensure it's reasonable
						}
					}
				}
	
				if (mBlockRecvLeft <= 0)
					break;

				//Thread.Sleep(1);

				int32 tryRecvSize = (int32)Math.Min(mBlockRecvLeft, recvSize);
				int32 result = -1;
				if (mSocket.Recv(data, tryRecvSize) case .Ok(let recvBytes))
				{
					result = (int32)recvBytes;
				}
				if (result <= 0)
				{
					if (!mSocket.IsConnected)
					{
						Disconnect();
					}
					break;
				}
				
				gApp.ReportBytesReceived(result);
				using (mDataMonitor.Enter())
				{
					mStatTotalBytesReceived += result;
					int32 curSize = mCircularBuffer.GetSize();
					mCircularBuffer.Grow(result);
					mCircularBuffer.Write(data, curSize, result);
					mBlockRecvLeft -= result;
					if (mBlockRecvLeft == 0)
					{
	                    mValidDataSize += mCurBlockSize;
						mCurBlockSize = 0;
						mGettingBlockSize = true;
					}
				}
			}
		}

		void ReadStr(String str)
		{
			while (true)
			{
				char8 c = (char8)mCircularBuffer.Read();
				if (c == 0)
					return;
				str.Append(c);
			}
		}

		int mReadIdx = 0;
		int mFrameTickCount = 0;

		void ReadTickDelta()
		{
			int64 tickDelta = mCircularBuffer.ReadSLEB128() * cBufTickScale;
			mCurTick += tickDelta;
		}

		void Disconnect()
		{
			if (mSessionOver)
				return;

			mDisconnectTime = DateTime.Now;
			mSessionOver = true;
			CalcBPS();
		}

		void PopCurEntry()
		{
			mCurCmdTarget.mStreamData.Write((uint8)BpCmd.Leave);
			mCurCmdTarget.mStreamData.WriteTickDelta(mCurTick);

			var entry = mCurCmdTarget.mEntryStack.PopBack();
			int32 stackDepth = (int32)mCurCmdTarget.mEntryStack.Count;

			if (entry.mStartTick <= mCurCmdTarget.mStreamData.mStartTick)
			{
				// This spans back
				for (int checkStreamIdx = mCurCmdTarget.mStreamDataList.Count - 2; checkStreamIdx >= 0; checkStreamIdx--)
				{
					var streamData = mCurCmdTarget.mStreamDataList[checkStreamIdx];
					if (entry.mStartTick > streamData.mSplitTick)
						break; // Not applicable this far back

					// Is this the Leave we've been waiting for?
					if (streamData.mPendingLeaveDepth != stackDepth + 1)
						continue;
					streamData.mPendingLeaveDepth--;

					/*if (checkStreamIdx == 0)
					{
						Debug.WriteLine("Back-propagating Leaving {0} -> {1}", entry.mStartTick, mCurTick);
					}*/

					streamData.Write((uint8)BpCmd.Leave);
					streamData.WriteTickDelta(mCurTick);
					Debug.Assert(streamData.mBuffer.Count <= streamData.mSizeOverride);

					for (var lod in mCurCmdTarget.mStreamLODs)
					{
						if (checkStreamIdx < lod.mStreamDataList.Count)
						{
							var lodStreamData = lod.mStreamDataList[checkStreamIdx];
							if (lodStreamData != null)
							{
								lodStreamData.Write((uint8)BpCmd.Leave);
								lodStreamData.WriteTickDelta(mCurTick);
								Debug.Assert(lodStreamData.mBuffer.Count <= lodStreamData.mSizeOverride);
							}
						}
					}
				}
			}

			if ((mCurCmdTarget.mEntryStack.Count == 1) && (mCurCmdTarget.mStreamDataList.Count <= 2))
			{
				//cmdTarget.SplitStreamData();
			}

		}

		void ProcessData()
		{
			if (mSessionOver)
				return;

			Debug.Assert(mValidDataSize <= mCircularBuffer.GetSize());

			String tempStr = scope String(64);

			int readIdx = 0;
			
			BpCmd prevCmd = default;

			//return;

			while (mValidDataSize > 0)
			{
				mReadIdx++;
				readIdx++;

				int32 startSize = mCircularBuffer.GetSize();
				uint8* curPtr = mCircularBuffer.GetPtr();

				uint8 Read()
				{
					return *(curPtr++);
				}

				void ReadStr(String str)
				{
					uint8* ptr = curPtr;
					while (*ptr != 0)
					{
						ptr++;
					}

					str.Append((char8*)curPtr, ptr - curPtr);
					curPtr = ptr + 1;
				}

				int64 ReadSLEB128()
				{
					int64 value = 0;
					int32 shift = 0;
					int64 curByte;
					repeat
					{
						curByte = *(curPtr++);
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
					int64 tickDelta = ReadSLEB128() * cBufTickScale;
					mCurTick += tickDelta;
				}

				BpCmd cmd = (BpCmd)Read();
				switch (cmd)
				{
				case .Init:
					int32 version = (int32)ReadSLEB128();
					if (version < 2)
					{
						Disconnect();
						return;
					}
					Debug.Assert(version == 2);
					Debug.Assert(mClientName == null);
					mClientEnv = new String();
					ReadStr(mClientEnv);
					ProcessClientEnv();

					if (!gApp.mWorkspacePanel.PassesFilter(this))
					{
						Disconnect();
						break;
					}

					mSessionInitialized = true;
					gApp.SessionInitialized(this);
				case .StrEntry:
					ReadStr(tempStr);
					BpZoneName zoneName = new BpZoneName();
					zoneName.mName = new String(tempStr);
					bool isDyn = false;
					for (int idx = 0; idx < zoneName.mName.Length; idx++)
					{
						let c = zoneName.mName[idx];
						if (c == '%')
						{
							let cNext = zoneName.mName[idx + 1];
							idx++;

							if (cNext == 'd')
								zoneName.mParamsSize += 4;
							else if (cNext == 'f')
								zoneName.mParamsSize += 4;
							else if (cNext == 's')
								isDyn = true;
						}
					}
					if (isDyn)
						zoneName.mParamsSize = -1;
					mZoneNames.Add(zoneName);
					tempStr.Clear();
				case .Enter:
					mNumZones++;
					ReadTickDelta();
					int32 zoneNameId = (int32)ReadSLEB128();

					BPEntry entry;
					entry.mStartTick = mCurTick;
					entry.mZoneNameId = zoneNameId;

					int32 paramSize = 0;
					if (zoneNameId >= 0)
					{
						let zoneName = mZoneNames[zoneNameId];
						paramSize = zoneName.mParamsSize;
						/*if (paramSize != 0)
							entry.mParamsReadPos = cmdTarget.mStreamData.mBuffer.Count;
						else
							entry.mParamsReadPos = 0;*/
					}
					else
					{
						paramSize = -1;
						//entry.mParamsReadPos = cmdTarget.mStreamData.mBuffer.Count;
					}

					mCurCmdTarget.mStreamData.mNumZones++;
					mCurCmdTarget.mStreamData.Write((uint8)cmd);
					mCurCmdTarget.mStreamData.WriteTickDelta(mCurTick);
					mCurCmdTarget.mStreamData.WriteSLEB128(zoneNameId);
					if (zoneNameId < 0)
					{
						int32 strLen = -zoneNameId;
						//var view = scope CircularBuffer.View();
						/*mCircularBuffer.MapView(0, strLen, view);
						mCurCmdTarget.mStreamData.Write(view.mPtr, strLen);
						mCircularBuffer.RemoveFront(strLen);*/
						mCurCmdTarget.mStreamData.Write(curPtr, strLen);
						curPtr += strLen;
					}

					entry.mParamsReadPos = (int32)mCurCmdTarget.mStreamData.mBuffer.Count;
					mCurCmdTarget.mEntryStack.Add(entry);

					if (paramSize == -1)
					{
						paramSize = (int32)ReadSLEB128();
						mCurCmdTarget.mStreamData.WriteSLEB128(paramSize);
					}

					if (paramSize != 0)
					{
						/*var view = scope CircularBuffer.View();
						mCircularBuffer.MapView(0, paramSize, view);
						mCurCmdTarget.mStreamData.Write(view.mPtr, paramSize);
						mCircularBuffer.RemoveFront(paramSize);*/
						mCurCmdTarget.mStreamData.Write(curPtr, paramSize);
						curPtr += paramSize;
					}
					
				case .Leave:
					ReadTickDelta();
					
					// This can happen when we Init after some entries are already in progress
					if (mCurCmdTarget.mEntryStack.Count == 0)
						break;

					PopCurEntry();
				case .SetThread:
					int64 threadNum = ReadSLEB128();
					if (threadNum == -1)
						mCurCmdTarget = mRootCmdTarget;
					else
						mCurCmdTarget = mThreads[(int)threadNum];
				case .ThreadAdd:
					ReadTickDelta();
					int32 threadId = (int32)ReadSLEB128();
					int32 nativeThreadId = (int32)ReadSLEB128();
					BpTrack newThread = new BpTrack(this, threadId);
					newThread.mCreatedTick = mCurTick;
					newThread.mStreamData.mStartTick = mCurTick;
					newThread.mStreamData.mEndTick = mCurTick;
					newThread.mNativeThreadId = nativeThreadId;
					while (threadId >= mThreads.Count)
						mThreads.Add(null);
					mThreads[threadId] = newThread;
					mThreadDataVersion++;
				case .ThreadRemove:
					ReadTickDelta();
					mCurCmdTarget.mRemoveTick = mCurTick;
					while (mCurCmdTarget.mEntryStack.Count > 0)
						PopCurEntry();
				case .ThreadName:
					var thread = (BpTrack)mCurCmdTarget;
					if (thread.mName == null)
						thread.mName = new String();
					ReadStr(thread.mName);
					mThreadDataVersion++;
				case .FrameTick:
					ReadTickDelta();
					mCurCmdTarget.mStreamData.Write((uint8)cmd);
					mCurCmdTarget.mStreamData.WriteTickDelta(mCurTick);
					mCurCmdTarget.mLastFrameTick = mCurTick;

					mFrameTickCount++;
				case .KeepAlive:
					ReadTickDelta();
				case .ClockInfo:
					int64 curTick = ReadSLEB128() * 100;
					Debug.Assert(curTick == mCurTick);
#unwarn
					int64 timeStampNow = ReadSLEB128() * 100;
#unwarn
					uint32 tickMSNow = (.)ReadSLEB128();
					int64 clockRate = (.)ReadSLEB128();

					mTicksToUSScale = 1000000.0 / clockRate;

					/*if (mFirstTimeStamp == -1)
					{
						mFirstTimeStamp = timeStampNow;
						mFirstTickMS = tickMSNow;
					}
					else
					{
						int64 deltaMS = (.)(tickMSNow - mFirstTickMS);
						int64 deltaTimeStamp = timeStampNow - mFirstTimeStamp;
						if (deltaMS > 100)
						{

						}
					}*/

				case .Event:
					ReadTickDelta();
					var nameStr = scope String(64);
					var detailsStr = scope String(256);
					ReadStr(nameStr);
					ReadStr(detailsStr);

					mCurCmdTarget.mStreamData.Write((uint8)cmd);
					mCurCmdTarget.mStreamData.WriteTickDelta(mCurTick);
					mCurCmdTarget.mStreamData.Write(nameStr.CStr(), nameStr.Length + 1);
					mCurCmdTarget.mStreamData.Write(detailsStr.CStr(), detailsStr.Length + 1);
				case .Cmd:
					var cmdStr = scope String();
					ReadStr(cmdStr);
					bool success = gApp.HandleClientCommand(this, cmdStr);
					int result = success ? 1 : 0;
					mSocket.Send(&result, 1).IgnoreError();
				default:
					Runtime.FatalError("Invalid stream byte");
				}
				prevCmd = cmd;

				mCircularBuffer.SetPtr(curPtr);
				
				if (mCurCmdTarget != null)
					mCurCmdTarget.MaybeSplitStreamData();

				if ((mFirstTick == 0) && (mCurTick != 0))
					mFirstTick = mCurTick;

				mValidDataSize -= (startSize - mCircularBuffer.GetSize());
			}
		}

		static int64 sDbgTick = 0;

		public void Update()
		{
			mUpdateCnt++;

			using (mDataMonitor.Enter())
			{
				ProcessData();
			}

			if (gApp.mUpdateCnt % 20 == 0)
				CalcBPS();
		}
	}
}
