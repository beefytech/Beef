using System;
using System.Collections;
using System.Diagnostics;
using Beefy.utils;

namespace BeefPerf
{
	// Headless equivalents of the Find and Profile panels, plus a cross-thread "time slice" query.
	//
	// The one thing to understand before touching any of this is how a zone that outlives a stream
	// buffer is stored. When a buffer is split (BpCmdTarget.SplitStreamData), every entry still open is
	// re-emitted as an .Enter at the head of the next buffer, carrying its original start tick. Then,
	// when it finally closes, BpClient.PopCurEntry walks *backwards* and writes the .Leave into every
	// buffer the entry spanned. So a zone covering 109 buffers is genuinely opened and closed in all
	// 109 of them, each time with identical start and end ticks -- a naive scan reports it 109 times.
	//
	// IsDuplicateSpan is the filter for that: count an entry only in the buffer where it began. The
	// exception is the first buffer a scan looks at, which is also where an entry that started before
	// the scan window has to be picked up, since the buffer it really began in is outside the window.
	// That is the same isFirstDrawn/isOld pair the Profile panel uses, and it is load-bearing.
	//
	// These do run with BPStateContext.mAutoLeave off, but that buys something much smaller than the
	// above: it only suppresses the synthetic .Leave at a buffer's end for zones still open at the live
	// edge of a running capture. Those are reported by the panels with an inferred end tick; a query
	// would rather omit them than report a duration that has not happened yet.
	static class BPQuery
	{
		// True if this closing entry is a continuation of one that began in an earlier buffer, and so
		// has already been counted there. isFirstBuffer suppresses the filter for the first buffer of a
		// scan, where a continuation is the only form an earlier-started entry can take.
		//
		// depth is the entry's own stack depth (ie after it has been popped), and the carryover count
		// is how many entries the buffer re-opened at its head, so depth < carryover means "this is one
		// of the re-opened ones". The start tick test guards against a fresh entry landing at one of
		// those depths after a carried-over sibling closed.
		public static bool IsDuplicateSpan(BPStateContext stateCtx, BpStreamData streamData, int64 entryStartTick, int32 depth, bool isFirstBuffer)
		{
			if (isFirstBuffer)
				return false;
			return (entryStartTick <= streamData.mStartTick) && (depth < stateCtx.mSplitCarryoverCount);
		}

		// Resolves the display name of a zone entry, matching what the Find/Profile panels show.
		// A negative zone name id means the name is a dynamic string stored inline just before the
		// params; otherwise it indexes the session's zone name table and may be a printf-style format
		// string that the params complete.
		public static void GetEntryName(BpSession session, BPStateContext stateCtx, int32 zoneNameId, int32 paramsReadPos, bool formatStrings, String outStr)
		{
			int32 paramsSize;
			String fmtStr = scope String(64);

			if (zoneNameId < 0)
			{
				int32 nameLen = -zoneNameId;
				fmtStr.Append((char8*)stateCtx.mReadStart + paramsReadPos - nameLen, nameLen);
				paramsSize = -1;
			}
			else
			{
				if (zoneNameId >= session.mZoneNames.Count)
				{
					outStr.Append("<invalid zone>");
					return;
				}
				let zoneName = session.mZoneNames[zoneNameId];
				fmtStr.Append(zoneName.mName);
				paramsSize = zoneName.mParamsSize;
			}

			if ((paramsSize != 0) && (formatStrings))
				stateCtx.FormatStr(paramsReadPos, paramsSize, fmtStr, outStr);
			else
				outStr.Append(fmtStr);
		}

		// Appends an event's "Name : Details" label, matching FindPanel's formatting (control chars
		// flattened to spaces so a details blob can't wreck the output, and a cap on the details).
		public static void AppendEventLabel(char8* name, char8* details, String outStr)
		{
			const int cMaxDetailChars = 128;

			outStr.Append(name);
			outStr.Append(" : ");

			int32 detailsLen = String.StrLen(details);
			int numDetailChars = Math.Min((int)detailsLen, cMaxDetailChars);
			for (int i < numDetailChars)
			{
				char8 c = details[i];
				if ((int32)c < 32)
					outStr.Append(' ');
				else
					outStr.Append(c);
			}
			if (detailsLen > cMaxDetailChars)
				outStr.Append("...");
		}

		// True if every buffer from this one on is past the end of [startTick, endTick], so the caller
		// can stop walking the track entirely. Buffers are in tick order.
		public static bool IsStreamPastEnd(BpStreamData streamData, int64 endTick)
		{
			return (endTick != 0) && (streamData.mStartTick > endTick);
		}

		// True if this buffer is entirely to the left of the range. A closed buffer records the tick it
		// was split at, so anything that finished before the range started can be skipped outright.
		public static bool IsStreamBeforeStart(BpStreamData streamData, int64 startTick)
		{
			return (startTick != 0) && (streamData.mSplitTick > 0) && (streamData.mSplitTick < startTick);
		}
	}

	// A zone or event instance located by a query.
	class BPFoundEntry
	{
		public String mName ~ delete _;
		public int64 mStartTick;
		public int64 mEndTick; // 0 for point events
		public int32 mDepth; // -1 for point events
		public int32 mTrackIdx;

		public int64 Length => (mEndTick != 0) ? (mEndTick - mStartTick) : 0;
	}

	// One aggregated row of a profile query -- the Profile panel's Name/Count/Total/Self columns.
	class BPPerfRow
	{
		public String mName ~ delete _;
		public int32 mCount;
		public int64 mTicks; // Inclusive
		public int64 mChildTicks;
		public int32 mStackCount; // Transient: how many instances of this name are on the stack right now

		public int64 SelfTicks => mTicks - mChildTicks;
	}

	// Per-entry state carried on the scan stack.
	struct BPScanEntry
	{
		public int64 mStartTick;
		public int32 mZoneNameId;
		public int32 mParamsReadPos;
		public bool mInScope;
		public BPPerfRow mRow;
	}

	// The Find panel, headless. Walks every stream buffer of every matching thread once and keeps the
	// best mMaxResults hits under the requested sort.
	class BPFindQuery
	{
		public const int32 cSortName = 0;
		public const int32 cSortStart = 1;
		public const int32 cSortLength = 2;
		public const int32 cSortTrack = 3;

		public TextSearcher mTextSearch = new TextSearcher() ~ delete _;
		public TextSearcher mTrackSearch = new TextSearcher() ~ delete _;

		public int64 mStartTick; // 0 = from the start of the session
		public int64 mEndTick; // 0 = to the end of the session
		public bool mIncludeZones = true;
		public bool mIncludeEvents = true;
		public bool mFormatStrings = true;
		public int32 mMaxResults = 100;
		public int32 mSortColumn = cSortLength;
		public bool mSortReverse = true;
		public int32 mTimeLimitMS = 5000;

		public int64 mTotalMatches;
		public bool mTimedOut;
		public List<BPFoundEntry> mResults = new List<BPFoundEntry>() ~ DeleteContainerAndItems!(_);

		BpSession mSession;

		int Compare(BPFoundEntry lhs, BPFoundEntry rhs)
		{
			int64 result = 0;
			if (mSortColumn == cSortName)
			{
				result = String.Compare(lhs.mName, rhs.mName, true);
				if (result == 0)
					result = lhs.mStartTick - rhs.mStartTick;
			}
			else if (mSortColumn == cSortStart)
			{
				result = lhs.mStartTick - rhs.mStartTick;
			}
			else if (mSortColumn == cSortTrack)
			{
				var lhsTrack = mSession.mThreads[lhs.mTrackIdx];
				var rhsTrack = mSession.mThreads[rhs.mTrackIdx];
				result = BpTrack.Compare(lhsTrack, rhsTrack);
				if (result == 0)
					result = lhs.mStartTick - rhs.mStartTick;
			}
			else // cSortLength
			{
				result = lhs.Length - rhs.Length;
			}

			if (mSortReverse)
				result = -result;
			// Narrow to a sign rather than casting -- a tick delta can be far wider than int
			return (result < 0) ? -1 : ((result > 0) ? 1 : 0);
		}

		// Results are collected unsorted and reduced whenever they pile up, so a search that matches
		// millions of zones still only ever holds a few times mMaxResults of them in memory.
		void SortAndTrim()
		{
			mResults.Sort(scope => Compare);
			while (mResults.Count > mMaxResults)
				delete mResults.PopBack();
		}

		void AddResult(BPFoundEntry entry)
		{
			mTotalMatches++;
			mResults.Add(entry);
			if (mResults.Count >= Math.Max((int)mMaxResults * 4, 64))
				SortAndTrim();
		}

		public void Run(BpSession session)
		{
			mSession = session;

			var stopwatch = scope Stopwatch();
			stopwatch.Start();

			String nameStr = scope String(256);
			String detailsStr = scope String(256);
			List<BPScanEntry> entryStack = scope List<BPScanEntry>();

			TrackLoop: for (int32 trackIdx < (int32)session.mThreads.Count)
			{
				var track = session.mThreads[trackIdx];

				if (!mTrackSearch.IsEmpty)
				{
					var trackName = scope String(128);
					track.GetName(trackName);
					if (!mTrackSearch.Matches(trackName))
						continue;
				}

				bool isFirstBuffer = true;

				for (var streamData in track.mStreamDataList)
				{
					if (BPQuery.IsStreamPastEnd(streamData, mEndTick))
						break;
					if (BPQuery.IsStreamBeforeStart(streamData, mStartTick))
						continue;

					BPStateContext stateCtx = scope BPStateContext(session, streamData);
					stateCtx.mAutoLeave = false;
					entryStack.Clear();

					CmdLoop: while (true)
					{
						switch (stateCtx.GetNextEvent())
						{
						case let .Enter(startTick, zoneNameId):
							BPScanEntry newEntry = default;
							newEntry.mStartTick = startTick;
							newEntry.mZoneNameId = zoneNameId;
							newEntry.mParamsReadPos = stateCtx.ReadPos;
							entryStack.Add(newEntry);
						case let .Leave(endTick):
							if (entryStack.IsEmpty)
								continue;
							let entry = entryStack.PopBack();
							if (!mIncludeZones)
								continue;
							if (BPQuery.IsDuplicateSpan(stateCtx, streamData, entry.mStartTick, (int32)entryStack.Count, isFirstBuffer))
								continue;
							if ((mEndTick != 0) && (entry.mStartTick > mEndTick))
								continue;
							if ((mStartTick != 0) && (endTick < mStartTick))
								continue;

							nameStr.Clear();
							BPQuery.GetEntryName(session, stateCtx, entry.mZoneNameId, entry.mParamsReadPos, mFormatStrings, nameStr);
							if (!mTextSearch.Matches(nameStr))
								continue;

							var foundZone = new BPFoundEntry();
							foundZone.mName = new String(nameStr);
							foundZone.mStartTick = entry.mStartTick;
							foundZone.mEndTick = endTick;
							foundZone.mDepth = (int32)entryStack.Count;
							foundZone.mTrackIdx = trackIdx;
							AddResult(foundZone);
						case let .Event(tick, name, details):
							if (!mIncludeEvents)
								continue;
							if ((mStartTick != 0) && (tick < mStartTick))
								continue;
							if ((mEndTick != 0) && (tick > mEndTick))
								continue;

							nameStr.Clear();
							nameStr.Append(name);
							detailsStr.Clear();
							detailsStr.Append(details);
							if ((!mTextSearch.Matches(nameStr)) && (!mTextSearch.Matches(detailsStr)))
								continue;

							var foundEvent = new BPFoundEntry();
							foundEvent.mName = new String(256);
							BPQuery.AppendEventLabel(name, details, foundEvent.mName);
							foundEvent.mStartTick = tick;
							foundEvent.mEndTick = 0;
							foundEvent.mDepth = -1;
							foundEvent.mTrackIdx = trackIdx;
							AddResult(foundEvent);
						case .EndOfStream:
							break CmdLoop;
						default:
						}
					}

					isFirstBuffer = false;

					if (stopwatch.ElapsedMilliseconds > mTimeLimitMS)
					{
						mTimedOut = true;
						break TrackLoop;
					}
				}
			}

			SortAndTrim();
		}
	}

	// The Profile panel, headless: aggregate a selected zone and everything under it by name.
	//
	// Two modes, matching the two ways the panel can be driven. With mDepth >= 0 the selection is one
	// specific entry -- the (start tick, stack depth) pair that identifies it in a thread -- and the
	// result covers that entry's whole subtree. With mDepth == -1 the selection is a bare time range
	// and the result covers every entry starting inside it, with times clamped to the range.
	class BPProfileQuery
	{
		const int32 cMaxDistinctRows = 20000;

		public int32 mThreadIdx;
		public int64 mStartTick;
		public int64 mEndTick;
		public int32 mDepth = -1; // -1 = time range mode
		public bool mFormatStrings = true;
		public int32 mMaxResults = 100;
		public int32 mTimeLimitMS = 5000;

		public bool mTimedOut;
		public bool mFoundSelection;
		public int64 mSelectionTicks;
		public int32 mTotalRows;
		public List<BPPerfRow> mResults = new List<BPPerfRow>() ~ DeleteContainerAndItems!(_);

		Dictionary<String, BPPerfRow> mRowMap = new Dictionary<String, BPPerfRow>() ~ delete _;
		String mTempStr = new String(256) ~ delete _;

		// Heaviest first. Tick counts can exceed what int holds, so narrow to a sign rather than casting
		int CompareRows(BPPerfRow lhs, BPPerfRow rhs)
		{
			int64 result = rhs.mTicks - lhs.mTicks;
			return (result < 0) ? -1 : ((result > 0) ? 1 : 0);
		}

		// Rows are keyed by (and own) their own name string, so deleting the result list frees both.
		BPPerfRow GetRow(BpSession session, BPStateContext stateCtx, BPScanEntry entry)
		{
			mTempStr.Clear();
			BPQuery.GetEntryName(session, stateCtx, entry.mZoneNameId, entry.mParamsReadPos, mFormatStrings, mTempStr);

			BPPerfRow row;
			if (mRowMap.TryGetValue(mTempStr, out row))
				return row;

			if (mResults.Count >= cMaxDistinctRows)
				return null;

			row = new BPPerfRow();
			row.mName = new String(mTempStr);
			mRowMap[row.mName] = row;
			mResults.Add(row);
			return row;
		}

		public void Run(BpSession session)
		{
			if ((mThreadIdx < 0) || (mThreadIdx >= session.mThreads.Count))
				return;

			var stopwatch = scope Stopwatch();
			stopwatch.Start();

			var track = session.mThreads[mThreadIdx];
			bool isManual = mDepth == -1;
			bool isFirstBuffer = true;
			bool selectionClosed = false;
			List<BPScanEntry> entryStack = scope List<BPScanEntry>();

			StreamLoop: for (var streamData in track.mStreamDataList)
			{
				if (BPQuery.IsStreamPastEnd(streamData, mEndTick))
					break;
				if (BPQuery.IsStreamBeforeStart(streamData, mStartTick))
					continue;

				BPStateContext stateCtx = scope BPStateContext(session, streamData);
				stateCtx.mAutoLeave = false;
				entryStack.Clear();

				CmdLoop: while (true)
				{
					switch (stateCtx.GetNextEvent())
					{
					case let .Enter(startTick, zoneNameId):
						int32 stackPos = (int32)entryStack.Count;

						BPScanEntry newEntry = default;
						newEntry.mStartTick = startTick;
						newEntry.mZoneNameId = zoneNameId;
						newEntry.mParamsReadPos = stateCtx.ReadPos;

						if (isManual)
						{
							newEntry.mInScope = (startTick >= mStartTick) && ((mEndTick == 0) || (startTick <= mEndTick));
						}
						else
						{
							// Once inside the selected zone everything below it is in scope too, so the
							// whole subtree gets aggregated without having to re-test each descendant
							bool parentInScope = (stackPos > 0) && (entryStack[stackPos - 1].mInScope);
							newEntry.mInScope = parentInScope || ((startTick == mStartTick) && (stackPos == mDepth));
						}

						if (newEntry.mInScope)
						{
							newEntry.mRow = GetRow(session, stateCtx, newEntry);
							if (newEntry.mRow != null)
								newEntry.mRow.mStackCount++;
						}

						entryStack.Add(newEntry);
					case let .Leave(endTick):
						if (entryStack.IsEmpty)
							continue;
						let entry = entryStack.PopBack();
						if ((!entry.mInScope) || (entry.mRow == null))
							continue;

						var row = entry.mRow;
						row.mStackCount--;

						if ((!isManual) && (entry.mStartTick == mStartTick) && (entryStack.Count == mDepth))
						{
							mFoundSelection = true;
							mSelectionTicks = endTick - entry.mStartTick;
							// The selection is re-opened and re-closed in every buffer it spans, so
							// only stop once it actually ends inside this one -- otherwise the rest of
							// its subtree is still ahead of us in the buffers that follow
							if (endTick <= streamData.mSplitTick)
								selectionClosed = true;
						}

						// Already counted in the buffer it began in
						if (BPQuery.IsDuplicateSpan(stateCtx, streamData, entry.mStartTick, (int32)entryStack.Count, isFirstBuffer))
							continue;

						int64 ticks = endTick - entry.mStartTick;
						if (isManual)
						{
							int64 clampedStart = Math.Max(entry.mStartTick, mStartTick);
							int64 clampedEnd = (mEndTick != 0) ? Math.Min(endTick, mEndTick) : endTick;
							ticks = clampedEnd - clampedStart;
							if (ticks <= 0)
								continue;
						}

						row.mCount++;
						// A recursive instance's time is already inside its outer instance's total,
						// so only the outermost one contributes to the inclusive figure.
						if (row.mStackCount == 0)
							row.mTicks += ticks;

						if (entryStack.Count > 0)
						{
							var parentRow = entryStack[entryStack.Count - 1].mRow;
							if (parentRow != null)
								parentRow.mChildTicks += ticks;
							// ...and for that same reason this time isn't really "child" time either
							if (row.mStackCount != 0)
								row.mChildTicks -= ticks;
						}
					case .EndOfStream:
						break CmdLoop;
					default:
					}
				}

				isFirstBuffer = false;

				// The selection ended inside the buffer we just finished, so its whole subtree is in
				if (selectionClosed)
					break;

				if (stopwatch.ElapsedMilliseconds > mTimeLimitMS)
				{
					mTimedOut = true;
					break;
				}
			}

			if (isManual)
				mSelectionTicks = mEndTick - mStartTick;

			mTotalRows = (int32)mResults.Count;
			mResults.Sort(scope => CompareRows);
			while (mResults.Count > mMaxResults)
			{
				var row = mResults.PopBack();
				mRowMap.Remove(row.mName);
				delete row;
			}
		}
	}

	// What every thread was doing across one span of time. This is the "why did these threads stall on
	// each other" view: for each thread it reports the zones still open when the slice began, the zones
	// that overlap the slice down to a depth limit, any point events inside it, and how much of the
	// slice that thread had a top-level zone running at all.
	class BPSliceTrack
	{
		public int32 mTrackIdx;
		public String mName ~ delete _;
		public int32 mNativeThreadId;
		public List<BPFoundEntry> mOpenStack = new List<BPFoundEntry>() ~ DeleteContainerAndItems!(_);
		public List<BPFoundEntry> mZones = new List<BPFoundEntry>() ~ DeleteContainerAndItems!(_);
		public List<BPFoundEntry> mEvents = new List<BPFoundEntry>() ~ DeleteContainerAndItems!(_);
		public int32 mTotalZones;
		public int32 mTotalEvents;
		public int64 mCoveredTicks;
		public bool mTrimmed;
	}

	class BPSliceQuery
	{
		public int64 mStartTick;
		public int64 mEndTick;
		public int32 mMaxDepth = 3;
		public int32 mMaxZonesPerTrack = 100;
		public int32 mMaxEventsPerTrack = 50;
		public bool mIncludeEvents = true;
		public bool mFormatStrings = true;
		public int32 mTimeLimitMS = 5000;

		public bool mTimedOut;
		public List<BPSliceTrack> mTracks = new List<BPSliceTrack>() ~ DeleteContainerAndItems!(_);

		int CompareByDepthThenStart(BPFoundEntry lhs, BPFoundEntry rhs)
		{
			if (lhs.mDepth != rhs.mDepth)
				return lhs.mDepth - rhs.mDepth;
			int64 result = lhs.mStartTick - rhs.mStartTick;
			return (result < 0) ? -1 : ((result > 0) ? 1 : 0);
		}

		public void Run(BpSession session)
		{
			var stopwatch = scope Stopwatch();
			stopwatch.Start();

			String nameStr = scope String(256);
			List<BPScanEntry> entryStack = scope List<BPScanEntry>();

			TrackLoop: for (int32 trackIdx < (int32)session.mThreads.Count)
			{
				var track = session.mThreads[trackIdx];

				var sliceTrack = new BPSliceTrack();
				sliceTrack.mTrackIdx = trackIdx;
				sliceTrack.mName = new String(64);
				track.GetName(sliceTrack.mName);
				sliceTrack.mNativeThreadId = track.mNativeThreadId;
				mTracks.Add(sliceTrack);

				bool isFirstBuffer = true;

				for (var streamData in track.mStreamDataList)
				{
					if (BPQuery.IsStreamPastEnd(streamData, mEndTick))
						break;
					if (BPQuery.IsStreamBeforeStart(streamData, mStartTick))
						continue;

					BPStateContext stateCtx = scope BPStateContext(session, streamData);
					stateCtx.mAutoLeave = false;
					entryStack.Clear();

					CmdLoop: while (true)
					{
						switch (stateCtx.GetNextEvent())
						{
						case let .Enter(startTick, zoneNameId):
							BPScanEntry newEntry = default;
							newEntry.mStartTick = startTick;
							newEntry.mZoneNameId = zoneNameId;
							newEntry.mParamsReadPos = stateCtx.ReadPos;
							entryStack.Add(newEntry);
						case let .Leave(endTick):
							if (entryStack.IsEmpty)
								continue;
							let entry = entryStack.PopBack();

							int32 depth = (int32)entryStack.Count;
							// Already counted in the buffer it began in. A zone that started before the
							// slice has no such buffer inside our window, which is exactly the case
							// isFirstBuffer keeps -- and those are the ones openAtStart is made of
							if (BPQuery.IsDuplicateSpan(stateCtx, streamData, entry.mStartTick, depth, isFirstBuffer))
								continue;

							// Overlap, not containment -- a zone that brackets the whole slice is
							// usually the most interesting thing on the thread
							if ((entry.mStartTick > mEndTick) || (endTick < mStartTick))
								continue;

							sliceTrack.mTotalZones++;

							if (depth == 0)
							{
								sliceTrack.mCoveredTicks += Math.Min(endTick, mEndTick) - Math.Max(entry.mStartTick, mStartTick);
							}

							bool isOpenAtStart = (entry.mStartTick <= mStartTick) && (endTick >= mStartTick);
							bool wantZone = (depth < mMaxDepth) && (sliceTrack.mZones.Count < mMaxZonesPerTrack);
							if ((!wantZone) && (!isOpenAtStart))
							{
								if (depth < mMaxDepth)
									sliceTrack.mTrimmed = true;
								continue;
							}

							nameStr.Clear();
							BPQuery.GetEntryName(session, stateCtx, entry.mZoneNameId, entry.mParamsReadPos, mFormatStrings, nameStr);

							if (isOpenAtStart)
							{
								var openEntry = new BPFoundEntry();
								openEntry.mName = new String(nameStr);
								openEntry.mStartTick = entry.mStartTick;
								openEntry.mEndTick = endTick;
								openEntry.mDepth = depth;
								openEntry.mTrackIdx = trackIdx;
								sliceTrack.mOpenStack.Add(openEntry);
							}

							if (wantZone)
							{
								var zoneEntry = new BPFoundEntry();
								zoneEntry.mName = new String(nameStr);
								zoneEntry.mStartTick = entry.mStartTick;
								zoneEntry.mEndTick = endTick;
								zoneEntry.mDepth = depth;
								zoneEntry.mTrackIdx = trackIdx;
								sliceTrack.mZones.Add(zoneEntry);
							}
						case let .Event(tick, name, details):
							if (!mIncludeEvents)
								continue;
							if ((tick < mStartTick) || (tick > mEndTick))
								continue;
							sliceTrack.mTotalEvents++;
							if (sliceTrack.mEvents.Count >= mMaxEventsPerTrack)
								continue;

							var eventEntry = new BPFoundEntry();
							eventEntry.mName = new String(256);
							BPQuery.AppendEventLabel(name, details, eventEntry.mName);
							eventEntry.mStartTick = tick;
							eventEntry.mEndTick = 0;
							eventEntry.mDepth = -1;
							eventEntry.mTrackIdx = trackIdx;
							sliceTrack.mEvents.Add(eventEntry);
						case .EndOfStream:
							break CmdLoop;
						default:
						}
					}

					isFirstBuffer = false;

					if (stopwatch.ElapsedMilliseconds > mTimeLimitMS)
					{
						mTimedOut = true;
						break;
					}
				}

				// Outermost first, then in time order -- reads like the timeline row it came from.
				// Done before bailing out on a timeout so the partial thread still comes back ordered.
				sliceTrack.mOpenStack.Sort(scope => CompareByDepthThenStart);
				sliceTrack.mZones.Sort(scope => CompareByDepthThenStart);

				if (mTimedOut)
					break TrackLoop;
			}
		}
	}
}
