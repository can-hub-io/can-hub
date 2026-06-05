set(CEST_DIR ${CMAKE_SOURCE_DIR}/vendor)

# add_unit_test(<name> SOURCES <test.cpp> [real_src.c ...])
#
# Registers a cest executable. Includes: cest + src/.
function(add_unit_test TEST_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    add_executable(${TEST_NAME} ${ARG_SOURCES})

    target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)

    target_include_directories(${TEST_NAME} PRIVATE
        ${CEST_DIR}
        ${ROOT_SRC_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()

# ── Registered tests ─────────────────────────────────────────────────────────

add_unit_test(test_version
    SOURCES
        version/test_version.cpp
        ${ROOT_SRC_DIR}/version.c
)
