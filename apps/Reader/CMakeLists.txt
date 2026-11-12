cmake_minimum_required(VERSION 3.10)
project(SimpleReader C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

if(NOT DEFINED PB_SDK_ROOT)
  set(PB_SDK_ROOT "/sdk")
endif()
set(PB_TRIPLE "arm-obreey-linux-gnueabi")
set(PB_SYSROOT "${PB_SDK_ROOT}/usr/${PB_TRIPLE}/sysroot")

set(READER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_subdirectory(${REPO_ROOT}/third_party/crengine_pb ${CMAKE_BINARY_DIR}/crengine_pb)

# ---- MuPDF static library (minimal feature set) ----
set(MUPDF_ROOT ${REPO_ROOT}/third_party/mupdf)
set(MUPDF_GENERATED ${CMAKE_BINARY_DIR}/mupdf_gen)
file(MAKE_DIRECTORY ${MUPDF_GENERATED})

# Generate required tables if not present via host; for cross we ship pre-generated stubs
# Prefer building with make in scripts/build_reader.sh; here we list core sources.
include(${CMAKE_CURRENT_SOURCE_DIR}/mupdf_sources.cmake OPTIONAL)

add_executable(Reader.app
  main.c
  bar.c
  config.c
  debug.c
  refresh.c
  frontlight.c
  library.c
  settings.c
  contents.c
  suggest.c
  extensions.c
  engine_select.c
  engine_crengine.cpp
  engine_mupdf.cpp
)

target_include_directories(Reader.app PRIVATE
  ${READER_DIR}
  ${PB_SYSROOT}/usr/include
  ${PB_SYSROOT}/usr/include/freetype2
  ${REPO_ROOT}/docs/sdk
  ${REPO_ROOT}/third_party/mupdf/include
  ${REPO_ROOT}/third_party/crengine/crengine/include
  ${REPO_ROOT}/third_party/crengine_pb
)

target_compile_options(Reader.app PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(Reader.app PRIVATE LINUX=1 _LINUX=1)

target_link_libraries(Reader.app PRIVATE
  crengine
  ${PB_SYSROOT}/usr/lib/libfreetype.a
  ${PB_SYSROOT}/usr/lib/libpng.a
  ${PB_SYSROOT}/usr/lib/libjpeg.a
  ${PB_SYSROOT}/usr/lib/libz.a
  inkview
  pthread
  dl
  m
  stdc++
)

# Prefer dynamic inkview from device; static libstdc++/libgcc
set_target_properties(Reader.app PROPERTIES
  LINK_FLAGS "-static-libstdc++ -static-libgcc -Wl,--gc-sections"
)
