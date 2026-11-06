#!/bin/sh
# Build Simple Reader inside PocketBook SDK container (crengine + MuPDF + UI).
set -e
cd /src

export LD_LIBRARY_PATH=/sdk/usr/lib:${LD_LIBRARY_PATH}
export PATH=/sdk/usr/bin:/opt/bin:$PATH
TRIPLE=arm-obreey-linux-gnueabi
if [ -x /sdk/usr/bin/${TRIPLE}-clang ]; then
  CC=/sdk/usr/bin/${TRIPLE}-clang
  CXX=/sdk/usr/bin/${TRIPLE}-clang++
else
  CC=/sdk/usr/bin/${TRIPLE}-gcc
  CXX=/sdk/usr/bin/${TRIPLE}-g++
fi
AR=/sdk/usr/bin/${TRIPLE}-ar
SYSROOT=/sdk/usr/${TRIPLE}/sysroot
OUT=/src/build/reader
mkdir -p "$OUT" /src/build

echo "=== Building crengine ==="
mkdir -p "$OUT/crengine"
cd "$OUT/crengine"
cmake /src/third_party/crengine_pb -DCMAKE_TOOLCHAIN_FILE=/src/third_party/toolchain-pocketbook.cmake -DPB_SDK_ROOT=/sdk -DCMAKE_BUILD_TYPE=Release
cmake --build . -j2
cd /src

echo "=== Building MuPDF ==="
MUPDF=/src/third_party/mupdf
MUPDF_OUT="$OUT/mupdf"
mkdir -p "$MUPDF_OUT" /src/third_party
if [ ! -f "$MUPDF/include/mupdf/fitz.h" ]; then
  echo "Fetching MuPDF 1.27.2 source..."
  cd /src/third_party
  if [ ! -f mupdf-1.27.2-source.tar.gz ]; then
    curl -L -o mupdf-1.27.2-source.tar.gz https://mupdf.com/downloads/archive/mupdf-1.27.2-source.tar.gz
  fi
  rm -rf mupdf
  tar -xzf mupdf-1.27.2-source.tar.gz
  mv mupdf-1.27.2-source mupdf
  cd /src
fi
rm -rf "$MUPDF/thirdparty/freeglut" "$MUPDF/thirdparty/curl" "$MUPDF/thirdparty/tesseract" "$MUPDF/thirdparty/leptonica" "$MUPDF/thirdparty/zint" "$MUPDF/thirdparty/zxing-cpp" "$MUPDF/thirdparty/brotli" 2>/dev/null || true

if [ ! -f "$MUPDF_OUT/libmupdf.a" ]; then
  XCFLAGS="-fPIC -O2 -ffunction-sections -fdata-sections -fvisibility=hidden -DNOBUILTINFONT -DFZ_ENABLE_HYPHEN=0 -DFZ_PLOTTERS_CMYK=0 -I$SYSROOT/usr/include -I$SYSROOT/usr/include/freetype2"
  make -C "$MUPDF" build=release shared=no verbose=yes OS=Other CC="$CC" CXX="$CXX" AR="$AR" USE_SYSTEM_FREETYPE=yes SYS_FREETYPE_CFLAGS="-I$SYSROOT/usr/include/freetype2" SYS_FREETYPE_LIBS="-L$SYSROOT/usr/lib -lfreetype" USE_SYSTEM_ZLIB=yes SYS_ZLIB_CFLAGS="-I$SYSROOT/usr/include" SYS_ZLIB_LIBS="-L$SYSROOT/usr/lib -lz" USE_SYSTEM_LIBJPEG=yes SYS_LIBJPEG_LIBS="-L$SYSROOT/usr/lib -ljpeg" USE_SYSTEM_HARFBUZZ=no barcode=no brotli=no mujs=no tesseract=no XCFLAGS="$XCFLAGS" OUT="$MUPDF_OUT" libs
else
  echo "MuPDF already built, skipping"
fi

echo "=== Linking Reader.app ==="
exec /src/scripts/link_reader.sh
