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

	// LinearToSrgb's curve sampled as a bias+scale lerp per eighth-octave of the float exponent range,
	// which is what makes LinearToSrgbByte a table lookup instead of a pow(). High 16 bits are the
	// bias (pre-shift), low 16 the scale. Index = (bits - cSrgbMinBits) >> 20.
	public const uint32[104] cLinearToSrgb8 = .(
		0x0073000D, 0x007A000D, 0x0080000D, 0x0087000D, 0x008D000D, 0x0094000D, 0x009A000D, 0x00A1000D,
		0x00A7001A, 0x00B4001A, 0x00C1001A, 0x00CE001A, 0x00DA001A, 0x00E7001A, 0x00F4001A, 0x0101001A,
		0x010E0033, 0x01280033, 0x01410033, 0x015B0033, 0x01750033, 0x018F0033, 0x01A80033, 0x01C20033,
		0x01DC0067, 0x020F0067, 0x02430067, 0x02760067, 0x02AA0067, 0x02DD0067, 0x03110067, 0x03440067,
		0x037800CE, 0x03DF00CE, 0x044600CE, 0x04AD00CE, 0x051400CE, 0x057B00C5, 0x05DD00BC, 0x063B00B5,
		0x06970158, 0x07420142, 0x07E30130, 0x087B0120, 0x090B0112, 0x09940106, 0x0A1700FC, 0x0A9500F2,
		0x0B0F01CB, 0x0BF401AE, 0x0CCB0195, 0x0D950180, 0x0E56016E, 0x0F0D015E, 0x0FBC0150, 0x10630143,
		0x11070264, 0x1238023E, 0x1357021D, 0x14660201, 0x156601E9, 0x165A01D3, 0x174401C0, 0x182401AF,
		0x18FE0331, 0x1A9602FE, 0x1C1502D2, 0x1D7E02AD, 0x1ED4028D, 0x201A0270, 0x21520256, 0x227D0240,
		0x239F0443, 0x25C003FE, 0x27BF03C4, 0x29A10392, 0x2B6A0367, 0x2D1D0341, 0x2EBE031F, 0x304D0300,
		0x31D105B0, 0x34A80555, 0x37520507, 0x39D504C5, 0x3C37048B, 0x3E7C0458, 0x40A8042A, 0x42BD0401,
		0x44C20798, 0x488E071E, 0x4C1C06B6, 0x4F76065D, 0x52A50610, 0x55AC05CC, 0x5892058F, 0x5B590559,
		0x5E0C0A23, 0x631C0980, 0x67DB08F6, 0x6C55087F, 0x70940818, 0x74A007BD, 0x787D076C, 0x7C330723);

	// The span cLinearToSrgb8 covers: 2^-13 (everything below rounds to byte 0) up to 1-epsilon.
	// Spelled as bit patterns so the clamp provably lands inside the table rather than relying on a
	// decimal literal rounding to the intended float.
	const uint32 cSrgbMinBits = 0x39000000;
	const float cSrgbMinV = [ConstEval]BitConverter.Convert<uint32, float>(0x39000000);
	const float cSrgbMaxV = [ConstEval]BitConverter.Convert<uint32, float>(0x3F7FFFFF);

	// Linear -> 8-bit sRGB with no pow() and no branches beyond the clamp. Max error is ~0.54 of a
	// byte step (D3D allows 0.6), it stays monotonic, and it round-trips exactly with
	// cSrgbByteToLinear. Out-of-range input saturates; NaN lands on 0, which is why the low compare
	// is written inverted.
	public static uint8 LinearToSrgbByte(float c)
	{
		float v = c;
		if (!(v > cSrgbMinV))
			v = cSrgbMinV;
		else if (v > cSrgbMaxV)
			v = cSrgbMaxV;

		uint32 bits = *(uint32*)&v;
		uint32 entry = cLinearToSrgb8[(bits - cSrgbMinBits) >> 20];
		uint32 bias = (entry >> 16) << 9;
		uint32 scale = entry & 0xFFFF;
		return (uint8)((bias + scale * ((bits >> 12) & 0xFF)) >> 16);
	}

	// Alpha is linear coverage in both types, so it only scales -- no transfer curve. NaN -> 0.
	static int32 CoverageByte(float a)
	{
		if (!(a > 0.0f))
			return 0;
		if (a >= 1.0f)
			return 255;
		return (int32)(a * 255.0f + 0.5f);
	}

	// SrgbToLinear evaluated at every n/255 -- the transfer curve is two pow()s per component and
	// byte colors are decoded in hot paths (per component, per pass). Regenerate alongside
	// SrgbToLinear if that curve ever changes.
	public const float[256] cSrgbByteToLinear = .(
		0.0f, 0.000303526991f, 0.000607053982f, 0.000910580973f, 0.00121410796f, 0.00151763496f, 0.00182116195f, 0.00212468882f,
		0.00242821593f, 0.0027317428f, 0.00303526991f, 0.00334653584f, 0.00367650739f, 0.00402471703f, 0.00439144205f, 0.00477695325f,
		0.00518151652f, 0.00560539169f, 0.00604883302f, 0.00651209056f, 0.00699541019f, 0.00749903219f, 0.00802319311f, 0.00856812578f,
		0.00913405884f, 0.00972121768f, 0.010329823f, 0.0109600937f, 0.0116122449f, 0.012286488f, 0.0129830325f, 0.0137020834f,
		0.0144438436f, 0.0152085144f, 0.0159962941f, 0.0168073755f, 0.0176419541f, 0.01850022f, 0.0193823613f, 0.0202885624f,
		0.0212190095f, 0.0221738853f, 0.0231533665f, 0.0241576321f, 0.0251868591f, 0.0262412224f, 0.0273208916f, 0.02842604f,
		0.0295568351f, 0.0307134446f, 0.0318960324f, 0.0331047662f, 0.0343398079f, 0.0356013142f, 0.0368894488f, 0.0382043719f,
		0.0395462364f, 0.0409151986f, 0.0423114114f, 0.043735031f, 0.045186203f, 0.0466650873f, 0.0481718257f, 0.0497065671f,
		0.0512694567f, 0.0528606474f, 0.054480277f, 0.0561284907f, 0.0578054301f, 0.0595112368f, 0.0612460524f, 0.0630100146f,
		0.064803265f, 0.0666259378f, 0.0684781671f, 0.0703600943f, 0.0722718537f, 0.0742135718f, 0.0761853829f, 0.078187421f,
		0.0802198201f, 0.0822827071f, 0.0843762085f, 0.0865004584f, 0.0886555836f, 0.0908417106f, 0.0930589661f, 0.0953074694f,
		0.097587347f, 0.0998987257f, 0.102241732f, 0.104616486f, 0.107023105f, 0.10946171f, 0.111932427f, 0.114435375f,
		0.116970666f, 0.119538426f, 0.122138776f, 0.124771819f, 0.127437681f, 0.130136475f, 0.13286832f, 0.135633335f,
		0.138431609f, 0.141263291f, 0.144128472f, 0.147027269f, 0.149959788f, 0.152926147f, 0.155926466f, 0.158960834f,
		0.162029371f, 0.165132195f, 0.168269396f, 0.171441108f, 0.174647406f, 0.177888423f, 0.18116425f, 0.18447499f,
		0.187820777f, 0.191201687f, 0.194617838f, 0.198069319f, 0.20155625f, 0.205078736f, 0.208636865f, 0.212230757f,
		0.215860501f, 0.219526201f, 0.223227963f, 0.226965874f, 0.230740055f, 0.23455058f, 0.238397568f, 0.242281124f,
		0.246201321f, 0.25015828f, 0.254152089f, 0.258182853f, 0.262250662f, 0.266355604f, 0.270497799f, 0.274677306f,
		0.278894275f, 0.283148736f, 0.287440836f, 0.291770637f, 0.296138257f, 0.300543785f, 0.304987311f, 0.309468925f,
		0.313988715f, 0.318546772f, 0.323143214f, 0.327778101f, 0.332451522f, 0.337163627f, 0.341914415f, 0.346704066f,
		0.351532608f, 0.356400132f, 0.361306787f, 0.366252601f, 0.371237695f, 0.376262128f, 0.38132602f, 0.386429429f,
		0.391572475f, 0.396755219f, 0.401977777f, 0.407240212f, 0.412542611f, 0.417885065f, 0.423267663f, 0.428690493f,
		0.434153646f, 0.439657182f, 0.445201188f, 0.450785786f, 0.456411034f, 0.462076992f, 0.467783809f, 0.473531485f,
		0.479320168f, 0.48514995f, 0.491020858f, 0.496932983f, 0.502886474f, 0.50888133f, 0.514917672f, 0.520995557f,
		0.527115107f, 0.533276379f, 0.539479494f, 0.545724452f, 0.55201143f, 0.558340371f, 0.564711511f, 0.571124852f,
		0.577580452f, 0.584078431f, 0.590618849f, 0.597201765f, 0.603827357f, 0.610495567f, 0.617206573f, 0.623960376f,
		0.630757153f, 0.637596846f, 0.644479692f, 0.651405632f, 0.658374846f, 0.665387273f, 0.672443151f, 0.679542482f,
		0.686685324f, 0.693871737f, 0.701101899f, 0.708375752f, 0.715693474f, 0.723055124f, 0.730460763f, 0.73791039f,
		0.745404184f, 0.752942204f, 0.760524511f, 0.768151164f, 0.775822222f, 0.783537805f, 0.791297913f, 0.799102724f,
		0.806952238f, 0.814846575f, 0.822785735f, 0.830769897f, 0.838799f, 0.846873224f, 0.854992628f, 0.863157213f,
		0.871367097f, 0.8796224f, 0.887923121f, 0.896269381f, 0.904661179f, 0.913098633f, 0.921581864f, 0.930110872f,
		0.938685715f, 0.947306514f, 0.955973327f, 0.964686275f, 0.973445296f, 0.982250571f, 0.991102099f, 1.0f);

	public static LinearColor FromSrgb(uint32 color)
	{
		Color c = color;
		return .(cSrgbByteToLinear[c.R], cSrgbByteToLinear[c.G], cSrgbByteToLinear[c.B], c.A / 255.0f);
	}

	// Lossy by design: HDR values clamp to the display's [0,1] before encoding.
	public Color ToSrgbColor()
	{
		return Color.Get((int32)LinearToSrgbByte(mR), (int32)LinearToSrgbByte(mG), (int32)LinearToSrgbByte(mB), CoverageByte(mA));
	}
}
