using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using IDE.Debugger;
using System.Diagnostics;
using Beefy.theme;

namespace IDE.ui
{
	public class EmbeddedEditWidget : ExpressionEditWidget
	{
		public override EditSelection GetCurExprRange()
		{
			int cursorPos = mEditWidgetContent.CursorTextPos;
			
			let data = mEditWidgetContent.mData;
			int exprStart = -1;

			for (int pos < data.mTextLength)
			{
				char8 c = data.mText[pos].mChar;
				if (c == '{') 
				{
					if (pos < data.mTextLength - 1)
					{
						char8 nextC = data.mText[pos + 1].mChar;
						if (nextC != '{')
						{
							exprStart = pos + 1;
						}
					}
				}
				else if (c == '}')
				{
					if (exprStart != -1)
					{
						if ((cursorPos >= exprStart) && (cursorPos <= pos))
						{  
							return .(exprStart, pos);
						}

						exprStart = -1;
					}
				}
			}

			return .();
		}
	}

	public class ClearCountButton : Widget
	{
		public override void Draw(Graphics g)
		{
			base.Draw(g);

			if ((mMouseOver) || (mMouseDown))
				g.Draw(DarkTheme.sDarkTheme.GetImage(.CloseOver));
			else
				g.Draw(DarkTheme.sDarkTheme.GetImage(.Close));
		}
	}

    public class ConditionDialog : IDEDialog
    {
        ExpressionEditWidget mConditionEdit;
		DarkEditWidget mThreadEdit;
		EmbeddedEditWidget mLogEdit;
		DarkCheckBox mBreakAfterLoggingCheckbox;
		DarkEditWidget mHitCountEdit;
		DarkComboBox mHitCountCombo;
		ClearCountButton mClearCountButton;
		List<String> mHitCountKinds = new List<String>() ~ delete _;
		int32 mStartingHitCount;
		int32 mExtraField;
		Breakpoint mBreakpoint;

        public this()
        {
			mHitCountKinds.Add("Always Break");
			mHitCountKinds.Add("When equals");
			mHitCountKinds.Add("When at least");
			mHitCountKinds.Add("When multiple of");
			mWindowFlags &= ~.Modal;
        }

		public ~this()
		{
			mBreakpoint?.Deref();
		}

        public override void CalcSize()
        {
            mWidth = GS!(360);
			mHeight = GS!(270);
        }

        public bool Finish()
        {
			/*if ((mConditionEdit.mLastError != null) && (mConditionEdit.mLastError != "sideeffects"))
			{
				var err = scope String();
				err.AppendF("Error in Condition:\n{0}", mConditionEdit.mLastError);
				gApp.Fail(err);
				return false;
			}

			if (mLogEdit.mLastError != null)
			{
				var err = scope String();
				err.AppendF("Error in Log String:\n{0}", mConditionEdit.mLastError);
				gApp.Fail(err);
				return false;
			}*/

            var sourceEditWidgetContent = (SourceEditWidgetContent)mConditionEdit.Content;
            if (sourceEditWidgetContent.mAutoComplete != null)            
                sourceEditWidgetContent.KeyChar('\t');                            

            String conditionStr = scope String();
            mConditionEdit.GetText(conditionStr);
            conditionStr.Trim();

			int threadId = -1;
			String threadStr = scope String();
			mThreadEdit.GetText(threadStr);
			threadStr.Trim();
			if (!threadStr.IsWhiteSpace)
			{
				if (threadStr == ".")
				{
					threadId = gApp.mDebugger.GetActiveThread();
				}
				else
				{
					switch (Int.Parse(threadStr))
					{
					case .Ok(out threadId):
					case .Err:
						gApp.Fail("Invalid value for Thread Id");
						return false;
					}
				}
			}

			String loggingStr = scope String();
			mLogEdit.GetText(loggingStr);
			loggingStr.Trim();

			String hitCountTargetStr = scope String();
			mHitCountEdit.GetText(hitCountTargetStr);
			hitCountTargetStr.Trim();
			int32 targetHitCount = 0;

			Breakpoint.HitCountBreakKind hitCountBreakKind = (.)mHitCountKinds.IndexOf(scope String(mHitCountCombo.Label));

			if (!hitCountTargetStr.IsWhiteSpace)
			{
				if (hitCountTargetStr == ".")
				{
					targetHitCount = mStartingHitCount;
				}
				else
				{
					switch (Int32.Parse(hitCountTargetStr))
					{
					case .Ok(out targetHitCount):
					case .Err:
						gApp.Fail("Invalid value for Target Hit Count");
						return false;
					}
				}
			}

			bool breakOnLogging = mBreakAfterLoggingCheckbox.Checked;
			gApp.mBreakpointPanel.ConfigureBreakpoint(mBreakpoint, conditionStr, threadId, loggingStr, breakOnLogging, targetHitCount, hitCountBreakKind);

            return true;
        }

		void SetupExprEvaluator(ExpressionEditWidget exprEditWidget)
		{
			if (mBreakpoint.mAddrType != null)
			{
				var exprPre = new String();
				var exprPost = new String();
				exprEditWidget.mExprPre = exprPre;
				exprEditWidget.mExprPost = exprPost;

				var enumerator = mBreakpoint.mAddrType.Split('\t');
				int langVal = int.Parse(enumerator.GetNext().Get()).Get();
				StringView addrVal = enumerator.GetNext().Get();
				if (langVal == (.)DebugManager.Language.C)
					exprPre.Append("@C:");
				else
					exprPre.Append("@Beef:");
				exprPost.AppendF(",_=*({0}*)0x", addrVal);
				mBreakpoint.mMemoryAddress.ToString(exprPost, "X", null);
				exprPost.Append("L");
			}
		}

        public void Init(Breakpoint breakpoint)
        {
			bool ignoreErrors = !gApp.mDebugger.IsPaused();

			mBreakpoint = breakpoint;
			mBreakpoint.AddRef();

			var title = scope String();
			title.Append("Configure Breakpoint - ");
			breakpoint.ToString_Location(title);
			Title = title;

            mDefaultButton = AddButton("OK", new (evt) => { if (!Finish()) evt.mCloseDialog = false; });
            mEscButton = AddButton("Cancel", new (evt) => Close());
            
			mStartingHitCount = (.)breakpoint.GetHitCount();

            mConditionEdit = new ExpressionEditWidget();
			SetupExprEvaluator(mConditionEdit);
			mConditionEdit.mIgnoreErrors = ignoreErrors;
			// Just try at first address
			void* nextAddr = null;
			mConditionEdit.mEvalAtAddress = breakpoint.GetAddress(ref nextAddr);
			if (breakpoint.mCondition != null)
			{
				mConditionEdit.SetText(breakpoint.mCondition);
				mConditionEdit.Content.SelectAll();
			}
            AddEdit(mConditionEdit);            

			mThreadEdit = new DarkEditWidget();
			((DarkEditWidgetContent)mThreadEdit.mEditWidgetContent).mScrollToStartOnLostFocus = true;
			if (breakpoint.mThreadId != -1)
			{
				var str = scope String();
				breakpoint.mThreadId.ToString(str);
				mThreadEdit.SetText(str);
				mThreadEdit.Content.SelectAll();
			}	
			AddEdit(mThreadEdit);

			mHitCountCombo = new DarkComboBox();
			mHitCountCombo.Label = mHitCountKinds[(int)breakpoint.mHitCountBreakKind];
			mHitCountCombo.mPopulateMenuAction.Add(new (dlg) =>
				{
					for (let str in mHitCountKinds)
					{
						var item = dlg.AddItem(str);
						item.mOnMenuItemSelected.Add(new (item) =>
							{
								// If there is no number etered yet, default to
								var hitCountStr = scope String();
								mHitCountEdit.GetText(hitCountStr);
								if ((hitCountStr.IsWhiteSpace) && (item.mLabel != mHitCountKinds[0]))
								{
									hitCountStr.Clear();
									breakpoint.GetHitCount().ToString(hitCountStr);
									mHitCountEdit.SetText(hitCountStr);
								}

								mHitCountCombo.Label = item.mLabel;
								MarkDirty();
							});
					}
				});
			AddDialogComponent(mHitCountCombo);

			mHitCountEdit = new DarkEditWidget();
			((DarkEditWidgetContent)mHitCountEdit.mEditWidgetContent).mScrollToStartOnLostFocus = true;
			if (breakpoint.mHitCountTarget != 0)
			{
				var str = scope String();
				breakpoint.mHitCountTarget.ToString(str);
				mHitCountEdit.SetText(str);
				mHitCountEdit.Content.SelectAll();
			}
			AddEdit(mHitCountEdit);

			mClearCountButton = new ClearCountButton();
			mClearCountButton.mOnMouseDown.Add(new (widget) =>
				{
					gApp.mBreakpointPanel.ClearHitCounts();
					mStartingHitCount = 0;
				});
			AddWidget(mClearCountButton);

			mLogEdit = new EmbeddedEditWidget();
			mLogEdit.mIgnoreErrors = ignoreErrors;
			SetupExprEvaluator(mLogEdit);
			AddEdit(mLogEdit);
			if (breakpoint.mLogging != null)
			{
				mLogEdit.SetText(breakpoint.mLogging);
				mLogEdit.Content.SelectAll();
			}

			mBreakAfterLoggingCheckbox = new DarkCheckBox();
			mBreakAfterLoggingCheckbox.Label = "Break after logging";
			mBreakAfterLoggingCheckbox.Checked = breakpoint.mBreakAfterLogging;
			AddDialogComponent(mBreakAfterLoggingCheckbox);
        }

        public override void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
        {
            base.PopupWindow(parentWindow, offsetX, offsetY);            
            mConditionEdit.SetFocus();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

			float editWidth = GS!(100);
			
            float curY = mHeight - GS!(20) - mButtonBottomMargin;
			mLogEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));
			mBreakAfterLoggingCheckbox.Resize(mWidth - mBreakAfterLoggingCheckbox.CalcWidth() - GS!(16), curY - GS!(36) - GS!(20), mBreakAfterLoggingCheckbox.CalcWidth() + GS!(4), GS!(28));

			curY -= GS!(56);
			mHitCountEdit.Resize(mWidth - editWidth - GS!(16), curY - GS!(36), editWidth, GS!(28));
			mHitCountCombo.Resize(GS!(16), curY - GS!(36) + GS!(2), mWidth - GS!(16)*2 - editWidth - GS!(4), GS!(28));
			mClearCountButton.Resize(mWidth - GS!(28), curY - GS!(36) - GS!(19), GS!(20), GS!(20));

			curY -= GS!(56);
			mThreadEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));

			curY -= GS!(56);
			mConditionEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));

			delete mClearCountButton.mMouseInsets;
			mClearCountButton.mMouseInsets = new .(GS!(4), GS!(4), GS!(4), GS!(4));
        }

        public override bool HandleTab(int dir)
        {
            var sourceEditWidgetContent = (SourceEditWidgetContent)mConditionEdit.Content;
            if ((sourceEditWidgetContent.mAutoComplete != null) && (sourceEditWidgetContent.mAutoComplete.IsShowing()))
            {
                //sourceEditWidgetContent.KeyChar('\t');
                return false;
            }

            return base.HandleTab(dir);
        }

		public override void Update()
		{
			base.Update();
			int hitCount = mBreakpoint.GetHitCount();
			if (hitCount != mStartingHitCount)
			{
				mStartingHitCount = (.)hitCount;
				MarkDirty();
			}

			if (mBreakpoint.mIsDead)
			{
				// This breakpoint was deleted
				Close();
			}
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

					using (g.PushColor(ThemeColors.Theme.Text.Color)) {
            g.DrawString("Breakpoint Condition", mConditionEdit.mX, mConditionEdit.mY - GS!(20));
			g.DrawString("Thread Id", mThreadEdit.mX, mThreadEdit.mY - GS!(20));
            g.DrawString("Log String", mLogEdit.mX, mLogEdit.mY - GS!(20));
			g.DrawString("Break on Hit Count", mHitCountCombo.mX, mHitCountEdit.mY - GS!(19));

			var str = scope String();
			str.AppendF("Current: {0}", mStartingHitCount);
			g.DrawString(str, mWidth - GS!(16) - GS!(8), mHitCountEdit.mY - GS!(19), .Right);
					}
        }
    }
}
