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

add_unit_test(test_list_message
    SOURCES
        protocol/test_list_message.cpp
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_open_message
    SOURCES
        protocol/test_open_message.cpp
        ${ROOT_SRC_DIR}/protocol/open_message.c
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

add_unit_test(test_admin_message
    SOURCES
        protocol/test_admin_message.cpp
        ${ROOT_SRC_DIR}/protocol/admin_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_reconnect_backoff
    SOURCES
        agent/domain/test_reconnect_backoff.cpp
        ${ROOT_SRC_DIR}/agent/domain/reconnect_backoff.c
)

add_unit_test(test_channel_map
    SOURCES
        agent/domain/test_channel_map.cpp
        ${ROOT_SRC_DIR}/agent/domain/channel_map.c
)

add_unit_test(test_quic_control_channel
    SOURCES
        platform/test_quic_control_channel.cpp
        ${ROOT_SRC_DIR}/platform/linux/quic/quic_control_channel.c
        ${ROOT_SRC_DIR}/platform/linux/shared/message_framer.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_tls_channel
    SOURCES
        platform/test_tls_channel.cpp
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_channel.c
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_client_security.c
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_server_security.c
        ${ROOT_SRC_DIR}/platform/linux/shared/pin_store.c
        ${ROOT_SRC_DIR}/platform/linux/shared/pinned_server_verifier.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_identity.c
        ${ROOT_SRC_DIR}/platform/linux/shared/message_framer.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)
target_link_libraries(test_tls_channel PRIVATE gnutls)

add_unit_test(test_identity_database
    SOURCES
        platform/test_identity_database.cpp
        ${ROOT_SRC_DIR}/platform/linux/sqlite/identity_database.c
)
target_link_libraries(test_identity_database PRIVATE sqlite3)

add_unit_test(test_agent
    SOURCES
        agent/test_agent.cpp
        ${ROOT_SRC_DIR}/agent/agent.c
        ${ROOT_SRC_DIR}/agent/domain/channel_map.c
        ${ROOT_SRC_DIR}/agent/domain/reconnect_backoff.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)
target_link_libraries(test_agent PRIVATE port_mocks)

add_unit_test(test_interface_registry
    SOURCES
        hub/domain/test_interface_registry.cpp
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_client_session
    SOURCES
        hub/domain/test_client_session.cpp
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
)

add_unit_test(test_admin_views
    SOURCES
        hub/domain/test_admin_views.cpp
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_hub_peer
    SOURCES
        hub/domain/test_hub_peer.cpp
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
)

add_unit_test(test_frame_routes
    SOURCES
        hub/domain/test_frame_routes.cpp
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_broker
    SOURCES
        hub/test_broker.cpp
        ${ROOT_SRC_DIR}/hub/broker.c
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/protocol/admin_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)
target_link_libraries(test_broker PRIVATE port_mocks)
