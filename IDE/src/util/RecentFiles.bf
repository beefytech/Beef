using System.Collections;
using System;
using System.IO;
using Beefy.sys;

namespace IDE.util
{
	class RecentFiles
	{
		public enum RecentKind
		{
			OpenedWorkspace,
			OpenedProject,
			OpenedFile,
			OpenedCrashDump,
			OpenedDebugSession,

			COUNT
		}

		public class Entry
		{
			public List<String> mList = new List<String>() ~ DeleteContainerAndItems!(_);
			public SysMenu mMenu;
			public List<SysMenu> mMenuItems = new List<SysMenu>() ~ delete _;
		}

		public List<Entry> mRecents = new .() ~ DeleteContainerAndItems!(_);

		public this()
		{
			for (RecentFiles.RecentKind recentKind = default; recentKind < RecentFiles.RecentKind.COUNT; recentKind++)
			{
				mRecents.Add(new Entry());
			}
		}

		public List<String> GetRecentList(RecentKind recentKind)
		{
			return mRecents[(int)recentKind].mList;
		}

		public static void UpdateMenu(List<String> items, SysMenu menu, List<SysMenu> menuItems, bool showSplit, delegate void(int idx, SysMenu sysMenu) onNewEntry)
		{
			if ((menuItems.IsEmpty) && (!items.IsEmpty) && (showSplit))
			{
				menuItems.Add(menu.AddMenuItem(null, null));
			}

			int offset = showSplit ? 1 : 0;

			int itemCount = Math.Min(items.Count, 10);

			int32 i;
			for (i = 0; i < itemCount; i++)
			{
			    String title = scope String();
			    if (i + 1 == 10)
			        title.AppendF("1&0 {1}", i + 1, items[i]);
			    else
			        title.AppendF("&{0} {1}", i + 1, items[i]);
			    if (i < menuItems.Count - offset)
			    {
			        menuItems[i + offset].Modify(title);
			    }
			    else
			    {
			        int32 idx = i;

					let newMenuItem = menu.AddMenuItem(title);
					menuItems.Add(newMenuItem);
					if (onNewEntry != null)
						onNewEntry(idx, newMenuItem);
			        //menuItems.Add(menu.AddMenuItem(title, null, new (evt) => openEntry(idx)));
			    }
			}

			while (i < menuItems.Count - offset)
			{
				var menuItem = menuItems[i + offset];
				menuItem.Dispose();
				delete menuItem;
			    menuItems.RemoveAt(i + offset);
			}

			if ((!menuItems.IsEmpty) && (items.IsEmpty))
			{
				var menuItem = menuItems[0];
				menuItem.Dispose();
				delete menuItem;
				menuItems.Clear();
			}

			/*if (menu.ChildCount == 0)
			{
				let newMenuItem = menu.AddMenuItem("< None >", null, null, null, null, false);
				menuItems.Add(newMenuItem);
			}*/
		}

		public static void Add(List<String> list, StringView path, int32 maxCount = 10)
		{
			int32 idx = -1;
			for (int32 i = 0; i < list.Count; i++)
			{
				if (Path.Equals(list[i], path))
					idx = i;
			}

			if (idx != -1)
			{
				String entry = list[idx];
				list.RemoveAt(idx);
				list.Insert(0, entry);
			}
			else
				list.Insert(0, new String(path));

			while (list.Count > maxCount)
			{
				delete list.PopBack();
			}
		}

		public void Add(RecentKind recentKind, StringView path)
		{
			Add(mRecents[(int32)recentKind].mList, path);
		}
	}
}
