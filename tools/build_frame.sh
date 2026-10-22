#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
cd "$root"

if [[ "$(uname -s)" != Linux || "$(uname -m)" != aarch64 ]]; then
    printf 'Steam Frame build requires Linux/aarch64 (got %s/%s).\n' "$(uname -s)" "$(uname -m)" >&2
    exit 2
fi

sdk_version=1.1.63
sdk_url="https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-${sdk_version}.tar.gz"
sdk_sha256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f
sdk_dir="$root/build/openxr-sdk-${sdk_version}"
sdk_src="$sdk_dir/source"
sdk_build="$sdk_dir/build"
sdk_install="$sdk_dir/install"
sdk_archive="$sdk_dir/openxr-sdk.tar.gz"

mkdir -p "$sdk_dir"
if [[ ! -f "$sdk_archive" ]] || [[ "$(sha256sum "$sdk_archive" | cut -d ' ' -f 1)" != "$sdk_sha256" ]]; then
    rm -f "$sdk_archive"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 "$sdk_url" -o "$sdk_archive"
    elif command -v wget >/dev/null 2>&1; then
        wget --tries=3 -O "$sdk_archive" "$sdk_url"
    else
        printf 'Need curl or wget to fetch the pinned OpenXR SDK.\n' >&2
        exit 1
    fi
fi
printf '%s  %s\n' "$sdk_sha256" "$sdk_archive" | sha256sum -c -

if [[ ! -f "$sdk_src/CMakeLists.txt" ]]; then
    mkdir -p "$sdk_src"
    tar -xzf "$sdk_archive" --strip-components=1 -C "$sdk_src"
fi

if [[ ! -f "$sdk_install/lib/libopenxr_loader.a" ]]; then
    cmake -S "$sdk_src" -B "$sdk_build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$sdk_install" \
        -DDYNAMIC_LOADER=OFF \
        -DBUILD_API_LAYERS=OFF \
        -DBUILD_TESTS=OFF \
        -DBUILD_CONFORMANCE_TESTS=OFF \
        -DBUILD_WITH_SYSTEM_JSONCPP=OFF \
        -DBUILD_WITH_XCB_HEADERS=OFF \
        -DBUILD_WITH_WAYLAND_HEADERS=OFF
    cmake --build "$sdk_build" --parallel 2
    cmake --install "$sdk_build"
fi

make -j2 build/vrde

cc -O2 -Wall -Wextra \
    -I"$sdk_install/include" \
    $(pkg-config --cflags sdl2 x11 gl) \
    tools/openxr_colour_probe.c \
    "$sdk_install/lib/libopenxr_loader.a" \
    $(pkg-config --libs sdl2 x11 gl) -ldl -pthread -lm -lstdc++ -static-libstdc++ -static-libgcc \
    -o build/openxr-colour-probe

printf 'Built flat game: %s/build/vrde\n' "$root"
printf 'Built standalone probe: %s/build/openxr-colour-probe\n' "$root"
