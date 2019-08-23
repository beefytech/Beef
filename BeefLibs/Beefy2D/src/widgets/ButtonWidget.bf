using System;
using System.Collections.Generic;
using System.Text;
using Beefy.gfx;
using System.Runtime.InteropServices;

namespace Beefy.widgets
{
    public class ButtonWidget : Widget
    {
        public bool mDisabled;

        public override void Draw(Graphics g)
        {            
            
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);

            if ((keyCode == KeyCode.Return) || (keyCode == KeyCode.Space))
                MouseDown(0, 0, 3, 1);
            else
                mParent.KeyDown(keyCode, isRepeat);            
        }

        public override void KeyUp(KeyCode keyCode)
        {
            base.KeyUp(keyCode);

            if ((keyCode == KeyCode.Return) || (keyCode == KeyCode.Space))
            {
                if (mMouseFlags != 0)
                    MouseUp(0, 0, 3);
            }
            else
                mParent.KeyUp(keyCode);
        }        
    }
}
