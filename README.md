# Infiltrator OS

Infiltrator OS is the operating-system project for Infiltrator Projects.

This repository currently contains the canonical Plymouth boot, reboot and shutdown branding package for Debian and Linux Mint derived installations.

## Plymouth theme

The theme installs to:

```text
/usr/share/plymouth/themes/infiltrator-os/
```

It uses one static Infiltrator Operating System image derived only from the supplied approved artwork. There are no generated animation frames and no alternative artwork.

During package construction, `packaging/stage-theme.c` places the PNG and Plymouth descriptors in the DEB payload. The build verifies the artwork SHA-256 first. Debian then extracts those exact files on installation. The installed C helper configures Plymouth for Debian and Linux Mint, adds `quiet splash` if needed, rebuilds GRUB and initramfs, and removes the alternative on package removal. Plymouth centres and displays the same image at boot and shutdown.

Installation normalises Plymouth selection through `/etc/plymouth/plymouthd.conf`, uses Debian's `plymouth-set-default-theme` when available, keeps the Ubuntu/Linux Mint `default.plymouth` alternative pointed at the same canonical theme for compatibility, ensures `quiet splash` is present for GRUB systems, and rebuilds initramfs.

## Build the DEB

```bash
bash scripts/build-deb.sh
```

The package is written to `dist/`.

GitHub Actions builds the DEB automatically on every push to `main` and on manual workflow runs. A tag matching `v*` also publishes the built DEB as a GitHub release asset.

## Artwork

`assets/infiltrator-os.png` is the sole Plymouth artwork used by this package. The build verifies its SHA-256 before packaging so an accidental artwork change cannot silently enter a release.


## System Monitor replacement

Version 1.0.2 adds Debian/Linux Mint System Monitor replacement. Installing the Infiltrator OS package schedules a one-shot post-install worker after the active package transaction finishes. The worker queries GitHub for the latest published Infiltrator System Monitor release, downloads that release's native `.run` installer, and executes it with `--profile aggressive --system-package-mode`.

The System Monitor native installer therefore compiles on the target machine with the aggressive profile: `-O3`, target-machine ISA/tuning, LTO and measured two-pass PGO. In OS package mode it replaces an existing `infiltrator-system-monitor` package immediately before installing the newly built local package. After the local installation succeeds, the Infiltrator OS worker removes installed Debian/Mint desktop System Monitor packages such as GNOME, MATE, Xfce, Plasma, LXDE and QPS variants.

No System Monitor release number is embedded in Infiltrator OS; the latest published release is resolved at installation time.
