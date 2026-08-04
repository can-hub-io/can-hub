# Builds a pinned static AWS-LC at configure time, mirroring openssl.cmake.
#
# Licence is the reason this exists: AWS-LC is Apache-2.0 OR ISC, so it works in
# both arms of a dual-licensed project. wolfSSL is smaller still but GPLv3,
# which the commercial arm cannot carry.
#
# ngtcp2 reuses its BoringSSL backend for AWS-LC (it probes OPENSSL_IS_AWSLC),
# and locates it through BORINGSSL_INCLUDE_DIR / BORINGSSL_LIBRARIES rather
# than a find module, so those are set below.

set(CAN_HUB_AWSLC_VERSION 5.4.0)
set(CAN_HUB_AWSLC_SHA256 ef310fdb20a4172a357ab60a1adb217b21aceb34f02e29758edec5a02b1bcc0f)
set(CAN_HUB_AWSLC_URL "https://github.com/aws/aws-lc/archive/refs/tags/v${CAN_HUB_AWSLC_VERSION}.tar.gz")
set(CAN_HUB_AWSLC_PREFIX "${CMAKE_BINARY_DIR}/awslc")

if(NOT EXISTS "${CAN_HUB_AWSLC_PREFIX}/lib/libssl.a")
    set(_awslc_tarball "${CMAKE_BINARY_DIR}/aws-lc-${CAN_HUB_AWSLC_VERSION}.tar.gz")
    set(_awslc_source "${CMAKE_BINARY_DIR}/aws-lc-${CAN_HUB_AWSLC_VERSION}")
    set(_awslc_build "${CMAKE_BINARY_DIR}/awslc-build")

    message(STATUS "Building AWS-LC ${CAN_HUB_AWSLC_VERSION}, one-off per build tree")
    file(DOWNLOAD "${CAN_HUB_AWSLC_URL}" "${_awslc_tarball}" EXPECTED_HASH SHA256=${CAN_HUB_AWSLC_SHA256})
    file(ARCHIVE_EXTRACT INPUT "${_awslc_tarball}" DESTINATION "${CMAKE_BINARY_DIR}")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${_awslc_source}" -B "${_awslc_build}"
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${CAN_HUB_AWSLC_PREFIX}
                -DCMAKE_INSTALL_LIBDIR=lib
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_TESTING=OFF
                -DBUILD_TOOL=OFF
                -DDISABLE_GO=ON
                -DDISABLE_PERL=ON
        RESULT_VARIABLE _awslc_configure_result
        OUTPUT_QUIET
    )
    if(NOT _awslc_configure_result EQUAL 0)
        message(FATAL_ERROR "AWS-LC configure failed")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${_awslc_build}" --parallel
        RESULT_VARIABLE _awslc_build_result
        OUTPUT_QUIET
    )
    if(NOT _awslc_build_result EQUAL 0)
        message(FATAL_ERROR "AWS-LC build failed")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --install "${_awslc_build}"
        RESULT_VARIABLE _awslc_install_result
        OUTPUT_QUIET
    )
    if(NOT _awslc_install_result EQUAL 0)
        message(FATAL_ERROR "AWS-LC install failed")
    endif()

    file(REMOVE_RECURSE "${_awslc_source}" "${_awslc_build}")
    file(REMOVE "${_awslc_tarball}")
endif()

set(BORINGSSL_INCLUDE_DIR "${CAN_HUB_AWSLC_PREFIX}/include" CACHE PATH "" FORCE)
set(BORINGSSL_LIBRARIES
    "${CAN_HUB_AWSLC_PREFIX}/lib/libssl.a;${CAN_HUB_AWSLC_PREFIX}/lib/libcrypto.a"
    CACHE STRING "" FORCE
)

add_library(awslc INTERFACE)
target_include_directories(awslc INTERFACE "${BORINGSSL_INCLUDE_DIR}")
target_link_libraries(awslc INTERFACE
    "${CAN_HUB_AWSLC_PREFIX}/lib/libssl.a"
    "${CAN_HUB_AWSLC_PREFIX}/lib/libcrypto.a"
)
