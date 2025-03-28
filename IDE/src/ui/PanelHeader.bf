using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.geom;

namespace IDE.ui
{
    public class PanelHeader : Widget
    {
		public enum Kind
		{
			None,
			WrongHash
		}

		public Kind mKind;
        public List<DarkButton> mButtons = new List<DarkButton>() ~ delete _;
        public String mLabel ~ delete _;
		public String mTooltipText ~ delete _;
        public float mFlashPct;
		public int mBaseHeight = 32;
		public bool mButtonsOnBottom;

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

        public override void Draw(Graphics g)
        {
            g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.PanelHeader), 0, 0, mWidth, mHeight);
            g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			float y = GS!(6);
            using (g.PushColor(0xFFFFFFFF))
            {
				for (var str in mLabel.Split('\n'))
                {
					float maxTextLen = mWidth - GS!(12 + 12);
					if ((mButtons.Count > 0) && (!@str.HasMore))
					    maxTextLen = mButtons[0].mX - GS!(12 + 2);

                    g.DrawString(scope String(str), GS!(12), y, FontAlign.Left, maxTextLen, FontOverflowMode.Ellipsis);
					y += g.mFont.GetLineSpacing();
				}
			}

            if (mFlashPct > 0)
            {
                float showPct = Math.Max(0.0f, (Math.Sin(mFlashPct * Math.PI_f * 4))) * 0.25f;
                using (g.PushColor(Color.Get(0xFFFFFFFF, showPct)))
                    g.FillRect(0, 0, mWidth, mHeight);
            }
        }

        void ResizeComponents()
        {
            var font = DarkTheme.sDarkTheme.mSmallFont;
            float x;
			
            x = mWidth - GS!(10);

			for (int buttonIdx = mButtons.Count - 1; buttonIdx >= 0; buttonIdx--)
			{
			    var button = mButtons[buttonIdx];

				float width = font.GetWidth(button.mLabel) + GS!(24);
		        x -= width;
		        button.Resize(x, mHeight - GS!(28), width, GS!(22));
		        x -= GS!(8);

		        button.mVisible = x >= 0;
			}
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        public ButtonWidget AddButton(String label)
        {
            DarkButton button = new DarkButton();
            button.Label = label;
            mButtons.Add(button);
            AddWidget(button);
            return button;
        }

		public void RemoveButtons()
		{
			for (var button in mButtons)
			{
				RemoveWidget(button);
				delete button;
			}
			mButtons.Clear();
		}

        public void Flash()
        {
            mFlashPct = 1.0f;
        }

        public override void Update()
        {
            base.Update();
			if (mFlashPct > 0)
            {
                mFlashPct = Math.Max(0, mFlashPct - 0.015f);
				MarkDirty();
			}

			if ((mTooltipText != null) && (!DarkTooltipManager.IsTooltipShown(this)))
			{
				var font = DarkTheme.sDarkTheme.mSmallFont;
				var tooltipWidth = font.GetWidth(mTooltipText);
				//if (tooltipWidth > mWidth - GS!(8))
				{
					Point point;
					if (DarkTooltipManager.CheckMouseover(this, 25, out point))
					{
						float showWidth = Math.Max(64, Math.Min(tooltipWidth, mWidth * 0.6f));
						float showHeight = font.GetWrapHeight(mTooltipText, showWidth);

						var tooltip = DarkTooltipManager.ShowTooltip("", this, point.x, 14, showWidth, showHeight + GS!(42), true, true);
						if (tooltip != null)
						{
							var editWidget = new WatchStringEdit(mTooltipText, null);
							
							tooltip.mRelWidgetMouseInsets = new Insets(0, 0, GS!(-8), 0);
							tooltip.mAllowMouseInsideInsets = new .();
							tooltip.AddWidget(editWidget);
							tooltip.mOnResized.Add(new (widget) => editWidget.Resize(GS!(6), GS!(6), widget.mWidth - GS!(6) * 2, widget.mHeight - GS!(6) * 2));
							tooltip.mOnResized(tooltip);
						}
					}
				}
			}
			//if (gApp.UpdateM)
        }
    }
}
