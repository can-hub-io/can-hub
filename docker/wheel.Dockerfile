# manylinux wheel for python-can-hub: builds libcanhub.so against glibc inside
# a manylinux image, bundles it into the package and produces a platform wheel
# repaired by auditwheel. The .so embeds OpenSSL/ngtcp2/SQLite statically, so
# glibc is the only dynamic dependency and the wheel is interpreter-independent
# (one wheel per platform, py3-none-manylinux_*).
#
#   docker build -f docker/wheel.Dockerfile \
#       --build-arg MANYLINUX_IMAGE=quay.io/pypa/manylinux_2_28_x86_64 \
#       --output dist/wheels .
#   scripts/build-python-wheel.sh x86_64|aarch64|armv7l
#
# The build python is only the wheel packer; setup.py forces the py3-none ABI
# tag, so the choice of interpreter does not leak into the artifact.

ARG MANYLINUX_IMAGE
FROM ${MANYLINUX_IMAGE} AS build

ARG BUILD_PYTHON=cp312-cp312
ENV PATH=/opt/python/${BUILD_PYTHON}/bin:$PATH

# ninja from pip works on every base; OpenSSL's Configure needs the full perl
# core, which the AlmaLinux-based images (manylinux_2_28) split out — the
# Ubuntu-based ones (manylinux_2_31_armv7l) already ship it.
RUN pip install ninja \
    && if command -v yum >/dev/null 2>&1; then yum install -y perl-core; fi

COPY . /src

RUN cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCAN_HUB_STATIC=OFF \
    && cmake --build /build --target canhub_shared

RUN cp /build/libcanhub.so.0 /src/python/canhub/libcanhub.so \
    && cd /src/python \
    && rm -rf build ./*.egg-info dist \
    && pip wheel . --no-deps -w dist \
    && auditwheel show dist/*.whl \
    && auditwheel repair dist/*.whl -w /out

FROM scratch AS artifact
COPY --from=build /out/*.whl /
