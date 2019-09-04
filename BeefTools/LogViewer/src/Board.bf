using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme.dark;
using System;
using System.IO;
using Beefy.utils;
using System.Threading;
using System.Collections.Generic;
using System.Diagnostics;

namespace LogViewer
{
	class Board : Widget
	{
		public struct Match
		{
			public int32 mFilterIdx;
			public int32 mTextIdx;
		}

		public DarkEditWidget mDocEdit;
		public DarkEditWidget mFilterEdit;
		public String mContent ~ delete _;
		public StatusBar mStatusBar;

		public String mFilter = new String() ~ delete _;
		public List<int32> mFilterLengths = new List<int32>() ~ delete _;
		public int mFilterDirtyCountdown = 1;

		public String mNewContent ~ delete _;
		public List<Match> mNewMatches ~ delete _;
		public bool mRefreshing;

		public uint32[] mColors = new .(
			0xFFFFFFFF,
			0xFFFC5858,
			0xFF00FF00,
			0xFF6E67FF,
			0xFFFF00FF,
			0xFFFFFF00,
			0xFF00FFFF,
			0XFFFFBD67
			) ~ delete _;

		public this()
		{

			mStatusBar = new StatusBar();
			AddWidget(mStatusBar);

			mDocEdit = new DarkEditWidget();
			var ewc = (DarkEditWidgetContent)mDocEdit.mEditWidgetContent;
			ewc.mIsMultiline = true;
			ewc.mFont = gApp.mFont;
			ewc.mWordWrap = false;
			ewc.mTextColors = mColors;
			mDocEdit.InitScrollbars(true, true);
			AddWidget(mDocEdit);

			mFilterEdit = new DarkEditWidget();
			ewc = (DarkEditWidgetContent)mFilterEdit.mEditWidgetContent;
			ewc.mIsMultiline = true;
			ewc.mFont = gApp.mFont;
			ewc.mWordWrap = false;
			ewc.mTextColors = mColors;
			mFilterEdit.InitScrollbars(true, true);
			AddWidget(mFilterEdit);
		}

		int GetColorIdx(int idx)
		{
			return (idx % (mColors.Count - 1)) + 1;
		}

		uint32 GetColor(int idx)
		{
			return mColors[(idx % (mColors.Count - 1)) + 1];
		}

		public void Load(StringView filePath)
		{
			scope AutoBeefPerf("Board.Load");

			delete mContent;
			mContent = new String();

			let result = File.ReadAllText(filePath, mContent, false);
			if (result case .Err)
			{
				gApp.Fail("Failed to open file '{0}'", filePath);
			}
			//mDocEdit.SetText(mContent);
		}

		void Refresh()
		{
			scope AutoBeefPerf("Board.Refresh");

			//let profileId = Profiler.StartSampling(Thread.CurrentThread).GetValueOrDefault();

			delete mNewContent;
			mNewContent = new String();
			delete mNewMatches;
			mNewMatches = new List<Match>();

			let filters = mFilter.Split!('\n');

			for (var line in mContent.Split('\n'))
			{
				bool hadMatch = false;
				bool hadFilter = false;

				for (var filter in filters)
				{
					if (filter.Length == 0)
						continue;
					hadFilter = true;
					int lastIdx = -1;
					while (true)
					{
						int findIdx = line.IndexOf(filter, lastIdx + 1);
						if (findIdx == -1)
							break;

						hadMatch = true;
						lastIdx = findIdx + filter.Length - 1;

						Match match;
						match.mFilterIdx = (.)@filter;
						match.mTextIdx = (.)(mNewContent.Length + findIdx);
						mNewMatches.Add(match);
					}
				}

				if ((hadMatch) || (!hadFilter))
				{
					mNewContent.Append(line);
					mNewContent.Append('\n');
				}
			}

			mRefreshing = false;

			/*if (profileId != 0)
				profileId.Dispose();*/
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			//using (g.PushColor(0xFFE02040))
				//g.FillRect(0, 0, mWidth, mHeight);

			g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Bkg), 0, 0, mWidth, mHeight);
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);

			/*g.SetFont(gApp.mFont);
			g.DrawString(scope String()..AppendF("FPS: {0}", gApp.mLastFPS), 0, 0);*/
		}

		void ResizeComponents()
		{
			float statusBarHeight = 20;

			float findHeight = 140;
			mDocEdit.Resize(0, 0, mWidth, mHeight - findHeight - statusBarHeight);
			mFilterEdit.Resize(0, mHeight - findHeight - statusBarHeight, mWidth, findHeight);

			mStatusBar.Resize(0, mHeight - statusBarHeight, mWidth, statusBarHeight);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void Update()
		{
			base.Update();

			if (mRefreshing)
			{
				// Just wait...
			}
			else
			{
				if (mNewContent != null)
				{
					var ewc = (DarkEditWidgetContent)mFilterEdit.mEditWidgetContent;
					for (int lineIdx < ewc.GetLineCount())
					{
						int colorIdx = GetColorIdx(lineIdx);
						ewc.GetLinePosition(lineIdx, let lineStart, let lineEnd);
						for (int i = lineStart; i < lineEnd; i++)
						{
							ewc.mData.mText[i].mDisplayTypeId = (.)colorIdx;
						}
					}

					ewc = (DarkEditWidgetContent)mDocEdit.mEditWidgetContent;
					mDocEdit.SetText(mNewContent);
					for (let match in mNewMatches)
					{
						int colorIdx = GetColorIdx(match.mFilterIdx);
						int matchLength = mFilterLengths[match.mFilterIdx];

						for (int i = match.mTextIdx; i < match.mTextIdx + matchLength; i++)
						{
							ewc.mData.mText[i].mDisplayTypeId = (.)colorIdx;
						}
					}

					DeleteAndNullify!(mNewContent);
					DeleteAndNullify!(mNewMatches);
				}

				var filter = scope String();
				mFilterEdit.GetText(filter);

				if (filter != mFilter)
				{
					mFilter.Set(filter);
					mFilterDirtyCountdown = 5;
				}

				mFilterLengths.Clear();
				for (let filterLine in mFilter.Split('\n'))
				{
					mFilterLengths.Add((int32)filterLine.Length);
				}

				if ((mFilterDirtyCountdown > 0) && (gApp.mIsUpdateBatchStart))
				{
					if (--mFilterDirtyCountdown == 0)
					{
						ThreadPool.QueueUserWorkItem(new => Refresh);
						mRefreshing = true;
					}
				}
			}
		}
	}
}
