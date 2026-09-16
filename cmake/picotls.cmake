# Builds a pinned picotls at configure time, mirroring openssl.cmake.
#
# picotls is the single TLS stack: MIT, small, and the only ngtcp2 crypto
# backend whose handshake engine we can keep while replacing the crypto
# underneath (see MIGRATION_PICOTLS.md).
#
# ngtcp2 locates it through PICOTLS_INCLUDE_DIR / PICOTLS_LIBRARIES and runs a
# check_symbol_exists against them at configure time, so the archives have to
# exist by then; FetchContent alone would not be enough. Submodules (cifra,
# micro-ecc) are required for the minicrypto binding.

set(CAN_HUB_PICOTLS_COMMIT f07f1c8c68b237f1468bc1f1fe1b68aba3ff23b4)
if(NOT DEFINED CAN_HUB_ROOT_DIR)
    set(CAN_HUB_ROOT_DIR "${CMAKE_SOURCE_DIR}")
endif()
set(CAN_HUB_PICOTLS_PREFIX "${CAN_HUB_ROOT_DIR}/build/picotls-src")
set(CAN_HUB_PICOTLS_BUILD "${CMAKE_BINARY_DIR}/picotls-build")

# picotls guards its POSIX includes with #ifndef _WINDOWS but its CMake build
# never defines it — upstream builds for Windows through a Visual Studio project
# instead. Define it here so the mingw cross build compiles the Windows path,
# where minicrypto's RNG already uses BCrypt.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # picotls's wincompat.h includes <Winsock2.h>, which only resolves on a
    # case-insensitive filesystem: upstream builds Windows with MSVC, where the
    # casing never mattered. mingw on Linux needs the name as written.
    # Upstream's wincompat.h is written for MSVC: it includes <Winsock2.h>,
    # which only resolves on a case-insensitive filesystem, and defines a
    # struct timezone that mingw already provides. mingw supplies everything
    # picotls needs from it, so this replaces it rather than patching it.
    set(_picotls_wincompat "${CMAKE_BINARY_DIR}/picotls-wincompat")
    file(WRITE "${_picotls_wincompat}/wincompat.h"
"#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <malloc.h>
#include <sys/time.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
")
    set(_picotls_platform_flags "-D_WINDOWS -I${_picotls_wincompat}")
else()
    set(_picotls_platform_flags "")
endif()

if(NOT DEFINED CAN_HUB_BUILD_PARALLELISM)
    include(ProcessorCount)
    ProcessorCount(CAN_HUB_BUILD_PARALLELISM)
    if(CAN_HUB_BUILD_PARALLELISM EQUAL 0)
        set(CAN_HUB_BUILD_PARALLELISM 1)
    endif()
endif()

# Both AES engines are selected from what the compiler says it targets, never
# from CMAKE_SYSTEM_PROCESSOR. A native build takes that variable from uname,
# and uname lies in the containers the release wheels are built in: the armv7l
# manylinux image runs on an arm64 host. Trusting it there turns the ARMv8
# engine on for a 32-bit toolchain and passes -march=armv8-a+crypto to a
# compiler that rejects it. __aarch64__ and __x86_64__ come from the compiler
# driving the build, so they are right under emulation, in a container, and
# through a cross toolchain file alike.
include(CheckCSourceCompiles)
check_c_source_compiles("
#if !defined(__aarch64__)
#error not aarch64
#endif
int main(void) { return 0; }
" CAN_HUB_TARGET_IS_AARCH64)
check_c_source_compiles("
#if !defined(__x86_64__)
#error not x86-64
#endif
int main(void) { return 0; }
" CAN_HUB_TARGET_IS_X86_64)

# fusion is picotls's AES-NI engine. It is the only fast AES available once
# OpenSSL is gone, and QUIC needs one whatever suite is negotiated: RFC 9001
# fixes AES-128-GCM for Initial packets, which unauthenticated peers can make
# the hub process. x86-64 only, and gated again at runtime by
# ptls_fusion_is_supported_by_cpu.
# The ARMv8 crypto extensions are the ARM counterpart of fusion: AESE, AESMC and
# PMULL are instructions, so the engine is a binding rather than an
# implementation. They are optional in ARMv8-A — a Raspberry Pi 4 has neither, a
# Pi 5 and every server part have both — so the build only compiles the engine
# in and a runtime HWCAP check decides whether it is used.
#
# ARMv8-A defines them in AArch32 as well, and ACLE spells the intrinsics the
# same in both states, so one source covers both. That matters for 32-bit
# userland on 64-bit silicon — Raspberry Pi OS armhf on a Pi 3, 4 or 5 — where
# OpenSSL found the instructions and we otherwise would not, which measured at
# 9.9x at CAN frame size (spike/aead-bench/RESULTS.md).
if(CAN_HUB_TARGET_IS_AARCH64)
    set(CAN_HUB_TLS_ARMV8_CRYPTO ON)
    set(CAN_HUB_ARM_CRYPTO_FLAGS -march=armv8-a+crypto)
else()
    # A 32-bit ARM toolchain may be configured for a baseline that cannot even
    # assemble these, so ask it rather than deciding from the triple.
    set(CMAKE_REQUIRED_FLAGS "-march=armv8-a -mfpu=crypto-neon-fp-armv8")
    check_c_source_compiles("
#include <arm_neon.h>
int main(void) {
    uint8x16_t s = vdupq_n_u8(0);
    poly64x2_t p = vreinterpretq_p64_u8(s);
    s = vaesmcq_u8(vaeseq_u8(s, s));
    (void)vmull_high_p64(p, p);
    return vgetq_lane_u8(s, 0);
}
" CAN_HUB_TARGET_HAS_AARCH32_CRYPTO)
    unset(CMAKE_REQUIRED_FLAGS)

    if(CAN_HUB_TARGET_HAS_AARCH32_CRYPTO)
        set(CAN_HUB_TLS_ARMV8_CRYPTO ON)
        set(CAN_HUB_ARM_CRYPTO_FLAGS -march=armv8-a -mfpu=crypto-neon-fp-armv8)
    else()
        set(CAN_HUB_TLS_ARMV8_CRYPTO OFF)
    endif()
endif()

if(CAN_HUB_TARGET_IS_X86_64)
    set(CAN_HUB_TLS_FUSION ON)
    set(_picotls_fusion_option "-DWITH_FUSION=ON")
    set(_picotls_fusion_target picotls-fusion)
else()
    set(CAN_HUB_TLS_FUSION OFF)
    set(_picotls_fusion_option "-DWITH_FUSION=OFF")
    set(_picotls_fusion_target "")
endif()

if(NOT EXISTS "${CAN_HUB_PICOTLS_BUILD}/libpicotls-core.a")
    message(STATUS "Building picotls ${CAN_HUB_PICOTLS_COMMIT}, one-off per build tree")

    if(NOT EXISTS "${CAN_HUB_PICOTLS_PREFIX}/.git")
        execute_process(
            COMMAND git clone --recurse-submodules https://github.com/h2o/picotls.git "${CAN_HUB_PICOTLS_PREFIX}"
            RESULT_VARIABLE _picotls_clone
        )
        if(NOT _picotls_clone EQUAL 0)
            message(FATAL_ERROR "could not clone picotls")
        endif()
    endif()

    # One execute_process per command: several COMMANDs in one call are a
    # pipeline, so they run concurrently and fight over .git/index.lock, and
    # RESULT_VARIABLE would only carry the last child's status.
    execute_process(
        COMMAND git -C "${CAN_HUB_PICOTLS_PREFIX}" checkout --quiet ${CAN_HUB_PICOTLS_COMMIT}
        RESULT_VARIABLE _picotls_checkout
    )
    if(NOT _picotls_checkout EQUAL 0)
        message(FATAL_ERROR "could not check out picotls ${CAN_HUB_PICOTLS_COMMIT}")
    endif()

    execute_process(
        COMMAND git -C "${CAN_HUB_PICOTLS_PREFIX}" submodule update --init --recursive
        RESULT_VARIABLE _picotls_submodules
    )
    if(NOT _picotls_submodules EQUAL 0)
        message(FATAL_ERROR "could not update the picotls submodules")
    endif()

    # The nested build has to be told it is cross-compiling, or it probes the
    # host and picks up host headers and host feature detection.
    set(_picotls_cross "")
    if(CMAKE_CROSSCOMPILING)
        list(APPEND _picotls_cross
            "-DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
            "-DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
        )
        if(CMAKE_TOOLCHAIN_FILE)
            get_filename_component(_picotls_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
            list(APPEND _picotls_cross "-DCMAKE_TOOLCHAIN_FILE=${_picotls_toolchain}")
        endif()
    endif()

    # cifra takes an MSVC-only branch under _WINDOWS: it calls _BitScanReverse,
    # which clang rejects on the uint32_t argument, and which computes leading
    # zeroes in a function named count_trailing_zeroes. Narrow it to MSVC so
    # mingw uses the GCC builtin. A no-op anywhere _WINDOWS is not defined.
    set(_cifra_bitops "${CAN_HUB_PICOTLS_PREFIX}/deps/cifra/src/bitops.h")
    file(READ "${_cifra_bitops}" _cifra_text)
    string(FIND "${_cifra_text}" "count_trailing_zeroes" _cifra_found)
    if(_cifra_found EQUAL -1)
        message(FATAL_ERROR "cifra changed shape: count_trailing_zeroes is gone, re-derive the mingw fix")
    endif()
    string(REPLACE "#ifdef _WINDOWS\n  uint32_t r = 0;\n  _BitScanReverse"
                   "#if defined(_WINDOWS) && defined(_MSC_VER)\n  uint32_t r = 0;\n  _BitScanReverse"
                   _cifra_text "${_cifra_text}")
    file(WRITE "${_cifra_bitops}" "${_cifra_text}")

    # fusion's CPU probe has the same MSVC-only shape: under _WINDOWS it calls
    # __cpuid(uint32_t[4], int), which is MSVC's, while clang only has the one
    # from <cpuid.h> with a different signature. The #else branch is GCC inline
    # asm that mingw compiles as it stands — and reads the PCLMUL bit at 1
    # rather than the Windows branch's 5, which is VMX. Narrow the guard to
    # MSVC. A no-op anywhere _WINDOWS is not defined.
    set(_fusion_source "${CAN_HUB_PICOTLS_PREFIX}/lib/fusion.c")
    file(READ "${_fusion_source}" _fusion_text)
    string(FIND "${_fusion_text}" "__cpuid(cpu_info, 0)" _fusion_found)
    if(_fusion_found EQUAL -1)
        message(FATAL_ERROR "picotls fusion changed shape: the MSVC cpuid probe is gone, re-derive the mingw fix")
    endif()
    string(REPLACE "#ifdef _WINDOWS\n/**\n * ptls_fusion_is_supported_by_cpu:"
                   "#if defined(_WINDOWS) && defined(_MSC_VER)\n/**\n * ptls_fusion_is_supported_by_cpu:"
                   _fusion_text "${_fusion_text}")
    file(WRITE "${_fusion_source}" "${_fusion_text}")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${CAN_HUB_PICOTLS_PREFIX}" -B "${CAN_HUB_PICOTLS_BUILD}"
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                ${_picotls_cross}
                ${_picotls_fusion_option}
                "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections ${_picotls_platform_flags}"
        RESULT_VARIABLE _picotls_configure
    )
    if(NOT _picotls_configure EQUAL 0)
        message(FATAL_ERROR "could not configure picotls")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${CAN_HUB_PICOTLS_BUILD}"
                --target picotls-core picotls-minicrypto ${_picotls_fusion_target}
                -j ${CAN_HUB_BUILD_PARALLELISM}
        RESULT_VARIABLE _picotls_build
    )
    if(NOT _picotls_build EQUAL 0)
        message(FATAL_ERROR "could not build picotls")
    endif()
endif()

set(PICOTLS_INCLUDE_DIR "${CAN_HUB_PICOTLS_PREFIX}/include")
set(PICOTLS_LIBRARIES
    "${CAN_HUB_PICOTLS_BUILD}/libpicotls-minicrypto.a"
    "${CAN_HUB_PICOTLS_BUILD}/libpicotls-core.a"
)
if(CAN_HUB_TLS_FUSION)
    list(INSERT PICOTLS_LIBRARIES 0 "${CAN_HUB_PICOTLS_BUILD}/libpicotls-fusion.a")
endif()

add_library(picotls INTERFACE)
if(CAN_HUB_TLS_ARMV8_CRYPTO)
    # The definition is global, the instruction-set flag is not: it belongs to
    # tls_aes_armv8.c alone, applied by whichever CMakeLists compiles it. On
    # 32-bit ARM the flag also turns NEON on, and the armhf baseline is
    # ARMv7 + VFPv3-D16 without it, so letting every file see it would license
    # the compiler to vectorise code that then dies with SIGILL on a CPU that
    # has no NEON. The engine itself is still gated at runtime by HWCAP.
    target_compile_definitions(picotls INTERFACE CAN_HUB_TLS_ARMV8_CRYPTO)
endif()
if(CAN_HUB_TLS_FUSION)
    target_compile_definitions(picotls INTERFACE CAN_HUB_TLS_FUSION)
    # fusion's 256-bit VAES path miscompiles under llvm-mingw: built there,
    # ptls_non_temporal_aes128gcm produces ciphertext that does not match
    # minicrypto's AES-128-GCM for the same key, nonce and input, while the
    # same source built with gcc on Linux matches. Forcing the 128-bit path
    # makes every engine byte-identical again. The QUIC engine never used the
    # 256-bit path (fusion.c hardcodes aesni256 to 0 there), so this costs
    # nothing on Windows beyond the TLS record layer at MTU size.
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_compile_definitions(picotls INTERFACE CAN_HUB_TLS_FUSION_NO_AESNI256)
    endif()
endif()
target_include_directories(picotls INTERFACE "${PICOTLS_INCLUDE_DIR}" "${CAN_HUB_PICOTLS_PREFIX}/lib")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # wincompat.h ships under the Visual Studio project, not under include/
    target_include_directories(picotls INTERFACE "${_picotls_wincompat}")
    target_compile_definitions(picotls INTERFACE _WINDOWS)
endif()
target_link_libraries(picotls INTERFACE ${PICOTLS_LIBRARIES})
