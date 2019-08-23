/* pngasmrd.h - assembler version of utilities to read a PNG file
 *
 * libpng 1.0.5 - October 15, 1999
 * For conditions of distribution and use, see copyright notice in png.h
 * Copyright (c) 1999 Glenn Randers-Pehrson
 *
 */

#ifdef PNG_ASSEMBLER_CODE_SUPPORTED

/* Set this in the makefile for VC++ on Pentium, not in pngconf.h */
#ifdef PNG_USE_PNGVCRD
/* Platform must be Pentium.  Makefile must assemble and load pngvcrd.c .
 * MMX will be detected at run time and used if present.
 */
#define PNG_HAVE_ASSEMBLER_COMBINE_ROW
#define PNG_HAVE_ASSEMBLER_READ_INTERLACE
#define PNG_HAVE_ASSEMBLER_READ_FILTER_ROW
#endif

/* Set this in the makefile for gcc on Pentium, not in pngconf.h */
#ifdef PNG_USE_PNGGCCRD
/* Platform must be Pentium.  Makefile must assemble and load pnggccrd.c
 * (not available in libpng 1.0.5).
 * MMX will be detected at run time and used if present.
 */
#define PNG_HAVE_ASSEMBLER_COMBINE_ROW
#define PNG_HAVE_ASSEMBLER_READ_INTERLACE
#define PNG_HAVE_ASSEMBLER_READ_FILTER_ROW
#endif

#endif
