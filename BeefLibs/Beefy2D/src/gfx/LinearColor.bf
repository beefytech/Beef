using System;
using Beefy.geom;

namespace Beefy.gfx;

// A linear-light RGBA color (unclamped floats -- HDR values above 1 are legal), as opposed to Color,
// which is a display-ready sRGB-encoded value. The engine convention: anything a human authors as a
// hex value or picks by eye is a Color; anything the renderer does math on is a LinearColor. The two
// only meet through FromSrgb/ToSrgbColor -- there is deliberately no cast between them, since a raw
// byte-normalize silently skips the transfer curve. Alpha is linear coverage in both types.
struct LinearColor
{
	public float mR, mG, mB, mA;

	public const LinearColor None = .(0, 0, 0, 0);
	public const LinearColor Black = .(0, 0, 0, 1);
	public const LinearColor White = .(1, 1, 1, 1);

	public this(float r, float g, float b, float a = 1.0f)
	{
		mR = r;
		mG = g;
		mB = b;
		mA = a;
	}

	// Vector4 is a space-agnostic float carrier (shader constants, math), so this pair stays implicit.
	public static Vector4 operator implicit(LinearColor self)
	{
		return .(self.mR, self.mG, self.mB, self.mA);
	}

	public static LinearColor operator implicit(Vector4 self)
	{
		return .(self.mX, self.mY, self.mZ, self.mW);
	}

	public static float SrgbToLinear(float c)
	{
		return (c <= 0.04045f) ? (c / 12.92f) : Math.Pow((c + 0.055f) / 1.055f, 2.4f);
	}

	public static float LinearToSrgb(float c)
	{
		return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * Math.Pow(c, 1.0f / 2.4f) - 0.055f);
	}

	public static LinearColor FromSrgb(uint32 color)
	{
		Color c = color;
		return .(SrgbToLinear(c.R / 255.0f), SrgbToLinear(c.G / 255.0f), SrgbToLinear(c.B / 255.0f), c.A / 255.0f);
	}

	// Lossy by design: HDR values clamp to the display's [0,1] before encoding.
	public Color ToSrgbColor()
	{
		float r = LinearToSrgb(Math.Clamp(mR, 0.0f, 1.0f));
		float g = LinearToSrgb(Math.Clamp(mG, 0.0f, 1.0f));
		float b = LinearToSrgb(Math.Clamp(mB, 0.0f, 1.0f));
		float a = Math.Clamp(mA, 0.0f, 1.0f);
		return Color.Get((int32)Math.Round(r * 255.0f), (int32)Math.Round(g * 255.0f), (int32)Math.Round(b * 255.0f), (int32)Math.Round(a * 255.0f));
	}
}
