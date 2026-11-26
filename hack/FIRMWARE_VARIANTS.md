# Firmware variants compared

The [original flash layout table](HISTORY.md#original-flash-layouts) shows the partition offsets and sizes. The same layout can contain different Linux builds, and the same Linux build can run different navigation and app-server binaries.

## Combinations found in flash dumps

All kernels identified here are Linux 4.14.14. The build number is the `#` suffix in the kernel version string. Hashes in the tables are SHA-256 prefixes. The Vendor column identifies the navigation and app-server pair.

| Sample | Vendor | Layout | Kernel build | `ygclient` | `demo_rplidar` | `wifi_app_server_grit` |
| --- | --- | --- | --- | --- | --- | --- |
| Ciccio and Caccamora | Zigma | Direct, five partitions | `#20` | `b95376d58002` | `b10fc7b48d1b` | `4cf0709919f0` |
| Ciccia | Zigma | Overlay, six partitions | `#23` | `ce73b4c04b01` | `b10fc7b48d1b` | `4cf0709919f0` |
| Issue 01a | Puron | Overlay, six partitions | `#23` | `ce73b4c04b01` | `6b7b11091132` | `784fbcda505d` |
| Issue 01b | Zigma | Direct, five partitions | `#16` | `2bc882825a5d` | `b10fc7b48d1b` | `4cf0709919f0` |
| Issue 09 (OKAMI U100 Plus) | RoboJet | Overlay, six partitions | `#23` | `ce73b4c04b01` | `f793e93abb85` | `8e323cfe110e` |
| Issue 03 | Puron | Direct, five partitions | `#20` | `cac314d83760` | `6b7b11091132` | `784fbcda505d` |

| Samples with matching base partitions | U-Boot | DTB | Kernel | SquashFS rootfs |
| --- | --- | --- | --- | --- |
| Ciccio, Caccamora and issue 03 | `2fa15f30a1f5` | `c8189701d5e4` | `b23dbe53462d` | `48345cb4721e` |
| Ciccia and issue 01a | `168160f18ce0` | `0ed7dd1f9470` | `0e912343ba5e` | `84f570dfc1bc` |
| Issue 01b | `f50232bdbcc6` | `c8189701d5e4` | `0c34365f2842` | `4034127dee23` |

- **Direct layout.** The JFFS2 data partition is mounted at `/root` and contains the vendor binaries. Ciccio, Caccamora and issue 03 share the same bootloader, DTB, kernel and SquashFS. Issue 01b shares their DTB and partition offsets. Its bootloader, kernel and SquashFS differ.
- **Overlay layout.** Issue 01a shares all four base partitions with Ciccia. Its active vendor executable paths link into the separate firmware partition. All three files supplied separately for issue 01a match the files extracted from that partition byte for byte.
- **Wi-Fi.** The `#20` and `#23` combinations use `wpa_supplicant` and `wpa_cli` from `/root/bin` and use `/root/lib` in the library search path. Issue 01b uses the tools from `/usr/bin` and `libnl.so.1` from `/usr/lib`. Its `8189fs.ko` also differs from the module in the other samples.
- **Flash protection.** On project hardware, Ciccia had active per-block protection (`WPSEL=1`). Caccamora reported `WPSEL=0` in a read-only register check. These observations apply to the tested chips. The user dumps record flash contents and partition layout.

The 32 MiB user dumps for issues 01a, 01b and 03 matched the hashes recorded for both reads and all 512 block hashes. They were taken after SSH setup. Issue 02 is represented by an installer log. It reported a direct layout and stopped before Full because three blocks differed between reads. The log leaves its firmware combination undetermined.

## Puron binaries and compatibility

Issues 01a and 03 contain the same pair of Puron binaries across two different base firmwares. The original project robots use the other pair in the table. We tested each Puron binary separately on Caccamora.

| Caccamora navigation | Caccamora app-server | Observed result |
| --- | --- | --- |
| Original | Original | Baseline used for the comparison |
| Puron | Original | The process started. The robot behaved erratically, manual and web controls did not respond as expected, and live LiDAR was unavailable. The original navigation binary was restored. |
| Original | Puron | The process started and maps and pose were received. Volume, settings and go-to failed during the test. The observer reported state from the app-server log and lacked the usual freshness fields. |

Those rows describe the earlier trials. On 17 November the owner clarified that Puron navigation works and the remaining navigation limitation is reading live LiDAR. The owner explicitly requested Full installation for the complete Puron pair on the Ciccio/Caccamora base. This owner report supersedes using the earlier Caccamora trial as a reason to block that installation.

The Puron app-server adapters were subsequently updated for settings, status, cleaning statistics and zone display. Go-to from Hibernating was confirmed by the owner. Zone edit/delete was verified with original navigation and the Puron app-server. Zone helper V1.3 also enables the exact Puron navigation hash for user validation, at the owner's request. The existing edit/delete test establishes the original-navigation/Puron-app-server result; operation with both Puron binaries awaits user hardware feedback.

## Installer checks

The [firmware catalogue](../installer/computer/known-firmware.json) supplies the reviewed hashes and Wi-Fi paths. The installer selects a profile using the layout, kernel and SquashFS hashes, then checks the other listed files.

- **Issue 01a** selects `ciccia-original-kernel23`. Its base partitions and `ygclient` match Ciccia, while its navigation/app-server pair matches Puron from issue 03. The complete Puron pair is now explicitly catalogued on this overlay base, permitting Tools and Full after all other required checks pass. Full retains the existing firmware-partition storage and bind mounts; the live LiDAR limitation is reported without downgrading installation.
- **Issue 01b** matches the base for `direct-kernel16`. The profile includes its Wi-Fi paths. Full installation and rollback on that hardware remain to be verified.
- **Issue 03** shares the base for `ciccio-original-kernel20`. Its exact Puron app-server/navigation pair now permits Tools and Full. The tested Puron app-server/original-navigation pair is also accepted on this base. A different readable `ygclient` hash still produces a warning under the existing policy.

Native exceptions are catalogued as complete `native_pairs` inside the matching base profile. They do not extend to another layout or kernel/rootfs combination. All other required component hashes still have to match, and an unknown or unreadable native pair retains the SSH-only fallback. The installer reports the known live LiDAR limitation for Puron navigation without blocking Full. Installation does not replace either native executable or distribute vendor binaries.

The [service's executable checks](../installer/robot/src/zigma-free-service) already accept both navigation hashes and both app-server hashes. The installer checks its own catalogue. Full installation and rollback on a user robot remain hardware checks; recognizing hashes does not prove every optional adapter on that combination. Live LiDAR reading from Puron navigation remains unsupported.

## Issue 10: Ciccio-Issue10

The 32 MiB backup matches both recorded whole-image hashes and all 512 block
hashes. Its five-partition direct layout and U-Boot, DTB, kernel (#16), and
SquashFS hashes match Issue 01b. The native Zigma pair and cloud/Wi-Fi binaries
also match the `direct-kernel16` catalogue. Of its 20 checks, only `S70yg` differs:
Issue 10 uses the existing Ciccio script (`1c7b3f64b486...`) rather than Issue
01b's `d700033a380b...`. This identifies a firmware combination, not different
physical hardware.

The `ciccio-issue10-kernel16` component profile permits Tools and Full. Selection
uses the exact app-server, navigation and S70yg hashes inside the kernel16 base;
all 20 component checks remain strict, including ygclient. The original Issue
01b profile and other bases retain their existing policies. No vendor file is
replaced by this catalogue change. Offline installer regression tests cover the
new combination, missing/unknown components and unchanged prior decisions.
Installation, cold boot and operation on the owner's hardware remain unverified.
The private original is archived as `originales/Zigma-Spark/Ciccio-Issue10`;
Ciccio denotes its direct-layout family. The support ZIP remains unchanged.

## RoboJet X-One Issue08 feature support

See [RoboJet Issue08 support and limitations](ROBOJET_ISSUE08_SUPPORT.md) for the
current map, position, geometry-control and statistics capabilities, their
limitations and the distinction between follow-up changes and the RC15 archives.


## Timezone check for every new variant

Review login profiles, startup scripts, explicit `TZ` assignments and the
`/etc/localtime` / `/root/etc/localtime` configuration for each new firmware.
During authorized on-device testing, compare UTC, interactive SSH time, the web
selection, cron's timezone, native process timezone and actual log timestamps.
Record evidence per variant; offline review alone does not confirm runtime
behavior. After a startup correction, verify the next authorized boot as well.
Do not infer timezone behavior from a model name, flash layout or one binary hash.

Distinguish the selected zone from the zone retained by existing processes and
from the zone used to parse historical logs. Do not adjust UTC to compensate for
a timezone mismatch. RoboJet's mismatch was confirmed; the checked Ciccio and
Caccamora installations were consistent. Other variants remain unverified.

### Issue10 adapted for Ciccia (RC17)

`ciccia-issue10-kernel23` enables Tools/Full for the prepared Issue10 adaptation.
Its 20 component hashes were verified against the adapted NOR. It retains the
Issue10 native pair and S70yg script, with Ciccia DTB, kernel #23 and Wi-Fi module.
Selection uses the native pair plus S70yg within the existing Ciccia direct base;
all components are checked strictly. The original `ciccio-issue10-kernel16` and
both RoboJet profiles remain supported. Physical installation/cold boot validation
is separate from offline recognition. No firmware files are replaced by detection.

## Issue 09: OKAMI U100 Plus

RC18 includes `okami-u100-plus-issue09-kernel23` for the received original firmware on the Ciccia kernel23 overlay base. Twenty required component checks include the exact RoboJet pair and original client/driver/startup scripts. The reviewed navigation template preserves numeric device calibration and also covers the Ciccia adaptation. Full installation and native v2 map persistence after reboot passed on Ciccia; original OKAMI hardware and physical cleaning remain unvalidated. See [profile and validation](OKAMI_ISSUE09_SUPPORT.md).

## Issue 12: ABIR X8

`abir-x8-issue12-kernel20` is a strict component profile on the Ciccio
direct kernel20 base. It uses the existing RoboJet/OKAMI app/navigation
adapters with `ygclient` ce73b4c0, daemon 4e14e540, and the supplied ABIR
startup scripts and Wi-Fi components. It permits Tools/Full when all
required component hashes match. Numeric calibration exports in S51hoslam
may differ under the reviewed template; scripts and calibration are not replaced.

Evidence comes from Forfoxy's sanitized package in [issue 12](https://github.com/bultza/zigma-spark-980-free/issues/12),
whose base partitions match Ciccio byte for byte. The writable partition was
erased in the supplied NOR and reconstructed privately from its separate
filesystem archive, with authorized donor/default files. Those donor factory
files and synthetic identity are not catalogue requirements or distributed
installer payloads. The profile hashes were checked against the reconstructed
files and supplied base; ygclient was recovered by its declared exact hash.

Bounded boot and native pairing AP/DHCP/HTTP passed in the adapted QEMU lab
(Ciccia kernel23, adapted DTB, hwsim and recorded MCU events). This does not
validate the original kernel20, physical ABIR, Full activation or upgrade.
Forfoxy's RC10 modifications, including pose/LiDAR and queue handling, are not
imported by this catalogue addition. Existing map-only navigation limitations
remain. Offline/local serial timezone is Asia/Shanghai; effective product
scheduling after installation remains untested.
