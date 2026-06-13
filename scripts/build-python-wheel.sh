#!/bin/sh
# Builds the python-can-hub wheel.
#
#   build-python-wheel.sh            host arch, glibc-tagged (linux_x86_64),
#                                    fast path for local development
#   build-python-wheel.sh x86_64    \
#   build-python-wheel.sh aarch64    > manylinux wheel in a docker container,
#   build-python-wheel.sh armv7l    /  repaired by auditwheel, ready for PyPI
#
# The cross arches need buildx with QEMU binfmt registered for the target.
set -e

cd "$(dirname "$0")/.."

ARCH="${1:-}"

if [ -z "$ARCH" ]; then
    make release
    cp build/x86_64/release/libcanhub.so python/canhub/libcanhub.so
    cd python
    rm -rf build ./*.egg-info
    python3 -m pip wheel . --no-deps -w dist
    ls -la dist/
    exit 0
fi

case "$ARCH" in
    x86_64)  IMAGE=quay.io/pypa/manylinux_2_28_x86_64;  PLATFORM=linux/amd64 ;;
    aarch64) IMAGE=quay.io/pypa/manylinux_2_28_aarch64; PLATFORM=linux/arm64 ;;
    armv7l)  IMAGE=quay.io/pypa/manylinux_2_31_armv7l;  PLATFORM=linux/arm/v7 ;;
    *) echo "unsupported arch '$ARCH' (x86_64, aarch64, armv7l)" >&2; exit 1 ;;
esac

docker build -f docker/wheel.Dockerfile \
    --platform "$PLATFORM" \
    --build-arg MANYLINUX_IMAGE="$IMAGE" \
    --output "python/dist/$ARCH" .
ls -la "python/dist/$ARCH"
