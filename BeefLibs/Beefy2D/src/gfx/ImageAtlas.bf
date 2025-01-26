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

	public Image Alloc(int32 width, int32 height)
	{
		Page page = null;
		if (!mPages.IsEmpty)
		{
			page = mPages.Back;
			if (page.mCurX + (int)width > page.mImage.mSrcWidth)
			{
				// Move down to next row
				page.mCurX = 0;
				page.mCurY += page.mMaxRowHeight;
				page.mMaxRowHeight = 0;
			}

			if (page.mCurY + height > page.mImage.mSrcHeight)
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

		Image image = page.mImage.CreateImageSegment(page.mCurX, page.mCurY, width, height);
		page.mCurX += width;
		page.mMaxRowHeight = Math.Max(page.mMaxRowHeight, height);
		return image;
	}
}