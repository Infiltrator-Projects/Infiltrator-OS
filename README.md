# Infiltrator OS

Infiltrator OS is the operating-system project for Infiltrator Projects.

This repository assembles the released Infiltrator OS desktop components for Debian-family amd64 installations. It builds an `infiltrator-os` dependency bundle, the Plymouth boot theme, and a native installer.

## Components

The `infiltrator-os` package installs InfiltratorFS, Filesystem Support, Software, System Settings, Calendar, System Monitor, Defragmenter, Calculator, and the Plymouth theme. APT resolves the current versions from the Infiltrator repository. The bundle installs applications and filesystem tools; it does not change the root filesystem or replace the distribution kernel.

## Install

If the Infiltrator beta APT source is already configured:

```bash
sudo apt update
sudo apt install infiltrator-os
```

The release also includes a native `.run` installer. To configure the current unsigned beta APT source explicitly and then install the bundle:

```bash
sudo ./infiltrator-os-1.1.0-linux-native.run --enable-beta-repository
```

The beta source currently uses APT's `trusted=yes` setting. The installer leaves existing APT source files alone and writes only `/etc/apt/sources.list.d/infiltrator-beta.list` when the option is given. Future signed repository support should replace that trust setting.

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

Both Debian packages and the native installer are written to `dist/`.

GitHub Actions builds and checks the packages on every push to `main` and on manual workflow runs. The first successful push of a new `VERSION` publishes the DEBs, native installer and source ZIP as release assets. Bump `VERSION` for each later release.

## Artwork

`assets/infiltrator-os.png` is the sole Plymouth artwork used by this package. The build verifies its SHA-256 before packaging so an accidental artwork change cannot silently enter a release.
