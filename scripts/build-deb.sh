#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="${VERSION:-1.0.3}"
DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltrator-os-plymouth-theme_${VERSION}"
PKGROOT="$WORK/root"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltrator-os"
ARTWORK="$ROOT/assets/infiltrator-os.png"
ARTWORK_SHA256="4820891225675ed9bb0bb1bb82e3680df4bbdccc736ce5d092b80e95e5d05978"

rm -rf "$WORK"
mkdir -p "$PKGROOT/DEBIAN" "$THEME_DIR" "$DIST"
mkdir -p "$PKGROOT/usr/lib/infiltrator-os"

printf '%s  %s\n' "$ARTWORK_SHA256" "$ARTWORK" | sha256sum -c -

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/stage-theme.c" -o "$WORK/stage-theme"
"$WORK/stage-theme" \
    "$ROOT/plymouth/infiltrator-os.plymouth" \
    "$ROOT/plymouth/infiltrator-os.script" \
    "$ARTWORK" "$THEME_DIR"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/select-theme.c" \
    -o "$PKGROOT/usr/lib/infiltrator-os/select-theme"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/install-system-monitor.c" \
    -o "$PKGROOT/usr/lib/infiltrator-os/install-system-monitor"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/start-system-monitor-install.c" \
    -o "$PKGROOT/usr/lib/infiltrator-os/start-system-monitor-install"

GTK_CFLAGS="$(pkg-config --cflags gtk+-3.0)"
GTK_LIBS="$(pkg-config --libs gtk+-3.0)"
# shellcheck disable=SC2086
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic $GTK_CFLAGS \
    "$ROOT/packaging/system-monitor-progress.c" \
    -o "$PKGROOT/usr/lib/infiltrator-os/system-monitor-progress" $GTK_LIBS

PNG_TYPE="$(file -b --mime-type "$THEME_DIR/infiltrator-os.png")"
if [ "$PNG_TYPE" != "image/png" ]; then
    echo "Artwork is not a PNG: $PNG_TYPE" >&2
    exit 1
fi

OUT="$DIST/infiltrator-os-plymouth-theme_${VERSION}_amd64.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"

dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null

echo "$OUT"
