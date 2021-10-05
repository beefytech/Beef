using System;
using System.Collections;
using System.Text;
using Beefy.gfx;
using Beefy.widgets;

namespace Beefy.theme.dark
{
    public class DarkDialog : Dialog
    {
        public Font mFont;
        public Insets mTextInsets = new Insets() ~ delete _;
        public float mMinWidth;
        public float mButtonBottomMargin = GS!(12);
        public float mButtonRightMargin = GS!(16);

        public this(String title = null, String text = null, Image icon = null) :
            base(title, text, icon)
        {
            mFont = DarkTheme.sDarkTheme.mSmallFont;

            mTextInsets.Set(GS!(20), GS!(20), GS!(20), GS!(20));

            if (icon != null)
                mTextInsets.mLeft += icon.mWidth + GS!(20);
        }

        public override ButtonWidget CreateButton(String label)
        {
            DarkButton button = new DarkButton();
            button.Label = label;
            button.mAutoFocus = true;
            return button;
        }

        public override EditWidget CreateEditWidget()
        {
            return new DarkEditWidget();
        }

        public override void CalcSize()
        {
            base.CalcSize();

            // Size buttons with a zero width first so we can determine a minimium size this dialog needs to be
            mWidth = mMinWidth;
            mHeight = 0;
            ResizeComponents();

			String str = scope String();
            for (var strView in mText.Split('\n'))
            {
				str.Clear();
				strView.ToString(str);
                mWidth = Math.Max(mWidth, mFont.GetWidth(str) + mTextInsets.Horz);
			}

            mWidth = Math.Max(mWidth, GS!(240));

			float maxWidth = GS!(900);
            if (mWidth >= maxWidth)
            {
				// We don't want to barely size the dialog large enough, otherwise the wrapping would leave just a short chunk
				if (mWidth < maxWidth * 1.4f)
					mWidth = maxWidth * 0.7f;
				else
					mWidth = maxWidth;
			}
            mWidth = Math.Max(-mButtons[0].mX + mTextInsets.mLeft, mWidth);
            mHeight = GS!(80);
            for (var strView in mText.Split('\n'))
			{
				str.Clear();
				strView.ToString(str);
                mHeight += mFont.GetWrapHeight(str, mWidth - mTextInsets.Horz);
			}

            if (mDialogEditWidget != null)
                mHeight += GS!(16);
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float maxTextLen = 0;

            for (DarkButton button in mButtons)
                maxTextLen = Math.Max(maxTextLen, mFont.GetWidth(button.mLabel));

            float buttonBaseSize = GS!(30) + maxTextLen;
            float spacing = GS!(8);

            float curY = mHeight - GS!(20) - mButtonBottomMargin;
            float curX = mWidth - (mButtons.Count * buttonBaseSize) - ((mButtons.Count - 1) * spacing) - mButtonRightMargin;

            if (mDialogEditWidget != null)
            {                
                mDialogEditWidget.Resize(GS!(16), curY - GS!(36), Math.Max(mWidth - GS!(16) * 2, 0), GS!(24));
            }

            for (DarkButton button in mButtons)
            {
                float aSize = buttonBaseSize;
                button.Resize(curX, curY, aSize, GS!(22));
                curX += aSize + spacing;
            }
        }              

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            using (g.PushColor(ThemeColors.Theme.Window.Color))
                g.FillRect(0, 0, mWidth, mHeight);            

            if (mIcon != null)
                g.Draw(mIcon, GS!(20), (mHeight - GS!(8) - mIcon.mHeight) / 2);

            g.SetFont(mFont);
            if (mText != null)
                g.DrawString(mText, mTextInsets.mLeft, mTextInsets.mTop, FontAlign.Left, mWidth - mTextInsets.Horz, FontOverflowMode.Wrap);
        }
    }
}
