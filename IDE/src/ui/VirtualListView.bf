using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.theme.dark;
using Beefy.widgets;

namespace IDE.ui
{
    /*public class VirtualListViewItem : DarkListViewItem
    {
        public VirtualListViewItem mVirtualHeadItem;
        public int32 mVirtualCount; // Including head item
        public int32 mVirtualIdx;
        public bool mDisabled = false;

        public this(VirtualListView listView)
            : base(listView)
        {            
        }
		
        public override void Update()
        {
            base.Update();

            if (mParentItem == null)
                return;

            var virtualListView = (VirtualListView)mListView;

            if (mParentItem.mChildAreaHeight != 0)
            {
                float itemHeight = virtualListView.mFont.GetLineSpacing();
                if (mVirtualHeadItem == this)
                {
                    /*float ofsX;
                    float ofsY;
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);*/

                    float ofsX;
                    float ofsY;
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);
                    ofsY -= mListView.mVertPos.mDest + mListView.mScrollContent.mY;

                    int32 curMemberIdx = 0;
                    VirtualListViewItem prevVirtualListViewItem = null;
                    VirtualListViewItem nextVirtualListViewItem = (VirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
                    
                    int32 showCount = mVirtualCount;

                    float curY = mY;
                    float prevY = curY;
                    float lastBottomPadding = 0;
                    for (int32 idx = 0; idx < showCount; idx++)
                    {
                        VirtualListViewItem curVirtualListViewItem = null;

                        if ((nextVirtualListViewItem != null) && (idx == nextVirtualListViewItem.mVirtualIdx))
                        {
                            curVirtualListViewItem = nextVirtualListViewItem;
                            curMemberIdx++;
                            if (curMemberIdx < mParentItem.mChildItems.Count)
                            {
                                nextVirtualListViewItem = (VirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
                                if (nextVirtualListViewItem.mVirtualHeadItem != this)
                                    nextVirtualListViewItem = null;
                                if (nextVirtualListViewItem != null)
                                    lastBottomPadding = nextVirtualListViewItem.mBottomPadding;
                            }
                            else
                                nextVirtualListViewItem = null;
                        }

                        bool wantsFillIn = (curY + ofsY + itemHeight >= 0) && (curY + ofsY < mListView.mHeight);
                        bool wantsDelete = !wantsFillIn;

                        if (mDisabled)
                        {
                            wantsFillIn = false;
                            wantsDelete = false;
                        }

                        /*bool wantsFillIn = (Utils.Rand() % 1000) == 0;
                        bool wantsDelete = (Utils.Rand() % 1000) == 0;*/

                        if ((curVirtualListViewItem == null) && (wantsFillIn))
                        {
                            prevVirtualListViewItem.mBottomPadding = (curY - prevVirtualListViewItem.mY) - prevVirtualListViewItem.mSelfHeight - prevVirtualListViewItem.mChildAreaHeight;
                            curVirtualListViewItem = (VirtualListViewItem)mParentItem.CreateChildItemAtIndex(curMemberIdx);
                            curVirtualListViewItem.mVisible = false;
                            curVirtualListViewItem.mX = mX;
                            curVirtualListViewItem.mVirtualHeadItem = this;
                            curVirtualListViewItem.mVirtualIdx = idx;
                            virtualListView.PopulateVirtualItem(curVirtualListViewItem);                            
                            curMemberIdx++;
                        }

                        if ((wantsDelete) && (idx != 0) && (curVirtualListViewItem != null) && (curVirtualListViewItem.mChildAreaHeight == 0))
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
                                prevVirtualListViewItem.mBottomPadding = (curY - prevY) - prevVirtualListViewItem.mSelfHeight - prevVirtualListViewItem.mChildAreaHeight;
                        }

                        if (curVirtualListViewItem != null)
                            prevY = curY;

                        curY += itemHeight;
                        if (curVirtualListViewItem != null)
                        {
                            curY += curVirtualListViewItem.mChildAreaHeight;
                            prevVirtualListViewItem = curVirtualListViewItem;
                        }
                    }
                   
                    if (prevVirtualListViewItem != null)
                    {
                        if (mDisabled)
                            prevVirtualListViewItem.mBottomPadding = 0;
                        else
                            prevVirtualListViewItem.mBottomPadding = (curY - prevY) - prevVirtualListViewItem.mSelfHeight - prevVirtualListViewItem.mChildAreaHeight;

                        if (prevVirtualListViewItem.mBottomPadding != lastBottomPadding)
                            mListView.mListSizeDirty = true;
                    }

                    while (curMemberIdx < mParentItem.mChildItems.Count)
                    {
                        var curVirtualListViewItem = (VirtualListViewItem)mParentItem.mChildItems[curMemberIdx];
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

    public class VirtualListView : IDEListView
    {
        protected override ListViewItem CreateListViewItem()
        {            
            var anItem = new VirtualListViewItem(this);
            return anItem;
        }

        public virtual void PopulateVirtualItem(VirtualListViewItem item)
        {
            
        }
    }*/
}
