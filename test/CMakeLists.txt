cmake_minimum_required(VERSION 3.16)
project(can_hub_tests C CXX)

# ROOT_SRC_DIR is the absolute path to src/ in the main project.
get_filename_component(ROOT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src" ABSOLUTE)
set(ROOT_SRC_DIR "${ROOT_SRC_DIR}" CACHE PATH "Path to project src/")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_subdirectory(unit)
