using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace Beefy.theme.dark
{
    public class DarkIconButton : ButtonWidget
    {
        public Image mIcon;
        public float mIconOfsX;
        public float mIconOfsY;

        public override void Draw(Graphics g)
        {
            base.Draw(g);
            g.Draw(mIcon, mIconOfsX, mIconOfsY);
        }
    }
}
