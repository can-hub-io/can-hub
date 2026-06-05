cmake_minimum_required(VERSION 3.16)
project(can_hub VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

add_compile_options(-Wall -Wextra)

include_directories(src)

set(COMMON_SOURCES
    src/version.c
)

add_executable(can-hub src/apps/hub/main.c ${COMMON_SOURCES})
add_executable(can-hub-agent src/apps/agent/main.c ${COMMON_SOURCES})
add_executable(can-hub-cli src/apps/cli/main.c ${COMMON_SOURCES})

install(TARGETS can-hub can-hub-agent can-hub-cli RUNTIME DESTINATION bin)
