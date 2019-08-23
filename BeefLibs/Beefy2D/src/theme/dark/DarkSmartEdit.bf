using System;
using System.Collections.Generic;
using System.Text;
using Beefy;
using Beefy.widgets;
using Beefy.events;
using Beefy.gfx;

namespace Beefy.theme.dark
{
    public class DarkSmartEdit : Widget
    {
        public String mText;
        public bool mAllowEdit = true;        
        public Object mValue;
        public List<Widget> mMoveWidgets;
        public Action<DarkSmartEdit> mValueChangedAction;

        bool mCancelingEdit;
        float mMouseDownX;
        float mMouseDownY;
        Object mMouseDownValue;
        bool mDidDragEdit;        
        EditWidget mCurEditWidget;        

        public void SetValue(float value)
        {
            mValue = value;
            mText.AppendF("{0:0.0}", value);
            UpdateWidth();
        }

        public void SetValue(String value)
        {
            mValue = value;
            mText = value;
            UpdateWidth();
        }

        public void SetDisplay(String value)
        {
            mText = value;
            UpdateWidth();
        }

        public void NudgeMoveWidgets(float offset)
        {
            if (mMoveWidgets != null)
            {
                for (Widget moveWidget in mMoveWidgets)
                    moveWidget.mX += offset;
            }
        }

        void UpdateWidth()
        {
            float newWidth;
            if (mCurEditWidget != null)
                newWidth = mCurEditWidget.mWidth + 2;
            else
                newWidth = DarkTheme.sDarkTheme.mSmallFont.GetWidth(mText);

            float adjust = newWidth - mWidth;
            NudgeMoveWidgets(adjust);
            mWidth = newWidth;            
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            if (mText == null)
                return;

            g.SetFont(DarkTheme.sDarkTheme.mSmallFont);

            if ((mMouseDown) && (mMouseDownValue is float))
            {
                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.LeftArrowDisabled), -mX - 11, 0);

                float endX = mWidth;
                if (mMoveWidgets != null)
                    endX = mMoveWidgets[mMoveWidgets.Count - 1].mX + mMoveWidgets[mMoveWidgets.Count - 1].mWidth - mX;

                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RightArrowDisabled), endX - 5, 0);
            }

            using (g.PushColor(0xFFCC9600))
                g.DrawString(mText, 0, 0);
        }

        void HandleEditLostFocus(Widget widget)
        {
            EditWidget editWidget = (EditWidget)widget;
            editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
            editWidget.mOnSubmit.Remove(scope => HandleEditSubmit, true);
            editWidget.mOnCancel.Remove(scope => HandleEditCancel, true);

            if (!mCancelingEdit)
            {
				mText.Clear();
				editWidget.GetText(mText);
                if (mValue is float)
                {
                    float aValue = 0;
                    aValue = float.Parse(mText);
                    SetValue(aValue);
                    if (mValueChangedAction != null)
                        mValueChangedAction(this);
                }
            }

            editWidget.RemoveSelf();
            mCurEditWidget = null;
            UpdateWidth();
        }

        void HandleEditCancel(EditEvent theEvent)
        {
            mCancelingEdit = true;
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        void HandleEditSubmit(EditEvent theEvent)
        {
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);

            mMouseDownX = x;
            mMouseDownY = y;
            mMouseDownValue = mValue;
            mDidDragEdit = false;
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

            if (mMouseDown)
            {
                if (mMouseDownValue is float)
                {
                    float delta = (float)Math.Round(x - mMouseDownX + y - mMouseDownY);                    
                    // First drag must be at least 5 pixels to avoid accidentally dragging when trying to click
                    if ((mDidDragEdit) || (Math.Abs(delta) >= 4))
                    {
                        mDidDragEdit = true;
                        float aValue = (float)mMouseDownValue + delta;
                        SetValue(aValue);
                        if (mValueChangedAction != null)
                            mValueChangedAction(this);
                    }
                }
            }
        }

        public override void MouseUp(float x, float y, int32 btn)
        {
            base.MouseUp(x, y, btn);

            if ((mMouseOver) && (!mDidDragEdit) && (mValue != null))
            {
                mCancelingEdit = false;

                EditWidget editWidget = new DarkEditWidget();
				String numString = scope String();
				numString.AppendF("{0}", mValue);
                editWidget.SetText(numString);
                editWidget.Content.SelectAll();

                float aX;
                float aY;                
                SelfToParentTranslate(0, 0, out aX, out aY);

                float width = mWidth + 8;

                editWidget.Resize(aX, aY, width, 20);
                mParent.AddWidget(editWidget);

                editWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
                editWidget.mOnSubmit.Add(new => HandleEditSubmit);
                editWidget.mOnCancel.Add(new => HandleEditCancel);
                editWidget.SetFocus();

                mCurEditWidget = editWidget;
                UpdateWidth();
            }
        }
    }
}
