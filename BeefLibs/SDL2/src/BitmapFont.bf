using System.Collections;

namespace SDL2
{
	class BitmapFont
	{
		public class Page
		{
			public Image mImage ~ delete _;
		}

		public struct CharData
		{
			public int32 mX;
			public int32 mY;
			public int32 mWidth;
			public int32 mHeight;
			public int32 mXOfs;
			public int32 mYOfs;
			public int32 mXAdvance;
			public int32 mPage;
		}

		public List<Page> mPages = new .() ~ DeleteContainerAndItems!(_);
		public List<CharData> mCharData = new .() ~ delete _;
			
		public this()
		{

		}
	}
}