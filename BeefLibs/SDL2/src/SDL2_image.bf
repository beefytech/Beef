using System;

/* Derived from SDL2# - C# Wrapper for SDL2
 *
 * Copyright (c) 2013-2016 Ethan Lee.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

namespace SDL2
{
	public static class SDLImage
	{
		/* Similar to the headers, this is the version we're expecting to be
		 * running with. You will likely want to check this somewhere in your
		 * program!
		 */
		public const int SDL_IMAGE_MAJOR_VERSION =	2;
		public const int SDL_IMAGE_MINOR_VERSION =	6;
		public const int SDL_IMAGE_PATCHLEVEL =		3;

		public enum InitFlags : int32
		{
			JPG =	0x00000001,
			PNG =	0x00000002,
			TIF =	0x00000004,
			WEBP =	0x00000008,
			JXL =   0x00000010,
			AVIF =  0x00000020
		}

		public static void SDL_IMAGE_VERSION(out SDL.Version X)
		{
			X.major = SDL_IMAGE_MAJOR_VERSION;
			X.minor = SDL_IMAGE_MINOR_VERSION;
			X.patch = SDL_IMAGE_PATCHLEVEL;
		}

		[LinkName("IMG_Linked_Version")]
		public static extern SDL.Version Linked_Version();

		[LinkName("IMG_Init")]
		public static extern int32 Init(InitFlags flags);

		[LinkName("IMG_Quit")]
		public static extern void Quit();

		[LinkName("IMG_Load")]
		public static extern SDL.Surface* Load(char8* file);

		[LinkName("IMG_Load_RW")]
		public static extern SDL.Surface* Load_RW(
			SDL.RWOps* src,
			int32 freesrc
		);

		[LinkName("IMG_LoadTyped_RW")]
		public static extern SDL.Surface* LoadTyped_RW(
			SDL.RWOps* src,
			int32 freesrc,
			char8* type
		);

		[LinkName("IMG_LoadTexture")]
		public static extern SDL.Texture* LoadTexture(
			SDL.Renderer* renderer,
			char8* file
		);

		[LinkName("IMG_LoadTexture_RW")]
		public static extern SDL.Texture* LoadTexture_RW(
			SDL.Renderer* renderer,
			SDL.RWOps* src,
			int32 freesrc
		);

		[LinkName("IMG_LoadTextureTyped_RW")]
		public static extern SDL.Texture* LoadTextureTyped_RW(
			SDL.Renderer* renderer,
			SDL.RWOps* src,
			int32 freesrc,
			char8* type
		);

		[LinkName("IMG_isAVIF")]
		public static extern int32 isAVIF(SDL.RWOps* src);

		[LinkName("IMG_isICO")]
		public static extern int32 isICO(SDL.RWOps* src);

		[LinkName("IMG_isCUR")]
		public static extern int32 isCUR(SDL.RWOps* src);

		[LinkName("IMG_isBMP")]
		public static extern int32 isBMP(SDL.RWOps* src);

		[LinkName("IMG_isGIF")]
		public static extern int32 isGIF(SDL.RWOps* src);

		[LinkName("IMG_isJPG")]
		public static extern int32 isJPG(SDL.RWOps* src);

		[LinkName("IMG_isJXL")]
		public static extern int32 isJXL(SDL.RWOps* src);

		[LinkName("IMG_isLBM")]
		public static extern int32 isLBM(SDL.RWOps* src);

		[LinkName("IMG_isPCX")]
		public static extern int32 isPCX(SDL.RWOps* src);

		[LinkName("IMG_isPNG")]
		public static extern int32 isPNG(SDL.RWOps* src);

		[LinkName("IMG_isPNM")]
		public static extern int32 isPNM(SDL.RWOps* src);

		[LinkName("IMG_isSVG")]
		public static extern int32 isSVG(SDL.RWOps* src);

		[LinkName("IMG_isQOI")]
		public static extern int32 isQOI(SDL.RWOps* src);

		[LinkName("IMG_isTIF")]
		public static extern int32 isTIF(SDL.RWOps* src);

		[LinkName("IMG_isXCF")]
		public static extern int32 isXCF(SDL.RWOps* src);

		[LinkName("IMG_isXPM")]
		public static extern int32 isXPM(SDL.RWOps* src);

		[LinkName("IMG_isXV")]
		public static extern int32 isXV(SDL.RWOps* src);

		[LinkName("IMG_isWEBP")]
		public static extern int32 isWEBP(SDL.RWOps* src);

		[LinkName("IMG_LoadAVIF_RW")]
		public static extern SDL.Surface* LoadAVIF_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadICO_RW")]
		public static extern SDL.Surface* LoadICO_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadCUR_RW")]
		public static extern SDL.Surface* LoadCUR_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadBMP_RW")]
		public static extern SDL.Surface* LoadBMP_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadGIF_RW")]
		public static extern SDL.Surface* LoadGIF_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadJPG_RW")]
		public static extern SDL.Surface* LoadJPG_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadJXL_RW")]
		public static extern SDL.Surface* LoadJXL_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadLBM_RW")]
		public static extern SDL.Surface* LoadLBM_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadPCX_RW")]
		public static extern SDL.Surface* LoadPCX_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadPNG_RW")]
		public static extern SDL.Surface* LoadPNG_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadPNM_RW")]
		public static extern SDL.Surface* LoadPNM_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadSVG_RW")]
		public static extern SDL.Surface* LoadSVG_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadQOI_RW")]
		public static extern SDL.Surface* LoadQOI_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadTGA_RW")]
		public static extern SDL.Surface* LoadTGA_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadTIF_RW")]
		public static extern SDL.Surface* LoadTIF_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadXCF_RW")]
		public static extern SDL.Surface* LoadXCF_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadXPM_RW")]
		public static extern SDL.Surface* LoadXPM_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadXV_RW")]
		public static extern SDL.Surface* LoadXV_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadWEBP_RW")]
		public static extern SDL.Surface* LoadWEBP_RW(SDL.RWOps* src);

		[LinkName("IMG_LoadSizedSVG_RW")]
		public static extern SDL.Surface* LoadSizedSVG_RW(SDL.RWOps* src, int32 width, int32 height);

		[LinkName("IMG_ReadXPMFromArrayToRGB888")]
		public static extern SDL.Surface* ReadXPMFromArrayToRGB888(char8** xpm);

		[LinkName("IMG_ReadXPMFromArray")]
		public static extern SDL.Surface* ReadXPMFromArray(char8** xpm);

		[LinkName("IMG_SavePNG")]
		public static extern int32 SavePNG(SDL.Surface* surface, char8* file);

		[LinkName("IMG_SavePNG_RW")]
		public static extern int32 SavePNG_RW(
			SDL.Surface* surface,
			SDL.RWOps* dst,
			int32 freedst
		);

		[LinkName("IMG_SaveJPG")]
		public static extern int32 SaveJPG(SDL.Surface* surface, char8* file, int32 quality);

		[LinkName("IMG_SaveJPG_RW")]
		public static extern int32 SaveJPG_RW(
			SDL.Surface* surface,
			SDL.RWOps* dst,
			int32 freedst,
			int32 quality
		);

		[CRepr]
		public struct Animation
		{
			public int32 w;
			public int32 h;
			public SDL.Surface** frames;
			public int32* delays;
		}

		[LinkName("IMG_LoadAnimation")]
		public static extern Animation* LoadAnimation(char8* file);

		[LinkName("IMG_LoadAnimation_RW")]
		public static extern Animation* LoadAnimation(SDL.RWOps* src, int32 freesrc);

		[LinkName("IMG_LoadAnimationTyped_RW")]
		public static extern Animation* LoadAnimationTyped_RW(SDL.RWOps* src, int32 freesrc, char8* type);

		[LinkName("IMG_FreeAnimation")]
		public static extern void IMG_FreeAnimation(Animation* anim);

		[LinkName("IMG_LoadGIFAnimation_RW")]
		public static extern Animation* LoadGIFAnimation_RW(SDL.RWOps *src);
	}
}
