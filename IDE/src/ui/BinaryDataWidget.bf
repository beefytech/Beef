using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Globalization;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.geom;
using Beefy.events;
using Beefy.utils;
using Beefy;
using System.Diagnostics;
using System.Security.Cryptography;

namespace IDE.ui
{
    public interface IBinaryDataContentProvider
    {
        bool IsReadOnly { get; }
        uint8[] ReadBinaryData(int offset, int32 size);
        void WriteBinaryData(uint8[] data, int offset);
    }

    class TestBinaryContentDataProvider : IBinaryDataContentProvider
    {
        public bool IsReadOnly { get { return true; } }

        public uint8[] ReadBinaryData(int offset, int32 size)
        {
            uint8[] result = new uint8[size];
            for (int32 i=0; i<size; ++i)
                result[i] = (uint8)(((int32)offset + i) & 255);
            return result;
        }
        public void WriteBinaryData(uint8[] data, int offset)
        {
        }
    }

    public class BinaryDataContent
    {
        public class LockedRange : IDisposable
        {
            bool mDisposed = false;
            BinaryDataContent mContent;
            public int mBaseOffset;
            public uint8[] mData ~ delete _;
            public bool mModified;

            public this(BinaryDataContent content, int baseOffset, uint8[] data)
            {
                mContent = content;
                mBaseOffset = baseOffset;
                mData = data;
                mModified = false;
            }

            public ~this()
            {
                Dispose(true);
            }

			public void Dispose()
			{
				Dispose(true);
			}

            public void Dispose(bool disposing)
            {
                if (disposing)
                {
                    if (!mDisposed)
                    {
                        mContent.UnlockRange(this);
                        mDisposed = true;
                    }
                }
            }
        }

        public class Snapshot
        {
            public int mBaseOffset;
            public uint8[] mData ~ delete _;

            public this()
            {
            }

            public this(LockedRange lockRange)
            {
                mBaseOffset = lockRange.mBaseOffset;
                mData = new uint8[lockRange.mData.Count];
                Array.Copy(lockRange.mData, 0, mData, 0, mData.Count);
            }
        }

        public class BinaryChangeAction : UndoAction
        {
            BinaryDataWidget mWidget;
            int mOffset;
            uint8 mOldValue;
            uint8 mNewValue;
            uint8 mSubChar;
            bool mInStrView;

            public this(BinaryDataWidget widget, int offset, uint8 oldValue, uint8 newValue, uint8 subChar, bool inStrView)
            {
                mWidget = widget;
                mOffset = offset;
                mOldValue = oldValue;
                mNewValue = newValue;
                mSubChar = subChar;
                mInStrView = inStrView;
            }

            public override bool Undo()
            {
                return mWidget.ApplyUndoAction(mOffset, mNewValue, mOldValue, mSubChar, mInStrView, false);
            }
            public override bool Redo()
            {
                return mWidget.ApplyUndoAction(mOffset, mOldValue, mNewValue, mSubChar, mInStrView, true);
            }
        }

        public class SnapshotDiff
        {

            public int mBaseOffset;
            public bool[] mChanged ~ delete _;
            public Dictionary<int, bool> mExtraChanged ~ delete _;

            public this()
            {
            }

            public this(Snapshot oldShot, Snapshot newShot)
            {
                int oldShotLen = oldShot.mData != null ? (int)oldShot.mData.Count : 0;
                int newShotLen = newShot.mData != null ? (int)newShot.mData.Count : 0;

                int rangeMin = Math.Max(oldShot.mBaseOffset, newShot.mBaseOffset);
                int rangeMax = Math.Min(oldShot.mBaseOffset+oldShotLen, newShot.mBaseOffset+newShotLen);

                if (rangeMin < rangeMax)
                {
                    int changeLen = rangeMax - rangeMin;
                    
                    mBaseOffset = rangeMin;
                    mChanged = new bool[(int32)changeLen];

                    for (int i=0; i<changeLen; ++i)
                        mChanged[(int32)i] = oldShot.mData[(int32)(i + mBaseOffset - oldShot.mBaseOffset)] != newShot.mData[(int32)(i + mBaseOffset - newShot.mBaseOffset)];
                }
            }

            public bool Changed(int offset)
            {
                bool changed = false;
                if (mChanged != null)
                {
                    if ((offset >= mBaseOffset) && (offset < (mBaseOffset + (int)mChanged.Count)))
                        changed = mChanged[(int32)(offset - mBaseOffset)];
                }
                if (!changed && mExtraChanged != null)
                    mExtraChanged.TryGetValue(offset, out changed);
                return changed;
            }

            public void SetChanged(int offset)
            {
                if (mChanged != null)
                {
                    if ((offset >= mBaseOffset) && (offset < (mBaseOffset + (int)mChanged.Count)))
                    {
                        mChanged[(int32)(offset - mBaseOffset)] = true;
                        return;
                    }
                }
                if (mExtraChanged == null)
                    mExtraChanged = new Dictionary<int,bool>();
                mExtraChanged[offset] = true;
            }
        }

        class Page
        {
            public int mPageKey;
            public int mBaseOffset;
            public uint8[] mPageData ~ delete _;
            public int32 mAge;
            public int32 mLockCount;
        }

        IBinaryDataContentProvider mProvider;
        Dictionary<int, Page> mPageDict = new Dictionary<int, Page>() ~ { for (let page in _.Values) delete page; delete _; };
        readonly int mPageSize;
        public int mCacheRevision;
        const int32 mMaxCachePages = 50;

        public this(int pageSize, IBinaryDataContentProvider provider)
        {
            mPageSize = pageSize;
            mProvider = provider;
            mCacheRevision = 1;
        }

		public ~this()
		{

		}

        public LockedRange LockRange(int offset, int size)
        {
            //System.Diagnostics.Stopwatch sw = new System.Diagnostics.Stopwatch();
            //sw.Start();

            uint8[] data = new uint8[(int32)size];
            
            int remainingSize = size;
            int writeOfs = 0;
            int readOfs = (int)offset % mPageSize;
            int curPage = (int)offset / mPageSize;

            while (remainingSize > 0UL)
            {
                Page p = GetPage(curPage);
                ++p.mLockCount;

                int readSize = Math.Min((int)p.mPageData.Count - readOfs, remainingSize);
                Array.Copy(p.mPageData, (int32)readOfs, data, (int32)writeOfs, (int32)readSize);

                remainingSize -= readSize;
                writeOfs += readSize;
                readOfs = 0;
                ++curPage;
            }

            UpdatePageCache();

            //sw.Stop();
            //double elapsedMs = sw.Elapsed.TotalMilliseconds;
            //if (elapsedMs > 0.001)
              //  Console.WriteLine("STOPWATCH(LockRange): {0}ms", elapsedMs);

            return new LockedRange(this, offset, data);
        }

        public void UnlockRange(LockedRange range)
        {
            int remainingSize = (int)range.mData.Count;
            int readOfs = 0;
            int writeOfs = range.mBaseOffset % mPageSize;
            int curPage = range.mBaseOffset / mPageSize;
            
            while (remainingSize > 0UL)
            {
                Page p = GetPage(curPage);
                //System.Diagnostics.Trace.Assert(p.mLockCount > 0);

                int writeSize = Math.Min((int)p.mPageData.Count - writeOfs, remainingSize);
                if (range.mModified && !mProvider.IsReadOnly)
                {
                    Array.Copy(range.mData, (int32)readOfs, p.mPageData, (int32)writeOfs, (int32)writeSize);
                    CommitPage(p);
                }
                
                --p.mLockCount;
                
                remainingSize -= writeSize;
                readOfs += writeSize;
                writeOfs = 0;
                ++curPage;
            }

            if (range.mModified)
            {
                IDEApp.sApp.mWatchPanel.MarkWatchesDirty(false);
            }
        }

        Page GetPage(int pageNum)
        {
            Page p;
            if (!mPageDict.TryGetValue(pageNum, out p))
            {
                p = new Page();
                p.mPageKey = pageNum;
                p.mBaseOffset = pageNum * mPageSize;
                p.mPageData = mProvider.ReadBinaryData((int)p.mBaseOffset, (int32)mPageSize);
                p.mLockCount = 0;
                
                mPageDict[p.mPageKey] = p;
            }

            p.mAge = 0;

            return p;
        }


        void CommitPage(Page p)
        {
            mProvider.WriteBinaryData(p.mPageData, (int)p.mBaseOffset);
        }

        void UpdatePageCache()
        {
            for (var p in mPageDict)
            {
                if (p.value.mLockCount == 0)
                    p.value.mAge++;
            }

            if (mPageDict.Count > mMaxCachePages)
            {
				List<Page> orderedPages = scope List<Page>();
				for (var page in mPageDict.Values)
					orderedPages.Add(page);
				orderedPages.Sort(scope (lhs, rhs) => lhs.mAge <=> rhs.mAge);

				for (int i = mMaxCachePages; i < orderedPages.Count; ++i)
				{
					var page = orderedPages[i];
				    if (page.mLockCount > 0)
				        continue;
				    mPageDict.Remove(page.mPageKey);
					delete page;
				}
				
                /*List<Page> pages = mPageDict.Values.OrderBy(p => p.mAge).ToList();
                for (int i=mMaxCachePages; i<mPageDict.Values.Count; ++i)
                {
                    if (pages[i].mLockCount > 0)
                        continue;

                    mPageDict.Remove(pages[i].mPageKey);
                }*/
            }
        }

        public void ClearCache()
        {
            for (var p in mPageDict)
			{
				delete p.value;
                //System.Diagnostics.Trace.Assert(p.Value.mLockCount == 0); //CDH cache clearing not currently threadsafe w/ respect to locked ranges
			}

            mPageDict.Clear();
            ++mCacheRevision;
        }
    }

#pragma warning disable 0067

    public class BinaryDataWidget : DockedWidget
    {
		public enum AutoResizeType
		{
			Manual,
			Auto,
			Auto_Mul8,
		}

		public DarkButton mGotoButton;
		public AutoResizeType mAutoResizeType = .Auto_Mul8;
        public int mBytesPerDisplayLine;
        public int mCurPosition;
        double mCurPositionDisplayOffset; //0.0-1.0
		double mShowPositionDisplayOffset; //0.0-1.0
        Font mFont;
		bool mCacheDirty;
		bool mTrackedExprsDirty;

        IBinaryDataContentProvider mProvider;
        BinaryDataContent mContent ~ delete _;
        
        public InfiniteScrollbar mInfiniteScrollbar;
        public Insets mScrollbarInsets = new Insets() ~ delete _;

        int mColumnDisplayStart;
        int mColumnDisplayStride;
        public int mColumnHeaderHeight;
        int mStrViewDisplayStartOffset;
        int mStrViewDisplayStride;
        int mRegLabelDisplayStart;

        //EditWidget mByteEditWidget;
        Selection mCurHoverSelection = null ~ delete _;
        //Selection mCurEditSelection = null;
        Cursor mCurKeyCursor ~ delete _;

		Cursor mSelection ~ delete _;
        int32 mCurKeyCursorBlinkTicks = 0;
		Cursor mDraggingSelectionStart = null;
        //string mCurKeyCursorText = null;

        BinaryDataContent.Snapshot mLastSnapshot = new BinaryDataContent.Snapshot() ~ delete _;
        BinaryDataContent.SnapshotDiff mLastSnapshotDiff = new BinaryDataContent.SnapshotDiff() ~ delete _;
        int mLastSnapshotCacheRevision = 0;

        UndoManager mUndoManager = new UndoManager() ~ delete _;

        List<TrackedExpression> mTrackedExprs = new List<TrackedExpression>() ~ DeleteContainerAndItems!(_);
		public HashSet<String> mPinnedTrackedExprs = new HashSet<String>() ~ DeleteContainerAndItems!(_);
        public delegate void TrackedExpressionsUpdatedDelegate(List<TrackedExpression> trackedExpressions);
        public Event<TrackedExpressionsUpdatedDelegate> TrackedExpressionsUpdated ~ _.Dispose();
        int mTrackedExprRangeStart;
        int mTrackedExprRangeSize;
        int mTrackedExprUpdateCnt;
        float mDelayedClearTrackedExprsTimer;

        public delegate void KeyCursorUpdatedDelegate();
        public Event<KeyCursorUpdatedDelegate> OnKeyCursorUpdated ~ _.Dispose();

        public delegate void ActiveWidthChangedDelegate();
        public Event<ActiveWidthChangedDelegate> OnActiveWidthChanged ~ _.Dispose();

		public Event<Action> mOnGotoAddress ~ _.Dispose();

        public class Cursor
        {
            public int mSelStart;
            public int mSelLength; //CDH TODO length ignored for now
            public int32 mSubChar;
            public bool mInStrView;

            public this()
            {
                mSelStart = mSelLength = 0;
                mSubChar = 0;
                mInStrView = false;
            }

            public this(int selStart, int selLength, int subChar, bool inStrView)
            {
                mSelStart = selStart;
                mSelLength = selLength;
                mSubChar = (.)subChar;
                mInStrView = inStrView;
            }

            public this(Selection fromSelection)
            {
                mSelStart = fromSelection.mSelStart;
                mSelLength = fromSelection.mSelLength;
                mSubChar = fromSelection.mSubChar;
                mInStrView = fromSelection.mInStrView;
            }
        }
        public class Selection
        {
            public Rect mBinRect;
            public Rect mStrRect;
            public int mSelStart;
            public int mSelLength;
            public int mBytePosX;
            public int mBytePosY;
            public int32 mSubChar;
            public bool mInStrView;

            public this()
            {
                mBinRect = Rect();
                mStrRect = Rect();
                mSelStart = mSelLength = 0;
                mBytePosX = mBytePosY = 0;
                mSubChar = 0;
                mInStrView = false;
            }
            public this(Rect binRect, Rect strRect, int selStart, int selLength, int bytePosX, int bytePosY, int32 subChar, bool inStrView)
            {
                mBinRect = binRect;
                mStrRect = strRect;
                mSelStart = selStart;
                mSelLength = selLength;
                mSubChar = subChar;
                mInStrView = inStrView;
            }
        }

        public class TrackedExpression
        {
			public BinaryDataWidget mBinaryDataWidget;
            public String mExpr ~ delete _;
            public int mMemStart;
            public int mSize;
            public int mCurValue;
            public String mError ~ delete _;
            public String mDisplayName ~ delete _;
            public uint32 mDisplayColor;
            public bool mIsReg;
            public bool mIsVisible;
			public Rect? mDrawRect;

            public this(BinaryDataWidget binaryDataWidget, StringView expr, int memStart, int size, StringView displayName, uint32 displayColor, bool isReg)
            {
				mBinaryDataWidget = binaryDataWidget;
                mExpr = new String(expr);
                mMemStart = memStart;
                mSize = size;
                mDisplayName = new String(displayName);
                mDisplayColor = displayColor;
                mIsReg = isReg;
            }

			public ~this()
			{

			}

            public void Update(int visibleMemoryRangeStart, int visibleMemoryRangeLen)
            {
                if (mMemStart != 0UL)
                {
                    mCurValue = mMemStart;
                }
                else
                {
					DeleteAndNullify!(mError);

                    String evalResultStr = scope String();
                    IDEApp.sApp.mDebugger.Evaluate(mExpr, evalResultStr, -1);
                    if (evalResultStr.StartsWith("!", StringComparison.Ordinal))
                    {
                        mError = new String(evalResultStr);
                    }
                    else
                    {
                        mError = null;

                        var lines = scope List<StringView>(evalResultStr.Split('\n'));
                        if (lines.Count > 0)
                        {
                            String valStr = scope String(lines[0]);
                            if (valStr.Contains(' '))
                            {
                                var tempSplit = scope List<StringView>(valStr.Split(' '));
                                valStr.Set(tempSplit[0]);
                            }

                            if (!EvalAddressExpression(valStr, out mCurValue))
                                mError = new String("!Integer expression required");
                        }
                        else
                            mError = new String("!Invalid expression format");
                    }
                }

                int rangeMin = Math.Max(visibleMemoryRangeStart, mCurValue);
                int rangeMax = Math.Min(visibleMemoryRangeStart + visibleMemoryRangeLen, mCurValue + mSize);
                mIsVisible = (rangeMax >= rangeMin) || (mBinaryDataWidget.mPinnedTrackedExprs.Contains(mDisplayName)) || (mDisplayName.StartsWith("$"));
            }

            //CDH TODO duplicate from memory panel.  Need some shared utilities here?
            bool EvalAddressExpression(String expr, out int position)
            {
                position = 0;
                StringView parseStr = expr;
                bool parsed = false;

				NumberStyles numberStyle = .Number;
                if (parseStr.StartsWith("0x", StringComparison.CurrentCultureIgnoreCase))
				{
                    parseStr = StringView(parseStr, 2);
					numberStyle = .HexNumber;
				}
                
				switch (uint64.Parse(parseStr, numberStyle))
				{
				case .Ok(let val):
					position = (.)val;
					parsed = true;
				default:
				}
            
                return parsed;
            }

        }

        public this(IBinaryDataContentProvider provider)
        {
			mGotoButton = new DarkButton();
			mGotoButton.Label = "Goto...";
			AddWidget(mGotoButton);
			mGotoButton.mOnMouseClick.Add(new (evt) =>
				{
					mOnGotoAddress();
				});

             mProvider = provider;
            if (mProvider == null)
                mProvider = new TestBinaryContentDataProvider();
            mContent = new BinaryDataContent(64, mProvider); //CDH TODO really low page size for now so we can test boundary conditions

            mClipGfx = true;
            mClipMouse = true;

            mBytesPerDisplayLine = 16;
            mCurPosition = 0x10000000;
            mCurPositionDisplayOffset = 0.0f;

            mColumnDisplayStart = 150;
            mColumnDisplayStride = 25;
            mColumnHeaderHeight = 23;
            mStrViewDisplayStartOffset = 10;
            mStrViewDisplayStride = 8;
            mRegLabelDisplayStart = 110;                        

            mFont = IDEApp.sApp.mCodeFont;//DarkTheme.sDarkTheme.mSmallFont;
        }

		public ~this()
		{
		}

        public void InitScrollbar()
        {
            mInfiniteScrollbar = ThemeFactory.mDefault.CreateInfiniteScrollbar();
            mInfiniteScrollbar.Init();
            mInfiniteScrollbar.mOnInfiniteScrollEvent.Add(new => ApplyScrollDelta);
            AddWidgetAtIndex(0, mInfiniteScrollbar);
        }

        public virtual void UpdateScrollbar()
        {            
            if (mInfiniteScrollbar != null)
            {
                mInfiniteScrollbar.Resize(mWidth - mScrollbarInsets.mRight - mInfiniteScrollbar.mBaseSize, mScrollbarInsets.mTop, mInfiniteScrollbar.mBaseSize,
                    mHeight - mScrollbarInsets.mTop - mScrollbarInsets.mBottom);

                float lineSpacing = GetLineSpacing();
                int lineCount = Math.Max(1, (int)(mHeight / lineSpacing) - 3);
                mInfiniteScrollbar.mScrollDeltaLevelAmount = (double)lineCount;
                
                mInfiniteScrollbar.ScrollDeltaLevel(0);
                mInfiniteScrollbar.ResizeContent();
            }

            //UpdateScrollbarData();
            //ScrollPositionChanged();
        }

        public float GetActiveWidth()
        {
            return GS!(mColumnDisplayStart) + mBytesPerDisplayLine*GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStartOffset) + mBytesPerDisplayLine*GS!(mStrViewDisplayStride) + GS!(10);
        }

        float GetLineSpacing()
        {
            return mFont.GetLineSpacing();// + 4.0f;
        }

        void SetColumns(int columnCount)
        {
            mBytesPerDisplayLine = columnCount;
			if ((mBytesPerDisplayLine & 15) == 0)
				mCurPosition &= ~15;
			else if ((mBytesPerDisplayLine & 7) == 0)
				mCurPosition &= ~7;
			
            if (OnActiveWidthChanged.HasListeners)
                OnActiveWidthChanged();
        }

        public void ShowMenu(float x, float y)
        {
            Menu menu = new Menu();

			var menuItem = menu.AddItem("Column width");
			var columnChoice = menuItem.AddItem("Auto");
			if (mAutoResizeType == .Auto)
				columnChoice.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Check);
			columnChoice.mOnMenuItemSelected.Add(new (evt) =>
				{
					mAutoResizeType = .Auto;
					RehupSize();
				});

			columnChoice = menuItem.AddItem("Auto multiples of 8");
			if (mAutoResizeType == .Auto_Mul8)
				columnChoice.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Check);
			columnChoice.mOnMenuItemSelected.Add(new (evt) =>
				{
					mAutoResizeType = .Auto_Mul8;
					RehupSize();
				});

            for (int32 c in scope int32[] { 4, 8, 16, 32, 64 })
            {
                columnChoice = menuItem.AddItem(StackStringFormat!("{0} bytes", c));
                if ((mAutoResizeType == .Manual) && (c == (int32)mBytesPerDisplayLine))
                    columnChoice.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Check);
                columnChoice.mOnMenuItemSelected.Add(new (evt) =>
					{
						mAutoResizeType = .Manual;
						SetColumns(c);
					});
            }
            //menu.AddItem(); // separatator

            //var menuItem = menu.AddItem("Fwee!");
            //menuItem.mMenuItemSelectedHandler = (evt) => { Console.WriteLine("BLARGO MENU!"); };

            /*menu.AddItem("Item 2");
            menu.AddItem();
            menu.AddItem("Item 3");*/

            MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);

            menuWidget.Init(this, x, y);
        }

        public override void Update()
        {
            base.Update();

			CheckClearData();
            ++mCurKeyCursorBlinkTicks;

			if (mHasFocus)
				MarkDirty();

            if (mDelayedClearTrackedExprsTimer > 0.0f)
            {
                mDelayedClearTrackedExprsTimer -= 0.01f;
                if (mDelayedClearTrackedExprsTimer <= 0.0f)
                {
                    ClearTrackedExprs(false);
                }
            }
        }

        public static void DrawMulticolorOutlineBox(Graphics g, uint32[] colors, float stride, float x, float y, float width, float height, float thickness = 1, float phase = 0)
        {
            if (colors.Count == 0)
                return;

            List<Rect>[] colorRects = new List<Rect>[colors.Count];
            for (int32 i=0; i<colorRects.Count; ++i)
                colorRects[i] = new List<Rect>();

			float usePhase = phase;
            usePhase = Math.Max(usePhase, 0.0f);
            int32 curColorIndex = (int32)usePhase;
            usePhase -= (float)curColorIndex;
            curColorIndex %= (int32)colors.Count;

            float curX = x;
            float curY = y;

            for (int32 iPart=0; iPart<4; ++iPart)
            {
                float remainingLen = ((iPart & 1) != 0) ? width : height;
                
                while (remainingLen > 0)
                {
                    float spanWidth = stride - (stride * usePhase);
                    int32 colorInc = 1;
                    if (spanWidth > remainingLen)
                    {
                        usePhase = 1.0f - ((spanWidth - remainingLen) / stride);
                        spanWidth = remainingLen;
                        colorInc = 0;
                    }
                    else
                    {
                        usePhase = 0;
                    }

                    switch (iPart)
                    {
                        case 0:
                            {
                                colorRects[curColorIndex].Add(Rect(curX, curY, thickness, spanWidth));
                                curY += spanWidth;
                            }
                            break;
                        case 1:
                            {
                                colorRects[curColorIndex].Add(Rect(curX, curY, spanWidth, thickness));
                                curX += spanWidth;
                            }
                            break;
                        case 2:
                            {
                                colorRects[curColorIndex].Add(Rect(curX, curY - spanWidth + thickness, thickness, spanWidth));
                                curY -= spanWidth;
                            }
                            break;
                        case 3:
                            {
                                colorRects[curColorIndex].Add(Rect(curX - spanWidth + thickness, curY, spanWidth, thickness));
                                curX -= spanWidth;
                            }
                            break;
                    }
            
                    curColorIndex = (curColorIndex + colorInc) % (int32)colors.Count;
                    remainingLen -= spanWidth;
                }
            }

            for (int32 iColor=0; iColor<colorRects.Count; ++iColor)
            {
                var rects = colorRects[iColor];

                using (g.PushColor(colors[iColor]))
                {
                    for (var r in rects)
                        g.FillRect(r.mX, r.mY, r.mWidth, r.mHeight);
                }
            }
        }

        public BinaryDataContent.LockedRange GetLockedRangeAtCursor(int32 size)
        {
            if (mCurKeyCursor == null)
                return null;

            return mContent.LockRange(mCurKeyCursor.mSelStart, (int)size);
        }

        class CoverageEntry
        {
            public List<TrackedExpression> mRegCoverage;
            public List<TrackedExpression> mVarCoverage;
            public bool mIsSelected;
			public bool mIsShared;
        }

		struct TrackedEntry
		{
			public float mStartY;
			public float mGoalY;
			public int mLineIdx;
			public int mRegRankForLine;
			public int mColIdx;

			public this(float startY, float goalY, int lineIdx, int regRankForLine, int colIdx)
			{
				mStartY = startY;
				mGoalY = goalY;
				mLineIdx = lineIdx;
				mRegRankForLine = regRankForLine;
				mColIdx = colIdx;
			}
		}

		public override void Draw(Graphics g)
		{
			using (g.PushClip(0, 0, mWidth - GS!(18), mHeight - GS!(18)))
				DoDraw(g);
		}

        public void DoDraw(Graphics g)
        {
            base.Draw(g);

			for (var te in mTrackedExprs)
				te.mDrawRect = null;

			int barThickness = (int)GS!(1.5f);

            /*
            //g.DrawString(string.Format("CacheRev: {0}", mContent.mCacheRevision), 150, 0);
            if (mInfiniteScrollbar != null)
            {
                g.DrawString(string.Format("Thumb: {0}, Accel: {1}, Vel: {2}, DO: {3}", mInfiniteScrollbar.mScrollThumbFrac, mInfiniteScrollbar.mScrollAccel, mInfiniteScrollbar.mScrollVelocity, mCurPositionDisplayOffset), 150, 30);
            }
            */            
            // column header
            using (g.PushClip(0, 0, mWidth, GS!(mColumnHeaderHeight)))
            {
                using (g.PushColor(0xFFFFFFFF))
                {
                    g.SetFont(mFont);
                    float strViewColumnStart = GS!(mColumnDisplayStart) + mBytesPerDisplayLine*GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStartOffset);

                    for (int i=0; i<mBytesPerDisplayLine; ++i)
                    {
                        g.DrawString(StackStringFormat!("{0:X1}", i), GS!(mColumnDisplayStart) + i*GS!(mColumnDisplayStride) + GS!(mColumnDisplayStride)*0.125f - ((i > 0xF) ? GS!(4) : 0), GS!(3), FontAlign.Left);
                        g.DrawString(StackStringFormat!("{0:X1}", i & 0xF), strViewColumnStart + i*GS!(mStrViewDisplayStride), GS!(3), FontAlign.Left);
                    }
                }
            }

            // binary content

			float lineSpacing = GetLineSpacing();
			int lineCount = (int)(mHeight / lineSpacing) + 3;
			int minAddr = mCurPosition;
			int maxAddr = mCurPosition + lineCount*mBytesPerDisplayLine;
			BumpAllocator bumpAlloc = scope BumpAllocator();

            using (g.PushClip(0, GS!(mColumnHeaderHeight) + GS!(2), mWidth, mHeight - GS!(mColumnHeaderHeight) - GS!(4)))
            {
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditBox), 0, GS!(mColumnHeaderHeight), mWidth, mHeight - GS!(mColumnHeaderHeight));

				if (!gApp.mDebugger.mIsRunning)
					return;
                
                float displayAdj = (float)(-mShowPositionDisplayOffset * lineSpacing);
                using (g.PushTranslate(0, displayAdj))
                {
                    using (g.PushColor(0xFFFFFFFF))
                    {
                        //ulong lineStart = mCurPosition / mBytesPerDisplayLine;
                        int lockSize = lineCount * mBytesPerDisplayLine;
#unwarn
                        float strViewColumnStart = GS!(mColumnDisplayStart) + mBytesPerDisplayLine*GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStartOffset);
#unwarn
                        Selection curKeySelection = (mCurKeyCursor != null) ? GetSelectionForCursor(mCurKeyCursor, false) : null;
						defer { delete curKeySelection; }

                        //ulong lineEnd = Math.Min((ulong)long.MaxValue, lineStart + (ulong)(mHeight / lineSpacing) + 3);

						var lockRange = mContent.LockRange(mCurPosition, lockSize);
						defer delete lockRange;

						TrackedBlock:
                        {
                            if (mLastSnapshotCacheRevision != mContent.mCacheRevision)
                            {
								defer delete mLastSnapshotDiff;
								defer delete mLastSnapshot;
                                var curSnapshot = new BinaryDataContent.Snapshot(lockRange);
                                mLastSnapshotDiff = new BinaryDataContent.SnapshotDiff(mLastSnapshot, curSnapshot);
                                mLastSnapshot = curSnapshot;
                                mLastSnapshotCacheRevision = mContent.mCacheRevision;
                            }

							//TrackedE

                            //var trackedCoverage = scope Dictionary<int, CoverageEntry>();
							CoverageEntry[] trackedCoverage = new:bumpAlloc CoverageEntry[maxAddr - minAddr];

							bool GetTrackedCoverage(int addr, out CoverageEntry value)
							{
								if ((addr < minAddr) || (addr >= maxAddr))
								{
									value = default;
									return false;
								}
								value = trackedCoverage[addr - minAddr];
								return value != null;
							}

							CoverageEntry DuplicateCoverageEntry(CoverageEntry prevEntry)
							{
								var entry = new:bumpAlloc CoverageEntry();
								if (prevEntry.mRegCoverage != null)
								{
									entry.mRegCoverage = new:bumpAlloc List<TrackedExpression>();
									for (var val in prevEntry.mRegCoverage)
										entry.mRegCoverage.Add(val);
								}
								if (prevEntry.mVarCoverage != null)
								{
									entry.mVarCoverage = new:bumpAlloc List<TrackedExpression>();
									for (var val in prevEntry.mVarCoverage)
										entry.mVarCoverage.Add(val);
								}
								return entry;
							}

                            //var trackedRegCoverage = new Dictionary<ulong, List<TrackedExpression>>();
                            //var trackedVarCoverage = new Dictionary<ulong, List<TrackedExpression>>();
                            //var trackedRegRanks = scope Dictionary<TrackedExpression, int32>();
                            //var trackedVarRanks = scope Dictionary<TrackedExpression, int32>();
                            for (var te in mTrackedExprs)
                            {
                                if (te.mError != null)
                                    continue;
                                if (!te.mIsVisible)
                                    continue;

								if (te.mDisplayName.StartsWith("$"))
									continue;

								TrackedEntryBlock: do
                                {
                                    int useSize = te.mIsReg ? 1 : te.mSize;
									int lowAddr = Math.Max(te.mCurValue, minAddr);
									int hiAddr = Math.Min(te.mCurValue + useSize, maxAddr);
									if (lowAddr == hiAddr)
										break TrackedEntryBlock;

									CoverageEntry entry = new:bumpAlloc CoverageEntry();
									entry.mIsShared = true;
                                    for (int ofs = lowAddr; ofs < hiAddr; ++ofs)
                                    {
										int idx = ofs - minAddr;

										var curCoverage = trackedCoverage[idx];
										if (curCoverage == null)
										{
											curCoverage = entry;
										}
										else if (curCoverage.mIsShared)
										{
											curCoverage = DuplicateCoverageEntry(curCoverage);
										}
										trackedCoverage[idx] = curCoverage;

                                        List<TrackedExpression> offsetCoverage = te.mIsReg ? curCoverage.mRegCoverage : curCoverage.mVarCoverage;
                                        if (offsetCoverage == null)
                                        {
                                            offsetCoverage = new:bumpAlloc List<TrackedExpression>();
                                            if (te.mIsReg)
                                                curCoverage.mRegCoverage = offsetCoverage;
                                            else
                                                curCoverage.mVarCoverage = offsetCoverage;
                                        }

										offsetCoverage.Add(te);
                                    }
                                }
                            }
                            if (mSelection != null)
                            {
								int lowAddr = Math.Max(mSelection.mSelStart, minAddr);
								int hiAddr = Math.Min(mSelection.mSelStart + mSelection.mSelLength, maxAddr);
								for (int ofs = lowAddr; ofs < hiAddr; ++ofs)
								{
									CoverageEntry entry = trackedCoverage[ofs - minAddr];
									if (entry == null)
										entry = new:bumpAlloc CoverageEntry();
									else if (entry.mIsShared)
										entry = DuplicateCoverageEntry(entry);
									entry.mIsSelected = true;
									trackedCoverage[ofs - minAddr] = entry;
								}
                            }

							var trackedRegYDict = scope Dictionary<TrackedExpression, TrackedEntry>(); // start Y, goal Y, lineIdx, regRankForLine, colIdx
                            bool[] trackedRegYShortLines = scope bool[lineCount]; // in case we have a reg arrow at the first byte of the row
                            var trackedRegsByLine = scope List<TrackedExpression>[lineCount];

                            for (int iPass=0; iPass<2; ++iPass)
                            {
                                float curTrackedRegY = 0.0f;

                                for (int lineIdx = 0; lineIdx < lineCount; lineIdx++)
                                {
                                    int baseOffset = lineIdx * mBytesPerDisplayLine;

                                    curTrackedRegY = Math.Max(curTrackedRegY, GS!(mColumnHeaderHeight) + (lineIdx * lineSpacing));

                                    if (iPass == 0)
                                    {
                                        //g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
                                        g.SetFont(IDEApp.sApp.mTinyCodeFont);
										var str = scope String();
										str.AppendF("{0:X16}", mCurPosition + baseOffset);
										str.Insert(8, '\'');
										int spaceCount = 0;
										for (int i < 7)
										{
											if (str[i] == '0')
												spaceCount++;
										}
										str.Remove(0, spaceCount);

                                        g.DrawString(str, GS!(4), GS!(mColumnHeaderHeight) + (lineIdx * lineSpacing), FontAlign.Left);

                                        g.SetFont(mFont);

                                        if (curKeySelection != null)
                                        {
                                            int relStart = (int)curKeySelection.mSelStart - (int)mCurPosition;
                                            if ((relStart - (relStart % mBytesPerDisplayLine)) == baseOffset)
                                            {
                                                using (g.PushColor(0x40808000))
                                                {
                                                    g.FillRect(GS!(mColumnDisplayStart), curKeySelection.mBinRect.mY - displayAdj, (strViewColumnStart + mBytesPerDisplayLine*GS!(mStrViewDisplayStride)) - mColumnDisplayStart, curKeySelection.mBinRect.mHeight);
                                                }
                                            }
                                        }
                                    }

                                    for (int i=0; i<mBytesPerDisplayLine; ++i)
                                    {
                                        if (iPass == 1)
                                        {
                                            // binary view

                                            IDisposable colorScope = null;
                                            uint8 val = lockRange.mData[baseOffset + i];
                                            if (mLastSnapshotDiff.Changed(mCurPosition + baseOffset + i))
                                            {
                                                colorScope = g.PushColor(0xff60C0D0);
                                            }

											var str = scope String();
											str.AppendF("{0:X2}", val);
                                            g.DrawString(str, GS!(mColumnDisplayStart) + i*GS!(mColumnDisplayStride), GS!(mColumnHeaderHeight) + (lineIdx * lineSpacing), FontAlign.Left);
                                            if (colorScope != null)
                                            {
                                                colorScope.Dispose();
                                                colorScope = null;
                                            }

                                            // string view

                                            char8 valChar = (char8)(val);
                                            if (valChar.IsControl || (valChar == '\u{2028}') || (valChar == '\u{2029}')) // unicode line/paragraph separators, also unprintable
                                            {
                                                valChar = '.';
                                                colorScope = g.PushColor(0xffffa000);
                                            }

											str.Clear();
											str.Append(valChar);
                                            g.DrawString(str, strViewColumnStart + i*GS!(mStrViewDisplayStride), GS!(mColumnHeaderHeight) + (lineIdx * lineSpacing), FontAlign.Left);
                                            if (colorScope != null)
                                            {
                                                colorScope.Dispose();
                                                colorScope = null;
                                            }

                                            if (mCurHoverSelection != null)
                                            {
                                                if ((mCurPosition + baseOffset + i) == mCurHoverSelection.mSelStart)
                                                {
                                                    Rect primaryRect = mCurHoverSelection.mInStrView ? mCurHoverSelection.mStrRect : mCurHoverSelection.mBinRect;
                                                    Rect secondaryRect = mCurHoverSelection.mInStrView ? mCurHoverSelection.mBinRect : mCurHoverSelection.mStrRect;
                                                    int cursorBarXAdj = mCurHoverSelection.mInStrView ? 0 : 3;

                                                    using (g.PushColor(0xffffffff))
                                                    {
                                                        g.FillRect(primaryRect.mX + cursorBarXAdj + GS!(8)*mCurHoverSelection.mSubChar + GS!(1), primaryRect.mY - displayAdj, GS!(2), primaryRect.mHeight);
                                                    }
                                                    using (g.PushColor(0xffc0c0c0))
                                                    {
                                                        g.OutlineRect(secondaryRect.mX, secondaryRect.mY - displayAdj, secondaryRect.mWidth, secondaryRect.mHeight, GS!(1));
                                                    }
                                                }
                                            }

											bool hasFocus = mHasFocus;
											if (!hasFocus)
											{
												if (mWidgetWindow.mHasFocus)
												{
													if (mWidgetWindow.mFocusWidget is MemoryRepListView)
														hasFocus = true;
													else if (mWidgetWindow.mFocusWidget is MemoryPanel)
														hasFocus = true;
												}
											}

                                            if ((curKeySelection != null) && (hasFocus))
                                            {
                                                if ((mCurPosition + baseOffset + i) == curKeySelection.mSelStart)
                                                {
                                                    Rect primaryRect = curKeySelection.mInStrView ? curKeySelection.mStrRect : curKeySelection.mBinRect;
                                                    Rect secondaryRect = curKeySelection.mInStrView ? curKeySelection.mBinRect : curKeySelection.mStrRect;

                                                    using (g.PushColor(0x80A0A0A0))
                                                    {
                                                        g.FillRect(secondaryRect.mX, secondaryRect.mY - displayAdj, secondaryRect.mWidth, secondaryRect.mHeight);
                                                    }
                                                
                                                    float brightness = (float)Math.Cos(Math.Max(0.0f, mCurKeyCursorBlinkTicks - 20) / 9.0f);                            
                                                    brightness = Math.Max(0, Math.Min(1.0f, brightness * 2.0f + 1.6f));
                                                    uint32 cursorBarColor = 0xffffff + ((uint32)(brightness * 128) << 24);
                                                    int cursorBarXAdj = curKeySelection.mInStrView ? 0 : GS!(3);
                                                    using (g.PushColor(cursorBarColor))
                                                    {
                                                        g.FillRect(primaryRect.mX + cursorBarXAdj + GS!(8)*mCurKeyCursor.mSubChar, primaryRect.mY - displayAdj, GS!(2), primaryRect.mHeight);
                                                    }
                                                }
                                            }
                                        }

                                        if (iPass == 0)
                                        {
                                            CoverageEntry coverageEntry = null;
                                            if (GetTrackedCoverage(mCurPosition + baseOffset + i, out coverageEntry))
                                            {
                                                List<TrackedExpression> coverage = coverageEntry.mRegCoverage;
                                                if (coverage != null)
                                                {
                                                    List<TrackedExpression> usedRanks = scope List<TrackedExpression>();
                                                    for (TrackedExpression c in coverage)
                                                    {
                                                        if (c != null)
                                                            usedRanks.Add(c);
                                                    }

                                                    Cursor tempCursor = scope Cursor(mCurPosition + baseOffset + i, 1, 0, false);
                                                    Selection tempSel = GetSelectionForCursor(tempCursor, false);
                                                    if (tempSel != null)
                                                    {
														defer delete tempSel;

                                                        //int cursorBarXAdj = 3;
                                                        Rect primaryRect = tempSel.mBinRect;
                                                        float imgX = primaryRect.mX - GS!(10);
                                                        float imgY = primaryRect.mY - displayAdj - GS!(1);

                                                        //System.Diagnostics.Trace.Assert(usedRanks.Count > 0);
                                                        if (usedRanks.Count == 1)
                                                        {
                                                            using (g.PushColor(usedRanks[0].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowSingle), imgX, imgY);
                                                            }
                                                        }
                                                        else if (usedRanks.Count == 2)
                                                        {
                                                            using (g.PushColor(usedRanks[0].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowDoubleTop), imgX, imgY);
                                                            }
                                                            using (g.PushColor(usedRanks[1].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowDoubleBottom), imgX, imgY);
                                                            }
                                                        }
                                                        else if (usedRanks.Count == 3)
                                                        {
                                                            using (g.PushColor(usedRanks[0].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowTripleTop), imgX, imgY);
                                                            }
                                                            using (g.PushColor(usedRanks[1].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowTripleMiddle), imgX, imgY);
                                                            }
                                                            using (g.PushColor(usedRanks[2].mDisplayColor))
                                                            {
                                                                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowTripleBottom), imgX, imgY);
                                                            }
                                                        }
                                                        else
                                                        {
                                                            g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MemoryArrowRainbow), imgX, imgY);
                                                        }
                                                    }
                                                }

                                                coverage = coverageEntry.mVarCoverage;
                                                if (coverage != null)
                                                {
                                                    List<TrackedExpression> usedRanks = scope List<TrackedExpression>();
                                                    for (TrackedExpression c in coverage)
                                                    {
                                                        if (c != null)
                                                            usedRanks.Add(c);
                                                    }

                                                    bool neighborCoverageFunc(int offsetAdj)
                                                    {
                                                        CoverageEntry neighborEntry = null;
                                                        bool showBorder = true;
                                                        if (GetTrackedCoverage((((int)mCurPosition + baseOffset + i) + offsetAdj), out neighborEntry))
                                                        {
                                                            List<TrackedExpression> neighborCoverage = neighborEntry.mVarCoverage;
                                                            if (neighborCoverage != null)
                                                            {
                                                                showBorder = false;

                                                                List<TrackedExpression> testRanks = scope List<TrackedExpression>();
                                                                for (TrackedExpression c in neighborCoverage)
                                                                {
                                                                    if (c != null)
                                                                        testRanks.Add(c);
                                                                }
                                                                if (testRanks.Count != usedRanks.Count)
                                                                {
                                                                    showBorder = true;
                                                                }
                                                                else
                                                                {
                                                                    for (int iRank=0; iRank<testRanks.Count; ++iRank)
                                                                    {
                                                                        if (testRanks[iRank] != usedRanks[iRank])
                                                                        {
                                                                            showBorder = true;
                                                                            break;
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }

                                                        return showBorder;
                                                    }

                                                    bool leftBorder = neighborCoverageFunc(-1);
                                                    bool rightBorder = neighborCoverageFunc(1);

                                                    Cursor tempCursor = scope Cursor(mCurPosition + baseOffset + i, 1, 0, false);
                                                    Selection tempSel = GetSelectionForCursor(tempCursor, false);
                                                    if (tempSel != null)
                                                    {
														defer delete tempSel;
                                                        //int cursorBarXAdj = 3;
                                                        Rect primaryRect = tempSel.mBinRect;
                                                        float rectX = primaryRect.mX;
                                                        float rectY = primaryRect.mY - displayAdj;
                                                        float rectW = primaryRect.mWidth + (rightBorder ? 0 : 1);
                                                        float rectH = primaryRect.mHeight - 1;
                                            
                                                        if ((i == 0) || leftBorder)
                                                        {
                                                            rectX += 4;
                                                            rectW -= 4;
                                                        }
                                                        if ((i == mBytesPerDisplayLine - 1) || rightBorder)
                                                        {
                                                            rectW -= 4;
                                                        }

                                                        Debug.Assert(usedRanks.Count > 0);
                                                        using (g.PushColor(usedRanks[0].mDisplayColor))
                                                        {
                                                            g.FillRect(rectX, rectY, rectW, 1);
                                                            g.FillRect(rectX, rectY + rectH, rectW, 1);

                                                            if (leftBorder)
                                                                g.FillRect(rectX, rectY + 1, 1, rectH - 1);
                                                            if (rightBorder)
                                                                g.FillRect(rectX + rectW, rectY, 1, rectH + 1);
                                                        }
                                                    }
                                                }
                                    
                                                if (coverageEntry.mIsSelected)
                                                {
                                                    bool neighborCoverageFunc(int offsetAdj)
                                                    {
                                                        CoverageEntry neighborEntry = null;
                                                        bool showBorder = true;
                                                        if (GetTrackedCoverage(((int)mCurPosition + baseOffset + i) + (int)offsetAdj, out neighborEntry))
                                                        {
                                                            showBorder = !neighborEntry.mIsSelected;
                                                        }
                                                        return showBorder;
                                                    }

                                                    bool leftBorder = neighborCoverageFunc(-1);
                                                    bool rightBorder = neighborCoverageFunc(1);

                                                    Cursor tempCursor = scope Cursor(mCurPosition + baseOffset + i, 1, 0, false);
                                                    Selection tempSel = GetSelectionForCursor(tempCursor, false);
                                                    if (tempSel != null)
                                                    {
														defer delete tempSel;
                                                        //int cursorBarXAdj = 3;
                                                        Rect primaryRect = tempSel.mBinRect;
                                                        float rectX = primaryRect.mX;
                                                        float rectY = primaryRect.mY - displayAdj;
                                                        float rectW = primaryRect.mWidth + (rightBorder ? 0 : 1);
                                                        float rectH = primaryRect.mHeight - 1;
                                            
                                                        rectY += 1;
                                                        rectH -= 1;
                                                        if (leftBorder)
                                                        {
                                                            rectX += 1;
                                                            rectW -= 1;
                                                        }

                                                        if ((i == 0) || leftBorder)
                                                        {
                                                            rectX += 4;
                                                            rectW -= 4;
                                                        }
                                                        if ((i == mBytesPerDisplayLine - 1) || rightBorder)
                                                        {
                                                            rectW -= 4;
                                                        }

                                                        using (g.PushColor(0xFF2f5c88)) //CDH TODO: same as DarkEditWidgetContent.mHiliteColor, link symbolically?
                                                        {
                                                            g.FillRect(rectX, rectY, rectW, rectH);
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        if (iPass == 0)
                                        {
                                            for (var te in mTrackedExprs)
                                            {
                                                if (te.mError != null)
                                                    continue;
                                                if (!te.mIsVisible)
                                                    continue;
                                                if (!te.mIsReg)
                                                    continue;

                                                if ((mCurPosition + baseOffset + i) == te.mCurValue)
                                                {
                                                    Cursor tempCursor = scope Cursor(te.mCurValue, 1, 0, false);
                                                    Selection tempSel = GetSelectionForCursor(tempCursor, false);
                                                    if (tempSel != null)
                                                    {
														defer delete tempSel;
                                                        var useFont = IDEApp.sApp.mTinyCodeFont;
                                                        //float strWidth = useFont.GetWidth(te.mExpr);

                                                        //int cursorBarXAdj = 3;
                                                        //Rect primaryRect = tempSel.mBinRect;
                                                        //float barX = primaryRect.mX + cursorBarXAdj;
                                                        //float barY = primaryRect.mY - displayAdj;

                                                        if (trackedRegsByLine[lineIdx] == null)
                                                            trackedRegsByLine[lineIdx] = scope:TrackedBlock List<TrackedExpression>();

                                                        int regRankForLine = trackedRegsByLine[lineIdx].Count;
                                                        trackedRegsByLine[lineIdx].Add(te);

                                                        trackedRegYDict[te] = TrackedEntry(curTrackedRegY, GS!(mColumnHeaderHeight) + (lineIdx * lineSpacing), lineIdx, regRankForLine, i);
                                                        curTrackedRegY += useFont.GetLineSpacing() + 4;
                                                    
                                                        if (i == 0)
                                                            trackedRegYShortLines[lineIdx] = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

							

                            for (var kvp in trackedRegYDict)
                            {
                                var te = kvp.key;
                                float barX = GS!(mRegLabelDisplayStart);
                                float barY = kvp.value.mStartY;
                                float barGoalY = kvp.value.mGoalY;
                                int lineIdx = kvp.value.mLineIdx;
                                int regRankForLine = kvp.value.mRegRankForLine;
                                int colIdx = kvp.value.mColIdx;
                                var useFont = IDEApp.sApp.mTinyCodeFont;
                                float strWidth = useFont.GetWidth(te.mExpr);
                                bool shortLine = trackedRegYShortLines[lineIdx];
								var useLineSpacing = useFont.GetLineSpacing();

								te.mDrawRect = Rect(barX, barY, strWidth + GS!(2), useLineSpacing + GS!(2));

								if (te.mDisplayName.StartsWith("$"))
									continue;

                                using (g.PushColor(te.mDisplayColor))
                                {
                                    var regsOnThisLine = trackedRegsByLine[lineIdx].Count;
                                    float barYAdj = barY + useLineSpacing*0.5f + GS!(1);
                                    float barGoalYAdj = barGoalY + useLineSpacing*0.5f + GS!(1);
                                    if (regsOnThisLine == 2)
                                        barGoalYAdj -= regsOnThisLine - regRankForLine*5;
                                    else if (regsOnThisLine == 3)
                                        barGoalYAdj -= regsOnThisLine - regRankForLine*3;
                                    else
                                        barGoalYAdj -= regsOnThisLine/2 - regRankForLine;

                                    int desiredLineWidth = shortLine ? GS!(5) : GS!(9);
                                    if (Math.Abs(barYAdj - barGoalYAdj) < 0.01f)
                                    {
                                        g.FillRect(barX + strWidth, barGoalYAdj, GS!(mColumnDisplayStart) - (barX + strWidth) - GS!(7), barThickness);
                                    }
                                    else
                                    {
                                        int firstPartWidth = GS!(2) + regRankForLine;
                                        g.FillRect(barX + strWidth, barYAdj, firstPartWidth, barThickness);
                                        g.FillRect(barX + strWidth + firstPartWidth, barGoalYAdj + barThickness, barThickness, barYAdj - barGoalYAdj);
                                        g.FillRect(barX + strWidth + firstPartWidth, barGoalYAdj, desiredLineWidth - firstPartWidth, barThickness);
                                    }

                                    for (int i=1; i<colIdx; ++i)
                                    {
                                        g.FillRect(GS!(mColumnDisplayStart) + i*GS!(mColumnDisplayStride)-GS!(6), barGoalYAdj, barThickness*2, barThickness);
                                    }

                                    g.FillRect(barX, barY + GS!(2), strWidth, useLineSpacing - 1);
                                }
                                                    
                                g.SetFont(useFont);
                                using (g.PushColor(0xffffffff))
                                {
                                    g.DrawString(te.mDisplayName, barX + GS!(2), barY + GS!(1), FontAlign.Left);
                                }
                                g.SetFont(mFont);
                            }
                        }
                    }
                }
            }
        }

        Selection GetSelectionForBytePosition(int ulx, int uly, int32 align, int32 length, int32 subChar, bool inStrView)
        {
            float lineSpacing = GetLineSpacing();
            float strViewColumnStart = GS!(mColumnDisplayStart) + mBytesPerDisplayLine*GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStartOffset);
            float displayAdj = (float)(-mShowPositionDisplayOffset) * lineSpacing;
            int lineCount = (int)(mHeight / lineSpacing) + 3;

            if ((ulx > mBytesPerDisplayLine) || (uly > lineCount))
                return null;

			var useUlx = ulx;
            if (align > 1)
                useUlx &= ~((int)align - 1);

            int selStart = (uly * mBytesPerDisplayLine) + useUlx + mCurPosition;
            int selLength = (int)length;

            Rect br = Rect();
            br.mX = GS!(mColumnDisplayStart) + useUlx*GS!(mColumnDisplayStride) - GS!(5);
            br.mY = GS!(mColumnHeaderHeight) + (uly * lineSpacing) + displayAdj;
            br.mWidth = GS!(mColumnDisplayStride) - 1;
            br.mHeight = lineSpacing;

            Rect sr = Rect();
            sr.mX = strViewColumnStart + useUlx*mStrViewDisplayStride;
            sr.mY = GS!(mColumnHeaderHeight) + (uly * lineSpacing) + displayAdj;
            sr.mWidth = mStrViewDisplayStride;
            sr.mHeight = lineSpacing;

            let selection = new Selection(br, sr, selStart, selLength, useUlx, uly, subChar, inStrView);
			return selection;
        }

        Selection GetSelectionForDisplayPosition(float x, float y, int32 align, int32 length)
        {
            float lineSpacing = GetLineSpacing();
            float strViewColumnStart = GS!(mColumnDisplayStart) + mBytesPerDisplayLine*GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStartOffset);
            float displayAdj = (float)(-mShowPositionDisplayOffset) * lineSpacing;

			float curX = x;
			float curY = y;

            curY -= displayAdj;
            curY -= GS!(mColumnHeaderHeight);
            curY /= lineSpacing;
        
            int lineCount = (int)(mHeight / lineSpacing) + 3;
            if ((curY < 0.0f) || (curY >= (float)lineCount))
                return null;

            bool inStrView = false;
            bool checkSubChar = true;
            float testX = curX;
            curX += 4; // offset by half a char8acter
            testX -= GS!(mColumnDisplayStart);
            testX /= (float)GS!(mColumnDisplayStride);
            if (testX < 0.0f)
                return null;
            if (testX >= (float)mBytesPerDisplayLine)
            {
                // check string view
                testX = curX;
                testX -= strViewColumnStart;
                testX /= GS!(mStrViewDisplayStride);
                if ((testX < 0.0f) || (testX >= (float)mBytesPerDisplayLine))
                    return null;
                
                inStrView = true;
                checkSubChar = false;
            }
            
            float origX = curX;
            curX = testX;

            int ulx = (int)curX;
            int uly = (int)curY;

            var result = GetSelectionForBytePosition(ulx, uly, align, length, 0, inStrView);

            if (checkSubChar && (result != null))
            {
                const int32 subCharsPerElement = 2; //CDH add support for multibyte
                float subCharFrac = (origX - result.mBinRect.mX) / result.mBinRect.mWidth;
                if (subCharFrac >= 1.0f && ulx < (mBytesPerDisplayLine - 1))
                {
                    ++ulx;
					delete result;
                    result = GetSelectionForBytePosition(ulx, uly, align, length, 0, inStrView);
                    subCharFrac = (origX - result.mBinRect.mX) / result.mBinRect.mWidth;
                }
                result.mSubChar = Math.Min(Math.Max(0, (int32)(subCharFrac * subCharsPerElement)), subCharsPerElement - 1);
            }

            return result;
        }

        Selection GetSelectionForCursor(Cursor cursor, bool ensureVisible)
        {
            //CDH TODO cursor length ignored for now; assume only single byte
            Selection result = null;
            if (cursor.mSelStart >= mCurPosition)
            {
                int ulx = (cursor.mSelStart - mCurPosition) % mBytesPerDisplayLine;
                int uly = (cursor.mSelStart - mCurPosition) / mBytesPerDisplayLine;
                result = GetSelectionForBytePosition(ulx, uly, 1, 1, cursor.mSubChar, cursor.mInStrView);
            }
            if ((result == null) && ensureVisible)
            {
                float lineSpacing = GetLineSpacing();
                int lineCount = Math.Max(1, (int)(mHeight / lineSpacing) - 2);

                // try and adjust current position if we're out of range

                int maxPosition = mCurPosition + (lineCount * mBytesPerDisplayLine);
                if (cursor.mSelStart < mCurPosition)
                {
                    int lineDelta = ((mCurPosition - cursor.mSelStart) / mBytesPerDisplayLine) + 2;
                    mCurPosition -= lineDelta * mBytesPerDisplayLine;
                    DirtyTrackedExprs();
                }
                else if (cursor.mSelStart >= maxPosition)
                {
                    int lineDelta = ((cursor.mSelStart - maxPosition) / mBytesPerDisplayLine) + 1;
                    mCurPosition += lineDelta * mBytesPerDisplayLine;
                    DirtyTrackedExprs();
                }
                
                result = GetSelectionForCursor(cursor, false);
            }
            return result;
        }

        public override void Resize(float x, float y, float width, float height)
        {
            //System.Diagnostics.Stopwatch sw = new System.Diagnostics.Stopwatch();
            //sw.Start();

            base.Resize(x, y, width, height);

			mGotoButton.Resize(GS!(6), GS!(2), GS!(132), GS!(20));

            ClearTrackedExprs(false);
            UpdateScrollbar();

			if (mAutoResizeType != .Manual)
			{
				int numColumns = (int)((mWidth - GS!(mColumnDisplayStart) - GS!(mStrViewDisplayStartOffset) - GS!(22)) / (GS!(mColumnDisplayStride) + GS!(mStrViewDisplayStride)));
				if (mAutoResizeType == .Auto_Mul8)
				{
					numColumns &= ~7;
					if (numColumns == 0)
						numColumns = 8;
				}
				if (numColumns < 4)
					numColumns = 4;
				SetColumns(numColumns);
			}

            //sw.Stop();
            //Console.WriteLine("STOPWATCH(Update): {0}ms", sw.Elapsed.TotalMilliseconds);
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

			//delete mCurHoverSelection;
            //mCurHoverSelection = GetSelectionForDisplayPosition(x, y, 1, 1);

			if (mDraggingSelectionStart != null)
			{
				let curSel = GetSelectionForDisplayPosition(x, y, 1, 1);
				if (curSel != null)
				{
					DeleteAndNullify!(mCurKeyCursor);
					DeleteAndNullify!(mSelection);
					mCurKeyCursor = new Cursor(curSel);
					mSelection = new Cursor(curSel);

					int leftSel = Math.Min(mCurKeyCursor.mSelStart, mDraggingSelectionStart.mSelStart);
					int rightSel = Math.Max(mCurKeyCursor.mSelStart + mCurKeyCursor.mSelLength, mDraggingSelectionStart.mSelStart + mDraggingSelectionStart.mSelLength);

					mSelection.mSelStart = leftSel;
					mSelection.mSelLength = rightSel - leftSel;
					
					delete curSel;
					KeyCursorUpdated();
				}
			}
        }

        //public override void MouseDown(float x, float y)
		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
			DeleteAndNullify!(mDraggingSelectionStart);

            if (btn == 0)
            {
                //if (mByteEditWidget != null)
                //{
                  //  OnSelEditLostFocus(mByteEditWidget);
                //}
				DeleteAndNullify!(mSelection);

                var selection = GetSelectionForDisplayPosition(x, y, 1, 1);

				if (selection == null)
				{
					for (var te in mTrackedExprs)
					{
						if ((var rect = te.mDrawRect) && (rect.Contains(x, y)))
						{
							SelectRange(te.mCurValue, 0);
							break;
						}
					}
				}

                if (selection != null)
                {
					delete mCurKeyCursor;
                    mCurKeyCursor = new Cursor(selection);
                    mCurKeyCursorBlinkTicks = 0;
                    if (mCurKeyCursor.mInStrView)
                        mCurKeyCursor.mSubChar = 0;
                    KeyCursorUpdated();

                    SetFocus();
					mDraggingSelectionStart = new Cursor(selection);

					//TODO:
					delete selection;

                    /*
                    mCurEditSelection = selection;

                    EditWidget ew = new DarkEditWidget();
                    mByteEditWidget = ew;
                    (ew.Content as DarkEditWidgetContent).SetFont(IDEApp.sApp.mCodeFont, true, true);
                    using (var lockRange = mContent.LockRange(selection.mSelStart, selection.mSelLength))
                    {
                        ew.SetText(string.Format("{0:X2}", lockRange.mData[0]));
                    }
                    ew.Content.SelectAll();
                    ew.Resize(selection.mBinRect.mX, selection.mBinRect.mY, selection.mBinRect.mWidth, selection.mBinRect.mHeight);
                    AddWidget(ew);

                    ew.mLostFocusHandler += OnSelEditLostFocus;
                    ew.mSubmitHandler += OnSelEditSubmit;
                    ew.mCancelHandler += OnSelEditCancel;
                    ew.SetFocus();
                    */
                }
				else
				{
					SetFocus();
				}
            }
            else if (btn == 1)
            {
                ShowMenu(x, y);
            }
        }

		public override void MouseUp(float x, float y, int32 btn)
		{
			base.MouseUp(x, y, btn);
			DeleteAndNullify!(mDraggingSelectionStart);
		}

        public bool ApplyUndoAction(int offset, uint8 fromVal, uint8 toVal, uint8 subChar, bool inStrView, bool advanceAfterAction)
        {
            bool result = false;

			delete mCurKeyCursor;
            mCurKeyCursor = new Cursor(offset, 1, subChar, inStrView);

			var lockRange = mContent.LockRange(mCurKeyCursor.mSelStart, mCurKeyCursor.mSelLength);
			defer delete lockRange;
            
            {
                if (lockRange.mData[0] == fromVal)
                {
                    lockRange.mData[0] = toVal;
                    lockRange.mModified = true;
                    result = true;
                }
            }

            Selection sel = GetSelectionForCursor(mCurKeyCursor, true);
            if (sel != null)
            {
				defer delete sel;

                mCurKeyCursorBlinkTicks = 0;
				DeleteAndNullify!(mCurHoverSelection);
            }

            if (advanceAfterAction)
            {
                const int32 subCharsPerElement = 2; //CDH add support for multibyte
                ++mCurKeyCursor.mSubChar;
                mCurKeyCursor.mSubChar %= subCharsPerElement;
                if (mCurKeyCursor.mSubChar == 0)
                {
                    AdvanceCursor(mCurKeyCursor);
                }
            }

            KeyCursorUpdated();

            return result;
        }

        void AdvanceCursor(Cursor cursor)
        {
            float lineSpacing = GetLineSpacing();
            int lineCount = Math.Max(1, (int)(mHeight / lineSpacing) - 2);

            int desiredPosition = cursor.mSelStart;

            ++desiredPosition; //CDH TODO generalize to general signed advance amount w/ ensure-visible support

            bool clearTrackedExprs = false;
            while (desiredPosition >= (mCurPosition + mBytesPerDisplayLine*lineCount))
            {
                mCurPosition += mBytesPerDisplayLine;
                clearTrackedExprs = true;
            }
            if (clearTrackedExprs)
                DirtyTrackedExprs();

            cursor.mSelStart = desiredPosition;
        }

        private void KeyCursorUpdated()
        {
			DirtyTrackedExprs();
            if (OnKeyCursorUpdated.HasListeners)
                OnKeyCursorUpdated();
        }

        public override void KeyChar(char32 theChar)
        {
            base.KeyChar(theChar);

            char8 ctrlChar = Utils.CtrlKeyChar(theChar);
            if (ctrlChar == 'Z')
            {
                mUndoManager.Undo();
            }
            else if (ctrlChar == 'Y')
            {
                mUndoManager.Redo();
            }
            else if ((mCurKeyCursor != null) && !theChar.IsControl)
            {
                bool accepted = false;

                if (mCurKeyCursor.mInStrView)
                {
					var lockRange = mContent.LockRange(mCurKeyCursor.mSelStart, mCurKeyCursor.mSelLength);
					defer delete lockRange;
                    
                    uint8 oldValue = lockRange.mData[0];
                    lockRange.mData[0] = (uint8)theChar;//(byte)Convert.ToUInt16(theChar); //CDH TODO add unicode strview support
                    lockRange.mModified = true;
                    accepted = true;
                    mCurKeyCursor.mSubChar = 0;

                    mLastSnapshotDiff.SetChanged(mCurKeyCursor.mSelStart);
                    mUndoManager.Add(new BinaryDataContent.BinaryChangeAction(this, mCurKeyCursor.mSelStart, oldValue, lockRange.mData[0], 0, true), false);
                }
                else
                {
					DeleteAndNullify!(mSelection);

					var lockRange = mContent.LockRange(mCurKeyCursor.mSelStart, mCurKeyCursor.mSelLength);
					defer delete lockRange;
                    
                    char32 c = theChar;
                    int32 t;
                    if ((c >= '0') && (c <= '9'))
                        t = (int32)(c - '0');
                    else if ((c >= 'a') && (c <= 'f'))
                        t = (int32)(c - 'a') + 10;
                    else if ((c >= 'A') && (c <= 'F'))
                        t = (int32)(c - 'A') + 10;
                    else
                        t = -1;

                    if (t >= 0)
                    {
                        const int32 subCharsPerElement = 2; //CDH add support for multibyte
                        int32 shiftBits = ((subCharsPerElement-1) - mCurKeyCursor.mSubChar) * 4;
                        uint8 mask = (uint8)(0xF << shiftBits);
                        uint8 val = lockRange.mData[0];
                        uint8 origVal = val;
                        val &= (uint8)~mask;
                        val |= (uint8)((t << shiftBits) & mask);
                        if (val != origVal)
                        {
                            uint8 oldValue = lockRange.mData[0];
                            lockRange.mData[0] = val;
                            lockRange.mModified = true;
                        
                            mLastSnapshotDiff.SetChanged(mCurKeyCursor.mSelStart);
                            mUndoManager.Add(new BinaryDataContent.BinaryChangeAction(this, mCurKeyCursor.mSelStart, oldValue, val, (uint8)mCurKeyCursor.mSubChar, false), false);
                        }
                        accepted = true;
                    
                        ++mCurKeyCursor.mSubChar;
                        mCurKeyCursor.mSubChar %= subCharsPerElement;
                    }
                }

                if (accepted)
                {
                    if (mCurKeyCursor.mSubChar == 0)
                    {
                        AdvanceCursor(mCurKeyCursor);
                    }
                    
                    mCurKeyCursorBlinkTicks = 0;
                    DeleteAndNullify!(mCurHoverSelection);//GetSelectionForCursor(mCurKeyCursor, false);

                    KeyCursorUpdated();
                }
            }
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);

            bool isCtrl = mWidgetWindow.IsKeyDown(KeyCode.Control);
            const int32 subCharsPerElement = 2; //CDH add support for multibyte

            switch(keyCode)
            {
            case KeyCode.Up: fallthrough;
            case KeyCode.Down: fallthrough;
            case KeyCode.Left: fallthrough;
            case KeyCode.Right: fallthrough;
            case KeyCode.PageUp: fallthrough;
            case KeyCode.PageDown: fallthrough;
            case KeyCode.Home: fallthrough;
            case KeyCode.End:
                {
					DeleteAndNullify!(mSelection);

                    if (mCurKeyCursor == null)
                        mCurKeyCursor = new Cursor(mCurPosition, 1, 0, false);

                    if (mCurKeyCursor != null)
                    {
                        Selection sel = GetSelectionForCursor(mCurKeyCursor, true);
                        if (sel != null)
                        {
							defer delete sel;

                            float lineSpacing = GetLineSpacing();
                            int lineCount = Math.Max(1, (int)(mHeight / lineSpacing) - 2);

                            int desiredPosition = mCurKeyCursor.mSelStart;
                            switch(keyCode)
                            {
                            case KeyCode.Up: desiredPosition -= mBytesPerDisplayLine; break;
                            case KeyCode.Down: desiredPosition += mBytesPerDisplayLine; break;
                            case KeyCode.Left:
                                {
                                    if (sel.mInStrView)
                                    {
                                        desiredPosition -= 1;
                                        mCurKeyCursor.mSubChar = 0;
                                    }
                                    else if (isCtrl)
                                    {
                                        if (mCurKeyCursor.mSubChar == 0)
                                            desiredPosition -= 1;
                                        else
                                            mCurKeyCursor.mSubChar = 0;
                                    }
                                    else
                                    {
                                        --mCurKeyCursor.mSubChar;
                                        if (mCurKeyCursor.mSubChar < 0)
                                        {
                                            desiredPosition -= 1;
                                            mCurKeyCursor.mSubChar = subCharsPerElement - 1;
                                        }
                                    }
                                }
                                break;
                            case KeyCode.Right:
                                {
                                    if (sel.mInStrView)
                                    {
                                        desiredPosition += 1;
                                        mCurKeyCursor.mSubChar = 0;
                                    }
                                    else if (isCtrl)
                                    {
                                        desiredPosition += 1;
                                        mCurKeyCursor.mSubChar = 0;
                                    }
                                    else
                                    {
                                        ++mCurKeyCursor.mSubChar;
                                        if (mCurKeyCursor.mSubChar >= subCharsPerElement)
                                        {
                                            desiredPosition += 1;
                                            mCurKeyCursor.mSubChar = 0;
                                        }
                                    }
                                }
                                break;
                            case KeyCode.PageUp: desiredPosition -= mBytesPerDisplayLine * lineCount; break;
                            case KeyCode.PageDown: desiredPosition += mBytesPerDisplayLine * lineCount; break;
                            case KeyCode.Home:
                                {
                                    desiredPosition -= mCurPosition;
                                    desiredPosition -= desiredPosition % mBytesPerDisplayLine;
                                    desiredPosition += mCurPosition;
                                    if (desiredPosition != mCurKeyCursor.mSelStart)
                                        mCurKeyCursor.mSubChar = 0;
                                        
                                }
                                break;
                            case KeyCode.End:
                                {
                                    desiredPosition -= mCurPosition;
                                    desiredPosition -= desiredPosition % mBytesPerDisplayLine;
                                    desiredPosition += mCurPosition + (mBytesPerDisplayLine - 1);
                                    if (desiredPosition != mCurKeyCursor.mSelStart)
                                        mCurKeyCursor.mSubChar = 0;
                                }
                                break;
							default:
                            }

                            bool clearTrackedExprs = false;
                            while (desiredPosition < mCurPosition)
                            {
                                mCurPosition -= mBytesPerDisplayLine;
                                clearTrackedExprs = true;
                            }
                            while (desiredPosition >= (mCurPosition + mBytesPerDisplayLine*lineCount))
                            {
                                mCurPosition += mBytesPerDisplayLine;
                                clearTrackedExprs = true;
                            }
                            if (clearTrackedExprs)
                                DirtyTrackedExprs();

                            mCurKeyCursor.mSelStart = desiredPosition;
                            mCurKeyCursorBlinkTicks = 0;

                            DeleteAndNullify!(mCurHoverSelection);//GetSelectionForCursor(mCurKeyCursor);

                            KeyCursorUpdated();
                        }
                    }
                }
                break;
            default:
                break;
        	}
        }

        /*
        void OnSelEditLostFocus(Widget widget)
        {
            EditWidget editWidget = (EditWidget)widget;
            if (editWidget == mByteEditWidget)
            {
                mCurEditSelection = null;

                editWidget.mLostFocusHandler -= OnSelEditLostFocus;
                editWidget.mSubmitHandler -= OnSelEditSubmit;
                editWidget.mCancelHandler -= OnSelEditCancel;
                
                editWidget.RemoveSelf();
                mByteEditWidget = null;
                
                if (mWidgetWindow.mFocusWidget == null)
                    SetFocus();
            }
        }
        void OnSelEditSubmit(EditEvent theEvent)
        {
            if (mCurEditSelection != null)
            {
                string text = mByteEditWidget.Text;
                if (text.Length == 2)
                {
                    int val = 0;
                    for (int i=0; i<2; ++i)
                    {
                        char8 c = text[i];
                        int t;
                        if ((c >= '0') && (c <= '9'))
                            t = c - '0';
                        else if ((c >= 'a') && (c <= 'f'))
                            t = (c - 'a') + 10;
                        else if ((c >= 'A') && (c <= 'F'))
                            t = (c - 'A') + 10;
                        else
                        {
                            val = -1;
                            break;
                        }
                    
                        val <<= 4;
                        val += t;
                    }
                    if (val >= 0)
                    {
                        using (var lockRange = mContent.LockRange(mCurEditSelection.mSelStart, mCurEditSelection.mSelLength))
                        {
                            lockRange.mData[0] = (byte)val;
                            lockRange.mModified = true;
                        }
                    }
                }
            }
            OnSelEditLostFocus((EditWidget)theEvent.mSender);
        }
        void OnSelEditCancel(EditEvent theEvent)
        {
            OnSelEditLostFocus((EditWidget)theEvent.mSender);
        }
        */

        public override void MouseWheel(float x, float y, int32 delta)
        {
            base.MouseWheel(x, y, delta);
            if (mInfiniteScrollbar != null)
                mInfiniteScrollbar.MouseWheel(x, y, delta);
        }

        public void ResetPosition(int position)
        {
            if (position == mCurPosition)
                return; // no change

            mCurPosition = Math.Min(position, (int)int64.MaxValue);
            mCurPositionDisplayOffset = 0;
			mShowPositionDisplayOffset = 0;

            ClearTrackedExprs(false);
        }

        public void DirtyTrackedExprs()
        {
            mDelayedClearTrackedExprsTimer = 0.03125f;
        }
        public void ClearTrackedExprs(bool force)
        {
            float lineSpacing = GetLineSpacing();
            int lineCount = (int)(mHeight / lineSpacing) + 3;
            int lockSize = lineCount * mBytesPerDisplayLine;

            mDelayedClearTrackedExprsTimer = 0.0f;

            //if (!force && ((mCurPosition == mTrackedExprRangeStart) && (lockSize == mTrackedExprRangeSize))
            if (!force)
            {
                if (mTrackedExprUpdateCnt == mUpdateCnt)
                    return;
                if ((mCurPosition == mTrackedExprRangeStart) && (lockSize == mTrackedExprRangeSize))
                    return;
            }

            Stopwatch sw = scope Stopwatch();
            sw.Start();
			
			ClearAndDeleteItems(mTrackedExprs);

			if (!gApp.mDebugger.IsPaused())
			{
				TrackedExpressionsUpdated(mTrackedExprs);
				return;
			}

            // slow part 1 (~6ms in mintest)
            String autoExprs = scope String();
			//IDEApp.sApp.mDebugger.GetAutoExpressions((uint64)mCurPosition, (uint64)lockSize, autoExprs);
			IDEApp.sApp.mDebugger.GetAutoExpressions((uint64)0, (uint64)0, autoExprs);

            if (!autoExprs.StartsWith("!"))
            {
                //var hashAlgo = System.Security.Cryptography.MD5.Create();

                //string[] autoSplit = autoExprs.Split('\n');
                for (var autoStr in autoExprs.Split('\n'))
                {
                    if (autoStr.Length == 0)
                        continue;

                    var tabSplit = autoStr.Split('\t');
                    
                    int trackedMemStart = 0;
                    int trackedMemSize = 0;

					String trackedExpr = scope String(tabSplit.GetNext());
					StringView sizeStr = tabSplit.GetNext();
					StringView addrStr = tabSplit.GetNext().GetValueOrDefault();

                    if (int.Parse(sizeStr) case .Ok(out trackedMemSize))
					{
					}
   
                    
                    uint32 displayColor = 0;
                    StringView displayName = scope String();
                    bool isReg = false;
                    if (trackedExpr.StartsWith("$"))
                    {
						displayName = StringView(trackedExpr, 1);
						isReg = true;

                        switch(displayName)
                        {
                        case "esp", "rsp": displayColor = Color.FromHSV(0.0f, 1.0f, 1.0f, 0x80);
                        case "ebp", "rbp": displayColor = Color.FromHSV(0.1f, 1.0f, 1.0f, 0x80);
                        case "ecx", "rcx": displayColor = Color.FromHSV(0.15f, 1.0f, 1.0f, 0x80);
                        case "edx", "rdx": displayColor = Color.FromHSV(0.25f, 1.0f, 1.0f, 0x80);
                        case "esi", "rsi": displayColor = Color.FromHSV(0.45f, 1.0f, 1.0f, 0x80);
                        case "edi", "rdi": displayColor = Color.FromHSV(0.575f, 1.0f, 1.0f, 0x80);
                        case "ebx", "rbx": displayColor = Color.FromHSV(0.8f, 1.0f, 1.0f, 0x80);
                        case "eip", "rip": displayColor = Color.FromHSV(0.85f, 1.0f, 1.0f, 0x80);
                        case "eax", "rax": displayColor = Color.FromHSV(0.9f, 1.0f, 1.0f, 0x80);

						case "r8": displayColor = Color.FromHSV(0.0f, 0.5f, 1.0f, 0x80);
						case "r9": displayColor = Color.FromHSV(0.1f, 0.05f, 1.0f, 0x80);
						case "r10": displayColor = Color.FromHSV(0.15f, 0.5f, 1.0f, 0x80);
						case "r11": displayColor = Color.FromHSV(0.25f, 0.5f, 1.0f, 0x80);
						case "r12": displayColor = Color.FromHSV(0.45f, 0.5f, 1.0f, 0x80);
						case "r13": displayColor = Color.FromHSV(0.575f, 0.5f, 1.0f, 0x80);
						case "r14": displayColor = Color.FromHSV(0.8f, 0.5f, 1.0f, 0x80);
						case "r15": displayColor = Color.FromHSV(0.85f, 0.5f, 1.0f, 0x80);
                        }
                    }
                    else if (trackedExpr.StartsWith("&"))
                    {
                        //displayColor = 0x8000c000;
                        displayName = StringView(trackedExpr, 1);

                        if (int.Parse(addrStr) case .Ok(out trackedMemStart))
						{
						}

						if (displayName == "$StackFrame")
						{
							if (mCurPosition == 0)
							{
								mCurPosition = trackedMemStart - mBytesPerDisplayLine*2;
								if ((mBytesPerDisplayLine & 15) == 0)
									mCurPosition &= ~15;
								else if ((mBytesPerDisplayLine & 7) == 0)
									mCurPosition &= ~7;
							}
						}
                    }
                    else if (trackedExpr.StartsWith("?"))
                    {
                        //displayColor = 0x8000c000;
                        displayName = StringView(trackedExpr, 1);
						trackedExpr.Clear();
						trackedExpr.Append("&");
						trackedExpr.Append(displayName);
                    }

                    if (!displayName.IsEmpty)
                    {
						let digest = MD5.Hash(Span<uint8>((uint8*)displayName.Ptr, displayName.Length));
                        
                        if (displayColor == 0)
                        {
                            float h = (float)digest.mHash[0] / 255.0f;
                            float s = 1.0f;//0.75f + ((float)digest[1] / 1024.0f);
                            float v = 1.0f;//0.5f + ((float)digest[2] / 512.0f);
                            displayColor = Color.FromHSV(h, s, v, 0x80);
                        }

						let te = new TrackedExpression(this, trackedExpr, trackedMemStart, trackedMemSize, displayName, displayColor, isReg);
                        mTrackedExprs.Add(te);
                    }
                }
            }

            // slow part 2 (~3ms in mintest)
            for (var te in mTrackedExprs)
                te.Update(mCurPosition, lockSize);

            sw.Stop();
            
            TrackedExpressionsUpdated(mTrackedExprs);

            double elapsedMs = sw.Elapsed.TotalMilliseconds;
            if ((elapsedMs > 0.0) || (mTrackedExprs.Count > 0))
                Console.WriteLine("STOPWATCH(ClearTrackedExprs {2}): {0}ms (te count = {1}, MemS = ({3} -> {4}), MemL = ({5} -> {6}))", elapsedMs, mTrackedExprs.Count, mUpdateCnt, mTrackedExprRangeStart, mCurPosition, mTrackedExprRangeSize, lockSize);

            mTrackedExprRangeStart = mCurPosition;
            mTrackedExprRangeSize = lockSize;
            mTrackedExprUpdateCnt = mUpdateCnt;
        }

		public void CheckClearData()
		{
			if (mCacheDirty)
				mContent.ClearCache();
			if (mTrackedExprsDirty)
			    ClearTrackedExprs(true);
			mCacheDirty = false;
			mTrackedExprsDirty = false;
		}

        public void ClearData(bool doRefreshTrackedExprs)
        {
            mCacheDirty = true;
			mTrackedExprsDirty |= doRefreshTrackedExprs;
			if (mWidgetWindow != null)
				CheckClearData();
        }

        public void ApplyScrollDelta(double scrollDelta)
        {
            if (scrollDelta == 0.0)
                return; // no change

			MarkDirty();
			double useScrollDelta = scrollDelta;
            int position = (int)mCurPosition;

            //float lineSpacing = GetLineSpacing();
            //ulong lineCount = (ulong)(mHeight / lineSpacing) + 3;

			//Debug.WriteLine("Scroll: {0}", scrollDelta);

            /*if (Math.Abs(useScrollDelta) >= 0.5)//(double)lineCount * 0.25)
            {
                mCurPositionDisplayOffset = 0.0;
                useScrollDelta = Math.Round(useScrollDelta);
            }*/

            mCurPositionDisplayOffset += useScrollDelta;
            if (mCurPositionDisplayOffset >= 1.0)
            {
                int wholeAdj = (int)mCurPositionDisplayOffset;
                mCurPositionDisplayOffset -= wholeAdj;
                position += (int64)(wholeAdj * mBytesPerDisplayLine);
            }
            else if (mCurPositionDisplayOffset < 0.0)
            {
                int64 wholeAdj = (int64)mCurPositionDisplayOffset - 1;
                mCurPositionDisplayOffset -= wholeAdj;
                
                if (mCurPositionDisplayOffset >= 1.0)
                {
                    int64 wholeAdj2 = (int64)mCurPositionDisplayOffset;
                    wholeAdj += wholeAdj2;
                    mCurPositionDisplayOffset -= wholeAdj2;
                }
                
                position = (int64)Math.Max((int64)mCurPosition + (wholeAdj * (int64)mBytesPerDisplayLine), (int64)0);
            }

			// Snap to full lines once we're moving at a fast enough rate
			if (Math.Abs(scrollDelta) < 0.25f)
				mShowPositionDisplayOffset = mCurPositionDisplayOffset;
			else
				mShowPositionDisplayOffset = 0;

            mCurPosition = (int)Math.Min((uint)Math.Max(0, position), (uint)-1);

            DirtyTrackedExprs();
        }

		public void EnsureCursorVisible()
		{
			float lineSpacing = GetLineSpacing();
			int lineCount = Math.Max(1, (int)(mHeight / lineSpacing) - 3);

			int selEnd = mCurKeyCursor.mSelStart + mCurKeyCursor.mSelLength;
			if (mSelection != null)
				selEnd = mSelection.mSelStart + mSelection.mSelLength;

			if (selEnd >= mCurPosition + lineCount * mBytesPerDisplayLine)
			{
				int adjust = ((selEnd - (mCurPosition + lineCount * mBytesPerDisplayLine) + mBytesPerDisplayLine - 1) / mBytesPerDisplayLine + Math.Min(3, lineCount - 1)) * mBytesPerDisplayLine;
				mCurPosition += adjust;
			}

			if (mCurKeyCursor.mSelStart <= mCurPosition)
			{
				int adjust = (mCurPosition - mCurKeyCursor.mSelStart + mBytesPerDisplayLine - 1) / mBytesPerDisplayLine * mBytesPerDisplayLine;
				mCurPosition -= adjust;
				mShowPositionDisplayOffset = 0;
				mCurPositionDisplayOffset = 0;
			}
			
			KeyCursorUpdated();
		}

        public void SelectRange(int memStart, int memLen)
        {
            //CDH TODO make this selection support more "real" once it looks good, add drag select etc, not just a "test" feature
			delete mSelection;
            mSelection = new Cursor(memStart, memLen, 0, false);

			delete mCurKeyCursor;
			mCurKeyCursor = new Cursor(memStart, 1, 0, false);

			EnsureCursorVisible();
			SetFocus();

			var sel = GetSelectionForCursor(mSelection, false);
			LocatorAnim.Show(.Always, this, sel.mBinRect.Left, sel.mBinRect.Top + sel.mBinRect.mHeight / 2);
			delete sel;
        }

		public void DeselectRange()
		{
			DeleteAndNullify!(mSelection);
		}

        public static void SanityTest()
        {
            var provider = new TestBinaryContentDataProvider();
            var content = new BinaryDataContent(64, provider);

            using (var range = content.LockRange(0x12345678, 200))
            {
                Console.WriteLine("SANITY TEST! {0} {1}", range.mBaseOffset, range.mData.Count);
            }
        }
    }
}
