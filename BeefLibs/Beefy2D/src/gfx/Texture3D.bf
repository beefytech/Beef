using System;
using System.Diagnostics;

namespace Beefy.gfx;

// Volume texture: sampleable as Texture3D from any shader slot (Graphics.SetTexture) and
// writable per mip from compute (Graphics.SetComputeUAV -> RWTexture3D). Not a render target.
public class Texture3D : Image
{
	[CallingConvention(.Stdcall), CLink]
	static extern void* Gfx_CreateTexture3D(int32 width, int32 height, int32 depth, int32 flags);

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_Texture3D_SetData(void* textureSegment, int32 mipLevel, void* data, int32 rowPitch, int32 slicePitch);

	[CallingConvention(.Stdcall), CLink]
	static extern bool Gfx_Texture3D_GetData(void* textureSegment, int32 mipLevel, void* outData, int32 outSize);

	public int32 mDepth;
	public int32 mMipLevels;
	public int32 mBytesPerTexel;

	public static int32 BytesPerTexel(RenderTargetFlags flags)
	{
		if (flags.HasFlag(.F16)) return 8;
		if (flags.HasFlag(.HighPrecision) || flags.HasFlag(.R32Uint)) return 4;
		if (flags.HasFlag(.RG8) || flags.HasFlag(.R16F)) return 2;
		if (flags.HasFlag(.R8)) return 1;
		return 4;
	}

	// Format flags as for render targets (F16/R8/RG8/R16F/HighPrecision/R32Uint, else RGBA8);
	// .Mipmaps allocates the full chain (GenerateMips-able, every level UAV-writable).
	public static Texture3D Create(int32 width, int32 height, int32 depth, RenderTargetFlags flags = .None)
	{
		void* seg = Gfx_CreateTexture3D(width, height, depth, (int32)flags);
		if (seg == null)
			return null;
		Texture3D tex = new Texture3D();
		tex.mNativeTextureSegment = seg;
		tex.mSrcWidth = width;
		tex.mSrcHeight = height;
		tex.mWidth = width;
		tex.mHeight = height;
		tex.mDepth = depth;
		tex.mBytesPerTexel = BytesPerTexel(flags);
		tex.mMipLevels = 1;
		if (flags.HasFlag(.Mipmaps))
		{
			int32 size = Math.Max(Math.Max(width, height), depth);
			while (((size >> tex.mMipLevels) >= 1) && (tex.mMipLevels < 16))
				tex.mMipLevels++;
		}
		return tex;
	}

	public int32 MipWidth(int32 mip) => Math.Max(1, mSrcWidth >> mip);
	public int32 MipHeight(int32 mip) => Math.Max(1, mSrcHeight >> mip);
	public int32 MipDepth(int32 mip) => Math.Max(1, mDepth >> mip);
	public int MipByteSize(int32 mip) => MipWidth(mip) * MipHeight(mip) * MipDepth(mip) * mBytesPerTexel;

	// Whole-mip upload of tightly packed texels (x fastest, then y, then z).
	public void SetData(int32 mipLevel, void* data)
	{
		int32 rowPitch = MipWidth(mipLevel) * mBytesPerTexel;
		Gfx_Texture3D_SetData(mNativeTextureSegment, mipLevel, data, rowPitch, rowPitch * MipHeight(mipLevel));
	}

	// Immediate readback (tightly packed) of what the GPU has finished -- draw the layer that wrote it first.
	public bool GetData(int32 mipLevel, void* outData, int outSize)
	{
		Debug.Assert(outSize >= MipByteSize(mipLevel));
		return Gfx_Texture3D_GetData(mNativeTextureSegment, mipLevel, outData, (.)outSize);
	}
}
