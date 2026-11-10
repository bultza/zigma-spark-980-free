#!/bin/sh
# Build dependencies for Debian and Ubuntu.
set -eu
if ! command -v apt-get >/dev/null 2>&1; then
    printf '%s\n' 'This script requires apt-get. See src/README.md for the tool list.' >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    exec sudo sh "$0" "$@"
fi
apt-get update
apt-get install build-essential python3 curl ca-certificates file xz-utils bzip2
