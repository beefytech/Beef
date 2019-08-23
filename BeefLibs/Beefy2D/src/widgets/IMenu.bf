using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.widgets
{
    public delegate void MenuItemSelectedHandler(IMenu menu);
    public delegate void MenuItemUpdateHandler(IMenu menu);

    public interface IMenu
    {
		void SetDisabled(bool enable);
    }

	public interface IMenuContainer
	{

	}
}
