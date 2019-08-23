using System;
using System.Collections.Generic;
using System.Text;
using Beefy.gfx;


namespace Beefy.widgets
{
    public class LabelWidget : Widget
    {
        public Font mFont;
        public String mLabel;
        public uint32 mColor = Color.White;

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            g.SetFont(mFont);
            using (g.PushColor(mColor))
                g.DrawString(mLabel, 0, 0);
        }
    }
}
