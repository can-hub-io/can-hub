# AEAD bench — standalone, not part of the main can-hub build.
#
# Links the archives an existing build tree already produced, so it measures the
# engines that build negotiates. Point BUILD at that tree:
#
#   make BUILD=../../build/x86_64/release && ./aead_bench
#   make BUILD=../../build/arm64/release   # on the arm64 target, not cross
#
# Fusion is x86-64 only; the build tree's own gate decides, so pass FUSION=0
# when the tree was configured without it (every non-x86-64 target).

BUILD ?= ../../build/x86_64/release
ROOT := ../..
PICOTLS_SRC := $(ROOT)/build/picotls-src
FUSION ?= 1

CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS := -I$(ROOT)/src -I$(PICOTLS_SRC)/include -I$(PICOTLS_SRC)/lib -I$(ROOT)/build/monocypher-src/src
LIBS := $(BUILD)/picotls-build/libpicotls-minicrypto.a \
        $(BUILD)/picotls-build/libpicotls-core.a \
        $(BUILD)/libmonocypher.a

ifeq ($(FUSION),1)
CFLAGS += -maes -mpclmul -mssse3 -mavx2
CPPFLAGS += -DCAN_HUB_TLS_FUSION
LIBS := $(BUILD)/picotls-build/libpicotls-fusion.a $(LIBS)
endif

# On aarch64 the hardware AES engine is ours rather than picotls's, so it is a
# source file instead of an archive. ARMV8=1 builds it in; the runtime HWCAP
# check still decides whether it is used.
ifeq ($(ARMV8),1)
CFLAGS += -march=armv8-a+crypto
CPPFLAGS += -DCAN_HUB_TLS_ARMV8_CRYPTO
EXTRA_SOURCES += $(ROOT)/src/platform/linux/shared/tls_aes_armv8.c
endif

aead_bench: aead_bench.c $(ROOT)/src/platform/linux/shared/tls_aead.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f aead_bench

.PHONY: clean
