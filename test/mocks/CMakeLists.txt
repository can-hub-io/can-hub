add_library(port_mocks STATIC
    transport_port_mock.c
    can_port_mock.c
    hub_transport_port_mock.c
    identity_store_mock.c
    authorization_mock.c
    broker_driver.c
)

target_include_directories(port_mocks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ROOT_SRC_DIR}
)
