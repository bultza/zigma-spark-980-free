# Spark 980 firmware 4.5.56 support

Profile: `spark980-sdk4556-overlay20` (layout `overlay`)

## Robot

- `fwversion` 4.5.56, Linux 4.14.14 #34 (Aug 18 2021), Buildroot 2017.08
- Wi-Fi: RTL8723DS (SDIO `024c:d723`, module `8723ds`)
- Source: verified NOR dump `ZigmaSpark980-20261128-005311/nor-backup.bin`
  (SHA-256 `f67cf620a642bd27a75a5febecfe0b18c9d74e4c23c608cb3a06446e5f052a4b`, both read passes identical)

## NOR layout

| mtd | name     | size       | original Ciccia | content |
|-----|----------|------------|-----------------|---------|
| 0   | uboot    | `0x050000` | same            | |
| 1   | dtb      | `0x010000` | same            | FDT |
| 2   | kernel   | `0x300000` | same            | zImage |
| 3   | rootfs   | `0x400000` | `0x800000`      | squashfs 4.0, zlib |
| 4   | data     | `0x4a0000` | same            | JFFS2, overlay upper |
| 5   | firmware | `0x1400000`| `0x1000000`     | JFFS2, mounted ro at `/root/firmware/arobot` |

Boot (`/init-overlay.sh`) uses the same scheme as the original Ciccia: mtdblock4 is
the overlayfs upper over the squashfs root, and mtdblock5 is mounted at
`/root/firmware/arobot`. Only the rootfs and firmware sizes differ. About 12.3 MiB
of the 20 MiB firmware partition is used (uncompressed estimate). The full stack
needs about 3.4 MiB plus a 2 MiB reserve.

## Comparison with issue #6 (`issue04-russian-original`)

These match exactly: app server `8084000b…`, navigation `1b2751ef…`, `ygclient`
`2bc88282…`, `8723ds.ko` `7458199f…`, `wpa_supplicant` `45b23ee8…`, dropbear,
busybox, `S70user`, `inittab`, `hold380`, `S35prepare`, `S60wifi`.

| path | 4.5.56 | issue04-russian-original |
|------|--------|--------------------------|
| `/dev/mtd1` | `652c1466…` | `0ed7dd1f…` |
| `/dev/mtd2` | `8b7e32c3…` | `0e912343…` |
| `/dev/mtd3` | `b3e2dc6a…` | `84f570df…` |
| `/root/app_server/daemon` | `2f542c25…` | `a0a587b6…` |
| `/root/lib/libnl.so.1` | `5286469b…` | `9ab8d584…` |
| `/root/etc/init.d/S50app` | `756e79e9…` | `07c8aa36…` |
| `/root/etc/init.d/S51hoslam` | `c6c43421…` (template `ae34c42e…`) | `7805c5c4…` (template `fd7bca14…`) |
| `/root/etc/init.d/S70yg` | `f35a6e78…` | `4763d5eb…` |
| `/root/etc/init.d/S99daemon` | `de2d94a3…` | `09569de8…` |

The overlay copies of `S35prepare`, `S51hoslam` and `S60wifi` were written by the
vendor OTA. Earlier Zigma transactions changed only `authorized_keys` and
`S98dropbear`.

## Changes

- `installer/computer/known-firmware.json`: adds the new base profile with all 21
  files pinned to the hashes from the dump. Existing profiles are restored from
  `main` (the research commit had overwritten the Ciccia profiles in place).
- `installer/computer/installer.py`: base profiles may set
  `navigation_calibration_template_sha256`, so `S51hoslam` is accepted when only
  its numeric calibration differs.
- `installer/robot/install.sh.in`, `installer/computer/upgrade.py`: detect the
  4 MiB rootfs / 20 MiB firmware geometry as `overlay`.
- `installer/robot/storage.sh`: accepts rootfs/firmware sizes of 8+16 MiB or
  4+20 MiB. It previously required a 16 MiB firmware partition, which would have
  failed a full install on this robot.
- `src/robot/src/zigma_nor_backup.c`: accepts both overlay geometries (the
  research commit had replaced the original one).
- `installer/tests/test_firmware_checks.py`: tests for the new profile and for
  base calibration-template validation.
- `installer/BUILD.md`: profile notes.

## Verification

- Installer suite: 150 tests pass (run with `installer/computer/.wizard-venv`).
- The generated firmware gate, run against the real files from the dump, passes
  all 21 checks and selects `spark980-sdk4556-overlay20` for ssh, tools and full.
- Changing only the calibration numbers in `S51hoslam` still passes. Changing
  any other setting falls back to SSH-only.
- A host build of the NOR reader (`-DNOR_TEST`) streams the dump partitions and
  reproduces SHA-256 `f67cf620…`. The original overlay geometry is still
  accepted, and other sizes are rejected.
- Layout detection in `install.sh.in` and `upgrade.py` returns `overlay` for
  this robot's `/proc/mtd`.

## Open items

- Rebuild the ARM binaries so the NOR reader change is included. The installer
  and catalog changes need no rebuild.
- If `/root/.zigma-install/pending` is still on the robot, the installer will
  refuse to run until it is removed. The dump was taken while an SSH install was
  running, and that install is recorded as completed.
- Upgrades do not replace `S02zigma-storage`, so the storage change applies to
  new installs only.
- A full install and physical cleaning have not been tested on this robot yet.
