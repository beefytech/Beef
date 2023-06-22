using System;
using System.Collections;
using System.Text;

namespace Beefy.gfx
{
    public struct Color : uint32
    {
        public const Color White = 0xFFFFFFFF;
        public const Color Black = 0xFF000000;
        public const Color Red = 0xFFFF0000;
        public const Color Green = 0xFF00FF00;
        public const Color Blue = 0xFF0000FF;
        public const Color Yellow = 0xFFFFFF00;

		public static implicit operator uint32(Color color);
		public static implicit operator Color(uint32 color);
		public static implicit operator Color(int color) => (uint32)color;

		public this(int32 r, int32 g, int32 b)
		{
			this = 0xFF000000 | (uint32)((r << 16) | (g << 8) | (b));
		}

		public this(int32 r, int32 g, int32 b, int32 a)
		{
			this = (uint32)((a << 24) | (r << 16) | (g << 8) | (b));
		}

		public uint8 R
		{
			get => (.)((uint32)this >> 16);
			set mut
			{
				this = ((uint32)this & 0xFF00FFFF) | ((uint32)value << 16);
			}
		}

		public uint8 G
		{
			get => (.)((uint32)this >> 8);
			set mut
			{
				this = ((uint32)this & 0xFFFF00FF) | ((uint32)value << 8);
			}
		}

		public uint8 B
		{
			get => (.)((uint32)this >> 0);
			set mut
			{
				this = ((uint32)this & 0xFFFFFF00) | ((uint32)value << 0);
			}
		}

		public uint8 A
		{
			get => (.)((uint32)this >> 24);
			set mut
			{
				this = ((uint32)this & 0x00FFFFFF) | ((uint32)value << 24);
			}
		}

		public Color SwappedRB => ((uint32)this & 0xFF00FF00) | (((uint32)this >> 16) & 0x000000FF) | (((uint32)this << 16) & 0x00FF0000);

        public static Color Get(float a)
        {
            return 0x00FFFFFF | (((uint32)(255.0f * a)) << 24);
        }

        public static Color Get(uint32 rgb, float a)
        {
            return (uint32)(((uint32)(a * 255) << 24) | (rgb & 0xffffff));
        }

        public static Color Get(int32 r, int32 g, int32 b)
        {
            return 0xFF000000 | (uint32)((r << 16) | (g << 8) | (b));
        }

        public static Color Get(int32 r, int32 g, int32 b, int32 a)
        {
            return (uint32)((a << 24) | (r << 16) | (g << 8) | (b));
        }

        public static float GetAlpha(uint32 color)
        {
            return ((float)(0xff & (color >> 24))) / 255.0f;
        }
        
        public static Color Mult(Color color, Color colorMult)
        {
            if (color == 0xFFFFFFFF)
                return colorMult;
            if (colorMult == 0xFFFFFFFF)
                return color;

            uint32 result =
              (((((color >> 24) & 0xFF) * ((colorMult >> 24) & 0xFF)) / 255) << 24) |
                (((((color >> 16) & 0xFF) * ((colorMult >> 16) & 0xFF)) / 255) << 16) |
                (((((color >> 8) & 0xFF) * ((colorMult >> 8) & 0xFF)) / 255) << 8) |
                (((color & 0xFF) * (colorMult & 0xFF)) / 255);
            return result;
        }

        public static Color Lerp(Color color1, Color color2, float pct)
        {
            if (color1 == color2)
                return color1;

            uint32 a = (uint32)(pct * 256.0f);
            uint32 oma = 256 - a;
            uint32 aColor =
                (((((color1 & 0x000000FF) * oma) + ((color2 & 0x000000FF) * a)) >> 8) & 0x000000FF) |
                (((((color1 & 0x0000FF00) * oma) + ((color2 & 0x0000FF00) * a)) >> 8) & 0x0000FF00) |
                (((((color1 & 0x00FF0000) * oma) + ((color2 & 0x00FF0000) * a)) >> 8) & 0x00FF0000) |
                ((((((color1 >> 24) & 0xFF) * oma) + (((color2 >> 24) & 0xFF) * a)) & 0x0000FF00) << 16);

            return aColor;
        }

        public static void ToHSV(uint32 color, out float h, out float s, out float v)
        {
            float r = ((color >> 16) & 0xFF) / 255.0f;
            float g = ((color >> 8) & 0xFF) / 255.0f;
            float b = ((color >> 0) & 0xFF) / 255.0f;

            float min, max, delta;
            min = Math.Min(r, Math.Min(g, b));
            max = Math.Max(r, Math.Max(g, b));
            v = max;               // v
            delta = max - min;
            if (max != 0)
                s = delta / max;       // s
            else
            {
                // r = g = b = 0		// s = 0, v is undefined
                s = 0;
                h = -1;
                return;
            }
            if (r == max)
                h = (g - b) / delta;       // between yellow & magenta
            else if (g == max)
                h = 2 + (b - r) / delta;   // between cyan & yellow
            else
                h = 4 + (r - g) / delta;   // between magenta & cyan
            h /= 6;                        // degrees
            if (h < 0)
                h += 1.0f;
        }

        public static Color FromHSV(float h, float s, float v, int32 a)
        {            
            float r, g, b;
		    if (s == 0.0f)
            {
			    r = g = b = v;
            }
            else
            {
                float useH = h * 6.0f;
		        int32 i = (int32)useH;
		        float f = useH - i;
		        if ((i & 1) == 0)
			        f = 1 - f;
		        float m = v * (1 - s);
		        float n = v * (1 - s*f);
		        switch(i)
		        {
		        case 0: fallthrough; 
                case 6: r = v; g = n; b = m; break;
		        case 1: r = n; g = v; b = m; break;
		        case 2: r = m; g = v; b = n; break;
		        case 3: r = m; g = n; b = v; break;
		        case 4: r = n; g = m; b = v; break;
		        case 5: r = v; g = m; b = n; break;
		        default: r = 0; g = 0; b = 0; break;
                }
		    }
        
            return Get((int32)(r * 255.0f), (int32)(g * 255.0f), (int32)(b * 255.0f), a);
        }

        public static Color FromHSV(float h, float s, float v)
        {
            return FromHSV(h, s, v, 0xFF);
        }

		public static uint32 ToNative(Color color)
		{
			return (color & 0xFF00FF00) | ((color & 0x00FF0000) >> 16) | ((color & 0x000000FF) << 16);
		}

		public static uint32 FromNative(Color color)
		{
			return (color & 0xFF00FF00) | ((color & 0x00FF0000) >> 16) | ((color & 0x000000FF) << 16);
		}
    }
}
