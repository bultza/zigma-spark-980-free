# RoboJet Issue08 native map deletion and reset

Maps V1.18 enables these operations for app SHA256
`8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c`
and navigation SHA256
`f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c`.
This is an offline protocol review and implementation; physical deletion/reset
and persistence after reboot have not been tested. Included in the regenerated RC17 archives.

## Binary findings

The app compares `WorkStatus` with `ClearMap` at 0x1cef8, refuses movement
and repeated requests within three seconds, then builds command bytes 0x2b/1.
It does not contain the `del_local_map` / `DelHismapIds` JSON interface used by
other variants. Merely enabling the generic JSON writer would not implement
saved-map deletion.

Navigation creates the app-control queue using `ftok("/",14)` at 0x27a73a.
Its dispatcher selects message type 27 at 0x27a85e and loads the target ID
at 0x27b06e from stack+0x50; the received message starts at stack+0x48.
Thus the ARM wire layout is mtype, length, target ID. The adapter sends a
4096-byte zero-filled payload with uint32 length 8 and uint32 nonzero ID.
No foreign executable offsets or JSON deletion assumptions are reused.

The handler calls `Relocation::localMapDelete` (0x29e630). For a saved,
inactive map it calls `MultipleMap::remove` (0x37b1ec) and refreshes map data.
For the selected map it checks native state via 0x28537c (states 1, 2 or 4),
then runs the active reset branch at 0x27b53c. The removal routine removes the
map record/file; the active path also relinquishes the selected map and resets
related navigation state. Zero ID is rejected by this executable and by our
adapter; bulk deletion is never requested.

## Integration

Both actions use this reviewed navigation operation. Saved-map deletion requires
a different ID from the active map; clear/reset requires the exact active ID and
the existing CLEAR confirmation. Existing revision, optional-backup and idle/error
checks remain. A required backup must match the source revision before dispatch.
The exact app/navigation pair and active ID are checked again before sending;
the queue receiver must belong to that navigation process (including its threads).
No map files are unlinked directly by Zigma Free, and no native binary is patched.

A successful queue send means dispatched, not completed. Refresh must establish
the resulting catalog/ID/map state. The web retains its reset-pending behavior;
no cleaning is started automatically. Native standby rejection or a stalled queue
can still prevent completion, so requests must not be repeated automatically.

Synthetic tests cover wire offsets, zero/overflow IDs, active/inactive separation,
unknown memory, idle/error checks and map v2 regressions. No real deletion command
was sent during implementation.
