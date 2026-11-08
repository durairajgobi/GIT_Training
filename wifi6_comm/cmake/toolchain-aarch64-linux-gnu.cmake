# Cross-compile for 64-bit Arm embedded Linux (e.g. NXP i.MX8, Raspberry Pi 4/5).
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake
# Inside a Yocto SDK, source the environment script and omit this file instead.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_PREFIX aarch64-linux-gnu- CACHE STRING "Toolchain prefix")
set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

if(DEFINED ENV{SYSROOT})
  set(CMAKE_SYSROOT $ENV{SYSROOT})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
