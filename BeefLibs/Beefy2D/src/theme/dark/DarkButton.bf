using System;
using System.Collections.Generic;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.utils;

namespace Beefy.theme.dark
{
    public class DarkButton : ButtonWidget, IHotKeyHandler
    {
        public String mLabel ~ delete _;
		public float mDrawDownPct;
		public float mLabelYOfs;

        [DesignEditable(DefaultEditString=true)]
		public String Label
		{
			get
			{
				return mLabel;
			}

			set
			{
				String.NewOrSet!(mLabel, value);
			}
		}

		bool IHotKeyHandler.Handle(KeyCode keyCode)
		{
			if (mDisabled)
				return false;

			if (DarkTheme.CheckUnderlineKeyCode(mLabel, keyCode))
			{
				mDrawDownPct = 1.0f;
				MouseClicked(0, 0, 0, 0, 3);
				return true;
			}
			return false;
		}

        public override void DefaultDesignInit()
        {
            base.DefaultDesignInit();
            mIdStr = "Button";
            Label = "Button";
            mWidth = GS!(80);
            mHeight = GS!(20);
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			bool drawDown = ((mMouseDown && mMouseOver) || (mMouseFlags.HasFlag(MouseFlag.Kbd)));
			if (mDrawDownPct > 0)
				drawDown = true;

            Image texture = drawDown ? DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.BtnDown] :
                mMouseOver ? DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.BtnOver] :
                DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.BtnUp];

            if (mDisabled)
                texture = DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.BtnUp];

            g.DrawBox(texture, 0, 0, mWidth, mHeight);

            if ((mHasFocus) && (!mDisabled))
            {
                using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
                    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, mWidth, mHeight);
            }

            g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
            if (mLabel != null)
            {
                using (g.PushColor(mDisabled ? 0x80FFFFFF : Color.White))
                {
					DarkTheme.DrawUnderlined(g, mLabel, GS!(2), (mHeight - GS!(20)) / 2 + mLabelYOfs, .Centered, mWidth - GS!(4), .Truncate);
                }
            }
        }

		public override void Update()
		{
			base.Update();
			if (mDrawDownPct > 0)
			{
				mDrawDownPct = Math.Max(mDrawDownPct - 0.25f, 0);
				MarkDirty();
			}
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);
            data.Add("Label", mLabel);
        }

        public override bool Deserialize(StructuredData data)
        {
            base.Deserialize(data);
            data.GetString(mLabel, "Label");
            return true;
        }

		public override void GotFocus()
		{
			base.GotFocus();
		}
    }
}
