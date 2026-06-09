# Debug variant of static.Dockerfile: agent only, -O0 -g, not stripped.
# Same musl + source-built GnuTLS static stack, so layers 1-96 are byte-for-byte
# identical to static.Dockerfile and reuse its docker layer cache.
#
#   docker build -f docker/static-debug.Dockerfile --build-arg ARCH=arm64 --output dist/arm64-debug .
#
# Every download is sha256-pinned.

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates cmake curl file gcc g++ git m4 make ninja-build pkg-config xz-utils \
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

ARG GMP_VERSION=6.3.0
ARG GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
RUN . /etc/static-build.env \
    && curl -fsSL -o /tmp/gmp.tar.xz "https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz" \
    && echo "$GMP_SHA256  /tmp/gmp.tar.xz" | sha256sum -c \
    && mkdir /tmp/gmp && tar -xf /tmp/gmp.tar.xz --strip-components=1 -C /tmp/gmp \
    && cd /tmp/gmp \
    && ./configure --host=$CROSS_TRIPLET --prefix=$STAGING --disable-shared --enable-static CFLAGS="-O2 -ffunction-sections -fdata-sections" \
    && make -j"$(nproc)" && make install \
    && rm -rf /tmp/gmp /tmp/gmp.tar.xz

ARG NETTLE_VERSION=3.10.2
ARG NETTLE_SHA256=fe9ff51cb1f2abb5e65a6b8c10a92da0ab5ab6eaf26e7fc2b675c45f1fb519b5
RUN . /etc/static-build.env \
    && curl -fsSL -o /tmp/nettle.tar.gz "https://ftp.gnu.org/gnu/nettle/nettle-$NETTLE_VERSION.tar.gz" \
    && echo "$NETTLE_SHA256  /tmp/nettle.tar.gz" | sha256sum -c \
    && mkdir /tmp/nettle && tar -xf /tmp/nettle.tar.gz --strip-components=1 -C /tmp/nettle \
    && cd /tmp/nettle \
    && ./configure --host=$CROSS_TRIPLET --prefix=$STAGING --disable-shared --enable-static CFLAGS="-O2 -ffunction-sections -fdata-sections" \
        --disable-documentation CPPFLAGS="-I$STAGING/include" LDFLAGS="-L$STAGING/lib" \
    && make -j"$(nproc)" && make install \
    && rm -rf /tmp/nettle /tmp/nettle.tar.gz

ARG GNUTLS_VERSION=3.8.13
ARG GNUTLS_SHA256=ffed8ec1bf09c2426d4f14aae377de4753b53e537d685e604e99a8b16ca9c97e
RUN . /etc/static-build.env \
    && curl -fsSL -o /tmp/gnutls.tar.xz "https://www.gnupg.org/ftp/gcrypt/gnutls/v3.8/gnutls-$GNUTLS_VERSION.tar.xz" \
    && echo "$GNUTLS_SHA256  /tmp/gnutls.tar.xz" | sha256sum -c \
    && mkdir /tmp/gnutls && tar -xf /tmp/gnutls.tar.xz --strip-components=1 -C /tmp/gnutls \
    && cd /tmp/gnutls \
    && ./configure --host=$CROSS_TRIPLET --prefix=$STAGING --disable-shared --enable-static CFLAGS="-O2 -ffunction-sections -fdata-sections" \
        --with-included-libtasn1 --with-included-unistring --without-p11-kit --without-idn \
        --without-brotli --without-zstd --without-zlib --without-tpm --without-tpm2 \
        --disable-doc --disable-tools --disable-tests --disable-cxx --disable-guile \
        CPPFLAGS="-I$STAGING/include" LDFLAGS="-L$STAGING/lib" \
    && make -j"$(nproc)" && make install \
    && rm -rf /tmp/gnutls /tmp/gnutls.tar.xz

COPY . /src
ENV CAN_HUB_SYSROOT=/opt/staging
RUN . /etc/static-build.env \
    && export CAN_HUB_CROSS_TRIPLET=$CROSS_TRIPLET CAN_HUB_PROCESSOR=$PROCESSOR \
    && cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchain-musl-static.cmake \
        -DCAN_HUB_STATIC=ON \
        -DCAN_HUB_PACKAGING=ON \
        -DCAN_HUB_DEB_STATIC=ON \
        -DCAN_HUB_DEB_ARCH=$DEB_ARCH \
    && cmake --build /build --target can-hub-agent \
    && file /build/can-hub-agent \
    && ls -la /build/can-hub-agent \
    && file /build/can-hub-agent | grep -q "statically linked" \
    && mkdir /out \
    && ( cd /build && cpack -G DEB -B /out -D CPACK_COMPONENTS_ALL=agent ) \
    && ls -la /out/*.deb

FROM scratch AS artifact
COPY --from=build /build/can-hub-agent /can-hub-agent
COPY --from=build /out/*.deb /
