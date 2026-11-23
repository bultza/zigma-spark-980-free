# RoboJet Issue08: native v2 map import and activation

Static analysis and authorized on-device validation, 23 November 2026.
The protocol analysis below was followed by the same-map restore described at
the end of this document.

Exact executable identities:

- app: `8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c`
- navigation: `f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c`

## App entry and archive contract

JSON dispatcher entry 0x25aac/0x25ab0 maps command 9 to Thumb handler 0x1c67c.
Both `HisMapId` and `HisFilePath` must be JSON strings. The ID is converted to
an integer. Omitting both fields requests the current map instead (navigation
type 0x16); supplying only an ID is not a proven activate-only shortcut.

At 0x1c6fe–0x1c714 the app executes, in order:

```
mv -f <HisFilePath> /tmp/tmp.tar.gz
gzip -d /tmp/tmp.tar.gz
tar -xf /tmp/tmp.tar -C /tmp
rm -f /tmp/tmp.tar
```

It then requires `/tmp/hoslam_map` to be readable and sends navigation message
0x18 via `send_msg_to_hoslam_app` (0x19480), with requested ID and that path.
The archive is consumed/moved, not retained. The existing generated single-file
tar.gz container is the right contract: its sole member is `hoslam_map`, holding
the complete original v2 file, without conversion to v6/v7 or a preview raster.
The path buffer is 128 bytes; use a short generated path with no shell syntax.
The app does not use each shell command's return value to decide success, so
exclusive empty temporary workspaces and subsequent native-result checks matter.

## Navigation dispatch, validation and activation

`app_ctrl_msg_receive_func` (0x27a718) dispatches type 0x18 at 0x27a852.
It tests `StateControl::OnStandby()` (0x28537c), otherwise returning result 5.
That predicate accepts its internal states 1, 2 and 4, but the receiver also
has a state-4 transition before dispatch. Do not equate those internal values
with app-cache state names or broaden UI idle guards without separate validation.

The accepted request calls `Relocation::setReEmployMap` (0x296078), storing ID
at +0xac8, filename string at +0xacc, and pending flag +0xac4. It signals worker
operation 14. The worker calls `reEmployMap` (0x29e4a8), which consumes the flag
and invokes `reEmploy` (0x29e1dc).

The actual import sequence is:

1. Require a nonzero requested ID.
2. Parse the extracted file with `MapFileInput::parse` (0x376610).
3. Require its embedded ID to equal the requested ID (0x29e25e–0x29e26c).
4. Copy it with `AmCopyFile` (0x2488e0) to `./MAP_<id>` (0x29e2b8).
   The native launcher runs navigation in `/root/hoslam`.
5. Invoke `MultipleMap::employ(id)` (0x37b8c0; call 0x29e390); its selected-ID
   store is at 0x37bdb4. Invoke `saveMapsFile` through 0x296278, which writes
   the `./maps` index through 0x37a4e8. Publish the current map ID via 0x296168.
6. Clear previous relocation info and call `loadFromMapFile` (0x298fa4), loading
   the decoded grid and saved restriction arrays (16-byte walls and 32-byte
   forbidden polygons), then update the room data path.
7. On success, notify reset/map refresh, invoke `SendBackupMapToMapWriter`
   (vtable +0xc0 -> 0x279f70), send saved restrictions to the app (0x27629c),
   and report the result. No second user-level activation command is needed
   on this route: import also selects and loads the map.

The destination MAP file can overwrite a map with the same ID. Restriction
content belongs to the imported map: do not promise to preserve newer current
no-go edits unless they were included in the saved copy. Back up current map,
colliding target and index/configuration before a physical test.

## Version 2 integrity

The parser branches on the version word at +4. Versions greater than 1 use
`AmCalcCheckSum` (0x247c34): it saves and clears the word at +0x98, then compares
the sum of file bytes. Version 2 follows that path and the existing v2 layout:
header ID at +0x14, data blocks beginning with a length at +0x9c, including the
protobuf grid and saved restrictions. This does not justify accepting arbitrary
future versions or malformed lengths. Keep the adapter's bounded v2 decoder.

The archival MAP_11 sample is version 2, embedded ID 11, and passes this checksum.
The verification script also confirms a byte-preserving single-member tar.gz
round trip. That is host validation, not execution of the native importer.

## Completion and errors

Navigation reports message 0x19 via 0x25eff4. App handler 0x2039c logs
`send set map result: %d` and emits `SetHistMap: 1` only for result 0;
nonzero results produce `SetHistMap: 0`. On success the app also requests
navigation updates 0x29 and 0x2b.

| Result | Observed code branch |
|---|---|
| 0 | Completed import path |
| 1 | Requested ID is zero (also wrapper default before an import runs) |
| 2 | Embedded ID differs from requested ID |
| 3 | Native file parsing/validation failed |
| 4 | Copy to MAP_<id> failed |
| 5 | Receiver's standby predicate rejects the request |

An accepted local queue send is not completion. In addition, `reEmploy` does
not propagate every later selection/index-save return value as a new error.
Verify a new result log, active ID, actual MAP/index content and map/restriction
readback; result 0 alone does not prove durable activation or physical relocation.

## Implemented adapter and on-device validation

Maps V1.17 and agent V1.35 implement the exact RoboJet pair, a separate 92-byte
app-state layout, the navigation MultipleMap initialized singleton/current ID,
v2 checksum/format checks and a single native import command. The current-ID
reader verifies the unique live navigation executable, its SHA256 and code
fingerprints; a stale queue receiver thread after native respawn is not reused.
Maps V1.18 additionally enables [native deletion/reset](ROBOJET_MAP_DELETE.md)
after offline protocol review; physical testing remains pending. Other map formats
are not enabled for this profile.

Confirmation requires native success, matching active ID and a map publication
at or after the result timestamp. Native log timestamps use the producer's TZ.
The map may arrive before the next result poll; polling time is not treated as
the import time. Completed-import cleanup removes only a byte-identical
`hoslam_map` extraction matching the protected copy, while idle and with no
remaining vendor tar/archive in flight. Unknown workspace contents are retained.

On Ciccia, one authorized same-map restoration was sent after backing up MAP_11,
the saved-map index, live map JSON and native wall/polygon arrays. A protected
master copy and the automatic pre-restore backup were also retained. The native
result was 0; ID 11 became active and the map plus all three no-go polygons were
republished. MAP_11 remained byte-identical (244110 bytes); the index was rewritten.
JSON geometry and raw native polygon arrays matched the pre-test copies exactly.
The web subsequently confirmed `new_map_observed`, with `reset_pending: false`.
No second import was sent while correcting confirmation handling.

Navigation later changed PID within the same boot; this was observed, not
requested by the test. The cause was not established. The active map was still
11 after the respawn. This is not a claim of reboot persistence or physical
localization. Switching to a different compatible map ID and persistence across
the next authorized robot boot still require separate validation.

Regression coverage includes synthetic v2/6/7 parsing, malformed inputs,
checksum and profile-format gates, exact native memory fingerprints and timezone
conversion. No private firmware or user map is included in this repository.

## RC16 follow-up: cross-ID test and remaining limits

A private conversion of an archived Caccamora v7 map preserved its 160×160
occupancy/auxiliary matrices, 5cm resolution and grid coordinates in RoboJet v2
framing. It had no exclusion zones and no legacy relocation records. Later
v7-specific sections were omitted; this was a test artifact, not a general
cross-model map converter.

Importing it as ID22 returned native result0, selected ID22 and published the
expected live grid with zero restrictions. The file and selected ID survived an
authorized reboot. No live grid was automatically republished after that boot;
full native reload/localization therefore remains unvalidated. The attempted
native return to the protected original was not confirmed. Standby-related
receiver stalls left commands queued, so retries were not treated as safe merely
because a web request timed out.

Agent V1.35 gives map helpers a bounded20s deadline (other helpers retain4s),
allowing exact firmware hashing and backup preparation. Confirmation records the
source ID and pre-request snapshot, so a backward clock correction during boot
does not by itself hide a proven ID change. Native success, matching target ID
and a map publication at/after the native result remain required. These changes
were exercised on Ciccia; they do not establish physical localization or remove
the native receiver's standby limitation.
