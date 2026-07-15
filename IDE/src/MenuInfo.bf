using System;
using System.Collections;
using Beefy.sys;
using Beefy.widgets;
using System.Diagnostics;

namespace IDE;

class MenuInfo
{
	public MenuInfo mParent;
	public List<MenuInfo> mChildren ~ DeleteContainerAndItems!(_);
	public delegate void(SysMenu menu) mOnCreate ~ delete _;

	public String mDispString ~ delete _;
	public String mCmdName ~ delete _;
	public String mHotKey ~ delete _;
	public MenuItemUpdateHandler mMenuItemUpdateHandler ~ delete _;
	public MenuItemSelectedHandler mMenuItemSelectedHandler ~ delete _;
	public SysBitmap mBitmap;
	public bool mEnabled;
	public int32 mCheckState;
	public bool mRadioCheck;

	public MenuInfo AddMenuItem(String dispString, String cmdName, String hotKey, MenuItemSelectedHandler menuItemSelectedHandler, MenuItemUpdateHandler menuItemUpdateHandler, SysBitmap bitmap, bool enabled,  int32 checkState, bool radioCheck)
	{
		MenuInfo child = new MenuInfo();
		if (mChildren == null)
			mChildren = new .();
		mChildren.Add(child);

		child.mParent = this;
		if (dispString != null)
			child.mDispString = new .(dispString);
		if (cmdName != null)
			child.mCmdName = new .(cmdName);
		if (hotKey != null)
			child.mHotKey = new .(hotKey);
		child.mMenuItemSelectedHandler = menuItemSelectedHandler;
		child.mMenuItemUpdateHandler = menuItemUpdateHandler;
		child.mBitmap = bitmap;
		child.mEnabled = enabled;
		child.mCheckState = checkState;
		child.mRadioCheck = radioCheck;
		return child;

	}

	public MenuInfo AddMenuItem(String dispString)
	{
		return AddMenuItem(dispString, null, null, null, null, null, true, -1, false);
	}

	public MenuInfo AddMenuItem(String dispString, String cmdName = null, MenuItemUpdateHandler menuItemUpdateHandler = null, SysBitmap bitmap = null, bool enabled = true,  int32 checkState = -1, bool radioCheck = false)
	{
		Debug.Assert(cmdName != null);
		return AddMenuItem(dispString, cmdName, null, null, menuItemUpdateHandler, bitmap, enabled, checkState, radioCheck);
	}

	public virtual MenuInfo AddMenuItem(String dispString, MenuItemSelectedHandler menuItemSelectedHandler, MenuItemUpdateHandler menuItemUpdateHandler = null,
		SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
	{
		return AddMenuItem(dispString, null, null, menuItemSelectedHandler, menuItemUpdateHandler, bitmap, enabled, checkState, radioCheck);
	}

	public virtual void MoveAfter(StringView prevItem)
	{
		mParent.mChildren.Remove(this);
		int idx = mParent.GetMenuIdx(prevItem);
		if (idx == -1)
			idx = mParent.mChildren.Count;
		else
			idx++;
		mParent.mChildren.Insert(idx, this);
	}

	static bool Equals(StringView str, StringView str2)
	{
		int i1 = 0;
		int i2 = 0;

		while ((i1 < str.Length) && (i2 < str2.Length))
		{
			char8 c1 = str[i1];
			if (c1 == '&')
			{
				i1++;
				continue;
			}

			char8 c2 = str2[i2];
			if (c2 == '&')
			{
				i2++;
				continue;
			}

			if (c1 != c2)
				return false;

			i1++;
			i2++;
		}

		return (i1 == str.Length) && (i2 == str2.Length);
	}

	public int GetMenuIdx(StringView str)
	{
		for (var child in mChildren)
		{
			if (Equals(child.mDispString, str))
				return @child.Index;
		}
		return -1;
	}

	public MenuInfo GetMenu(StringView str)
	{
		int menuIdx = GetMenuIdx(str);
		return (menuIdx == -1) ? null : mChildren[menuIdx];
	}
}