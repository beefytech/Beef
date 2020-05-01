using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using Beefy.gfx;
using Beefy.events;

namespace Beefy.widgets
{
    public delegate void DialogEventHandler(DialogEvent theEvent);

	interface IHotKeyHandler
	{
		bool Handle(KeyCode keyCode);
	}

    public abstract class Dialog : Widget
    {
        public bool mInPopupWindow;
        public String mTitle ~ delete _;
        public String mText ~ delete _;
        public Image mIcon;
        public BFWindowBase.Flags mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Modal | .PopupPosition;
        public List<ButtonWidget> mButtons = new List<ButtonWidget>() ~ delete _;
        public List<Widget> mTabWidgets = new List<Widget>() ~ delete _;
        public Dictionary<ButtonWidget, DialogEventHandler> mHandlers = new Dictionary<ButtonWidget, DialogEventHandler>() ~ delete _;
        public ButtonWidget mDefaultButton;
        public ButtonWidget mEscButton;
        public EditWidget mDialogEditWidget;
        public bool mClosed;
        public Event<Action> mOnClosed ~ _.Dispose();

        static String STRING_OK = "Ok";
        static String STRING_CANCEL = "Cancel";
        static String STRING_YES = "Yes";
        static String STRING_NO = "No";
        static String STRING_CLOSE = "Close";

		public String Title
		{
			get
			{
				return mTitle;
			}

			set
			{
				String.NewOrSet!(mTitle, value);
				if (mWidgetWindow != null)
					mWidgetWindow.SetTitle(mTitle);
			}
		}

        public this(String title = null, String text = null, Image icon = null)
        {
			if (title != null)
            	mTitle = new String(title);
			if (text != null)
            	mText = new String(text);
            mIcon = icon;
        }

		public ~this()
		{			
            for (var handler in mHandlers.Values)
				delete handler;
		}

        public virtual void Close()
        {
            mClosed = true;
			if (mWidgetWindow == null)
				return;

            if (mInPopupWindow)
            {                                
                BFWindow lastChildWindow = null;
                if (mWidgetWindow.mParent.mChildWindows != null)
                {                    
                    for (var checkWindow in mWidgetWindow.mParent.mChildWindows)
                        if (checkWindow != mWidgetWindow)
                            lastChildWindow = checkWindow;
                    if (lastChildWindow != null)
                        lastChildWindow.SetForeground();                    
                }
                if (lastChildWindow == null)
                {
                    if (!mWidgetWindow.mParent.mHasClosed)
                        mWidgetWindow.mParent.SetForeground();
                }

				mWidgetWindow.mOnWindowKeyDown.Remove(scope => WindowKeyDown, true);
                mWidgetWindow.Close(true);
                
                mInPopupWindow = false;
                
                if (mOnClosed.HasListeners)
                    mOnClosed();
            }            
        }

        public virtual ButtonWidget CreateButton(String label)
        {
            return null;
        }

        public virtual EditWidget CreateEditWidget()
        {
            return null;
        }
        
        void HandleMouseClick(MouseEvent theEvent)
        {   
            ButtonWidget widget = (ButtonWidget) theEvent.mSender;

            DialogEvent dialogEvent = scope DialogEvent();
            dialogEvent.mButton = widget;
            dialogEvent.mCloseDialog = true;
            dialogEvent.mSender = this;

            if (mHandlers[widget] != null)
                mHandlers[widget](dialogEvent);
            
            if (dialogEvent.mCloseDialog)
                Close();
        }

        public virtual ButtonWidget AddButton(String label, DialogEventHandler handler = null)
        {
			Debug.AssertNotStack(handler);

            ButtonWidget button = CreateButton(label);
            mHandlers[button] = handler;
            button.mOnMouseClick.Add(new => HandleMouseClick);
            mButtons.Add(button);
            mTabWidgets.Add(button);
            AddWidget(button);
            return button;
        }

        public virtual EditWidget AddEdit(String text, DialogEventHandler handler = null)
        {
			Debug.AssertNotStack(handler);

            var editWidget = CreateEditWidget();            
            AddWidget(editWidget);
            if (text != null)
            {
                editWidget.SetText(text);
                editWidget.Content.SelectAll();
            }
            mTabWidgets.Add(editWidget);
			if (mDialogEditWidget == null)
            	mDialogEditWidget = editWidget;
            editWidget.mOnSubmit.Add(new => EditSubmitHandler);
            editWidget.mOnCancel.Add(new => EditCancelHandler);
            return editWidget;
        }

		public virtual EditWidget AddEdit(EditWidget editWidget, DialogEventHandler handler = null)
		{
			Debug.AssertNotStack(handler);
			if (editWidget.mParent == null)
				AddWidget(editWidget);
		    mTabWidgets.Add(editWidget);
			if (mDialogEditWidget == null)
		    	mDialogEditWidget = editWidget;
		    editWidget.mOnSubmit.Add(new => EditSubmitHandler);
		    editWidget.mOnCancel.Add(new => EditCancelHandler);
		    return editWidget;
		}

		public void AddDialogComponent(Widget widget)
		{
			mTabWidgets.Add(widget);
			AddWidget(widget);
		}

        protected void EditSubmitHandler(EditEvent theEvent)
        {
            Submit();
        }

        protected void EditCancelHandler(EditEvent theEvent)
        {
            if (mEscButton != null)
                mEscButton.MouseClicked(0, 0, 0, 0, 3);
        }

        public virtual void AddCloseButton(DialogEventHandler closeHandler, int32 theDefault = -1)
        {
			Debug.AssertNotStack(closeHandler);

#unwarn
            ButtonWidget closeButton = AddButton(STRING_CLOSE, closeHandler);
        }

        public virtual void AddYesNoButtons(DialogEventHandler yesHandler = null, DialogEventHandler noHandler = null, int32 theDefault = -1, int32 escapeBtn = -1)
        {
			Debug.AssertNotStack(yesHandler);
			Debug.AssertNotStack(noHandler);

            ButtonWidget yesButton = AddButton(STRING_YES, yesHandler);
            ButtonWidget noButton = AddButton(STRING_NO, noHandler);

            mDefaultButton = (theDefault == 0) ? yesButton : (theDefault == 1) ? noButton : null;
            mEscButton = (escapeBtn == 0) ? yesButton : (escapeBtn == 1) ? noButton : null;
        }

        public virtual void AddOkCancelButtons(DialogEventHandler okHandler = null, DialogEventHandler cancelHandler = null, int32 theDefault = -1, int32 escapeBtn = -1)
        {
			Debug.AssertNotStack(okHandler);
			Debug.AssertNotStack(cancelHandler);

            ButtonWidget okButton = AddButton(STRING_OK, okHandler);
            ButtonWidget cancelButton = AddButton(STRING_CANCEL, cancelHandler);

            mDefaultButton = (theDefault == 0) ? okButton : (theDefault == 1) ? cancelButton : null;
            mEscButton = (escapeBtn == 0) ? okButton : (escapeBtn == 1) ? cancelButton : null;
        }

        public virtual void PreShow()
        {
        }

        public virtual void CalcSize()
        {
        }

        public virtual void ResizeComponents()
        {
            
        }

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

        public override void AddedToParent()
        {
            if (mDialogEditWidget != null)
                mDialogEditWidget.SetFocus();
            else if (mDefaultButton != null)
                mDefaultButton.SetFocus();
            else
                SetFocus();
        }  

		public virtual void WindowCreated()
		{

		}

        public virtual void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
        {
            if (mClosed)
                return;

            mInPopupWindow = true;

            CalcSize();
            ResizeComponents();

			int desktopWidth;
			int desktopHeight;
			BFApp.sApp.GetDesktopResolution(out desktopWidth, out desktopHeight);

            //TODO: Clip to desktop width / height
            mWidth = Math.Min(desktopWidth - 32, mWidth);
            mHeight = Math.Min(desktopHeight - 32, mHeight);

            float aX = parentWindow.mClientX + (parentWindow.mClientWidth - mWidth) / 2 + offsetX;
            float aY = parentWindow.mClientY + (parentWindow.mClientHeight - mHeight) / 2 + offsetY;

            for (int32 i = 0; i < parentWindow.mChildWindows.Count; i++)
            {
                var otherChild = parentWindow.mChildWindows[i];

                if (otherChild.mWindowFlags.HasFlag(BFWindowBase.Flags.Modal))
                {
                    aX = Math.Max(aX, otherChild.mClientX + 16);
                    aY = Math.Max(aY, otherChild.mClientY + 16);
                }                
            }

            WidgetWindow widgetWindow = new WidgetWindow(parentWindow,
                mTitle ?? "Dialog",
                (int32)(aX), (int32)(aY),
                (int32)mWidth, (int32)mHeight,
                mWindowFlags,
                this);

			WindowCloseQueryHandler windowCloseHandler = new (evt) =>
                {
                    Close();
                    return false;
                };

            widgetWindow.mOnWindowCloseQuery.Add(windowCloseHandler);
            widgetWindow.mOnWindowKeyDown.Add(new => WindowKeyDown);
			WindowCreated();
        }

        public virtual bool HandleTab(int dir)
        {
            return Widget.HandleTab(dir, mTabWidgets);
        }

		public virtual void Escape()
		{
			mEscButton.MouseClicked(0, 0, 0, 0, 3);
		}

		public virtual void Submit()
		{
			if ((mDefaultButton != null) && (!mDefaultButton.mDisabled))
				mDefaultButton.MouseClicked(0, 0, 0, 0, 3);
		}

        void WindowKeyDown(KeyDownEvent evt)
        {
			if ((evt.mKeyCode != .Alt) && (mWidgetWindow.IsKeyDown(.Alt)) && (!mWidgetWindow.IsKeyDown(.Control)))
			{
				if (mChildWidgets != null)
				{
					for (var widget in mChildWidgets)
					{
						if (let hotKeyHandler = widget as IHotKeyHandler)
						{
							if (hotKeyHandler.Handle(evt.mKeyCode))
							{
								evt.mHandled = true;
								return;
							}
						}
					}
				}
			}

			if (evt.mKeyCode == .Escape)
			{
				if (mWidgetWindow.mFocusWidget != null)
				{
					let widgetWindow = mWidgetWindow;

					let focusWidget = widgetWindow.mFocusWidget;
					widgetWindow.mFocusWidget.KeyDown(evt);
					if (focusWidget != widgetWindow.mFocusWidget)
						evt.mHandled = true; // Infers we handled the event
					if (evt.mHandled)
						return;
					evt.mHandled = true;
				}

	            if (mEscButton != null)
	            {
	                if ((mDefaultButton != null) && (mDefaultButton.mMouseDown))
	                {
	                    mDefaultButton.mMouseFlags = default;
	                    mDefaultButton.mMouseDown = false;
	                }
	                else
						Escape();
	                evt.mHandled = true;
	            }
			}

			if ((evt.mKeyCode == .Return) && (mDefaultButton != null))
			{
				if ((!(mWidgetWindow.mFocusWidget is EditWidget)) &&
					(!(mWidgetWindow.mFocusWidget is ButtonWidget)))
				{
					Submit();
				}
			}

            if (evt.mKeyCode == KeyCode.Tab)
            {
                int32 dir = mWidgetWindow.IsKeyDown(KeyCode.Shift) ? -1 : 1;
                if (HandleTab(dir))
				{
					evt.mHandled = true;
				}
            }
        }
    }
}
