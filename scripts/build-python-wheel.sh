#!/bin/sh
# Builds the python-can-hub wheel for the host architecture: compiles
# libcanhub.so (release), bundles it into the package and produces a
# platform-tagged wheel under python/dist/.
set -e

cd "$(dirname "$0")/.."

make release
cp build/x86_64/release/libcanhub.so python/canhub/libcanhub.so

cd python
rm -rf build ./*.egg-info
python3 -m pip wheel . --no-deps -w dist
ls -la dist/
