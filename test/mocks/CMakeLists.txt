add_library(port_mocks STATIC
    transport_port_mock.c
    can_port_mock.c
)

target_include_directories(port_mocks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ROOT_SRC_DIR}
)
