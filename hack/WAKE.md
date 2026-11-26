# Experimental clean/pause wake

`zigmactl wake` requests a wake through the native cleaning command and then
pauses the robot. It is not a dedicated MCU wake opcode. Motors and fans may
start briefly. The shell prototype was reported working on Ciccia. The owner reported V1.23
working well on Caccamora; integrated wake remains untested on other variants.

```sh
zigmactl wake --check
zigmactl wake
```

`--check` reads the observer state and checks whether `madplay` already has a
mount over it. It takes the control lock, but does not mount, mute, send a command
or test the kernel's bind-mount support. It is not a physical wake test.

## Sequence and verification

- Require a supported `robot_work_state_cache` observation, a recent observer
  generation (same-boot monotonic age at most five seconds), and cache/channel
  activity within fifteen seconds. Refuse unavailable or stale state.
- A recognized awake state returns success without sending anything. In
  particular, an already-cleaning robot is not paused by a new wake request.
- For hibernation, bind-mount a private executable RAM stub (`exit 0`) over
  `/usr/bin/madplay`. Refuse symlink targets, pre-existing mounts, a non-executable
  stub or mount failure before sending cleaning. No chmod, replacement or
  remount of the original firmware filesystem is performed.
- Recheck hibernation, select Quiet (level 1), and send clean exactly once.
  Repeat the fan selection after 500 ms as in the existing `clean 1` command.
- Send the first pause at 2 seconds from clean dispatch, using the timing
  requested for V1.23. Do not advance
  this pause based on an intermediate awake state: that state does not establish
  that clean startup has completed.
- Send a second pause at 3.5 seconds from clean dispatch, independently of the
  observer. On a transport error or caller loss before the first pause, attempt
  that pause immediately and retain the second at the same 3.5-second deadline.
  Log the relative dispatch time and success/failure of each pause.
- After both pause attempts, enter **observation-only confirmation**. Require
  at least two distinct samples newer than the second pause, in awake idle
  (`Standby`, `Pause`, `CleanDone`, or charging), spanning at least one second.
  Allow up to ten seconds for confirmation. Timeout, unavailable telemetry and
  cancellation in this phase send **no additional command**. These are software
  observations, not wheel-motion measurements.
- Keep voice playback suppressed until at least two seconds after the second
  pause. Never retry clean or reboot. A failed pause dispatch returns an error
  even if the other dispatch succeeded.
- Detach only the mount whose device/inode matches this invocation's stub.
  Remove its private temporary files and release the lock.

An application status report can retain an error for a different, older state
while `robot_work_state_cache` supplies the current state. An error rejects wake
when the report's state code matches the work-state code. A mismatched report
is not treated as authoritative current error evidence. This is not proof that
all physical sensors are healthy.

## Recovery and concurrency

A child guardian owns the mount, bounded sequence and control lock. The calling
process holds a pipe open. On caller termination (including SIGKILL or loss of
the caller's SSH session), the guardian sees EOF and completes the bounded pause sequence if a start was
attempted, then restores audio without any additional post-confirmation command. It does not depend on a shell EXIT trap. The guardian also
handles HUP/INT/TERM. Killing the guardian itself with SIGKILL or a kernel failure
cannot be recovered by that same process; a reboot clears the RAM bind mount.
A blocked kernel syscall cannot be given a hard real-time guarantee.

Wake shares `/tmp/zigma-navigation.lock` with navigation and ordinary CLI
clean/resume/setfan/dock/reboot commands. Concurrent calls are refused. Emergency
`zigmactl pause` remains available. This does not lock out IR commands, physical
buttons, native schedules, old shell scripts, or other direct IPC producers.

The stub suppresses future calls through `/usr/bin/madplay` during the window.
It does not silence an already-running player or another audio path. The normal
volume setting is untouched. No guarantee of silence or zero movement has yet
been established for the integrated command on physical hardware.

If audio restoration fails, the command returns a distinct error and leaves
the stub for inspection rather than detaching a possibly unrelated mount.
Inspect `/proc/self/mountinfo` and the target before manually unmounting it.
A subsequent wake refuses to stack another mount over it.

| Exit | Meaning |
| --- | --- |
| 0 | Already awake without commands, or stable awake idle observed after pause |
| 2 | Invalid command arguments |
| 3 | State unavailable, stale, inconsistent, or changed during setup |
| 4 | Awake idle not confirmed before the deadline |
| 5 | Root required, mute setup failed, or transport/setup failure |
| 6 | Another control operation holds the lock |
| 7 | Audio restoration or guardian completion could not be confirmed |
| 130 | Cancelled; recovery attempted |

## Validation

`make -C src/robot test-wake` exercises the state machine with injected clocks,
states and failures, then tests real parent/guardian processes with simulated
command and bind-mount syscalls in a disposable host directory. It covers
successful wake, no-op when awake, stale evidence, mute/transport/restoration
errors, cancellation, deadlines, concurrent rejection and caller SIGKILL
recovery. The mount shim does not establish physical kernel compatibility.

The Caccamora shell failure was inspected without sending movement: its root
filesystem is read-only SquashFS, so changing `/usr/bin/madplay` permissions
fails. A bind mount is designed to avoid modifying that underlying file.
The integrated ARM binary is built locally; this change alone neither deploys
it to a robot nor updates previously generated release archives.

V1.21 extends only post-pause confirmation from five to ten seconds. The
observer publishes about every 2.5 seconds; transition latency plus two new
samples can exceed the old deadline. The clean-to-pause limit remains 2.5
seconds. A regression fixture publishes at the real cadence and delays the
idle transition, confirming success after the old deadline without another
clean or recovery pause. Physical V1.20 wake on Caccamora was reported successful
by the user despite exit 4; subsequent read-only inspection found Standby,
NoError and the original madplay restored. The exact failed transition was
not captured, so cadence is a plausible explanation, not a reconstructed trace.


## V1.22: stop scheduling separated from confirmation

A further physical V1.21 test on Caccamora reportedly cleaned until the late
recovery pause, about ten seconds after the first. The earlier interpretation
as merely a false-negative confirmation was incorrect. V1.22 removes all command
sending from the confirmation/timeout path and uses the two dispatch deadlines
above. The exact reason the first physical pause was ineffective is not yet
traced. An early intermediate awake state is a plausible contributing condition.

Regression tests model an ignored first pause, intermediate Standby, missing or
stale telemetry, delayed idle observations, dispatch failures and caller death.
They assert all attempted pause commands occur by the three-second deadline
(with simulated tick tolerance), independently of the eventual confirmation
result. Host process tests still use fake mount and IPC syscalls. Deadlines are
software dispatch targets, not a guarantee that the MCU stops motors by then.
V1.22 requires a supervised physical test; installing it or running `--check`
does not perform that test.


## V1.23: user-requested timing adjustment

First pause moves from +2.5s to +2s; second pause moves from +3s to +3.5s,
relative to clean dispatch. Fan commands and observation-only confirmation
remain unchanged. The two-second audio tail now ends no earlier than +5.5s;
the confirmation deadline is +13.5s. Regression assertions use the new dispatch
times; the 2.5-second observer sampling fixture remains unchanged. The owner subsequently reported that this timing worked much better on
Caccamora. No assistant-initiated wake was executed during deployment.
