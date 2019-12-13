using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;

namespace IDE.ui
{
    public enum LocatorType
    {
        None,
        Smart,
        Always,
		Extra
    }

    public class LocatorAnim : Widget
    {
		LocatorType mType;
        float mPct;

		public this()
		{

		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			bool isExtra = mType == .Extra;

            int32 circleCount = 2;
            for (int32 i = 0; i < circleCount; i++)
            {
                float sepPct = 0.3f;
                float maxSep = (circleCount - 2) * sepPct;
                float pct = (mPct - maxSep + (i * 0.3f)) / (1.0f - maxSep);

				if (isExtra)
					pct *= 1.2f;

                if ((pct < 0.0f) || (pct > 1.0f))
                    continue;

                float scale = (float)Math.Sin(pct * Math.PI_f / 2) * 0.5f;
                float alpha = Math.Min(0.3f, (1.0f - (float)Math.Sin(pct * Math.PI_f / 2)) * 1.0f);

				if (isExtra)
				{
					scale *= 1.2f;
					alpha *= 1.2f;
				}

                using (g.PushColor(Color.Get(alpha)))
                {
                    using (g.PushScale(scale, scale, GS!(32), GS!(32)))
                        g.Draw(IDEApp.sApp.mCircleImage);
                }                
            }
        }

        public override void Update()
        {
            base.Update();

            mPct += (mType == .Extra) ? 0.02f : 0.03f;

            if (mPct >= 1.0f)
			{
                RemoveSelf();
				//DeferDelete();
				gApp.DeferDelete(this);
			}

			MarkDirty();
        }

        public static void Show(LocatorType locatorType, Widget refWidget, float x, float y)
        {
			if (!gApp.mSettings.mEditorSettings.mShowLocatorAnim)
				return;

			float xOfs = GS!(-32.0f);
			float yOfs = GS!(-32.0f);

            LocatorAnim anim = new LocatorAnim();
			anim.mType = locatorType;
            anim.mX = x + xOfs;
            anim.mY = y + yOfs;
            refWidget.AddWidget(anim);            
        }
    }
}
