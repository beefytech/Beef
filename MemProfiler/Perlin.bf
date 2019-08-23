using Beefy.geom;
using System;

namespace Mercury
{
	class Perlin
	{
		Vector2[,] mGradient ~ delete _;

		public this(int w, int h, int seed = -1)
		{
			Random rand;
			if (seed == -1)
			{
				 rand = stack Random();
			}
			else
			{
				 rand = stack Random(seed);
			}

			mGradient = new Vector2[h, w];
			for (int y < h)
			{
				for (int x < w)
				{
					float ang = (float)(rand.NextDouble() * Math.PI * 2);
					mGradient[y, x] = Vector2((float)Math.Cos(ang), (float)Math.Sin(ang));
				}									 
			}
		}

		float DotGridGradient(int ix, int iy, float x, float y)
		{
			// Precomputed (or otherwise) gradient vectors at each grid node
			// Compute the distance vector
			float dx = x - (float)ix;
			float dy = y - (float)iy;

			// Compute the dot-product
			return (dx * mGradient[iy, ix].mX + dy * mGradient[iy, ix].mY);
		}

		// Compute Perlin noise at coordinates x, y
		public float Get(float x, float y)
		{
			// Determine grid cell coordinates
			int x0 = ((x > 0.0) ? (int)x : (int)x - 1);
			int x1 = x0 + 1;
			int y0 = ((y > 0.0) ? (int)y : (int)y - 1);
			int y1 = y0 + 1;

			// Determine interpolation weights
			// Could also use higher order polynomial/s-curve here
			float sx = x - (float)x0;
			float sy = y - (float)y0;

			// Interpolate between grid point gradients
			float n0, n1, ix0, ix1, value;
			n0 = DotGridGradient(x0, y0, x, y);
			n1 = DotGridGradient(x1, y0, x, y);
			ix0 = Math.Lerp(n0, n1, sx);
			n0 = DotGridGradient(x0, y1, x, y);
			n1 = DotGridGradient(x1, y1, x, y);
			ix1 = Math.Lerp(n0, n1, sx);
			value = Math.Lerp(ix0, ix1, sy);

			return value;
		}

		public float GetF(float pctX, float pctY)
		{
			return Get(pctX * mGradient.GetLength(1), pctY * mGradient.GetLength(0));
		}
	}
}
