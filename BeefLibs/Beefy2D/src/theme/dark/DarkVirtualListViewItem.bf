using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.theme.dark;
using Beefy.widgets;
using System.Diagnostics;

namespace Beefy.theme.dark
{
    public class DarkVirtualListViewItem : DarkListViewItem
    {
        public DarkVirtualListViewItem mVirtualHeadItem;
        public int32 mVirtualCount; // Including head item
        public int32 mVirtualIdx;
        public bool mDisabled = false;
		public bool mUpdating = false;

		public ~this()
		{
			Debug.Assert(!mUpdating);
		}

		public virtual float GetItemHeight(int32 virtualIdx, float fontLineSpacing, DarkVirtualListViewItem listViewItem)
		{
			return fontLineSpacing;
		}

		public virtual bool IsDeleteAllowed(DarkVirtualListViewItem listViewItem, float fontLineSpacing)
		{
			// Don't allow deleting if we have children
			return listViewItem.mChildAreaHeight == 0;
		}

        public override void Update()
        {
			mUpdating = true;
			defer { mUpdating = false; }

            base.Update();

            if (mParentItem == null)
                return;

            var virtualListView = (DarkVirtualListView)mListView;

            if (mParentItem.mChildAreaHeight != 0)
            {
                float fontLineSpacing = virtualListView.mFont.GetLineSpacing();
                if (mVirtualHeadItem == this)
                {
                    float ofsX;
                    float ofsY;
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);
                    ofsY -= (float)(mListView.mVertPos.mDest + mListView.mScrollContent.mY);

                    int32 curMemberIdx = 0;
                    DarkVirtualListViewItem prevVirtualListViewItem = null;
                    DarkVirtualListViewItem nextVirtualListViewItem = (DarkVirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
                    
                    int32 showCount = mVirtualCount;

                    float curY = mY;
                    float prevY = curY;
                    float prevAdvance = 0;
                    float lastBottomPadding = 0;
                    for (int32 idx = 0; idx < showCount; idx++)
                    {
                        DarkVirtualListViewItem curVirtualListViewItem = null;

                        if ((nextVirtualListViewItem != null) && (idx == nextVirtualListViewItem.mVirtualIdx))
                        {
                            curVirtualListViewItem = nextVirtualListViewItem;
                            curMemberIdx++;
                            if (curMemberIdx < mParentItem.mChildItems.Count)
                            {
                                nextVirtualListViewItem = (DarkVirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
                                if (nextVirtualListViewItem.mVirtualHeadItem != this)
                                    nextVirtualListViewItem = null;
                                if (nextVirtualListViewItem != null)
                                    lastBottomPadding = nextVirtualListViewItem.mBottomPadding;
                            }
                            else
                                nextVirtualListViewItem = null;
                        }

						float itemHeight = GetItemHeight(idx, fontLineSpacing, ((curVirtualListViewItem != null) && (curVirtualListViewItem.mVirtualIdx == idx)) ? curVirtualListViewItem : null);

						float childHeight = 0;
						if (curVirtualListViewItem != null)
						{
							childHeight = curVirtualListViewItem.mChildAreaHeight;
						}

                        bool wantsFillIn = (curY + ofsY + itemHeight + childHeight >= 0) && (curY + ofsY < mListView.mHeight);
                        bool wantsDelete = !wantsFillIn;

                        if (mDisabled)
                        {
                            wantsFillIn = false;
                            wantsDelete = false;
                        }

                        if ((curVirtualListViewItem == null) && (wantsFillIn))
                        {
                            prevVirtualListViewItem.mBottomPadding = (curY - prevVirtualListViewItem.mY) - prevVirtualListViewItem.mSelfHeight - prevVirtualListViewItem.mChildAreaHeight;
                            curVirtualListViewItem = (DarkVirtualListViewItem)mParentItem.CreateChildItemAtIndex(curMemberIdx);
                            curVirtualListViewItem.mVisible = false;
                            curVirtualListViewItem.mX = mX;
                            curVirtualListViewItem.mVirtualHeadItem = this;
                            curVirtualListViewItem.mVirtualIdx = idx;
                            virtualListView.PopulateVirtualItem(curVirtualListViewItem);
                            curMemberIdx++;
                        }

						if ((wantsDelete) && (curVirtualListViewItem != null) && (curVirtualListViewItem.mIsSelected))
						{
							// Don't deselect items
							wantsDelete = false;
						}

                        if ((wantsDelete) && (idx != 0) && (curVirtualListViewItem != null) && (IsDeleteAllowed(curVirtualListViewItem, fontLineSpacing)))
                        {
                            curMemberIdx--;
                            mParentItem.RemoveChildItem(curVirtualListViewItem);
                            curVirtualListViewItem = null;
                        }

                        if (prevVirtualListViewItem != null)
                        {
                            if (mDisabled)
                                prevVirtualListViewItem.mBottomPadding = 0;
                            else
                                prevVirtualListViewItem.mBottomPadding = (curY - prevY) - prevAdvance;
                        }

                        if (curVirtualListViewItem != null)
                            prevY = curY;

                        curY += itemHeight;
                        if (curVirtualListViewItem != null)
                        {
                            curY += curVirtualListViewItem.mChildAreaHeight;
                            // What we actually advanced by, rather than mSelfHeight + mChildAreaHeight. An
                            // item reified during this pass was measured by GetItemHeight as an unreified
                            // subtree, and its mChildAreaHeight stays 0 until the list is sized afterwards --
                            // deriving the padding from those fields charges the difference as a visible gap.
                            prevAdvance = itemHeight + curVirtualListViewItem.mChildAreaHeight;
                            prevVirtualListViewItem = curVirtualListViewItem;
                        }
                    }
                   
                    if (prevVirtualListViewItem != null)
                    {
                        if (mDisabled)
                            prevVirtualListViewItem.mBottomPadding = 0;
                        else
                            prevVirtualListViewItem.mBottomPadding = (curY - prevY) - prevAdvance;

                        if (prevVirtualListViewItem.mBottomPadding != lastBottomPadding)
                            mListView.mListSizeDirty = true;
                    }


                    while ((curMemberIdx > 0) && (curMemberIdx < mParentItem.mChildItems.Count))
                    {
                        var curVirtualListViewItem = (DarkVirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
                        if (curVirtualListViewItem.mVirtualHeadItem != this)
                            break;
                        mParentItem.RemoveChildItem(curVirtualListViewItem);
                        if (mParentItem == null) // Last item
                            return;
                    }
                }
            }
        }
    }

    public class DarkVirtualListView : DarkListView
    {
        protected override ListViewItem CreateListViewItem()
        {            
            var anItem = new DarkVirtualListViewItem();
            return anItem;
        }

        public virtual void PopulateVirtualItem(DarkVirtualListViewItem item)
        {
            
        }
    }
}
