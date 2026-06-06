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
    src/protocol/wire.c
    src/protocol/message_header.c
    src/protocol/frame_message.c
    src/protocol/hello_message.c
    src/protocol/error_message.c
    src/protocol/list_message.c
    src/protocol/open_message.c
    src/protocol/register_message.c
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

set(AGENT_SOURCES
    src/application/agent.c
    src/apps/agent/agent_app.c
    src/domain/channel_map.c
    src/domain/reconnect_backoff.c
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/socketcan/socketcan_adapter.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_client_transport.c
    src/platform/linux/quic/quic_client_security.c
    src/platform/linux/quic/quic_client_transport.c
    src/platform/linux/quic/quic_connection.c
    src/platform/linux/quic/quic_control_channel.c
    src/platform/linux/quic/quic_udp_endpoint.c
)

set(HUB_SOURCES
    src/application/broker.c
    src/apps/hub/hub_app.c
    src/domain/client_session.c
    src/domain/frame_routes.c
    src/domain/interface_registry.c
    src/domain/peer_directory.c
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_server_transport.c
)

add_executable(can-hub src/platform/linux/hub_main.c ${COMMON_SOURCES} ${HUB_SOURCES})
add_executable(can-hub-agent src/platform/linux/agent_main.c ${COMMON_SOURCES} ${AGENT_SOURCES})
target_link_libraries(can-hub-agent PRIVATE ngtcp2_static ngtcp2_crypto_gnutls_static gnutls)
set(CLIENT_SOURCES
    src/platform/linux/clock/clock.c
    src/platform/linux/shared/message_framer.c
    src/platform/linux/tcp/tcp_channel.c
    src/platform/linux/tcp/tcp_client_transport.c
)

add_executable(can-hub-client src/platform/linux/client_main.c ${COMMON_SOURCES} ${CLIENT_SOURCES})
add_executable(can-hub-cli src/platform/linux/cli_main.c ${COMMON_SOURCES})

install(TARGETS can-hub can-hub-agent can-hub-cli RUNTIME DESTINATION bin)
