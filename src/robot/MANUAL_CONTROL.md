# Manual control (experimental, Caccamora only)

V1.12 integrates the native release/exit sequence into **zigmactl itself**.
No separate helper executable, permanent daemon, SPI interceptor or boot change
is needed. The SPI interceptor is only a diagnostic recorder.

The earlier MoveStop + Pause implementation did not stop the physical robot.
One supervised prototype trial subsequently succeeded using native release
(event 0x14, parameter 0xa1), followed by native pause (0x1b, parameter 0).
The user then verified that the IR remote worked normally. The integrated
version has passed offline tests; its shorter, state-driven timings still need
a supervised physical trial. Controller state is not a measurement of braking
or fan speed.

## CLI

```
zigmactl drive forward 3s
zigmactl drive backward 3000
zigmactl drive left 1s
zigmactl drive right 1s
zigmactl stop
zigmactl manual-check
```

These are alternative examples, not a script to execute together. Duration is
100..10000 milliseconds or 0.1s..10s. It bounds direction requests, not measured
wheel motion, distance or angle. A short request may only start the fan.
`manual-check` runs the same observer and native admission checks as drive,
without commands, events or freezing processes. It returns 3 with a specific
reason if blocked; passing is a snapshot, not a reservation of robot state. No automatic wake is performed.

Drive requires Caccamora's identity, exact executable hashes, fresh observer
communication (15 seconds), NoError and idle state. The native app must report
Standby (2) or CleanDone (70), with its direction timer off, wheel commands neutral, and the native
queue empty. Process identity is rechecked during the session. Directions use
the existing app JSON queue and refresh every 500 ms:

| Direction | WorkStatus | Native direction value |
| --- | --- | --- |
| forward | MoveFront | 0 |
| backward | MoveBack | 3 |
| left | MoveLeft | 1 |
| right | MoveRight | 2 |

## Exit and failure handling

Duration expiry, Ctrl+C, SIGTERM, SIGHUP, lease expiry and runtime failures all
enter the same cleanup after any attempted direction submission. An explicit
`stop` invalidates the active generation under a shared gate, preventing its
old driver from submitting another direction. The navigation lock excludes
concurrent CLI navigation sessions; it cannot exclude IR/vendor commands.

Cleanup queues MoveStop then Pause to cancel the native app timer. Timer zero
must remain observed for 250 ms (longer than its 200 ms callback interval), within a 600 ms cancellation budget. It then
publishes release once, waits for both desired/previous wheel pairs to become
neutral, publishes pause once, and verifies native state 26 and app Standby /
NoError with an empty queue. It does not force the native active flag to zero:
the successful physical prototype left that flag set.

The software cleanup budget is 2.5 seconds. Each publication uses a temporary
child of zigmactl: it freezes navigation threads (300 ms budget), refuses queue
critical sections or unexpected state, writes a five-byte slot, publishes the
write pointer last, and resumes the threads. The parent bounds each child to
700 ms or the remaining cleanup budget. A timeout kills only that tracer child;
its exit detaches the traced tasks. No vendor process is killed. Hashing occurs
before movement, not in each exit transaction. These are software deadlines,
not a hard real-time or physical stopping guarantee under kernel failure.

An uncertain publication is never replayed. Failed/unconfirmed cleanup returns
8 and explicitly reports UNCONFIRMED; there is no automatic restart, reboot or
repeated injection. SIGKILL of the driver, power loss, unavailable processes or
kernel failure can still prevent cleanup. A queued command alone never counts
as a successful controller exit.

Exit codes: 0 confirmed controller exit (or no movement requested); 1 local /
transport error; 2 invalid input; 3 state/link/identity guard; 6 navigation busy;
7 unsupported profile; 8 unconfirmed exit; 130 interrupted/cancelled after
successful cleanup. Cleanup failure takes precedence over the initiating error.

## Held-button contract for the web

`zigmactl hold forward` (also backward/left/right) reads a local pipe. It has no
fixed movement duration and sends no direction before the first renewal:

- `.` renews a 750 ms lease; send every 250 ms or less while pressed.
- `!` or EOF releases immediately into the same cleanup path.
- Missing renewals expire the lease; invalid/oversized input fails closed.

The future authenticated web adapter must own one session at a time, forward
renewals only while the user's button is pressed, and release on pointerup,
pointercancel, lost capture, visibility loss or connection expiry. A server-side
watchdog must stop forwarding stale browser renewals. Changing direction should
release the old session before starting the next. Do not generate unconditional
keepalives just because a socket remains connected. The current change prepares
the CLI contract; **the web endpoints and buttons are not yet implemented**.
A release initiates verified cleanup; it does not promise instantaneous braking.

## Variant investigation (offline images, 21 November 2026)

| Image / robot | App SHA prefix | Navigation SHA prefix | Result |
| --- | --- | --- | --- |
| Caccamora original | 4cf0709919f0 | b10fc7b48d1b | Only enabled target; prototype physical exit succeeded |
| Ciccio original snapshot | 4cf0709919f0 | b10fc7b48d1b | Same native binaries; physical validation pending |
| Ciccia original | 4cf0709919f0 | b10fc7b48d1b | Same native binaries; physical validation pending |
| German issue01b original | 4cf0709919f0 | b10fc7b48d1b | Same native binaries; physical validation pending |
| Puron issue01a / issue03 | 784fbcda505d | 6b7b11091132 | Different app and navigation; requires an exit-profile port |
| Russian issue04 original | 8084000b686d | 1b2751efd2a4 | Different app and navigation; requires an exit-profile port |
| Russian issue04 hybrid | 8084000b686d | b10fc7b48d1b | Same navigation, different app; timer/status addresses need research |

Image files were read offline and app/navigation binaries reconstructed from
JFFS2 with CRC checks. No other robot was contacted. Identical binaries support
reusing code/addresses, but do not establish live firmware identity, MCU behavior
or physical timing on a different robot. Models and flash layouts alone are not
compatibility evidence. All non-Caccamora identities remain rejected.

Exact supported hashes:

- App: `4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a`
- Navigation: `b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255`

## Validation

`make test-manual-control test-navigation test-manual-queue zigmactl` builds the
ARM executable and tests direction mapping, durations, cancellation, competing
stop, process/state/IPC failures, uncertain exit without replay, lease loss,
release before first renewal, and a maintained session longer than ten seconds.
The queue test uses a local synthetic two-thread process and real ptrace to
verify both events, pointer wrap and resumption; ptrace permission is required.
Mock timing is not physical timing. The host control/navigation tests also pass
ASan/UBSan with leak detection disabled because this sandbox blocks LSan.

V1.12 admits CleanDone at both the observer and native-app gates, retaining all
error, freshness, neutral-wheel, timer, queue and navigation-state checks.
Rejections now include their reason and observed values. Exit verification and
its timing are unchanged; accepting CleanDone does not weaken exit confirmation.
