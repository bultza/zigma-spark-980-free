# Mbed TLS SHA256 subset

Unmodified dependency files from Mbed TLS3.6.7, also used by the project Telegram transport. `zigma_crypto_config.h` enables SHA256 only. Sources: `library/sha256.c` and `library/platform_util.c`; included dependency headers are retained. Used for HMAC-SHA256 authentication of local robot command coordination. See LICENSE (Apache-2.0 or GPL-2.0-or-later) and file SPDX notices. The original upstream archive digest/build provenance is recorded in `../../telegram/build-transport.sh`.
