using System;
using System.Diagnostics;

namespace Beefy.gfx
{
#if !STUDIO_CLIENT
	// GPU structured buffer (StructuredBuffer<T> in HLSL). It shares the texture slots, so bind it
	// with Graphics.SetTexture; SetData is queued with the draw commands like constant data.
	public class GpuBuffer : Image
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CreateStructuredBuffer(int32 stride, int32 count);

		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_Buffer_SetData(void* textureSegment, void* data, int32 size);

		public int32 mStride;
		public int32 mCount;

		public int ByteSize => mStride * mCount;

		public static GpuBuffer Create(int32 stride, int32 count)
		{
			void* seg = Gfx_CreateStructuredBuffer(stride, count);
			if (seg == null)
				return null;
			GpuBuffer buffer = new GpuBuffer();
			buffer.mNativeTextureSegment = seg;
			buffer.mStride = stride;
			buffer.mCount = count;
			buffer.mSrcWidth = count;
			buffer.mSrcHeight = 1;
			buffer.mWidth = count;
			buffer.mHeight = 1;
			return buffer;
		}

		public static GpuBuffer Create<T>(int32 count) where T : struct => Create((int32)sizeof(T), count);

		public void SetData(void* data, int size)
		{
			Debug.Assert(size <= ByteSize);
			Gfx_Buffer_SetData(mNativeTextureSegment, data, (.)size);
		}

		public void SetData<T>(Span<T> data) where T : struct
		{
			SetData(data.Ptr, data.Length * sizeof(T));
		}
	}
#endif
}
