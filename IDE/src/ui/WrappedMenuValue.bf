using Beefy.widgets;
using Beefy.sys;
namespace IDE.ui
{
	struct WrappedMenuValue
	{
		public IMenu mMenu;
		public bool mIsSet;

		public bool Bool
		{
			get
			{
				return mIsSet;
			}

			set mut
			{
				mIsSet = value;
				var sysMenu = (SysMenu)mMenu;
				sysMenu.Modify(null, null, null, true, mIsSet ? 1 : 0);
			}
		}

		public void Toggle() mut
		{
			Bool = !mIsSet;
		}

		public this(bool isSet)
		{
			mMenu = null;
			mIsSet = isSet;
		}

		public this(IMenu menu, bool isSet)
		{
			mMenu = menu;
			mIsSet = isSet;
		}
	}
}
