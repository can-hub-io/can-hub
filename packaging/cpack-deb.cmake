# Per-binary Debian packages. Included from CMakeLists.txt only when
# CAN_HUB_PACKAGING=ON (set by `make deb`). Each component is its own .deb:
#   can-hub         hub daemon + admin CLI + systemd service + config + system user
#   can-hub-agent   SocketCAN exporter + systemd service (CAP_NET_RAW) + config
#   can-hub-client  reference consumer (list/dump/send/attach) + socketcand bridge service + config
# The hub and agent share the system user can-hub but keep separate state dirs
# (/var/lib/can-hub and /var/lib/can-hub-agent), so either can be purged alone.

# Packaged directories default to 0755 (the build umask is often 0002, which
# would otherwise ship group-writable 0775 dirs that lintian rejects).
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

install(TARGETS can-hub RUNTIME DESTINATION bin COMPONENT hub)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/can-hub.service
    DESTINATION /lib/systemd/system
    COMPONENT hub
)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/hub.conf
    DESTINATION /etc/can-hub
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

install(TARGETS can-hub-cli RUNTIME DESTINATION bin COMPONENT hub)

set(_completions ${CMAKE_CURRENT_SOURCE_DIR}/packaging/completions)
install(
    FILES ${_completions}/bash/can-hub ${_completions}/bash/can-hub-cli
    DESTINATION share/bash-completion/completions
    COMPONENT hub
)
install(
    FILES ${_completions}/zsh/_can-hub ${_completions}/zsh/_can-hub-cli
    DESTINATION share/zsh/vendor-completions
    COMPONENT hub
)
install(
    FILES ${_completions}/bash/can-hub-agent
    DESTINATION share/bash-completion/completions
    COMPONENT agent
)
install(
    FILES ${_completions}/zsh/_can-hub-agent
    DESTINATION share/zsh/vendor-completions
    COMPONENT agent
)

install(TARGETS can-hub-client RUNTIME DESTINATION bin COMPONENT client)
install(
    FILES ${_completions}/bash/can-hub-client
    DESTINATION share/bash-completion/completions
    COMPONENT client
)
install(
    FILES ${_completions}/zsh/_can-hub-client
    DESTINATION share/zsh/vendor-completions
    COMPONENT client
)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/can-hub-socketcand.service
    DESTINATION /lib/systemd/system
    COMPONENT client
)
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/socketcand.conf
    DESTINATION /etc/can-hub
    COMPONENT client
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL hub agent client)

set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Javier Moragon <jamofer@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Javier Moragon <jamofer@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/can-hub-io/can-hub")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# All components share one source package (can-hub), so a single source-named
# changelog is valid for every binary package.
set(CPACK_DEBIAN_PACKAGE_SOURCE "can-hub")
# Strip binaries and force standard 0644/0755 permissions on the control files
# (the maintainer scripts are group-writable in the source tree).
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

# Debian docs required by lintian: a native-format changelog (gzipped, no
# timestamp) and a DEP-5 copyright, installed into every binary's doc dir.
set(_can_hub_doc "${CMAKE_BINARY_DIR}/debian-doc")
file(MAKE_DIRECTORY "${_can_hub_doc}")
file(WRITE "${_can_hub_doc}/changelog"
"can-hub (${PROJECT_VERSION}) unstable; urgency=medium\n\n  * Release ${PROJECT_VERSION}. See https://github.com/can-hub-io/can-hub/releases.\n\n -- Javier Moragon <jamofer@gmail.com>  Thu, 01 Jan 1970 00:00:00 +0000\n")
execute_process(COMMAND gzip -9 -n -f "${_can_hub_doc}/changelog")
file(WRITE "${_can_hub_doc}/copyright"
"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: can-hub\nSource: https://github.com/can-hub-io/can-hub\n\nFiles: *\nCopyright: 2026 Javier Moragon\nLicense: AGPL-3.0-only\n On Debian systems, the full text of the GNU Affero General Public License\n version 3 can be found in /usr/share/common-licenses/AGPL-3.\n")

foreach(_pair "hub:can-hub" "agent:can-hub-agent" "client:can-hub-client")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _component)
    list(GET _pair 1 _pkg)
    install(FILES "${_can_hub_doc}/changelog.gz" "${_can_hub_doc}/copyright"
            DESTINATION "share/doc/${_pkg}" COMPONENT ${_component})
    # Intentional, documented lintian exceptions: the maintainer scripts manage
    # systemd units only when systemd is the running init (no deb-systemd-helper
    # by design), and the tools are self-documenting via --help/--version. The
    # static debs additionally ship musl-static, zero-dependency binaries.
    set(_overrides "${_pkg}: maintainer-script-calls-systemctl\n${_pkg}: no-manual-page\n")
    if(CAN_HUB_DEB_STATIC)
        string(APPEND _overrides "${_pkg}: statically-linked-binary\n")
    endif()
    file(WRITE "${_can_hub_doc}/${_pkg}.overrides" "${_overrides}")
    install(FILES "${_can_hub_doc}/${_pkg}.overrides"
            DESTINATION "share/lintian/overrides" RENAME "${_pkg}" COMPONENT ${_component})
endforeach()
# Static debs (CAN_HUB_DEB_STATIC, built in docker/static.Dockerfile) carry
# musl binaries with zero runtime deps, so shlibdeps is off and there are no
# Depends; the target architecture is fixed explicitly (it is a cross build).
# The native build keeps shlibdeps, which pins libc6 to the build host.
if(CAN_HUB_DEB_STATIC)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CAN_HUB_DEB_ARCH}")
else()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_DEBIAN_HUB_PACKAGE_NAME "can-hub")
set(CPACK_COMPONENT_HUB_DESCRIPTION "CAN-over-network hub daemon and admin CLI
The can-hub daemon routes SocketCAN frames between NAT'd agents and clients
over QUIC, TLS or TCP, with mTLS identities and per-client ACLs. Ships a
systemd service and the can-hub-cli administration tool.")
set(CPACK_DEBIAN_HUB_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/hub/postrm")

set(CPACK_DEBIAN_AGENT_PACKAGE_NAME "can-hub-agent")
set(CPACK_COMPONENT_AGENT_DESCRIPTION "SocketCAN exporter agent for can-hub
Exports local SocketCAN interfaces to a can-hub over QUIC, TLS or TCP,
surviving NAT, with a systemd service and a stable ED25519 identity.")
set(CPACK_DEBIAN_AGENT_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/agent/postrm")

set(CPACK_DEBIAN_CLIENT_PACKAGE_NAME "can-hub-client")
set(CPACK_COMPONENT_CLIENT_DESCRIPTION "Reference can-hub client and socketcand bridge
Lists, dumps, sends and attaches remote can-hub interfaces as local vcan, and
bridges them to socketcand-speaking tools. Includes a systemd bridge service.")
set(CPACK_DEBIAN_CLIENT_PACKAGE_RECOMMENDS "can-hub")
set(CPACK_DEBIAN_CLIENT_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/client/conffiles;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/client/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/client/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/client/postrm")

include(CPack)
