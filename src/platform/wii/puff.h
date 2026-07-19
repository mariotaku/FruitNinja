/* puff.h
 * Copyright (C) 2002-2013 Mark Adler
 * For conditions of distribution and use, see copyright notice in puff.c
 * version 2.3, 21 Jan 2013
 */

// Port specific: vendored public-domain puff (zlib contrib) -- one-shot boot-splash
// inflate, no libz dep. See puff.cpp for the copyright/license text and
// src/platform/wii/SplashBootScreen.cpp for the call site. puff.cpp is
// compiled as C++ (see its header comment); no extern "C" needed here since
// both the declaration (this header, included from SplashBootScreen.cpp) and
// the definition (puff.cpp) now use ordinary C++ linkage.

#ifndef NIL
#  define NIL ((unsigned char *)0)      /* for no output option */
#endif

int puff(unsigned char *dest,           /* pointer to destination pointer */
         unsigned long *destlen,        /* amount of output space */
         const unsigned char *source,   /* pointer to source data pointer */
         unsigned long *sourcelen);     /* amount of input available */
