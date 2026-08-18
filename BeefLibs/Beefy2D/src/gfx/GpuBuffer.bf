using System;
using System.Diagnostics;

namespace Beefy.gfx
{
#if !STUDIO_CLIENT
	// GPU structured buffer (StructuredBuffer<T> in HLSL). It shares the texture slots, so bind it
	// with Graphics.SetTexture; SetData is queued with the draw commands like constant data. A
	// GPU-writable buffer can also be bound as a compute UAV (RWStructuredBuffer<T>) and read back.
	public class GpuBuffer : Image
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CreateStructuredBuffer(int32 stride, int32 count, int32 flags);

		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_Buffer_SetData(void* textureSegment, void* data, int32 size);

		[CallingConvention(.Stdcall), CLink]
		static extern bool Gfx_Buffer_GetData(void* textureSegment, void* outData, int32 size);

		public int32 mStride;
		public int32 mCount;
		public bool mGpuWritable;

		public int ByteSize => mStride * mCount;

		public static GpuBuffer Create(int32 stride, int32 count, bool gpuWritable = false)
		{
			void* seg = Gfx_CreateStructuredBuffer(stride, count, gpuWritable ? 1 : 0);
			if (seg == null)
				return null;
			GpuBuffer buffer = new GpuBuffer();
			buffer.mNativeTextureSegment = seg;
			buffer.mStride = stride;
			buffer.mCount = count;
			buffer.mGpuWritable = gpuWritable;
			buffer.mSrcWidth = count;
			buffer.mSrcHeight = 1;
			buffer.mWidth = count;
			buffer.mHeight = 1;
			return buffer;
		}

		public static GpuBuffer Create<T>(int32 count, bool gpuWritable = false) where T : struct => Create((int32)sizeof(T), count, gpuWritable);

		public void SetData(void* data, int size)
		{
			Debug.Assert(size <= ByteSize);
			Gfx_Buffer_SetData(mNativeTextureSegment, data, (.)size);
		}

		public void SetData<T>(Span<T> data) where T : struct
		{
			SetData(data.Ptr, data.Length * sizeof(T));
		}

		// Immediate readback of what the GPU has finished -- draw the layer that wrote it first.
		public bool GetData(void* outData, int size)
		{
			Debug.Assert(size <= ByteSize);
			return Gfx_Buffer_GetData(mNativeTextureSegment, outData, (.)size);
		}

		public bool GetData<T>(Span<T> outData) where T : struct
		{
			return GetData(outData.Ptr, outData.Length * sizeof(T));
		}
	}
#endif
}
