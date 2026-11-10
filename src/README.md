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

Extract the whole archive before running it. Open `computer/start-windows.cmd` on Windows or run `sh computer/start-linux.sh` on Linux. The archive contains its own installation guide and file checksum manifest. For the installation flow and file layout, see [installer/BUILD.md](../installer/BUILD.md).

## Historical V1.0 packages

The historical V1.0 binaries were built against glibc. The build and packaging steps above produce new musl-based archives. Use a new release label for them.
