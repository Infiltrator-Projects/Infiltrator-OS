# Infiltrator OS

Infiltrator OS is the operating-system project for Infiltrator Projects.

This repository currently contains the canonical Plymouth boot, reboot and shutdown branding package for Debian and Linux Mint derived installations.

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

## Artwork

`assets/infiltrator-os.png` is the sole Plymouth artwork used by this package. The build verifies its SHA-256 before packaging so an accidental artwork change cannot silently enter a release.
