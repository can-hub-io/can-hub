# The instruction-set flag for the ARM AES engine reaches exactly one source
# file. See the note in cmake/picotls.cmake: on 32-bit ARM it also enables NEON,
# which the armhf baseline does not guarantee.
if(CAN_HUB_ARM_CRYPTO_FLAGS)
    set_source_files_properties(${ROOT_SRC_DIR}/platform/linux/shared/tls_aes_armv8.c
        PROPERTIES COMPILE_OPTIONS "${CAN_HUB_ARM_CRYPTO_FLAGS}")
endif()

set(CEST_DIR ${CMAKE_SOURCE_DIR}/vendor)

find_package(Threads REQUIRED)

# add_unit_test(<name> SOURCES <test.cpp> [real_src.c ...])
#
# Registers a cest executable. Includes: cest + src/.
function(add_unit_test TEST_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    add_executable(${TEST_NAME} ${ARG_SOURCES})

    target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter ${CAN_HUB_TEST_WARNINGS})

    target_include_directories(${TEST_NAME} PRIVATE
        ${CEST_DIR}
        ${ROOT_SRC_DIR}
        ${ROOT_SRC_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()

# ── Registered tests ─────────────────────────────────────────────────────────

add_unit_test(test_version
    SOURCES
        version/test_version.cpp
        ${ROOT_SRC_DIR}/version.c
)

add_unit_test(test_log
    SOURCES
        platform/test_log.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/log.c
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

add_unit_test(test_subscribe_message
    SOURCES
        protocol/test_subscribe_message.cpp
        ${ROOT_SRC_DIR}/protocol/subscribe_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_ifconfig_message
    SOURCES
        protocol/test_ifconfig_message.cpp
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_interface_status_message
    SOURCES
        protocol/test_interface_status_message.cpp
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
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

add_unit_test(test_txn_message
    SOURCES
        protocol/test_txn_message.cpp
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_txn_steps_message
    SOURCES
        protocol/test_txn_steps_message.cpp
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
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
        shared/test_reconnect_backoff.cpp
        ${ROOT_SRC_DIR}/shared/reconnect_backoff.c
)

add_unit_test(test_channel_map
    SOURCES
        agent/domain/test_channel_map.cpp
        ${ROOT_SRC_DIR}/agent/domain/channel_map.c
)

add_unit_test(test_echo_correlator
    SOURCES
        agent/domain/test_echo_correlator.cpp
        ${ROOT_SRC_DIR}/agent/domain/echo_correlator.c
)

add_unit_test(test_tx_pacer
    SOURCES
        agent/domain/test_tx_pacer.cpp
        ${ROOT_SRC_DIR}/agent/domain/tx_pacer.c
)

add_unit_test(test_quic_control_channel
    SOURCES
        platform/test_quic_control_channel.cpp
        ${ROOT_SRC_DIR}/platform/linux/quic/quic_control_channel.c
        ${ROOT_SRC_DIR}/platform/linux/shared/message_framer.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_connect_url
    SOURCES
        platform/test_connect_url.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/connect_url.c
)

add_unit_test(test_interface_settings_text
    SOURCES
        platform/test_interface_settings_text.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/interface_settings_text.c
)

add_unit_test(test_listen_endpoint
    SOURCES
        platform/test_listen_endpoint.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/listen_endpoint.c
)

add_unit_test(test_bounded_string
    SOURCES
        shared/test_bounded_string.cpp
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_tcp_keepalive
    SOURCES
        platform/test_tcp_keepalive.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/tcp_keepalive.c
)

add_unit_test(test_host_resolver
    SOURCES
        platform/test_host_resolver.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/host_resolver.c
        ${ROOT_SRC_DIR}/platform/linux/shared/log.c
)
target_link_libraries(test_host_resolver PRIVATE Threads::Threads)

add_unit_test(test_quic_datagram_backlog
    SOURCES
        platform/test_quic_datagram_backlog.cpp
        ${ROOT_SRC_DIR}/platform/linux/quic/quic_datagram_backlog.c
)

add_unit_test(test_message_framer
    SOURCES
        platform/test_message_framer.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/message_framer.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_bridge_routes
    SOURCES
        hub/domain/test_bridge_routes.cpp
        ${ROOT_SRC_DIR}/hub/domain/bridge_groups.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_routes.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_targets.c
)

add_unit_test(test_bridge_groups
    SOURCES
        hub/domain/test_bridge_groups.cpp
        ${ROOT_SRC_DIR}/hub/domain/bridge_groups.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_routes.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_targets.c
)

add_unit_test(test_filter_spec
    SOURCES
        platform/test_filter_spec.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/filter_spec.c
)

add_unit_test(test_pin_store
    SOURCES
        platform/test_pin_store.cpp
        ${ROOT_SRC_DIR}/platform/linux/shared/pin_store.c
)

add_unit_test(test_tls_channel
    SOURCES
        platform/test_tls_channel.cpp
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_channel.c
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_client_security.c
        ${ROOT_SRC_DIR}/platform/linux/tls/tls_server_security.c
        ${ROOT_SRC_DIR}/platform/linux/shared/pin_store.c
        ${ROOT_SRC_DIR}/platform/linux/shared/pinned_server_verifier.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_defaults.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_identity.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_identity_ed25519.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_peer_certificate.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_ed25519.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_aead.c
        ${ROOT_SRC_DIR}/platform/linux/shared/tls_aes_armv8.c
        ${ROOT_SRC_DIR}/platform/linux/shared/message_framer.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)
target_link_libraries(test_tls_channel PRIVATE picotls monocypher)

add_unit_test(test_identity_database
    SOURCES
        platform/test_identity_database.cpp
        ${ROOT_SRC_DIR}/platform/linux/sqlite/identity_database.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)
target_link_libraries(test_identity_database PRIVATE sqlite3)

add_unit_test(test_step_ring
    SOURCES
        agent/domain/test_step_ring.cpp
        ${ROOT_SRC_DIR}/agent/domain/step_ring.c
)

add_unit_test(test_canhub_frames
    SOURCES
        platform/test_canhub_frames.cpp
        ${ROOT_SRC_DIR}/platform/linux/libcanhub/canhub_frames.c
        ${ROOT_SRC_DIR}/client/can_id.c
)

add_unit_test(test_isotp_link
    SOURCES
        agent/domain/test_isotp_link.cpp
        ${ROOT_SRC_DIR}/agent/domain/isotp_link.c
)

add_unit_test(test_transaction_runner
    SOURCES
        agent/domain/test_transaction_runner.cpp
        ${ROOT_SRC_DIR}/agent/domain/transaction_runner.c
        ${ROOT_SRC_DIR}/agent/domain/isotp_link.c
        ${ROOT_SRC_DIR}/agent/domain/step_ring.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_agent
    SOURCES
        agent/test_agent.cpp
        ${ROOT_SRC_DIR}/agent/agent.c
        ${ROOT_SRC_DIR}/agent/domain/channel_map.c
        ${ROOT_SRC_DIR}/agent/domain/echo_correlator.c
        ${ROOT_SRC_DIR}/agent/domain/step_ring.c
        ${ROOT_SRC_DIR}/agent/domain/transaction_runner.c
        ${ROOT_SRC_DIR}/agent/domain/isotp_link.c
        ${ROOT_SRC_DIR}/shared/reconnect_backoff.c
        ${ROOT_SRC_DIR}/agent/domain/tx_pacer.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_agent PRIVATE port_mocks)

add_unit_test(test_interface_registry
    SOURCES
        hub/domain/test_interface_registry.cpp
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_client_session
    SOURCES
        hub/domain/test_client_session.cpp
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_admin_views
    SOURCES
        hub/domain/test_admin_views.cpp
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/transaction_table.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_hub_peer
    SOURCES
        hub/domain/test_hub_peer.cpp
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_echo_tokens
    SOURCES
        hub/domain/test_echo_tokens.cpp
        ${ROOT_SRC_DIR}/hub/domain/echo_tokens.c
)

add_unit_test(test_rejected_peers
    SOURCES
        hub/domain/test_rejected_peers.cpp
        ${ROOT_SRC_DIR}/hub/domain/rejected_peers.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_egress_queue
    SOURCES
        hub/domain/test_egress_queue.cpp
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
)

add_unit_test(test_egress_shaper
    SOURCES
        hub/domain/test_egress_shaper.cpp
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
)

add_unit_test(test_frame_routes
    SOURCES
        hub/domain/test_frame_routes.cpp
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_transaction_table
    SOURCES
        hub/domain/test_transaction_table.cpp
        ${ROOT_SRC_DIR}/hub/domain/transaction_table.c
)

add_unit_test(test_broker
    SOURCES
        hub/test_broker.cpp
        ${ROOT_SRC_DIR}/hub/broker.c
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_groups.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_routes.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_targets.c
        ${ROOT_SRC_DIR}/hub/domain/echo_tokens.c
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/protocol/error_message.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/hub/domain/rejected_peers.c
        ${ROOT_SRC_DIR}/hub/domain/transaction_table.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/admin_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/subscribe_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_broker PRIVATE port_mocks)

add_unit_test(test_broker_bridges
    SOURCES
        hub/test_broker_bridges.cpp
        ${ROOT_SRC_DIR}/hub/broker.c
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_groups.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_routes.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_targets.c
        ${ROOT_SRC_DIR}/hub/domain/echo_tokens.c
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/protocol/error_message.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/hub/domain/rejected_peers.c
        ${ROOT_SRC_DIR}/hub/domain/transaction_table.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/admin_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/subscribe_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_broker_bridges PRIVATE port_mocks)

add_unit_test(test_broker_transactions
    SOURCES
        hub/test_broker_transactions.cpp
        ${ROOT_SRC_DIR}/hub/broker.c
        ${ROOT_SRC_DIR}/hub/domain/admin_views.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_groups.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_routes.c
        ${ROOT_SRC_DIR}/hub/domain/bridge_targets.c
        ${ROOT_SRC_DIR}/hub/domain/echo_tokens.c
        ${ROOT_SRC_DIR}/hub/domain/egress_queue.c
        ${ROOT_SRC_DIR}/hub/domain/frame_routes.c
        ${ROOT_SRC_DIR}/protocol/error_message.c
        ${ROOT_SRC_DIR}/hub/domain/hub_peer.c
        ${ROOT_SRC_DIR}/hub/domain/interface_registry.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/hub/domain/client_session.c
        ${ROOT_SRC_DIR}/hub/domain/peer_directory.c
        ${ROOT_SRC_DIR}/hub/domain/rejected_peers.c
        ${ROOT_SRC_DIR}/hub/domain/transaction_table.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/admin_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/subscribe_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_broker_transactions PRIVATE port_mocks)

add_unit_test(test_frame_spec
    SOURCES
        client/test_frame_spec.cpp
        ${ROOT_SRC_DIR}/client/frame_spec.c
)

add_unit_test(test_checksum
    SOURCES
        client/txn_script/test_checksum.cpp
        ${ROOT_SRC_DIR}/client/txn_script/checksum.c
)

add_unit_test(test_script_parser
    SOURCES
        client/txn_script/test_script_parser.cpp
        ${ROOT_SRC_DIR}/client/txn_script/script.c
        ${ROOT_SRC_DIR}/client/txn_script/script_error.c
        ${ROOT_SRC_DIR}/client/txn_script/script_lexer.c
        ${ROOT_SRC_DIR}/client/txn_script/script_parser.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_script_program
    SOURCES
        client/txn_script/test_script_program.cpp
        ${ROOT_SRC_DIR}/client/can_id.c
        ${ROOT_SRC_DIR}/client/transaction_builder.c
        ${ROOT_SRC_DIR}/client/txn_script/checksum.c
        ${ROOT_SRC_DIR}/client/txn_script/script.c
        ${ROOT_SRC_DIR}/client/txn_script/script_compiler.c
        ${ROOT_SRC_DIR}/client/txn_script/script_error.c
        ${ROOT_SRC_DIR}/client/txn_script/script_generator.c
        ${ROOT_SRC_DIR}/client/txn_script/script_lexer.c
        ${ROOT_SRC_DIR}/client/txn_script/script_parser.c
        ${ROOT_SRC_DIR}/client/txn_script/script_resolver.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_transaction_builder
    SOURCES
        client/test_transaction_builder.cpp
        ${ROOT_SRC_DIR}/client/can_id.c
        ${ROOT_SRC_DIR}/client/transaction_builder.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_transaction_sender
    SOURCES
        client/test_transaction_sender.cpp
        ${ROOT_SRC_DIR}/client/transaction_sender.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
)

add_unit_test(test_client
    SOURCES
        client/test_client.cpp
        ${ROOT_SRC_DIR}/client/client.c
        ${ROOT_SRC_DIR}/client/transaction_sender.c
        ${ROOT_SRC_DIR}/protocol/txn_message.c
        ${ROOT_SRC_DIR}/protocol/txn_steps_message.c
        ${ROOT_SRC_DIR}/protocol/error_message.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/interface_status_message.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/subscribe_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/egress_shaper.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_client PRIVATE port_mocks)

add_unit_test(test_mirror_app
    SOURCES
        mirror/test_mirror_app.cpp
        ${ROOT_SRC_DIR}/mirror/mirror_app.c
        ${ROOT_SRC_DIR}/shared/reconnect_backoff.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_mirror_app PRIVATE port_mocks)

add_unit_test(test_interface_name
    SOURCES
        protocol/test_interface_name.cpp
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/register_message.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)

add_unit_test(test_ascii_framer
    SOURCES
        socketcand/test_ascii_framer.cpp
        ${ROOT_SRC_DIR}/socketcand/domain/ascii_framer.c
)

add_unit_test(test_socketcand_codec
    SOURCES
        socketcand/test_socketcand_codec.cpp
        ${ROOT_SRC_DIR}/socketcand/domain/socketcand_codec.c
)

add_unit_test(test_interface_catalogue
    SOURCES
        socketcand/test_interface_catalogue.cpp
        ${ROOT_SRC_DIR}/socketcand/domain/interface_catalogue.c
)

add_unit_test(test_beacon_xml
    SOURCES
        socketcand/test_beacon_xml.cpp
        ${ROOT_SRC_DIR}/socketcand/domain/beacon_xml.c
        ${ROOT_SRC_DIR}/socketcand/domain/interface_catalogue.c
)

add_unit_test(test_socketcand_bridge
    SOURCES
        socketcand/test_socketcand_bridge.cpp
        ${ROOT_SRC_DIR}/socketcand/socketcand_bridge.c
        ${ROOT_SRC_DIR}/socketcand/domain/ascii_framer.c
        ${ROOT_SRC_DIR}/socketcand/domain/socketcand_codec.c
        ${ROOT_SRC_DIR}/socketcand/domain/interface_catalogue.c
        ${ROOT_SRC_DIR}/socketcand/domain/connection_table.c
        ${ROOT_SRC_DIR}/socketcand/domain/beacon_xml.c
        ${ROOT_SRC_DIR}/protocol/frame_message.c
        ${ROOT_SRC_DIR}/protocol/ifconfig_message.c
        ${ROOT_SRC_DIR}/protocol/hello_message.c
        ${ROOT_SRC_DIR}/protocol/interface_name.c
        ${ROOT_SRC_DIR}/protocol/list_message.c
        ${ROOT_SRC_DIR}/protocol/open_message.c
        ${ROOT_SRC_DIR}/protocol/message_header.c
        ${ROOT_SRC_DIR}/protocol/wire.c
        ${ROOT_SRC_DIR}/shared/bounded_string.c
)
target_link_libraries(test_socketcand_bridge PRIVATE port_mocks)
