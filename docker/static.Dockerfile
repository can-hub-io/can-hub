# Fully static can-hub binaries (hub, agent, client, cli) against musl and
# a source-built OpenSSL stack. Zero runtime dependencies: the binaries run
# on any Linux of the right architecture regardless of the distro, its libc
# or its package set (embedded boards, vendor OSes, old installations).
#
# Architecture is selected with one build arg; the toolchain pins live here:
#
#   docker build -f docker/static.Dockerfile --build-arg ARCH=armv7 --output dist/armv7 .
#   make static ARCH=armv7|arm64|x86_64
#
# Every download is sha256-pinned.

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates cmake curl file gcc g++ git m4 make ninja-build perl pkg-config xz-utils \
    && rm -rf /var/lib/apt/lists/*

ARG ARCH=armv7
ARG BOOTLIN=https://toolchains.bootlin.com/downloads/releases/toolchains
ARG BOOTLIN_RELEASE=stable-2025.08-1

RUN case "$ARCH" in \
        armv7) \
            echo "TOOLCHAIN_URL=$BOOTLIN/armv7-eabihf/tarballs/armv7-eabihf--musl--$BOOTLIN_RELEASE.tar.xz" >> /etc/static-build.env; \
            echo "TOOLCHAIN_SHA256=2f3a34458c3a8b961bd09f89669130fcdc4c1dbc6e31ada720527e4ad3741c11" >> /etc/static-build.env; \
            echo "CROSS_TRIPLET=arm-buildroot-linux-musleabihf" >> /etc/static-build.env; \
            echo "PROCESSOR=arm" >> /etc/static-build.env; \
            echo "DEB_ARCH=armhf" >> /etc/static-build.env;; \
        arm64) \
            echo "TOOLCHAIN_URL=$BOOTLIN/aarch64/tarballs/aarch64--musl--$BOOTLIN_RELEASE.tar.xz" >> /etc/static-build.env; \
            echo "TOOLCHAIN_SHA256=defba831ffa1175236f137069333e21ed46d4d19feb5080a90cf248b6fc2cb08" >> /etc/static-build.env; \
            echo "CROSS_TRIPLET=aarch64-buildroot-linux-musl" >> /etc/static-build.env; \
            echo "PROCESSOR=aarch64" >> /etc/static-build.env; \
            echo "DEB_ARCH=arm64" >> /etc/static-build.env;; \
        x86_64) \
            echo "TOOLCHAIN_URL=$BOOTLIN/x86-64/tarballs/x86-64--musl--$BOOTLIN_RELEASE.tar.xz" >> /etc/static-build.env; \
            echo "TOOLCHAIN_SHA256=09fca3aa89540f1b01b5f4210d488cbeb00f522044c53e9989b1dd8a38076912" >> /etc/static-build.env; \
            echo "CROSS_TRIPLET=x86_64-buildroot-linux-musl" >> /etc/static-build.env; \
            echo "PROCESSOR=x86_64" >> /etc/static-build.env; \
            echo "DEB_ARCH=amd64" >> /etc/static-build.env;; \
        *) echo "unsupported ARCH '$ARCH' (armv7, arm64, x86_64)" >&2; exit 1;; \
    esac

RUN . /etc/static-build.env \
    && curl -fsSL -o /tmp/toolchain.tar.xz "$TOOLCHAIN_URL" \
    && echo "$TOOLCHAIN_SHA256  /tmp/toolchain.tar.xz" | sha256sum -c \
    && mkdir /opt/toolchain \
    && tar -xf /tmp/toolchain.tar.xz --strip-components=1 -C /opt/toolchain \
    && rm /tmp/toolchain.tar.xz

ENV PATH=/opt/toolchain/bin:$PATH
ENV STAGING=/opt/staging
ENV PKG_CONFIG_PATH=$STAGING/lib/pkgconfig
ENV PKG_CONFIG_LIBDIR=$STAGING/lib/pkgconfig

ARG OPENSSL_VERSION=3.5.7
ARG OPENSSL_SHA256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
RUN . /etc/static-build.env \
    && case "$PROCESSOR" in \
        x86_64) OPENSSL_TARGET=linux-x86_64;; \
        aarch64) OPENSSL_TARGET=linux-aarch64;; \
        arm) OPENSSL_TARGET=linux-armv4;; \
    esac \
    && curl -fsSL -o /tmp/openssl.tar.gz "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
    && echo "$OPENSSL_SHA256  /tmp/openssl.tar.gz" | sha256sum -c \
    && mkdir /tmp/openssl && tar -xf /tmp/openssl.tar.gz --strip-components=1 -C /tmp/openssl \
    && cd /tmp/openssl \
    && ./Configure "$OPENSSL_TARGET" --cross-compile-prefix=$CROSS_TRIPLET- --prefix=$STAGING --libdir=lib \
        no-shared no-apps no-docs no-tests -O2 -ffunction-sections -fdata-sections \
    && make -j"$(nproc)" && make install_sw \
    && rm -rf /tmp/openssl /tmp/openssl.tar.gz

COPY . /src
ENV CAN_HUB_SYSROOT=/opt/staging
RUN . /etc/static-build.env \
    && export CAN_HUB_CROSS_TRIPLET=$CROSS_TRIPLET CAN_HUB_PROCESSOR=$PROCESSOR \
    && cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchain-musl-static.cmake \
        -DCAN_HUB_STATIC=ON \
        -DCAN_HUB_PACKAGING=ON \
        -DCAN_HUB_DEB_STATIC=ON \
        -DCAN_HUB_DEB_ARCH=$DEB_ARCH \
    && cmake --build /build --target can-hub can-hub-agent can-hub-client can-hub-cli \
    && $CROSS_TRIPLET-strip /build/can-hub /build/can-hub-agent /build/can-hub-client /build/can-hub-cli \
    && file /build/can-hub /build/can-hub-agent /build/can-hub-client /build/can-hub-cli \
    && ls -la /build/can-hub /build/can-hub-agent /build/can-hub-client /build/can-hub-cli \
    && file /build/can-hub | grep -q "statically linked" \
    && file /build/can-hub-agent | grep -q "statically linked" \
    && file /build/can-hub-client | grep -q "statically linked" \
    && file /build/can-hub-cli | grep -q "statically linked" \
    && mkdir /out \
    && ( cd /build && cpack -G DEB -B /out ) \
    && ls -la /out/*.deb

FROM scratch AS artifact
COPY --from=build /build/can-hub /can-hub
COPY --from=build /build/can-hub-agent /can-hub-agent
COPY --from=build /build/can-hub-client /can-hub-client
COPY --from=build /build/can-hub-cli /can-hub-cli
COPY --from=build /out/*.deb /
