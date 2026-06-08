# Per-binary Debian packages. Included from CMakeLists.txt only when
# CAN_HUB_PACKAGING=ON (set by `make deb`). Each component is its own .deb:
#   can-hub         hub daemon + systemd service + system user
#   can-hub-agent   SocketCAN exporter + systemd service (CAP_NET_RAW) + config
#   can-hub-cli     admin CLI (local unix socket)
#   can-hub-client  reference consumer (list/dump/send, attach)
# The hub and agent share the system user can-hub but keep separate state dirs
# (/var/lib/can-hub and /var/lib/can-hub-agent), so either can be purged alone.

install(TARGETS can-hub RUNTIME DESTINATION bin COMPONENT hub)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/can-hub.service
    DESTINATION /lib/systemd/system
    COMPONENT hub
)

install(TARGETS can-hub-agent RUNTIME DESTINATION bin COMPONENT agent)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/can-hub-agent.service
    DESTINATION /lib/systemd/system
    COMPONENT agent
)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/agent.conf
    DESTINATION /etc/can-hub
    COMPONENT agent
)

install(TARGETS can-hub-cli RUNTIME DESTINATION bin COMPONENT cli)
install(TARGETS can-hub-client RUNTIME DESTINATION bin COMPONENT client)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL hub agent cli client)

set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Javier Moragon <jamofer@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Javier Moragon <jamofer@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/jamofer/can-hub")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_DEBIAN_HUB_PACKAGE_NAME "can-hub")
set(CPACK_COMPONENT_HUB_DESCRIPTION "CAN-over-network hub daemon (QUIC/TLS/TCP) with systemd service.")
set(CPACK_DEBIAN_HUB_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/postrm")

set(CPACK_DEBIAN_AGENT_PACKAGE_NAME "can-hub-agent")
set(CPACK_COMPONENT_AGENT_DESCRIPTION "can-hub SocketCAN exporter agent with systemd service.")
set(CPACK_DEBIAN_AGENT_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/postrm")

set(CPACK_DEBIAN_CLI_PACKAGE_NAME "can-hub-cli")
set(CPACK_COMPONENT_CLI_DESCRIPTION "Admin CLI for the can-hub hub over its local unix socket.")
set(CPACK_DEBIAN_CLI_PACKAGE_RECOMMENDS "can-hub")

set(CPACK_DEBIAN_CLIENT_PACKAGE_NAME "can-hub-client")
set(CPACK_COMPONENT_CLIENT_DESCRIPTION "Reference can-hub consumer: list, dump, send and attach interfaces.")

include(CPack)
