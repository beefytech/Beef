using System;
using System.Collections;
using Beefy.gfx;
using Beefy.geom;
using Beefy.widgets;

namespace Beefy.mcp
{
	// Renders windows and widgets offscreen and turns the result into PNG bytes.
	//
	// Offscreen rather than reading the swapchain: it works while the window is minimized, covered
	// or on another desktop, and it can composite every window (popups, tooltips, menus) into one
	// image at their screen positions. The pipeline is the one Brisk's SceneSimWidget.SaveScreenshot
	// uses -- a DrawLayer drawn into a render target, read back with GetBits -- applied to a
	// WidgetWindow's root widget the way BFApp.Draw would draw it. Fonts and theme assets draw
	// identically, so what comes back is what the user sees, minus the OS frame and menu bar.
	//
	// Each window is always drawn at its own origin, exactly as in a real frame: some widgets read
	// their position back out of the graphics matrix (the autocomplete list derives its scroll
	// offset that way), so shifting the tree to composite or crop would break them. Compositing and
	// cropping happen on the pixels afterwards instead.
	public static class UICapture
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Res_EncodePNG(uint32* bits, int32 width, int32 height, int32* outSize);

		[CallingConvention(.Stdcall), CLink]
		static extern bool Res_WritePNG(uint32* bits, int32 width, int32 height, char8* filePath);

		public const int32 cMaxDimension = 8192;

		public class Bitmap
		{
			public int32 mWidth;
			public int32 mHeight;
			public uint32[] mBits ~ delete _; // RGBA, row-major, opaque

			public this()
			{
			}

			public this(int32 width, int32 height, uint32 fill = 0xFF000000)
			{
				mWidth = width;
				mHeight = height;
				mBits = new uint32[width * height];
				for (int i < mBits.Count)
					mBits[i] = fill;
			}

			public uint32 GetPixel(int x, int y)
			{
				if ((x < 0) || (y < 0) || (x >= mWidth) || (y >= mHeight))
					return 0;
				return mBits[y * mWidth + x];
			}
		}

		// Draws drawFunc into a fresh render target of the given size in device pixels and reads
		// it back. Runs the normal StartDraw/EndDraw bracket on its own draw layer, so it is safe
		// to call from Update, outside the app's own Draw.
		public static Bitmap Render(int32 width, int32 height, delegate void(Graphics g) drawFunc)
		{
			if ((width <= 0) || (height <= 0) || (width > cMaxDimension) || (height > cMaxDimension))
				return null;

			var app = BFApp.sApp;
			if (app.mGraphics == null)
				app.InitGraphics();
			var g = app.mGraphics;

			var renderTarget = Image.CreateRenderTarget(width, height);
			if (renderTarget == null)
				return null;
			defer delete renderTarget;
			renderTarget.Clear();

			g.StartDraw();
			DrawLayer drawLayer = scope .(null);
			using (g.PushDrawLayer(drawLayer))
			{
				// Opaque ground: windows assume something is behind them
				using (g.PushColor(0xFF000000))
					g.FillRect(0, 0, width, height);
				drawFunc(g);
			}
			g.EndDraw();
			drawLayer.DrawToRenderTarget(renderTarget);

			var bitmap = new Bitmap();
			bitmap.mWidth = width;
			bitmap.mHeight = height;
			bitmap.mBits = new uint32[width * height];
			renderTarget.GetBits(0, 0, width, height, width, bitmap.mBits.Ptr);
			// Blending can leave partial alpha in the target; the image is of an opaque window
			for (int i < bitmap.mBits.Count)
				bitmap.mBits[i] |= 0xFF000000;
			return bitmap;
		}

		// Draws one window's widget tree the way BFApp.Draw would (mScaleMatrix maps widget space
		// to device pixels, identity for most windows)
		public static void DrawWindow(Graphics g, WidgetWindow window)
		{
			if (window.mRootWidget == null)
				return;
			g.PushMatrix(window.mScaleMatrix);
			window.mRootWidget.DrawAll(g);
			g.PopMatrix();
		}

		// The client area of one window
		public static Bitmap CaptureWindow(WidgetWindow window)
		{
			return Render(window.mClientWidth, window.mClientHeight, scope (g) =>
				{
					DrawWindow(g, window);
				});
		}

		// Copies src into dst at dstX/dstY, clipped to dst
		public static void Blit(Bitmap dst, Bitmap src, int32 dstX, int32 dstY)
		{
			int32 srcStartX = Math.Max(0, -dstX);
			int32 srcEndX = Math.Min(src.mWidth, dst.mWidth - dstX);
			if (srcEndX <= srcStartX)
				return;
			for (int32 y < src.mHeight)
			{
				int32 dy = dstY + y;
				if ((dy < 0) || (dy >= dst.mHeight))
					continue;
				Internal.MemCpy(&dst.mBits[dy * dst.mWidth + dstX + srcStartX], &src.mBits[y * src.mWidth + srcStartX], (srcEndX - srcStartX) * 4);
			}
		}

		// A rectangle of src as a new bitmap, clipped to src; null if nothing is left
		public static Bitmap Crop(Bitmap src, int32 x, int32 y, int32 width, int32 height)
		{
			int32 x0 = Math.Max(x, 0);
			int32 y0 = Math.Max(y, 0);
			int32 x1 = Math.Min(x + width, src.mWidth);
			int32 y1 = Math.Min(y + height, src.mHeight);
			if ((x1 <= x0) || (y1 <= y0))
				return null;
			var bitmap = new Bitmap(x1 - x0, y1 - y0);
			Blit(bitmap, src, -x0, -y0);
			return bitmap;
		}

		// Every visible window composited at its screen position -- popups, tooltips and menus
		// included, in creation order so later (topmost) windows land on top. originX/Y is the
		// screen position of the image's top-left corner.
		public static Bitmap CaptureAll(out int32 originX, out int32 originY)
		{
			int32 minX = int32.MaxValue;
			int32 minY = int32.MaxValue;
			int32 maxX = int32.MinValue;
			int32 maxY = int32.MinValue;
			for (var window in BFApp.sApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if ((widgetWindow == null) || (!widgetWindow.mVisible) || (widgetWindow.mClientWidth <= 0) || (widgetWindow.mClientHeight <= 0))
					continue;
				minX = Math.Min(minX, widgetWindow.mClientX);
				minY = Math.Min(minY, widgetWindow.mClientY);
				maxX = Math.Max(maxX, widgetWindow.mClientX + widgetWindow.mClientWidth);
				maxY = Math.Max(maxY, widgetWindow.mClientY + widgetWindow.mClientHeight);
			}

			originX = minX;
			originY = minY;
			if (minX >= maxX)
				return null;

			var composite = new Bitmap(Math.Min(maxX - minX, cMaxDimension), Math.Min(maxY - minY, cMaxDimension));
			for (var window in BFApp.sApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if ((widgetWindow == null) || (!widgetWindow.mVisible) || (widgetWindow.mClientWidth <= 0) || (widgetWindow.mClientHeight <= 0))
					continue;
				var windowBitmap = CaptureWindow(widgetWindow);
				if (windowBitmap == null)
					continue;
				Blit(composite, windowBitmap, widgetWindow.mClientX - minX, widgetWindow.mClientY - minY);
				delete windowBitmap;
			}
			return composite;
		}

		// Converts a point in a window's widget space to device pixels
		public static void ToDevice(WidgetWindow window, float x, float y, out int32 deviceX, out int32 deviceY)
		{
			var pt = window.mScaleMatrix.Multiply(Point(x, y));
			deviceX = (int32)Math.Round(pt.x);
			deviceY = (int32)Math.Round(pt.y);
		}

		// One widget, cropped to its own rect out of a capture of its window
		public static Bitmap CaptureWidget(Widget widget)
		{
			var window = widget.mWidgetWindow;
			if (window == null)
				return null;

			widget.SelfToRootTranslate(0, 0, var rootX, var rootY);
			ToDevice(window, rootX, rootY, var x0, var y0);
			ToDevice(window, rootX + widget.mWidth, rootY + widget.mHeight, var x1, var y1);
			if ((x1 <= x0) || (y1 <= y0))
				return null;

			var windowBitmap = CaptureWindow(window);
			if (windowBitmap == null)
				return null;
			defer delete windowBitmap;
			return Crop(windowBitmap, x0, y0, x1 - x0, y1 - y0);
		}

		// The composited pixels the OS actually shows for a window's client area, for the cases
		// where that differs from what the widgets draw. Needs the window on screen.
		public static Bitmap CaptureScreen(WidgetWindow window)
		{
			if ((window.mClientWidth <= 0) || (window.mClientHeight <= 0))
				return null;
			var bitmap = new Bitmap();
			bitmap.mWidth = window.mClientWidth;
			bitmap.mHeight = window.mClientHeight;
			bitmap.mBits = new uint32[bitmap.mWidth * bitmap.mHeight];
			if (!window.CaptureClientBits(bitmap.mBits.Ptr, bitmap.mWidth, bitmap.mHeight))
			{
				delete bitmap;
				return null;
			}
			for (int i < bitmap.mBits.Count)
				bitmap.mBits[i] |= 0xFF000000;
			return bitmap;
		}

		// Box-filter downscale in place, for keeping big captures within an image budget
		public static void Downscale(Bitmap bitmap, float scale)
		{
			if ((scale <= 0) || (scale >= 1))
				return;

			int32 srcWidth = bitmap.mWidth;
			int32 srcHeight = bitmap.mHeight;
			int32 dstWidth = Math.Max(1, (int32)(srcWidth * scale));
			int32 dstHeight = Math.Max(1, (int32)(srcHeight * scale));
			uint32[] dst = new uint32[dstWidth * dstHeight];

			for (int32 dy < dstHeight)
			{
				int32 sy0 = dy * srcHeight / dstHeight;
				int32 sy1 = Math.Max(sy0 + 1, (dy + 1) * srcHeight / dstHeight);
				for (int32 dx < dstWidth)
				{
					int32 sx0 = dx * srcWidth / dstWidth;
					int32 sx1 = Math.Max(sx0 + 1, (dx + 1) * srcWidth / dstWidth);

					uint32 sumR = 0;
					uint32 sumG = 0;
					uint32 sumB = 0;
					uint32 count = 0;
					for (int32 sy = sy0; sy < sy1; sy++)
					{
						for (int32 sx = sx0; sx < sx1; sx++)
						{
							uint32 pixel = bitmap.mBits[sy * srcWidth + sx];
							sumR += pixel & 0xFF;
							sumG += (pixel >> 8) & 0xFF;
							sumB += (pixel >> 16) & 0xFF;
							count++;
						}
					}
					dst[dy * dstWidth + dx] = 0xFF000000 | ((sumB / count) << 16) | ((sumG / count) << 8) | (sumR / count);
				}
			}

			delete bitmap.mBits;
			bitmap.mBits = dst;
			bitmap.mWidth = dstWidth;
			bitmap.mHeight = dstHeight;
		}

		public static bool EncodePNG(Bitmap bitmap, List<uint8> outData)
		{
			int32 size = 0;
			void* ptr = Res_EncodePNG(bitmap.mBits.Ptr, bitmap.mWidth, bitmap.mHeight, &size);
			if ((ptr == null) || (size <= 0))
				return false;
			outData.AddRange(Span<uint8>((uint8*)ptr, size));
			return true;
		}

		public static bool WritePNG(Bitmap bitmap, StringView path)
		{
			return Res_WritePNG(bitmap.mBits.Ptr, bitmap.mWidth, bitmap.mHeight, path.ToScopeCStr!());
		}
	}
}
