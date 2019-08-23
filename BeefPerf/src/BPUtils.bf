using Beefy.gfx;
using System;
using Beefy.theme.dark;

namespace BeefPerf
{
	class BPUtils
	{
		public static void DrawWait(Graphics g, float x, float y)
		{
			for (int segIdx < 8)
			{
				int relSegIdx = (segIdx + (gApp.mUpdateCnt / 10)) % 8;

				using (g.PushColor(Color.Get(1.0f - (float)relSegIdx * 0.1f)))
					using (g.PushRotate((float)Math.PI_f * -(float)segIdx / 4, x + 10, y + 10))
			        	g.Draw(DarkTheme.sDarkTheme.GetImage(.WaitBar), x, y);
			}
		}

		static Image sScratchHiliteImage ~ delete _;

		static Image GetOutlineHiliteImage(Image hiliteImage, int ofs, int maxWidth)
		{
			int xOfs = ofs % 18;
			int width = Math.Min(maxWidth, 18 - xOfs);
			if (sScratchHiliteImage == null)
			{
				sScratchHiliteImage = hiliteImage.CreateImageSegment(xOfs, 0, width, 2);
			}
			else
			{
				sScratchHiliteImage.Modify(hiliteImage, xOfs, 0, width, 2);
			}

			return sScratchHiliteImage;
		}

		static void DrawOutlineSegment(Graphics g, Image hiliteImage, int len, int ofs)
		{
			int curOfs = ofs;
			int lenLeft = len;
			int xOfs = 0;
			while (lenLeft > 0)
			{
				var image = GetOutlineHiliteImage(hiliteImage, curOfs, lenLeft);
				g.Draw(image, (float)xOfs, 0);
				lenLeft -= (int)image.mWidth;
				xOfs += (int)image.mWidth;
				curOfs = 0;
			}
		}

		public static void DrawOutlineHilite(Graphics g, float x, float y, float width, float height)
		{
			var hiliteImage = DarkTheme.sDarkTheme.GetImage((Math.Min(width, height) > 2.5f) ? DarkTheme.ImageIdx.HiliteOutline : DarkTheme.ImageIdx.HiliteOutlineThin);

			Matrix mat;
			int ofs = 17 - (gApp.mUpdateCnt / 6)%18;

			if (width > 1.5f)
			{
				mat = Matrix.IdentityMatrix;
				mat.Translate(x, y);
				using (g.PushMatrix(mat))
				{
					DrawOutlineSegment(g, hiliteImage, (int)width, ofs);
					ofs += (int)width;
				}
			}

			mat = Matrix.IdentityMatrix;
			mat.Rotate((float)Math.PI_f * 0.5f);
			mat.Translate(x + width, y + 1);
			using (g.PushMatrix(mat))
			{
				DrawOutlineSegment(g, hiliteImage, (int)height - 1, ofs);
				ofs += (int)height - 1;
			}

			if (width < 1.5f)
				return;
			
			mat = Matrix.IdentityMatrix;
			mat.Rotate((float)Math.PI_f);
			mat.Translate(x + width - 1, y + height);
			using (g.PushMatrix(mat))
			{
				DrawOutlineSegment(g, hiliteImage, (int)width - 1, ofs);
				ofs += (int)width - 1;
			}

			mat = Matrix.IdentityMatrix;
			mat.Rotate((float)Math.PI_f * 1.5f);
			mat.Translate(x, y + height - 1);
			using (g.PushMatrix(mat))
			{
				DrawOutlineSegment(g, hiliteImage, (int)height - 2, ofs);
				ofs += (int)height - 2;
			}
		}
	}
}
