using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy;
using System.Diagnostics;

namespace Beefy.sys
{    
    public class SysMenu : IMenu
    {        
        public String mText ~ delete _;
		public String mHotKey ~ delete _;
		public SysBitmap mBitmap;
		public bool mEnabled;
		public int32 mCheckState;
		public bool mRadioCheck;

        public SysMenu mParent;
        public BFWindow mWindow;
        public void* mNativeBFMenu;
        public Event<MenuItemSelectedHandler> mOnMenuItemSelected ~ _.Dispose();
        public Event<MenuItemUpdateHandler> mOnMenuItemUpdate ~ _.Dispose();
        public List<SysMenu> mChildren ~ DeleteContainerAndItems!(_);

		public int ChildCount
		{
			get
			{
				return (mChildren != null) ? mChildren.Count : 0;
			}
		}

        public virtual SysMenu AddMenuItem(String text, String hotKey = null, MenuItemSelectedHandler menuItemSelectedHandler = null, MenuItemUpdateHandler menuItemUpdateHandler = null,
            SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
        {            
            if (mChildren == null)
                mChildren = new List<SysMenu>();

            SysMenu sysMenu = new SysMenu();
			if (text != null)
            	sysMenu.mText = new String(text);
			if (hotKey != null)
				sysMenu.mHotKey = new String(hotKey);
			sysMenu.mBitmap = bitmap;
			sysMenu.mEnabled = enabled;
			sysMenu.mCheckState = checkState;
			sysMenu.mRadioCheck = radioCheck;
			if (menuItemSelectedHandler != null)
            	sysMenu.mOnMenuItemSelected.Add(menuItemSelectedHandler);
			if (menuItemUpdateHandler != null)
            	sysMenu.mOnMenuItemUpdate.Add(menuItemUpdateHandler);
            sysMenu.mNativeBFMenu = mWindow.AddMenuItem(mNativeBFMenu, mChildren.Count, text, hotKey, (bitmap != null) ? bitmap.mNativeBFBitmap : null, enabled, checkState, radioCheck);
            sysMenu.mParent = this;
            sysMenu.mWindow = mWindow;

            mWindow.mSysMenuMap[(int)sysMenu.mNativeBFMenu ] = sysMenu;
            mChildren.Add(sysMenu);

            return sysMenu;
        }

		public void SetDisabled(bool disabled)
		{
			mEnabled = !disabled;
			Modify(mText, mHotKey, mBitmap, mEnabled, mCheckState, mRadioCheck);
		}

		public void SetCheckState(int32 checkState)
		{
			mCheckState = checkState;
			Modify(mText, mHotKey, mBitmap, mEnabled, mCheckState, mRadioCheck);
		}

		public void SetHotKey(StringView hotKey)
		{
			if (hotKey.IsNull)
			{
				DeleteAndNullify!(mHotKey);
			}
			else
			{
				if (mHotKey == null)
					mHotKey = new String(hotKey);
				else
					mHotKey.Set(hotKey);
			}
			UpdateChanges();
		}

		public void UpdateChanges()
		{
			mWindow.ModifyMenuItem(mNativeBFMenu, mText, mHotKey, (mBitmap != null) ? mBitmap.mNativeBFBitmap : null, mEnabled, mCheckState, mRadioCheck);
		}

        public virtual void Modify(String text, String hotKey = null, SysBitmap bitmap = null, bool enabled = true, int32 checkState = -1, bool radioCheck = false)
        {
			if ((Object)mText != text)
			{
				if (text != null)
				{
					if (mText == null)
						mText = new String(text);
					else
						mText.Set(text);
				}
				else
					DeleteAndNullify!(mText);
			}

			if ((Object)mHotKey != hotKey)
			{
				if (hotKey != null)
				{
					if (mHotKey == null)
						mHotKey = new String(hotKey);
					else
						mHotKey.Set(hotKey);
				}
				else
					DeleteAndNullify!(mHotKey);
			}
			mBitmap = bitmap;
			mEnabled = enabled;
			mCheckState = checkState;
			mRadioCheck = radioCheck;

            UpdateChanges();
        }

        public virtual void Dispose()
        {
            mWindow.mSysMenuMap.Remove((int)mNativeBFMenu);
            mWindow.DeleteMenuItem(mNativeBFMenu);
			if (mParent != null)
				mParent.mChildren.Remove(this);
        }

		public void UpdateChildItems()
		{
			if (mChildren != null)
			{
				for (SysMenu child in mChildren)
					child.mOnMenuItemUpdate(child);
			}
		}

        public virtual void Selected()
        {
            mOnMenuItemSelected(this);
            UpdateChildItems();
        }
    }
}
