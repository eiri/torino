#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

picotool="${PICOTOOL:-picotool}"
command -v "$picotool" >/dev/null || {
    echo "Install picotool or set PICOTOOL to its executable" >&2
    exit 1
}

# Keep pinned dependencies and the cross-compiler in the ignored target directory.
checkout() {
    local path="$1" url="$2" revision="$3"
    if [[ ! -d "$path/.git" ]]; then
        git init "$path"
        git -C "$path" remote add origin "$url"
        git -C "$path" fetch --depth 1 origin "$revision"
        git -C "$path" checkout --detach FETCH_HEAD
    fi
    [[ "$(git -C "$path" rev-parse HEAD)" == "$revision" ]]
    git -C "$path" diff --quiet
}
mkdir -p target/host-deps target/tools
checkout target/host-deps/pico-sdk https://github.com/raspberrypi/pico-sdk.git a1438dff1d38bd9c65dbd693f0e5db4b9ae91779
checkout target/host-deps/Pico-PIO-USB https://github.com/sekigon-gonnoc/Pico-PIO-USB.git b186e36bea0f3bcc2ee94ca1fa81e7f71281443a
git -C target/host-deps/pico-sdk submodule update --init --depth 1 lib/tinyusb
[[ "$(git -C target/host-deps/pico-sdk/lib/tinyusb rev-parse HEAD)" == 86ad6e56c1700e85f1c5678607a762cfe3aa2f47 ]]
git -C target/host-deps/pico-sdk/lib/tinyusb diff --quiet

compiler="${ARM_GCC_ROOT:-$root/target/tools/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi}"
if [[ ! -x "$compiler/bin/arm-none-eabi-gcc" ]]; then
    if [[ -n "${ARM_GCC_ROOT:-}" ]]; then
        echo "ARM_GCC_ROOT does not contain bin/arm-none-eabi-gcc" >&2; exit 1
    fi
    [[ "$(uname -s)-$(uname -m)" == Darwin-arm64 ]] || {
        echo "Set ARM_GCC_ROOT to an installed ARM bare-metal GCC toolchain" >&2; exit 1;
    }
    archive=target/tools/arm-gnu-13.2.tar.xz
    curl --fail --location --silent --show-error \
        https://developer.arm.com/-/media/Files/downloads/gnu/13.2.Rel1/binrel/arm-gnu-toolchain-13.2.Rel1-darwin-arm64-arm-none-eabi.tar.xz -o "$archive"
    printf '%s  %s\n' 39c44f8af42695b7b871df42e346c09fee670ea8dfc11f17083e296ea2b0d279 "$archive" | shasum -a 256 -c -
    tar -xJf "$archive" -C target/tools
fi

# Build only the USB relay firmware. Tests use a separate native CMake build.
PICO_SDK_PATH="$root/target/host-deps/pico-sdk" cmake -S . -B target/build \
    -DPICO_PIO_USB_PATH="$root/target/host-deps/Pico-PIO-USB" \
    -DPICO_TOOLCHAIN_PATH="$compiler" -DPICO_NO_PICOTOOL=1 -DCMAKE_BUILD_TYPE=Release \
    -DTORINO_STORE_LIMIT="${TORINO_STORE_LIMIT:-24}"
cmake --build target/build --parallel
"$picotool" uf2 convert target/build/torino.elf target/build/torino.uf2
shasum -a 256 target/build/torino.uf2
