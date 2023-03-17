
using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using Beefy.gfx;
using Beefy.events;

namespace Beefy.widgets
{   
    public abstract class ListViewItem : Widget
    {
        public ListView mListView;
        public String mLabel ~ delete _;
        public IDrawable mIconImage;
        public int32 mColumnIdx;
        public float mSelfHeight; // Designed height only for ourselves, not including children
        public float mChildAreaHeight;
        public float mBottomPadding;
        public int32 mDepth;
        public float mShowChildPct;
        public uint32 mIconImageColor;

        public ListViewItem mParentItem;
        public List<ListViewItem> mSubItems; // de DeleteContainerAndItems!(_);
        public List<ListViewItem> mChildItems ~ DeleteContainerAndItems!(_); // Tree view        

		//int mIdx = sIdx++;
		//static int sIdx = 0;

        public this()
        {
        }

		public ~this()
		{
			if (mColumnIdx == 0)
				delete mSubItems;
		}
        
        virtual public StringView Label 
        { 
            get { return (mLabel != null) ? mLabel : default; } 
            set { String.NewOrSet!(mLabel, value); } 
        }
        virtual public IDrawable IconImage { get { return mIconImage; } set { mIconImage = value; } }
        virtual public uint32 IconImageColor { get { return mIconImageColor; } set { mIconImageColor = value; } }
        protected bool mIsSelected;
        protected bool mIsFocused;
        virtual public bool Selected 
        {
            get
            {
                return mIsSelected;
            }
            set
            {
				if (mIsSelected != value)
				{
	                mIsSelected = value;
					MarkDirty();
				}

				if ((!mIsSelected) && (mIsFocused))
				{
					mIsFocused = false;
					MarkDirty();
				}
            }
        }

        virtual public bool Focused
        {
            get
            {
                return mIsFocused;
            }
            set
            {                
                if (mIsFocused != value)
                {
                    Selected = value;
                    mIsFocused = value;
                    if (mListView.mOnFocusChanged.HasListeners)
                        mListView.mOnFocusChanged(this);
					MarkDirty();
                }
            }
        }

        virtual public bool IsParent
        {
            get
            {
                return mChildItems != null;
            }

            set
            {
                if (value)
                    MakeParent();
                else if (IsParent)
                    Runtime.FatalError("Cannot undo parent status");
            }
        }

		public virtual bool IsOpen
		{
			get
			{
				return (mChildItems != null) && (mChildItems.Count > 0) && (mChildAreaHeight > 0);
			}
		}

		public virtual bool IsDragging
		{
			get
			{
				return false;
			}
		}

        public virtual float LabelX { get { return 0; } }
        public virtual float LabelWidth { get { return mWidth; } }

		public virtual void Init(ListView listView)
		{

		}

		protected override void RemovedFromWindow()
		{
			base.RemovedFromWindow();
			if (mChildItems != null)
			{
				for (var child in mChildItems)
					child.RemovedFromWindow();
			}
			if ((mColumnIdx == 0) && (mSubItems != null))
			{
				for (var subItem in mSubItems)
				{
					if (subItem != this)
						subItem.RemovedFromWindow();
				}
			}
		}

        public void WithItems(delegate void(ListViewItem) func)
        {                                    
            if (mChildItems != null)
            {                
                for (ListViewItem child in mChildItems)
                {
                    func(child);                    
                    child.WithItems(func);
                }
            }
        }

        public void WithSelectedItems(delegate void(ListViewItem) func, bool skipSelectedChildrenOnSelectedItems = false, bool skipClosed = false)
        {
            bool selfSelected = Selected;
            if (selfSelected)
                func(this);

            if ((mChildItems != null) && ((!skipSelectedChildrenOnSelectedItems) || (!selfSelected)))
            {
				if ((!skipClosed) || (mParentItem == null) || (IsOpen))
				{
					for (int i < mChildItems.Count)
	                {
						var child = mChildItems[i];
	                    child.WithSelectedItems(func, skipSelectedChildrenOnSelectedItems, skipClosed);
						if ((i < mChildItems.Count) && (mChildItems[i] != child))
							i--;
	                }
				}
            }
        }
        
        public ListViewItem FindFirstSelectedItem()
        {
            if (Selected)
                return this;

            if (mChildItems != null)
            {
                for (ListViewItem child in mChildItems)
                {
                    ListViewItem selected = child.FindFirstSelectedItem();
                    if (selected != null)
                        return selected;
                }
            }

            return null;
        }

		public int32 GetSelectedItemCount()
		{
		    int32 count = 0;
			WithSelectedItems(scope [&] (item) =>
				{
					count++;
				});
		    return count;
		}

        public ListViewItem FindLastSelectedItem(bool filterToVisible = false)
        {
            ListViewItem selected = null;
            if (mIsSelected && (!filterToVisible || mVisible))
                selected = this;

            if (mChildItems != null)
            {                
                for (ListViewItem child in mChildItems)
                {
                    ListViewItem checkSelected = child.FindLastSelectedItem(filterToVisible);
                    if (checkSelected != null)
                        selected = checkSelected;
                }
            }            

            return selected;
        }

        public ListViewItem FindFocusedItem()
        {            
            ListViewItem selectedListViewItem = null;
            WithSelectedItems(scope [&] (listViewItem) =>
                {                    
                    if ((listViewItem.mIsFocused) || (selectedListViewItem == null))
                        selectedListViewItem = listViewItem;                    
                });
            
            return selectedListViewItem;            
        }

        public void SelectItemExclusively(ListViewItem item)
        {            
            WithSelectedItems(scope (listViewItem) =>
                {
                    if (listViewItem != item)
                        listViewItem.Selected = false;
                });


            if (item != null)
            {
                item.Selected = true;
                item.Focused = true;
            }
        }

        public void SelectItem(ListViewItem item, bool checkKeyStates = false)
        {
            if (item == null)
            {
                SelectItemExclusively(null);
                return;
            }

            if (item != null)
            {
                if ((mListView.mAllowMultiSelect) && (checkKeyStates) && (mWidgetWindow.IsKeyDown(KeyCode.Shift)))
                {
                    var focusedItem = FindFocusedItem();
					if ((focusedItem != item) && (focusedItem != null))
					{
						ListViewItem spanStart = null;
						ListViewItem selectEndElement = null;
						ListViewItem prevItem = null;
						WithItems(scope [&] (checkItem) =>
							{
								defer { prevItem = checkItem; }

								/*if (selectEndElement != null)
									return;*/

								if (checkItem == focusedItem)
								{
									if (spanStart != null)
									{
										// Done. Select from end.
										selectEndElement = spanStart;
										return;
									}
									spanStart = checkItem;
									return;
								}

								if (spanStart != null)
								{
									if (!checkItem.Selected)
									{
										if (spanStart == focusedItem)
										{
											selectEndElement = prevItem;
										}
										spanStart = null;
									}
								}
								else if (spanStart == null)
								{
									if (checkItem.Selected)
									{
										spanStart = checkItem;
									}
									
								}
							});

						focusedItem.Focused = false;
						bool foundOldEnd = false;
						bool foundNewHead = false;
						WithItems(scope [?] (checkItem) =>
							{
								checkItem.Selected = foundNewHead ^ foundOldEnd;
								if (checkItem == item)
								{
									foundNewHead = true;
									checkItem.Selected = true;
								}
								if (checkItem == selectEndElement)
								{
									foundOldEnd = true;
									checkItem.Selected = true;
								}
							});
						item.Focused = true;

						return;
					}
                }

                if ((mListView.mAllowMultiSelect) && (checkKeyStates) && (mWidgetWindow.IsKeyDown(KeyCode.Control)))
                {                    
                    if (!item.Focused)
                        item.Selected = !item.Selected;
                }
                else
                {
					if ((item.Selected) && (item.mMouseOver))
					{
						item.mOnMouseUp.AddFront(new => ItemMouseUpHandler);

						var focusedItem = FindFocusedItem();
						if (focusedItem != null)
						{
							focusedItem.Focused = false;
							focusedItem.Selected = true;
						}
						item.Focused = true;
					}
					else
					{
						SelectItemExclusively(item);
						if (item.Selected)
							item.Focused = true;
					}
                    //SelectItemExclusively(item);
                    //if (item.Selected)
                        //item.Focused = true;
                }
            }
        }

		void ItemMouseUpHandler(MouseEvent evt)
		{
			var item = evt.mSender as ListViewItem;
			if ((item.mMouseOver) && (!item.IsDragging))
			{
				SelectItemExclusively(item);
				if (item.Selected)
					item.Focused = true;
			}

			item.mOnMouseUp.Remove(scope => ItemMouseUpHandler, true);
		}

        public virtual void Clear()
        {
            if (mChildItems == null)
                return;
            for (var childItem in mChildItems)
			{
				childItem.DoRemove(false);
				delete childItem;
			}
			mChildItems.Clear();
        }

		void DoRemove(bool removeFromList)
		{
			if ((mSubItems != null) && (mColumnIdx == 0))
			{
			    for (int32 i = 1; i < mSubItems.Count; i++)
				{
					var subItem = mSubItems[i];
			        subItem.Remove();
					delete subItem;
				}
				mSubItems.Clear();
			}

			if (mColumnIdx == 0)
			{
				if (removeFromList)
				{
					mListView.mListSizeDirty = true;
					mParentItem.mChildItems.Remove(this);
				}
			}
			else
			{
				base.RemoveSelf();
			}

			mParentItem = null;
			RemovedFromWindow();
		}

        public virtual void Remove()
        {
            DoRemove(true);
        }

		public int GetSubItemCount()
		{
			if (mSubItems == null)
				return 0;
			return mSubItems.Count;
		}

		public void InitSubItem(int columnIdx, ListViewItem subItem)
		{
		    Debug.Assert(columnIdx > 0);            
		    if (mSubItems == null)
		    {
		        mSubItems = new List<ListViewItem>();
		        mSubItems.Add(this); // Add ourselves to column zero
		    }
		    while (columnIdx >= mSubItems.Count)
		        mSubItems.Add(null);

		    subItem.mDepth = mDepth;
		    subItem.mListView = mListView;
		    subItem.mSubItems = mSubItems; // Share with column zero
		    subItem.mColumnIdx = (.)columnIdx;
		    subItem.mParentItem = mParentItem;
		    mSubItems[columnIdx] = subItem;
		    /*mParentItem.*/AddWidget(subItem);

		    mListView.mListSizeDirty = true;
		}

        public ListViewItem CreateSubItem(int columnIdx)
        {
            ListViewItem subItem = mListView.CreateListViewItem_Internal();
			InitSubItem(columnIdx, subItem);
            return subItem;
        }
        
        public ListViewItem GetMainItem()
        {
            if (mColumnIdx == 0)
                return this;
            return mSubItems[0];
        }

        public ListViewItem GetSubItem(int columnIdx)
        {
			if (mColumnIdx == columnIdx)
				return this;
            return mSubItems[columnIdx];
        }

		public ListViewItem GetOrCreateSubItem(int columnIdx)
		{
			if (mColumnIdx == columnIdx)
				return this;
			if ((mSubItems != null) && (columnIdx < mSubItems.Count))
		    	return mSubItems[columnIdx];
			return CreateSubItem(columnIdx);
		}

        public virtual void MakeParent()
        {
            if (mChildItems == null)
                mChildItems = new List<ListViewItem>();
        }

		public virtual void TryUnmakeParent()
		{
			if ((mChildItems != null) && (mChildItems.Count == 0))
			{
				DeleteAndNullify!(mChildItems);
			}
		}

        public virtual bool Open(bool open, bool immediate = false)
        {
            return false;
        }

		public override void InitChildren()
		{
			base.InitChildren();
			if (mChildItems != null)
			{
				for (var item in mChildItems)
				{
					item.mWidgetWindow = mWidgetWindow;
					item.InitChildren();
				}
			}
		}

        public virtual ListViewItem CreateChildItemAtIndex(int idx)
        {
            MakeParent();

            ListViewItem child = mListView.CreateListViewItem_Internal();
            child.mListView = mListView;
            child.mParentItem = this;
            child.mDepth = mDepth + 1;
            child.mVisible = mShowChildPct > 0.0f;
            mChildItems.Insert(idx, child);
            AddWidgetUntracked(child);

            mListView.mListSizeDirty = true;

            return child;
        }

        public virtual ListViewItem CreateChildItem()
        {
            return CreateChildItemAtIndex((mChildItems != null) ? mChildItems.Count : 0);
        }

        public int GetIndexOfChild(ListViewItem item)
        {
            return mChildItems.IndexOf(item);
        }

        public virtual void RemoveChildItemAt(int idx, bool deleteItem = true)
        {
            var item = mChildItems[idx];
            mChildItems.RemoveAt(idx);

            RemoveWidget(item);
            item.mParentItem = null;
            item.mListView = null;

            mListView.mListSizeDirty = true;
			if (deleteItem)
				delete item;
        }

        public virtual void RemoveChildItem(ListViewItem item, bool deleteItem = true)
        {
            int idx = mChildItems.IndexOf(item);
            RemoveChildItemAt(idx, deleteItem);
        }

        protected void SetDepth(int32 depth)
        {
            if (mDepth == depth)
                return;

            mDepth = depth;
            
            if (mSubItems != null)
                for (ListViewItem anItem in mSubItems)                
                    anItem.SetDepth(depth);                
            if (mChildItems != null)
                for (ListViewItem anItem in mChildItems)
                    anItem.SetDepth(depth + 1);
        }

        public virtual void AddChildAtIndex(int index, ListViewItem item)
        {
            mChildItems.Insert(index, item);
            
            AddWidgetUntracked(item);

            item.mParentItem = this;
            item.mListView = mListView;
            item.SetDepth(mDepth + 1);
            mListView.mListSizeDirty = true;
        }

        public virtual void InsertChild(ListViewItem item, ListViewItem insertBefore)
        {
            int pos = (insertBefore != null) ? mChildItems.IndexOf(insertBefore) : mChildItems.Count;
            AddChildAtIndex(pos, item);
        }

        public virtual void AddChild(ListViewItem item, ListViewItem addAfter = null)
        {
            int pos = (addAfter != null) ? (mChildItems.IndexOf(addAfter) + 1) : mChildItems.Count;
            AddChildAtIndex(pos, item);
        }

        public int GetChildCount()
        {
            if (mChildItems == null)
                return 0;
            return mChildItems.Count;
        }

        public ListViewItem GetChildAtIndex(int idx)
        {
            return mChildItems[idx];
        }

        public virtual float CalculatedDesiredHeight()
        {            
            mChildAreaHeight = 0;

            if (mChildItems != null)
            {
                for (ListViewItem listViewItem in mChildItems)
                    mChildAreaHeight += listViewItem.CalculatedDesiredHeight();
            }

            mChildAreaHeight *= mShowChildPct;

            return mChildAreaHeight + mSelfHeight + mBottomPadding;
        }

        public virtual float ResizeComponents(float xOffset)
        {            
            float curY = 0;
            
            if ((mSubItems != null) && (mColumnIdx == 0))
            {
                float curX = -mX - xOffset;
                int columnCount = Math.Min(mSubItems.Count, mListView.mColumns.Count);
                for (int32 aColumnIdx = 0; aColumnIdx < columnCount; aColumnIdx++)
                {
                    ListViewColumn aColumn = mListView.mColumns[aColumnIdx];
                    if (aColumnIdx > 0)
                    {
                        ListViewItem subItem = mSubItems[aColumnIdx];
                        if (subItem != null)
                        {                            
                            subItem.mVisible = mVisible;
                            if (aColumnIdx == columnCount - 1)
                                subItem.Resize(curX, /*mY*/0, Math.Max(aColumn.mWidth, mWidth - curX), mHeight);
                            else
                            {
                                float aWidth = aColumn.mWidth;
                                subItem.Resize(curX, /*mY*/0, aWidth, mHeight);
                            }
                            if (aColumnIdx > 0)
                                subItem.ResizeComponents(xOffset);
                        }
                    }
                    
                    curX += aColumn.mWidth;
                }
            }

            curY += mSelfHeight;

            if (mChildItems != null)
            {
                for (ListViewItem child in mChildItems)
                {
                    child.mVisible = (mShowChildPct > 0.0f);
                    child.ResizeClamped(child.mX, curY, mWidth - child.mX, child.mSelfHeight);
                    float resizeXOfs = xOffset;
                    if (mParentItem != null)
                        resizeXOfs += mX;
                    float childSize = (int)child.ResizeComponents(resizeXOfs);
                    curY += childSize;
                }
            }

            mChildAreaHeight = CalculatedDesiredHeight() - mSelfHeight - mBottomPadding;
            return mChildAreaHeight + mSelfHeight + mBottomPadding;
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            
        }

		public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
		{
			base.MouseClicked(x, y, origX, origY, btn);
			if (mParentItem != null) // Don't notify for root
				mListView.mOnItemMouseClicked(this, x, y, btn);
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			if (mParentItem != null) // Don't notify for root
				mListView.mOnItemMouseDown(this, x, y, btn, btnCount);
		}
    }

    public class ListViewColumn
    {
        public String mLabel ~ delete _;
        public float mWidth;
        public float mMinWidth;
        public float mMaxWidth;

		public String Label
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
    }

    public abstract class ListView : ScrollableWidget
    {
        public List<ListViewColumn> mColumns = new List<ListViewColumn>() ~ DeleteContainerAndItems!(_);
        protected ListViewItem mRoot;
        public bool mListSizeDirty;
        public float mHeaderHeight;
        public Event<delegate void(ListViewItem)> mOnFocusChanged ~ _.Dispose();
        public float mBottomInset = 8;
        public bool mAllowMultiSelect = true;
		public Event<delegate void(ListViewItem item, float x, float y, int32 btnNum, int32 btnCount)> mOnItemMouseDown ~ _.Dispose();
		public Event<delegate void(ListViewItem item, float x, float y, int32 btnNum)> mOnItemMouseClicked ~ _.Dispose();

        public this()
        {
            mRoot = CreateListViewItem();            
            mRoot.mSelfHeight = 0;
            mRoot.mListView = this;
            mListSizeDirty = true;
            mScrollContent = mRoot;
            mScrollContentContainer.AddWidget(mRoot);
        }

        public virtual ListViewColumn AddColumn(float width, String label)
        {
            ListViewColumn aColumn = new ListViewColumn();
            aColumn.mWidth = width;
            aColumn.Label = label;
            aColumn.mMinWidth = 64;
            mColumns.Add(aColumn);
            return aColumn;
        }

        protected virtual ListViewItem CreateListViewItem()
        {                        
            return null;
        }

        public virtual ListViewItem CreateListViewItem_Internal()
        {
            var listViewItem = CreateListViewItem();
			listViewItem.Init(this);
			return listViewItem;
        }

        public virtual ListViewItem GetRoot()
        {
            return mRoot;            
        }

        protected virtual void ColumnResized(ListViewColumn column)
        {
            mListSizeDirty = true;
        }

        public override void Update()
        {
            base.Update();            
        }

        public override void UpdateAll()
        {
            base.UpdateAll();
            UpdateListSize();
        }

		public override void UpdateFAll(float updatePct)
		{
		    base.UpdateFAll(updatePct);
		    UpdateListSize();
		}

        public virtual float GetListWidth()
        {
            float columnWidths = 0;
            for (ListViewColumn aColumn in mColumns)
                columnWidths += aColumn.mWidth;
            return columnWidths;
        }

        public void UpdateListSize()
        {
            // Do this in UpdateAll to give children a chance to resize items
            if (mListSizeDirty)
            {
				float listWidth = GetListWidth();
				mRoot.mWidth = Math.Max(listWidth, mScrollContentContainer.mWidth);

                mRoot.mHeight = mRoot.CalculatedDesiredHeight() + mBottomInset;
                mRoot.ResizeComponents(0);

                UpdateScrollbarData();
                mListSizeDirty = false;
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
			if ((x == mX) && (y == mY) && (width == mWidth) && (height == mHeight))
				return;

            base.Resize(x, y, width, height);
            mListSizeDirty = true;
        }

        public void Resized()
        {
            mListSizeDirty = true;
        }

		public enum VisibleKind
		{
			WasVisible,
			Scrolled
		}

        public VisibleKind EnsureItemVisible(ListViewItem item, bool centerView)
        {
            if (mVertScrollbar == null)
                return .WasVisible;

            if (mListSizeDirty)
                UpdateListSize();

			if (mScrollContentContainer.mHeight <= 0)
				return .WasVisible;

            float aX;
            float aY;
            item.SelfToOtherTranslate(mScrollContent, 0, 0, out aX, out aY);

            float topInsets = 0;
            float lineHeight = item.mSelfHeight;

            if (aY < mVertPos.mDest + topInsets)
            {
                float scrollPos = aY - topInsets;
                if (centerView)
                {
                    scrollPos -= mScrollContentContainer.mHeight * 0.50f;
                    scrollPos = (float)Math.Round(scrollPos / lineHeight) * lineHeight;
                }
                VertScrollTo(scrollPos);
				return .Scrolled;
            }
            else if (aY + lineHeight + mBottomInset >= mVertPos.mDest + mScrollContentContainer.mHeight)
            {                
                float scrollPos = aY + lineHeight + mBottomInset - mScrollContentContainer.mHeight;
                if (centerView)
                {
                    // Show slightly more content on bottom
                    scrollPos += mScrollContentContainer.mHeight * 0.50f;
                    scrollPos = (float)Math.Round(scrollPos / lineHeight) * lineHeight;
                }
                VertScrollTo(scrollPos);
				return .Scrolled;
            }
			return .WasVisible;
        }

        ListViewItem FindClosestItemAtYPosition(ListViewItem parentItem, float y, bool addHeight)
        {
            if (parentItem.mChildItems != null)
            {
                for (var item in parentItem.mChildItems)
                {
                    var closestItem = FindClosestItemAtYPosition(item, y + item.mY, addHeight);
                    if (closestItem != null)
                        return closestItem;
                }
            }

            if ((y >= 0) && (y < parentItem.mSelfHeight))
                return parentItem;
            return null;
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);

			switch (keyCode)
			{
			case (KeyCode)'A':
			    if ((mAllowMultiSelect) && (mWidgetWindow.GetKeyFlags() == KeyFlags.Ctrl))
			    {
			        mRoot.WithItems(scope (listViewItem) =>
			            {
			                listViewItem.Selected = true;
			            });
			    }
			    break;
			default:
			}

            bool isDoingSpanSelection = mAllowMultiSelect && mWidgetWindow.IsKeyDown(KeyCode.Shift);

            //ListViewItem firstSelectedItem = isDoingSpanSelection ? mRoot.FindFirstSelectedItem() : mRoot.FindFocusedItem();
			ListViewItem firstSelectedItem = mRoot.FindFocusedItem();

            if (firstSelectedItem != null)
            {
                ListViewItem selectedItem = firstSelectedItem;
                
                bool triedMove = false;
                ListViewItem newSelection = null;
                switch (keyCode)
                {
                case KeyCode.Home:
                    newSelection = GetRoot().mChildItems[0];
                case KeyCode.End:
                    newSelection = GetRoot();
                    while (newSelection.mChildAreaHeight > 0)
                    {
                        newSelection = newSelection.mChildItems[newSelection.mChildItems.Count - 1];
                    }    
                case KeyCode.PageUp:
					selectedItem.SelfToOtherTranslate(mScrollContent, 0, 0, var absX, var absY);

                    int32 numIterations = (int32)(mScrollContentContainer.mHeight / selectedItem.mSelfHeight);
                    for (int32 i = 0; i < numIterations; i++)
                        KeyDown(KeyCode.Up, false);                            
                case KeyCode.PageDown:
                    int32 numIterations = (int32)(mScrollContentContainer.mHeight / selectedItem.mSelfHeight);
                    for (int32 i = 0; i < numIterations; i++)
                        KeyDown(KeyCode.Down, false);
                case KeyCode.Up:
					int idx = selectedItem.mParentItem.mChildItems.IndexOf(selectedItem);
					if (idx > 0)
					{
					    newSelection = selectedItem.mParentItem.mChildItems[idx - 1];
					    while (newSelection.mChildAreaHeight > 0)
					        newSelection = newSelection.mChildItems[newSelection.mChildItems.Count - 1];
					}
					else if (selectedItem.mParentItem != mRoot)
					{
					    newSelection = selectedItem.mParentItem;
					}
					triedMove = true;
                case KeyCode.Down:
                    if ((selectedItem.IsOpen) && (!selectedItem.mChildItems.IsEmpty))
					{
                        newSelection = selectedItem.mChildItems[0];
					}
                    else
                    {
                        while (selectedItem != mRoot)
                        {
                            var childItems = selectedItem.mParentItem.mChildItems;
                            int idx = childItems.IndexOf(selectedItem);
                            if (idx < childItems.Count - 1)
                            {
                                newSelection = childItems[idx + 1];
                                break;
                            }

                            selectedItem = selectedItem.mParentItem;
                        }
                    }
                    triedMove = true;
                case KeyCode.Left:
                    if (!selectedItem.Open(false))
                    {
                        if (selectedItem.mParentItem != GetRoot())
                            newSelection = selectedItem.mParentItem;
                    }
                case KeyCode.Right:
                    if (!selectedItem.Open(true))
                    {
                        if ((selectedItem.mChildItems != null) && (selectedItem.mChildItems.Count > 0))
                            newSelection = selectedItem.mChildItems[0];
                    }
				case KeyCode.Space:
					if (!selectedItem.Open(false))
						selectedItem.Open(true);
				case KeyCode.Return:
					var mouseEvent = scope Beefy.events.MouseEvent();
					mouseEvent.mSender = selectedItem;
					mouseEvent.mBtnCount = 2;
					selectedItem.mOnMouseDown(mouseEvent);
					mOnItemMouseDown(selectedItem, 0, 0, 0, 2);
				default:
                }

                if (newSelection != null)
                {
                    mRoot.SelectItem(newSelection, isDoingSpanSelection);
                    if (EnsureItemVisible(newSelection, false) == .Scrolled)
						newSelection.mParent.UpdateAll(); // Update virtual list
                }
                else if ((triedMove) && (!isDoingSpanSelection) && (firstSelectedItem != null))
                    mRoot.SelectItemExclusively(firstSelectedItem);
            }
        }
    }
}
