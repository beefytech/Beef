using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;

namespace IDE.ui
{
    public class MainFrame : Widget
    {
        public StatusBar mStatusBar;
        public DarkDockingFrame mDockingFrame;

        public this()
        {
            mStatusBar = new StatusBar();
            AddWidget(mStatusBar);
            mDockingFrame = (DarkDockingFrame)ThemeFactory.mDefault.CreateDockingFrame();
            AddWidget(mDockingFrame);
        }

		public void Reset()
		{
			mDockingFrame.RemoveSelf();
			gApp.DeferDelete(mDockingFrame);
			mDockingFrame = (DarkDockingFrame)ThemeFactory.mDefault.CreateDockingFrame();
            AddWidget(mDockingFrame);

			RehupSize();
		}

		public ~this()
		{
		}

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            int32 statusHeight = GS!(20);
            mDockingFrame.Resize(0, 0, width, height - statusHeight);
            mStatusBar.Resize(0, mHeight - statusHeight, width, statusHeight);
        }
    }
}
