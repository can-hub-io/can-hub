# Cross toolchain for the fully static musl builds (bootlin toolchains).
# The cross triplet, target processor and static dependency staging prefix
# come from the environment: CAN_HUB_CROSS_TRIPLET, CAN_HUB_PROCESSOR and
# CAN_HUB_SYSROOT (see docker/static.Dockerfile).

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $ENV{CAN_HUB_PROCESSOR})

set(CMAKE_C_COMPILER $ENV{CAN_HUB_CROSS_TRIPLET}-gcc)
set(CMAKE_CXX_COMPILER $ENV{CAN_HUB_CROSS_TRIPLET}-g++)

set(CMAKE_FIND_ROOT_PATH $ENV{CAN_HUB_SYSROOT})
set(CMAKE_C_FLAGS_INIT "-isystem $ENV{CAN_HUB_SYSROOT}/include")
set(CMAKE_CXX_FLAGS_INIT "-isystem $ENV{CAN_HUB_SYSROOT}/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L$ENV{CAN_HUB_SYSROOT}/lib")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
