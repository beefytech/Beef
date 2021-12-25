#if false

using Beefy.theme;
using System;
using System.Collections;
using System.Linq;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace IDE
{
    public class Popup : Widget
    {
        public override void Draw(Graphics g)
        {
            base.Draw(g);
            using (g.PushColor(ThemeColors.Widget.FindListViewItem032.Color))
                g.FillRect(0, 0, mWidth - 20, mHeight - 20);            
        }
    }
}

#endif
