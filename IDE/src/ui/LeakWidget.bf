using Beefy.widgets;
using Beefy.gfx;
using Beefy.geom;
using Beefy.theme.dark;
using System;
using System.Collections;

namespace IDE.ui
{
	class LeakWidget : Widget
	{
		public String mExpr ~ delete _;
		public List<String> mStackAddrs = new List<String>() ~ DeleteContainerAndItems!(_);
		public OutputPanel mOutputPanel ~
			{
				if (mCreatedTextPanel)
					delete _;
			};
		bool mCreatedTextPanel;

		public this(OutputPanel panel = null)
		{
			mOutputPanel = panel;
		}

		public override void Draw(Graphics g)
		{
			g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MoreInfo));
		}

		public override void MouseEnter()
		{
		    base.MouseEnter();
		    
			/*if (mOutputPanel == null)
			{
				var emptyTextPanel = new EmptyTextPanel();
				emptyTextPanel.mHoverWatchRect = Rect(0, 0, mWidth, mHeight);
				mOutputPanel = emptyTextPanel;
				AddWidget(mOutputPanel);
				//mCreatedTextPanel = true;
			}*/

			int line;
			int column;
			mOutputPanel.EditWidget.Content.GetLineAndColumnAtCoord(mX + mWidth / 2, mY + mHeight / 2, out line, out column);
			mOutputPanel.mHoverWatchLine = (int32)line;

			// So we can default our evaluation to the general global Beef state
			/*let prevIdx = gApp.mDebugger.mSelectedCallStackIdx;
			defer { gApp.mDebugger.mSelectedCallStackIdx = prevIdx; };
			gApp.mDebugger.mSelectedCallStackIdx = -1;	 */

			if (mOutputPanel.mHoverWatch != null)
				mOutputPanel.mHoverWatch.Close();

			String evalStr = scope String();
			evalStr.Append(mExpr);
			//evalStr.AppendF("{0}", mAddr);

		    var hoverWatch = new HoverWatch();
			hoverWatch.mLanguage = .Beef;
			hoverWatch.mDeselectCallStackIdx = true;
		    hoverWatch.mAllowLiterals = true;
		    hoverWatch.mAllowSideEffects = true;
		    //hoverWatch.mOrigEvalString.Set(evalStr);
		    //hoverWatch.SetListView(mImmediateWidget.mResultHoverWatch.mListView);
		    //hoverWatch.Show(mTextPanel, mX + 2, mY + 3, evalStr);
			hoverWatch.mOpenMousePos = DarkTooltipManager.sLastRelMousePos;
			hoverWatch.Show(mOutputPanel, mX, mY, evalStr, evalStr);
		    mOutputPanel.mHoverWatch = hoverWatch;
		}
	}
}
