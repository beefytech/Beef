using Beefy.theme.dark;
using System;
using Beefy.events;

namespace IDE.ui
{
	class QuickWatchDialog : IDEDialog
	{
		WatchPanel mWatchPanel;

		public void Init(StringView initStr)
		{				 
			mWatchPanel = new WatchPanel(false);
			AddWidget(mWatchPanel);

			Title = "Quick Watch";
			mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable;

			mWatchPanel.[Friend]mDisabled = gApp.mWatchPanel.[Friend]mDisabled;
			mWatchPanel.Update();
			mWatchPanel.mOnKeyDown.Add(new => OnKeyDown);

			if (!initStr.IsEmpty)
			{
				mWatchPanel.AddWatchItem(scope String(initStr));
			}
		}

		public override void AddedToParent()
		{
			base.AddedToParent();

			mWatchPanel.SetFocus();
			var lvItem = (WatchListViewItem)mWatchPanel.mListView.GetRoot().GetChildAtIndex(0);
			if ((lvItem.mWatchEntry != null) && (!lvItem.mWatchEntry.mEvalStr.IsEmpty))
			{
				mWatchPanel.mListView.GetRoot().GetChildAtIndex(0).Focused = true;
			}
			else
			{
				if (!mWatchPanel.[Friend]mDisabled)
					mWatchPanel.EditListViewItem(lvItem);
			}
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();
			mWatchPanel.Resize(mX + GS!(4), mY + GS!(4), mWidth - GS!(4)*2, mHeight - GS!(4)*2);
		}

		public override void CalcSize()
		{
			mWidth = GS!(600);
			mHeight = GS!(380);
		}

		void OnKeyDown(KeyDownEvent evt)
		{
			if (evt.mKeyCode == .Escape)
			{
				evt.mHandled = true;
				Close();
			}
		}

		public override void Update()
		{
			base.Update();
		}
	}
}
