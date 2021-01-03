using System;
using System.Collections;
using System.Text;

namespace Beefy.events
{
	public enum DragKind
	{
		None,
		Inside,
		Before,
		After
	}

    public class DragEvent : Event
    {
        public float mX;
        public float mY;
        public Object mDragTarget;
        public DragKind mDragKind = .Inside;
    }
}
