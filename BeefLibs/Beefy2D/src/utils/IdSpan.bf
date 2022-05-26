using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using System.Security.Cryptography;

namespace Beefy.utils
{
    public struct IdSpan : IFormattable
    {
		public class LookupContext
		{
			public enum SortKind
			{
				None,
				Id,
				Index
			}

			public struct Entry
			{
				public int32 mIdStart;
				public int32 mIndexStart;
				public int32 mLength;
			}

			public List<Entry> mEntries = new .() ~ delete _;
			public SortKind mSortKind;

			public this(IdSpan idSpan)
			{
				int encodeIdx = 0;
				int charId = 1;
				int charIdx = 0;
				while (true)
				{
				    int cmd = Utils.DecodeInt(idSpan.mData, ref encodeIdx);
				    if (cmd > 0)
				        charId = cmd;
				    else
				    {
						if (cmd == 0)
							break;

				        int spanSize = -cmd;

						Entry entry;
						entry.mIdStart = (.)charId;
						entry.mIndexStart = (.)charIdx;
						entry.mLength = (.)spanSize;
						mEntries.Add(entry);

				        charId += spanSize;
				        charIdx += spanSize;
				    }
				}
			}

			public int GetIndexFromId(int32 findCharId)
			{
				if (mSortKind != .Id)
				{
					mEntries.Sort(scope (lhs, rhs) => lhs.mIdStart <=> rhs.mIdStart);
					mSortKind = .Id;
				}

				int lo = 0;
				int hi = mEntries.Count - 1;

				while (lo <= hi)
				{
				    int i = (lo + hi) / 2;
					var midVal = ref mEntries[i];
					if ((findCharId >= midVal.mIdStart) && (findCharId < midVal.mIdStart + midVal.mLength))
						return midVal.mIndexStart + (findCharId - midVal.mIdStart);

				    if (findCharId > midVal.mIdStart)
				        lo = i + 1;
				    else
				        hi = i - 1;
				}

				return -1;
			}

			public int32 GetIdAtIndex(int32 findCharId)
			{
				if (mSortKind != .Index)
				{
					mEntries.Sort(scope (lhs, rhs) => lhs.mIndexStart <=> rhs.mIndexStart);
					mSortKind = .Index;
				}

				int lo = 0;
				int hi = mEntries.Count - 1;

				while (lo <= hi)
				{
				    int i = (lo + hi) / 2;
					var midVal = ref mEntries[i];
					if ((findCharId >= midVal.mIndexStart) && (findCharId < midVal.mIndexStart + midVal.mLength))
						return midVal.mIdStart + (findCharId - midVal.mIndexStart);

				    if (findCharId > midVal.mIndexStart)
				        lo = i + 1;
				    else
				        hi = i - 1;
				}

				return -1;
			}

			public override void ToString(String strBuffer)
			{
				if (mSortKind != .Index)
				{
					mEntries.Sort(scope (lhs, rhs) => lhs.mIndexStart <=> rhs.mIndexStart);
					mSortKind = .Index;
				}

				strBuffer.AppendF("IdSpan.LookupCtx(");
				for (var entry in mEntries)
				{
					if (@entry.Index > 0)
						strBuffer.Append(' ');
					strBuffer.AppendF($"{entry.mIndexStart}:{entry.mLength}={entry.mIdStart}");
				}	
				strBuffer.AppendF(")");
			}
		}

		enum Change
		{
			case Insert(int32 index, int32 id, int16 length);
			case Remove(int32 index, int16 length);

			public int32 GetIndex()
			{
				int32 index;
				switch (this)
				{
				case .Insert(out index, ?, ?):
				case .Remove(out index, ?):
				}
				return index;
			}
		}
		enum ChangeKind
		{
			case None;
			case Insert;
			case Remove;
		}

        static uint8[] sEmptyData = new uint8[] ( 0 ) ~ delete _;

        public uint8[] mData;
        public int32 mLength;
		List<Change> mChangeList;

		public static int32 sId;
		public int32 mId = ++sId;

		public bool mAlwaysPrepare = false;

		/*public uint8[] mData
		{
			get
			{
				Debug.Assert(mChangeList == null);
				return mData;
			}
		}*/

		public this()
		{
			mData = sEmptyData;
			mLength = 0;
			mChangeList = null;
		}

        public this(uint8[] data, int32 length)
        {
            mData = data;
            mLength = length;
			mChangeList = null;
        }

		bool HasChangeList
		{
			get
			{
				return mChangeList != null;
			}
		}

		public bool IsEmpty
		{
			get
			{
				return mLength == 0;
			}
		}

		struct Span
		{
			public int32 mIndex;
			public int32 mId;
			public int32 mLength;

			public int32 mNext;
		}

		public void PrepareReverse() mut
		{
			PrepareReverse(0, mChangeList.Count);
			DeleteAndNullify!(mChangeList);
		}

		// Decode change lists, move backwards through it applying changes, the reencode it.
		//  Repeats as necessary if items aren't in reverse order
		void PrepareReverse(int startChangeIdx, int endChangeIdx) mut
		{
			int changeIdx = startChangeIdx;

			List<Span> spans = scope List<Span>();

			while (changeIdx < endChangeIdx)
			{
				spans.Clear();

				int encodeIdx = 0;
				int32 charId = 1;
				int32 charIdx = 0;
				while (true)
				{
				    int32 cmd = Utils.DecodeInt(mData, ref encodeIdx);
				    if (cmd > 0)
				    {
						charId = cmd;
					}
				    else
				    {
				        int32 spanSize = -cmd;
				        
				        if (cmd == 0)
				            break;

						Span span;
						span.mId = charId;
						span.mIndex = charIdx;
						span.mLength = spanSize;
						span.mNext = -1;
						spans.Add(span);

						charId += spanSize;
						charIdx += spanSize;

						if (spans.Count > 1)
							spans[spans.Count - 2].mNext = (int32)spans.Count - 1;
				    }
				}

				// Initialize to something so we can pick up the insert
				if (spans.Count == 0)
				{
					Span span;
					span.mId = 1;
					span.mIndex = 0;
					span.mLength = 0;
					span.mNext = -1;
					spans.Add(span);
				}

				
				int32 index = -1;
				int32 length = -1;
				int32 curId = -1;
				ChangeKind changeKind = .None;

				int32 checkIdx = (int32)spans.Count - 1;
				int32 headSpanIdx = 0;

				// Reverse find the first span
				while ((checkIdx >= 0) && (changeIdx < endChangeIdx))
				{
					if (changeKind == .None)
					{
						switch (mChangeList[changeIdx])
						{
						case .Insert(out index, out curId, out length):
							changeKind = .Insert;
						case .Remove(out index, out length):
							changeKind = .Remove;
						}
					}

					var checkSpan = ref spans[checkIdx];
					if ((index >= checkSpan.mIndex) && (index <= checkSpan.mIndex + checkSpan.mLength))
					{
						if (changeKind == .Insert)
						{
							if (index == checkSpan.mIndex)
							{
								if (checkSpan.mLength == 0)
								{
									checkSpan.mLength = length;
									checkSpan.mId = curId;
								}
								else
								{
									int32 newSpanIdx = (.)spans.Count;

									// Insert before span
									Span span;
									span.mIndex = index;
									span.mId = curId;
									span.mLength = length;
									span.mNext = (.)checkIdx;
									spans.Add(span);

									if (checkIdx > 0)
									{
										Debug.Assert(spans[checkIdx - 1].mNext == checkIdx);
										spans[checkIdx - 1].mNext = newSpanIdx;
									}
									else
										headSpanIdx = newSpanIdx;

									// Since we remapped the previous entries mNext, we are no longer in order and can't be reused this pass
									checkIdx = checkIdx - 1;
								}
							}
							else
							{
								var checkSpan;

								// Split span
								int32 leftLength = index - checkSpan.mIndex;

								int32 newSpanId = (.)spans.Count;
								int32 newRightId = (.)spans.Count + 1;
								@checkSpan.mNext = newSpanId;
								@checkSpan.mLength = leftLength;

								Span span;
								span.mIndex = index;
								span.mId = curId;
								span.mLength = length;
								span.mNext = newRightId;
								spans.Add(span);

								Span rightSpan;
								rightSpan.mIndex = index + span.mLength;
								rightSpan.mId = checkSpan.mId + leftLength;
								rightSpan.mLength = checkSpan.mLength - leftLength;
								rightSpan.mNext = checkSpan.mNext;
								spans.Add(rightSpan);
							}
						}
						else // Remove
						{
							int removeSpanIdx = checkIdx;
							if (index == checkSpan.mIndex)
							{
								// Removing from front of span.  Handled in loop below.
							}
							else if (index + length >= checkSpan.mIndex + checkSpan.mLength)
							{
								// Removing up to or past end of span
								int32 removeCount = Math.Min(length, checkSpan.mLength - (index - checkSpan.mIndex));
								checkSpan.mLength -= removeCount;
								length -= removeCount;
								removeSpanIdx = checkSpan.mNext;
							}
							else
							{
								var checkSpan;

								int32 splitIdx = index - checkSpan.mIndex;
								int32 splitOfs = splitIdx + length;
								int32 newRightId = (.)spans.Count;
								@checkSpan.mNext = newRightId;
								@checkSpan.mLength = index - checkSpan.mIndex;

								Span rightSpan;
								rightSpan.mIndex = checkSpan.mIndex + splitIdx;
								rightSpan.mId = checkSpan.mId + splitOfs;
								rightSpan.mLength = checkSpan.mLength - splitOfs;
								rightSpan.mNext = checkSpan.mNext;
								spans.Add(rightSpan);

								length = 0;

								if (newRightId == checkIdx + 1)
									checkIdx = newRightId; // rightSpan index is valid now and it is next in sequence
							}

							while (length > 0)
							{
								var removeSpan = ref spans[removeSpanIdx];

								// Remove from start of span
								int32 removeCount = Math.Min(removeSpan.mLength, length);
								removeSpan.mId += removeCount;
								removeSpan.mLength -= removeCount;

								length -= removeCount;
								
								removeSpanIdx = removeSpan.mNext;
							}
						}

						changeIdx++;
						changeKind = .None;
						continue;
					}

					checkIdx--;
				}

				curId = 1;
				int32 spanIdx = headSpanIdx;
				int curEncodeIdx = 0;
				if (mData != sEmptyData)
					delete mData;
				mData = new uint8[spans.Count * 8];

				while (spanIdx != -1)
				{
					var span = ref spans[spanIdx];

					if (span.mLength == 0)
					{
						spanIdx = span.mNext;
						continue;
					}	

					if (span.mId != curId)
					{
						Utils.EncodeInt(mData, ref curEncodeIdx, span.mId);
						curId = span.mId;
					}

					Utils.EncodeInt(mData, ref curEncodeIdx, -span.mLength);
					curId += span.mLength;

					spanIdx = span.mNext;
				}
				Utils.EncodeInt(mData, ref curEncodeIdx, 0);
				mLength = (int32)curEncodeIdx;
			}
		}

		void MaybePrepare() mut
		{
			// For sanity - only queue up so many changes
			if (mChangeList.Count >= 8192)
			{
				Prepare();
			}
		}

		public void Prepare() mut
		{
			if (mChangeList == null)
				return;

			scope AutoBeefPerf("IdSpan.Prepare");

			int changeIdx = 0;

			while (changeIdx < mChangeList.Count)
			{
				// Check to see if we have a reverse-order encoding.  This can occur when undoing forward-ordered changes (for example)
				bool hasReverse = false;
				int reverseLastIdx = changeIdx;

				int32 prevIndex = mChangeList[changeIdx].GetIndex();
				for (int checkIdx = changeIdx + 1; checkIdx < mChangeList.Count; checkIdx++)
				{
					int32 nextIndex = mChangeList[checkIdx].GetIndex();
					if (nextIndex > prevIndex)
						break;
					if (nextIndex < prevIndex)
						hasReverse = true;
					reverseLastIdx = checkIdx;
					prevIndex = nextIndex;
				}

				if (hasReverse)
				{
					PrepareReverse(changeIdx, reverseLastIdx + 1);
					changeIdx = reverseLastIdx + 1;
					continue;
				}

				// We need space to encode '-length', the new span ID, 
				//  reverting back to the original ID, and a split span length
				uint8[] textIdData = new uint8[mLength + mChangeList.Count*16];

				int prevCharIdx = 0;
				int prevEncodeIdx = 0;
				int prevLastSpanLength = 0;
				int prevLastSpanIdStart = 1;

				int curEncodeIdx = 0;
				int curSpanIdStart = 1;
				bool foundSpan = false;
				int ignoreLength = 0;

				int32 index;
				int32 length;
				int32 curId = -1;
				ChangeKind changeKind;

				

				switch (mChangeList[changeIdx++])
				{
				case .Insert(out index, out curId, out length):
					changeKind = .Insert;
				case .Remove(out index, out length):
					changeKind = .Remove;
				}

				while (prevLastSpanIdStart != -1)
				{
				    if (ignoreLength > 0)
				    {
				        int handleLength = Math.Min(prevLastSpanLength, ignoreLength);
				        ignoreLength -= handleLength;
				        prevLastSpanIdStart += handleLength;
				        prevLastSpanLength -= handleLength;
				    }

				    if ((curSpanIdStart != prevLastSpanIdStart) && (prevLastSpanLength > 0) && (ignoreLength == 0))
				    {
				        Utils.EncodeInt(textIdData, ref curEncodeIdx, prevLastSpanIdStart);
				        curSpanIdStart = prevLastSpanIdStart;
				    }

				    if ((prevCharIdx + prevLastSpanLength >= index) && (!foundSpan) && (ignoreLength == 0))
				    {
				        foundSpan = true;

						if (curSpanIdStart != prevLastSpanIdStart)
						{
						    Utils.EncodeInt(textIdData, ref curEncodeIdx, prevLastSpanIdStart);
						    curSpanIdStart = prevLastSpanIdStart;
						}

						if (changeKind case .Insert)
						{
							// Time to insert
							int leftSplitLen = index - prevCharIdx;
							if (leftSplitLen > 0)
							{
							    Utils.EncodeInt(textIdData, ref curEncodeIdx, -leftSplitLen);
							    curSpanIdStart += leftSplitLen;
							    prevLastSpanIdStart += leftSplitLen;
							    prevCharIdx += leftSplitLen;
							    prevLastSpanLength -= leftSplitLen;
							}

							if (curSpanIdStart != curId)
							{
							    curSpanIdStart = curId;
							    Utils.EncodeInt(textIdData, ref curEncodeIdx, curSpanIdStart);
							}
							curId += length;

							if (length > 0)
							{
							    Utils.EncodeInt(textIdData, ref curEncodeIdx, -length);
							    curSpanIdStart += length;
								prevCharIdx += length;
							}
						}
						else
						{
					        ignoreLength = length;

					        // Time to insert
					        int leftSplitLen = index - prevCharIdx;
					        if (leftSplitLen > 0)
					        {
					            Utils.EncodeInt(textIdData, ref curEncodeIdx, -leftSplitLen);
					            curSpanIdStart += leftSplitLen;
					            prevLastSpanIdStart += leftSplitLen;
					            prevCharIdx += leftSplitLen;
					            prevLastSpanLength -= leftSplitLen;
					        }
						}

						if (changeIdx < mChangeList.Count)
						{
							switch (mChangeList[changeIdx])
							{
							case .Insert(out index, out curId, out length):
								changeKind = .Insert;
							case .Remove(out index, out length):
								changeKind = .Remove;
							}
							if (index >= prevCharIdx)
							{
								// We are inserting into a just-removed location
								foundSpan = false;
								changeIdx++;
							}
						}

				        continue;
				    }

				    int cmd = Utils.DecodeInt(mData, ref prevEncodeIdx);
				    if (cmd >= 0)
				    {
				        if (prevLastSpanLength > 0)
				        {
				            Utils.EncodeInt(textIdData, ref curEncodeIdx, -prevLastSpanLength);
				            curSpanIdStart += prevLastSpanLength;
				            prevLastSpanIdStart += prevLastSpanLength;
				            prevCharIdx += prevLastSpanLength;
				            prevLastSpanLength = 0;
				        }

				        Debug.Assert(prevLastSpanLength == 0);
				        prevLastSpanIdStart = cmd;

				        if (cmd == 0)
				            break;
				    }
				    else
				        prevLastSpanLength += -cmd;
				}

				Utils.EncodeInt(textIdData, ref curEncodeIdx, 0);
				mLength = (int32)curEncodeIdx;
				if (mData != sEmptyData)
					delete mData;
				mData = textIdData;
			}
			DeleteAndNullify!(mChangeList);
		}

		public IdSpan GetPrepared() mut
		{
			Prepare();
			return this;
		}

		public void Dispose() mut
		{
			if (mData != sEmptyData)
				delete mData;
			delete mChangeList;
			mData = sEmptyData;
			mLength = 0;
		}

		public void DuplicateFrom(ref IdSpan span) mut
		{
			Dispose();
			this = span.Duplicate();
		}

        public static readonly IdSpan Empty = IdSpan(sEmptyData, 1);

        public void Insert(int index, int length, ref int32 curId) mut
        {
			var index;
			var length;

			if (mChangeList == null)
				mChangeList = new .();
			else if (mChangeList.Count > 0)
			{
				var change = ref mChangeList.Back;
				if (change case .Insert(let prevIndex, let prevId, var ref prevLength))
				{
					if ((prevIndex + prevLength == index) && (prevId + prevLength == curId))
					{
						int16 curLen = (int16)Math.Min(length, 0x7FFF - prevLength);
						prevLength += curLen;

						curId += curLen;
						index += curLen;
						length -= curLen;
					}
				}
			}

			while (length > 0)
			{
				int16 curLen = (int16)Math.Min(length, 0x7FFF);
				mChangeList.Add(.Insert((int32)index, curId, curLen));

				curId += curLen;
				index += curLen;
				length -= curLen;
			}

			if (mAlwaysPrepare)
				Prepare();
			else
				MaybePrepare();
        }

        public void Remove(int index, int length) mut
        {
			var index;
			var length;

			if (mChangeList == null)
				mChangeList = new .();
			else if (mChangeList.Count > 0)
			{
				var change = ref mChangeList.Back;
				if (change case .Remove(let prevIndex, var ref prevLength))
				{
					if (prevIndex == index)
					{
						int16 curLen = (int16)Math.Min(length, 0x7FFF - prevLength);
						prevLength += curLen;
						length -= curLen;
					}
				}
			}

			while (length > 0)
			{
				int16 curLen = (int16)Math.Min(length, 0x7FFF);
				mChangeList.Add(.Remove((int32)index, curLen));
				length -= curLen;
			}

			if (mAlwaysPrepare)
				Prepare();
			else
				MaybePrepare();
        }
        
        public int GetIndexFromId(int32 findCharId)
        {
			Debug.Assert(!HasChangeList);
            int encodeIdx = 0;
            int charId = 1;
            int charIdx = 0;
            while (true)
            {
                int cmd = Utils.DecodeInt(mData, ref encodeIdx);
                if (cmd > 0)
                    charId = cmd;
                else
                {
                    int spanSize = -cmd;
                    if ((findCharId >= charId) && (findCharId < charId + spanSize))
                        return charIdx + (findCharId - charId);
                    charId += spanSize;
                    charIdx += spanSize;

                    if (cmd == 0)
                        return -1;
                }
            }
        }

        public int32 GetIdAtIndex(int findIndex)
        {
            int encodeIdx = 0;
            int32 charId = 1;
            int charIdx = 0;
            while (true)
            {
                int32 cmd = Utils.DecodeInt(mData, ref encodeIdx);
                if (cmd > 0)
                    charId = cmd;
                else
                {
                    int32 spanSize = -cmd;
                    if ((findIndex >= charIdx) && (findIndex < charIdx + spanSize))
                        return charId + (int32)(findIndex - charIdx);
                    charId += spanSize;
                    charIdx += spanSize;

                    if (cmd == 0)
                        return -1;
                }
            }
        }

        public IdSpan Duplicate()
        {
            IdSpan idSpan = IdSpan();
            if (mData != null)
            {
                idSpan.mData = new uint8[mLength];
				mData.CopyTo(idSpan.mData, 0, 0, mLength);
                idSpan.mLength = mLength;
            }
			if (mChangeList != null)
			{
				idSpan.mChangeList = new .();
				for (var change in mChangeList)
					idSpan.mChangeList.Add(change);
			}
            return idSpan;
        }

        public bool Equals(IdSpan idData2)
        {
			Debug.Assert(!HasChangeList);
			Debug.Assert(!idData2.HasChangeList);
			
			if ((mLength == 0) || (idData2.mLength == 0))
				return (mLength == 0) && (idData2.mLength == 0);

            int encodeIdx1 = 0;
            int encodeIdx2 = 0;

            int curSpanId1 = 1;
            int curSpanId2 = 1;

            int spanLen1 = 0;
            int spanLen2 = 0;

            while (true)
            {
                while (spanLen1 == 0)
                {
                    int cmd = Utils.DecodeInt(mData, ref encodeIdx1);
                    if (cmd < 0)
                    {
                        spanLen1 = -cmd;
                    }
                    else
                    {
                        curSpanId1 = cmd;
                        if (cmd == 0)
                            break;
                    }
                }

                while (spanLen2 == 0)
                {
                    int32 cmd = Utils.DecodeInt(idData2.mData, ref encodeIdx2);
                    if (cmd < 0)
                    {
                        spanLen2 = -cmd;
                    }
                    else
                    {
                        curSpanId2 = cmd;
                        if (cmd == 0)
                        {
                            // They are equal if both spans are at the end
                            return spanLen1 == 0;
                        }
                    }
                }

                if (curSpanId1 != curSpanId2)
                    return false;
                int minLen = Math.Min(spanLen1, spanLen2);
                curSpanId1 += minLen;
                spanLen1 -= minLen;
                curSpanId2 += minLen;
                spanLen2 -= minLen;
            }
        }

        public int GetTotalLength()
        {
            int len = 0;
            int encodeIdx = 0;
            while (true)
            {
                int cmd = Utils.DecodeInt(mData, ref encodeIdx);
                if (cmd == 0)
                    return len;
                if (cmd < 0)
                    len += -cmd;
            }
        }

        static bool FindId(uint8[] idData, ref int encodeIdx, ref int32 curSpanId, ref int32 spanLen, int32 findSpanId)
        {
            while (true)
            {
                int32 cmd = Utils.DecodeInt(idData, ref encodeIdx);
                if (cmd < 0)
                {
                    spanLen = -cmd;
                    if ((findSpanId >= curSpanId) && (findSpanId < curSpanId + spanLen))
                    {
                        int32 delta = findSpanId - curSpanId;
                        curSpanId += delta;
                        spanLen -= delta;
                        return true;
                    }
					curSpanId += spanLen;
                }
                else
                {
                    curSpanId = cmd;
                    if (cmd == 0)
                        return false;
                }
            }
        }

        public bool IsRangeEqual(IdSpan idData2, int32 startCharId, int32 endCharId)
        {
            int encodeIdx1 = 0;
            int encodeIdx2 = 0;

            int32 curSpanId1 = 1;
            int32 curSpanId2 = 1;

            int32 spanLen1 = 0;
            int32 spanLen2 = 0;

            if (!FindId(mData, ref encodeIdx1, ref curSpanId1, ref spanLen1, startCharId))
                return false;
            if (!FindId(idData2.mData, ref encodeIdx2, ref curSpanId2, ref spanLen2, startCharId))
                return false;

            while (true)
            {
                while (spanLen1 == 0)
                {
                    int32 cmd = Utils.DecodeInt(mData, ref encodeIdx1);
                    if (cmd < 0)
                    {
                        spanLen1 = -cmd;
                    }
                    else
                    {
                        curSpanId1 = cmd;
                        if (cmd == 0)
                            break;
                    }
                }

                while (spanLen2 == 0)
                {
                    int32 cmd = Utils.DecodeInt(idData2.mData, ref encodeIdx2);
                    if (cmd < 0)
                    {
                        spanLen2 = -cmd;
                    }
                    else
                    {
                        curSpanId2 = cmd;
                        if (cmd == 0)
                        {
                            // They are equal if both spans are at the end
                            return spanLen1 == 0;
                        }
                    }
                }

                if (curSpanId1 != curSpanId2)
                    return false;
                if (curSpanId1 == endCharId)
                    return true;

                int minLen = Math.Min(spanLen1, spanLen2);
                if ((endCharId >= curSpanId1) && (endCharId < curSpanId1 + minLen))
                    minLen = Math.Min(minLen, endCharId - curSpanId1);
                if ((endCharId >= curSpanId2) && (endCharId < curSpanId2 + minLen))
                    minLen = Math.Min(minLen, endCharId - curSpanId2);

                curSpanId1 += (int32)minLen;
                spanLen1 -= (.)minLen;
                curSpanId2 += (int32)minLen;
                spanLen2 -= (.)minLen;
            }
        }

        public static IdSpan GetDefault(int32 length)
        {            
            uint8[] idData = new uint8[8];
            int encodeIdx = 0;
            Utils.EncodeInt(idData, ref encodeIdx, (int32)-length);
            Utils.EncodeInt(idData, ref encodeIdx, 0);

            IdSpan idSpan = IdSpan();
            idSpan.mData = idData;
            idSpan.mLength = (int32)encodeIdx;
            return idSpan;
        }

		public override void ToString(String strBuffer)
		{
			ToString(strBuffer, "", null);
		}

		public void Dump() mut
		{
			Prepare();
			Debug.WriteLine("IdSpan Dump:");
		    int encodeIdx = 0;
		    int charId = 1;
		    int charIdx = 0;
		    while (true)
		    {
		        int32 cmd = Utils.DecodeInt(mData, ref encodeIdx);
		        if (cmd > 0)
		        {
					charId = cmd;
					Debug.WriteLine(" Id: {0}", charId);
				}
		        else
		        {
		            int32 spanSize = -cmd;
		            
		            charId += spanSize;
		            charIdx += spanSize;

		            if (cmd == 0)
		                return;

					Debug.WriteLine(" Len: {0}", spanSize);
		        }
		    }
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (HasChangeList)
			{
				IdSpan span = Duplicate();
				span.ToString(outString, format, formatProvider);
				return;
			}

			outString.AppendF($"Span(Length:{mLength} ChangeList:{(mChangeList?.Count).GetValueOrDefault()})");

			if (format == "D")
			{
				outString.Append("{");
				int encodeIdx = 0;
				int charId = 1;
				int charIdx = 0;
				while (true)
				{
				    int32 cmd = Utils.DecodeInt(mData, ref encodeIdx);
				    if (cmd > 0)
				    {
						charId = cmd;
						outString.AppendF($" #{charId}");
					}
				    else
				    {
				        int32 spanSize = -cmd;
				        
				        charId += spanSize;
				        charIdx += spanSize;

				        if (cmd == 0)
						{
							outString.AppendF($"}} EndIdx:{charIdx}");
				            return;
						}

						outString.AppendF($":{spanSize}");
				    }
				}
			}
		}

		public int GetHashCode() mut
		{
			Prepare();
			var hash = MD5.Hash(.(mData, 0, mLength));
			return *(int*)&hash;
		}
    }
}
