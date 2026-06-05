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

add_unit_test(test_message_header
    SOURCES
        protocol/test_message_header.cpp
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_frame_message
    SOURCES
        protocol/test_frame_message.cpp
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_hello_message
    SOURCES
        protocol/test_hello_message.cpp
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_error_message
    SOURCES
        protocol/test_error_message.cpp
        ${ROOT_SRC_DIR}/protocol/error_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_register_message
    SOURCES
        protocol/test_register_message.cpp
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_reconnect_backoff
    SOURCES
        domain/test_reconnect_backoff.cpp
        ${ROOT_SRC_DIR}/domain/reconnect_backoff.c
)

add_unit_test(test_channel_map
    SOURCES
        domain/test_channel_map.cpp
        ${ROOT_SRC_DIR}/domain/channel_map.c
)
