using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.events;

namespace Beefy.theme.dark
{
    public class DarkComboBox : ComboBox
    {
		public class CBMenuWidget : DarkMenuWidget
		{
			public this(Menu menu) :
			    base(menu)
			{
			}

			public override float GetReverseAdjust()
			{
				return GS!(-10);
			}
		}

        /*public string Label
        {
            get
            {
                if (mEditWidget == null)
                    return mLabel;
                else
                    return mEditWidget.Text;
            }

            set
            {
                if (mEditWidget == null)
                    mLabel = value;
                else
                    mEditWidget.Text = value;
            }
        }*/

		public enum FrameKind
		{
			OnWindow,
			Frameless,
			Transparent
		}

        String mLabel ~ delete _;
        public float mLabelX = GS!(8);
        public FontAlign mLabelAlign = FontAlign.Centered;
        public FrameKind mFrameKind = .OnWindow;

        public Event<delegate void(Menu)> mPopulateMenuAction ~ _.Dispose();
        public CBMenuWidget mCurMenuWidget;
        bool mJustClosed;
        public uint32 mBkgColor;
        public DarkEditWidget mEditWidget;
		bool mAllowReverseDropdown; // Allow popdown to "popup" if there isn't enough space
		public Widget mPrevFocusWidget;
		public bool mFocusDropdown = true;
		
		virtual public StringView Label 
        { 
            get
			{
				if (mEditWidget != null)
				{
					if (mLabel == null)
						mLabel = new String();
					mLabel.Clear();
					mEditWidget.GetText(mLabel);
				}
				return mLabel;
			}

            set
			{
				String.NewOrSet!(mLabel, value);
				if (mEditWidget != null)
					mEditWidget.SetText(mLabel);
			} 
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);
            
            float yOfs = 0;
            if (mEditWidget != null)
            {
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(.EditBox), 0, 0, mWidth, mHeight);
                g.Draw(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.ComboEnd], mWidth - GS!(25), (mHeight - GS!(22)) / 2 + yOfs);

                if ((mHasFocus) || (mEditWidget.mHasFocus))
                {
                    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
                        g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Outline), 0, 0, mWidth, mHeight);
                }

                return;
            }

            if ((mFrameKind == .OnWindow) || (mFrameKind == .Frameless))
            {
                Image texture = DarkTheme.sDarkTheme.mImages[(mFrameKind == .OnWindow) ? (int32)DarkTheme.ImageIdx.ComboBox : (int32)DarkTheme.ImageIdx.ComboBoxFrameless];
                g.DrawBox(texture, 0, GS!(-2), mWidth, mHeight);
            }
            else
            {
                if (mBkgColor != 0)
                {
                    using (g.PushColor(mBkgColor))
                        g.FillRect(0, 0, mWidth, mHeight);
                }
                yOfs = 2.0f;
            }

            if (mEditWidget == null)
            {
                using (g.PushColor(mDisabled ? 0x80FFFFFF : Color.White))
                {
                    g.Draw(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.ComboEnd], mWidth - GS!(25), (mHeight - GS!(24)) / 2 + yOfs);

                    g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
					String label = scope String();
					GetLabel(label);
                    if (label != null)
                    {
						float fontHeight = g.mFont.GetHeight();

                        //g.DrawString(label, mLabelX, (mHeight - GS!(24)) / 2, mLabelAlign, mWidth - mLabelX - GS!(24), FontOverflowMode.Ellipsis);
						using (g.PushColor(DarkTheme.COLOR_TEXT))
							g.DrawString(label, mLabelX, (mHeight - fontHeight) / 2 - (int)GS!(3.5f), mLabelAlign, mWidth - mLabelX - GS!(24), FontOverflowMode.Ellipsis);
                    }
                }

				if (mHasFocus)
				{
				    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
				        g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Outline), GS!(2), 0, mWidth - GS!(4), mHeight - GS!(4));
				}
            }

			/*using (g.PushColor(0x1FFF0000))
				g.FillRect(0, 0, mWidth, mHeight);*/
        }

		public void GetLabel(String label)
		{
			if (mEditWidget == null)
			{
				if (mLabel != null)
                	label.Append(mLabel);
			}
			else
			    mEditWidget.GetText(label);
		}

        void HandleKeyDown(KeyDownEvent evt)
        {
            if (evt.mKeyCode == KeyCode.Escape)
            {
                evt.mHandled = true;
                mCurMenuWidget.Close();
            }
        }

        public virtual void MenuClosed()
        {
			if (mPrevFocusWidget != null)
			{
				mPrevFocusWidget.SetFocus();
			}
        }

        void HandleClose(Menu menu, Menu selectedItem)
        {
            mCurMenuWidget = null;
            mJustClosed = true;
            //mWidgetWindow.mWindowKeyDownDelegate -= HandleKeyDown;
            MenuClosed();
        }

        public virtual MenuWidget ShowDropdown()
        {
			mPrevFocusWidget = mWidgetWindow.mFocusWidget;

            float popupXOfs = GS!(5);
            float popupYOfs = GS!(-2);
            if (mEditWidget != null)
            {
                popupXOfs = GS!(2);
                popupYOfs = GS!(2);
            }
            else if (mFrameKind == .Transparent)
            {
                popupXOfs = GS!(2);
                popupYOfs = GS!(2);
            }

			//if (mCurMenuWidget != null)
				//mCurMenuWidget.Close();

            mAutoFocus = false;

            Menu aMenu = new Menu();
            mPopulateMenuAction(aMenu);

            WidgetWindow menuWindow = null;
            if (mCurMenuWidget != null)
                menuWindow = mCurMenuWidget.mWidgetWindow;

            let menuWidget = new CBMenuWidget(aMenu);
            //menuWidget.SetShowPct(0.0f);
            //menuWidget.mWindowFlags &= ~(BFWindow.Flags.PopupPosition);
            mCurMenuWidget = menuWidget;
            aMenu.mOnMenuClosed.Add(new => HandleClose);
            menuWidget.mMinContainerWidth = mWidth + GS!(8);
            menuWidget.mMaxContainerWidth = Math.Max(menuWidget.mMinContainerWidth, 1536);
			menuWidget.mWindowFlags = .ClientSized | .DestAlpha | .FakeFocus;
			if (!mFocusDropdown)
				menuWidget.mWindowFlags |= .NoActivate;
			menuWidget.CalcSize();

			float popupY = mHeight + popupYOfs;
			//TODO: Autocomplete didn't work on this: BFApp.sApp.GetW...
            menuWidget.Init(this, popupXOfs, popupY, .AllowScrollable, menuWindow);
			// Why were we capturing?
			mWidgetWindow.TransferMouse(menuWidget.mWidgetWindow);
            if (menuWindow == null)
                menuWidget.SetShowPct(0.0f);
			return menuWidget;
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);

            if (mDisabled)
                return;

            if ((mCurMenuWidget == null) && (!mJustClosed))
            {
				if (mEditWidget != null)
					SetFocus();
                ShowDropdown();

				//mCurMenuWidget.SetFocus();

                //mWidgetWindow.mWindowKeyDownDelegate += HandleKeyDown;
            }
        }

        public override void SetFocus()
        {
            if (mEditWidget != null)
            {
                mEditWidget.SetFocus();
                return;
            }
            base.SetFocus();
        }

        public override void Update()
        {
            base.Update();
            mJustClosed = false;

			bool hasFocus = mHasFocus;
			if ((mEditWidget != null) && (mEditWidget.mHasFocus))
				hasFocus = true;
			if ((!hasFocus) && (mCurMenuWidget != null))
				if (mCurMenuWidget.mWidgetWindow.mHasFocus)
					hasFocus = true;
			if ((!hasFocus) && (mCurMenuWidget != null))
			{
				mCurMenuWidget.SubmitSelection();
				if (mCurMenuWidget != null)
					mCurMenuWidget.Close();
			}
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        private void ResizeComponents()
        {
            if (mEditWidget != null)
            {
                mEditWidget.Resize(0, 0, Math.Max(mWidth - GS!(24), 0), mHeight);                
            }
        }

		public virtual bool WantsKeyHandling()
		{
			return true;
		}	

		void EditKeyDownHandler(KeyDownEvent evt)
		{
			if (!WantsKeyHandling())
				return;

			if ((evt.mKeyCode == .Up) || (evt.mKeyCode == .Down) || 
			    (evt.mKeyCode == .PageUp) || (evt.mKeyCode == .PageDown) ||
			    (evt.mKeyCode == .Home) || (evt.mKeyCode == .End))
			{
			    if (mCurMenuWidget != null)
			        mCurMenuWidget.KeyDown(evt.mKeyCode, false);
			}

			if ((evt.mKeyCode == .Down) && (mCurMenuWidget == null))
			{
				ShowDropdown();

				var label = Label;
				for (let itemWidget in mCurMenuWidget.mItemWidgets)
				{
					if (itemWidget.mMenuItem.mLabel == label)
						mCurMenuWidget.SetSelection(@itemWidget.Index);
				}
			}

			if ((evt.mKeyCode == .Escape) && (mCurMenuWidget != null) && (mEditWidget != null))
			{
				mCurMenuWidget.Close();
				mEditWidget.SetFocus();
				evt.mHandled = true;
			}

			if ((evt.mKeyCode == .Return) && (mCurMenuWidget != null))
			{
				mCurMenuWidget.SubmitSelection();
				evt.mHandled = true; 	
			}
		}

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			if (mCurMenuWidget != null)
				mCurMenuWidget.KeyDown(keyCode, isRepeat);
		}

        public void MakeEditable(DarkEditWidget editWidget = null)
        {
            if (mEditWidget == null)
            {
				mEditWidget = editWidget;
				if (mEditWidget == null)
                	mEditWidget = new DarkEditWidget();
                mEditWidget.mDrawBox = false;
                mEditWidget.mOnSubmit.Add(new => EditSubmit);
				mEditWidget.mOnKeyDown.Add(new => EditKeyDownHandler);
				var ewc = (DarkEditWidgetContent)mEditWidget.mEditWidgetContent;
				ewc.mScrollToStartOnLostFocus = true;
                AddWidget(mEditWidget);
                ResizeComponents();
            }
        }

        private void EditSubmit(EditEvent theEvent)
        {
            if (mCurMenuWidget != null)
            {
                if (mCurMenuWidget.mSelectIdx != -1)
                {
                    mCurMenuWidget.SubmitSelection();
                    if (mCurMenuWidget != null)
                        mCurMenuWidget.Close();
                }
            }
        }

		public override void KeyDown(KeyDownEvent keyEvent)
		{
			base.KeyDown(keyEvent);
			EditKeyDownHandler(keyEvent);
		}
    }
}
