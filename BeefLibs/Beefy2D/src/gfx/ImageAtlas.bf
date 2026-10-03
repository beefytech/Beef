using System.Collections;
using System;
namespace Beefy.gfx;

class ImageAtlas
{
	public class Page
	{
		public Image mImage ~ delete _;
		public int32 mCurX;
		public int32 mCurY;
		public int32 mMaxRowHeight;
	}

	public List<Page> mPages = new .() ~ DeleteContainerAndItems!(_);
	public bool mAllowMultiplePages = true;
	
	public int32 mImageWidth = 1024;
	public int32 mImageHeight = 1024;

	public this()
	{

	}

	// guard: transparent texels kept around the segment, so a filtered (scaled, rotated) draw of it never samples a
	// neighbour or the page's unwritten fill.
	public Image Alloc(int32 width, int32 height, int32 guard = 0)
	{
		int32 cellWidth = width + guard * 2;
		int32 cellHeight = height + guard * 2;
		Page page = null;
		if (!mPages.IsEmpty)
		{
			page = mPages.Back;
			if (page.mCurX + (int)cellWidth > page.mImage.mSrcWidth)
			{
				// Move down to next row
				page.mCurX = 0;
				page.mCurY += page.mMaxRowHeight;
				page.mMaxRowHeight = 0;
			}

			if (page.mCurY + cellHeight > page.mImage.mSrcHeight)
			{
				// Doesn't fit
				page = null;
			}
		}

		if (page == null)
		{
			page = new .();
			page.mImage = Image.CreateDynamic(mImageWidth, mImageHeight);

			uint32* colors = new uint32[mImageWidth*mImageHeight]*;
			defer delete colors;
			for (int i < mImageWidth*mImageHeight)
				colors[i] = 0xFF000000 | (.)i;

			page.mImage.SetBits(0, 0, mImageWidth, mImageHeight, mImageWidth, colors);
			mPages.Add(page);
		}

		if (guard > 0)
		{
			// The page starts filled with debug colours.
			uint32* clear = new uint32[cellWidth * cellHeight]*;
			defer delete clear;
			Internal.MemSet(clear, 0, cellWidth * cellHeight * sizeof(uint32));
			page.mImage.SetBits(page.mCurX, page.mCurY, cellWidth, cellHeight, cellWidth, clear);
		}

		Image image = page.mImage.CreateImageSegment(page.mCurX + guard, page.mCurY + guard, width, height);
		page.mCurX += cellWidth;
		page.mMaxRowHeight = Math.Max(page.mMaxRowHeight, cellHeight);
		return image;
	}
}