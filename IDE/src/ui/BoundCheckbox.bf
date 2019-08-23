using Beefy.theme.dark;

namespace IDE.ui
{
	class BoundCheckbox : DarkCheckBox
	{
		bool* mBoundValue;

		public this(ref bool boundValue)
		{
			mBoundValue = &boundValue;
		}

		public override bool Checked
		{
			get
			{
				mState = *mBoundValue ? .Checked : .Unchecked;
				//base.Checked = *mBoundValue;
				return *mBoundValue;
			}

			set
			{
				*mBoundValue = value;
				mState = *mBoundValue ? .Checked : .Unchecked;
			}
		}

		public override void Update()
		{
			base.Update();
			Checked = *mBoundValue;
		}
	}
}
