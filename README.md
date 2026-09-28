# Infiltrator OS

Infiltrator OS is the operating-system project for Infiltrator Projects.

This repository contains the canonical Plymouth boot, reboot and shutdown branding package for Debian and Linux Mint derived installations, plus native provisioning for core Infiltrator desktop applications.

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

## Version 1.0.3 installation experience

Version 1.0.3 removes the transient randomly named `systemd-run` unit used by 1.0.2. The package now starts a detached native C worker directly after the package transaction, keeps a stable lock and status file under `/run/infiltrator-os`, and writes the full worker log to `/var/log/infiltrator-os-system-monitor.log`.

When a graphical desktop session is active, a small native GTK progress window follows that status file and shows the installation stages directly to the user: waiting for the package manager, checking the latest release, downloading, compiling and optimising, cleaning up replaced monitor packages, verifying the aggressive native build, and completion or failure. The System Monitor build still uses the latest published native installer dynamically with `--profile aggressive --system-package-mode`; the user no longer needs to know about or inspect a background service to understand whether the operation is running or has failed.


## Calculator replacement

Version 1.0.4 adds Calculator under the same operating-system provisioning model as System Monitor. After the package transaction finishes, a detached native C worker resolves the latest published Calculator release, downloads its deterministic source bundle, installs the required Debian/Mint C++ and GTK4 build prerequisites, and performs the build on the target machine rather than installing the generic release DEB.

The Calculator build is two-pass and aggressive. The first pass uses `-O3`, `-march=native`, `-mtune=native`, LTO and profile generation. Calculator's own test suite provides the primary PGO workload, and the instrumented GTK application is then exercised briefly under Xvfb to add startup/rendering paths. The second pass rebuilds the Debian package with the collected profile, LTO and CPU-native flags, verifies that those flags survived into CMake's final configuration, and installs that locally built package.

Only after `infiltrator-calculator` and `/usr/bin/infiltrator-calc` are verified does Infiltrator OS purge known distribution calculator packages. The current replacement set is GNOME Calculator, MATE Calculator, KCalc, Galculator, Deepin Calculator and UKUI Calculator.

Calculator status is written to `/run/infiltrator-os/calculator-install.status`, with the worker log at `/var/log/infiltrator-os-calculator.log`. When a graphical session is available, a GTK progress window presents the native-build stages directly to the user.

System Monitor and Calculator retain their own duplicate-install locks, while version 1.0.4 also adds a shared native-build lock. This serialises the two heavyweight local builds so they cannot compete for APT/dpkg locks or saturate the machine simultaneously.
