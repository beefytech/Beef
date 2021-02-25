/* SDL2# - C# Wrapper for SDL2
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
 
using System;

namespace SDL2
{
	public static class SDLTTF
	{
		/* Similar to the headers, this is the version we're expecting to be
		 * running with. You will likely want to check this somewhere in your
		 * program!
		 */
		public const int32 SDL_TTF_MAJOR_VERSION =	2;
		public const int32 SDL_TTF_MINOR_VERSION =	0;
		public const int32 SDL_TTF_PATCHLEVEL =		12;

		public const int32 UNICODE_BOM_NATIVE =	0xFEFF;
		public const int32 UNICODE_BOM_SWAPPED = 0xFFFE;

		public const int32 TTF_STYLE_NORMAL = 0x00;
		public const int32 TTF_STYLE_BOLD = 0x01;
		public const int32 TTF_STYLE_ITALIC = 0x02;
		public const int32 TTF_STYLE_UNDERLINE = 0x04;
		public const int32 TTF_STYLE_STRIKETHROUGH = 0x08;

		public const int32 TTF_HINTING_NORMAL =	0;
		public const int32 TTF_HINTING_LIGHT =	1;
		public const int32 TTF_HINTING_MONO =	2;
		public const int32 TTF_HINTING_NONE =	3;

		public static void SDL_TTF_VERSION(out SDL.Version X)
		{
			X.major = SDL_TTF_MAJOR_VERSION;
			X.minor = SDL_TTF_MINOR_VERSION;
			X.patch = SDL_TTF_PATCHLEVEL;
		}

		[LinkName("TTF_LinkedVersion")]
		public static extern SDL.Version LinkedVersion();

		[LinkName("TTF_ByteSwappedUNICODE")]
		public static extern void ByteSwappedUNICODE(int32 swapped);

		[LinkName("TTF_Init")]
		public static extern int32 Init();

		/* IntPtr refers to a TTF_Font* */
		[LinkName("TTF_OpenFont")]
		public static extern Font* OpenFont(char8* file, int32 ptsize);

		/* src refers to an SDL_RWops*, IntPtr to a TTF_Font* */
		/* THIS IS A PUBLIC RWops FUNCTION! */
		[LinkName("TTF_OpenFontRW")]
		public static extern Font* OpenFontRW(
			SDL.RWOps* src,
			int freesrc,
			int ptsize
		);

		/* IntPtr refers to a TTF_Font* */
		[LinkName("TTF_OpenFontIndex")]
		public static extern Font* OpenFontIndex(
			char8* file,
			int32 ptsize,
			int64 index
		);

		/* src refers to an SDL_RWops*, IntPtr to a TTF_Font* */
		/* THIS IS A PUBLIC RWops FUNCTION! */
		[LinkName("TTF_OpenFontIndexRW")]
		public static extern Font* OpenFontIndexRW(
			SDL.RWOps* src,
			int32 freesrc,
			int32 ptsize,
			int64 index
		);

		public struct Font;
		
		[LinkName("TTF_GetFontStyle")]
		public static extern int GetFontStyle(Font* font);

		[LinkName("TTF_SetFontStyle")]
		public static extern void SetFontStyle(Font* font, int32 style);

		[LinkName("TTF_GetFontOutline")]
		public static extern int GetFontOutline(Font* font);

		[LinkName("TTF_SetFontOutline")]
		public static extern void SetFontOutline(Font* font, int32 outline);
		
		[LinkName("TTF_GetFontHinting")]
		public static extern int GetFontHinting(Font* font);

		[LinkName("TTF_SetFontHinting")]
		public static extern void SetFontHinting(Font* font, int32 hinting);

		[LinkName("TTF_FontHeight")]
		public static extern int32 FontHeight(Font* font);
		
		[LinkName("TTF_FontAscent")]
		public static extern int32 FontAscent(Font* font);

		[LinkName("TTF_FontDescent")]
		public static extern int32 FontDescent(Font* font);

		[LinkName("TTF_FontLineSkip")]
		public static extern int32 FontLineSkip(Font* font);

		[LinkName("TTF_GetFontKerning")]
		public static extern int32 GetFontKerning(Font* font);

		[LinkName("TTF_SetFontKerning")]
		public static extern void SetFontKerning(Font* font, int allowed);
		
		[LinkName("TTF_FontFaces")]
		public static extern int64 FontFaces(Font* font);
		
		[LinkName("TTF_FontFaceIsFixedWidth")]
		public static extern int FontFaceIsFixedWidth(Font* font);

		[LinkName("TTF_FontFaceFamilyName")]
		public static extern char8* FontFaceFamilyName(Font* font);

		[LinkName("TTF_FontFaceStyleName")]
		public static extern char8* FontFaceStyleName(Font* font);
		
		[LinkName("TTF_GlyphIsProvided")]
		public static extern int32 GlyphIsProvided(Font* font, uint16 ch);
		
		[LinkName("TTF_GlyphMetrics")]
		public static extern int32 GlyphMetrics(
			Font* font,
			uint16 ch,
			out int32 minx,
			out int32 maxx,
			out int32 miny,
			out int32 maxy,
			out int32 advance
		);

		[LinkName("TTF_SizeText")]
		public static extern int32 SizeText(
			Font* font,
			char8* text,
			out int w,
			out int h
		);

		[LinkName("TTF_SizeUTF8")]
		public static extern int32 SizeUTF8(
			Font* font,
			char8* text,
			out int w,
			out int h
		);

		[LinkName("TTF_SizeUNICODE")]
		public static extern int32 SizeUNICODE(
			Font* font,
			char8* text,
			out int32 w,
			out int32 h
		);

		[LinkName("TTF_RenderText_Solid")]
		public static extern SDL.Surface* RenderText_Solid(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderUTF8_Solid")]
		public static extern SDL.Surface* RenderUTF8_Solid(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderUNICODE_Solid")]
		public static extern SDL.Surface* RenderUNICODE_Solid(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderGlyph_Solid")]
		public static extern SDL.Surface* RenderGlyph_Solid(
			Font* font,
			uint16 ch,
			SDL.Color fg
		);

		[LinkName("TTF_RenderText_Shaded")]
		public static extern SDL.Surface* RenderText_Shaded(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderUTF8_Shaded")]
		public static extern SDL.Surface* RenderUTF8_Shaded(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderUNICODE_Shaded")]
		public static extern SDL.Surface* RenderUNICODE_Shaded(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderGlyph_Shaded")]
		public static extern SDL.Surface* RenderGlyph_Shaded(
			Font* font,
			uint16 ch,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderText_Blended")]
		public static extern SDL.Surface* RenderText_Blended(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderUTF8_Blended")]
		public static extern SDL.Surface* RenderUTF8_Blended(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderUNICODE_Blended")]
		public static extern SDL.Surface* RenderUNICODE_Blended(
			Font* font,
			char8* text,
			SDL.Color fg
		);

		[LinkName("TTF_RenderText_Blended_Wrapped")]
		public static extern SDL.Surface* RenderText_Blended_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint wrapped
		);

		[LinkName("TTF_RenderUTF8_Blended_Wrapped")]
		public static extern SDL.Surface* RenderUTF8_Blended_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint wrapped
		);

		[LinkName("TTF_RenderUNICODE_Blended_Wrapped")]
		public static extern SDL.Surface* RenderUNICODE_Blended_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint wrapped
		);

		[LinkName("TTF_RenderGlyph_Blended")]
		public static extern SDL.Surface* RenderGlyph_Blended(
			Font* font,
			uint16 ch,
			SDL.Color fg
		);

		[LinkName("TTF_CloseFont")]
		public static extern void CloseFont(Font* font);

		[LinkName("TTF_Quit")]
		public static extern void Quit();

		[LinkName("TTF_WasInit")]
		public static extern int WasInit();

		[LinkName("SDL_GetFontKerningSize")]
		public static extern int GetFontKerningSize(
			Font* font,
			int prev_index,
			int index
		);
	}
}