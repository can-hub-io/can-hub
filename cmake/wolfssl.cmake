# Builds a pinned static wolfSSL at configure time, mirroring openssl.cmake.
#
# Spike for roadmap §4. Measured motive: OpenSSL contributes ~90% of a release
# binary (4.62 MB of the 5.11 MB of symbols in can-hub-agent), and ngtcp2 ships
# a supported crypto-wolfssl backend.
#
# It has to be installed rather than added as a subproject: ngtcp2 locates it
# with find_package/find_library, which needs real files on disk when ngtcp2 is
# configured. Unlike OpenSSL it needs no autotools — wolfSSL has a first-class
# CMake build, so this drives cmake instead of Configure/make.
#
# WOLFSSL_OPENSSLEXTRA is the OpenSSL compatibility layer the TLS code is
# written against. Its headers install under <prefix>/include/wolfssl/openssl,
# so that directory goes on the include path and `#include <openssl/ssl.h>`
# resolves unchanged.

set(CAN_HUB_WOLFSSL_VERSION 5.8.2-stable)
set(CAN_HUB_WOLFSSL_SHA256 3ef126e3466e2f8f6ebb62b916a7f8fb26c6709dbdf2b63a167759f2fdb53068)
set(CAN_HUB_WOLFSSL_URL "https://github.com/wolfSSL/wolfssl/archive/refs/tags/v${CAN_HUB_WOLFSSL_VERSION}.tar.gz")
set(CAN_HUB_WOLFSSL_PREFIX "${CMAKE_BINARY_DIR}/wolfssl")

if(NOT EXISTS "${CAN_HUB_WOLFSSL_PREFIX}/lib/libwolfssl.a")
    set(_wolfssl_tarball "${CMAKE_BINARY_DIR}/wolfssl-${CAN_HUB_WOLFSSL_VERSION}.tar.gz")
    set(_wolfssl_source "${CMAKE_BINARY_DIR}/wolfssl-${CAN_HUB_WOLFSSL_VERSION}")
    set(_wolfssl_build "${CMAKE_BINARY_DIR}/wolfssl-build")

    message(STATUS "Building wolfSSL ${CAN_HUB_WOLFSSL_VERSION}, one-off per build tree")
    file(DOWNLOAD "${CAN_HUB_WOLFSSL_URL}" "${_wolfssl_tarball}" EXPECTED_HASH SHA256=${CAN_HUB_WOLFSSL_SHA256})
    file(ARCHIVE_EXTRACT INPUT "${_wolfssl_tarball}" DESTINATION "${CMAKE_BINARY_DIR}")

    # quic: required by the ngtcp2 backend. ed25519/keygen/certgen: the
    # self-signed identities. alpn: the canhub/0 protocol selector.
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${_wolfssl_source}" -B "${_wolfssl_build}"
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${CAN_HUB_WOLFSSL_PREFIX}
                -DCMAKE_INSTALL_LIBDIR=lib
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                # wolfSSL 5.8.2 hardcodes -Werror and its own src/tls.c trips
                # -Wstringop-overflow on gcc 13. Upstream's warnings are not
                # ours to gate on.
                # HAVE_AES_ECB/AES_DIRECT: header protection in the ngtcp2
                # backend; they are configure.ac defines, not cmake options.
                "-DCMAKE_C_FLAGS=-Wno-error -DHAVE_AES_ECB -DWOLFSSL_AES_DIRECT"
                -DBUILD_SHARED_LIBS=OFF
                -DWOLFSSL_QUIC=yes
                -DWOLFSSL_TLS13=yes
                -DWOLFSSL_OPENSSLEXTRA=yes
                # OPENSSL_ALL gates SSL_CTX_set_cert_verify_callback, which is
                # what replaces chain verification with accept-any + TOFU.
                -DWOLFSSL_OPENSSLALL=yes
                -DWOLFSSL_SESSION_TICKET=yes
                -DWOLFSSL_ED25519=yes
                -DWOLFSSL_KEYGEN=yes
                -DWOLFSSL_CERTGEN=yes
                -DWOLFSSL_CERTREQ=yes
                -DWOLFSSL_ALPN=yes
                -DWOLFSSL_EXAMPLES=no
                -DWOLFSSL_CRYPT_TESTS=no
        RESULT_VARIABLE _wolfssl_configure_result
        OUTPUT_QUIET
    )
    if(NOT _wolfssl_configure_result EQUAL 0)
        message(FATAL_ERROR "wolfSSL configure failed")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${_wolfssl_build}" --parallel
        RESULT_VARIABLE _wolfssl_build_result
        OUTPUT_QUIET
    )
    if(NOT _wolfssl_build_result EQUAL 0)
        message(FATAL_ERROR "wolfSSL build failed")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --install "${_wolfssl_build}"
        RESULT_VARIABLE _wolfssl_install_result
        OUTPUT_QUIET
    )
    if(NOT _wolfssl_install_result EQUAL 0)
        message(FATAL_ERROR "wolfSSL install failed")
    endif()

    file(REMOVE_RECURSE "${_wolfssl_source}" "${_wolfssl_build}")
    file(REMOVE "${_wolfssl_tarball}")
endif()

# What ngtcp2's Findwolfssl.cmake looks for.
set(WOLFSSL_INCLUDE_DIR "${CAN_HUB_WOLFSSL_PREFIX}/include" CACHE PATH "" FORCE)
set(WOLFSSL_LIBRARY "${CAN_HUB_WOLFSSL_PREFIX}/lib/libwolfssl.a" CACHE FILEPATH "" FORCE)

add_library(wolfssl STATIC IMPORTED GLOBAL)
set_target_properties(wolfssl PROPERTIES
    IMPORTED_LOCATION "${WOLFSSL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WOLFSSL_INCLUDE_DIR};${WOLFSSL_INCLUDE_DIR}/wolfssl"
    # The compat layer only declares the OpenSSL types once the build's feature
    # macros are in scope, and those live in options.h. Force-including it keeps
    # every `#include <openssl/...>` in the tree working untouched.
    INTERFACE_COMPILE_OPTIONS "-include;wolfssl/options.h"
)
