#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="${VERSION:-1.0.0}"
DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltrator-os-plymouth-theme_${VERSION}"
PKGROOT="$WORK/root"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltrator-os"
ARTWORK="$ROOT/assets/infiltrator-os.png"
ARTWORK_SHA256="4820891225675ed9bb0bb1bb82e3680df4bbdccc736ce5d092b80e95e5d05978"

rm -rf "$WORK"
mkdir -p "$PKGROOT/DEBIAN" "$THEME_DIR" "$DIST"

printf '%s  %s\n' "$ARTWORK_SHA256" "$ARTWORK" | sha256sum -c -

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"

install -m 0644 "$ROOT/plymouth/infiltrator-os.plymouth" "$THEME_DIR/infiltrator-os.plymouth"
install -m 0644 "$ROOT/plymouth/infiltrator-os.script" "$THEME_DIR/infiltrator-os.script"
install -m 0644 "$ARTWORK" "$THEME_DIR/infiltrator-os.png"

PNG_TYPE="$(file -b --mime-type "$THEME_DIR/infiltrator-os.png")"
if [ "$PNG_TYPE" != "image/png" ]; then
    echo "Artwork is not a PNG: $PNG_TYPE" >&2
    exit 1
fi

OUT="$DIST/infiltrator-os-plymouth-theme_${VERSION}_all.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"

dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null

echo "$OUT"
