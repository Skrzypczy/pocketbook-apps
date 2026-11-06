#!/bin/sh
set -e
cd /src
TRIPLE=arm-obreey-linux-gnueabi
CC=/sdk/usr/bin/${TRIPLE}-clang
CXX=/sdk/usr/bin/${TRIPLE}-clang++
SYSROOT=/sdk/usr/${TRIPLE}/sysroot
OUT=/src/build/reader
MUPDF_OUT=$OUT/mupdf
export LD_LIBRARY_PATH=/sdk/usr/lib
INC="-I/src/apps/Reader -I/src/docs/sdk -I$SYSROOT/usr/include -I$SYSROOT/usr/include/freetype2 -I/src/third_party/mupdf/include -I/src/third_party/crengine/crengine/include -I/src/third_party/crengine_pb"
CFLAGS="-Wall -Wextra -O2 -fPIC -ffunction-sections -fdata-sections"
CXXFLAGS="$CFLAGS -std=gnu++17 -include /src/third_party/crengine_pb/crsetup.h"
DEFS="-DLINUX=1 -D_LINUX=1"

echo "Compiling Reader sources..."
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/main.c -o $OUT/main.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/bar.c -o $OUT/bar.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/config.c -o $OUT/config.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/refresh.c -o $OUT/refresh.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/frontlight.c -o $OUT/frontlight.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/library.c -o $OUT/library.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/settings.c -o $OUT/settings.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/contents.c -o $OUT/contents.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/suggest.c -o $OUT/suggest.o
$CC $CFLAGS $DEFS $INC -c /src/apps/Reader/engine_select.c -o $OUT/engine_select.o
$CXX $CXXFLAGS $DEFS $INC -c /src/apps/Reader/engine_crengine.cpp -o $OUT/engine_crengine.o
$CXX $CXXFLAGS $DEFS $INC -c /src/apps/Reader/engine_mupdf.cpp -o $OUT/engine_mupdf.o

echo "Linking..."
$CXX -o /src/build/Reader.app \
  $OUT/main.o $OUT/bar.o $OUT/config.o $OUT/refresh.o $OUT/frontlight.o \
  $OUT/library.o $OUT/settings.o $OUT/contents.o $OUT/suggest.o $OUT/engine_select.o \
  $OUT/engine_crengine.o $OUT/engine_mupdf.o \
  -Wl,--start-group \
  $OUT/crengine/libcrengine.a $OUT/crengine/chmlib/libchmlib.a $OUT/crengine/antiword/libantiword.a \
  $MUPDF_OUT/libmupdf.a $MUPDF_OUT/libmupdf-third.a \
  -Wl,--end-group \
  -L$SYSROOT/usr/lib -lfreetype -lpng -ljpeg -lz -lcurl \
  -linkview -lpthread -ldl -lm \
  -static-libstdc++ -static-libgcc
ls -la /src/build/Reader.app

