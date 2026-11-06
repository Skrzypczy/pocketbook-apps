/* PocketBook Era build of koreader/crengine — SDK libs only. */
#ifndef CRSETUP_H_INCLUDED
#define CRSETUP_H_INCLUDED

#include <stddef.h>
#ifdef __cplusplus
#include <cstdint>
extern "C" {
#endif
/* PocketBook FreeType is older than 2.10 — stub provided in ft_compat.c */
const char *FT_Error_String(int error_code);
#ifdef __cplusplus
}
#endif

#define LINUX 1
#define _LINUX 1

#define USE_ZLIB 1
#define USE_ZSTD 0

#define DOCUMENT_CACHING_MIN_SIZE 0x10000
#define DOCUMENT_CACHING_SIZE_THRESHOLD 0x100000

#define CHM_SUPPORT_ENABLED 1
#define CR3_ANTIWORD_PATCH 1
#define ENABLE_ANTIWORD 1
#define USE_MD4C 0

#define CR_EMULATE_GETTEXT 1

#define ARBITRARY_IMAGE_SCALE_ENABLED 1
#define MAX_IMAGE_SCALE_MUL 2
#define USE_GIF 1
#define USE_LIBJPEG 1
#define USE_LIBPNG 1
#define USE_LIBWEBP 0
#define USE_LUNASVG 0
#define USE_NANOSVG 0

#define MATHML_SUPPORT 0
#define USE_SRELL_REGEX 0

#define COLOR_BACKBUFFER 0
#define CR_INTERNAL_PAGE_ORIENTATION 0
#define GRAY_BACKBUFFER_BITS 8
#define GRAY_INVERSE 0

#define HAVE_OFF64_T 1
#ifdef HAVE_OFF64_T
# define HAVE_STAT64 1
# define _LARGEFILE64_SOURCE 1
#endif
#define LVLONG_FILE_SUPPORT 0
#define USE_ANSI_FILES 1
#define FILE_STREAM_BUFFER_SIZE 0x20000
#define ZIP_STREAM_BUFFER_SIZE 0x40000

#define CR_USE_THREADS 0
#define LDOM_USE_OWN_MEM_MAN 1

#define USE_LIMITED_FONT_SIZES_SET 0
#define USE_BITMAP_FONTS 0
#define USE_WIN32_FONTS 0
#define USE_GLYPHCACHE_HASHTABLE 0
#define GLYPH_CACHE_SIZE 0x40000
#define ALLOW_KERNING 1
#define USE_FONTCONFIG 0
#define USE_FREETYPE 1
#define USE_FRIBIDI 0
#define USE_HARFBUZZ 0
#define USE_LIBUNIBREAK 0
#define USE_UTF8PROC 0

#define CR_ENABLE_PAGE_IMAGE_CACHE 0

#endif /* CRSETUP_H_INCLUDED */
