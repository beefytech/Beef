using System;
using System.Collections.Generic;
using System.Text;
using Beefy.widgets;

namespace Beefy.events
{
    public class MouseEvent : Event
    {                
        public float mX;
        public float mY;
        public int32 mBtn;
        public int32 mBtnCount;
        public int32 mWheelDelta;

        public void GetRootCoords(out float x, out float y)
        {
            Widget widget = (Widget)mSender;
            widget.SelfToRootTranslate(mX, mY, out x, out y);
        }
    }
}
