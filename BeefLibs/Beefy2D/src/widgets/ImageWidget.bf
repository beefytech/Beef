using System;
using System.Collections;
using System.Text;
using Beefy.gfx;

namespace Beefy.widgets
{
    public class ImageWidget : Widget
    {
        public IDrawable mImage;
		public IDrawable mOverImage;
		public IDrawable mDownImage;

        public override void Draw(Graphics g)
        {
			if ((mMouseOver && mMouseDown) && (mDownImage != null))
				g.Draw(mDownImage);
			else if ((mMouseOver) && (mOverImage != null))
            	g.Draw(mOverImage);
			else
				g.Draw(mImage);
        }
    }
}
