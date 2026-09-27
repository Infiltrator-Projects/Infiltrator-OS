#!/bin/sh
set -eu

SOURCE_FILE=/etc/apt/sources.list.d/infiltrator-beta.list
SOURCE_LINE='deb [trusted=yes arch=amd64] https://infiltrator-projects.github.io/Infiltrator-Repository beta main'

usage() {
    echo 'Usage: sudo ./infiltrator-os-*-linux-native.run [--enable-beta-repository]' >&2
    echo 'Without the option, an existing APT source must provide infiltrator-os.' >&2
}

case "${1:-}" in
    '') ;;
    --enable-beta-repository)
        if [ "$#" -ne 1 ]; then usage; exit 2; fi
        ;;
    *) usage; exit 2 ;;
esac

if [ "$(id -u)" -ne 0 ]; then
    echo 'Run this installer with sudo.' >&2
    exit 1
fi
if [ "$(dpkg --print-architecture)" != amd64 ]; then
    echo 'The current Infiltrator OS desktop packages require amd64.' >&2
    exit 1
fi

if [ "${1:-}" = --enable-beta-repository ]; then
    echo 'Enabling the Infiltrator beta APT repository (currently unsigned/trusted=yes).'
    printf '%s\n' "$SOURCE_LINE" > "$SOURCE_FILE"
fi

apt-get update
if ! apt-cache show infiltrator-os >/dev/null 2>&1; then
    echo 'infiltrator-os is unavailable from configured APT sources.' >&2
    echo 'Use --enable-beta-repository to configure the Infiltrator beta source.' >&2
    exit 1
fi
apt-get install infiltrator-os
