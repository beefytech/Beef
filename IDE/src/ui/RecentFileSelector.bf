using System;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;
using System.IO;

namespace IDE.ui
{
	class RecentFileSelector
	{
		class RecentMenuWidget : DarkMenuWidget
		{
			public this(Menu menu) : base(menu)
			{

			}

			public override void Update()
			{
				if ((!mWidgetWindow.IsKeyDown(.Control)) &&
					(!mWidgetWindow.IsKeyDown(.Shift)) &&
					(!mWidgetWindow.IsKeyDown(.Menu)))
				{
					SubmitSelection();
					Close();
				}
			}
		}

		RecentMenuWidget mMenuWidget;

		public void Show()
		{
			if (gApp.mRecentlyDisplayedFiles.IsEmpty)
			{
				Closed();
				return;
			}

			bool hadActiveDocument = gApp.GetActiveDocumentPanel() != null;

			Widget parentWidget = gApp.GetActiveDocumentPanel();
			if (parentWidget != null)
			{
				if (parentWidget.mParent is TabbedView)
					parentWidget = parentWidget.mParent;
			}
			if (parentWidget == null)
				parentWidget = gApp.mMainWindow.mRootWidget;

			Menu menu = new Menu();
			Menu item;

			for (var recentItem in gApp.mRecentlyDisplayedFiles)
			{
				String fileName = scope .();
				Path.GetFileName(recentItem, fileName);
				item = menu.AddItem(fileName);
				item.mOnMenuItemSelected.Add(new (menu) =>
					{
						gApp.[Friend]ShowRecentFile(@recentItem.Index);
						mMenuWidget.Close();
					});
			}

			mMenuWidget = new RecentMenuWidget(menu);
			mMenuWidget.Init(parentWidget, parentWidget.mWidth / 2, GS!(20), .CenterHorz);
			mMenuWidget.mWidgetWindow.mOnWindowKeyDown.Add(new => gApp.[Friend]SysKeyDown);
			mMenuWidget.mOnKeyDown.Add(new (keyboardEvent) =>
				{
					if (keyboardEvent.mKeyCode == .Right)
					{
						if (mMenuWidget.mSelectIdx != -1)
							gApp.[Friend]ShowRecentFile(mMenuWidget.mSelectIdx, false);
					}
				});
			mMenuWidget.mOnRemovedFromParent.Add(new (widget, prevParent, widgetWindow) => Closed());

			if (!hadActiveDocument)
				mMenuWidget.SetSelection(0);
			else
				mMenuWidget.SetSelection(Math.Min(1, menu.mItems.Count - 1));
		}

		public void Next()
		{
			mMenuWidget.KeyDown(.Down, false);
		}

		public void Prev()
		{
			mMenuWidget.KeyDown(.Up, false);
		}

		void Closed()
		{
			gApp.mRecentFileSelector = null;
			delete this;
		}
	}
}
