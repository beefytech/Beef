using System;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.geom;
using System.Collections;
using Beefy.utils;
using System.Diagnostics;
using Beefy.theme.dark;

namespace BeefPerf
{
	struct BPEntry
	{
		public int32 mZoneNameId;
		public int32 mParamsReadPos;
		public int64 mStartTick;
	}

	struct BPSelection
	{
		public int32 mThreadIdx;
		public int64 mTickStart;
		public int64 mTickEnd;
		public int32 mDepth;
	}
	
	class Board : Widget
	{
		public PerfView mPerfView;
		
		public this()
		{
			
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Bkg), 0, 0, mWidth, mHeight);
		}

		void GetData()
		{
		}
		
		public void ShowSession(BpSession session)
		{
			if (mPerfView != null)
			{
				mPerfView.RemoveSelf();
				DeleteAndNullify!(mPerfView);
			}

			if (session != null)
			{
				mPerfView = new PerfView(this, session);
				mPerfView.mSession = session;
				AddWidget(mPerfView);

				ResizeComponents();

				if (mWidgetWindow != null)
					mPerfView.SetFocus();
			}
		}
		
		void ResizeComponents()
		{
			if (mPerfView != null)
				mPerfView.Resize(0, 0, mWidth, mHeight);
		}
	}
}

