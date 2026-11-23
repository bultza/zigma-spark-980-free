# RoboJet X-One Issue08: support and limitations

Updated: 23 November 2026.

This document describes Zigma Free's software support for the RoboJet X-One
Issue08 firmware variant. Hardware compatibility of vendor binaries is outside
this feature inventory.

## Implemented features

- Saved `relo` version 2 maps: catalog, checksum validation, occupancy decoding
  and previews with the correct grid coordinates and orientation.
- Byte-preserving protected map copies and their previews.
- Native restoration of a protected v2 map for the exact RoboJet app/navigation
  pair, with backups, checksum validation and native/map-publication confirmation.
  A same-map restore on Ciccia preserved all three no-go polygons.
- Robot coordinates from the available map report, recent path estimate or last
  recorded path point. The UI and status output identify the source and quality.
- App-server cache readings for state, battery, error and supported settings.
- Area and time statistics: raw area is treated as square metres and raw time
  as minutes (multiplied by 60 for API seconds). The web UI displays m² or ft²
  and minutes; raw counters remain available for validation.
- Rectangular area selection and the native `PlanningRect` command adapter.
- General no-go polygon creation, editing and deletion, with native response and
  configuration readback. Existing virtual walls are displayed and preserved.
- Geometry controls on the live map and on Detailed view when the saved map
  matches the current map identity and coordinate frame.
- One configuration-read request per native process identity to initialize the
  zone cache, including after boot. A confirmed empty list is distinguished from
  an unavailable cache.

## Unavailable or restricted features

| Feature | Limitation | Reason |
|---|---|---|
| Live LiDAR scan in Zigma Free | Unavailable for this navigation profile. | The scan buffers and their memory layout have not been identified and validated for this executable. Known offsets from other variants are deliberately not reused. |
| Navigation-memory pose and heading | Unavailable. Map-derived coordinates remain available where supplied. | This hash has no validated navigation RAM layout. Map coordinates do not establish a current physical fix; missing heading stays unknown. |
| Blocking Go to with arrival confirmation | Rejected before dispatch when the observer reports no navigation RAM support. | Recorded or estimated map points cannot safely confirm arrival. Nonblocking command semantics are unchanged. |
| Separate no-vacuum and no-mop restrictions | Unavailable; only general exclusion polygons are supported. | The legacy format stores four coordinate pairs without a restriction-class field. |
| Virtual-wall editing | Walls can be viewed and preserved, but not created, edited or deleted through this variant's editor. | The implemented writer handles forbidden polygons. It omits wall fields to preserve the existing wall configuration. |
| Reliable restore after standby / complete reload after reboot | Not fully validated. Same-map and cross-ID imports have succeeded. | Native requests sometimes remained queued; the converted test map retained its file and selected ID after reboot without republishing its live grid. |
| Native saved-map deletion and clear/reset | Enabled in Maps V1.18; physical outcome and reboot persistence remain untested. | Uses the reviewed navigation type-27 ID operation; queue delivery alone does not confirm completion. |
| Physical-unit validation for area/time | Statistics are enabled; nonzero physical measurements have not independently verified the scale. | The executable publishes these RAM fields unchanged as `CleanArea` and `CleanTime`. We apply the existing JSON contract (m² and minutes), but a nonzero real-world sample is still needed to validate the scale. |
| Editing an arbitrary saved preview | Unavailable. Detailed-view editing requires the current map identity and a supported coordinate transform. | Sending geometry against an unrelated saved frame could target the wrong location. Reset and relocation also block editing. |

The polygon editor supports up to twelve forbidden polygons. Unknown zone
configuration, missing coordinates, unsupported restriction types and changed
map/base snapshots are rejected rather than guessed. Command dispatch and
configuration readback are distinct from physical navigation validation.

See [native deletion/reset protocol](ROBOJET_MAP_DELETE.md). Maps V1.18 is a source/build update after RC17; existing release archives have not been rebuilt.

## Installer profiles

RC17 includes both `robojet-issue08-kernel16` (original Issue08) and
`ciccia-robojet-issue08-kernel23` (adapted for Ciccia). The original profile's
23 hashes were derived from the received NOR. DTB, kernel, Wi-Fi module and
navigation calibration script differ between the two profiles. Installation
preserves the device's files and calibrations; profile recognition does not
replace them. Original-hardware installation and cold boot remain unverified.

## Version scope

The current implementation uses agent V1.35, observer V1.22, maps V1.18,
area V1.2, zones V1.8, service V1.10 and zigmactl V1.15, with the updated web UI.
The RoboJet command adapters require the reviewed native executable identities;
the relevant paired operations also verify the matching navigation executable.

The changes through Maps V1.17 were installed on Ciccia and are included in RC16.
Maps V1.18 deletion/reset has not been deployed or physically tested.
Existing RC15 distribution archives have not been rebuilt.

Implementation details and regression commands are in [src/README.md](../src/README.md).

Restoration validation covers the same saved map (ID 11), its grid and three
no-go polygons, plus a converted Caccamora map with a different ID (22). Both
native imports returned success and published the expected maps. The converted
file and selected ID survived reboot, but its live map was not republished.
Native requests also remained queued during standby-related tests. Reliable
post-boot reload, physical localization and restoration after standby remain
unvalidated. The experimental converter is not part of RC16. See
[restore evidence](ROBOJET_MAP_IMPORT.md).
