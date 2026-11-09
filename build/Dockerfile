# Reproducible build host for VQEAF OS. Use this when you do not want a
# cross toolchain loose on your machine:
#   docker build -t qeafdropos-build build/
#   docker run --rm -v "$PWD":/work -w /work qeafdropos-build make all
FROM debian:12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential curl ca-certificates cpio lz4 zstd gzip \
        device-tree-compiler python3 git \
    && rm -rf /var/lib/apt/lists/*

ENV TARGET_ARCH=aarch64
RUN /usr/bin/env bash -c 'curl -fL https://musl.cc/aarch64-linux-musl-cross.tgz \
        -o /tmp/tc.tgz && mkdir -p /opt/tc && tar -xf /tmp/tc.tgz -C /opt/tc \
        --strip-components=1 && rm /tmp/tc.tgz' || \
    echo 'toolchain download failed; mount one and set CROSS_COMPILE'

ENV PATH=/opt/tc/bin:$PATH
ENV CROSS_COMPILE=aarch64-linux-musl-

WORKDIR /work
CMD ["make", "all"]
