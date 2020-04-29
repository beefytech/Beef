using System;
using System.Collections;
using System.Text;
using System.Diagnostics;

namespace Beefy.widgets
{
    public abstract class CheckBox : Widget
    {
		public enum State
		{
			Unchecked,
			Checked,
			Indeterminate
		}

        [DesignEditable]
        public abstract bool Checked { get; set; }
		public virtual State State
			{
				get
				{
					return Checked ? .Checked : .Unchecked;
				}

				set
				{
					Debug.Assert(value != .Indeterminate);
					Checked = value != .Unchecked;
				}
			}
		public Event<Action> mOnValueChanged ~ _.Dispose();

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
        }

		public override void MouseUp(float x, float y, int32 btn)
		{
			if (mMouseOver)
			{
				Checked = !Checked;
				mOnValueChanged();
			}
			base.MouseUp(x, y, btn);
		}
    }
}
