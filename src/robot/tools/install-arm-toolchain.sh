#!/bin/sh
# Install the pinned Bootlin ARMv5 soft-float musl SDK for an x86-64 Linux host.
set -eu

if [ "$(uname -s):$(uname -m)" != Linux:x86_64 ]; then
    echo 'The pinned Bootlin SDK requires an x86-64 Linux build host.' >&2
    exit 1
fi

cache=$1
name=armv5-eabi--musl--stable-2026.08-1
archive=$name.tar.xz
digest=a7db571eaa0f1ba2eea9a3932e8ad64bfaf33bfb7e7b96dd09702fe3ecedcf1a
base=https://toolchains.bootlin.com/downloads/releases/toolchains/armv5-eabi/tarballs
mkdir -p "$cache"
cd "$cache"

if [ ! -f "$archive" ]; then
    curl --fail --location --silent --show-error "$base/$archive" --output "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha256sum -c -
tar -xJf "$archive"
(cd "$name" && ./relocate-sdk.sh)
test "$("$name/bin/arm-buildroot-linux-musleabi-gcc" -dumpmachine)" = arm-buildroot-linux-musleabi
touch "$name/.ready"
