# Cross toolchain: x86_64 Windows via llvm-mingw (UCRT). Used for the
# libcanhub Windows port; point CAN_HUB_MINGW_ROOT (or PATH) at an
# llvm-mingw or gcc mingw-w64 installation.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{CAN_HUB_MINGW_ROOT})
    set(_mingw_bin "$ENV{CAN_HUB_MINGW_ROOT}/bin/")
else()
    set(_mingw_bin "")
endif()

set(CMAKE_C_COMPILER ${_mingw_bin}x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER ${_mingw_bin}x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER ${_mingw_bin}x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
