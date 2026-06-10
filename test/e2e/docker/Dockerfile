# Bench image for the can-hub end-to-end suite. Runs privileged with the host
# /lib/modules mounted so it can modprobe vcan and create vcan links inside the
# per-Server network namespaces. The repo is mounted at /work; the release
# binaries are taken from $CAN_HUB_BIN_DIR (default /work/build/x86_64/release).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        iproute2 \
        iptables \
        can-utils \
        kmod \
        libgnutls30 \
        python3 \
        python3-pip \
        iputils-ping \
    && pip3 install --no-cache-dir --break-system-packages \
        robotframework \
        python-can \
    && rm -rf /var/lib/apt/lists/*

ENV CAN_HUB_BIN_DIR=/work/build/x86_64/release
ENV PYTHONPATH=/work/test/e2e
WORKDIR /work/test/e2e
