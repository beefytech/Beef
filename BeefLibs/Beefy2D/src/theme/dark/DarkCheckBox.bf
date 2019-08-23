using System;
using System.Collections.Generic;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace Beefy.theme.dark
{
    public class DarkCheckBox : CheckBox, IHotKeyHandler
    {
        public bool mLargeFormat;
        public Image mCheckIcon;
		public Image mIndeterminateIcon;
        public String mLabel ~ delete _;
        public Font mFont;
		public bool mDisabled;
		public State mState;

		public override bool Checked
		{
			get
			{
				return mState != .Unchecked;
			}

			set
			{
				mState = value ? .Checked : .Unchecked;
			}
		}

		public override State State
		{
			get
			{
				return mState;
			}

			set
			{
				mState = value;
			}
		}

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

        public this()
        {
            mMouseInsets = new Insets(2, 2, 3, 3);
            mCheckIcon = DarkTheme.sDarkTheme.GetImage(.Check);
			mIndeterminateIcon = DarkTheme.sDarkTheme.GetImage(.CheckIndeterminate);
            mFont = DarkTheme.sDarkTheme.mSmallFont;
        }

		bool IHotKeyHandler.Handle(KeyCode keyCode)
		{
			if (mDisabled)
				return false;

			if (DarkTheme.CheckUnderlineKeyCode(mLabel, keyCode))
			{
				//SetFocus();
				Checked = !Checked;
				return true;
			}
			return false;
		}

        public override void DefaultDesignInit()
        {
            base.DefaultDesignInit();
            mIdStr = "CheckBox";
            mWidth = 20;
            mHeight = 20;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            if (mLargeFormat)
            {
                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.CheckboxLarge));
            }
            else
            {
                if (mMouseOver)
                {
                    if (mMouseDown)
                        g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.CheckboxDown));
                    else
                        g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.CheckboxOver));
                }
                else
                    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Checkbox));
            }

            if (mState == .Checked)
                g.Draw(mCheckIcon);
			else if (mState == .Indeterminate)
				g.Draw(mIndeterminateIcon);

			if (mHasFocus)
			{
			    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
			        g.DrawButton(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, mWidth);
			}

            if (mLabel != null)
            {
				g.SetFont(mFont);

				DarkTheme.DrawUnderlined(g, mLabel, GS!(22), 0);

				/*int underlinePos = mLabel.IndexOf('&');
				if ((underlinePos != -1) && (underlinePos < mLabel.Length - 1))
				{
					String label = scope String();
					label.Append(mLabel, 0, underlinePos);
					float underlineX = mFont.GetWidth(label);

					char32 underlineC = mLabel.GetChar32(underlinePos + 1).0;
					float underlineWidth = mFont.GetWidth(underlineC);

					label.Append(mLabel, underlinePos + 1);
					g.DrawString(label, GS!(22), 0);

					g.FillRect(GS!(22) + underlineX, mFont.GetAscent() + GS!(1), underlineWidth, (int)GS!(1.2f));
				}
				else
				{
	                g.DrawString(mLabel, GS!(22), 0);
				}*/
            }
        }

		public override void DrawAll(Graphics g)
		{
			if (mDisabled)
			{
				using (g.PushColor(0x80FFFFFF))
					base.DrawAll(g);
			}
			else
				base.DrawAll(g);
		}

        public float CalcWidth()
        {
            if (mLabel == null)
                return 20;
            return mFont.GetWidth(mLabel) + GS!(22);
        }

		public override void KeyChar(char32 theChar)
		{
			if (mDisabled)
				return;

			base.KeyChar(theChar);
			if (theChar == ' ')
				Checked = !Checked;
		}
    }
}
