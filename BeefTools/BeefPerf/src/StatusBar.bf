using Beefy.widgets;
using Beefy.theme.dark;
using System;

namespace BeefPerf
{
	class StatusBar : Widget
	{
		public double mShowTime;
		public double mSelTime;
		public int64 mSelTick;

		public override void Draw(Beefy.gfx.Graphics g)
		{
			base.Draw(g);

			uint32 bkgColor = 0xFF404040;
			using (g.PushColor(bkgColor))
				g.FillRect(0, 0, mWidth, mHeight);

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			g.DrawString(scope String()..AppendF("FPS: {0}", gApp.mLastFPS), 4, 0);

			//
			g.DrawString(scope String()..AppendF("{0}", gApp.mUpdateCnt), 0, 0, .Right, mWidth - 8);

			float curY = 64;
			if (!gApp.mListenSocket.IsConnected)
			{
				using (g.PushColor(0xFFFF4040))
					g.DrawString(scope String()..AppendF("Failed to listen on port {0}", gApp.mListenPort), 0, 0, .Right, mWidth - 8);
			}
			else
			{
				g.DrawString(scope String()..AppendF("Clients: {0}", gApp.mClients.Count), curY, 0);
				curY += 64;

				float kPerSec = gApp.mStatBytesPerSec / 1024.0f;
				if ((kPerSec > 0) && (kPerSec < 0.1f))
					kPerSec = 0.1f;
				g.DrawString(scope String()..AppendF("BPS: {0:0.0}k", kPerSec), curY, 0);
				curY += 80;
			}

			if (gApp.mBoard.mPerfView != null)
			{
				var client = gApp.mBoard.mPerfView.mSession;
				g.DrawString(scope String()..AppendF("Zones: {0}", (int32)client.mNumZones), curY, 0);
				curY += 118;

				var str = scope String();
				BpClient.TimeToStr(client.TicksToUS(client.mCurTick - client.mFirstTick), str);
				g.DrawString(str, curY, 0);
				curY += 108;
			}

			if (mShowTime != 0)
			{
				var str = scope String();
				BpClient.TimeToStr(mShowTime, str);
				g.DrawString(str, curY, 0);
				curY += 108;
			}

			if (mSelTime != 0)
			{
				var str = scope String();
				BpClient.ElapsedTimeToStr(mSelTime, str);
				g.DrawString(str, curY, 0);
				curY += 108;
			}

			/*{
				var str = scope String();
				str.FormatInto("{0}", mSelTick);
				g.DrawString(str, 550, 0);
			}*/
		}
	}
}
