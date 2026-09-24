# Infiltrator OS

Infiltrator OS is the operating-system project for Infiltrator Projects.

The repository currently contains the canonical Plymouth boot, reboot and shutdown branding package for Debian and Linux Mint derived installations.

## Plymouth theme

The theme installs to:

```text
/usr/share/plymouth/themes/infiltrator-os/
```

It uses one static Infiltrator Operating System image derived only from the supplied approved artwork. There are no generated animation frames and no alternative artwork.

Installation normalises Plymouth selection through `/etc/plymouth/plymouthd.conf`, uses Debian's `plymouth-set-default-theme` when available, keeps the Ubuntu/Linux Mint `default.plymouth` alternative pointed at the same canonical theme for compatibility, ensures `quiet splash` is present for GRUB systems, and rebuilds initramfs.

## Build the DEB

```bash
bash scripts/build-deb.sh
```

The package is written to `dist/`.

GitHub Actions builds the DEB automatically on every push to `main` and on manual workflow runs. A tag matching `v*` also publishes the built DEB as a GitHub release asset.

## Source artwork

The files `assets/infiltrator-os.png.b64.chunk-*` are ordered base64 chunks of the approved Infiltrator Operating System image, resized for the small Plymouth splash. The build script reconstructs exactly one PNG from those chunks.
