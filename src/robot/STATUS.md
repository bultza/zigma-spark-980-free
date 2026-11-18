# Read robot status with zigmactl

Available in zigmactl V1.8 and later:

```sh
/root/bin/zigmactl status
/root/bin/zigmactl status --json
```

The command reads `/tmp/zigma-free/observation/status.json`, produced by the observer. It does not send native commands, consume IPC messages, contact the web API, or require its token. It can run even when the web agent is unavailable. The observer must be installed and publishing status.

Human output contains state, battery, error, telemetry quality and observation age when known. State/error names are the native identifiers. JSON is a single object with `schema_version: 1` and these fields:

| Field | Meaning |
| --- | --- |
| `available` | A readable, valid status object with a state was found. This does not mean fresh. |
| `working_status`, `error` | Native state/error identifiers; `unknown` when absent. |
| `battery_level` | Percentage, or `null` if unknown/invalid. |
| `clean_time_s`, `clean_area_m2` | Reported cleaning statistics, or `null`. |
| `telemetry_source` | Observer source identifier, or `unknown`. |
| `observed_at`, `server_time` | Unix timestamps; observation may be zero or null when unknown. |
| `observation_age_s` | Seconds since observation, or `null` when unknown/in the future. |
| `fresh` | Observation is at most 30 seconds old, with 1 second of future-clock tolerance. |
| `cache_available` | Observer read the native cache within 15 seconds. |
| `local_link_active` | Cache is recent and local communication was observed within 15 seconds. |
| `telemetry_state` | `fresh`, `cached`, `stale`, `missing`, `clock_mismatch`, or `unavailable`. |

`cached` explicitly does **not** imply `fresh`: a sleeping robot can retain values without issuing a new observation. Consumers requiring current measurements should check `fresh`; never substitute zero for an unknown battery. These freshness windows follow the web status API.

Exit status is 0 for a readable status, including stale/cached data; 1 for unavailable or malformed telemetry; 2 for invalid arguments. `--json` still emits a structured unavailable result on a telemetry error. Argument errors go to stderr. No XML mode is provided.

Offline regression tests:

```sh
python3 -m unittest discover -s installer/tests -p test_ctl_status.py
```
