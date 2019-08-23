using Beefy.widgets;
using System;
using System.Collections.Generic;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.events;

namespace IDE.ui
{
	class MultiSelectDialog : IDEDialog
	{
		class ListView : IDEListView
		{
			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				base.KeyDown(keyCode, isRepeat);

				var multiSelectDialog = (MultiSelectDialog)mParent;

				if (keyCode == .Space)
				{
					var root = GetRoot();
					int childCount = root.GetChildCount();
					for (int i < childCount)
					{
						var listViewItem = root.GetChildAtIndex(i);
						if (listViewItem.Selected)
						{
							var entry = multiSelectDialog.[Friend]mEntries[i];
							entry.mCheckbox.Checked = !entry.mCheckbox.Checked;
						}
					}
				}
			}
		}

		ListView mListView;

		public class Entry
		{
			public String mName ~ delete _;
			public CheckBox mCheckbox;
			public bool mInitialChecked;
		}

		public List<Entry> mEntries = new .() ~ DeleteContainerAndItems!(_);
		Dictionary<ListViewItem, Entry> mEntryMap = new .() ~ delete _;

		public this()
		{
			mTitle = new String("Configuration Manager");
			mWindowFlags |= .Resizable;

			mListView = new ListView();
			mListView.InitScrollbars(false, true);
			mListView.mVertScrollbar.mPageSize = GS!(100);
			mListView.mVertScrollbar.mContentSize = GS!(500);
			mListView.UpdateScrollbars();
			mListView.AddColumn(100, "Name");
			mListView.mLabelX = GS!(32);
			mTabWidgets.Add(mListView);

			AddWidget(mListView);
		}

		public void Add(String name, bool isChecked = false)
		{
			var entry = new Entry();
			entry.mName = new String(name);
			entry.mInitialChecked = isChecked;
			mEntries.Add(entry);
		}

		public void FinishInit()
		{
			mEntries.Sort(scope (lhs, rhs) => String.Compare(lhs.mName, rhs.mName, true));

			for (let entry in mEntries)
			{
				let listViewItem = mListView.GetRoot().CreateChildItem();
				listViewItem.Label = entry.mName;
				listViewItem.mOnMouseDown.Add(new => ValueClicked);

				entry.mCheckbox = new DarkCheckBox();
				entry.mCheckbox.Checked = entry.mInitialChecked;
				entry.mCheckbox.Resize(GS!(8), GS!(1), GS!(20), GS!(20));
				listViewItem.AddWidget(entry.mCheckbox);

				if (@entry.Index == 0)
					listViewItem.Focused = true;

				mEntryMap[listViewItem] = entry;
			}
		}

		public override void AddedToParent()
		{
			mListView.SetFocus();
		}

		public override void CalcSize()
		{
		    mWidth = GS!(280);
		    mHeight = GS!(190);
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			mListView.Resize(GS!(6), GS!(6), mWidth - GS!(12), mHeight - GS!(48));
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
			IDEUtils.DrawOutline(g, mListView, 0, 1);
		}

		public void ValueClicked(MouseEvent theEvent)
		{
		    let clickedItem = (DarkListViewItem)theEvent.mSender;
		    mListView.SetFocus();

			if (theEvent.mBtn == 1)
			{
				if (!clickedItem.Selected)
					mListView.GetRoot().SelectItem(clickedItem, true);
			}
			else
			{
				mListView.GetRoot().SelectItem(clickedItem, true);
			}
		}
	}
}
