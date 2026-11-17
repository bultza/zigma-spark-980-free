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
