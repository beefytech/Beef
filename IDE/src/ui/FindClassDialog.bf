using Beefy.theme.dark;
using Beefy.widgets;
using System;

namespace IDE.ui
{
	class FindClassDialog : IDEDialog
	{
		ClassViewPanel mClassViewPanel;

		public this()
		{
			mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable;

			AddOkCancelButtons(new (evt) => { GotoClass(); }, null, 0, 1);

			Title = "Find Class";

			mButtonBottomMargin = GS!(6);
			mButtonRightMargin = GS!(6);

			mClassViewPanel = new ClassViewPanel(this);
			AddWidget(mClassViewPanel);
		}

		public override void CalcSize()
		{
		    mWidth = GS!(660);
		    mHeight = GS!(512);
		}

		void GotoClass()
		{
			mClassViewPanel.[Friend]mSearchEdit.mOnSubmit(null);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			mClassViewPanel.Resize(0, 0, width, height - GS!(34));
		}

		public override void PopupWindow(WidgetWindow parentWindow, float offsetX, float offsetY)
		{
			base.PopupWindow(parentWindow, offsetX, offsetY);
			mClassViewPanel.[Friend]mSearchEdit.SetFocus();
		}
	}
}
