# Zigma Free source and build

The robot programs are in [robot/src/](robot/src/). The browser interface is in [web/](web/) and uses plain HTML, CSS, and JavaScript. The shell scripts that run on the robot are in [installer/robot/](../installer/robot/).

These sources build the current development version. 

Please consider that this part of the development has been 100% vibecoding with the help of ChatGPT :-D.

## Install the build tools

On an x86-64 Debian or Ubuntu build computer, run this from the repository root:

```sh
sh src/install-build-deps.sh
```

The script installs Make, the host build tools, Python 3, curl, CA certificates, and archive utilities. On another x86-64 Linux distribution, install the equivalent packages.

The first build downloads the pinned Bootlin ARMv5 EABI musl SDK, verifies its SHA-256, and extracts it under `src/robot/build/toolchain/`. The SDK builds statically linked, 32-bit, soft-float ARM Linux executables. This toolchain is also used for the Telegram transport.

## Compile the robot programs

Build the main ARM binaries together:

```sh
make -C src/robot -j4
```

For an individual component, run `make -C src/robot TARGET` using a target from the table. Each target writes a file of the same name under `src/robot/build/musl/`.

| Target | What it builds |
| --- | --- |
| `free-zigma-agent-arm` | Web server, API, scheduler, Telegram integration, and integrated NOR reader. |
| `zigma-core-arm` | Local firmware IPC bridge and state cache. |
| `zigma-observe-arm` | Status, map, pose, and LiDAR observer. |
| `zigma-network-arm` | Wi-Fi manager and local pairing portal. |
| `zigmactl` | Command-line control utility for the robot. |
| `zigma-area-arm` | Rectangular cleaning helper. |
| `zigma-zones-arm` | No-go zone helper. |
| `zigma-maps-arm` | Saved-map management helper. |
| `zigma-options-arm` | Native settings helper. |
| `zigma-nor-backup-arm` | Optional standalone flash reader. The deployment uses the integrated reader in the agent. |

The version header is generated from `src/robot/versions.json`. The browser assets are copied directly into the deployment archive.

## No-go zone adapter profiles

`zigma-zones-arm` V1.3 verifies the executable identity and SHA-256 of the app-server queue receiver. The original `4cf07099…` profile retains its type 1 JSON command. The Puron `784fbcda…` profile accepts either the original `b10fc7b4…` or the known Puron `6b7b1109…` navigation receiver and sends classified quadrilaterals directly on the navigation queue: type 6 applies the complete set; one type 16 request reads the resulting configuration back through the native app-server. Unknown identities are rejected. Editing with Puron navigation is enabled at the owner's request for user validation; the physical edit/delete test so far used original navigation with the Puron app-server.

This avoids Puron's incompatible JSON parser and its truncated legacy zone-cache allocation. Coordinates, restrictions, backups, idle/error checks, unchanged-map checks and confirmation remain part of the existing web/API flow. No periodic map requests are enabled. `--dry-run` validates and prints the canonical input JSON without accessing IPC; it does not exercise a running firmware profile.

Run the offline command and observer regressions with:

```sh
make -C src/robot test-zone-commands test-puron-zones
```

## Build the Telegram HTTPS transport

```sh
make -C src/robot transport
```

This downloads the pinned curl and Mbed TLS source archives, checks their SHA-256 hashes, and cross-compiles a static `zigma-curl-arm`. It also downloads and verifies the CA bundle and matching Mozilla source, then collects the upstream licence notices. The inputs and results are cached under `src/robot/build/musl/transport/`. This step needs internet access on the build computer.

## Package a deployment

After building the main binaries and transport, run:

```sh
python3 src/package-release.py --label dev
```

The packager assembles the computer and robot scripts, compiled musl ARM binaries, browser assets, version catalog, and dependency notices. The archives include the [third-party notices](robot/vendor/notices/README.md), the upstream musl source archive, and the matching Mozilla CA source. It checks that the installer can build a full payload, then writes a ZIP and a TAR.GZ to `dist/`, each with a matching `.sha256` file. Use `--output-dir` for a different destination. Choose a new label if the output files already exist.

Extract the whole archive before running it. Open `install-from-windows.cmd` on Windows or run `sh install-from-linux.sh` on Linux from the top-level folder. The archive contains its own installation guide and file checksum manifest. For the installation flow and file layout, see [installer/BUILD.md](../installer/BUILD.md).

## Historical V1.0 packages

The historical V1.0 binaries were built against glibc. The build and packaging steps above produce new musl-based archives. Use a new release label for them.

For existing installations, use `upgrade-from-windows.cmd` or `sh upgrade-from-linux.sh`. See the [upgrade guide](../installer/README.md#upgrade-an-existing-installation).

## Issue04 app-server runtime profile

The exact `8084000b…` app-server has a separate `zigma-issue04` profile in
`robot/src/zigma_app_profile.h`. The observer hashes the running executable
and selects status, initialization, volume and zone-cache addresses independently
of the navigation profile. Executable identity changes invalidate the hash cache.
Unknown app-server images do not fall through to the original Zigma addresses.

This variant uses the Zigma-family 100-byte status block and classified-zone
JSON route. Maps additionally verify the profile-specific getter fingerprint.
Saved-map restoration is enabled for this exact profile after a native round-trip
with the current map: success acknowledgment, fresh map publication and identical
restored bytes were verified. Switching to a different map and physical
relocalization have not been exercised. On Ciccia with
original navigation, manual deployment verified local IPC, status/settings,
saved-map listing and the existing navigation LiDAR reader. Physical commands,
zone edits and cold boot remain unvalidated. RC12 catalogs only the tested Ciccia adaptation with original navigation;
the original Issue04 navigation remains SSH-only.

Run `make -C src/robot test-app-profiles test-puron-settings test-puron-zones
test-zone-commands test-map-formats test-map-native test-navigation-profiles`
from the repository root (as one command) for the relevant offline checks.

### Issue04 original navigation reader

Observer V1.17 selects the passive navigation layout by the exact running executable hash. Russian navigation `1b2751ef…` uses singleton slot `0x4687a4`, object `0x47e440`, vtable `0x45f544`, and pose block `+0x112c`; Zigma/Puron retain `+0x1120`. Scan and map geometry offsets remain `+4` and `+0xe44`. The Russian layout is statically traced and covered by synthetic regression fixtures; runtime validation on that navigation build is pending. The subsequent Issue04 installer update enables this exact original pair with its RTL8723DS module and reviewed scripts, alongside the Ciccia hybrid. Zones V1.6 allows its reviewed type16/type17 configuration query. Physical validation of the original robot remains pending.


### RoboJet Issue08: reduced navigation and map position fallback

For the current feature inventory, see [RoboJet Issue08 support and limitations](../hack/ROBOJET_ISSUE08_SUPPORT.md).

Observer V1.18 recognizes navigation SHA-256
`f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c`
as `robojet-issue08-map-only`. Its passive pose and LiDAR readers are disabled:
no `/proc/PID/mem` is opened by these readers for this profile. The profile has
no borrowed addresses. Existing Zigma, Puron and Issue04 RAM readers remain.

`zigmactl status --json` (V1.13) and the web coordinates/marker select a valid
navigation cache first, then a reported map position, a recent path estimate
(up to six seconds), or the last recorded path point. Both upstream sources
remain independent. A map point has no inferred heading or resolution;
`position.source`, `quality`, `age_s` and nullable `heading` describe the result.
`available: true` means a usable coordinate, not a current physical fix.
`fresh` remains false and the measurement timestamp remains unknown. Recorded
points can remain available while stationary or after updates stop, explicitly
labelled recorded; missing or invalid points return unavailable. Map identity,
geometry, bounds, reset and relocation checks prevent unrelated overlays.

Map fallback is for display/status only. Blocking `goto` refuses to dispatch
when the observer explicitly reports no navigation RAM support; recorded path
points are never substituted for arrival confirmation. Nonblocking command
semantics are unchanged.

The initial V1.18 change provided reduced **navigation** support only.
Subsequent app, map and installation support is described below; the linked
feature inventory records the current implementation. V1.13 avoids reusing the
V1.10–V1.12 identifiers reserved by the separate manual-drive branch.

Offline checks: `make -C src/robot test-navigation-profiles test-navigation`,
`python3 -m unittest installer.tests.test_ctl_status`, and
`node installer/tests/test_position_selection.js`.


### RoboJet Issue08 app cache adapter

Observer V1.19 recognizes app-server SHA-256
`8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c`.
It reads exactly 92 bytes at 0x4395c, guarded by initialization at 0x43e08,
and volume at 0x43a7e. State, battery, error, continue-clean and obstacle
crossing use the statically verified offsets. Both reads must agree; receipt
is not physical freshness. Observer V1.22 enables area/time using the existing
JSON contract: `clean_area_m2 = vendor_clean_area_raw` and
`clean_time_s = vendor_clean_time_raw * 60`. Zero is a valid available value.
The binary publishes status offsets +0x24 and +0x28 unchanged as `CleanArea`
and `CleanTime`, respectively. This establishes field correspondence, not
independent proof of physical units. The RAM adapter reports
`statistics_units_source=robojet_json_contract_provisional`; raw counters remain available for validation.
The web UI displays area in m² (or ft² by preference) and time in minutes.
Independent core JSON telemetry retains its existing conversion.

The zone adapter snapshots both counted arrays: virtual walls
at pointer slot 0x43e24 with 16-byte records, and forbidden polygons at 0x43e28
with 32-byte records. Both entire snapshots and pointers must agree; malformed
counts, invalid coordinates, missing pointers and incomplete reads publish no
partial geometry. A zero count is an empty array; a null pointer is unavailable.
The output uses `kind: virtual_wall` (two points) or `forbidden_polygon`
(four points). Walls retain `read_only: true` and null restriction classes.
With observer V1.20, forbidden polygons expose both cleaning flags as false:
this legacy format has one exclusion type, not separate vacuum/mop classes.
Agent V1.30 and zones V1.7 support editing these polygons while preserving walls.

This integrates with the existing map-position fallback and map-only navigation
profile. RoboJet app-cache readings, map output and polygon configuration
readback are checked on Ciccia. Supported writes are limited to the reviewed
command adapters; the map decoder does not enable native map import. Existing firmware profiles and manual-drive branch are unchanged.

Validation: `make -C src/robot test-robojet test-app-profiles test-puron-settings
test-puron-zones test-map-formats test-navigation-profiles test-zone-commands`,
plus the production JS selector/wall-render tests. Fixtures are synthetic;
original firmware and disassembly remain private.


### RC15 installation trial

The source-only installer/service restrictions described above are superseded
by RC15 for the exact Ciccia kernel23 + RoboJet Issue08 component profile.
Full is enabled for the owner's installation trial, retaining the reduced
navigation limitations and the initial read-only geometry adapter. Subsequent
geometry editing and map support are documented in the current feature inventory. Physical compatibility is not
claimed by this opt-in release; see installer/BUILD.md.

### Saved-map decoder V1.13

`zigma-maps-arm` reads the RoboJet `relo` version 2 framing as well as the
existing version 6/7 `GRID` format. Version 2 uses length-prefixed zone config,
16-byte metadata, a Grid2D protobuf and relocation data. Its packed (or unpacked)
cell field contains two consecutive matrices: correspondence costs and auxiliary
bytes. The decoder validates the full-file checksum, section bounds, geometry,
varints, cell counts and value ranges before exposing a preview. Unknown fields
with supported protobuf wire types are skipped; TSDF and other framing variants
remain unsupported. Full-grid coordinates and the existing preview orientation
are retained.

Master copies preserve the original bytes and can be previewed. Maps V1.17 and
agent V1.35 enable native v2 restoration for the exact RoboJet pair, with current-ID
and idle checks, backups, checksum validation and native/map-publication
confirmation. A same-map restore with three no-go polygons was verified on Ciccia;
a cross-ID import also succeeded. The converted map retained its file and ID
after reboot but did not republish its live grid, so full reload remains unvalidated. See
[native import and validation](../hack/ROBOJET_MAP_IMPORT.md). No native IPC is
sent by catalog or preview operations. Synthetic regression tests: `make -C src/robot test-map-formats
test-map-native`. No private maps are included in the repository.

### RoboJet geometry controls (agent V1.30, observer V1.20)

Area V1.2 uses the reviewed `PlanningRect` JSON protocol and checks the native
receiver image; RoboJet also requires its exact navigation image. Zones V1.7
converts full polygon snapshots into the legacy flat `forbidden_zone_x/y` arrays.
The native app performs Y inversion, sends 32-byte quads and requests a one-shot
configuration readback. The helper never sends classified 36-byte records to
RoboJet and refuses vacuum-only or mop-only restrictions. Wall keys are omitted
to preserve existing virtual walls; their edit/delete controls remain disabled.

Both tools work on the live map and on Detailed view when its saved map identity
and coordinate transform agree with the current map. Drawing uses live grid
coordinates, including when the detailed preview is downsampled. Frame changes,
reset and relocation block editing. RoboJet's idle Hibernating state is accepted
with confirmed local communication and no error. Other profiles retain their
existing state guards. Area dispatch can start motion; it is only sent after the
user chooses Clean this area. Zone changes retain backups, base-snapshot checks
and feedback confirmation. Up to twelve forbidden polygons are supported.

Installed on Ciccia with API/capability/frame checks and unchanged saved map.
No physical cleaning or zone changes were performed during deployment. Tests:
`make -C src/robot test-robojet test-zone-commands test-navigation test-map-formats`,
`node installer/tests/test_geometry_controls.js` and
`node installer/tests/test_position_selection.js`. RC15 release archives are unchanged.

RoboJet startup follow-up: zones V1.8 and service V1.10 request configuration
once per native process identity, including after cold boot. Observer V1.21
distinguishes unavailable zones from a confirmed empty list when deciding
whether to publish the cache. Null native pointers still mean unavailable;
they are never silently treated as an empty zone set.
