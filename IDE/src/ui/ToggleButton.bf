using Beefy.theme.dark;
using Beefy.gfx;

namespace IDE.ui;

class ToggleButton : DarkButton
{
	public bool mToggled;

	public override void Draw(Graphics g)
	{
		base.Draw(g);

		if (mToggled)
		{
			g.DrawBox(DarkTheme.sDarkTheme.GetImage(mHasFocus ? DarkTheme.ImageIdx.MenuSelect : DarkTheme.ImageIdx.MenuNonFocusSelect),
				0, 0, mWidth, mHeight);
		}
	}

	public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
	{
		mToggled = !mToggled;
		base.MouseDown(x, y, btn, btnCount);
	}
}