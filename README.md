# Zigma Free

In August 2026, all Zigma Spark-980 robots lost its WiFi connectivity all over the world. During the investigation, we captured a command delivered through the cloud service that disables the Wi-Fi on the robot (an auto destruct command!). A similar loss of app control affected the Redmond RV-R670S in October, when we found expired TLS certificates on the service's HTTPS endpoints. [Skip to how we discovered access](hack/HISTORY.md) if you’re here for the hack.

If you just want to install it on your Zigma (or Redmond, Puron or RoboJet) robots, go directly to the [Installation Manual](installer/README.md)

The Zigma Spark-980 is a robot vacuum cleaner that uses a LiDAR laser scanner to map your home and navigate as it cleans. It returns to its charging dock on its own and was originally controlled through a mobile app that relied on the manufacturer's cloud service.

I started building Zigma Free to get my robot back under my control and make it easier to use. More than anything, I wanted it to clean my house every day on its own again.

With Zigma Free, you can control the vacuum from a web browser on your local network, over SSH, or through a Telegram bot when you're away from home. Everyday control works independently of the manufacturer's cloud. On supported navigation profiles, the live LiDAR view shows what the robot's laser scanner detects.

Daily cleaning schedules run on the robot's own clock, so they work even without Wi-Fi. If the robot loses track of time after being powered off, set the clock again so cleaning starts at the right time.

![Zigma Free desktop dashboard showing the robot controls and map](images/zigma-free-dashboard-desktop.png)

## Model compatibility

The installer will be able to access and expose the SSH of your robots for these models, however the "Free-Zigma" app will depend on the status of the project.

### Fully compatible models
- **Zigma Spark-980** - Tested in three different software variants (in which partition tables or even the way of handling the memory is different).

### Partially compatible models

- **Puron PR10** - Currently under testing
- **RoboJet X-One (Issue08)** - Exact-profile support, tested using its adapted firmware on Ciccia. Maps, map-derived position, settings, area commands and general no-go polygons are implemented; live LiDAR and navigation-memory pose are not enabled. See the [support matrix](hack/ROBOJET_ISSUE08_SUPPORT.md).

### Potentially compatible models

- **Redmond RV-R670S** - I need beta testers so please try the installer and report with an issue!
- **Redmond RV-R650S** - It has not lidar but it should be possible to at least get a SSH terminal.

## Firmware variants and RC16 findings

Compatibility is selected by flash layout and exact component hashes, not by the
model name alone. The **Issue10** direct kernel16 profile is included, with its
reviewed startup scripts and existing Zigma navigation/app pair. Its original
hardware installation and cold boot still need owner validation. Unknown or mixed
components retain the installer fallback rather than inheriting another profile's
permissions. See [firmware variants](hack/FIRMWARE_VARIANTS.md).

RC17 recognizes both the original Issue08 RoboJet (kernel #16) and its Ciccia
adaptation (kernel #23), using separate exact component profiles. The original
profile was checked against the received NOR; installation and cold boot on the
original hardware still require owner validation. Issue10 support is preserved.

RoboJet uses a different app-cache layout and `relo` v2 saved maps. RC16 includes
the bounded v2 decoder, byte-preserving protected copies, map-derived robot
coordinates, general no-go polygon editing, area commands, and area/time statistics
(m² and minutes, converted to seconds in the API). Nonzero physical measurements
are still needed to verify the statistics' scale. Navigation RAM and live LiDAR
remain unavailable for this exact executable.

Native restoration was verified with the same map and with a converted Caccamora
map using another ID. Both imports returned native success and published their
maps. The converted map's file and selected ID survived reboot, but a usable live
map was not republished automatically. Full reload/localization and reliable
restoration after standby therefore remain unvalidated. A queued request is not
completion; keep protected backups and do not automatically repeat an uncertain
operation. The conversion is a private test artifact, not a supported cross-model
map converter. See [native map import and validation](hack/ROBOJET_MAP_IMPORT.md).

RC16 also applies the web-selected timezone to native processes at the next boot.
Already-running processes can retain their previous timezone until then. Every new
firmware variant must be audited for timezone overrides; the RoboJet override must
not be assumed to exist on other variants.

## Features

The web interface and control software run on the robot itself. The original navigation software still handles driving and cleaning. Telegram is optional and requires an internet connection.

- Run weekly cleaning schedules locally using cron.
- Start, pause, or resume cleaning, or send the robot back to its dock.
- View the current map, manage protected saved-map copies, and see live LiDAR readings where the firmware profile supports them.
- Adjust voice volume and manage voice prompts.
- Access the robot over SSH or control it remotely through Telegram.

![Animated demo of the Zigma Free web interface](images/video_usage.gif)

## Zigma Spark-980 hardware
During the investigation we used 3 different robots, the three of them with different versions and in at least one, a totally different linux partition (when the OS is different, I call them "variants"). This makes it particularly difficult because even if they use the same hardware, in some of the robots the internal NOR memory has a permanent flag applied that makes its linux kernel not compatible other robots.

The robot has two main PCBs. The **AM580 main logic board** runs Linux, and the **AM380 chassis controller board** handles low-level chassis control.

- **Processor and RAM:** Amicro AM580 (`amicro,am580`). and 64 MiB of DRAM.
- **Storage:** 32 MiB (256 Mbit) of Macronix SPI NOR flash, identified as MX25L25635F/MX25L25645G.
- **Wi-Fi:** 2.4 GHz Wi-Fi via SDIO. The stock Linux kernel uses the Realtek `rtl8189fs` driver.
- **UART:** The main board exposes a 3.3 V TTL serial console at 115200 baud, 8N1, with no flow control. The stock software takes over the console during boot.

![Both sides of the AM580 main logic board and AM380 chassis controller board](images/zigma-spark-980-boards.jpg)

*AM580 main logic board in the top row and AM380 chassis controller board in the bottom row. Top views are on the left and bottom views on the right.*

## How to install

> [!WARNING]
> Installing Zigma Free can leave your robot unable to boot or operate. The risk appears low on supported firmwares as long as you follow the guide, but a mistake or a power interruption at the wrong time can still brick the robot. Check your firmware, keep the robot powered during installation, and stop if the wizard cannot verify the flash backup. If the NOR backup fails, you can try again. It is strange to fail 3 times in a row but it can happen. Proceed only if you are prepared to recover the robot.

On a Windows or Linux computer with Python 3.10 or later, download and extract the [latest release package](https://github.com/bultza/zigma-spark-980-free/releases). Then follow the detailed instructions from here: [Install Zigma Free](installer/README.md)

> [!WARNING]
> Building from source is an advanced option. For most users, we recommend the release package above. If you want to build it yourself, follow the [manual build instructions](src/README.md).

## Instructions and troubleshooting

See [Instructions and troubleshooting](USAGE.md) for how to use your brand new robot, how to add SSH public keys after installation and recover access if you move house or lose your old computer.

## How we discovered access

We started with the UART console. It let us watch the robot boot, but the factory debug server took over the serial port before we could get a shell. So we wired a Raspberry Pi Pico to the SPI flash, read the memory, and modified the writable partition to start the robot's built-in SSH server.

Later, we worked out how to unlock the UART console and found an easier way to get SSH without opening the robot. The factory Wi-Fi pairing process could download and run a script from a computer on the local network. Once we had shell access, we captured the cloud command that deleted the robot's Wi-Fi settings. We called it the “auto-destruct” command.

[How we hacked it](hack/HISTORY.md) tells the full story, from the wires on the bench to local control.

![Raspberry Pi Pico wired to the Spark-980 main logic board to read its SPI flash](images/zigma_NOR_Memory_dump_messy.jpg)

## Plan for the future

The long-term goal is to replace the factory operating system and proprietary binaries with an open-source software stack that can run the robot independently.

The table tracks the main factory executables and their replacement status in Zigma Free local mode.

| Factory executable | What it does | Status |
| --- | --- | --- |
| ~~`ygclient`~~ | Sets up Wi-Fi and cloud registration, translates commands and telemetry, publishes maps and cleaning history, and handles cloud updates. Its replacement needs to cover the local functions that the rest of the robot expects. | Replaced in local mode by the Zigma Free agent and network services. |
| `wifi_app_server_grit` | Interprets high-level commands and forwards them to the navigation process. It also handles settings, voice prompts, and reports about robot state and maps. | Still in use. |
| `demo_rplidar` | Runs the HOSLAM navigation stack. It processes LiDAR scans, builds maps, estimates the robot's position, and handles navigation and calibration. | Still in use. |
| ~~`daemon`~~ | Monitors and restarts factory processes and coordinates recovery and reboot operations. | Replaced in local mode by the Zigma Free supervisor. |
| `upgrade380` | Updates the firmware on the AM380 chassis controller. | Replacement pending. |
| `factory_mode` | Provides factory test and service functions. It also participates in factory update handling. | Replacement pending. |
| `rescue` | Provides a factory recovery path that the supervisor can launch. It also participates in factory update handling. | Replacement pending. |

Zigma Free disables both factory executables (ygclient and daemon) in local mode.

The Linux boot stack is a separate part of the roadmap. It includes U-Boot, the kernel, the device tree, and the Buildroot userspace. The AM380 controller also has its own firmware, separate from the Linux executables. Replacing that firmware requires documenting how the controller talks to the main board and validating the resulting motor and sensor behavior on the robot.

The plan is to replace components in stages and test cleaning, mapping, docking, and recovery after each major change.

## Extra Guides
- [Manual hack](hack/MANUAL-HACK.md) explains how to enable and verify SSH access on your own robot.

## License

Zigma Free's original software is available under the [MIT License](LICENSE). Third-party components retain their own licences. See the notices for [cJSON](src/robot/vendor/cjson/LICENSE) and [Mbed TLS](src/robot/vendor/mbedtls/LICENSE). New deployment archives also include the [third-party notices](src/robot/vendor/notices/README.md) for musl, the GCC runtime, and the Telegram transport and CA bundle.
