# Cross-compile toolchain for PocketBook SDK (larento/pocketbook-sdk or SDK 6.8).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED PB_SDK_ROOT)
  set(PB_SDK_ROOT "/sdk")
endif()

set(PB_TRIPLE "arm-obreey-linux-gnueabi")
set(PB_SYSROOT "${PB_SDK_ROOT}/usr/${PB_TRIPLE}/sysroot")

# Clang 7 from the SDK supports C++17 (needed by modern crengine).
# Fall back to gcc/g++ if clang is absent.
if(EXISTS "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-clang")
  set(CMAKE_C_COMPILER   "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-clang")
  set(CMAKE_CXX_COMPILER "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-clang++")
else()
  set(CMAKE_C_COMPILER   "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-gcc")
  set(CMAKE_CXX_COMPILER "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-g++")
endif()
set(CMAKE_AR           "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP        "${PB_SDK_ROOT}/usr/bin/${PB_TRIPLE}-strip" CACHE FILEPATH "")

set(CMAKE_SYSROOT "${PB_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${PB_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_C_FLAGS_INIT "-fPIC -O2 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "-fPIC -O2 -ffunction-sections -fdata-sections")
