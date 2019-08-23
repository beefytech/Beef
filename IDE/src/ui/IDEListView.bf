using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy.gfx;
using Beefy.geom;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.events;

namespace IDE.ui
{
    public class IDEListViewItem : DarkListViewItem
    {
    }

    public class IDEListView : DarkListView
    {
		public bool mCancelingEdit;
		public ListViewItem mEditingItem;
		public Event<delegate void(EditWidget editWidget, bool cancelled)> mOnEditDone ~ _.Dispose();
		public SingleLineEditWidget mEditWidget;

		//public delegate void DoneEvent(EditWidget editWidget, bool cancelled);
		//public Event<DoneEvent> mOnEditDone;

		public this()
		{
			mAutoFocus = true;
		}

        protected override ListViewItem CreateListViewItem()
        {
            var anItem = new IDEListViewItem();
            return anItem;
        }

		public void EditListViewItem(ListViewItem listViewItem)
        {
            mCancelingEdit = false;
			mEditingItem = (DarkListViewItem)listViewItem;

            SingleLineEditWidget editWidget = new SingleLineEditWidget();
			mEditWidget = editWidget;
            editWidget.SetText(listViewItem.mLabel);
            editWidget.Content.SelectAll();

			ResizeComponents();
            AddWidget(editWidget);            

            editWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
            editWidget.mOnSubmit.Add(new => HandleRenameSubmit);
            editWidget.mOnCancel.Add(new => HandleRenameCancel);
            editWidget.SetFocus();
        }

		void HandleEditLostFocus(Widget widget)
		{
		    EditWidget editWidget = (EditWidget)widget;
		    editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
		    editWidget.mOnSubmit.Remove(scope => HandleRenameSubmit, true);
		    editWidget.mOnCancel.Remove(scope => HandleRenameCancel, true);

			mOnEditDone(editWidget, mCancelingEdit);

			mEditWidget = null;
			editWidget.RemoveSelf();
			gApp.DeferDelete(editWidget);
		}

		void HandleRenameCancel(EditEvent theEvent)
		{
		    mCancelingEdit = true;
		    HandleEditLostFocus((EditWidget)theEvent.mSender);
		}

		void HandleRenameSubmit(EditEvent theEvent)
		{
		    HandleEditLostFocus((EditWidget)theEvent.mSender);
		}

		void ResizeComponents()
		{
			if (mEditWidget != null)
			{
				let listViewItem = mEditingItem;

				float aX;
				float aY;
				listViewItem.SelfToOtherTranslate(this, 0, 0, out aX, out aY);

				aY += GS!(0);
				aX = listViewItem.LabelX - GS!(4);
				float aWidth = mWidth - aX - GS!(20);
				mEditWidget.ResizeAround(aX, aY, aWidth);
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}
    }
}
