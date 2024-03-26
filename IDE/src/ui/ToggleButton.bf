using Beefy.theme.dark;
using Beefy.gfx;
using System;
using Beefy.geom;

namespace IDE.ui;

class ToggleButton : DarkButton
{
	public bool mToggled;
	public String mHoverText ~ delete _;

	public StringView HoverText
	{
		get => mHoverText;
		set => String.NewOrSet!(mHoverText, value);
	}

	public override void Draw(Graphics g)
	{
		base.Draw(g);

		if (mToggled)
		{
			g.DrawBox(DarkTheme.sDarkTheme.GetImage(mHasFocus ? DarkTheme.ImageIdx.MenuSelect : DarkTheme.ImageIdx.MenuNonFocusSelect),
				0, 0, mWidth, mHeight);
		}
	}

	public override void Update()
	{
		base.Update();

		if ((DarkTooltipManager.sLastMouseWidget == this) && (mHoverText != null))
		{
		    Point mousePoint;
		    if (DarkTooltipManager.CheckMouseover(this, 20, out mousePoint))
		    {
				DarkTooltipManager.ShowTooltip(mHoverText, this, 0, mHeight);
		    }
		}
	}

	public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
	{
		mToggled = !mToggled;
		base.MouseDown(x, y, btn, btnCount);
	}
}