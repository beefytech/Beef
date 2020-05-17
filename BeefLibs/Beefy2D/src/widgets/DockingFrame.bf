using System;
using System.Collections;
using System.Text;
using Beefy.theme;
using Beefy.gfx;
using System.Diagnostics;
using Beefy.geom;

namespace Beefy.widgets
{    
    public class DockingFrame : DockedWidget
    {        
        public enum WidgetAlign
        {         
            Inside,
            Left,
            Right,
            Top,
            Bottom
        };

        public enum SplitType
        {
            None,
            Horz,
            Vert
        };                
        
        public List<DockedWidget> mDockedWidgets = new List<DockedWidget>() ~ delete _;
        public SplitType mSplitType = SplitType.None;
        public float mMinWindowSize = 32;
        public float mDragMarginSize = 64;
        public float mDragWindowMarginSize = 10;

        public float mWindowMargin = 0;
        public float mSplitterSize = 6.0f;
        public float mWindowSpacing = 2.0f;
        public int32 mDownSplitterNum = -1;

        public IDockable mDraggingDock;
        public DockedWidget mDraggingRef;
        public WidgetAlign mDraggingAlign;
        public ICustomDock mDraggingCustomDock ~ delete (Object)_;

        public virtual void AddDockedWidget(DockedWidget widget, DockedWidget refWidget, WidgetAlign align)
        {
            SplitType wantSplitType = ((align == WidgetAlign.Left) || (align == WidgetAlign.Right)) ?
                SplitType.Horz : SplitType.Vert;

            if (mDockedWidgets.Count == 0)
                wantSplitType = SplitType.None;

            if ((wantSplitType != mSplitType) && (mSplitType != SplitType.None))
            {
                DockingFrame newChildFrame = ThemeFactory.mDefault.CreateDockingFrame(this);
                bool hasFillWidget = mHasFillWidget;
                                
                if (refWidget != null)
                {
                    newChildFrame.mRequestedWidth = refWidget.mRequestedWidth;
                    newChildFrame.mRequestedHeight = refWidget.mRequestedHeight;
                    ReplaceDockedWidget(refWidget, newChildFrame);

                    // Just split out the one referenced widget
                    newChildFrame.mParentDockingFrame = this;
                    refWidget.mParentDockingFrame = newChildFrame;
                    newChildFrame.mDockedWidgets.Add(refWidget);
                    newChildFrame.AddWidget(refWidget);

                    newChildFrame.AddDockedWidget(widget, null, align);
                    ResizeContent(true);
                    return;
                }
                else
                {
                    newChildFrame.mRequestedWidth = 0;
                    newChildFrame.mRequestedHeight = 0;

                    for (DockedWidget aDockedWidget in mDockedWidgets)
                    {
                        if (mSplitType == SplitType.Horz)
                        {
                            newChildFrame.mRequestedWidth += aDockedWidget.mRequestedWidth;
                            newChildFrame.mRequestedHeight = Math.Max(newChildFrame.mRequestedHeight, aDockedWidget.mRequestedHeight);
                        }
                        else
                        {
                            newChildFrame.mRequestedWidth = Math.Max(mRequestedWidth, aDockedWidget.mRequestedWidth);
                            newChildFrame.mRequestedHeight += aDockedWidget.mRequestedHeight;
                        }

                        RemoveWidget(aDockedWidget);
                        newChildFrame.mDockedWidgets.Add(aDockedWidget);
                        newChildFrame.AddWidget(aDockedWidget);
                        aDockedWidget.mParentDockingFrame = newChildFrame;
                        hasFillWidget |= aDockedWidget.mHasFillWidget;
                    }
                    mDockedWidgets.Clear();
                }

                newChildFrame.mParentDockingFrame = this;
                newChildFrame.mHasFillWidget = hasFillWidget;
                newChildFrame.mSplitType = mSplitType;
                newChildFrame.mWidth = newChildFrame.mRequestedWidth;
                newChildFrame.mHeight = newChildFrame.mRequestedHeight;

                mDockedWidgets.Add(newChildFrame);
                AddWidget(newChildFrame);
            }
            
            widget.mParentDockingFrame = this;
            
			//TODO:
            /*if (fillWidget != null)
            {
                // Don't take more than half remaining space at a time
                widget.mRequestedWidth = Math.Min(fillWidget.mWidth / 2, widget.mRequestedWidth);
                widget.mRequestedHeight = Math.Min(fillWidget.mHeight / 2, widget.mRequestedHeight);
            }*/

            widget.mRequestedWidth = Math.Max(mMinWindowSize, widget.mRequestedWidth);
            widget.mRequestedHeight = Math.Max(mMinWindowSize, widget.mRequestedHeight);            

            if ((align == WidgetAlign.Left) || (align == WidgetAlign.Top))
            {
                if (refWidget != null)
                    mDockedWidgets.Insert(mDockedWidgets.IndexOf(refWidget), widget);
                else
                    mDockedWidgets.Insert(0, widget);
            }
            else
            {
                if (refWidget != null)
                    mDockedWidgets.Insert(mDockedWidgets.IndexOf(refWidget) + 1, widget);
                else
                    mDockedWidgets.Add(widget);
            }

            AddWidget(widget);

            mSplitType = wantSplitType;

			if ((widget.mHasFillWidget) || (widget.mIsFillWidget))
				widget.GetRootDockingFrame().Rehup();
            ResizeContent(true);
        }

		public override void Rehup()
		{
			bool allHaveSizes = true;

			bool didHaveFillWidget = mHasFillWidget;
			base.Rehup();
			for (var dockedWiget in mDockedWidgets)
			{
				dockedWiget.mHasFillWidget = dockedWiget.mIsFillWidget;
				dockedWiget.Rehup();
				if (dockedWiget.mHasFillWidget)
				{
					mHasFillWidget = true;
				}
				if ((dockedWiget.mWidth == 0) || (dockedWiget.mHeight == 0))
					allHaveSizes = false;
			}

			// If we turn off mHasFillWidget and go to an all-non-fill situation then we reset the size priority 
			if (didHaveFillWidget != mHasFillWidget)
			{
				for (var dockedWiget in mDockedWidgets)
					dockedWiget.mSizePriority = 0;

				//if (!mHasFillWidget)
				if (allHaveSizes)
				{
					for (var dockedWidget in mDockedWidgets)
					{
						if (mSplitType == .Horz)
							dockedWidget.mRequestedWidth = dockedWidget.mWidth;
						else
							dockedWidget.mRequestedHeight = dockedWidget.mHeight;
					}
				}
			}

			for (var dockedWiget in mDockedWidgets)
			{
				// Reset the size priority to be whatever the current size is, it should stabilize sizes when
				//  toggling mIsFillWidget
				if (dockedWiget.mHasFillWidget == mHasFillWidget)
				{
					if (dockedWiget.mSizePriority == 0)
					{
						if (mSplitType == .Horz)
							dockedWiget.mSizePriority = dockedWiget.mWidth;
						else
							dockedWiget.mSizePriority = dockedWiget.mHeight;
					}
				}
				else
					dockedWiget.mSizePriority = 0;
			}
		}

		public override DockingFrame GetRootDockingFrame()
		{
			var parentFrame = this;
			while (parentFrame != null)
			{
				if (parentFrame.mParentDockingFrame == null)
					break;
				parentFrame = parentFrame.mParentDockingFrame;
			}
			return parentFrame;
		}

        public void WithAllDockedWidgets(delegate void(DockedWidget) func)
        {
            for (var dockedWidget in mDockedWidgets)
            {                
                func(dockedWidget);

                var dockingFrame = dockedWidget as DockingFrame;
                if (dockingFrame != null)
                {
                    dockingFrame.WithAllDockedWidgets(func);                    
                }
            }
        }

        public int GetDockedWindowCount()
        {
            return mDockedWidgets.Count;
        }

        public void Simplify()
        {            
            if ((mDockedWidgets.Count == 0) && (mParentDockingFrame != null))
			{
                mParentDockingFrame.RemoveWidget(this);
				mParentDockingFrame.mDockedWidgets.Remove(this);
				BFApp.sApp.DeferDelete(this);
			}
            else if ((mDockedWidgets.Count == 1) && (mParentDockingFrame != null))
            {
                // Just a single object, remove ourselves from the frame
                DockedWidget aDockedWidget = mDockedWidgets[0];
                mDockedWidgets.Clear();
                RemoveWidget(aDockedWidget);
                mParentDockingFrame.ReplaceDockedWidget(this, aDockedWidget);
				BFApp.sApp.DeferDelete(this);
            }            
        }

        public virtual void RemoveDockedWidget(DockedWidget dockedWidget)
        {
            mDockedWidgets.Remove(dockedWidget);
            RemoveWidget(dockedWidget);
			if (!mIsFillWidget)
            	mHasFillWidget = GetHasFillWidget();
            ResizeContent(true);

            if ((mDockedWidgets.Count == 0) && (mParentDockingFrame == null))
            {
                // Automatically close when last docked widget is removed
                //  Should only happen on tool windows
				if (!mWidgetWindow.mWindowFlags.HasFlag(.QuitOnClose))
                	mWidgetWindow.Close();                
            }            
        }

        // Used when an embedded docking frame gets down to just a single widget
        public virtual void ReplaceDockedWidget(DockedWidget dockedWidget, DockedWidget replaceWidget)
        {
            int index = mDockedWidgets.IndexOf(dockedWidget);
            RemoveWidget(dockedWidget);
            mDockedWidgets[index] = replaceWidget;
            AddWidget(replaceWidget);
            replaceWidget.mParentDockingFrame = this;
            ResizeContent(true);
        }

        protected virtual bool GetHasFillWidget()
        {
            for (DockedWidget aDockedWidget in mDockedWidgets)
                if (aDockedWidget.mHasFillWidget)
                    return true;
            return false;
        }

        public virtual void StartContentInterpolate()
        {
            for (DockedWidget aDockedWidget in mDockedWidgets)
            {                
                aDockedWidget.StartInterpolate();
            }
        }

        public virtual void ResizeContent(bool interpolate = false)
        {
            float sizeLeft = (mSplitType == SplitType.Horz) ? (mWidth - mWindowMargin*2) : (mHeight - mWindowMargin*2);
            sizeLeft -= (mDockedWidgets.Count - 1) * mWindowSpacing;
			if (sizeLeft <= 0)
				return;
   
            List<DockedWidget> widgetsLeft = scope List<DockedWidget>();
            for (DockedWidget aDockedWidget in mDockedWidgets)
            {
                widgetsLeft.Add(aDockedWidget);
                if (interpolate)
                    aDockedWidget.StartInterpolate();
            }

			//DockedWidget fillWidget = GetFillWidget();
			//if (fillWidget != null)
			{
				bool hasFillWidget = false;
				for (int32 widgetIdx = 0; widgetIdx < widgetsLeft.Count; widgetIdx++)
				{
					DockedWidget aDockedWidget = widgetsLeft[widgetIdx];
					if (aDockedWidget.mHasFillWidget)
					{
						hasFillWidget = true;
						break;
					}
				}

				if (hasFillWidget)
				{
				    for (int32 widgetIdx = 0; widgetIdx < widgetsLeft.Count; widgetIdx++)
				    {
				        DockedWidget aDockedWidget = widgetsLeft[widgetIdx];
						if (aDockedWidget.mHasFillWidget)
							continue;

				        float requestedSize = (mSplitType == SplitType.Horz) ? (aDockedWidget.mRequestedWidth) : aDockedWidget.mRequestedHeight;
				        float size = Math.Round(Math.Max(mMinWindowSize, Math.Min(requestedSize, sizeLeft - (widgetsLeft.Count * mMinWindowSize))));

				        if (mSplitType == SplitType.Horz)
				            aDockedWidget.mWidth = size;
				        else
				            aDockedWidget.mHeight = size;

				        sizeLeft -= size;
				        widgetsLeft.RemoveAt(widgetIdx);
				        widgetIdx--;
				    }
				}

				//widgetsLeft.Add(fillWidget);
			    /*if (mSplitType == SplitType.Horz)
			        fillWidget.mWidth = sizeLeft;
			    else
			        fillWidget.mHeight = sizeLeft;*/
			}

			//if (fillWidget == null)
			{                
			    int32 totalPriCount = 0;
			    int32 newPriCounts = 0;
			    float totalPriAcc = 0.0f;
			    float avgPri = 0;

			    for (DockedWidget aDockedWidget in widgetsLeft)
			    {
			        totalPriAcc += aDockedWidget.mSizePriority;
			        if (aDockedWidget.mSizePriority == 0)
			            newPriCounts++;
			        else
			            totalPriCount++;
			    }

				if (totalPriCount == 0)
				{
					// Totally uninitialized, possibly from having a document frame and then removing it
					for (DockedWidget aDockedWidget in widgetsLeft)
						if ((aDockedWidget.mSizePriority == 0) && (aDockedWidget.mWidth > 0) && (aDockedWidget.mHeight > 0))
						{
						    aDockedWidget.mSizePriority = (mSplitType == .Horz) ? aDockedWidget.mWidth : aDockedWidget.mHeight;
						    totalPriCount++;
						    totalPriAcc += aDockedWidget.mSizePriority;
							newPriCounts--;
						}
				}

			    if (newPriCounts > 0)
			    {
			        if (totalPriAcc > 0)
			            avgPri = totalPriAcc / totalPriCount;
			        else
			            avgPri = 1.0f / newPriCounts;

			        for (DockedWidget aDockedWidget in widgetsLeft)
			            if (aDockedWidget.mSizePriority == 0)
			            {
			                aDockedWidget.mSizePriority = avgPri;
			                totalPriCount++;
			                totalPriAcc += aDockedWidget.mSizePriority;
			            }
			    }


			    float sharedWidth = sizeLeft;

			    for (int32 widgetIdx = 0; widgetIdx < widgetsLeft.Count; widgetIdx++)
			    {
			        DockedWidget aDockedWidget = widgetsLeft[widgetIdx];

			        float size = (aDockedWidget.mSizePriority / totalPriAcc) * sharedWidth;
			        size = (float) Math.Round(size);

			        if (widgetIdx == widgetsLeft.Count - 1)
			            size = sizeLeft;

			        if (mSplitType == SplitType.Horz)
			        {
			            aDockedWidget.mWidth = size;                        
			        }
			        else
			        {
			            aDockedWidget.mHeight = size;                        
			        }
			        
			        sizeLeft -= size;
			        widgetsLeft.RemoveAt(widgetIdx);
			        widgetIdx--;
			    }               
			}

            /*DockedWidget fillWidget = GetFillWidget();
            if (fillWidget == null)
            {                
                int totalPriCount = 0;
                int newPriCounts = 0;
                float totalPriAcc = 0.0f;
                float avgPri = 0;

                foreach (DockedWidget aDockedWidget in widgetsLeft)
                {
                    totalPriAcc += aDockedWidget.mSizePriority;
                    if (aDockedWidget.mSizePriority == 0)
                        newPriCounts++;
                    else
                        totalPriCount++;
                }

                if (newPriCounts > 0)
                {
                    if (totalPriAcc > 0)
                        avgPri = totalPriAcc / totalPriCount;
                    else
                        avgPri = 1.0f / newPriCounts;

                    foreach (DockedWidget aDockedWidget in widgetsLeft)
                        if (aDockedWidget.mSizePriority == 0)
                        {
                            aDockedWidget.mSizePriority = avgPri;
                            totalPriCount++;
                            totalPriAcc += aDockedWidget.mSizePriority;
                        }
                }


                float sharedWidth = sizeLeft;

                for (int widgetIdx = 0; widgetIdx < widgetsLeft.Count; widgetIdx++)
                {
                    DockedWidget aDockedWidget = widgetsLeft[widgetIdx];

                    float size = (aDockedWidget.mSizePriority / totalPriAcc) * sharedWidth;
                    size = (float) Math.Round(size);

                    if (widgetIdx == widgetsLeft.Count - 1)
                        size = sizeLeft;

                    if (mSplitType == SplitType.Horz)
                    {
                        aDockedWidget.mWidth = size;                        
                    }
                    else
                    {
                        aDockedWidget.mHeight = size;                        
                    }
                    
                    sizeLeft -= size;
                    widgetsLeft.RemoveAt(widgetIdx);
                    widgetIdx--;
                }               
            }
            else
            {
                widgetsLeft.Remove(fillWidget);

                for (int widgetIdx = 0; widgetIdx < widgetsLeft.Count; widgetIdx++)
                {
                    DockedWidget aDockedWidget = widgetsLeft[widgetIdx];

                    float requestedSize = (mSplitType == SplitType.Horz) ? (aDockedWidget.mRequestedWidth) : aDockedWidget.mRequestedHeight;
                    float size = Math.Max(mMinWindowSize, Math.Min(requestedSize, sizeLeft - (widgetsLeft.Count * mMinWindowSize)));

                    if (mSplitType == SplitType.Horz)
                        aDockedWidget.mWidth = size;
                    else
                        aDockedWidget.mHeight = size;

                    sizeLeft -= size;
                    widgetsLeft.RemoveAt(widgetIdx);
                    widgetIdx--;
                }

                 if (mSplitType == SplitType.Horz)
                    fillWidget.mWidth = sizeLeft;
                else
                    fillWidget.mHeight = sizeLeft;
            }*/

            float curPos = mWindowMargin;
            for (DockedWidget aDockedWidget in mDockedWidgets)
            {
                float size = (mSplitType == SplitType.Horz) ? aDockedWidget.mWidth : aDockedWidget.mHeight;

                if (mSplitType == SplitType.Horz)
                {
                    aDockedWidget.ResizeDocked(curPos, mWindowMargin, size, mHeight - mWindowMargin*2);
                }
                else
                {
                    aDockedWidget.ResizeDocked(mWindowMargin, curPos, mWidth - mWindowMargin*2, size);
                }

                curPos += size;
                curPos += mWindowSpacing;
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeContent();
        }

        public int32 FindSplitterAt(float x, float y)
        {
            if (!Contains(x, y))
                return -1;

            float curPos = 0;
            float findPos = (mSplitType == SplitType.Horz) ? (x - mWindowMargin) : (y - mWindowMargin);
            for (int32 widgetIdx = 0; widgetIdx < mDockedWidgets.Count; widgetIdx++)            
            {
                DockedWidget aDockedWidget = mDockedWidgets[widgetIdx];
                float size = (mSplitType == SplitType.Horz) ? aDockedWidget.mWidth : aDockedWidget.mHeight;

                float diff = (findPos - curPos) + mWindowSpacing + (mSplitterSize - mWindowSpacing) / 2.0f;
                if ((diff >= 0) && (diff < mSplitterSize))
                    return widgetIdx - 1;

                curPos += size;
                curPos += mWindowSpacing;
            }

            return -1;
        }

        DockingFrame GetDockingFrame(WidgetWindow widgetWindow)
        {
            if (widgetWindow == null)
                return null;

            DockingFrame dockingFrame = widgetWindow.mRootWidget as DockingFrame;
            if (dockingFrame != null)
                return dockingFrame;
            
            for (var child in widgetWindow.mRootWidget.mChildWidgets)
            {
                dockingFrame = child as DockingFrame;
                if (dockingFrame != null)
                    return dockingFrame;
            }
            return null;
        }

        public virtual void ShowDragTarget(IDockable draggingItem)
        {
            Debug.Assert(mParentDockingFrame == null);

            for (BFWindow window in BFApp.sApp.mWindows)
            {
                if (window.mAlpha != 1.0f)
                    continue;

                WidgetWindow widgetWindow = window as WidgetWindow;

                var dockingFrame = GetDockingFrame(widgetWindow);
                if (dockingFrame != null)
                {
                    if ((widgetWindow.mHasMouseInside) || (widgetWindow.mHasProxyMouseInside))
                        dockingFrame.ShowDragTarget(draggingItem, widgetWindow.mMouseX, widgetWindow.mMouseY);
                    else
                        dockingFrame.HideDragTargets();                        
                }                
            }
        }

        public virtual void HideDragTarget(IDockable draggingItem, bool executeDrag = false)
        {
            Debug.Assert(mParentDockingFrame == null);

            for (int32 windowIdx = 0; windowIdx < BFApp.sApp.mWindows.Count; windowIdx++)
            {
                BFWindow window = BFApp.sApp.mWindows[windowIdx];
                WidgetWindow widgetWindow = window as WidgetWindow;
                if (widgetWindow != null)
                {                    
                    DockingFrame dockingFrame = GetDockingFrame(widgetWindow);
                    if (dockingFrame != null)
                        dockingFrame.HideDragTargets(executeDrag);
                }
            }
        }

        void HideDragTargets(bool executeDrag = false)
        {
            if ((executeDrag) && (mDraggingDock != null))
            {
                if (mDraggingCustomDock != null)
                    mDraggingCustomDock.Dock(mDraggingDock);
                else
                    mDraggingDock.Dock(this, mDraggingRef, mDraggingAlign);
            }
            mDraggingDock = null;
            mDraggingRef = null;
			delete (Object)mDraggingCustomDock;
            mDraggingCustomDock = null;

            for (int32 dockedWidgetIdx = 0; dockedWidgetIdx < mDockedWidgets.Count; dockedWidgetIdx++)
            {
                DockedWidget aDockedWidget = mDockedWidgets[dockedWidgetIdx];
                DockingFrame childFrame = aDockedWidget as DockingFrame;
                if (childFrame != null)
                    childFrame.HideDragTargets(executeDrag);
            }
        }


        bool FindDragTarget(IDockable draggingItem, float x, float y, ref DockingFrame containingFrame, ref DockedWidget refWidget, ref WidgetAlign align, ref ICustomDock customDock, int32 minDist = -2)
        {
			//int foundMinDist = -1;
			bool foundInSelf = false;

            for (DockedWidget aDockedWidget in mDockedWidgets)
            {
                float childX;
                float childY;
                aDockedWidget.ParentToSelfTranslate(x, y, out childX, out childY);

				var rect = Rect(-2, -2, aDockedWidget.mWidth + 4, aDockedWidget.mHeight + 4);
				
                //if (aDockedWidget.Contains(childX, childY))
				if (rect.Contains(childX, childY))
                {
                    float leftDist = childX;
                    float topDist = childY;
                    float rightDist = aDockedWidget.mWidth - childX;
                    float botDist = aDockedWidget.mHeight - childY;

					if (Math.Min(Math.Min(Math.Min(leftDist, topDist), rightDist), botDist) < minDist)
						continue;

                    float marginX = Math.Min(mDragMarginSize, aDockedWidget.mWidth / 4);
                    float marginY = Math.Min(mDragMarginSize, aDockedWidget.mHeight / 4);

                    if ((marginX < Math.Min(leftDist, rightDist)) && (marginY < Math.Min(topDist, botDist)))
                        align = WidgetAlign.Inside;
                    else if (leftDist < Math.Min(topDist, Math.Min(rightDist, botDist)))
                        align = WidgetAlign.Left;
                    else if (topDist < Math.Min(rightDist, botDist))
                        align = WidgetAlign.Top;
                    else if (rightDist < botDist)
                        align = WidgetAlign.Right;
                    else
                        align = WidgetAlign.Bottom;

                    if (draggingItem.CanDock(this, aDockedWidget, align))
                    {
                        customDock = aDockedWidget.GetCustomDock(draggingItem, childX, childY);

                        containingFrame = this;
                        refWidget = aDockedWidget;
						foundInSelf = true;
                        break;
                    }
                }
            }

			int32 newMinDist = minDist;
			if (foundInSelf)
				newMinDist = Math.Max(minDist, 0) + 2;

			for (DockedWidget aDockedWidget in mDockedWidgets)
			{
			    float childX;
			    float childY;
			    aDockedWidget.ParentToSelfTranslate(x, y, out childX, out childY);

			    if (aDockedWidget.Contains(childX, childY))
			    {
			        DockingFrame childFrame = aDockedWidget as DockingFrame;
			        if (childFrame != null)
			        {
			            childFrame.FindDragTarget(draggingItem, childX, childY, ref containingFrame, ref refWidget, ref align, ref customDock, newMinDist);
			            break;
			        }
			    }
			}

            if ((mParentDockingFrame == null) && (customDock == null))
            {
                if (Contains(x, y))
                {
                    float leftDist = x;
                    float topDist = y;
                    float rightDist = mWidth - x;
                    float botDist = mHeight - y;                    

                    float marginX = Math.Min(mDragWindowMarginSize, mWidth / 4);
                    float marginY = Math.Min(mDragWindowMarginSize, mHeight / 4);
                    WidgetAlign anAlign;

                    if ((marginX < Math.Min(leftDist, rightDist)) && (marginY < Math.Min(topDist, botDist)))
                        anAlign = WidgetAlign.Inside;
                    else if (leftDist < Math.Min(topDist, Math.Min(rightDist, botDist)))
                        anAlign = WidgetAlign.Left;
                    else if (topDist < Math.Min(rightDist, botDist))
                        anAlign = WidgetAlign.Top;
                    else if (rightDist < botDist)
                        anAlign = WidgetAlign.Right;
                    else
                        anAlign = WidgetAlign.Bottom;

                    if ((anAlign != WidgetAlign.Inside) && (draggingItem.CanDock(this, null, align)))
                    {
                        containingFrame = this;
                        refWidget = null;
                        customDock = null;
                        align = anAlign;
                    }
                }
            }

            
            return containingFrame != null;
        }

        void ShowDragTarget(IDockable draggingItem, float x, float y)
        {
            DockingFrame containingFrame = null;
            DockedWidget refWidget = null;
            WidgetAlign align = .Inside;
            ICustomDock customDock = null;

			/*containingFrame = null;
			refWidget = null;
			align = WidgetAlign.Inside;
			customDock = null;*/

			MarkDirty();
            HideDragTargets();

			if ((y < 0) && (y >= -6) && (x >= 0) && (x < mWidgetWindow.mWindowWidth))
			{
				containingFrame = this;

				containingFrame.mDraggingDock = draggingItem;
				containingFrame.mDraggingRef = null;
				containingFrame.mDraggingAlign = .Top;

				delete (Object)containingFrame.mDraggingCustomDock;
				containingFrame.mDraggingCustomDock = null;
				return;
			}	

            if (FindDragTarget(draggingItem, x, y, ref containingFrame, ref refWidget, ref align, ref customDock))
            {
                containingFrame.mDraggingDock = draggingItem;
                containingFrame.mDraggingRef = refWidget;
                containingFrame.mDraggingAlign = align;

				delete (Object)containingFrame.mDraggingCustomDock;
                containingFrame.mDraggingCustomDock = customDock;
            }
        }

       
        public override Widget FindWidgetByCoords(float x, float y)
        {
            if (FindSplitterAt(x, y) != -1)
                return this;
            return base.FindWidgetByCoords(x, y);
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
            mDownSplitterNum = FindSplitterAt(x, y);            
        }        

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

            if (mDownSplitterNum != -1)
            {
                float wantPos = (mSplitType == SplitType.Horz) ? x : y;

				DockedWidget resizedWidget = null;
                DockedWidget widget1 = mDockedWidgets[mDownSplitterNum];
                DockedWidget widget2 = mDockedWidgets[mDownSplitterNum + 1];

				List<DockedWidget> widgetsLeft = scope List<DockedWidget>();
				float sizeLeft = (mSplitType == SplitType.Horz) ? (mWidth - mWindowMargin*2) : (mHeight - mWindowMargin*2);

				if ((widget1.mHasFillWidget) && (widget2.mHasFillWidget))
				{
					bool foundWidget = false;
					for (var dockedWidget in mDockedWidgets)
					{
						if (dockedWidget == widget1)
							foundWidget = true;
						if (dockedWidget.mHasFillWidget)
							widgetsLeft.Add(dockedWidget);
						else
						{
							float curSize = (mSplitType == SplitType.Horz) ? dockedWidget.mWidth : dockedWidget.mHeight;
							if (!foundWidget)
								wantPos -= curSize;
							sizeLeft -= curSize;
						}
					}
				}
				else if (widget1.mHasFillWidget != widget2.mHasFillWidget)
				{
					if (widget2.mHasFillWidget)
					{
						if (mSplitType == .Horz)
							widget1.mRequestedWidth = wantPos - widget1.mX;
						else
							widget1.mRequestedHeight = wantPos - widget1.mY;
					}
					else
					{
						if (mSplitType == .Horz)
						{
							float totalSize = widget1.mWidth + widget2.mWidth;
							widget2.mRequestedWidth = Math.Max(0, widget2.mRequestedWidth + (widget2.mX - wantPos));
							widget2.mRequestedWidth = Math.Min(widget2.mRequestedWidth, totalSize - mMinWindowSize * 2);
						}
						else
						{
							float totalSize = widget1.mHeight + widget2.mHeight;
							widget2.mRequestedHeight = Math.Max(0, widget2.mRequestedHeight + (widget2.mY - wantPos));
							widget2.mRequestedHeight = Math.Min(widget2.mRequestedHeight, totalSize - mMinWindowSize * 2);
						}
						resizedWidget = widget2;
						//Debug.WriteLine("RW:{0} WH:{1}", widget2.mRequestedWidth, widget2.mRequestedHeight);
					}
				}
				else
				{
					bool hasFillWidget = false;
					for (var dockedWidget in mDockedWidgets)
					{
						if (dockedWidget.mHasFillWidget)
							hasFillWidget = true;
					}

					if (hasFillWidget)
					{
						for (var dockedWidget in mDockedWidgets)
						{
							// Size prioritizes will get initialized later if we remove the fill widget
							if (!dockedWidget.mHasFillWidget)
								dockedWidget.mSizePriority = 0;
						}

						if (mSplitType == .Horz)
						{
							float totalSize = widget1.mWidth + widget2.mWidth;
							widget2.mRequestedWidth = Math.Clamp(widget2.mRequestedWidth + (widget2.mX - wantPos), mMinWindowSize, totalSize - mMinWindowSize);
							widget1.mRequestedWidth = totalSize - widget2.mRequestedWidth;
						}			   	 
						else
						{
							float totalSize = widget1.mHeight + widget2.mHeight;
							widget2.mRequestedHeight = Math.Clamp(widget2.mRequestedHeight + (widget2.mY - wantPos), mMinWindowSize, totalSize - mMinWindowSize);
							widget1.mRequestedHeight = totalSize - widget2.mRequestedHeight;
						}
					}
					else
					{
						for (var dockedWidget in mDockedWidgets)
							widgetsLeft.Add(dockedWidget);
					}
				}

				if (widgetsLeft.Count > 0)
                {                                        
                    // First we normalize to 1.0
                    float sizePriTotal = 0.0f;
                    for (DockedWidget aDockedWidget in widgetsLeft)
                        sizePriTotal += aDockedWidget.mSizePriority;
                    
                    for (DockedWidget aDockedWidget in widgetsLeft)
                        aDockedWidget.mSizePriority = aDockedWidget.mSizePriority / sizePriTotal;


                    float totalPrevSize = widget1.mSizePriority + widget2.mSizePriority;

                    float startCurPos = 0;
                    for (DockedWidget aDockedWidget in widgetsLeft)
                    {
                        if (aDockedWidget == widget1)
                            break;

                        startCurPos += aDockedWidget.mSizePriority * sizeLeft + mWindowSpacing;
                    }

                    float wantSize = Math.Max(mMinWindowSize, wantPos - startCurPos);
                    wantSize = Math.Min(wantSize, totalPrevSize * sizeLeft - mMinWindowSize);

                    wantSize /= sizeLeft;
                    widget1.mSizePriority = wantSize;
                    widget2.mSizePriority = totalPrevSize - wantSize;
                }

                ResizeContent();

				// Set to actual used value
				if (resizedWidget != null)
				{
					if (mSplitType == .Horz)
						resizedWidget.mRequestedWidth = resizedWidget.mWidth;
					else
						resizedWidget.mRequestedHeight = resizedWidget.mHeight;
				}
            }
            else
            {
                if (FindSplitterAt(x, y) != -1)
                {
                    if (mSplitType == SplitType.Horz)
                        BFApp.sApp.SetCursor(Cursor.SizeWE);
                    else
                        BFApp.sApp.SetCursor(Cursor.SizeNS);
                }
                else
                {
                    BFApp.sApp.SetCursor(Cursor.Pointer);
                }
            }
        }

        public override void MouseUp(float x, float y, int32 btn)
        {
            base.MouseUp(x, y, btn);            
            mDownSplitterNum = -1;
            MouseMove(x, y);
        }

        public override void MouseLeave()
        {            
            base.MouseLeave();
            if (!mMouseDown)
                BFApp.sApp.SetCursor(Cursor.Pointer);
        }

        public virtual void DrawDraggingDock(Graphics g)
        {
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);

            if (mDraggingDock != null)            
                DrawDraggingDock(g);            
        }

        public override void Update()
        {
            base.Update();
            Simplify();            
        }
    }
}
