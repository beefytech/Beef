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

		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_Buffer_UpdateRange(void* textureSegment, int32 offset, void* data, int32 size);

		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_Buffer_FlushUpdates(void* textureSegment);

		public int32 mStride;
		public int32 mCount;
		public bool mGpuWritable;
		public bool mCpuUpdatable;
		public bool mStreaming;

		public int ByteSize => mStride * mCount;

		// cpuUpdatable = default usage the CPU writes in place with UpdateRange (immediately, not queued).
		// streaming = an append-only per-frame stream: UpdateRange at offset 0 discards the buffer,
		// later ranges append; never rewrite a range within a frame.
		public static GpuBuffer Create(int32 stride, int32 count, bool gpuWritable = false, bool cpuUpdatable = false, bool streaming = false)
		{
			void* seg = Gfx_CreateStructuredBuffer(stride, count, (gpuWritable ? 1 : 0) | (cpuUpdatable ? 2 : 0) | (streaming ? 4 : 0));
			if (seg == null)
				return null;
			GpuBuffer buffer = new GpuBuffer();
			buffer.mNativeTextureSegment = seg;
			buffer.mStride = stride;
			buffer.mCount = count;
			buffer.mGpuWritable = gpuWritable;
			buffer.mCpuUpdatable = cpuUpdatable;
			buffer.mStreaming = streaming;
			buffer.mSrcWidth = count;
			buffer.mSrcHeight = 1;
			buffer.mWidth = count;
			buffer.mHeight = 1;
			return buffer;
		}

		public static GpuBuffer Create<T>(int32 count, bool gpuWritable = false, bool cpuUpdatable = false, bool streaming = false) where T : struct => Create((int32)sizeof(T), count, gpuWritable, cpuUpdatable, streaming);

		public void SetData(void* data, int size)
		{
			Debug.Assert(size <= ByteSize);
			Gfx_Buffer_SetData(mNativeTextureSegment, data, (.)size);
		}

		public void SetData<T>(Span<T> data) where T : struct
		{
			SetData(data.Ptr, data.Length * sizeof(T));
		}

		// Immediate write of a byte range (cpuUpdatable buffers only): lands before every draw still
		// queued in any draw layer, so a pass can publish what its already-queued draws will read.
		public void UpdateRange(int offset, void* data, int size)
		{
			Debug.Assert(mCpuUpdatable || mStreaming);
			Debug.Assert((offset >= 0) && (size > 0) && (offset + size <= ByteSize));
			Gfx_Buffer_UpdateRange(mNativeTextureSegment, (.)offset, data, (.)size);
		}

		// Lands every range UpdateRange accumulated since the last call, as GPU-queue copies. Call
		// after a batch of updates and before anything reads the buffer -- unflushed writes are
		// simply not there yet.
		public void FlushUpdates()
		{
			Gfx_Buffer_FlushUpdates(mNativeTextureSegment);
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
