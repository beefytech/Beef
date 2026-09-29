using System;
using System.Collections;

namespace Beefy.gfx
{
	// DirectComposition content inside one of our windows: another process's swapchain (CreateExternal)
	// and our own premultiplied surfaces (CreateSurface). Composed content always sits above whatever the
	// window draws itself. Changes show up on the next Commit. Windows only; Create returns null on
	// failure (see LastResult).
	public class CompositionHost
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionHost_Create(void* window);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionHost_Delete(void* host);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionHost_Commit(void* host);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_Composition_GetLastResult();

		void* mNative;
		List<CompositionVisual> mVisuals = new .() ~ delete _;

		// HRESULT of the last failed Create.
		public static int32 LastResult => Gfx_Composition_GetLastResult();

		public static CompositionHost Create(BFWindow window)
		{
			if ((window == null) || (window.mNativeWindowClosed))
				return null;
			void* native = Gfx_CompositionHost_Create(window.mNativeWindow);
			if (native == null)
				return null;
			let host = new CompositionHost();
			host.mNative = native;
			return host;
		}

		public ~this()
		{
			// The native host frees its visuals with it.
			while (!mVisuals.IsEmpty)
				delete mVisuals.Back;
			Gfx_CompositionHost_Delete(mNative);
		}

		public int32 Commit() => Gfx_CompositionHost_Commit(mNative);

		// Takes ownership of surfaceHandle, which must already be valid in this process.
		public CompositionVisual CreateExternal(void* surfaceHandle)
		{
			void* native = CompositionVisual.[Friend]Gfx_CompositionVisual_CreateExternal(mNative, surfaceHandle);
			return (native != null) ? AddVisual(native) : null;
		}

		// An RGBA8 premultiplied surface, filled with UpdateFrom.
		public CompositionVisual CreateSurface(int32 width, int32 height)
		{
			void* native = CompositionVisual.[Friend]Gfx_CompositionVisual_CreateSurface(mNative, width, height);
			return (native != null) ? AddVisual(native) : null;
		}

		CompositionVisual AddVisual(void* native)
		{
			let visual = new CompositionVisual();
			visual.[Friend]mHost = this;
			visual.[Friend]mNative = native;
			mVisuals.Add(visual);
			return visual;
		}
	}

	// Owned by the caller; deleting it removes it from its host (commit to see that). Offset is in the
	// window's client pixels; clip and scale are in the visual's own space.
	public class CompositionVisual
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionVisual_CreateExternal(void* host, void* surfaceHandle);
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionVisual_CreateSurface(void* host, int32 width, int32 height);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_Delete(void* visual);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionVisual_SetExternalSurface(void* visual, void* surfaceHandle);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionVisual_ResizeSurface(void* visual, int32 width, int32 height);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionVisual_UpdateFromTexture(void* visual, void* textureSegment, int32 srcX, int32 srcY, int32 width, int32 height);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_SetOffset(void* visual, float x, float y);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_SetScale(void* visual, float scaleX, float scaleY);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_SetClip(void* visual, float x, float y, float width, float height);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_SetVisible(void* visual, int32 visible);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionVisual_SetOrder(void* visual, int32 order);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionVisual_TakeContentLost(void* visual);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_Texture_ClearToColor(void* textureSegment, uint32 color);

		CompositionHost mHost;
		void* mNative;

		public ~this()
		{
			Gfx_CompositionVisual_Delete(mNative);
			mHost.[Friend]mVisuals.Remove(this);
		}

		// Replaces the external content; takes ownership of surfaceHandle. Null shows nothing.
		public int32 SetExternalSurface(void* surfaceHandle) => Gfx_CompositionVisual_SetExternalSurface(mNative, surfaceHandle);

		public int32 ResizeSurface(int32 width, int32 height) => Gfx_CompositionVisual_ResizeSurface(mNative, width, height);

		// Copies from a single-sample RGBA8 render target holding premultiplied color.
		public int32 UpdateFrom(Image image, int32 srcX, int32 srcY, int32 width, int32 height) =>
			Gfx_CompositionVisual_UpdateFromTexture(mNative, image.mNativeTextureSegment, srcX, srcY, width, height);

		public void SetOffset(float x, float y) => Gfx_CompositionVisual_SetOffset(mNative, x, y);

		public void SetScale(float scaleX, float scaleY) => Gfx_CompositionVisual_SetScale(mNative, scaleX, scaleY);

		// Clears a render target to transparent right away, ready to draw a surface's contents into.
		public static void ClearRenderTarget(Image renderTarget) => Gfx_Texture_ClearToColor(renderTarget.mNativeTextureSegment, 0);

		// In the visual's own space: the scale applies to the clip too.
		public void SetClip(float x, float y, float width, float height) => Gfx_CompositionVisual_SetClip(mNative, x, y, width, height);

		public void ClearClip() => Gfx_CompositionVisual_SetClip(mNative, 0, 0, -1, -1);

		public void SetVisible(bool visible) => Gfx_CompositionVisual_SetVisible(mNative, visible ? 1 : 0);

		// Higher is on top; equal orders stack in creation order.
		public void SetOrder(int32 order) => Gfx_CompositionVisual_SetOrder(mNative, order);

		// True once after the surface's pixels were lost (created, resized, device re-created): redraw it.
		public bool TakeContentLost() => Gfx_CompositionVisual_TakeContentLost(mNative) != 0;
	}

	// A swapchain presented into a composition surface that another process shows (its
	// CompositionHost.CreateExternal). Draw into Texture like any render target, then Present. Pass
	// FramePacer to BFApp.SetFramePacer to draw in step with the display showing it.
	public class CompositionTarget
	{
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionTarget_Create(int32 width, int32 height);
		[CallingConvention(.Stdcall), CLink]
		static extern void Gfx_CompositionTarget_Delete(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionTarget_GetTexture(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_Resize(void* target, int32 width, int32 height);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_Present(void* target, int32 syncInterval);
		[CallingConvention(.Stdcall), CLink]
		static extern void* Gfx_CompositionTarget_GetFramePacer(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_WaitForFrame(void* target, int32 timeoutMS);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_GetGeneration(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_GetTextureVersion(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_GetWidth(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern int32 Gfx_CompositionTarget_GetHeight(void* target);
		[CallingConvention(.Stdcall), CLink]
		static extern uint64 Gfx_CompositionTarget_DuplicateHandleTo(void* target, int32 processId);

		void* mNative;
		Image mTexture;
		int32 mTextureVersion = -1;

		public static CompositionTarget Create(int32 width, int32 height)
		{
			void* native = Gfx_CompositionTarget_Create(width, height);
			if (native == null)
				return null;
			let target = new CompositionTarget();
			target.mNative = native;
			return target;
		}

		public ~this()
		{
			DeleteAndNullify!(mTexture);
			Gfx_CompositionTarget_Delete(mNative);
		}

		public int32 Width => Gfx_CompositionTarget_GetWidth(mNative);
		public int32 Height => Gfx_CompositionTarget_GetHeight(mNative);
		public void* FramePacer => Gfx_CompositionTarget_GetFramePacer(mNative);
		// Changes when the surface handle was replaced (device re-creation): duplicate it to the host again.
		public int32 Generation => Gfx_CompositionTarget_GetGeneration(mNative);

		// For pacing without BFApp.SetFramePacer: true once every earlier Present is done with.
		public bool WaitForFrame(int32 timeoutMS) => Gfx_CompositionTarget_WaitForFrame(mNative, timeoutMS) != 0;

		// The current back buffer. Take it again after Resize or a device re-creation; do both between
		// frames, never while a draw into the old one is queued.
		public Image Texture
		{
			get
			{
				int32 version = Gfx_CompositionTarget_GetTextureVersion(mNative);
				if (version != mTextureVersion)
				{
					DeleteAndNullify!(mTexture);
					void* segment = Gfx_CompositionTarget_GetTexture(mNative);
					if (segment != null)
						mTexture = Image.CreateFromNativeTextureSegment(segment);
					mTextureVersion = version;
				}
				return mTexture;
			}
		}

		public int32 Resize(int32 width, int32 height)
		{
			DeleteAndNullify!(mTexture);
			return Gfx_CompositionTarget_Resize(mNative, width, height);
		}

		// syncInterval 0 never waits: it skips the present (returning 1) while the display has no room.
		public int32 Present(int32 syncInterval = 1) => Gfx_CompositionTarget_Present(mNative, syncInterval);

		// The surface handle's value in another process, or 0.
		public uint64 DuplicateHandleTo(int32 processId) => Gfx_CompositionTarget_DuplicateHandleTo(mNative, processId);
	}
}
