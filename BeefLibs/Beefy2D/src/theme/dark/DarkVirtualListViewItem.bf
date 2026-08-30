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

		// A fresh head is its range's only row until the first reify pass, so the list would measure
		// short by the unreified tail -- and the scrollbar clamp turns that into a scroll jump.
		// The reify pass replaces this estimate with exact values.
		public void SeedVirtualHeight()
		{
			var virtualListView = (DarkVirtualListView)mListView;
			float fontLineSpacing = virtualListView.mFont.GetLineSpacing();
			float tail = 0;
			for (int32 idx = 1; idx < mVirtualCount; idx++)
				tail += GetItemHeight(idx, fontLineSpacing, null);
			mBottomPadding = tail;
			mListView.mListSizeDirty = true;
		}

		public virtual bool IsDeleteAllowed(DarkVirtualListViewItem listViewItem, float fontLineSpacing)
		{
			// Don't allow deleting if we have children
			return listViewItem.mChildAreaHeight == 0;
		}

        public override void Update()
        {
            base.Update();
			UpdateVirtualItems();
        }

		// The reify/reap pass over this head's range. Also runs out-of-band: scroll input lands
		// between update and draw, and the draw must never see unreified rows.
		public void UpdateVirtualItems()
		{
			if (mUpdating)
				return;
			mUpdating = true;
			defer { mUpdating = false; }

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
					// Embeds the ANIMATED scroll position, not mVertPos.mDest: windowing on the
					// destination would reify a smooth scroll's whole span at once.
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);

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
							// Re-measure as unreified so curY still advances by the deleted subtree's height
							itemHeight = GetItemHeight(idx, fontLineSpacing, null);
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
                            // The actual advance, not mSelfHeight + mChildAreaHeight: a just-reified
                            // item's mChildAreaHeight is still 0, and padding derived from it gaps.
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

		// Handle scroll position even if we don't get another update before the draw
		public override void UpdateContentPosition()
		{
			base.UpdateContentPosition();
			UpdateVirtualItems();
		}

		// Runs every virtual head's reify/reap pass. Index-based on purpose: a head's pass inserts
		// and reaps siblings in the very lists being walked. A skipped slot is caught next frame.
		public void UpdateVirtualItems()
		{
			void UpdateTree(ListViewItem item)
			{
				if (var virtualItem = item as DarkVirtualListViewItem)
				{
					if (virtualItem.mVirtualHeadItem == virtualItem)
						virtualItem.UpdateVirtualItems();
				}
				if (item.mChildItems != null)
				{
					for (int32 i = 0; i < item.mChildItems.Count; i++)
						UpdateTree(item.mChildItems[i]);
				}
			}

			var root = GetRoot();
			if (root != null)
				UpdateTree(root);
		}
    }
}
