# E0 spike — standalone build, not part of the main can-hub build.
#
# Requires ngtcp2 >= 1.0 built with GnuTLS (distro 0.12 has the PTO bug, see RESULTS.md):
#   PKG_CONFIG_PATH=/tmp/ngtcp2-install/lib/pkgconfig make
#   LD_LIBRARY_PATH=/tmp/ngtcp2-install/lib ./run-bench.sh

CFLAGS ?= -O2 -Wall -Wextra -g
PKG_DEPS := libngtcp2 libngtcp2_crypto_gnutls gnutls

tunnel: tunnel.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(PKG_DEPS)) -o $@ $< $(shell pkg-config --libs $(PKG_DEPS))

clean:
	rm -f tunnel

.PHONY: clean
