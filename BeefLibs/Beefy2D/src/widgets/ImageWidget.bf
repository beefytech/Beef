using System;
using System.Collections.Generic;
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
			if ((mMouseOver) && (mOverImage != null))
            	g.Draw(mOverImage);
			else
				g.Draw(mImage);
        }
    }
}
