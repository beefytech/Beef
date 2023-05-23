using System;
using System.Collections;
using System.Text;
using Beefy.gfx;
using Beefy.events;
using System.Diagnostics;
using Beefy.utils;
using Beefy.geom;

namespace Beefy.widgets
{
    public enum MouseFlag
    {
        Left = 1,
        Right = 2,
        Middle = 4,
        Kbd = 8
    }
    
    public delegate void ResizedHandler(Widget widget);
    public delegate void LostFocusHandler(Widget widget);
    public delegate void MouseEventHandler(MouseEvent mouseArgs);
    public delegate void RemovedFromParentHandler(Widget widget, Widget prevParent, WidgetWindow widgetWindow);
    public delegate void AddedToParentHandler(Widget widget);

    public class Widget
    {   
        public class TransformData
        {
            public float a;
            public float b;
            public float c;
            public float d;
        };

        public TransformData mTransformData ~ delete _;
        public float mX;
        public float mY;
        public float mWidth;
        public float mHeight;
        public int32 mUpdateCnt;
		public double mUpdateCntF;
        public String mIdStr ~ delete _;
        public List<Widget> mChildWidgets;
        public MouseFlag mMouseFlags;
        public int32 mMouseCaptureCount;
        public Widget mParent;
        public WidgetWindow mWidgetWindow;
        public bool mMouseOver;
        public bool mMouseDown;
        public bool mClipGfx;
        public bool mClipMouse;        
        public bool mVisible = true;
        public bool mHasFocus;
        public bool mSelfMouseVisible = true;
        public bool mMouseVisible = true;
		public bool mDeferredDelete;
        public Insets mMouseInsets ~ delete _;
        public bool mAutoFocus;
		public bool mAlwaysUpdateF;
        
        public float X { get { return mX; } set { mX = value; } }        
        public float Y { get { return mY; } set { mY = value; } }

		public float CenterX { get { return mX + mWidth / 2; } }
		public float CenterY { get { return mY + mHeight / 2; } }

        [DesignEditable(SortName="0pos2")]
        public float Width { get { return mWidth; } set { mWidth = value; } }
        [DesignEditable(SortName="0pos3")]
        public float Height { get { return mHeight; } set { mHeight = value; } }
        [DesignEditable(SortName="0name")]
        public String Id { get { return mIdStr; } set { mIdStr = value; } }

		public Event<delegate void(Widget)> mOnGotFocus ~ _.Dispose();
        public Event<LostFocusHandler> mOnLostFocus ~ _.Dispose();
        //public event MouseEventHandler mMouseMoveHandler;
        public Event<MouseEventHandler> mOnMouseDown ~ _.Dispose();
        public Event<MouseEventHandler> mOnMouseUp ~ _.Dispose();
        public Event<MouseEventHandler> mOnMouseClick ~ _.Dispose();
        public Event<MouseEventHandler> mOnMouseMove ~ _.Dispose(); 
        public Event<ResizedHandler> mOnResized ~ _.Dispose();
        public Event<RemovedFromParentHandler> mOnRemovedFromParent ~ _.Dispose();
        public Event<AddedToParentHandler> mOnAddedToParent ~ _.Dispose();
        public Event<KeyDownHandler> mOnKeyDown ~ _.Dispose();
		public Event<delegate void(Widget)> mOnDeleted ~ _.Dispose();

        public Matrix Transform 
        {
            get
            {
                if (mTransformData == null)
                    return Matrix(1, 0, 0, 1, mX, mY);
                return Matrix(mTransformData.a, mTransformData.b, mTransformData.c, mTransformData.d, mX, mY);
            }

            set
            {
                if ((value.a == 1) && (value.b == 0) && (value.c == 0) && (value.d == 1))
                {
                    DeleteAndNullify!(mTransformData);
                    mX = value.tx;
                    mY = value.ty;
                }
                else
                {
                    if (mTransformData == null)
                        mTransformData = new TransformData();
                    mTransformData.a = value.a;
                    mTransformData.b = value.b;
                    mTransformData.c = value.c;
                    mTransformData.d = value.d;
                    mX = value.tx;
                    mY = value.ty;
                }
            }
        }        

		public this()
		{
		}

		public ~this()
		{
			if (mChildWidgets != null)
			{
				for (var child in mChildWidgets)
					child.ParentDeleted();
				delete mChildWidgets;
			}

			if ((mParent != null) && (mOnRemovedFromParent.HasListeners))
			{
				var prevParent = mParent;
				mParent = null;
				mOnRemovedFromParent(this, prevParent, mWidgetWindow);
			}
			mOnDeleted(this);
		}

		public void ClearTransform()
		{
			DeleteAndNullify!(mTransformData);
		}

		public void MarkDirty()
		{
			if (mWidgetWindow != null)
			{
				mWidgetWindow.mIsDirty = true;
			}
		}

		public static void RemoveAndDelete(Widget widget)
		{
			if ((widget != null) && (widget.mParent != null))
				widget.RemoveSelf();
			delete widget;
		}

		public virtual void ParentDeleted()
		{
			delete this;
		}

        public virtual void DefaultDesignInit()
        {            
            mIdStr = new String();
            GetType().GetName(mIdStr);
            mWidth = 32;
            mHeight = 32;
        }

        public virtual void Serialize(StructuredData data)
        {            
            //data.Add("X", mX);
            //data.Add("Y", mY);
            data.Add("W", mWidth);
            data.Add("H", mHeight);            
        }

        public virtual bool Deserialize(StructuredData data)
        {            
            //mX = data.GetFloat("X");
            //mY = data.GetFloat("Y");
            mWidth = data.GetFloat("W");
            mHeight = data.GetFloat("H");
            return true;
        }

		public virtual void AddWidgetUntracked(Widget widget)
		{
			Debug.Assert(widget.mParent == null);

			widget.mParent = this;
			widget.mWidgetWindow = mWidgetWindow;
			if (mWidgetWindow != null)
			    widget.InitChildren();
			widget.AddedToParent();
		}

        public virtual void AddWidgetAtIndex(int idx, Widget widget)
        {
            Debug.Assert(widget.mParent == null);

            if (mChildWidgets == null)
                mChildWidgets = new List<Widget>();

            widget.mParent = this;
            widget.mWidgetWindow = mWidgetWindow;
            mChildWidgets.Insert(idx, widget);
            if (mWidgetWindow != null)
                widget.InitChildren();

            widget.AddedToParent();
        }

		public bool HasParent(Widget wantParent)
		{
			var checkParent = mParent;
			while (checkParent != null)
			{
				if (checkParent == wantParent)
					return true;
				checkParent = checkParent.mParent;
			}
			return false;
		}

        public virtual void AddWidget(Widget widget)
        {
            int pos = (mChildWidgets != null) ? mChildWidgets.Count : 0;
            AddWidgetAtIndex(pos, widget);
        }

        public virtual void AddWidget(Widget widget, Widget addAfter)
        {
            int pos = mChildWidgets.IndexOf(addAfter) + 1;
            AddWidgetAtIndex(pos, widget);
        }

        public virtual void InsertWidget(Widget widget, Widget insertBefore)
        {
            int pos = (insertBefore != null) ? mChildWidgets.IndexOf(insertBefore) : mChildWidgets.Count;
            AddWidgetAtIndex(pos, widget);
        }        

        public virtual void InitChildren()
        {
            if (mChildWidgets != null)
            {
                for (Widget child in mChildWidgets)
                {
                    if (child.mWidgetWindow != mWidgetWindow)
                    {
                        child.mWidgetWindow = mWidgetWindow;
                        child.InitChildren();
                    }
                }
            }
        }

        protected virtual void RemovedFromWindow()
        {
            WidgetWindow prevWidgetWindow = mWidgetWindow;
			//TODO: Why did we set it to NULL here?  That caused LostFocus to not have the widget in window
            //mWidgetWindow = null;
            if (prevWidgetWindow != null)
            {
                if (prevWidgetWindow.mFocusWidget == this)
                {
                    prevWidgetWindow.mFocusWidget = null;
					if (mHasFocus)
                    	LostFocus();
					else					
						Debug.Assert(!prevWidgetWindow.mHasFocus);
                }
                if (prevWidgetWindow.mCaptureWidget == this)
                    prevWidgetWindow.mCaptureWidget = null;
                if (prevWidgetWindow.mOverWidget == this)
                {                    
                    prevWidgetWindow.mOverWidget = null;
                    MouseLeave();
                }
            }
			mWidgetWindow = null;

            if (mChildWidgets != null)
            {                
                for (int32 childIdx = 0; childIdx < mChildWidgets.Count; childIdx++)
                {
                    var child = mChildWidgets[childIdx];
                    child.RemovedFromWindow();
                }
            }
        }

        public virtual void RemoveWidget(Widget widget)
        {
            Debug.Assert(widget.mParent == this);

            WidgetWindow prevWidgetWindow = widget.mWidgetWindow;

            widget.mParent = null;

			if (mChildWidgets != null)
            	mChildWidgets.Remove(widget);
            widget.RemovedFromWindow();                        
            widget.RemovedFromParent(this, prevWidgetWindow);
        }

        public Widget FindByIdStr(String name, bool recurseChildren = true)
        {
            if (mIdStr == name)
                return this;

            if ((recurseChildren) && (mChildWidgets != null))
            {
                for (Widget child in mChildWidgets)
                {
                    Widget found = child.FindByIdStr(name, recurseChildren);
                    if (found != null)
                        return found;
                }
            }

            return null;
        }

        public virtual void RemoveSelf()
        {
            mParent.RemoveWidget(this);
        }

        public virtual bool ContainsWidget(Widget widget)
        {
            if (mChildWidgets != null)
            {
                for (Widget child in mChildWidgets)
                {
                    if (child == widget)
                        return true;
                    if (child.ContainsWidget(widget))
                        return true;
                }
            }
            return false;
        }

        public virtual void CaptureMouse()
        {
			if (mWidgetWindow == null)
				return;
			//Debug.WriteLine("CaptureMouse {0}", this);
            if (mWidgetWindow.mHasFocus)
                mWidgetWindow.mCaptureWidget = this;            
        }

        public virtual void ReleaseMouseCapture()
        {
			if (mWidgetWindow == null)
				return;
            mWidgetWindow.mCaptureWidget = null;
            mWidgetWindow.RehupMouse(false);
        }

        public virtual void SetFocus()
        {
			MarkDirty();
            mWidgetWindow?.SetFocus(this);
        }

        public virtual void SetVisible(bool visible)
        {
            mVisible = visible;
        }

        public virtual void AddedToParent()
        {
            if (mOnAddedToParent.HasListeners)
            {
                mOnAddedToParent(this);
            }
        }

        public virtual void RemovedFromParent(Widget previousParent, WidgetWindow window)
        {
            if (mOnRemovedFromParent.HasListeners)
            {
                mOnRemovedFromParent(this, previousParent, window);
            }
        }

        public virtual void GotFocus()
        {
			Debug.Assert(!mHasFocus);
            mHasFocus = true;
			mOnGotFocus(this);
        }

        public virtual void LostFocus()
        {
			Debug.Assert(mHasFocus);
            mHasFocus = false;
            mOnLostFocus(this);
        }

        public virtual void Draw(Graphics g)
        {
        }

        public virtual void DrawAll(Graphics g)
        {
            if (!mVisible)
                return;

            if (mClipGfx)
			{
				if ((mWidth <= 0) || (mHeight <= 0))
					return;
                g.PushClip(0, 0, mWidth, mHeight);
			}

            Draw(g);

            if (mChildWidgets != null)
            {
                for (int anIdx = 0; anIdx < mChildWidgets.Count; anIdx++)
                {
                    Widget child = mChildWidgets[anIdx];                
                    Debug.Assert(child.mParent == this);

					int startDepth = g.mMatrixStackIdx;

                    if (child.mTransformData != null)
                    {                        
                        Matrix m = child.Transform;
                        g.PushMatrix(m);                        
                    }
                    else
                    {
                        g.PushTranslate(child.mX, child.mY);
                    }
                    
                    child.DrawAll(g);

                    g.PopMatrix();

					Debug.Assert(startDepth == g.mMatrixStackIdx);
                }
            }

            if (mClipGfx)
                g.PopClip();
        }

        public virtual void Update()
        {
            mUpdateCnt++;
			if ((mAlwaysUpdateF) && (mWidgetWindow != null))
				UpdateF((float)(mUpdateCnt - mUpdateCntF));
			mUpdateCntF = mUpdateCnt;
        }

		public virtual void UpdateF(float updatePct)
		{
		    mUpdateCntF += updatePct;
		}

		public void DeferDelete()
		{
			mDeferredDelete = true;
		}

        public virtual void UpdateAll()
        {
            Update();
			if (mDeferredDelete)
			{
				delete this;
				return;
			}

            // Removed self?
            if (mWidgetWindow == null)
                return;

            if (mChildWidgets != null)
            {
                for (int32 anIdx = 0; anIdx < mChildWidgets.Count; anIdx++)
                {
                    Widget child = mChildWidgets[anIdx];
                    Debug.Assert(child.mParent == this);
                    Debug.Assert(child.mWidgetWindow == mWidgetWindow);
                    child.UpdateAll();
                }
            }
        }

		public virtual void UpdateFAll(float updatePct)
		{
		    UpdateF(updatePct);
			if (mDeferredDelete)
			{
				delete this;
				return;
			}

		    // Removed self?
		    if (mWidgetWindow == null)
		        return;

		    if (mChildWidgets != null)
		    {
		        for (int32 anIdx = 0; anIdx < mChildWidgets.Count; anIdx++)
		        {
		            Widget child = mChildWidgets[anIdx];
		            Debug.Assert(child.mParent == this);
		            Debug.Assert(child.mWidgetWindow == mWidgetWindow);
		            child.UpdateFAll(updatePct);
		        }
		    }
		}

        public virtual void Resize(float x, float y, float width, float height)
        {
			Debug.Assert(width >= 0);
			Debug.Assert(height >= 0);

            mX = x;
            mY = y;
            mWidth = width;
            mHeight = height;
			if (mOnResized.HasListeners)
            	mOnResized(this);
        }

		public void ResizeClamped(float x, float y, float width, float height)
		{
			Resize(x, y, Math.Max(width, 0), Math.Max(height, 0));
		}

		public void RehupSize()
		{
			Resize(mX, mY, mWidth, mHeight);
		}

		public virtual void RehupScale(float oldScale, float newScale)
		{
			if (mChildWidgets != null)
			{
				for (var child in mChildWidgets)
					child.RehupScale(oldScale, newScale);
			}
		}

        public Rect GetRect()
        {
            return Rect(mX, mY, mWidth, mHeight);
        }

        public virtual void MouseMove(float x, float y)
        {
			MarkDirty();

            if (mOnMouseMove.HasListeners)
            {
                MouseEvent e = scope MouseEvent();
                e.mX = x;
                e.mY = y;
                mOnMouseMove(e);
            }
        }
        
        public virtual Widget FindWidgetByCoords(float x, float y)
        {
            if (!mVisible || !mMouseVisible)
                return null;

            if (mClipMouse)
            {
                if (!Contains(x, y))
                    return null;                
            }

            if (mChildWidgets != null)
            {
                for (int i = mChildWidgets.Count - 1; i >= 0; --i)
                {
                    Widget child = mChildWidgets[i];
                    child.ParentToSelfTranslate(x, y, var childX, var childY);
                    Widget aFoundWidget = child.FindWidgetByCoords(childX, childY);
                    if (aFoundWidget != null)
                        return aFoundWidget;
                }
            }

            if (!mSelfMouseVisible)
                return null;

            if (mClipMouse)
                return this;

            if (Contains(x, y))
                return this;

            return null;
        }

        public virtual bool Contains(float x, float y)
        {
            if (mMouseInsets != null)
            {
                return ((mWidth != 0) &&
                    (x >= mMouseInsets.mLeft) && (y >= mMouseInsets.mTop) && 
                    (x < mWidth - mMouseInsets.mRight) && (y < mHeight - mMouseInsets.mBottom));
            }

            return ((mWidth != 0) &&
                (x >= 0) && (y >= 0) && (x < mWidth) && (y < mHeight));
        }

        public virtual void SelfToOtherTranslate(Widget targetWidget, float x, float y, out float transX, out float transY)
        {
            if (targetWidget == this)
            {
                transX = x;
                transY = y;
                return;
            }

            SelfToParentTranslate(x, y, out transX, out transY);

            if (targetWidget.mParent == mParent)
            {
                mParent.ParentToSelfTranslate(transX, transY, out transX, out transY);
                return;
            }            
            
            if (mParent != null)
            {
                float parentX = transX;
                float parentY = transY;
                mParent.SelfToOtherTranslate(targetWidget, parentX, parentY, out transX, out transY);
            }            
        }

        public virtual void SelfToRootTranslate(float x, float y, out float transX, out float transY)
        {
            SelfToParentTranslate(x, y, out transX, out transY);

            if (mParent != null)
            {
                float parentX = transX;
                float parentY = transY;
                mParent.SelfToRootTranslate(parentX, parentY, out transX, out transY);
            }
        }

        public virtual void RootToSelfTranslate(float x, float y, out float transX, out float transY)
        {
			float curX = x;
			float curY = y;
            if (mParent != null)
            {
                mParent.RootToSelfTranslate(curX, curY, out transX, out transY);
                curX = transX;
                curY = transY;                
            }

            ParentToSelfTranslate(curX, curY, out transX, out transY);
        }

        public virtual void ParentToSelfTranslate(float x, float y, out float transX, out float transY)
        {
            if (mTransformData == null)
            {
                transX = x - mX;
                transY = y - mY;
            }
            else
            {                
                Matrix m = Transform;

                float den = m.a * m.d - m.b * m.c;

                float a = m.d / den;
                float b = -m.b / den;
                float c = -m.c / den;
                float d = m.a / den;
                float tx = (m.c * m.ty - m.d * m.tx) / den;
                float ty = -(m.a * m.ty - m.b * m.tx) / den;

                transX = tx + a * (x - mX) + c * (y - mY);
                transY = ty + b * (x - mX) + d * (y - mY);
            }
        }

        public virtual void SelfToParentTranslate(float x, float y, out float transX, out float transY)
        {
            if (mTransformData == null)
            {
                transX = x + mX;
                transY = y + mY;
            }
            else
            {
                transX = mX + mTransformData.a * x + mTransformData.c * y;
                transY = mY + mTransformData.b * x + mTransformData.d * y;
            }
        }

        public virtual void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
			MarkDirty();

            if (mAutoFocus)
                SetFocus();

            mMouseFlags |= (MouseFlag)(1 << btn);
            mMouseDown = true;
            if (btn != 3) // Not kbd
                CaptureMouse();

            if (mOnMouseDown.HasListeners)
            {
                MouseEvent mouseEvent = scope MouseEvent();
                mouseEvent.mSender = this;
                mouseEvent.mX = x;
                mouseEvent.mY = y;
                mouseEvent.mBtn = btn;
                mouseEvent.mBtnCount = btnCount;
                mOnMouseDown(mouseEvent);
            }
        }

        public virtual void MouseClicked(float x, float y, float origX, float origY, int32 btn)
        {
            if (mOnMouseClick.HasListeners)
            {
                MouseEvent mouseEvent = scope MouseEvent();
                mouseEvent.mSender = this;
                mouseEvent.mX = x;
                mouseEvent.mY = y;
                mouseEvent.mBtn = btn;
                mOnMouseClick(mouseEvent);
            }
        }

        public virtual void MouseWheel(float x, float y, float deltaX, float deltaY)
        {
			MarkDirty();

            if (mParent != null)
            {
                // Keep passing it up until some is interested in using it...
                float aX;
                float aY;
                SelfToParentTranslate(x, y, out aX, out aY);
                mParent.MouseWheel(aX, aY, deltaX, deltaY);
            }
        }

        public virtual void MouseUp(float x, float y, int32 btn)
        {
			MarkDirty();

            bool hadPhysMouseDown = mMouseFlags != MouseFlag.Kbd;
            mMouseFlags &= (MouseFlag)(~(1 << btn));
            bool hasPhysMouseDown = (mMouseFlags != MouseFlag.Kbd) && (mMouseFlags != 0);
            
            mMouseDown = mMouseFlags != 0;
            if ((hadPhysMouseDown) && (!hasPhysMouseDown))
                ReleaseMouseCapture();

            if (mOnMouseUp.HasListeners)
            {
                MouseEvent mouseEvent = scope MouseEvent();
                mouseEvent.mSender = this;
                mouseEvent.mX = x;
                mouseEvent.mY = y;
                mouseEvent.mBtn = btn;                
                mOnMouseUp(mouseEvent);
            }
        }

        public virtual void MouseEnter()
        {
            mMouseOver = true;
        }

        public virtual void MouseLeave()
        {
            mMouseOver = false;
        }

        public virtual bool WantsMouseEvent(float x, float y, ref bool foundWidget)
        {
            if ((mMouseFlags != 0) || (mMouseCaptureCount > 0))
                return true;

            if (mChildWidgets != null)
            {
                for (int i = mChildWidgets.Count - 1; i >= 0; --i)
                {
                    Widget child = mChildWidgets[i];
                    float childX, childY;
                    child.ParentToSelfTranslate(x, y, out childX, out childY);

                    if (child.WantsMouseEvent(childX, childY, ref foundWidget))
                        return true;
                }
            }

            return false;
        }

		public virtual void KeyChar(KeyCharEvent keyEvent)
		{
			if (!keyEvent.mHandled)
				KeyChar(keyEvent.mChar);
		}

        public virtual void KeyChar(char32 c)
        {
        }

		public virtual void KeyDown(KeyDownEvent keyEvent)
		{
			if (!keyEvent.mHandled)
				mOnKeyDown(keyEvent);
			if (!keyEvent.mHandled)
				KeyDown(keyEvent.mKeyCode, keyEvent.mIsRepeat);
		}

        public virtual void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            
        }

        public virtual void KeyUp(KeyCode keyCode)
        {
        }

		public static bool HandleTab(int dir, List<Widget> tabWidgets)
		{
			Widget wantFocus = null;

			if (tabWidgets.IsEmpty)
				return false;

			for (int32 idx = 0; idx < tabWidgets.Count; idx++)
			{
			    Widget widget = tabWidgets[idx];
			    if (widget.mHasFocus)
			    {
					var nextWidget = tabWidgets[(idx + tabWidgets.Count + dir) % tabWidgets.Count];
					wantFocus = nextWidget;
					break;
			    }
			}

			if (wantFocus == null)
				wantFocus = tabWidgets[0];

			wantFocus.SetFocus();
			if (var editWidget = wantFocus as EditWidget)
				editWidget.mEditWidgetContent.SelectAll();
			return true;
		}
    }

	class SafeWidgetRef
	{
		Widget mWidget;

		public this(Widget widget)
		{
			mWidget = widget;
			mWidget.mOnDeleted.Add(new => OnDelete);
		}

		public ~this()
		{
			mWidget?.mOnDeleted.Remove(scope => OnDelete, true);
		}

		public Widget Value => mWidget;

		void OnDelete(Widget widget)
		{
			mWidget = null;
		}
	}
}
