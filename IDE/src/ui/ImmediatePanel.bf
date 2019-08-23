using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using Beefy.utils;
using Beefy.theme.dark;
using System.Diagnostics;

namespace IDE.ui
{
    public class ImmediatePanel : TextPanel
    {
        ImmediateWidget mImmediateWidget;
		bool mHasResult;
		String mQueuedText = new String() ~ delete _;

        public override SourceEditWidget EditWidget
        {
            get
            {
                return mImmediateWidget;
            }
        }

        public this()
        {
            mImmediateWidget = new ImmediateWidget();
            mImmediateWidget.mPanel = this;                        
            mImmediateWidget.InitScrollbars(true, true);
            var darkEditContent = (ImmediateWidgetContent)mImmediateWidget.Content;
            darkEditContent.mOnEscape = new () => EscapeHandler();
            darkEditContent.mFont = IDEApp.sApp.mCodeFont;
			//darkEditContent.mFont = DarkTheme.sDarkTheme.mSmallFont;
            AddWidget(mImmediateWidget);

            mImmediateWidget.StartEntry();
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "ImmediatePanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

        public override void Clear()
        {
			if ((mImmediateWidget.mInfoButton != null) && (mImmediateWidget.mInfoButton.mParent != null))
			{
				mImmediateWidget.mInfoButton.RemoveSelf();
			}
			mImmediateWidget.SetText("");
			mImmediateWidget.StartEntry();

			var editWidgetContent = (ImmediateWidgetContent)mImmediateWidget.mEditWidgetContent;
			if (editWidgetContent.mAutoComplete != null)
				editWidgetContent.mAutoComplete.Close();
        }

        public void Write(String text)
        {
            //mImmediateWidget.Content.AppendText(text);
			mQueuedText.Append(text);
        }

		public void WriteResult(String text)
		{
		    //mImmediateWidget.Content.AppendText(text);
			//mImmediateWidget.Content.CursorToEnd();
			mHasResult = true;
			Write(text);
		}

        public void GotoNextError()
        {
            var content = (ImmediateWidgetContent)mImmediateWidget.Content;
            int curLine;
            int lineChar;
            content.GetLineCharAtIdx(content.CursorTextPos, out curLine, out lineChar);

            int32 lineCount = content.GetLineCount();
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
            mImmediateWidget.Resize(0, 0, width, height);
        }

        void UpdateMouseover()
        {
            if (!CheckAllowHoverWatch())
                return;

            if ((mImmediateWidget.mInfoButton != null) && (mImmediateWidget.mInfoButton.IsOnLine()))
                return;

            if (IDEApp.sApp.HasPopupMenus())
                return;

            if ((mHoverWatch != null) && (mHoverWatch.mCloseDelay == 0))
                mHoverWatch.Close();
        }

        public override bool EscapeHandler()
        {
            if (base.EscapeHandler())
                return true;

            mImmediateWidget.ClearEntry();
            return true;
        }

        public override void SetFocus()
        {
            base.SetFocus();
            var content = (ImmediateWidgetContent)mImmediateWidget.Content;
            content.MoveCursorToIdx(content.mData.mTextLength);
        }

        public override void Update()
        {
            base.Update();

            UpdateMouseover();
			if (mQueuedText.Length > 0)
			{
				scope AutoBeefPerf("OutputPanel.Update:QueuedText");

				var editData = mImmediateWidget.mEditWidgetContent.mData;

				int line;
				int lineChar;
				mImmediateWidget.Content.GetLineCharAtIdx(mImmediateWidget.Content.mData.mTextLength, out line, out lineChar);
				mImmediateWidget.Content.mAutoHorzScroll = false;
				int32 startLen = editData.mTextLength;
				mImmediateWidget.Content.AppendText(mQueuedText);
				mImmediateWidget.Content.mAutoHorzScroll = true;
				Debug.Assert(editData.mTextLength == startLen + mQueuedText.Length);
				mQueuedText.Clear();
			}

			/*if (mHasResult)
			{
				mHasResult = false;
				mImmediateWidget.Content.CursorToEnd();
			}*/
        }

		public override bool HasAffinity(Widget otherPanel)
		{
			return base.HasAffinity(otherPanel) || (otherPanel is OutputWidget);
		}

		public void GetQuickExpression(String expr)
		{
			if (mImmediateWidget.GetCmdText(expr))
				return;

			if (!mImmediateWidget.mHistoryList.IsEmpty)
				expr.Append(mImmediateWidget.mHistoryList.Back);
		}
    }
}
