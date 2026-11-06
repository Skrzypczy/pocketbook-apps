cmake_minimum_required(VERSION 3.10)
project(crengine_pb C CXX)

set(CRE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../crengine")
set(CRE_DIR  "${CRE_ROOT}/crengine")
set(CRE_3RD  "${CRE_ROOT}/thirdparty")

set(CR3_ANTIWORD_PATCH 1)
add_subdirectory("${CRE_3RD}/chmlib" "${CMAKE_CURRENT_BINARY_DIR}/chmlib")
add_subdirectory("${CRE_3RD}/antiword" "${CMAKE_CURRENT_BINARY_DIR}/antiword")
target_compile_definitions(antiword PUBLIC ANTIWORD_EXTERNAL_IO=1 CR3_ANTIWORD_PATCH=1)

add_library(crengine STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/ft_compat.c
  ${CMAKE_CURRENT_SOURCE_DIR}/xxhash.c
  ${CRE_DIR}/src/chmfmt.cpp
  ${CRE_DIR}/src/cp_stats.cpp
  ${CRE_DIR}/src/cri18n.cpp
  ${CRE_DIR}/src/crtxtenc.cpp
  ${CRE_DIR}/src/docxfmt.cpp
  ${CRE_DIR}/src/epubfmt.cpp
  ${CRE_DIR}/src/fb3fmt.cpp
  ${CRE_DIR}/src/hist.cpp
  ${CRE_DIR}/src/hyphman.cpp
  ${CRE_DIR}/src/lstridmap.cpp
  ${CRE_DIR}/src/lvdocview.cpp
  ${CRE_DIR}/src/lvdrawbuf.cpp
  ${CRE_DIR}/src/lvfnt.cpp
  ${CRE_DIR}/src/lvfntman.cpp
  ${CRE_DIR}/src/lvimg.cpp
  ${CRE_DIR}/src/lvmemman.cpp
  ${CRE_DIR}/src/lvopc.cpp
  ${CRE_DIR}/src/lvpagesplitter.cpp
  ${CRE_DIR}/src/lvrend.cpp
  ${CRE_DIR}/src/lvstream.cpp
  ${CRE_DIR}/src/lvstring.cpp
  ${CRE_DIR}/src/lvstsheet.cpp
  ${CRE_DIR}/src/lvstyles.cpp
  ${CRE_DIR}/src/lvtextfm.cpp
  ${CRE_DIR}/src/lvtinydom.cpp
  ${CRE_DIR}/src/lvxml.cpp
  ${CRE_DIR}/src/odtfmt.cpp
  ${CRE_DIR}/src/odxutil.cpp
  ${CRE_DIR}/src/pdbfmt.cpp
  ${CRE_DIR}/src/props.cpp
  ${CRE_DIR}/src/renderutil.cpp
  ${CRE_DIR}/src/rtfimp.cpp
  ${CRE_DIR}/src/textlang.cpp
  ${CRE_DIR}/src/txtselector.cpp
  ${CRE_DIR}/src/wordfmt.cpp
  ${CRE_DIR}/qimagescale/qimagescale.cpp
)

target_compile_features(crengine PRIVATE cxx_std_17)
target_compile_options(crengine PRIVATE -Wall)
target_compile_options(crengine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
target_compile_options(crengine PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/crsetup.h>
)
# Force-include crsetup for C sources that need FEATURE macros (none currently)
target_include_directories(crengine PUBLIC
  ${CRE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(crengine PRIVATE XXH_NAMESPACE=CR)
target_include_directories(crengine PRIVATE
  ${CRE_3RD}/antiword
  ${CRE_3RD}/chmlib/src
  ${PB_SYSROOT}/usr/include
  ${PB_SYSROOT}/usr/include/freetype2
)
target_compile_definitions(crengine PRIVATE
  LINUX=1
  _LINUX=1
)
target_link_libraries(crengine PUBLIC chmlib antiword)

# Export for parent
add_library(crengine::crengine ALIAS crengine)
