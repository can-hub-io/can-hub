#!/bin/sh
# Measures the AEAD engines on a real ARM core, both execution states.
#
# A Cortex-A76 (Raspberry Pi 5) implements AArch32 at EL0, so one machine can
# time the armv7 build and the arm64 build on the same silicon. That is the
# only way to separate the cost of the 32-bit ISA from the cost of the core it
# runs on, and it needs no emulator: qemu answers agreement, never speed.
#
# Run it on the Pi, from a can-hub checkout:
#
#   sh spike/aead-bench/run-on-pi.sh
#
# What it cannot tell you: how the armv7 fleet performs. An A76 running 32-bit
# code is still an A76. The ratio against OpenSSL on the same core is the part
# that carries over to an A7 or an A53; the absolute microseconds do not.

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BENCH="$ROOT/spike/aead-bench"

echo "core:   $(grep -m1 'model name\|Model' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
echo "hwcap:  $(grep -o 'aes\|pmull' /proc/cpuinfo | sort -u | tr '\n' ' ')"
echo "kernel: $(uname -mr)"
echo

if [ ! -d /proc/sys/fs/binfmt_misc ] || ! grep -qi 'CONFIG_COMPAT=y' /boot/config-"$(uname -r)" 2>/dev/null; then
    echo "note: could not confirm CONFIG_COMPAT from /boot; the armv7 run below"
    echo "      answers it directly — 'Exec format error' means 64-bit only."
    echo
fi

build_tree() {
    arch=$1
    if [ -f "$ROOT/build/$arch/release/libmonocypher.a" ]; then
        return 0
    fi
    echo "== building the $arch tree (one-off, needs network on first configure) =="
    make -C "$ROOT" release ARCH="$arch"
}

run_bench() {
    arch=$1; cc=$2; fusion=$3; armv8=$4
    echo "== $arch =="
    rm -f "$BENCH/aead_bench"
    make -C "$BENCH" \
        CC="$cc" \
        CFLAGS="-O2 -Wall -Wextra -static${armv8:+ -march=armv8-a+crypto}" \
        BUILD="$ROOT/build/$arch/release" \
        FUSION="$fusion" ${armv8:+ARMV8=1} >/dev/null
    "$BENCH/aead_bench" || echo "  -> did not run on this kernel"
    rm -f "$BENCH/aead_bench"
    echo
}

# arm64 first: it is the configuration that ships, and it needs no cross
# toolchain on an arm64 host.
build_tree arm64 2>/dev/null || make -C "$ROOT" release
run_bench arm64 cc 0 1

if ! command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    echo "armv7 skipped: install gcc-arm-linux-gnueabihf to build it"
    exit 0
fi

build_tree armhf
run_bench armhf arm-linux-gnueabihf-gcc 0 ""

echo "== OpenSSL on the same core, for the ratio that transfers =="
echo "   64-bit:"
openssl speed -elapsed -evp chacha20-poly1305 -bytes 1200 2>/dev/null | tail -2 || echo "   (openssl not installed)"
echo
echo "   32-bit needs an armhf OpenSSL:  sudo dpkg --add-architecture armhf"
echo "                                   sudo apt install openssl:armhf"
