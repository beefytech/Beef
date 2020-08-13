using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy.widgets;
using Beefy.utils;
using Beefy.gfx;
using Beefy.theme.dark;

namespace IDE.ui
{
    public class OutputPanel : TextPanel
    {
		struct QueuedDisplayChange
		{
			public int32 mOfs;
			public int32 mLen;
			public uint8 mDisplayTypeId;
			public Widget mWidget;

			public this(int ofs, int len, uint8 displayType)
			{
				mOfs = (int32)ofs;
				mLen = (int32)len;
				mDisplayTypeId = displayType;
				mWidget = null;
			}

			public this(int ofs, Widget addWidget)
			{
				mOfs = (int32)ofs;
				mLen = 0;
				mDisplayTypeId = 0;
				mWidget = addWidget;
			}
		}

		struct InlineWidgetEntry
		{
			public Widget mWidget;
			public float mOfsX;
			public float mOfsY;
			public int32 mLine;
			public int32 mLineChar;
		}

        public OutputWidget mOutputWidget;
		String mQueuedText = new String() ~ delete _;
		List<QueuedDisplayChange> mQueuedDisplayChanges = new List<QueuedDisplayChange>() ~ delete _;
		List<InlineWidgetEntry> mInlineWidgets = new List<InlineWidgetEntry>() ~ delete _;
		public int32 mHoverWatchLine;

        public override SourceEditWidget EditWidget
        {
            get
            {
                return mOutputWidget;
            }
        }

        public this()
        {
            mOutputWidget = new OutputWidget();            
            mOutputWidget.Content.mIsMultiline = true;
            mOutputWidget.Content.mWordWrap = false;
            mOutputWidget.InitScrollbars(true, true);
            var darkEditContent = (OutputWidgetContent)mOutputWidget.Content;
            darkEditContent.mOnEscape = new () => EscapeHandler();
            darkEditContent.mFont = IDEApp.sApp.mCodeFont;
			//darkEditContent.mFont = DarkTheme.sDarkTheme.mSmallFont;
            darkEditContent.mUnfocusedHiliteColor = (darkEditContent.mHiliteColor & 0x00FFFFFF) | 0x60000000;
            AddWidget(mOutputWidget);
        }

		public this(bool remapToHighestCompileIdx) : this()
		{
			var darkEditContent = (OutputWidgetContent)mOutputWidget.Content;
			darkEditContent.mRemapToHighestCompileIdx = remapToHighestCompileIdx;
		}

        public override void Serialize(StructuredData data)
        {            
            data.Add("Type", "OutputPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

		public override void FocusForKeyboard()
		{
			mOutputWidget.mEditWidgetContent.mSelection = null;
			mOutputWidget.mEditWidgetContent.CursorLineAndColumn = EditWidgetContent.LineAndColumn(mOutputWidget.mEditWidgetContent.GetLineCount() - 1, 0);
			mOutputWidget.mEditWidgetContent.EnsureCursorVisible();
		}

        public override void Clear()
        {
			scope AutoBeefPerf("OutputPanel.Clear");

            mOutputWidget.SetText("");
			for (var widgetEntry in mInlineWidgets)
			{
				widgetEntry.mWidget.RemoveSelf();
				delete widgetEntry.mWidget;
			}
			mInlineWidgets.Clear();
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);

			for (var widgetEntry in mInlineWidgets)
			{
				var widget = widgetEntry.mWidget;
				mOutputWidget.Content.GetTextCoordAtLineChar(widgetEntry.mLine, widgetEntry.mLineChar, var xOfs, var yOfs);
				widget.mX = widgetEntry.mOfsX + xOfs;
				widget.mY = widgetEntry.mOfsY + yOfs;
			}
		}

        public void Write(StringView text)
        {
            mQueuedText.Append(text);
        }

		void UpdateHoverWatch()
		{
			base.Update();

			if (mWidgetWindow == null)
				return;

			if (!CheckAllowHoverWatch())
		        return;

			if (IDEApp.sApp.HasPopupMenus())
				return;

			/*if (mHoverWatchRect.HasValue)
			{
				float x;
				float y;
				RootToSelfTranslate(mWidgetWindow.mMouseX, mWidgetWindow.mMouseY, out x, out y);
				if (mHoverWatchRect.Value.Contains(x, y))
					return;
			} */

			if ((mHoverWatch != null) && (mHoverWatch.mCloseDelay == 0))
			{
				float x;
				float y;
				EditWidget.Content.RootToSelfTranslate(mWidgetWindow.mMouseX, mWidgetWindow.mMouseY, out x, out y);
				int line;
				int column;
				EditWidget.Content.GetLineAndColumnAtCoord(x, y, out line, out column);

				if (line == mHoverWatchLine)
					return;
		        mHoverWatch.Close();
			}
		}

		public override void Update()
		{
			base.Update();

			var editData = mOutputWidget.mEditWidgetContent.mData;

			int lineStartZero = -1;
			if (editData.mLineStarts != null)
			{
				lineStartZero = editData.mLineStarts[0];
				Debug.Assert(lineStartZero <= editData.mTextLength);
			}

			UpdateHoverWatch();
			if (mQueuedText.Length > 0)
			{
				scope AutoBeefPerf("OutputPanel.Update:QueuedText");

				int line;
				int lineChar;
				mOutputWidget.Content.GetLineCharAtIdx(mOutputWidget.Content.mData.mTextLength, out line, out lineChar);
				mOutputWidget.Content.mAutoHorzScroll = false;
				mOutputWidget.Content.mEnsureCursorVisibleOnModify = mOutputWidget.Content.IsLineVisible(line);
				int startLen = editData.mTextLength;
				mOutputWidget.Content.AppendText(mQueuedText);
				mOutputWidget.Content.mAutoHorzScroll = true;
				mOutputWidget.Content.mEnsureCursorVisibleOnModify = true;
				Debug.Assert(editData.mTextLength == startLen + mQueuedText.Length);

				for (var queuedDisplayChange in mQueuedDisplayChanges)
				{
					if (queuedDisplayChange.mWidget != null)
					{
						var widget = queuedDisplayChange.mWidget;
						mOutputWidget.mEditWidgetContent.AddWidget(queuedDisplayChange.mWidget);
						mOutputWidget.Content.GetLineCharAtIdx(startLen + queuedDisplayChange.mOfs, out line, out lineChar);
						mOutputWidget.Content.GetTextCoordAtLineChar(line, lineChar, var xOfs, var yOfs);

						InlineWidgetEntry widgetEntry;
						widgetEntry.mOfsX = widget.mX;
						widgetEntry.mOfsY = widget.mY;
						widgetEntry.mLine = (.)line;
						widgetEntry.mLineChar = (.)lineChar;
						widgetEntry.mWidget = queuedDisplayChange.mWidget;
						mInlineWidgets.Add(widgetEntry);

						widget.mX = widgetEntry.mOfsX + xOfs;
						widget.mY = widgetEntry.mOfsY + yOfs;
					}
					else
					{
						for (int i = startLen + queuedDisplayChange.mOfs; i < startLen + queuedDisplayChange.mOfs + queuedDisplayChange.mLen; i++)
							editData.mText[i].mDisplayTypeId = queuedDisplayChange.mDisplayTypeId;
					}
				}

				mQueuedText.Clear();
				mQueuedDisplayChanges.Clear();
			}
		}

		public void WriteSmart(StringView text)
		{
			for (var line in text.Split('\n'))
			{
				if (@line.Pos != 0)
					Write("\n");

				if ((line.StartsWith("ERROR:")) || (line.StartsWith("ERROR(")))
				{
					if ((gApp.mRunningTestScript) && (!gApp.mFailed))
					{
						gApp.Fail(scope String()..Append(line));
						continue;
					}

					mQueuedDisplayChanges.Add(QueuedDisplayChange(mQueuedText.Length, "ERROR".Length, (.)SourceElementType.BuildError));
				}
				if ((line.StartsWith("WARNING:")) || (line.StartsWith("WARNING(")))
				{
					mQueuedDisplayChanges.Add(QueuedDisplayChange(mQueuedText.Length, "WARNING".Length, (.)SourceElementType.BuildWarning));
				}
				Write(line);
			}
		}

		public void AddInlineWidget(Widget widget)
		{
			mQueuedDisplayChanges.Add(QueuedDisplayChange(mQueuedText.Length, widget));
		}

        public void GotoNextSourceReference()
        {
            var content = (OutputWidgetContent)mOutputWidget.Content;
            int curLine;
            int lineChar;
            content.GetLineCharAtIdx(content.CursorTextPos, out curLine, out lineChar);

            int lineCount = content.GetLineCount();
            for (int lineOfs = 1; lineOfs < lineCount + 1; lineOfs++)
            {
                int lineIdx = (curLine + lineOfs) % lineCount;

                if (content.GotoRefrenceAtLine(lineIdx))
                    break;                
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mOutputWidget.Resize(0, 0, width, height);
        }
    }
}
