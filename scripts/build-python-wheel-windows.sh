#!/bin/sh
# Builds the python-can-hub wheel for x86_64 Windows: cross-compiles
# libcanhub.dll (make windows, llvm-mingw/mingw-w64 in PATH or
# CAN_HUB_MINGW_ROOT), bundles it into the package and produces a
# win_amd64-tagged wheel under python/dist/.
set -e

cd "$(dirname "$0")/.."

make windows
rm -f python/canhub/libcanhub.so
cp build/mingw-x86_64/release/libcanhub.dll python/canhub/libcanhub.dll

cd python
rm -rf build ./*.egg-info
python3 setup.py bdist_wheel --plat-name win_amd64 --dist-dir dist
rm -f canhub/libcanhub.dll
ls -la dist/
