# OKAMI U100 Plus Issue09: support and validation

The firmware reported in [issue 09](https://github.com/bultza/zigma-spark-980-free/issues/9)
is supported by `okami-u100-plus-issue09-kernel23` in RC18. Recognition uses the
received original firmware components, not the model name or a test-only image.

## Firmware profile

- Linux 4.14.14 #23, six-partition overlay; base DTB/kernel/rootfs match original Ciccia.
- Original client and RTL8189FS driver, with Wi-Fi tools in `/root/bin` and libraries in `/root/lib`.
- Exact app SHA256 `8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c`.
- Exact navigation SHA256 `f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c`.
- Twenty required component checks; the reviewed S51hoslam template preserves numeric device calibration. Other startup edits, missing components and mixed native pairs retain the SSH-only fallback.

The app/navigation pair is identical to the reviewed RoboJet Issue08 pair, so its
existing adapters apply: `relo` v2 maps, map-derived position, settings, area
commands and general no-go polygons. Live LiDAR and navigation-memory pose remain
unavailable. See the [RoboJet feature matrix](ROBOJET_ISSUE08_SUPPORT.md).
Vendor executables, calibration, voices and credentials are not distributed in RC18.

## Verified on Ciccia, 24 November 2026

The reviewed adaptation preserves the received native executables and uses
Ciccia's calibration. Full installation passed all 20 firmware checks; 450
installed stack/tool hashes were verified. Authenticated web, local-mode
activation and SSH passed. A native RoboJet MAP_11 import returned success and
published the map and three no-go polygons. After a coordinated reboot, the same
map file, active ID, live map and all three polygons were present; SSH and web
returned with Standby/NoError. No cleaning or movement was requested.

The received original contained no saved MAP_* files. This import used a separate
private RoboJet map; it is not included in the deployment.

Timezone was audited separately: the original uses Asia/Shanghai; after the web
selection changed to Europe/Amsterdam, reboot applied the selected localtime to
native app, navigation and cron. UTC and local time were checked independently.

Original OKAMI hardware installation, physical localization and cleaning still
require owner validation. Reboot success on Ciccia does not establish those tests.
Caccamora v7 conversion remains experimental and is not enabled by this profile.
