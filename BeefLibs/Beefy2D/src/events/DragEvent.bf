using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.events
{
    public class DragEvent : Event
    {
        public float mX;
        public float mY;
        public Object mDragTarget;
        public int32 mDragTargetDir;
        public bool mDragAllowed = true;
    }
}
