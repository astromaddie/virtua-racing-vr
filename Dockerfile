# Clean ARM64 Linux build environment for Steam Frame.
# Build with: docker buildx build --platform linux/arm64 -t virtua-racing-vr-build .
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        file \
        libgl-dev \
        libx11-dev \
        libsdl2-dev \
        pkg-config \
        python3 \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
