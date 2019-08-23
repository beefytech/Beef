using Beefy.theme.dark;
using Beefy.widgets;
using IDE.util;
using System;
using Beefy.gfx;

namespace IDE.ui
{
	class IDEDialog : DarkDialog
	{
		public DarkButton mPrevButton;
		public DarkButton mNextButton;
		public SettingHistoryManager mSettingHistoryManager;

		public void CreatePrevNextButtons()
		{
			mPrevButton = new DarkButton();
			mPrevButton.Label = "< &Prev";
			AddDialogComponent(mPrevButton);
			mPrevButton.mOnMouseClick.Add(new (btn) =>
				{
					var propBag = mSettingHistoryManager.GetPrev();
					if (propBag != null)
						UseProps(propBag);
					UpdateHistory();
				});

			/*mPrevButton = AddButton("< &Prev", new (btn) =>
				{

				});*/
			mNextButton = new DarkButton();
			mNextButton.Label = "&Next >";
			AddDialogComponent(mNextButton);
			mNextButton.mOnMouseClick.Add(new (btn) =>
				{
					var propBag = mSettingHistoryManager.GetNext();
					if (propBag != null)
						UseProps(propBag);
					UpdateHistory();
				});

			/*mMatchCaseCheckbox = new DarkCheckBox();
			mMatchCaseCheckbox.Label = "Match &case";
			AddDialogComponent(mMatchCaseCheckbox);*/
			UpdateHistory();
		}

		public override void AddedToParent()
		{
			base.AddedToParent();
			RehupMinSize();
		}

		protected virtual void RehupMinSize()
		{
			if (mWidgetWindow.mWindowFlags.HasFlag(.Resizable))
				mWidgetWindow.SetMinimumSize(GS!(240), GS!(180), true);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			RehupMinSize();
		}

		protected virtual void UseProps(PropertyBag propBag)
		{

		}

		public override void Close()
		{
			base.Close();
			if (mSettingHistoryManager != null)
				mSettingHistoryManager.ToEnd();
		}

		protected void UpdateHistory()
		{
			mPrevButton.mDisabled = !mSettingHistoryManager.HasPrev();
			mNextButton.mDisabled = !mSettingHistoryManager.HasNext();
		}	

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			if (mPrevButton != null)
			{
				mPrevButton.Resize(mWidth - GS!(64)*2 - GS!(6) - GS!(4), 6, GS!(64), GS!(22));
				mNextButton.Resize(mWidth - GS!(64) - GS!(6), 6, GS!(64), GS!(22));
			}
		}

		public override void Escape()
		{
			if ((let editWidget = mWidgetWindow.mFocusWidget as EditWidget) && (let ewc = editWidget.mEditWidgetContent as SourceEditWidgetContent))
			{
				if ((ewc.mAutoComplete != null) && (ewc.mAutoComplete.IsShowing()))
				{
					ewc.mAutoComplete.Close();
					return;
				}
			}

			base.Escape();
		}

		public override bool HandleTab(int dir)
		{
			if ((let editWidget = mWidgetWindow.mFocusWidget as EditWidget) && (let ewc = editWidget.mEditWidgetContent as SourceEditWidgetContent))
		    {
				if ((ewc.mAutoComplete != null) && (ewc.mAutoComplete.IsShowing()))
				{
				    //ewc.KeyChar('\t');
				    return false;
				}
			}

		    return base.HandleTab(dir);
		}

		public override void Submit()
		{
			if ((mDefaultButton != null) && (mDefaultButton.mDisabled))
			{
				IDEApp.Beep(.Error);
				return;
			}

			base.Submit();
		}

		public void DrawLabel(Graphics g, Widget widget, StringView label)
		{
			g.DrawString(label, widget.mX, widget.mY - GS!(18));
		}
	}
}
