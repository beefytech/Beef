using Beefy.widgets;
using Beefy.gfx;

namespace LogViewer
{
	class StatusBar : Widget
	{
		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.SetFont(gApp.mFont);

			uint32 bkgColor = 0xFF404040;
			using (g.PushColor(bkgColor))
				g.FillRect(0, 0, mWidth, mHeight);

			var lineAndColumn = gApp.mBoard.mDocEdit.mEditWidgetContent.CursorLineAndColumn;
			g.DrawString(StackStringFormat!("Ln {0}", lineAndColumn.mLine + 1), mWidth - 160, 2);
			g.DrawString(StackStringFormat!("Col {0}", lineAndColumn.mColumn + 1), mWidth - 80, 2);

			if ((gApp.mBoard.mFilterDirtyCountdown != 0) || (gApp.mBoard.mRefreshing))
				g.DrawString("Refreshing", 8, 2);
		}
	}
}
