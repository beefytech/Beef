///
using System;
using System.Collections;
using System.Text;
using Beefy.gfx;
using Beefy.theme;
using Beefy.widgets;
using Beefy.events;
using System.Diagnostics;
using Beefy.geom;

namespace Beefy.theme.dark
{
    public class DarkTreeOpenButton : ButtonWidget
    {
		public DarkListViewItem mItem;
        public float mRot = 0;
        public bool mIsOpen;
        public bool mAllowOpen = true;
		public bool mIsReversed;

		public this()
		{
			mAlwaysUpdateF = true;
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            Matrix matrix = Matrix.IdentityMatrix;
            matrix.Translate(-DarkTheme.sUnitSize/2, -DarkTheme.sUnitSize/2);
			if (mIsReversed)
            	matrix.Rotate(-mRot);
			else
				matrix.Rotate(mRot);
            matrix.Translate(DarkTheme.sUnitSize/2, DarkTheme.sUnitSize/2);

            using (g.PushMatrix(matrix))
                g.Draw(DarkTheme.sDarkTheme.mTreeArrow);
        }
        
        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            bool wasOpen = mIsOpen;
            base.MouseDown(x, y, btn, btnCount);
            if (wasOpen == mIsOpen)
                Open(!mIsOpen, false);
        }        

        public void Open(bool open, bool immediate)
        {
            if ((open) && (!mAllowOpen))
                return;
            mIsOpen = open;
            if (immediate)
                mRot = mIsOpen ? (Math.PI_f / 2) : 0;
        }

        public override void UpdateF(float updatePct)
        {
            base.UpdateF(updatePct);

            int childCount = mItem.mChildItems.Count;

            float rotSpeed = 0.12f + (1.0f / (childCount + 1));

            if ((mIsOpen) && (mRot < Math.PI_f / 2))
            {
                mRot = Math.Min(Math.PI_f / 2, mRot + rotSpeed * updatePct);
                mItem.mListView.mListSizeDirty = true;
				MarkDirty();
				mWidgetWindow.mTempWantsUpdateF = true;
            }
            else if ((!mIsOpen) && (mRot > 0))
            {
                mRot = (float)Math.Max(0, mRot - rotSpeed * updatePct);
                mItem.mListView.mListSizeDirty = true;
				MarkDirty();
				mWidgetWindow.mTempWantsUpdateF = true;
            }

            float x;
            float y;
            SelfToOtherTranslate(mItem.mListView, 0, 0, out x, out y);
            if (mItem.mListView.mColumns.Count > 0)
                mVisible = x + 8 < mItem.mListView.mColumns[0].mWidth;
            else
                mVisible = true;
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

        }
    }    

    public class DarkListViewItem : ListViewItem, IDragInterface
    {        
        public float mChildIndent;
        public DarkTreeOpenButton mOpenButton;
        public uint32? mTextColor;
        public uint32 mFocusColor = DarkTheme.COLOR_MENU_FOCUSED;
        public uint32 mSelectColor = DarkTheme.COLOR_MENU_SELECTED;
        public float mTextAreaLengthOffset;        

        public DragEvent mCurDragEvent ~ delete _;
        public DragHelper mDragHelper ~ delete _;
        public DarkListViewItem mDragTarget;
        public DragKind mDragKind;
        public bool mOpenOnDoubleClick = true;
        public bool mIsBold;

		public bool AllowDragging
		{
			get
			{
				return mDragHelper != null;
			}

			set
			{
				if (value)
				{
					if (mDragHelper == null)
					{
						mDragHelper = new DragHelper(this, this);
						mDragHelper.mMinDownTicks = 15;
						mDragHelper.mTriggerDist = 2;
					}
				}
				else
				{
					DeleteAndNullify!(mDragHelper);
				}
			}
		}

        public override float LabelX 
        { 
            get 
            {
                float x;

                if (mColumnIdx == 0)
                    x = ((DarkListView)mListView).mLabelX;
                else
                    x = 6;
                float absX;
                float absY;
                SelfToOtherTranslate(mListView, x, 0, out absX, out absY);
                return absX;
            } 
        }

        public override float LabelWidth
        {
            get
            {
                DarkListView listView = (DarkListView)mListView;
                float labelX = (mColumnIdx == 0) ? ((DarkListView)mListView).mLabelX : 6;
                float calcWidth = 0;
                for (int32 i = mColumnIdx; i < mListView.mColumns.Count; i++)
                {
                    if ((mSubItems != null) && (i > mColumnIdx) && (i < mSubItems.Count) && (mSubItems[i] != null))                    
                        break;
                    calcWidth += mListView.mColumns[i].mWidth;
                }
                if (mColumnIdx == 0)
                    calcWidth -= (mDepth - 1) * listView.mChildIndent;
                return calcWidth - labelX;
            }
        }

		public override bool IsOpen
		{
			get
			{
				return (mOpenButton != null) && (mOpenButton.mIsOpen);
			}
		}

		public override bool IsDragging
		{
			get
			{
				return (mDragTarget != null) && (mDragHelper.mIsDragging);
			}
		}

        public this()
        {
            
        }

		public override void Init(ListView listView)
		{
			var darkListView = (DarkListView)listView;
			if (darkListView.mFont != null)
				mSelfHeight = darkListView.mFont.GetLineSpacing();
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			if (mOpenButton != null)
			{
				mOpenButton.Resize(((DarkListView)mListView).mOpenButtonX, 0, DarkTheme.sUnitSize, DarkTheme.sUnitSize);
			}

			base.RehupScale(oldScale, newScale);
			var listView = (DarkListView)mListView;
			if (mX != 0)
				mX = listView.mChildIndent;
			if ((listView.mFont != null) && (mSelfHeight != 0))
				mSelfHeight = listView.mFont.GetLineSpacing();
			Utils.RoundScale(ref mBottomPadding, newScale / oldScale);
			if (mChildItems != null)
			{
				for (var child in mChildItems)
					child.RehupScale(oldScale, newScale);
			}

			mFocusColor = DarkTheme.COLOR_MENU_FOCUSED;
			mSelectColor = DarkTheme.COLOR_MENU_SELECTED;
		}

        protected virtual float GetLabelOffset()
        {
            return 0;
        }

		public virtual bool WantsTooltip(float mouseX, float mouseY)
		{
		    // The default tooltip behavior is to show a tooltip if the text is obscured-
		    //  meaning either offscreen or in a column that's too small

		    if (mLabel == null)
		        return false;

		    var ideListView = (DarkListView)mListView;

		    float x = 8;
		    if (mColumnIdx == 0)
		        x = LabelX + 8;
		    float textWidth = ideListView.mFont.GetWidth(mLabel);
		    bool isObscured = false;
		    if (mColumnIdx < ideListView.mColumns.Count - 1)
		    {
		        float maxWidth = ideListView.mColumns[mColumnIdx].mWidth;
		        isObscured |= (x + textWidth + 8 >= maxWidth);
		    }

		    float parentEndX;
		    float parentEndY;
		    SelfToOtherTranslate(ideListView.mScrollContentContainer, x + textWidth, 0, out parentEndX, out parentEndY);
		    isObscured |= (parentEndX > ideListView.mScrollContentContainer.mWidth);

		    return isObscured;
		}

		public virtual void ShowTooltip(float mouseX, float mouseY)
		{
		    float x = GS!(8);
		    if (mColumnIdx == 0)
		        x = LabelX + GS!(8);
		    DarkTooltipManager.ShowTooltip(mLabel, this, x, mHeight);
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            /*if (mDepth != 0)
            {
                using (g.PushColor(0x400000FF))
                    g.FillRect(1, 1, mWidth - 2, mHeight - 2);
            }*/

            DarkListView listView = (DarkListView)mListView;
            float labelOfs = GetLabelOffset();
            float labelX = labelOfs + ((mColumnIdx == 0) ? ((DarkListView)mListView).mLabelX : GS!(6));
            DarkListView darkListView = (DarkListView)listView;
            g.SetFont(mIsBold ? darkListView.mBoldFont : darkListView.mFont);

            int32 nextContentColumn = -1;
            float calcWidth = 0;
            for (int32 i = mColumnIdx; i < mListView.mColumns.Count; i++)
            {
                if ((mSubItems != null) && (i > mColumnIdx) && (i < mSubItems.Count) && (mSubItems[i] != null))
                {
                    nextContentColumn = i;
                    break;
                }
                calcWidth += mListView.mColumns[i].mWidth;
            }

            if (Selected)
            {
                /*float lastStrWidth = labelX + g.mFont.GetWidth(mLabel);

                for (int i = 1; i < mListView.mColumns.Count; i++)
                {
                    if ((mSubItems != null) && (i > mColumnIdx) && (i < mSubItems.Count) && (mSubItems[i] != null))
                    {
                        DarkListViewItem subItem = (DarkListViewItem) mSubItems[i];
                        if (subItem.mLabel != null)
                            lastStrWidth = subItem.mX - mX + g.mFont.GetWidth(subItem.mLabel) - 6;
                        //g.FillRect(subItem.mX - mX, subItem.mY - mY, 3, 3);
                    }
                }
                
                using (g.PushColor(mSelectColor))
                    g.DrawButton(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MenuSelect), 4 + listView.mHiliteOffset, 0, Math.Max(lastStrWidth + 16, mWidth - 4 * 2 - listView.mHiliteOffset));*/
            }
            
            if (mIconImage != null)
            {
                IDisposable colorScope = null;
                if (mIconImageColor != 0)
                    colorScope = g.PushColor(mIconImageColor);
                g.Draw(mIconImage, listView.mIconX + labelOfs, 0);
                if (colorScope != null)
                    colorScope.Dispose();
            }

            if (mLabel != null)
            {
                float wantWidth = calcWidth - labelX;
                if (mColumnIdx == 0)
                    wantWidth -= (mDepth - 1) * listView.mChildIndent;
                wantWidth += GS!(mTextAreaLengthOffset);

				if ((listView.mEndInEllipsis) && (nextContentColumn == -1) && (listView.mVertScrollbar == null))
				{
					wantWidth = mListView.mWidth - labelX - mX;
					if (listView.mInsets != null)
						wantWidth -= listView.mInsets.mRight;
				}							 

                using (g.PushColor(mTextColor ?? DarkTheme.COLOR_TEXT))
				{
					FontOverflowMode overflowMode = ((nextContentColumn != -1) || (listView.mEndInEllipsis)) ? .Ellipsis : .Overflow;
					if (listView.mWordWrap)
						overflowMode = .Wrap;
                    g.DrawString(mLabel, labelX, 0, .Left, wantWidth, overflowMode);
				}
            }
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);

            if ((btn == 0) && (btnCount > 1) && (mOpenOnDoubleClick) && (mOpenButton != null))
                mOpenButton.Open(!mOpenButton.mIsOpen, false);
        }

        public override void MakeParent()
        {
            if ((mChildItems == null) && (mDepth > 0))
            {
                DarkTreeOpenButton treeOpenButton = new DarkTreeOpenButton();
                treeOpenButton.mItem = this;
                AddWidget(treeOpenButton);
                treeOpenButton.Resize(((DarkListView)mListView).mOpenButtonX, 0, DarkTheme.sUnitSize, DarkTheme.sUnitSize);
                mOpenButton = treeOpenButton;
            }

            base.MakeParent();
        }

		public override void TryUnmakeParent()
		{
			base.TryUnmakeParent();
			if ((mChildItems == null) && (mOpenButton != null))
			{
				mOpenButton.RemoveSelf();
				DeleteAndNullify!(mOpenButton);
			}
		}

        public override ListViewItem CreateChildItem()
        {
            MakeParent();
            DarkListViewItem child = (DarkListViewItem)base.CreateChildItem();            
            if (mDepth != 0)
                child.mX = ((DarkListView)mListView).mChildIndent;
            return child;
        }

        public override void AddChildAtIndex(int index, ListViewItem item)
        {
            base.AddChildAtIndex(index, item);
            if (mDepth != 0)
                item.mX = ((DarkListView)mListView).mChildIndent;
            else
                item.mX = 0;
        }        

        void UpdateShowChildPct()
        {
            // Fast to slow
            if (mOpenButton.mIsOpen)            
                mShowChildPct = Math.Min((float)Math.Sin(mOpenButton.mRot) + 0.001f, 1.0f);            
            else            
                mShowChildPct = Math.Max(1.0f - Math.Sin(Math.PI_f / 2 - mOpenButton.mRot) - 0.001f, 0.0f);            
        }

        public override float CalculatedDesiredHeight()
        {            
            mShowChildPct = 1.0f;
            
            if (mOpenButton != null)
            {
                UpdateShowChildPct();
                if (mShowChildPct >= 0.999f)
                    mShowChildPct = 1.0f;
            }

			mChildAreaHeight = 0;
			if ((mChildItems != null) && (mShowChildPct > 0))
			{
			    for (ListViewItem listViewItem in mChildItems)
			        mChildAreaHeight += listViewItem.CalculatedDesiredHeight();
				mChildAreaHeight *= mShowChildPct;
			}

			return mChildAreaHeight + mSelfHeight + mBottomPadding;

            //return base.CalculatedDesiredHeight();
        }

        public override ListViewItem CreateChildItemAtIndex(int idx)
        {
            var child = base.CreateChildItemAtIndex(idx);
            if (mOpenButton != null)
                UpdateShowChildPct();
            child.mVisible = mShowChildPct > 0.0f;
            return child;
        }

        public override Widget FindWidgetByCoords(float x, float y)
        {
            if ((mShowChildPct < 1.0f) && (y > mHeight))
                return null;

            let result = base.FindWidgetByCoords(x, y);

			if (mChildItems != null)
			{
				if ((y >= 0) && (y < mChildAreaHeight + mSelfHeight + mBottomPadding))
				{
					int itemStart = Math.Max(0, FindItemAtY(y) - 1);
					int itemEnd = Math.Min(itemStart + 1, mChildItems.Count);

					for (int childIdx = itemStart; childIdx < itemEnd; childIdx++)
					{
						let child = mChildItems[childIdx];
					    child.ParentToSelfTranslate(x, y, var childX, var childY);
					    Widget foundWidget = child.FindWidgetByCoords(childX, childY);
					    if (foundWidget != null)
					        return foundWidget;
					}
				}
			}

			return result;
        }

		public bool IsVisible(Graphics g)
		{
			float checkY = g.mMatrix.ty;            

			float minY = g.mClipRect.Value.mY;
			float maxY = minY + g.mClipRect.Value.mHeight;

			if ((checkY + Math.Max(mHeight, mChildAreaHeight) < minY) || (checkY > maxY))
			    return false;
			return true;
		}

        void DrawLinesGrid(Graphics g)
        {
            var listView = (DarkListView)mListView;
            float originX;
            float originY;
            SelfToOtherTranslate(listView, 0, 0, out originX, out originY);

            for (int32 i = 0; i < mChildItems.Count; i++)
            {
                var childItem = mChildItems[i];
				if (childItem.mHeight == 0)
					continue;
                using (g.PushColor(listView.mGridLinesColor))
                {
                    float linePos = childItem.mY + childItem.mHeight;
                    if (linePos <= mChildAreaHeight + mSelfHeight)
                        g.FillRect(-originX, linePos, listView.mWidth, 1);
                }
            }
        }

        public virtual void DrawSelect(Graphics g)
        {
            var listView = (DarkListView)mListView;
            float labelOfs = GetLabelOffset();
            float labelX = labelOfs + ((mColumnIdx == 0) ? ((DarkListView)mListView).mLabelX : 6);
            float lastStrWidth = labelX + g.mFont.GetWidth(mLabel);

            for (int32 i = 1; i < mListView.mColumns.Count; i++)
            {
                if ((mSubItems != null) && (i > mColumnIdx) && (i < mSubItems.Count) && (mSubItems[i] != null))
                {
                    DarkListViewItem subItem = (DarkListViewItem)mSubItems[i];
                    if (subItem.mLabel != null)
                        lastStrWidth = subItem.mX - mX + g.mFont.GetWidth(subItem.mLabel) - GS!(6);
                }
            }

			float hiliteX = GS!(4) + listView.mHiliteOffset;
			

			if (mListView.mColumns.Count > 0)
			{
				float adjust = LabelX - mListView.mColumns[0].mWidth;

				if (adjust > 0)
				{
					hiliteX -= adjust;
				}
			}

			var darkListView = mListView as DarkListView;
			float height = darkListView.mFont.GetLineSpacing();

            uint32 color = Focused ? mFocusColor : mSelectColor;
            using (g.PushColor(color))
			{
				if (Math.Abs(height - mSelfHeight) < height * 0.5f)
				{
	                g.DrawButton(DarkTheme.sDarkTheme.GetImage(Focused ? DarkTheme.ImageIdx.MenuSelect : DarkTheme.ImageIdx.MenuNonFocusSelect),
	                    hiliteX, 0, Math.Max(lastStrWidth + GS!(16), mWidth - GS!(4) - hiliteX));
				}
				else
				{
					g.DrawBox(DarkTheme.sDarkTheme.GetImage(Focused ? DarkTheme.ImageIdx.MenuSelect : DarkTheme.ImageIdx.MenuNonFocusSelect),
						hiliteX, 0, Math.Max(lastStrWidth + GS!(16), mWidth - GS!(4) - hiliteX), mSelfHeight);
				}
			}
        }

		int FindItemAtY(float y)
		{
			int lo = 0;
		    int hi = mChildItems.Count - 1;
			
			while (lo <= hi)
		    {
		        int i = (lo + hi) / 2;
				let midVal = mChildItems[i];
				float c = midVal.mY - y;

		        if (c == 0) return i;
		        if (c < 0)
		            lo = i + 1;                    
		        else
		            hi = i - 1;
			}
			return lo;
		}

		protected virtual void DrawChildren(Graphics g, int itemStart, int itemEnd)
		{
			for (int childIdx = itemStart; childIdx < itemEnd; childIdx++)
			{
				if (childIdx >= mChildItems.Count)
					break;
				let child = mChildItems[childIdx];
			    g.PushTranslate(child.mX, child.mY);
			    child.DrawAll(g);
			    g.PopMatrix();
			}
		}

		void DrawChildren(Graphics g)
		{
			base.DrawAll(g);

			if (mChildItems != null)
			{
				//mChildItems.BinarySearch()
				float drawStartY = g.mClipRect.Value.mY - g.mMatrix.ty;
				float drawEndY = g.mClipRect.Value.Bottom - g.mMatrix.ty;
				int itemStart = 0;
				if (drawStartY > 0)
					itemStart = Math.Max(0, FindItemAtY(drawStartY) - 1);
				int itemEnd = FindItemAtY(drawEndY);

				if (itemStart < itemEnd)
					DrawChildren(g, itemStart, itemEnd);
			}
		}

        public override void DrawAll(Graphics g)
        {
            if ((!mVisible) || (mHeight <= 0))
                return;

            var listView = (DarkListView)mListView;

            if (Selected)
            {
                DrawSelect(g);
            }
            
            if ((mShowChildPct < 1.0f) && (mShowChildPct > 0.0f))            
            {
                using (g.PushClip(0, 0, mWidgetWindow.mClientWidth, mSelfHeight + mChildAreaHeight))
                {
                    if ((listView.mShowGridLines) && (mShowChildPct > 0) && (mChildItems != null))
                        DrawLinesGrid(g);
                    DrawChildren(g);
                }
                return;
            }

            if ((listView.mShowGridLines) && (mShowChildPct > 0) && (mChildItems != null))
                DrawLinesGrid(g);

			DrawChildren(g);

			if (mDragTarget != null)
			{
				listView.mOnPostDraw.Add(new (g) =>
					{
						float targetX;
						float targetY;
						mDragTarget.SelfToOtherTranslate(mListView, 0, 0, out targetX, out targetY);

						//using (g.PushTranslate(targetX, targetY))
						{
						    /*if ((mUpdateCnt % 60) == 0)
						        Debug.WriteLine(String.Format("{0} indent {1}", mDragTarget.mLabel, mDragTarget.mDepth));*/

							if (mDragKind == .Inside)
							{
								using (g.PushColor(0xFF6f9761))
								{
									//g.FillRect(targetX + GS!(4), targetY, mListView.mWidth - targetX - GS!(28), GS!(22));
									//g.OutlineRect(targetX + GS!(4), targetY, mListView.mWidth - targetX - GS!(28), GS!(20));
									g.DrawButton(DarkTheme.sDarkTheme.GetImage(Focused ? DarkTheme.ImageIdx.MenuSelect : DarkTheme.ImageIdx.MenuNonFocusSelect),
										targetX + GS!(2), targetY, listView.mWidth - targetX - GS!(24));
								}
							}
							else
							{
								var darkListView = (DarkListView)mListView;

								if (mDragTarget.mParentItem != mListView.GetRoot())
									targetX += darkListView.mLabelX - darkListView.mChildIndent;

						        if ((mDragKind == .Inside) || (mDragKind == .After)) // Inside or after
						            targetY += mDragTarget.mSelfHeight;
						        
						        if (mDragKind == .After) // After
						            targetY += mDragTarget.mChildAreaHeight + mDragTarget.mBottomPadding;

						        if (mDragKind == .Inside) // Inside
						            targetX += ((DarkListView)mListView).mChildIndent + mDragTarget.mChildIndent;

						        /*if (-curY + targetY > mHeight)                    
						            wasTargetBelowBottom = true;*/                    

						        using (g.PushColor(0xFF95A68F))
						            g.FillRect(targetX + GS!(4), targetY, mListView.mWidth - targetX - GS!(28), GS!(2));
							}
						}
					});
			}	

            /*if (mDragTarget != null)
            {
                float targetX;
                float targetY;
                mDragTarget.SelfToOtherTranslate(mListView, 0, 0, out targetX, out targetY);

                float curX;
                float curY;
                SelfToOtherTranslate(mListView, 0, 0, out curX, out curY);

                bool wasTargetBelowBottom = false;

                using (g.PushTranslate(-curX, -curY))
                {
                    /*if ((mUpdateCnt % 60) == 0)
                        Debug.WriteLine(String.Format("{0} indent {1}", mDragTarget.mLabel, mDragTarget.mDepth));*/

					if (mDragKind == .Inside)
					{
						using (g.PushColor(0xFF95A68F))
							g.FillRect(targetX + GS!(4), targetY, mListView.mWidth - targetX - GS!(28), GS!(22));
					}
					else
					{
	                    if ((mDragKind == .Inside) || (mDragKind == .After)) // Inside or after
	                        targetY += mDragTarget.mSelfHeight;
	                    
	                    if (mDragKind == .After) // After
	                        targetY += mDragTarget.mChildAreaHeight + mDragTarget.mBottomPadding;

	                    if (mDragKind == .Inside) // Inside
	                        targetX += ((DarkListView)mListView).mChildIndent + mDragTarget.mChildIndent;

	                    if (-curY + targetY > mHeight)                    
	                        wasTargetBelowBottom = true;                    

	                    using (g.PushColor(0xFF95A68F))
	                        g.FillRect(targetX + GS!(4), targetY, mListView.mWidth - targetX - GS!(28), GS!(2));
					}
                }

                if (wasTargetBelowBottom)
                {
                    /*Image img = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MoveDownArrow);
                    g.Draw(img, mWidth / 2 - img.mWidth / 2, mHeight - img.mHeight);*/
                }
            }*/
        }

        public override bool Open(bool open, bool immediate = false)
        {
            if ((mOpenButton != null) && (mOpenButton.mIsOpen != open))
            {
                mOpenButton.Open(open, immediate);
                return true;
            }
            return false;
        }

        public void DragStart()
        {            
        }

        public void DragEnd()
        {            
            DarkListView listView = (DarkListView)mListView;

            if (mCurDragEvent != null)
            {
                DragEvent e = mCurDragEvent;
                e.mSender = GetMainItem();
                e.mDragTarget = mDragTarget;                
                if (mDragHelper.mAborted)
                    e.mDragKind = .None;
                if (listView.mOnDragEnd.HasListeners)
                    listView.mOnDragEnd(e);
            }
            mDragTarget = null;
        }

        public void MouseDrag(float x, float y, float dX, float dY)
        {
            float aX = x;
            float aY = y;

            ListViewItem head = this;
            if (mColumnIdx != 0)
                head = head.GetSubItem(0);

            if (!head.Selected)
                return;

            DarkListView listView = (DarkListView)mListView;

            if (listView.mOnDragUpdate.HasListeners)
            {                
                SelfToRootTranslate(x, y, out aX, out aY);
                Widget foundWidget = mWidgetWindow.mRootWidget.FindWidgetByCoords(aX, aY);

                if (foundWidget == null)
                {                                        
                    return;
                }

                var root = mListView.GetRoot();
                if ((foundWidget is ListView) || (foundWidget == root))
                {                    
                    var lastItem = root.mChildItems[root.mChildItems.Count - 1];
                    float lastWindowX;
                    float lastWindowY;
                    lastItem.SelfToRootTranslate(lastItem.mSelfHeight + lastItem.mChildAreaHeight + lastItem.mBottomPadding, 0, out lastWindowX, out lastWindowY);
                    if (aY > lastWindowY)
                    {
                        // After last item
                        foundWidget = lastItem;
                    }
                }

                var listViewItem = foundWidget as DarkListViewItem;
                if (listViewItem == null)
                    listViewItem = foundWidget.mParent as DarkListViewItem;
                if (listViewItem == null)
                    return;

                if (listViewItem.mColumnIdx != 0)
                    listViewItem = (DarkListViewItem)listViewItem.GetSubItem(0);
                foundWidget = listViewItem;                

                float childX;
                float childY;
                foundWidget.SelfToRootTranslate(0, 0, out childX, out childY);

				if ((listViewItem.mOpenButton != null) && (aX - childX < GS!(30)))
				{
					mDragKind = .Inside;
				}
				else
				{
	                float yOfs = aY - childY;
	                if (yOfs < mHeight / 2)
	                    mDragKind = .Before;
	                else
	                {
	                    mDragKind = .After;
	                    if ((listViewItem.mOpenButton != null) && (!listViewItem.mChildItems.IsEmpty)  && (listViewItem.mOpenButton.mIsOpen))
						{
							var firstChild = listViewItem.mChildItems[0];
							foundWidget = firstChild;
	                        mDragKind = .Before;
						}
	                }
				}

				if (Math.Abs(dY) < mSelfHeight * 0.21f)
				{
					mDragKind = .None;
					mDragTarget = null;
					return;
				}

				delete mCurDragEvent;
                mCurDragEvent = new DragEvent();
                mCurDragEvent.mX = x;
                mCurDragEvent.mY = y;
                mCurDragEvent.mSender = head;
                mCurDragEvent.mDragTarget = foundWidget;
                mCurDragEvent.mDragKind = mDragKind;
                listView.mOnDragUpdate(mCurDragEvent);                
                mDragKind = mCurDragEvent.mDragKind;

                mDragTarget = mCurDragEvent.mDragTarget as DarkListViewItem;
                if (mCurDragEvent.mDragKind == .None)
                    mDragTarget = null;

                if (mDragTarget != null)
                {
                    if (mDragTarget == mListView.GetRoot())
                    {
                        mDragTarget = null;
                    }
                    else
                    {
                        if (mDragTarget.mColumnIdx != 0)
                            mDragTarget = (DarkListViewItem)mDragTarget.GetSubItem(0);      
                    }
                }
            }
        }

        public override void Update()
        {
            base.Update();
            mDragHelper?.Update();

			if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mRelWidget == this))
			{
			    Point mousePoint;
			    DarkTooltipManager.CheckMouseover(this, -1, out mousePoint);
			    if (!WantsTooltip(mousePoint.x, mousePoint.y))
			        DarkTooltipManager.CloseTooltip();
			}

			if (DarkTooltipManager.sLastMouseWidget == this)
			{
			    Point mousePoint;
			    if (DarkTooltipManager.CheckMouseover(this, 20, out mousePoint))
			    {
			        if (WantsTooltip(mousePoint.x, mousePoint.y))                    
			            ShowTooltip(mousePoint.x, mousePoint.y);
			        else
			            DarkTooltipManager.CloseTooltip();
			    }
			}
        }

		public override void UpdateAll()
		{
			if (mVisible)
			{
				base.UpdateAll();

				if (mChildItems != null)
				{
					for (int32 anIdx = 0; anIdx < mChildItems.Count; anIdx++)
					{
					    Widget child = mChildItems[anIdx];
					    Debug.Assert(child.mParent == this);
					    Debug.Assert(child.mWidgetWindow == mWidgetWindow);
					    child.UpdateAll();
					}
				}
			}
		}

		public override void UpdateFAll(float updatePct)
		{
			if (mVisible)
			{
				base.UpdateFAll(updatePct);

				if (mChildItems != null)
				{
					for (int32 anIdx = 0; anIdx < mChildItems.Count; anIdx++)
					{
					    Widget child = mChildItems[anIdx];
					    Debug.Assert(child.mParent == this);
					    Debug.Assert(child.mWidgetWindow == mWidgetWindow);
					    child.UpdateFAll(updatePct);
					}
				}
			}
		}
    }

    public class DarkListView : ListView
    {
		public struct SortType
		{
			public int mColumn;
			public bool mReverse;

			public this()
			{
				mColumn = -1;
				mReverse = false;
			}

			public this(int column, bool reverse)
			{
				mColumn = column;
				mReverse = reverse;
			}
		}

        public float mHeaderSplitIdx = -1;        
        public float mDragOffset = 0;
        public bool mShowColumnGrid;
        public bool mShowGridLines;
		public Color mGridLinesColor = 0x0CFFFFFF;
        public bool mShowHeader = true;
		public bool mEndInEllipsis;
		public bool mWordWrap;
        public float mLabelX = DarkTheme.sUnitSize;        
        public float mChildIndent = DarkTheme.sUnitSize;
        public float mOpenButtonX = 0;
        public float mIconX = GS!(4);
        public float mHiliteOffset;        
        public Font mFont;
        public Font mBoldFont;
        public DarkTheme.ImageIdx mHeaderImageIdx = DarkTheme.ImageIdx.ListViewHeader;
        public float mHeaderLabelYOfs = 0;
		public SortType mSortType = SortType() ~ { mSortType.mColumn = -1; };
		public Insets mInsets ~ delete _;

        public Event<delegate void(DragEvent)> mOnDragUpdate ~ _.Dispose();
        public Event<delegate void(DragEvent)> mOnDragEnd ~ _.Dispose();
		public Event<delegate void(Graphics)> mOnPostDraw ~ _.Dispose();

        public this()
        {
            mFont = DarkTheme.sDarkTheme.mSmallFont;
            mBoldFont = DarkTheme.sDarkTheme.mSmallBoldFont;
            SetShowHeader(true);
			
        }

        public void SetShowHeader(bool showHeader)
        {
            mShowHeader = showHeader;
            SetScaleData();
        }

		protected virtual void SetScaleData()
		{
			mHeaderHeight = mShowHeader ? DarkTheme.sUnitSize : 0;
			mScrollbarInsets.Set(mShowHeader ? GS!(18) : 0, 0, 0, 0);
			mScrollContentInsets.Set(GS!(2), 0, GS!(-1), GS!(-1));
			mLabelX = DarkTheme.sUnitSize;
			mChildIndent = DarkTheme.sUnitSize;
			mListSizeDirty = true;
		}

        public override void InitScrollbars(bool wantHorz, bool wantVert)
        {
            if (!wantHorz)
                mScrollContentInsets.mBottom += GS!(2);

            base.InitScrollbars(wantHorz, wantVert);
        }

        protected override ListViewItem CreateListViewItem()
        {
            DarkListViewItem anItem = new DarkListViewItem();            
            return anItem;
        }

        float GetMaxContentWidth(float xOfs, DarkListViewItem listViewItem)
        {            
            float maxWidth = 0;
            if ((listViewItem.mSubItems != null) && (listViewItem.mSubItems.Count > 1))
            {
                var subItem = listViewItem.mSubItems[listViewItem.mSubItems.Count - 1];
                if (subItem.mLabel != null)
                    maxWidth = mFont.GetWidth(subItem.mLabel) + xOfs + listViewItem.mX + subItem.mX + 6;
            }
            else if (listViewItem.mLabel != null)
            {
                maxWidth = mFont.GetWidth(listViewItem.mLabel) + xOfs + listViewItem.mX + mLabelX;
            }

            if ((listViewItem.mChildItems != null) && (listViewItem.mChildAreaHeight > 0))
            {
                for (var child in listViewItem.mChildItems)
                    maxWidth = Math.Max(maxWidth, GetMaxContentWidth(listViewItem.mX + xOfs, (DarkListViewItem)child));
            }

            return maxWidth;
        }

        public override float GetListWidth()
        {
            float listWidth = base.GetListWidth();
            float maxContentWidth = GetMaxContentWidth(0, (DarkListViewItem)mRoot) + 6;
            return Math.Max(listWidth, maxContentWidth);
        }

        protected virtual void DrawColumnGridColumn(Graphics g, float x, float y, float height, uint32 color)
        {
            using (g.PushColor(color))
                g.FillRect(x, y, 1, height);
        }

		public virtual void DrawColumn(Graphics g, int32 columnIdx)
		{
			var column = mColumns[columnIdx];
			float drawXOfs = GS!(6);
			float drawWidth = column.mWidth - drawXOfs - GS!(6);
			using (g.PushColor(DarkTheme.COLOR_TEXT))
				g.DrawString(column.mLabel, drawXOfs, mHeaderLabelYOfs + GS!(2), FontAlign.Left, drawWidth, (columnIdx < mColumns.Count - 1) ? FontOverflowMode.Ellipsis : FontOverflowMode.Overflow);
			
			if (columnIdx != 0)
			{
			    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Separator), GS!(-2), mHeaderLabelYOfs);
			}

			if ((mShowColumnGrid) && (columnIdx < mColumns.Count - 1))
			{
			    DrawColumnGridColumn(g, column.mWidth, DarkTheme.sUnitSize, mHeight - DarkTheme.sUnitSize - 1, 0xFF707070);
			}

			float sortArrowX = g.mFont.GetWidth(column.mLabel) + DarkTheme.sUnitSize/2;
			if (columnIdx == mSortType.mColumn)
			{
				if (!mSortType.mReverse)
				{
					using (g.PushScale(1.0f, -1.0f, column.mWidth - DarkTheme.sUnitSize, DarkTheme.sUnitSize/2))
						g.Draw(DarkTheme.sDarkTheme.GetImage(.ListViewSortArrow), sortArrowX, 0);
				}
				else
			        g.Draw(DarkTheme.sDarkTheme.GetImage(.ListViewSortArrow), sortArrowX, 0);
			}
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			if (mWidth <= GS!(3))
				return;

            if (mShowHeader)
            {
                if (mHeaderImageIdx < DarkTheme.ImageIdx.COUNT)
                    g.DrawButton(DarkTheme.sDarkTheme.GetImage(mHeaderImageIdx), 0, 0, mWidth);
            
                using (g.PushClip(1, 0, mWidth - GS!(2), mHeight))
                {
                    using (g.PushTranslate(mScrollContent.mX, 0))
                    {
                        g.SetFont(DarkTheme.sDarkTheme.mHeaderFont);

                        float curX = 0;
                        for (int32 columnIdx = 0; columnIdx < mColumns.Count; columnIdx++)
                        {
                            ListViewColumn column = mColumns[columnIdx];
							using (g.PushTranslate(curX, 0))
								DrawColumn(g, columnIdx);
							curX += column.mWidth;
                        }
                    }
                }
            }
            else if (mShowColumnGrid)
            {                
                using (g.PushClip(1, 0, mWidth - GS!(2), mHeight))
                {
                    using (g.PushTranslate(mScrollContent.mX, 0))
                    {                        
                        float curX = 0;
                        for (int32 columnIdx = 0; columnIdx < mColumns.Count; columnIdx++)
                        {
                            ListViewColumn column = mColumns[columnIdx];                            
                            curX += column.mWidth;                            

                            if ((mShowColumnGrid) && (columnIdx < mColumns.Count - 1))
                            {
                                DrawColumnGridColumn(g, curX, GS!(4), mHeight - GS!(8), 0xFF888888);
                            }
                        }
                    }
                }
            }
        }

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
			mOnPostDraw(g);
			mOnPostDraw.Dispose();
		}

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

			float useX = x - mScrollContent.mX;
			float useY = y;

            if (mMouseDown)
            {
                float curX = 0;
                for (int32 columnIdx = 0; columnIdx < mColumns.Count - 1; columnIdx++)
                {
                    ListViewColumn column = mColumns[columnIdx];
                    
                    if (columnIdx == mHeaderSplitIdx)
                    {
                        mColumns[columnIdx].mWidth = useX - curX - mDragOffset;
                        if (mColumns[columnIdx].mMinWidth != 0)
                            mColumns[columnIdx].mWidth = (float) Math.Max(mColumns[columnIdx].mMinWidth, mColumns[columnIdx].mWidth);
                        if (mColumns[columnIdx].mMaxWidth != 0)
                            mColumns[columnIdx].mWidth = (float)Math.Min(mColumns[columnIdx].mMaxWidth, mColumns[columnIdx].mWidth);
                        ColumnResized(mColumns[columnIdx]);
                        mListSizeDirty = true;
                    }

                    curX += column.mWidth;
                }
            }
            else
            {
                mHeaderSplitIdx = -1;

                if ((useY >= 0) && ((useY < mHeaderHeight) || (mShowColumnGrid)))
                {
                    float curX = 0;
                    for (int32 columnIdx = 0; columnIdx < mColumns.Count - 1; columnIdx++)
                    {
                        ListViewColumn column = mColumns[columnIdx];
                        curX += column.mWidth;

                        if (Math.Abs(useX - curX + 1) <= GS!(3))
                        {
                            mHeaderSplitIdx = columnIdx;
                            mDragOffset = useX - curX;
                        }
                    }
                }

                if (mHeaderSplitIdx != -1)
                    BFApp.sApp.SetCursor(Cursor.SizeWE);
                else
                    BFApp.sApp.SetCursor(Cursor.Pointer);
            }
        }

		public (int columnIdx, bool isSplitter) GetColumnAt(float x, float y)
		{
			float useX = x - mScrollContent.mX;
			float useY = y;

			if ((useY >= 0) && (useY < mHeaderHeight))
			{
			    float curX = 0;
			    for (int32 columnIdx = 0; columnIdx < mColumns.Count; columnIdx++)
			    {
			        ListViewColumn column = mColumns[columnIdx];
			        curX += column.mWidth;

			        if (Math.Abs(useX - curX + 1) <= 3)
			        {
			            return (columnIdx, true);
			        }

					if (useX <= curX)
						return (columnIdx, false);
			    }
			}

			return (-1, false);
		}

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);

            let (column, isSplitter) = GetColumnAt(x, y);
			if ((column != -1) && (!isSplitter))
			{
				var newSortType = mSortType;
				if (newSortType.mColumn == column)
				{
					newSortType.mReverse = !newSortType.mReverse;
				}
				else
				{
					newSortType.mColumn = column;
				}
				ChangeSort(newSortType);
			}
        }

		public virtual void ChangeSort(SortType sortType)
		{

		}

        public override void MouseUp(float x, float y, int32 btn)
        {
            base.MouseUp(x, y, btn);
            MouseMove(x, y);
        }

        public override void MouseLeave()
        {
            base.MouseLeave();
            BFApp.sApp.SetCursor(Cursor.Pointer);
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			float valScale = newScale / oldScale;
			for (var column in mColumns)
			{
				Utils.RoundScale(ref column.mWidth, valScale);
				Utils.RoundScale(ref column.mMinWidth, valScale);
				Utils.RoundScale(ref column.mMaxWidth, valScale);
			}

			mListSizeDirty = true;
			mIconX *= valScale;
			mScrollbarInsets.Scale(valScale);
			SetScaleData();

			base.RehupScale(oldScale, newScale);
		}
    }
}
