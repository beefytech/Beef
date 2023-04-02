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
		public const int32 SDL_TTF_MINOR_VERSION =	20;
		public const int32 SDL_TTF_PATCHLEVEL =		2;

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
		public const int32 TTF_HINTING_LIGHT_SUBPIXEL =	4;

		public const int32 TTF_DIRECTION_LTR = 0;
		public const int32 TTF_DIRECTION_RTL = 1;
		public const int32 TTF_DIRECTION_TTB = 2;
		public const int32 TTF_DIRECTION_BTT = 3;

		public static void SDL_TTF_VERSION(out SDL.Version X)
		{
			X.major = SDL_TTF_MAJOR_VERSION;
			X.minor = SDL_TTF_MINOR_VERSION;
			X.patch = SDL_TTF_PATCHLEVEL;
		}

		[LinkName("TTF_Linked_Version")]
		public static extern SDL.Version LinkedVersion();

		[LinkName("TTF_GetFreeTypeVersion")]
		public static extern void GetFreeTypeVersion(int32* major, int32* minor, int32* patch);

		[LinkName("TTF_GetHarfBuzzVersion")]
		public static extern void GetHarfBuzzVersion(int32* major, int32* minor, int32* patch);

		[LinkName("TTF_ByteSwappedUNICODE")]
		public static extern void ByteSwappedUNICODE(SDL.Bool swapped);

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
			int32 freesrc,
			int32 ptsize
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

		[LinkName("TTF_OpenFontDPI")]
		public static extern Font* OpenFontDPI(
			char8* file,
			int32 ptsize,
			uint32 hdpi,
			uint32 vdpi
		);

		[LinkName("TTF_OpenFontIndexDPI")]
		public static extern Font* OpenFontIndexDPI(
			char8* file,
			int32 ptsize,
			int32 index,
			uint32 hdpi,
			uint32 vdpi
		);

		[LinkName("TTF_OpenFontDPIRW")]
		public static extern Font* OpenFontDPIRW(
			SDL.RWOps* src,
			int32 freesrc,
			int32 ptsize,
			uint32 hdpi,
			uint32 vdpi
		);

		[LinkName("TTF_OpenFontIndexDPIRW")]
		public static extern Font* OpenFontIndexDPIRW(
			SDL.RWOps* src,
			int32 freesrc,
			int32 ptsize,
			int32 index,
			uint32 hdpi,
			uint32 vdpi
		);

		[LinkName("TTF_SetFontSizeDPI")]
		public static extern int32 SetFontSizeDPI(
			Font* font,
			int32 ptsize,
			uint32 hdpi,
			uint32 vdpi
		);

		public struct Font;

		[LinkName("TTF_GetFontStyle")]
		public static extern int32 GetFontStyle(Font* font);

		[LinkName("TTF_SetFontStyle")]
		public static extern void SetFontStyle(Font* font, int32 style);

		[LinkName("TTF_GetFontOutline")]
		public static extern int32 GetFontOutline(Font* font);

		[LinkName("TTF_SetFontOutline")]
		public static extern void SetFontOutline(Font* font, int32 outline);

		[LinkName("TTF_GetFontHinting")]
		public static extern int32 GetFontHinting(Font* font);

		[LinkName("TTF_SetFontHinting")]
		public static extern void SetFontHinting(Font* font, int32 hinting);

		[LinkName("TTF_GetFontWrappedAlign")]
		public static extern int32 GetFontWrappedAlign(Font* font);

		[LinkName("TTF_SetFontWrappedAlign")]
		public static extern void SetFontWrappedAlign(Font* font, int32 align);

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
		public static extern void SetFontKerning(Font* font, int32 allowed);

		[LinkName("TTF_FontFaces")]
		public static extern int64 FontFaces(Font* font);

		[LinkName("TTF_FontFaceIsFixedWidth")]
		public static extern int32 FontFaceIsFixedWidth(Font* font);

		[LinkName("TTF_FontFaceFamilyName")]
		public static extern char8* FontFaceFamilyName(Font* font);

		[LinkName("TTF_FontFaceStyleName")]
		public static extern char8* FontFaceStyleName(Font* font);

		[LinkName("TTF_GlyphIsProvided")]
		public static extern int32 GlyphIsProvided(Font* font, uint16 ch);

		[LinkName("TTF_GlyphIsProvided32")]
		public static extern int32 GlyphIsProvided32(Font* font, uint32 ch);

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

		[LinkName("TTF_GlyphMetrics32")]
		public static extern int32 GlyphMetrics32(
			Font* font,
			uint32 ch,
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
			out int32 w,
			out int32 h
		);

		[LinkName("TTF_SizeUTF8")]
		public static extern int32 SizeUTF8(
			Font* font,
			char8* text,
			out int32 w,
			out int32 h
		);

		[LinkName("TTF_SizeUNICODE")]
		public static extern int32 SizeUNICODE(
			Font* font,
			char8* text,
			out int32 w,
			out int32 h
		);

		[LinkName("TTF_MeasureText")]
		public static extern int32 MeasureText(
			Font* font,
			char8* text,
			int32 measure_width,
			out int32 extent,
			out int32 count
		);

		[LinkName("TTF_MeasureUTF8")]
		public static extern int32 MeasureUTF8(
			Font* font,
			char8* text,
			int32 measure_width,
			out int32 extent,
			out int32 count
		);

		[LinkName("TTF_MeasureUNICODE")]
		public static extern int32 MeasureUNICODE(
			Font* font,
			char16* text,
			int32 measure_width,
			out int32 extent,
			out int32 count
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

		[LinkName("TTF_RenderText_Solid_Wrapped")]
		public static extern SDL.Surface* RenderText_Solid_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUTF8_Solid_Wrapped")]
		public static extern SDL.Surface* RenderUTF8_Solid_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUNICODE_Solid_Wrapped")]
		public static extern SDL.Surface* RenderUNICODE_Solid_Wrapped(
			Font* font,
			char16* text,
			SDL.Color fg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderGlyph_Solid")]
		public static extern SDL.Surface* RenderGlyph_Solid(
			Font* font,
			uint16 ch,
			SDL.Color fg
		);

		[LinkName("TTF_RenderGlyph32_Solid")]
		public static extern SDL.Surface* RenderGlyph32_Solid(
			Font* font,
			uint32 ch,
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

		[LinkName("TTF_RenderText_Shaded_Wrapped")]
		public static extern SDL.Surface* RenderText_Shaded_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrappedLength
		);

		[LinkName("TTF_RenderUTF8_Shaded_Wrapped")]
		public static extern SDL.Surface* RenderUTF8_Shaded_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrappedLength
		);

		[LinkName("TTF_RenderUNICODE_Shaded_Wrapped")]
		public static extern SDL.Surface* RenderUNICODE_Shaded_Wrapped(
			Font* font,
			char16* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrappedLength
		);

		[LinkName("TTF_RenderGlyph_Shaded")]
		public static extern SDL.Surface* RenderGlyph_Shaded(
			Font* font,
			uint16 ch,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderGlyph32_Shaded")]
		public static extern SDL.Surface* RenderGlyph32_Shaded(
			Font* font,
			uint32 ch,
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
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUTF8_Blended_Wrapped")]
		public static extern SDL.Surface* RenderUTF8_Blended_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUNICODE_Blended_Wrapped")]
		public static extern SDL.Surface* RenderUNICODE_Blended_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderGlyph_Blended")]
		public static extern SDL.Surface* RenderGlyph_Blended(
			Font* font,
			uint16 ch,
			SDL.Color fg
		);

		[LinkName("TTF_RenderGlyph32_Blended")]
		public static extern SDL.Surface* RenderGlyph32_Blended(
			Font* font,
			uint32 ch,
			SDL.Color fg
		);

		[LinkName("TTF_RenderText_LCD")]
		public static extern SDL.Surface* RenderText_LCD(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderUTF8_LCD")]
		public static extern SDL.Surface* RenderUTF8_LCD(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderUNICODE_LCD")]
		public static extern SDL.Surface* RenderUNICODE_LCD(
			Font* font,
			char16* text,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderText_LCD_Wrapped")]
		public static extern SDL.Surface* RenderText_LCD_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUTF8_LCD_Wrapped")]
		public static extern SDL.Surface* RenderUTF8_LCD_Wrapped(
			Font* font,
			char8* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderUNICODE_LCD_Wrapped")]
		public static extern SDL.Surface* RenderUNICODE_LCD_Wrapped(
			Font* font,
			char16* text,
			SDL.Color fg,
			SDL.Color bg,
			uint32 wrapLength
		);

		[LinkName("TTF_RenderGlyph_LCD")]
		public static extern SDL.Surface* RenderGlyph_LCD(
			Font* font,
			char16 ch,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_RenderGlyph32_LCD")]
		public static extern SDL.Surface* RenderGlyph32_LCD(
			Font* font,
			char32 ch,
			SDL.Color fg,
			SDL.Color bg
		);

		[LinkName("TTF_CloseFont")]
		public static extern void CloseFont(Font* font);

		[LinkName("TTF_Quit")]
		public static extern void Quit();

		[LinkName("TTF_WasInit")]
		public static extern int32 WasInit();

		[LinkName("SDL_GetFontKerningSize")]
		public static extern int32 GetFontKerningSize(
			Font* font,
			int32 prev_index,
			int32 index
		);

		[LinkName("TTF_GetFontKerningSizeGlyphs")]
		public static extern int32 GetFontKerningSizeGlyphs(
			Font* font,
			char16 previous_ch,
			char16 ch
		);

		[LinkName("TTF_SetFontSDF")]
		public static extern int32 SetFontSDF(
			Font* font,
			SDL.Bool on_off
		);

		[LinkName("TTF_GetFontSDF")]
		public static extern SDL.Bool GetFontSDF(
			Font* font
		);

		[LinkName("TTF_SetDirection")]
		public static extern int32 SetDirection(
			int32 direction
		);

		[LinkName("TTF_SetScript")]
		public static extern int32 SetScript(
			int32 script
		);

		[LinkName("TTF_SetFontDirection")]
		public static extern int32 SetFontDirection(
			Font* font,
			int32 direction
		);

		[LinkName("TTF_SetFontScriptName")]
		public static extern int32 SetFontScriptName(
			Font* font,
			char8* script
		);
	}
}