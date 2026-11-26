# Zigma Free v1.1-rc22

## Changes from RC20 v2

- Adds the strict ABIR X8 Issue12 firmware profile, with 23 required component hashes and a reviewed numeric calibration template. Existing adapters are reused; vendor firmware and private dumps are not distributed.
- Includes agent V1.38 and the existing independent web fan selection and local cleaning-report history changes.
- Adds `zigmactl wake` and `wake --check` in V1.23. Wake temporarily overlays madplay with a RAM stub, sends clean once at Quiet power, and dispatches pauses at +2s and +3.5s. Confirmation is observation-only: no delayed pause at timeout. A guardian handles caller loss and restores audio. `--check` sends no robot command.
- Shares the control lock with navigation and clean/resume/setfan/dock/reboot. Emergency pause remains available.

Wake is experimental. The owner reports V1.23 working well on Caccamora. The earlier shell prototype worked on Ciccia; integrated wake is not established on other variants. Software dispatch times do not guarantee MCU response times, silence or absence of brief movement. See [Wake implementation and limits](WAKE.md).

## Validation and scope

- All 148 installer tests pass using the existing wizard Python environment.
- Wake state-machine and host process tests cover missed first pause, intermediate Standby, stale/missing observations, command failures, independent stop deadlines, concurrent calls and caller SIGKILL recovery. Mount and IPC operations are simulated in the process tests.
- Navigation, fan and CLI status regressions pass. All ARM components were rebuilt against the current version catalogue; rebuilt bytes do not imply new behavior in every component.
- All 494 package manifest hashes pass. ZIP and TAR contain identical files. The packaged zigmactl SHA-256 is `3752efc595002f0d66ca2b5cc9d415259eeac81b946ad23c96e8534d01bcf849`, matching the owner-tested Caccamora binary.
- A previous RC22 candidate passed reconstructed ABIR clean Full installation, activation and cold boot in QEMU. RC20 v2 cannot provide that profile's Full upgrade baseline. That result is historical: the regenerated artifacts have not undergone a new QEMU campaign or a complete variant matrix.

RC22 is a local candidate, not a published release. No tag, push or release publication is performed by this regeneration. The old standalone wake scripts were removed from Ciccio/Caccamora; Ciccia's temporary copies were already absent after its power cycle. Removing them does not upgrade Ciccio or Ciccia to V1.23.
