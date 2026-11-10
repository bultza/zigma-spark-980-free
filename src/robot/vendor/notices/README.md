# Third-party notices for the deployment

Zigma Free's own code is MIT-licensed. The deployment contains these third-party components under their respective terms.

| Component | Notice and source |
| --- | --- |
| musl 1.2.6 | [Copyright and licence](musl-COPYRIGHT). The deployment includes the [upstream source archive](https://musl.libc.org/releases/musl-1.2.6.tar.gz) with per-file notices. |
| GCC runtime code | [GPL v3](GPL-3.0) with the [GCC Runtime Library Exception](GCC-RUNTIME-EXCEPTION) where stated in the runtime source. [Bootlin toolchain sources and build information](https://toolchains.bootlin.com/releases_armv5-eabi.html). |
| cJSON | MIT. The full notice is in `robot/vendor/cjson/LICENSE`. |
| Mbed TLS | Apache-2.0 option of the upstream dual licence. The full notice is in `robot/vendor/mbedtls/LICENSE`. The Telegram transport's Mbed TLS notice is in `robot/build/telegram-licenses.txt`. |
| curl | The curl notice is in `robot/build/telegram-licenses.txt`. The transport build pins the upstream sources and documents its local version-reporting patch. |
| Mozilla CA certificates | The PEM conversion in `robot/build/telegram-ca.pem` is under [MPL 2.0](MPL-2.0). Its matching Mozilla source is included as `certdata.txt` in this directory in the deployment archive. [curl CA Extract](https://curl.se/ca/). |

The ARM binaries are built with the Bootlin ARMv5 EABI musl SDK, release `2026.08-1`. The SDK itself is obtained separately.
