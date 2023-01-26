using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using Beefy.theme;
using Beefy.events;
using Beefy.sys;
using Beefy.gfx;
using Beefy.geom;

namespace Beefy.widgets
{
    public class MenuItemWidget : Widget
    {
        public Menu mMenuItem;
        public MenuWidget mMenuWidget;
        public int32 mIndex;
        public bool mIsSelected;
        public MenuWidget mSubMenu;

        public this(Menu menuItem)
        {
            mMenuItem = menuItem;
			if (mMenuItem.mWidget != null)
				AddWidget(mMenuItem.mWidget);
        }

		public ~this()
		{
			
		}

		protected override void RemovedFromWindow()
		{
			base.RemovedFromWindow();
			if (mMenuItem.mWidget != null)
				RemoveWidget(mMenuItem.mWidget);
		}

        public override void MouseEnter()
        {
            base.MouseEnter();
            if ((!mWidgetWindow.mIsMouseMoving) || (mUpdateCnt == 0))
            {
                // We can get a MouseEnter from the widget moving to under the mouse, but we 
                //  only want to respond to the mouse actually moving
                return;
            }
            if (mMenuItem.mLabel != null)
            {
				mMenuWidget.SetSelection(mIndex);

				if (!mWidgetWindow.mHasFocus)
				{
					var parentMenuItemWidget = mMenuWidget.mParentMenuItemWidget;
					while (parentMenuItemWidget != null)
					{
						parentMenuItemWidget.mMenuWidget.mOpeningSubMenu = true;
						parentMenuItemWidget = parentMenuItemWidget.mMenuWidget.mParentMenuItemWidget;
					}

					mWidgetWindow.SetForeground();

					parentMenuItemWidget = mMenuWidget.mParentMenuItemWidget;
					while (parentMenuItemWidget != null)
					{
						parentMenuItemWidget.mMenuWidget.mOpeningSubMenu = false;
						parentMenuItemWidget = parentMenuItemWidget.mMenuWidget.mParentMenuItemWidget;
					}
				}
			}
        }

        public override void MouseLeave()
        {
            base.MouseLeave();
            if ((mWidgetWindow == null) || (!mWidgetWindow.mIsMouseMoving))
                return;
            if (mSubMenu == null)
                mMenuWidget.SetSelection(-1);
        }
                
        public override void MouseUp(float x, float y, int32 btn)
        {
            base.MouseUp(x, y, btn);
            //if ((mIsOver) && (mMouseClickHandler != null))
                //mMouseClickHandler(this);

            if (!mMouseOver)
                mMenuWidget.Close();
        }

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			ReleaseMouseCapture();
		}

        public virtual void Submit()
        {

        }
    }

    public class MenuContainer : ScrollableWidget
    {
        public bool mReverse;
    }

    public class MenuWidget : Widget
    {
		enum ShowFlags
		{
			None,
			AllowScrollable = 1,
			CenterHorz = 2,
			CenterVert = 4,
		}

        public MenuItemWidget mParentMenuItemWidget;
        public Menu mMenu;        
        //public BFWindowBase.Flags mWindowFlags = BFWindowBase.Flags.ClientSized | BFWindowBase.Flags.NoActivate | BFWindowBase.Flags.NoMouseActivate;
		public BFWindowBase.Flags mWindowFlags = .ClientSized | .FakeFocus | .PopupPosition;
        public int32 mSelectIdx = -1;
        public Widget mRelativeWidget;
        public List<MenuItemWidget> mItemWidgets = new List<MenuItemWidget>() ~ delete _;
        public Menu mItemSelected;
		public bool mOpeningSubMenu;
        public bool mReplacing;
		public bool mReverse;
        public float mMinContainerWidth = 0;
        public float mMaxContainerWidth = Int32.MaxValue;
        public Insets mPopupInsets = new Insets() ~ delete _;
		public bool mHasClosed;
		public Event<delegate void(int)> mOnSelectionChanged ~ _.Dispose();
		public bool mWasInitialized;

        public this(Menu menu)
        {
            mMenu = menu;
        }

		public ~this()
		{
			// Only delete when we're the top-level menu
			if (mMenu.mParent == null)
			{
				delete mMenu;
			}

			if ((!mHasClosed) && (mWasInitialized))
				Close();

			//Debug only
			/*for (var call in WidgetWindow.sWindowMovedHandler.GetInvocationList())
			{
				if ((int)call.GetTarget() == (int)(void*)this)
				{
					Runtime.FatalError("MenuWidget not removed from sWindowMovedHandler!");
				}
			}*/
		}

        public virtual MenuContainer CreateMenuContainer()
        {
            return new MenuContainer();
        }

        public virtual void ResizeComponents()
        {
        }

        public virtual void CalcSize()
        {
        }        

        public virtual MenuItemWidget CreateMenuItemWidget(Menu menuItem)
        {
            return new MenuItemWidget(menuItem);
        }

        public virtual void EnsureItemVisible(int itemIdx)
        {

        }

        public virtual void SetSelection(int selIdx)
        {
			MarkDirty();
            EnsureItemVisible(selIdx);
            mSelectIdx = (int32)selIdx;            
            if ((mParentMenuItemWidget != null) && (selIdx != -1))
                mParentMenuItemWidget.mMenuWidget.SetSelection(mParentMenuItemWidget.mIndex);

			mOnSelectionChanged(selIdx);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
        }

        public virtual void CalcContainerSize(float x, float y, MenuContainer menuContainer, bool allowScrollable, ref float screenX, ref float screenY, out float width, out float height)
        {
            Rect menuRect = menuContainer.CalcRectFromContent();
            width = Math.Min(mMaxContainerWidth, Math.Max(mMinContainerWidth, menuRect.mWidth));
            height = menuRect.mHeight;

            int workspaceX;
            int workspaceY;
            int workspaceWidth;
            int workspaceHeight;
            BFApp.sApp.GetWorkspaceRectFrom((.)x, (.)y, 0, 0, out workspaceX, out workspaceY, out workspaceWidth, out workspaceHeight);

			float maxY = workspaceY + workspaceHeight;

            if ((!allowScrollable) && (screenX + width > workspaceWidth))
                screenX = screenX - width + mPopupInsets.mRight;
            else
                screenX -= mPopupInsets.mLeft;

			if (mReverse)
			{
				maxY = screenY;
				screenY = Math.Max(workspaceY, screenY - height);
			}

            if ((!allowScrollable) && (screenY + height > maxY))
                screenY = screenY - height + mPopupInsets.mBottom;
            else
            {
                screenY -= mPopupInsets.mTop;

                if (screenY + height > maxY)
                {
                    menuContainer.InitScrollbars(false, true);
                    height = maxY - screenY;
                }                
                else
                    menuContainer.InitScrollbars(false, false);

                menuRect = menuContainer.CalcRectFromContent();
                width = Math.Min(mMaxContainerWidth, Math.Max(mMinContainerWidth, menuRect.mWidth));                
            }
        }

		public virtual float GetReverseAdjust()
		{
			return 10;
		}

        public virtual void Init(Widget relativeWidget, float x, float y, ShowFlags showFlags = 0, WidgetWindow widgetWindow = null)
        {
			bool allowScrollable = showFlags.HasFlag(.AllowScrollable);
			mWasInitialized = true;

			float curY = y;
			WidgetWindow curWidgetWindow = widgetWindow;

			bool reverse = false;
			bool allowReverse = true;
			if ((allowReverse) && (relativeWidget != null))
			{
				if (mHeight == 0)
					CalcSize();

				float minHeight;
				if (allowScrollable)
                	minHeight = Math.Min(mHeight, 200) + 8;
				else
					minHeight = mHeight;

				float screenX;
				float screenY;
				relativeWidget.SelfToRootTranslate(0, curY, out screenX, out screenY);
				screenX += relativeWidget.mWidgetWindow.mClientX;
				screenY += relativeWidget.mWidgetWindow.mClientY;

				int wsX;
				int wsY;
				int wsWidth;
				int wsHeight;
				BFApp.sApp.GetWorkspaceRectFrom((.)screenX, (.)screenY, 0, 0, out wsX, out wsY, out wsWidth, out wsHeight);
				float spaceLeft = (wsY + wsHeight) - (screenY);
				if (spaceLeft < minHeight)
				{
					curY += GetReverseAdjust();
					reverse = true;
				}
			}

         	mReverse = reverse;

            int32 idx = 0;
            for (Menu item in mMenu.mItems)
            {
                MenuItemWidget itemWidget = CreateMenuItemWidget(item);
                itemWidget.mIndex = idx;
                itemWidget.mMenuWidget = this;                
                mItemWidgets.Add(itemWidget);
                AddWidget(itemWidget);
                ++idx;
            }

            float screenX = x;
            float screenY = curY;

            CalcSize();            

            if (relativeWidget != null)
                relativeWidget.SelfToRootTranslate(x, curY, out screenX, out screenY);
            screenX += relativeWidget.mWidgetWindow.mClientX;
            screenY += relativeWidget.mWidgetWindow.mClientY;

			if (showFlags.HasFlag(.CenterHorz))
				screenX -= mWidth / 2;
			if (showFlags.HasFlag(.CenterVert))
				screenY -= mHeight / 2;

            MenuContainer menuContainer;

            if (curWidgetWindow == null)
            {
                menuContainer = CreateMenuContainer();                                
				menuContainer.mReverse = reverse;
                menuContainer.mScrollContent = this;
                menuContainer.mScrollContentContainer.AddWidget(this);                

                float screenWidth;
                float screenHeight;
                CalcContainerSize(screenX, screenY, menuContainer, allowScrollable, ref screenX, ref screenY, out screenWidth, out screenHeight);

				screenWidth = Math.Max(screenWidth, 32);
				screenHeight = Math.Max(screenHeight, 32);

				WidgetWindow parentWindow = (relativeWidget != null) ? relativeWidget.mWidgetWindow : null;
                curWidgetWindow = new WidgetWindow(parentWindow,
                    "Popup",
                    (int32)(screenX), (int32)(screenY),
                    (int32)screenWidth, (int32)screenHeight,
                    mWindowFlags,
                    menuContainer);
				
				if (parentWindow != null)
				{
					parentWindow.TransferMouse(curWidgetWindow);
				}

                //Resize(0, 0, mWidth, mHeight);
                menuContainer.UpdateScrollbars();
                mRelativeWidget = relativeWidget;                
            }
            else
            {
                menuContainer = (MenuContainer)curWidgetWindow.mRootWidget;
				menuContainer.mReverse = reverse;
                var oldMenuWidget = (MenuWidget)menuContainer.mScrollContent;
                oldMenuWidget.mReplacing = true;
                oldMenuWidget.RemoveSelf();
				oldMenuWidget.Close();
				delete oldMenuWidget;
                menuContainer.mScrollContent = this;
                menuContainer.AddWidget(this);

                float screenWidth;
                float screenHeight;
                CalcContainerSize(x, y, menuContainer, allowScrollable, ref screenX, ref screenY, out screenWidth, out screenHeight);

                curWidgetWindow.Resize((int32)(screenX), (int32)(screenY),
                    (int32)screenWidth, (int32)screenHeight);
            }

            menuContainer.mScrollContent.mWidth = menuContainer.mScrollContentContainer.mWidth;
            ResizeComponents();

            WidgetWindow.sOnKeyDown.Add(new => HandleKeyDown);
            WidgetWindow.sOnWindowLostFocus.Add(new => WindowLostFocus);
            WidgetWindow.sOnMouseDown.Add(new => HandleMouseDown);
            WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
            WidgetWindow.sOnWindowMoved.Add(new => HandleWindowMoved);
            WidgetWindow.sOnMenuItemSelected.Add(new => HandleSysMenuItemSelected);

            if (mRelativeWidget != null)
            {
				mRelativeWidget.mOnRemovedFromParent.Add(new => HandleRemovedFromManager);
				mRelativeWidget.mOnMouseUp.Add(new => HandleMouseUp);
			}

            curWidgetWindow.SetFocus(this);
              
        }

        public void SubmitSelection()
        {
			if (mSelectIdx == -1)
				return;
            //mItemWidgets[mSelectIdx].MouseClicked(0, 0, 0);
            mItemWidgets[mSelectIdx].Submit();
        }

		void HandleRemovedFromManager(Widget widget, Widget prevParent, WidgetWindow widgetWindow)
		{
			Close();
		}

        void HandleMouseUp(MouseEvent theEvent)
        {
            if ((mSelectIdx != -1) && (theEvent.mBtn == 0))
            {
                //SubmitSelection();
            }
        }

        void HandleMouseWheel(MouseEvent theEvent)
        {
            HandleMouseDown(theEvent);
        }

        void HandleMouseDown(MouseEvent theEvent)
        {            
            WidgetWindow widgetWindow = (WidgetWindow)theEvent.mSender;            
            if (!(widgetWindow.mRootWidget is MenuContainer))
                Close();
        }

        void HandleSysMenuItemSelected(IMenu sysMenu)
        {
            Close();
        }

        bool IsMenuWindow(BFWindow window)
        {
            var newWidgetWindow = window as WidgetWindow;
            if (newWidgetWindow != null)
            {
                if (newWidgetWindow.mRootWidget is MenuContainer)
                    return true;
            }
            return false;
        }

        void WindowLostFocus(BFWindow window, BFWindow newFocus)
        {
			if (mOpeningSubMenu)
				return;
            if (IsMenuWindow(newFocus))
                return;

            if ((mWidgetWindow != null) && (!mWidgetWindow.mHasFocus))
                Close();
        }

        void HandleKeyDown(KeyDownEvent evt)
        {
            if (evt.mKeyCode == KeyCode.Escape)
            {
                evt.mHandled = true;
                Close();
            }
        }

        void HandleWindowMoved(BFWindow window)
        {
            if (IsMenuWindow(window))
                return;
            Close();
        }

        public void RemoveHandlers()
        {
            WidgetWindow.sOnMouseDown.Remove(scope => HandleMouseDown, true);
            WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
            WidgetWindow.sOnWindowLostFocus.Remove(scope => WindowLostFocus, true);
            WidgetWindow.sOnWindowMoved.Remove(scope => HandleWindowMoved, true);
            WidgetWindow.sOnMenuItemSelected.Remove(scope => HandleSysMenuItemSelected, true);
            WidgetWindow.sOnKeyDown.Remove(scope => HandleKeyDown, true);

            if (mRelativeWidget != null)
			{
				mRelativeWidget.mOnRemovedFromParent.Remove(scope => HandleRemovedFromManager, true);
                mRelativeWidget.mOnMouseUp.Remove(scope => HandleMouseUp, true);
			}
        }

        public void Close()
        {
			if (mHasClosed)
				return;
			mHasClosed = true;

            RemoveHandlers();

			//Debug.Assert(mWidgetWindow != null);
            
			if (mWidgetWindow != null)
			{
	            mWidgetWindow.Close();            
	            if (mMenu.mOnMenuClosed.HasListeners)
	                mMenu.mOnMenuClosed(mMenu, mItemSelected);
			}
        }

        public override void LostFocus()
        {
            base.LostFocus();
			if (mOpeningSubMenu)
				return;
            if (!mReplacing)
            {
                Close();
            }
            else
            {
                RemoveHandlers();
            }
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);

            if (mItemWidgets.Count > 0)
            {
                int32 itemsPerPage = (int32)Math.Ceiling((mParent.mHeight - 8) / mItemWidgets[0].mHeight) - 1;
                switch (keyCode)
                {
                case .Home:
                    SetSelection(0);
                    break;
                case .End:
                    SetSelection(mItemWidgets.Count - 1);
                    break;
                case .PageUp:                        
                    SetSelection(Math.Max(0, mSelectIdx - itemsPerPage));
                    break;
                case .PageDown:                        
                    SetSelection(Math.Min(mItemWidgets.Count - 1, Math.Max(0, mSelectIdx) + itemsPerPage));
                    break;
                case .Up:
                    if (mSelectIdx == -1)
                    {
                        SetSelection(0);
                        break;
                    }
					else if ((mSelectIdx == 0) && (!mItemWidgets.IsEmpty))
						SetSelection(mItemWidgets.Count - 1);
                    else if (mSelectIdx > 0)
                        SetSelection(mSelectIdx - 1);
                    break;
                case .Down:
                    if (mSelectIdx == -1)
                    {
                        SetSelection(0);
                        break;
                    }
                    else if (mSelectIdx < mItemWidgets.Count - 1)
                        SetSelection(mSelectIdx + 1);
					else
						SetSelection(0);
                    break;
				case .Return:
					SubmitSelection();
				case .Right:
					if (mItemSelected != null)
					{
						if (!mItemSelected.mItems.IsEmpty)
						{
							
						}
					}
				default:
                }
            }
        }
    }

    public class Menu : IMenu
    {
        public Menu mParent;
        public String mLabel ~ delete _;
		public String mTooltip ~ delete _;
        public List<Menu> mItems = new List<Menu>() ~ DeleteContainerAndItems!(_);
        
        public Event<delegate void(Menu menu)> mOnMenuItemSelected ~ _.Dispose();
        public Event<delegate void(Menu menu)> mOnMenuItemUpdate ~ _.Dispose();
        public Event<delegate void(Menu menu, Menu itemSelected)> mOnMenuClosed ~ _.Dispose();
		public Event<delegate void(Menu menu)> mOnChildOpen ~ _.Dispose();
        public Object mThemeData;
        public bool mDisabled;
		public bool mBold;
        public IDrawable mIconImage;
		public bool mForceParent;
		public Widget mWidget ~ delete _;

		public ~this()
		{
			
		}

		public bool IsEmpty
		{
			get
			{
				return mItems.IsEmpty;
			}
		}

		public bool IsParent
		{
			get
			{
				return !mItems.IsEmpty || mForceParent;
			}

			set
			{
				mForceParent = value;
			}
		}

        public virtual Menu AddItem(StringView label = default)
        {
            Menu item = new Menu();
            
            item.mParent = this;
			if (!label.IsEmpty)
            	item.mLabel = new String(label);
            mItems.Add(item);
            return item;
        }

		public virtual Menu AddWidgetItem(Widget widget)
		{
			Menu item = new Menu();
			item.mParent = this;
			item.mWidget = widget;
			mItems.Add(item);
			return item;
		}

		public void SetDisabled(bool disabled)
		{
			mDisabled = disabled;
		}

		public void SetCheckState(int32 checkState)
		{
			
		}
    }
}
