# Builds a pinned static OpenSSL at configure time. Distros still ship 3.0/3.2
# and the ngtcp2 crypto-ossl backend needs the QUIC TLS API that landed in 3.5
# (SSL_set_quic_tls_cbs). Built once per build tree, cached afterwards.

set(CAN_HUB_OPENSSL_VERSION 3.5.7)
set(CAN_HUB_OPENSSL_SHA256 a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8)
set(CAN_HUB_OPENSSL_URL "https://github.com/openssl/openssl/releases/download/openssl-${CAN_HUB_OPENSSL_VERSION}/openssl-${CAN_HUB_OPENSSL_VERSION}.tar.gz")
set(CAN_HUB_OPENSSL_PREFIX "${CMAKE_BINARY_DIR}/openssl")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(_openssl_target linux-x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(_openssl_target linux-aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    set(_openssl_target linux-armv4)
else()
    message(FATAL_ERROR "no OpenSSL Configure target mapped for ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(NOT EXISTS "${CAN_HUB_OPENSSL_PREFIX}/lib/libssl.a")
    set(_openssl_tarball "${CMAKE_BINARY_DIR}/openssl-${CAN_HUB_OPENSSL_VERSION}.tar.gz")
    set(_openssl_source "${CMAKE_BINARY_DIR}/openssl-${CAN_HUB_OPENSSL_VERSION}")

    message(STATUS "Building OpenSSL ${CAN_HUB_OPENSSL_VERSION} (${_openssl_target}), one-off per build tree")
    file(DOWNLOAD "${CAN_HUB_OPENSSL_URL}" "${_openssl_tarball}" EXPECTED_HASH SHA256=${CAN_HUB_OPENSSL_SHA256})
    file(ARCHIVE_EXTRACT INPUT "${_openssl_tarball}" DESTINATION "${CMAKE_BINARY_DIR}")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER}
                ./Configure ${_openssl_target}
                --prefix=${CAN_HUB_OPENSSL_PREFIX} --libdir=lib
                no-shared no-apps no-docs no-tests -fPIC
        WORKING_DIRECTORY "${_openssl_source}"
        RESULT_VARIABLE _openssl_configure_result
        OUTPUT_QUIET
    )
    if(NOT _openssl_configure_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL Configure failed")
    endif()

    include(ProcessorCount)
    ProcessorCount(_openssl_jobs)
    if(_openssl_jobs EQUAL 0)
        set(_openssl_jobs 2)
    endif()
    execute_process(
        COMMAND make -j${_openssl_jobs}
        WORKING_DIRECTORY "${_openssl_source}"
        RESULT_VARIABLE _openssl_build_result
        OUTPUT_QUIET
    )
    if(NOT _openssl_build_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL build failed")
    endif()
    execute_process(
        COMMAND make install_sw
        WORKING_DIRECTORY "${_openssl_source}"
        RESULT_VARIABLE _openssl_install_result
        OUTPUT_QUIET
    )
    if(NOT _openssl_install_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL install failed")
    endif()

    file(REMOVE_RECURSE "${_openssl_source}")
    file(REMOVE "${_openssl_tarball}")
endif()

set(OPENSSL_ROOT_DIR "${CAN_HUB_OPENSSL_PREFIX}")
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
