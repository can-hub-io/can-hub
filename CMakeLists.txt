cmake_minimum_required(VERSION 3.16)
project(can_hub VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

add_compile_options(-Wall -Wextra)

option(CAN_HUB_STATIC "Fully static binaries against a static GnuTLS stack" OFF)
if(CAN_HUB_STATIC)
    add_compile_options(-ffunction-sections -fdata-sections)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -Wl,--gc-sections")
    set(GNUTLS_STATIC_EXTRA_LIBRARIES hogweed nettle gmp atomic)
endif()

include_directories(src)

set(COMMON_SOURCES
    src/version.c
    src/protocol/wire.c
    src/protocol/admin_message.c
    src/protocol/message_header.c
    src/protocol/frame_message.c
    src/protocol/hello_message.c
    src/protocol/error_message.c
    src/protocol/list_message.c
    src/protocol/open_message.c
    src/protocol/register_message.c
    src/protocol/subscribe_message.c
    src/protocol/ifconfig_message.c
)

include(FetchContent)
set(ENABLE_GNUTLS ON CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ngtcp2
    GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
    GIT_TAG v1.23.0
)
FetchContent_MakeAvailable(ngtcp2)

FetchContent_Declare(sqlite3
    URL https://sqlite.org/2026/sqlite-amalgamation-3530200.zip
    URL_HASH SHA256=8a310d0a16c7a90cacd4c884e70faa51c902afed2a89f63aaa0126ab83558a32
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=0)

set(AGENT_SOURCES
    src/agent/agent.c
    src/agent/agent_app.c
    src/agent/domain/channel_map.c
    src/agent/domain/echo_correlator.c
    src/agent/domain/reconnect_backoff.c
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/connect_url.c
    src/platform/linux/shared/epoll_registry.c
    src/platform/linux/shared/pin_store.c
    src/platform/linux/shared/pinned_server_verifier.c
    src/platform/linux/shared/tls_identity.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/socketcan/socketcan_adapter.c
    src/platform/linux/socketcan/can_netlink.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_client_transport.c
    src/platform/linux/tls/tls_channel.c
    src/platform/linux/tls/tls_client_security.c
    src/platform/linux/tls/tls_client_transport.c
    src/platform/linux/quic/quic_client_security.c
    src/platform/linux/quic/quic_client_transport.c
    src/platform/linux/quic/quic_connection.c
    src/platform/linux/quic/quic_control_channel.c
    src/platform/linux/quic/quic_egress.c
    src/platform/linux/quic/quic_udp_endpoint.c
)

set(HUB_SOURCES
    src/hub/broker.c
    src/hub/hub_app.c
    src/hub/domain/admin_views.c
    src/hub/domain/client_session.c
    src/hub/domain/frame_routes.c
    src/hub/domain/hub_peer.c
    src/hub/domain/interface_registry.c
    src/hub/domain/peer_directory.c
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/connect_url.c
    src/platform/linux/shared/epoll_registry.c
    src/platform/linux/shared/pin_store.c
    src/platform/linux/shared/tls_identity.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_server_transport.c
    src/platform/linux/tls/tls_channel.c
    src/platform/linux/tls/tls_server_security.c
    src/platform/linux/tls/tls_server_transport.c
    src/platform/linux/quic/quic_connection.c
    src/platform/linux/quic/quic_control_channel.c
    src/platform/linux/quic/quic_egress.c
    src/platform/linux/quic/quic_server_security.c
    src/platform/linux/quic/quic_server_transport.c
    src/platform/linux/sqlite/identity_database.c
)

add_executable(can-hub src/platform/linux/hub_main.c ${COMMON_SOURCES} ${HUB_SOURCES})
target_link_libraries(can-hub PRIVATE ngtcp2_static ngtcp2_crypto_gnutls_static gnutls sqlite3 ${GNUTLS_STATIC_EXTRA_LIBRARIES})
add_executable(can-hub-agent src/platform/linux/agent_main.c ${COMMON_SOURCES} ${AGENT_SOURCES})
target_link_libraries(can-hub-agent PRIVATE ngtcp2_static ngtcp2_crypto_gnutls_static gnutls ${GNUTLS_STATIC_EXTRA_LIBRARIES})
set(CLIENT_SOURCES
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/connect_url.c
    src/platform/linux/shared/epoll_registry.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_client_transport.c
)

set(CLIENT_TLS_SOURCES
    src/platform/linux/shared/pin_store.c
    src/platform/linux/shared/pinned_server_verifier.c
    src/platform/linux/shared/tls_identity.c
    src/platform/linux/tls/tls_channel.c
    src/platform/linux/tls/tls_client_security.c
    src/platform/linux/tls/tls_client_transport.c
    src/platform/linux/quic/quic_client_security.c
    src/platform/linux/quic/quic_client_transport.c
    src/platform/linux/quic/quic_connection.c
    src/platform/linux/quic/quic_control_channel.c
    src/platform/linux/quic/quic_egress.c
    src/platform/linux/quic/quic_udp_endpoint.c
)

set(SOCKETCAND_SOURCES
    src/socketcand/socketcand_bridge.c
    src/socketcand/socketcand_app.c
    src/socketcand/domain/ascii_framer.c
    src/socketcand/domain/socketcand_codec.c
    src/socketcand/domain/interface_catalogue.c
    src/socketcand/domain/connection_table.c
    src/socketcand/domain/beacon_xml.c
    src/platform/linux/socketcand/socketcand_server.c
    src/platform/linux/socketcand/beacon_udp.c
)

add_executable(can-hub-client src/platform/linux/client_main.c ${COMMON_SOURCES} ${CLIENT_SOURCES} ${CLIENT_TLS_SOURCES} ${SOCKETCAND_SOURCES})
target_link_libraries(can-hub-client PRIVATE ngtcp2_static ngtcp2_crypto_gnutls_static gnutls ${GNUTLS_STATIC_EXTRA_LIBRARIES})
add_executable(can-hub-cli src/platform/linux/cli_main.c ${COMMON_SOURCES} ${CLIENT_SOURCES})

install(TARGETS can-hub can-hub-agent can-hub-cli RUNTIME DESTINATION bin)

option(CAN_HUB_PACKAGING "Generate per-binary Debian packages with CPack" OFF)
if(CAN_HUB_PACKAGING)
    include(${CMAKE_CURRENT_SOURCE_DIR}/packaging/cpack-deb.cmake)
endif()
