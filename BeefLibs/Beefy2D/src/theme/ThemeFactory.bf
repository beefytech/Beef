using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace Beefy.theme
{
    public class DesignToolboxEntry
    {
        public String mGroupName;
        public String mName;
        public String mSortName;
        public Image mIcon;
        public Type mType;

        public this(String name, Type theType, Image icon = null)
        {
            int32 index = (int32)name.IndexOf('@');
            if (index != -1)
            {
                mName = new String(index);
                mName.Append(name, 0, index);
				mSortName = new String();
                mSortName.Append(name, index + 1);
            }
            else
            {
                mName = new String(name);
                mSortName = new String(name);
            }
            mType = theType;
            mIcon = icon;
        }
    }

    public class ThemeFactory
    {
        public static ThemeFactory mDefault;

        public virtual void Init()
        {
        }

		public virtual void Update()
		{
		}

        public virtual ButtonWidget CreateButton(Widget parent, String caption, float x, float y, float width, float height)
        {
            return null;
        }

        public virtual CheckBox CreateCheckbox(Widget parent, float x = 0, float y = 0, float width = 0, float height = 0)
        {
            return null;
        }

        public virtual EditWidget CreateEditWidget(Widget parent, float x = 0, float y = 0, float width = 0, float height = 0)
        {
            return null;
        }

        public virtual TabbedView CreateTabbedView(TabbedView.SharedData sharedData, Widget parent = null, float x = 0, float y = 0, float width = 0, float height = 0)
        {
            return null;
        }

        public virtual DockingFrame CreateDockingFrame(DockingFrame parent = null)
        {
            return null;
        }

        public virtual ListView CreateListView()
        {
            return null;
        }

        public virtual Scrollbar CreateScrollbar(Scrollbar.Orientation orientation)
        {
            return null;
        }
       
        public virtual InfiniteScrollbar CreateInfiniteScrollbar()
        {
            return null;
        }

        public virtual MenuWidget CreateMenuWidget(Menu menu)
        {
            return null;
        }

        public virtual Dialog CreateDialog(String title = null, String text = null, Image icon = null)
        {
            return null;
        }
    }
}
