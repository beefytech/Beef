using Beefy;
using Beefy.widgets;
using Beefy.geom;
using System;

namespace IDE.ui
{
	class PanelSplitter : Widget
	{
		public Panel mTopPanel;
		public Panel mBottomPanel;
		public float mSplitPct = 0.3f;

		public Point mDownPoint;

		public Action mSplitAction ~ delete _;
		public Action mUnsplitAction ~ delete _;

		public this(Panel topPanel, Panel bottomPanel)
		{
			mTopPanel = topPanel;
			mBottomPanel = bottomPanel;
		}

		public override void MouseEnter()
		{
			base.MouseEnter();			
		}

		public override void MouseLeave()
		{
			base.MouseLeave();
			if (!mMouseDown)
				BFApp.sApp.SetCursor(Cursor.Pointer);
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			mDownPoint = Point(x, y);			
		}

		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);

			BFApp.sApp.SetCursor(Cursor.SizeNS);

			if (mMouseDown)
			{
				float ofsY = mBottomPanel.mY + mHeight;
                if (mTopPanel != null)
                    ofsY = mTopPanel.mY;
				float totalHeight = mBottomPanel.mHeight;
				float curY = mY + (y - mDownPoint.y) + mHeight /*+ mDownPoint.y*/;
				
				mSplitPct = (curY - ofsY - mHeight) / (totalHeight - ofsY - mHeight);

				if ((mSplitPct > 0) && (mTopPanel == null) && (mSplitAction != null))
					mSplitAction();
				if ((mSplitPct <= 0) && (mTopPanel != null) && (mUnsplitAction != null))
					mUnsplitAction();

				mBottomPanel.Resize(mBottomPanel.mX, mBottomPanel.mY, mBottomPanel.mWidth, mBottomPanel.mHeight);								
			}
		}

		public override void Update()
		{
			base.Update();
			
		}

		public override void Draw(Beefy.gfx.Graphics g)
		{
			base.Draw(g);

			/*using (g.PushColor(0xFFFF0000))
				g.FillRect(0, 0, mWidth, mHeight);*/
		}
	}
}
