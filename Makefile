# can-hub — build entry points. Thin wrapper over cmake.
#
#   make release [ARCH=x86_64|armhf|arm64]   Optimized build (-O2).
#   make debug   [ARCH=...]                  Debug build (-O0 -g).
#   make install [ARCH=...] [PREFIX=...]     Install release binaries.
#   make test                                Build + run host unit tests (CEST).
#   make clean                               Remove build trees.

CMAKE ?= cmake
GENERATOR ?= Ninja
ARCH ?= x86_64
PREFIX ?= /usr/local

_TOOLCHAIN := $(abspath cmake/toolchain-$(ARCH).cmake)
_BUILD_RELEASE := build/$(ARCH)/release
_BUILD_DEBUG := build/$(ARCH)/debug
_BUILD_TEST := build/test
_CEST_RUNNER := test/vendor/cest-runner_linux_x86_64

.PHONY: release debug install test clean

release:
	$(CMAKE) -B $(_BUILD_RELEASE) \
	         -G $(GENERATOR) \
	         -DCMAKE_BUILD_TYPE=Release \
	         -DCMAKE_TOOLCHAIN_FILE=$(_TOOLCHAIN) \
	         -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	$(CMAKE) --build $(_BUILD_RELEASE)

debug:
	$(CMAKE) -B $(_BUILD_DEBUG) \
	         -G $(GENERATOR) \
	         -DCMAKE_BUILD_TYPE=Debug \
	         -DCMAKE_TOOLCHAIN_FILE=$(_TOOLCHAIN) \
	         -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	$(CMAKE) --build $(_BUILD_DEBUG)

install: release
	$(CMAKE) --install $(_BUILD_RELEASE)

test:
	$(CMAKE) -B $(_BUILD_TEST) test/ \
	         -G $(GENERATOR) \
	         -DCMAKE_TOOLCHAIN_FILE=$(abspath cmake/toolchain-x86_64.cmake)
	$(CMAKE) --build $(_BUILD_TEST)
	@chmod +x $(_CEST_RUNNER)
	# setarch -R: the runner is an ASan build (mandatory: memory/leak checks)
	# and ASan shadow memory collides randomly with the high-entropy mmap
	# ASLR of kernels >= 6.5 (intermittent SIGSEGV inside ASan's handler).
	# Disabling ASLR for the runner process keeps ASan fully functional.
	setarch -R $(_CEST_RUNNER) $(_BUILD_TEST)

clean:
	rm -rf build/
