# MCU–Linux communications

Reverse-engineering notes for the Zigma firmware tested on Caccamora, updated
22 November 2026. This is an observed protocol inventory, not a complete vendor
specification. It distinguishes captured behavior, static code findings and
interpretations that still need verification.

## Firmware scope

| Component | SHA-256 |
| --- | --- |
| `demo_rplidar` — Linux navigation | `b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255` |
| `wifi_app_server_grit` — native application server | `4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a` |

Addresses below are virtual code addresses in these builds, not portable file
offsets. Navigation code was analysed as ARM Thumb. See
[Firmware variants](FIRMWARE_VARIANTS.md) before applying findings to another
binary. Matching Linux executables do not establish matching MCU firmware.

## Responsibilities and transport

The lidar is acquired through UART on Linux. That does not mean its information
stays on Linux: navigation sends processed laser information to the MCU over
SPI during captured cleaning sessions.

```text
Lidar ──UART──> Linux: acquisition and navigation calculations
                         │
                         ├──SPI──> MCU: commands and derived navigation data
                         └<─SPI─── MCU: status, events and requests
```

Messages `0x400C` and `0x4100` carry compact results derived from laser samples.
`0x400A` provides wall-follow assistance, and `0x400B` reports an obstacle-related
calculation. This supports navigation responsibilities being shared between
Linux and the MCU; it does not establish that the MCU performs SLAM or directly
reads the lidar UART.

A reported robot variant without a lidar tower is compatible with a different
navigation mode. It does not prove that this exact MCU firmware can navigate
independently with equivalent behavior.

The traced Linux transport reaches the SPI worker using `/dev/spidev0.1`.
The sender at `0xDAF4C` builds the previously identified `AA55` framing,
fragmentation and CRC. Inside that transport, the messages examined here have
an eight-byte header: a little-endian 32-bit message ID followed by a
little-endian 32-bit payload length. Sizes below exclude both this header and
the outer SPI framing.

## Five navigation messages: Linux → MCU

| SPI ID | Payload size | Recognized purpose | Confidence and remaining questions |
| --- | --- | --- | --- |
| `0x4004` | 24 bytes | Position/orientation-related update, additional scalar and apparent time value | Strong interpretation from coordinate transformations and field copies; fourth scalar, reference frame and units remain unresolved. |
| `0x400A` | 24 bytes | Laser-assisted wall-follow information | Code path explicitly logs `laser_wf_pose(eslam)`; indicator meanings and units remain incomplete. |
| `0x400B` | 8 bytes | Result of a laser obstacle-direction calculation | Code logs `laser_fobs_dir`; status handling is identified, but the numerical result's exact convention is not. |
| `0x400C` | 60 bytes | Processed laser information for navigation/avoidance, including twelve distances | Laser input and distance array are traced; the leading scalar and trailing indicators are not fully identified. |
| `0x4100` | 64 bytes | Eight distance–angle pairs summarizing angular ranges | Scan iteration and minimum selection are traced; exact interval boundaries, orientation and distance scale remain to be verified. |

### `0x4004`: position-related update

The builder at `0x615D8`, with ID assignment at `0x61644`, serializes four
32-bit floating-point arguments followed by two words, consistent with a
64-bit time value. The first three floats are also stored at object offsets
`0x1098`, `0x109C` and `0x10A0`.

The associated local call to `0x1D368C` receives a transformed representation:
negative second coordinate, first coordinate and a normalized angular value.
This is evidence for a pose/orientation update, not a direct wheel-speed command.
The fourth float has not been assigned a verified meaning.

One captured payload decoded as approximately:

```text
float32: -0.00564128, -0.00270340, 2.07897258, 0.55452478
uint32:  1690805, 0
```

That packet was captured at SPI uptime about 1688.946 seconds. Do not equate the
apparent timestamp with the capture clock without deriving their relationship.

### `0x400A`: wall-follow assistance

Builders at `0x62F14` and the path around `0x7B010` use this layout:

```text
uint32 value0
float32 value1, value2, value3
uint32 value4, value5
```

The latter path calls `0x9E840`. Logging within that calculation, around
`0x9ED48`–`0x9EDD8`, identifies `bump_dir_cacl.cc`, `laser_wf_pose(eslam)`,
`point`, `l_dist` and `w_dist`. This connects the message to laser-assisted
wall following.

The `0x1DF920 → 0x1D6B60 → 0x62F14` path supplies final field `1`; the path
around `0x7B090` supplies `0`. Their full semantics are unresolved. A captured
example is `[255, 0.0, 0.0, 0.0, 255, 0]`; `255` must not be assigned a particular
error meaning solely from that example.

### `0x400B`: obstacle-direction calculation result

The builder at `0x69AE8` calls `0x9DD90`, obtaining a boolean result and a float
output. Packet construction at `0x69C14`–`0x69C30` produces:

```text
int32 status       // 0 on successful calculation, -1 on failure
float32 result     // meaningful only on the successful branch
```

The local mirror at `0x1DEE24` logs `laser_fobs_dir:%d,%f`. A front-obstacle
interpretation of `fobs` is plausible, but the angle/distance convention is not
fully established.

**On failure, this builder does not initialize the result slot.** Both packets
in the inspected cleaning trace carried status `-1`. Their second word must
not be interpreted as a valid measurement.

A separate incoming 20-byte message handled at `0x8D9C6` can trigger this
calculation when flag bit 2 rises. This is not evidence that sending `0x400B`
to Linux requests the calculation.

### `0x400C`: twelve processed distances

Builder `0x62E34` emits:

```text
float32 leading_value
float32 distances[12]
uint32 trailing_value0, trailing_value1
```

The caller at `0x7C838` copies 360 floats (`0x5A0` bytes) from the laser buffer
under a mutex, processes them through `0x6A4B0`, logs twelve `dist` values and
calls the sender at `0x7CA78`.

The leading float was frequently approximately `6.28318548` (`2π`). An angular
or avoidance-heading interpretation is a candidate, not a verified field
contract. Both trailing words were zero in the example inspected. The twelve
distances changed during the recorded cleaning session.

### `0x4100`: angular-range obstacle summaries

The builder spans `0x69FC0`–`0x6A474` and logs `rapid dir ranges`.

- `0x6A190`–`0x6A2A6` iterates 360 scan samples and calculates transformed radii
  and angles.
- The code selects minima and examines eight configured angular ranges around
  `0x6A2AA`–`0x6A31x`.
- `0x6A3AE`–`0x6A3D4` copies eight pairs into the 64-byte payload.

The pairs represent selected distances and their associated angles. The angle
calculations use radians. Exact sector boundaries and the distance scale still
need verification; do not assume eight uniformly spaced sectors.

Examples include `(0.39953, 0.01636)` and `(0.20201, 5.31057)`. An observed
`(100, 0)` pair contains an initialized/default distance value; it is not proof
of an obstacle 100 metres away.

## Commands carried inside `0x404F`

These are distinct from the navigation-data messages above. The native
application sends an inner command through navigation IPC; navigation forwards
it as SPI `0x404F`. The inner frame is:

```text
AA 03 B0 B1 B2 CHECKSUM
CHECKSUM = (B0 + B1 + B2) & 0xff
```

| Inner bytes `B0 B1 B2` | Recognized operation | Evidence |
| --- | --- | --- |
| `02 21 00` | Forward | Native mapping and physical SPI captures. |
| `02 21 01` | Left | Native mapping and emulation. |
| `02 21 02` | Right | Native mapping and emulation. |
| `02 21 03` | Backward | Native mapping and emulation. |
| `02 22 02` | Automatic cleaning / resume | Code and supervised trials. |
| `02 22 03` | Return to dock | Native control implementation. |
| `02 24 02` | Quiet fan setting | Native table and isolated SPI trial. |
| `02 24 00` | Normal fan setting | Native table. |
| `02 24 01` | Strong fan setting | Native table. |
| `02 26 00` | Pause | Captured; insufficient to exit some manual states. |
| `02 29 01` | Location alarm | Static handler identification; not a wake command. |

`0x4050` is the observed response to `0x404F`, often carrying zero. It does not
prove execution: the MCU responded to every forward command in the hibernating
trial without entering manual mode.

Native `MoveStop` cancels the application's direction timer; it does not emit a
verified `02 21 04` stop frame. See [Manual control](../src/robot/MANUAL_CONTROL.md)
and the implementation in [zigmactl.c](../src/robot/src/zigmactl.c).

## MCU → Linux events and responses

| SPI ID | Recognized content | Interpretation |
| --- | --- | --- |
| `0x4001` | Robot status and diagnostic messages | Includes MCU work state; also observed voice/button diagnostics. |
| `0x4046` | Voice request | Linux responds with `0x4047`. |
| `0x4050` | Response to `0x404F` | Receipt/response does not establish physical execution. |
| `0x4101` | Event `14`, parameter `9D` / `9E` / `9F` / `A0` | Forward / backward / left / right, correlated with isolated IR trials. |
| `0x4101` | Event `14`, parameter `A1` | Direction release/neutral event. |
| `0x4101` | Event `1B`, parameter `00` | Pause-related navigation transition. |
| `0x4101` | Event `06`, parameter `00` | Start of the cleaning sequence. |
| `0x4104` | Response associated with outgoing `0x4103` | Navigation-state exchange; complete field semantics pending. |

Linux acknowledges `0x4101` with `0x4102`. Event pairs above are hexadecimal;
the captured event wrapper contains two little-endian 32-bit words.

Some captured RX frames have the same IDs as the five outgoing navigation
messages. **They are not established acknowledgements.** In the native
receive dispatcher at `0x8C9B0`, the jump table at `0x8C9DC` sends all five IDs
directly to the return at `0x8CE58`. Other transport handling, echoes or stale
buffer effects remain unresolved; do not invent request/response pairings.

## Hibernation and wake observations

The native application's real `robot_work_state` is at virtual address
`0x48EAC` in the app-server build above. Its getter/cache at `0x48E24` is a
different value and has been observed remaining at CleanDone while MCU status
changed. Writing either Linux variable would not demonstrate a changed MCU
state.

An idle trial observed Standby → Hibernating about 181.5 seconds after wake.
The MCU first reported state `4` through `0x4001`; Linux then logged
`enter powersave: 1`. The surrounding SPI window contained only unchanged
periodic outgoing `0x4009` data and `0x4059` data changing in its clock field.
This supports MCU-initiated idle hibernation, but does not exclude earlier Linux
configuration of the timer or another hardware control channel.

Supervised trials established the following behavior for this robot:

- Pause alone and the already-selected Quiet fan setting did not wake it.
- Fifteen direct forward frames over three seconds reached the MCU and received
  replies, but it remained hibernating. Bypassing Linux admission is insufficient.
- Cleaning woke it and entered cleaning mode.
- Cleaning followed by Pause 104 ms later did not reliably cancel the pending
  cleaning transition: it briefly reported Standby, then entered cleaning.
- IR Power and the chassis button woke it, without an identified outgoing Linux
  SPI wake command in those windows.

No dedicated software Power/wake opcode has been identified.

## Scan export versus normal navigation data

A separate compiled path builds `0x405C` scan-export messages, gated by requested
sample count, and a `0x4029` response associated with its request handler. That
path reaches the SPI transport. It was not observed in the cleaning window used
for the five-message analysis, so it does not demonstrate routine transmission
of complete raw scans. The compact `0x400C` and `0x4100` summaries were observed.

## Evidence and reproducibility

The five-message analysis used the firmware hashes above, static builder and
caller inspection, and an existing 60-second cleaning capture from
22 November 2026. It did not require new robot commands.

| ID | Captured TX frames | Same-ID RX frames | Payload bytes |
| --- | ---: | ---: | ---: |
| `0x4004` | 106 | 12 | 24 |
| `0x400A` | 276 | 4 | 24 |
| `0x400B` | 2 | 0 | 8 |
| `0x400C` | 109 | 2 | 60 |
| `0x4100` | 151 | 5 | 64 |

The complete window contained 5,682 records, 6,492 TX frames and 8,245 RX frames,
all with valid decoded CRCs. No new buffer-full losses or incomplete records
were detected. The archived robot payload and streamed host payload matched
SHA-256 `a99c6e31a6a49894ad120d05ac0facc2c59a7abcff588feec0dd2f9ea88ab585`.

Maintainer evidence identifiers (private archive, not files shipped here):

- `analysis/spi_navigation_five_20261122/`: selected disassembly, extraction
  scripts and `capture-crosscheck.json`.
- `runtime/caccamora_power_capture_20261122/clean-pause-100ms-window-1795353090/`:
  source SPI capture and decoded frames.
- `runtime/caccamora_hibernate_capture_1795373550/`: idle transition and direct
  forward-from-hibernation trials.
- `analysis/lidar_stream_20261105/`: separately traced scan-export path.

These identifiers and addresses support follow-up analysis; the private firmware
and captures are not required or downloaded by the normal robot software.

## Extended receive-dispatch inventory (22 November 2026)

An offline traversal of `demo_rplidar` found **75 non-default message entries**
in a 279-entry dispatch interval, `0x4001` through `0x4117`. This count is for
one receive dispatcher, not the whole wire protocol. Response-only IDs can be
handled elsewhere and are not counted here. Compiled support does not prove
that every message is emitted by the installed MCU or enabled in every mode.

The entry at `0x8C9B0` reads the inner ID, subtracts `0x4001`, checks the index
against `0x116`, and uses the Thumb halfword jump table at `0x8C9DC`.
Unimplemented entries lead to `0x8CE58`. Fifty entries identify themselves
directly through `FLAT_*` length-error strings; other rows use reviewed handler
behavior or explicitly remain unnamed. Shared error branches are not reliable
names: `0x4085` and `0x4087` reuse messages belonging to other IDs.

**Direction:** the input column is received by Linux from the MCU-facing
transport. The output column is the response constructed by Linux. Do not
reverse these pairs to invent Linux-to-MCU control commands. `—` means no
response was established in the inspected paths, not proof that no response
exists. Sizes are decimal payload bytes excluding the inner header; `variable`
means no fixed initial length check was identified.

| Input ID | Recognized name or behavior | Input bytes | Constructed output ID | Handler address |
| --- | --- | ---: | --- | --- |
| `4001` | Status/diagnostic forwarding | variable | — | `0008DA3A` |
| `4002` | Enable communication flag; publish Linux state | 4 | — | `0008DA22` |
| `4003` | Flags and pose input; can trigger obstacle-direction calculation | 20 | — | `0008D9C6` |
| `400E` | Pose-related input (partial) | 16 | — | `0008D8F4` |
| `400F` | 144-byte periodic input; detailed sensor fields unresolved | 144 | — | `0008DB46` |
| `4010` | GetNearbyObstaclePositionRequest | 28 | `4011` | `0008DAF6` |
| `4012` | SetRegionWallFollowStatusRequest | 4 | `4013` | `0008D7B6` |
| `4014` | GetMapTypeRequest | 8 | `4015` | `0008D7A0` |
| `4016` | GetCenterFreePositionRequest | 24 | `4017` | `0008D894` |
| `4018` | GetUncoveredAreaRequest | 24 | `4019` | `0008D87E` |
| `401A` | SendAreaRectRequest | 24 | `401B` | `0008D868` |
| `401C` | GetInfoRequest | 32 | `401D` | `0008D828` |
| `4020` | GetWallFollowLoopStatusRequest | 4 | `4021` | `0008DACE` |
| `4022` | RegionAddEdgeRequest | 24 | `4023` | `0008DABA` |
| `4024` | RegionAddNodeRequest | 16 | `4025` | `0008DB1E` |
| `4026` | RegionGetNextRegionRequest | 12 | `4027` | `0008D6EA` |
| `4028` | LaserStreamRequest | 8 | `4029` | `0008DC8C` |
| `402A` | RadiaCalibrationRequest | 4 | `402B` | `0008DB32` |
| `402C` | GyroCalibrationRequest | 12 | `402D` | `0008DBF6` |
| `402E` | HoslamStateRequest | 4 | `402F` | `0008D8C0` |
| `4030` | EvadeInfoRequest | 20 | `4031` | `0008DC1E` |
| `4032` | GetRobotTrappedRequest | 4 | `4033` | `0008DAE2` |
| `4034` | RobotConfigRequest | 188 | `4035` | `0008DBCE` |
| `4036` | EnableMapRequest | 4 | `4037` | `0008D716` |
| `4038` | GetWfLoopWithPoseRequest | 12 | `4039` | `0008DC0A` |
| `403A` | SendHasCleanInfoRequest | 24 | `403B` | `0008DB0A` |
| `403C` | SendFindpathRectRequest | 16 | `403D` | `0008DBE2` |
| `403E` | SetParamRequest | 28 | `403F` | `0008D7E2` |
| `4040` | RegionSetRegionResultRequest | 8 | `4041` | `0008DBBA` |
| `4046` | Voice request forwarding | variable | `4047` | `0008DA94` |
| `404C` | LaserFactoryRequest | 4 | `404D` | `0008DBA6` |
| `4052` | FindPathRequest | 24 | `4053` | `0008D534` |
| `4054` | GetCleanPointRequest | 4 | `4055` | `0008D6D4` |
| `4056` | GetLastObsPosesRequest | 4 | `4057` | `0008D6A8` |
| `4059` | SendFloatInfoAsync | 72 | — | `0008D6BE` |
| `405A` | SendFloatBundleAsync | variable | — | `0008D68E` |
| `405B` | ESLAM-related diagnostic path | 192 | — | `0008D8D6` |
| `405F` | Five-float calculation input (partial) | 20 | — | `0008D72C` |
| `4061` | SendDAStateRequest | 4 | `4062` | `0008D7F8` |
| `4063` | LidarDistanceRequest | 8 | `4064` | `0008D678` |
| `4065` | RoomSegmentRequest | 4 | `4066` | `0008DA80` |
| `4067` | RoomsGetNextRoomRequest | 4 | `4068` | `0008D8AA` |
| `4069` | RoomsSetResultRequest | 8 | `406A` | `0008DA6C` |
| `406B` | RoomsGetEdgesRequest | 12 | `406C` | `0008D700` |
| `406D` | RoomsGetExtentRequest | 4 | `406E` | `0008DA58` |
| `406F` | RoomsGetObstacleRequest | 16 | `4070` | `0008D7CC` |
| `4071` | DynamicStartRegionRequest | 8 | `4072` | `0008DA44` |
| `4073` | DynamicGetPolygonRequest | 12 | `4074` | `0008D520` |
| `4077` | DynamicGetNextRegionRequest | 8 | `4078` | `0008D4F8` |
| `4079` | DynamicStopRegionRequest | 4 | `407A` | `0008D4E4` |
| `407B` | DynamicDropRegionRequest | 12 | `407C` | `0008D50C` |
| `407D` | LidarDockInfoRequest | 4 | `407E` | `0008D4D0` |
| `407F` | RoomsAppDemoRequest | 4 | `4080` | `0008D4BC` |
| `4081` | VirtualWallCommandRequest | 20 | `4082` | `0008D4A8` |
| `4083` | DynamicSetModeRequest | 4 | `4084` | `0008D494` |
| `4085` | Unnamed 4-byte operation (do not infer name from shared error text) | 4 | `4086` | `0008D464` |
| `4087` | Unnamed 24-byte operation (shared error text is misleading) | 24 | `4088` | `0008D410` |
| `4089` | GeneralOperationRequest | 8 | `408A` | `0008D38E` |
| `408B` | RoomsExchangeRoomRequest | 8 | `408C` | `0008D37A` |
| `408D` | DynamicSetExtentRequest | 4 | `408E` | `0008D366` |
| `408F` | 28-byte input to 0x9D384 (partial) | 28 | — | `0008D350` |
| `4091` | GetReachableInRectRequest | 24 | `4092` | `0008D33C` |
| `4095` | Two three-word inputs to 0xAC3B4 (partial) | 24 | — | `0008D306` |
| `4098` | LocalPlannerRequest | 8 | — | `0008D282` |
| `409A` | LocalPlannerStopRequest | 4 | `409B` | `0008D244` |
| `4101` | Navigation event | 8 | `4102` | `0008D216` |
| `4105` | 16-byte operation passed to 0x1B9FB0 | 16 | `4106` | `0008D1D6` |
| `4107` | Four floats and one word passed to 0x1B6224 | 20 | `4108` | `0008D2AA` |
| `4109` | SendVrepGoal | 8 | — | `0008D296` |
| `410A` | RelocationWithConstraintRequest | 40 | — | `0008D2EA` |
| `410C` | CalPointCloudCentroidResponse | 16 | `410D` | `0008D1C2` |
| `410E` | GetRRTGoal path (logged name) | 4 | `410F` | `0008D0A0` |
| `4110` | GetGeneralPoseRequest | 8 | `4111` | `0008CE64` |
| `4116` | VREP-related variable message | variable | — | `0008D2FE` |
| `4117` | Acknowledged VREP-related variable message | variable | `4118` | `0008DD94` |

### Newly resolved lidar request paths

- **`0x4028 → 0x4029`: scan-export setup.** The eight-byte request contains
  two signed 32-bit values stored at object offsets `0x1100` and `0x1104`.
  The first is wrapped to `[0, 359]`; the second is adjusted by multiples of
  360 into `[0, 360]`. Code preserves positive `360` for the second field.
  The four-byte response returns that second value. In the separately traced
  producer, the first selects scan indexing and a positive second value
  controls the sample count for `0x405C`. A zero count bypasses that producer.
  This resolves the request ID that the earlier scan-export analysis lacked;
  no request was injected and live enable/disable behavior remains untested.
- **`0x4063 → 0x4064`: lidar-distance calculation.** Handler `0x8FE54`
  reads a float and an integer, calls `0x63480`, and returns one float.
  The helper copies 360 laser samples, applies heading transformations and
  searches an angular neighborhood. Angle/reference-frame details and the
  integer's precise external convention remain incomplete.
- **`0x407D → 0x407E`: `LidarDockInfoRequest`.** Identified from its own
  length-check branch, not from a neighboring string. It accepts four bytes;
  the detailed docking fields remain to be decoded.

### Calibration and configuration are real mutation paths

`0x4034` (`RobotConfigRequest`, 188 input bytes) accesses configuration for
`HOSLAM_GYRO_KGTG`, `HOSLAM_LASER_ANGLE`, ToF offset/crosstalk, laser near offset
and accelerometer offsets, and builds `0x4035`. Its logs also expose SDK/TAG
information. The field-by-field request/response contract is not complete.

`0x403E` accepts 28 bytes and has a second jump table at `0x8E77E`. Its selector
is the first payload word. The following effects are supported by the inspected
branches; they are not recommended calibration values or live-tested commands.

| Selector (decimal) | Identified code path |
| --- | --- |
| 0 | Laser-angle calibration parameter (`HOSLAM_LASER_ANGLE`). |
| 1 | Gyroscope parameter (`HOSLAM_GYRO_KGTG`). |
| 2 | ToF offset (`HOSLAM_TOF_OFFSET`). |
| 3 | ToF crosstalk (`HOSLAM_TOF_XTALK`). |
| 4 | Additional laser-angle-related path; distinction from selector 0 unresolved. |
| 5 | Configuration collection/storage path involving the calibration fields; exact persistence semantics unresolved. |
| 6 | Laser near offset (`HOSLAM_LASER_NEAR_OFFSET`). |
| 7 | Accelerometer X/Y/Z offsets. |

The response is `0x403F`. The inspected construction advertises 36 payload
bytes; only six float stores are explicit in the common return sequence.
Do not assume that all advertised bytes have defined values without tracing
initialization along each path.

`0x402A` and `0x402C` separately identify `RadiaCalibrationRequest` and
`GyroCalibrationRequest`, returning `0x402B` and `0x402D`. Their existence does
not make calibration safe to probe with guessed payloads. None were executed.

### State control, general operations and compiled stubs

`0x402E` (`HoslamStateRequest`) is **not merely a status query**. It passes its
four-byte payload to the state-control function at `0x98CD4`, which contains a
12-case switch and a path logged as `relocate hoslam...`. It then publishes
Linux state and returns `0x402F`. This does not identify an MCU wake command.

`0x4089` (`GeneralOperationRequest`) accepts two words, with a selector table
covering 0–54 at `0x8D3A2`, and returns `0x408A`. Several selectors share a
fallback branch: there are not necessarily 55 distinct operations. Examples
from inspected paths:

- Selector 29: relocalization-result handling, logged as `relocation: relo_result=`.
- Selector 32: logs `Relocation: Stop all` and calls local relocalization helpers.
  This is not a demonstrated physical motor stop, nor an identified MCU command.
- Other selectors still require semantic tracing; a generic successful-looking
  response must not be interpreted as a completed physical action.

`0x4098` (`LocalPlannerRequest`) accepts eight bytes but takes the valid-length
branch directly to return in this build. It is a compiled stub in this handler,
not evidence of an available working planner API. `0x409A` has a separate local
planner stop path and constructs `0x409B` on the normal return.

`0x4116` and `0x4117` both call `0x740C8`, whose logs mention VREP message length
and a version string. `0x4117` constructs `0x4118` before calling that helper.
Their presence does not demonstrate simulator traffic on the physical robot.

### Periodic traffic and passive cross-check

The formerly unnamed outgoing `0x4009` builder is at `0x628A0`: it publishes
one state-related word and one float (eight payload bytes). It is called by
incoming `0x4002` and after `HoslamStateRequest`. The exact state enumeration
and scalar semantics remain unresolved; do not label it an MCU sleep request.

Incoming `0x4002` checks four payload bytes, sets an enable flag at object
`+0x1094` and calls that publisher. This is more specific than treating the ID
as a generic acknowledgement.

The existing cleaning trace provides independent length checks against compiled
receive handlers:

| Input ID | Compiled fixed payload | Observed frames | Observed payload |
| --- | ---: | ---: | ---: |
| `0x4002` | 4 | 145 | 4 |
| `0x400F` | 144 | 5,947 | 144 |
| `0x4059` | 72 | 7 | 72 |
| `0x4101` | 8 | 4 | 8 |

`0x400F` unpacks integers, floats and embedded blocks and calls `0x2C4A4`; its
individual sensor fields have not yet been assigned verified meanings.
`0x4059` identifies itself as `SendFloatInfoAsync` on the receive side. Its
appearance in both directions does not, by itself, explain every field or prove
an echo mechanism. `0x4001` remains variable-length, including status and debug
text; it must not be decoded as one fixed structure unconditionally.

### Analysis artifacts and limitations

Private maintainer archive `analysis/spi_dispatch_inventory_20261122/` contains
`dispatch.json`, selected Thumb disassembly, `paths.json`, selector-table
extracts, `crosscheck.json` and extraction scripts. The scripts check the source
SHA-256 and read ELF load segments rather than assuming one file/virtual offset.
Branch traversal is an analysis aid: indirect calls and nested switches require
separate inspection, and strings from shared error paths can mislead naming.

A separate linear scan found 86 distinct message-range immediate constants in
part of the navigation code. They are retained only as search candidates:
constants are not 86 confirmed outgoing commands, and linear disassembly can
include literal pools. This inventory does not claim exhaustive coverage or a
new wake opcode. No MCU requests, calibration changes, movement or robot
connections were made for this extension.

## Hoslam state control, general operations and MCU updater (offline follow-up)

This section is static analysis of the Caccamora binaries identified above. No
robot connection, injection, updater execution or GPIO operation was performed.
Addresses are build-specific Thumb entry points, not callable API guarantees.

### `0x402E`: a request to Linux, not a discovered MCU wake command

The receive handler (`0x8D8C0`, valid path `0x8FE26`) takes one 32-bit selector,
obtains the state-control object and calls `0x98CD4`. Afterwards it publishes
`0x4009` and replies with `0x402F`, payload word zero. That reply does not report
whether a physical wake occurred. Direction is **MCU → Linux** for the request;
sending the same ID towards the MCU is not an established inverse operation.

The table at `0x98CE2` decodes all twelve selectors (decimal):

| Selector | Log / branch | Verified local action |
| ---: | --- | --- |
| 0 | reset hoslam | Changes control state, calls reset helpers, emits internal events `0x22`, `0x24`. |
| 1 | pause hoslam | Calls `0x98714` with zero and relocation stop helper `0xAAD88`; stores control state 2. |
| 2 | resume hoslam | Stores control state 5, sets object byte `+1`, calls `0x98714` with one. |
| 3 | reset async hoslam | Clears object byte `+0`, emits event 0, resumes control and emits `0x22`, `0x24`. |
| 4 | sleeping hoslam | Normally saves previous state at `+8` and stores 4 at `+4`; a separate communications-mode check can instead take the sleep-notification path. |
| 5 | showmap hoslam | Sets object word `+0x38` and emits event `0x23`. |
| 6 | clear hoslam | Calls clearing/reset helpers and emits event `0x23`; not a read-only operation. |
| 7 | sleep hoslam | Only when local control state is 4: emits `0x22`, `0x24`, calls `0xA0DBC`, sets SPI-off flag and sends a local sleep notification. |
| 8 | wake hoslam | Only when local control state is 4: restores saved state, sends IPC wake with acknowledgement wait, clears SPI-off flag, sets another local flag, waits 800 ms and invokes `0xA0C6C`. |
| 9 | wake hoslam | Same state guard/restoration; sends IPC wake without that acknowledgement wait, then polls object byte `+0` (roughly 500 ms maximum) before the common wake tail. |
| 10 | no log at entry | Sets object byte `+0` to one: the notification flag polled by selector 9. It does not itself execute the wake sequence. |
| 11 | relocate hoslam | Emits `0x22`, `0x24`, then event 2, and waits for the local relocation flag to change. |

These are **Hoslam control states**, not the MCU's work-state enumeration or the
manual interceptor's native navigation state. Equal numeric values do not make
them the same variable. Selectors 8 and 9 share entry `0x98FB8` but differ after
`0x98FF2`; selector 10 enters at `0x99016`. The `irq wake` log belongs to the
selector-9 polling tail, not to selector 10's short entry itself.

Wake helper `0x5FF94` calls `msgsnd` with message type 9 and zero data-length
field; its optional wait uses `msgrcv` for type 9, in 10 ms steps. Sleep helper
`0x60064` sends type 11 with a four-byte local value. This is SysV IPC, not the
SPI transport. The IPC peer and its complete downstream effects are not proven
by these two functions alone; do not confuse these numeric IPC types with the
app-server's separate JSON-handler table.

`0xDB1A8` stores a local byte and logs `spi:off`; `0x60958 → 0x1B4B2C` stores
another local word to one. The final `0xA0C6C` also invokes registered callbacks,
so its entire transitive effect is not established here. None of this supplies
a verified Linux→MCU wake packet. It is useful evidence for how Linux catches
up with wake/sleep, and a lead for further IPC-peer analysis, not a replacement
for Power. Likewise, selector 1 is not the already tested manual-exit sequence
of releasing the direction event and then queuing pause.

### `0x4089`: suboperations, aliases and misleading success values

The request has two 32-bit words: selector and argument. The 55-entry table at
`0x8D3A2` covers selectors 0–54. A four-byte result goes back in `0x408A`.
The following effects are supported by direct branches and inspected helpers;
unknown field names remain unknown rather than being labelled wake/stop.

| Selector (decimal) | Evidence / effect |
| --- | --- |
| 0, 1, 3 | Read room-cleaning object fields `+0x168` (byte), `+0x169` (byte), `+0x174` (word), respectively. |
| 2 | Clears the room-cleaning object's byte `+0x169`. |
| 5 | Copies room-cleaning word `+0x60` to `+0x64`. |
| 7, 12 | Exact alias: same branch `0x8DDBA`, calls `0x1142E8`. |
| 9 | Calls `0xFDA0C` with argument and local position data; logs `offline in room=`. |
| 13 | `0xAC148`, `SetAutoDetectLevel`: accepts values 0–2 in the relocation object. |
| 14 | Room lookup `0xFF528`; forwards its result through local helper `0x60234` with type `0x2F`; SPI response itself is zero. |
| 15 | `0xFF128`: copies room-cleaning fields `+0x90 → +0x94`, `+0x178 → +0x17C` under its mutex. |
| 18 | Reads relocation byte `+0xE1D`. |
| 19, 26 | Store boolean argument in relocation bytes `+0xE28`, `+0xE29`. |
| 20 | Calls `0xFF71C`, logging `is_same_room(`. |
| 21 | Stores argument in room-cleaning word `+0x18C`. |
| 22 | `0xFF8B4`, logging `set_clean_method(` and an invalid-method path. |
| 24 | `0xFF17C`, `RoomCleaning::get_clean_queue_size`, with first parameter 1 and the supplied argument. |
| 27 | Stores boolean argument in another object's byte `+0xC`, through `0x16E338`. |
| 29 | Relocation-result handling; logs `relocation: relo_result=`, updates state control and reports result. |
| 31 | Relocation cleanup calls `0xAC3F4`, `0xACEE0`, `0xB5528`. |
| 32 | Logs `Relocation: Stop all`; calls `0xAAD88` and `0xB5528` on the relocation object. |
| 36 | Calls `0xAD1C0`, logging `Relocation::check_near_kidnap_place score:`. |
| 37 | Stores boolean argument in receiving object's byte `+0x2008`. |
| 39 | Calls `0xAD2FC`, logging `check_kidnap_place_result`. |
| 53 | `0x1DCF08`: stores boolean IMU-debug flag and logs `set_imu_debug`. |
| 54 | `0x1BCF68`: stores laser-mode value when signed argument is ≤4, logs `wf laser mode=`. No lower-bound check is visible in that helper. |

Selectors **17, 30, 33–35, 40–43, 45, 47–52** use the shared default branch
`0x8DE30`, returning zero without a selector-specific action. Out-of-range
selectors also reach that default. Therefore a zero `0x408A` result is not
proof that an operation exists or succeeded physically.

Selectors 4, 6, 8, 10, 11, 16, 23, 25, 28, 38, 44 and 46 have distinct code
paths but their full semantics are not established here. They must not be
classified as harmless from their short dispatcher bodies. The archived
selector map retains their helper addresses for further analysis.

For selector 32, `0xAAD88` clears relocation byte `+0xD30` and operates on a
local worker; `0xB5528` releases an object and resets associated storage. These
are not the known manual release/pause event pair. No inspected branch proves
that this request exits MCU DirectionControl, wakes a sleeping MCU, or enters
its firmware updater. That is a limit of the evidence, not a safety whitelist.

### `upgrade380`: a separate, destructive UART protocol

Original `bin/upgrade380` SHA-256:
`6e2b7fe3e3c9ef1d2f69a31c77b433f164ca2ad25e2e27a68d4ef719d82b7964`.
Its exported `CDownloaderEx` symbols identify the protocol phases directly.
The analyzed MCU update path is `afw_upgrade` (`0x146FC`), using **`/dev/ttyS0`
at 115200 baud**, not the normal SPI `0x40xx` envelope.

Before testing firmware readability, `afw_upgrade` invokes the vendor navigation
stop script. On `Connecting to Bootloader`, callback `0x144B0` sets GPIO 165
high, pulses GPIO 148 low/high, releases that GPIO and changes pin multiplexing
through `devmem`. The accompanying `hold380`/`reset380` scripts identify 148 as
MCU reset. On `Upgrade Complete` the callback waits three seconds and sets 165
low. This is not a passive diagnostic program, even before flash traffic.

| Linux→MCU UART opcode | Payload / phase | MCU→Linux opcode |
| --- | --- | --- |
| `F1` | Four-byte update handshake (`send_code_update_request`, `0x13880`). | `F2`, update acknowledgement. |
| `F3` | Eight bytes from image metadata; starts flash erase after the accepted handshake. | `F4`, erase acknowledgement. |
| `F5` | Empty payload; polls erase status. | `F6`, erase status/progress. |
| `F7` | Packet sequence byte, block-kind byte and block data. | `F8` ACK, `F9` NAK/retransmit handling. |
| `FA` | Empty payload; update-completion phase. | `FB`, completion acknowledgement. |

`set_firmware_data` (`0x13BE8`) copies the eight bytes used for `F3` from
image offsets `+0x14..+0x1B`; their precise MCU interpretation is not established
by the sender. `get_next_data_packet` (`0x13EAC`) constructs `F7` blocks with an
incrementing byte sequence and kinds 0, 1, 2: respectively eight metadata bytes
from image `+0x0C`, eight from `+0x14`, then 16-byte data blocks. The generic
packet sender permits at most 128 payload bytes; this does not mean this build
normally sends 128-byte firmware blocks.

UART framing (`0x136B8`) is start byte `0x6F`, escaped command, escaped one-byte
length, escaped payload and escaped CRC16 low/high bytes. CRC covers command,
length and payload, starts at zero and uses polynomial `0x8005`. Bytes `0x6F`,
`0x48`, `0x80` are escaped as `0x80` followed by byte XOR `0x80`. The receive
parser waits for `0x48`; its jump table at `0x13E00` maps the responses above.
A separate raw `0x70` date-tag request and `0x4A` receive path also exist; they
are not demonstrated to be safe standalone probes of the running MCU.

**Critical boundary:** with a prepared image, a valid `F2` reply causes the
updater to advance automatically to `F3` erase. It is not safe to invoke this
utility merely to test connectivity or query a version. CRC/handshake checks
are protocol checks, not a guarantee that an image is suitable for the robot.
Do not use bootloader entry/reset as a workaround for manual mode or hibernation.

This establishes one actual firmware-writing route; it does **not** prove the
MCU has no SPI reset, calibration, persistent-write or bootloader-entry command.
The Linux receive dispatcher cannot enumerate all commands accepted by MCU
firmware. Unknown SPI IDs remain unclassified, and `0x402E`/`0x4089` must not
be sent in the opposite direction on the strength of these Linux handlers.

Private evidence: `analysis/mcu_control_updater_20261122/` contains selected
assembly, source hashes, selector checks and this analysis. No production code
or robot configuration was changed for this follow-up.

## UART use in `demo_rplidar` (offline follow-up)

For the same Caccamora navigation hash, the traced MCU UART path is primarily
text logging, not a discovered motion-command interface. The default device is
`/dev/ttyS0`, overridable by `DEV_HOSLAM_380_LOG`. Both the startup write and
console-reader setup are conditional on `HOSLAM_SAVE_ESLAM_LOG`; the boolean
helper `0x4BE44` returns false for an absent value (default zero) or string `0`,
and true for another supplied string. This does not establish the live setting.

| Destination | Bytes sent by the inspected path | Trigger / evidence |
| --- | --- | --- |
| MCU UART, default ttyS0, 115200 | `00` (one binary NUL, no newline or framing) | Optional startup at `0x3FBFC`; byte initialized at `0x3FB16`. Serial object is then closed and destroyed. MCU-side purpose unknown. |
| Lidar UART, default ttyS1, 115200 for this startup write | `00` | Startup at `0x3FCD4`, same initialized byte. Device override `DEV_HOSLAM_RADAR`. This is separate from later model-specific serial configuration. |
| Lidar UART, YDLIDAR S2PRO driver | `A5 60` | At `0x15D978`, between two 500 ms waits; source string `laser_ydlidar_s2pro.cc` and log `Resume`. Not a command to the MCU, and not proof this model is selected on Caccamora. |

Writer `0xCE9C4` locks the serial write side and calls `0x186410`, which reaches
`write()` at `0x1865E2`. The static direct-call scan finds the three callers
above; no literal function-pointer copies of either writer entry were found.
This is coverage of that writer, not proof of absence of all computed/indirect
or alternate serial-write routes in the entire program.

The console initializer at `0xDB650` constructs the ttyS0 serial object at
115200 and publishes it at virtual `0x47D5A0`. It launches the `console_r`
thread at `0xDA398`. The thread calls reader `0xCE994 → 0x185118 → read()`
with a maximum of 399 bytes, adds NUL termination when needed and copies text
into Linux logging. A full buffer logs `uart buffer overflow`; the loop targets
roughly 40 ms between iterations (read latency can affect timing). There is no
command parser, opcode dispatch or UART reply in this inspected reader loop.

The strings `++ spi irq wake up` in adjacent functions are formatted Linux log
messages, not demonstrated UART transmit commands. No UART `wake`, `stop`,
manual-direction, erase or firmware-write command was found in the inspected
MCU path. `00` must not be labelled wake, reset or log-enable without MCU-side
evidence. The F1/F3/F5/F7/FA updater protocol belongs to `upgrade380`, not to
this identified navigation path. No robot connection or UART transmission was
made during this analysis.
