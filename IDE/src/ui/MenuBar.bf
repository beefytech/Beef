using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.geom;
using IDE;
using IDE.util;
using System;
using System.Diagnostics;
using System.Collections;
using Beefy.sys;

namespace IDE.ui;

public class MenuBar : Widget
{
	class UISysMenu : SysMenu
	{
		public override SysMenu AddMenuItem(String text, String hotKey = null, MenuItemSelectedHandler menuItemSelectedHandler = null,
			MenuItemUpdateHandler menuItemUpdateHandler = null, SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
		{
			if (mChildren == null)
			    mChildren = new List<SysMenu>();

			UISysMenu sysMenu = new UISysMenu();
			if (text != null)
			{
				sysMenu.mText = new String(text);
				let bindIndex = sysMenu.mText.IndexOf('&');
				if (bindIndex != -1)
					sysMenu.mText.Remove(bindIndex);
			}
			if ((hotKey != null) && (hotKey.Length != 0) && ((hotKey.Length > 1 ) || (hotKey[0] != '#')))
			{
				sysMenu.mHotKey = new String(hotKey);
				sysMenu.mHotKey.TrimStart('#');
			}

			sysMenu.mBitmap = bitmap;
			sysMenu.mEnabled = enabled;
			sysMenu.mCheckState = checkState;
			sysMenu.mRadioCheck = radioCheck;
			if (menuItemSelectedHandler != null)
				sysMenu.mOnMenuItemSelected.Add(menuItemSelectedHandler);
			if (menuItemUpdateHandler != null)
				sysMenu.mOnMenuItemUpdate.Add(menuItemUpdateHandler);
			sysMenu.mNativeBFMenu = null;
			sysMenu.mParent = this;
			sysMenu.mWindow = mWindow;
			mChildren.Add(sysMenu);

			return sysMenu;
		}

		public override void Modify(String text, String hotKey = null, SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
		{
			if (((Object)mText != text) && (text != null))
			{
				if (text.Length > 0)
				{
					String.NewOrSet!(mText, text);
					let bindIndex = mText.IndexOf('&');
					if (bindIndex != -1)
						mText.Remove(bindIndex);
				}
				else
					DeleteAndNullify!(mText);
			}

			if (((Object)mHotKey != hotKey) && (hotKey != null))
			{
				if ((hotKey.Length > 0) && ((hotKey.Length > 1 ) || (hotKey[0] != '#')))
				{
					String.NewOrSet!(mHotKey, hotKey);
					mHotKey.TrimStart('#');
				}
				else
					DeleteAndNullify!(mHotKey);
			}
			mBitmap = bitmap;
			mEnabled = enabled;
			mCheckState = checkState;
			mRadioCheck = radioCheck;
		}
	}

	public class UISysMenuRoot : UISysMenu
	{
		public MenuBar mRootMenuBar;
		public this()
		{

		}

		public override SysMenu AddMenuItem(String text, String hotKey = null, MenuItemSelectedHandler menuItemSelectedHandler = null, MenuItemUpdateHandler menuItemUpdateHandler = null,
			SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
		{
			let item =  base.AddMenuItem(text, hotKey, menuItemSelectedHandler, menuItemUpdateHandler, bitmap, enabled, checkState, radioCheck);
			let button = new Button();
			button.Label = text;
			button.mSysMenu = item;
			button.mOnMouseClick.Add(new (evt) => {
				mOnMenuItemSelected(this);
				mRootMenuBar.ShowMenu(button);
			});
			mRootMenuBar.AddItemButton(button);
			return item;
		}
	}

	public class Button : ButtonWidget
	{
		private String mLabel ~ delete _;

		public StringView Label
		{
			get
			{
				return mLabel;
			}

			set
			{
				String.NewOrSet!(mLabel, value);
			}
		}

		public SysMenu mSysMenu;

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			if (mMouseOver)
			{
				using (g.PushColor(0x40FFFFFF))
				{
					g.FillRect(0, 0, mWidth, mHeight);
				}
			}

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			if (mLabel != null)
			{
			    using (g.PushColor(mDisabled ? 0x80FFFFFF : Color.White))
			    {
					using (g.PushColor(DarkTheme.COLOR_TEXT))
						DarkTheme.DrawUnderlined(g, mLabel, GS!(2), (mHeight - GS!(20)) / 2, .Centered, mWidth - GS!(4), .Truncate);
			    }
			}
		}
	}


	public append List<Button> mButtons = .(8);
	public UISysMenuRoot mSysMenuRoot = new .() ~ delete _;

	public this()
	{
		mSysMenuRoot.mRootMenuBar = this;
	}

	public override void InitChildren()
	{
		base.InitChildren();
		mSysMenuRoot.mWindow = mWidgetWindow;
	}

	public void AddItemButton(Button btn)
	{
		mButtons.Add(btn);
		AddWidget(btn);
	}

	public void ShowMenu(Button btn)
	{
		if (btn.mSysMenu.mChildren == null)
			return;

		let menu = new Menu();
		btn.mSysMenu.mOnMenuItemUpdate(btn.mSysMenu);
		PopulateMenu(menu, btn.mSysMenu);
		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
		menuWidget.Init(btn, 0, mHeight);
	}

	void PopulateMenu(Menu parentMenu, SysMenu sysMenu)
	{
		if (sysMenu.mChildren == null)
			return;

		String buffer = scope .();
		for (let item in sysMenu.mChildren)
		{
			if (item.mText == null)
			{
				parentMenu.AddItem(null);
				continue;
			}

			StringView displayText = item.mText;
			if (!String.IsNullOrEmpty(item.mHotKey))
			{
				buffer..Clear().AppendF($"{item.mText}|{item.mHotKey}");
				displayText = buffer;
			}

			item.mOnMenuItemUpdate(item);
			let menu = AddMenuItem(parentMenu, displayText, new (menu) => item.mOnMenuItemSelected(menu), !item.mEnabled, item.mCheckState > 0, item.mRadioCheck);
			PopulateMenu(menu, item);
		}
	}

	Menu AddMenuItem(Menu menu, StringView label, delegate void(Menu menu) action, bool disabled = false, bool isChecked = false, bool radio = false)
	{
		Menu item = menu.AddItem(label);
		item.SetDisabled(disabled);

		if (isChecked)
		{
			item.mIconImage = DarkTheme.sDarkTheme.GetImage((radio ? .RadioOff : .Check));
		}

		if (action != null)
		{
			item.mOnMenuItemSelected.Add(action);
		}

		return item;
	}

	void ResizeComponents()
	{
		let font = DarkTheme.sDarkTheme.mSmallFont;
		let padding = GS!(20);
		float offset = 0;

		for (let btn in mButtons)
		{
			let width = font.GetWidth(btn.Label) + padding;
			btn.Resize(offset, 0, width, mHeight);
			offset += width;
		}
	}

	public override void Resize(float x, float y, float width, float height)
	{
		base.Resize(x, y, width, height);
		ResizeComponents();
	}

	public override void Draw(Graphics g)
	{
		using (g.PushColor(DarkTheme.COLOR_BKG))
		{
			g.FillRect(0, 0, mWidth, mHeight);
		}
	}
}
