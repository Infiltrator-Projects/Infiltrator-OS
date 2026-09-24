#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="${VERSION:-1.0.0}"
DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltrator-os-plymouth-theme_${VERSION}"
PKGROOT="$WORK/root"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltrator-os"

rm -rf "$WORK"
mkdir -p "$PKGROOT/DEBIAN" "$THEME_DIR" "$DIST"

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"

install -m 0644 "$ROOT/plymouth/infiltrator-os.plymouth" "$THEME_DIR/infiltrator-os.plymouth"
install -m 0644 "$ROOT/plymouth/infiltrator-os.script" "$THEME_DIR/infiltrator-os.script"

cat "$ROOT"/assets/infiltrator-os.png.b64.chunk-* | base64 -d > "$THEME_DIR/infiltrator-os.png"
chmod 0644 "$THEME_DIR/infiltrator-os.png"

PNG_TYPE="$(file -b --mime-type "$THEME_DIR/infiltrator-os.png")"
if [ "$PNG_TYPE" != "image/png" ]; then
    echo "Reconstructed artwork is not a PNG: $PNG_TYPE" >&2
    exit 1
fi

OUT="$DIST/infiltrator-os-plymouth-theme_${VERSION}_all.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"

dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null

echo "$OUT"
