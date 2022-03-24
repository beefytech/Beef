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
		public const int SDL_IMAGE_MINOR_VERSION =	0;
		public const int SDL_IMAGE_PATCHLEVEL =		2;

		public enum InitFlags : int32
		{
			JPG =	0x00000001,
			PNG =	0x00000002,
			TIF =	0x00000004,
			WEBP =	0x00000008
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
		public static extern int Init(InitFlags flags);

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

		[LinkName("IMG_ReadXPMFromArray")]
		public static extern SDL.Surface* ReadXPMFromArray(char8** xpm);

		[LinkName("IMG_SavePNG")]
		public static extern int32 SavePNG(SDL.Surface* surface, char8* file);

		[LinkName("IMG_SavePNG_RW")]
		public static extern int SavePNG_RW(
			SDL.Surface* surface,
			SDL.RWOps* dst,
			int32 freedst
		);

		[LinkName("IMG_SaveJPG")]
		public static extern int32 SaveJPG(SDL.Surface* surface, char8* file, int32 quality);

		[LinkName("IMG_SaveJPG_RW")]
		public static extern int SaveJPG_RW(
			SDL.Surface* surface,
			SDL.RWOps* dst,
			int32 freedst,
			int32 quality
		);
	}
}
