using Beefy.theme.dark;

namespace IDE.ui
{
	class SingleLineEditWidgetContent : DarkEditWidgetContent
	{
		public this()
		{
			SetFont(DarkTheme.sDarkTheme.mSmallFont, false, false);
			mTextInsets.Set(GS!(1), GS!(2), 0, GS!(2));
		}

		public override float GetLineHeight(int line)
		{
			return GS!(21);
		}
	}

	class SingleLineEditWidget : DarkEditWidget
	{
		public this()
		    : base(new SingleLineEditWidgetContent())
		{
			mScrollContentInsets.Set(GS!(2), GS!(3), GS!(2), GS!(3));
		}

		public void ResizeAround(float targetX, float targetY, float width)
		{
			Resize(targetX - GS!(1), targetY - GS!(1), width + GS!(2), GS!(23));
		}
	}
}
