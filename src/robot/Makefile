export TZ := UTC

SDK := $(abspath build/toolchain/armv5-eabi--musl--stable-2026.08-1)
ARM_PREFIX := $(SDK)/bin/arm-buildroot-linux-musleabi
OUTPUT := build/musl
CFLAGS ?= -Os -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -Ivendor/cjson -DCJSON_NESTING_LIMIT=32 \
            -Ivendor/mbedtls/include -Ivendor/mbedtls \
            -DMBEDTLS_CONFIG_FILE=\"zigma_crypto_config.h\"
CJSON := vendor/cjson/cJSON.c
CRYPTO := vendor/mbedtls/library/sha256.c vendor/mbedtls/library/platform_util.c
HEADERS := $(wildcard src/*.h vendor/cjson/*.h vendor/mbedtls/*.h \
                       vendor/mbedtls/include/mbedtls/*.h vendor/mbedtls/library/*.h)

COMPONENTS := free-zigma-agent-arm zigma-observe-arm zigma-core-arm zigma-network-arm \
              zigma-area-arm zigma-zones-arm zigma-maps-arm zigma-options-arm zigmactl
ARM_BINARIES := $(addprefix $(OUTPUT)/,$(COMPONENTS))

.PHONY: all $(COMPONENTS) zigma-nor-backup-arm
all: $(ARM_BINARIES)

$(COMPONENTS) zigma-nor-backup-arm: %: $(OUTPUT)/%

$(OUTPUT)/free-zigma-agent-arm: private CPPFLAGS += -DNOR_EMBED
$(OUTPUT)/free-zigma-agent-arm: src/zigma_agent.c src/zigma_nor_backup.c $(CJSON) $(CRYPTO)
$(OUTPUT)/zigma-observe-arm: src/zigma_observe.c $(CJSON) $(CRYPTO)
$(OUTPUT)/zigma-core-arm: src/zigma_core.c src/zigma_local_ipc.c $(CJSON) $(CRYPTO)
$(OUTPUT)/zigma-network-arm: src/zigma_network.c
$(OUTPUT)/zigma-area-arm: src/zigma_area.c $(CRYPTO)
$(OUTPUT)/zigma-zones-arm: src/zigma_zones.c $(CJSON) $(CRYPTO)
$(OUTPUT)/zigma-maps-arm: src/zigma_maps.c $(CJSON) $(CRYPTO)
$(OUTPUT)/zigma-options-arm: src/zigma_options.c
$(OUTPUT)/zigmactl: src/zigmactl.c $(CJSON)
$(OUTPUT)/zigma-nor-backup-arm: src/zigma_nor_backup.c $(CRYPTO)

$(ARM_BINARIES) $(OUTPUT)/zigma-nor-backup-arm: $(HEADERS) src/zigma_versions_generated.h Makefile $(SDK)/.ready
	mkdir -p $(@D)
	"$(ARM_PREFIX)-gcc" $(CPPFLAGS) $(CFLAGS) -static $(filter %.c,$^) -lm -o $@

src/zigma_versions_generated.h: versions.json tools/generate-version-catalog.py
	python3 tools/generate-version-catalog.py

.PHONY: transport
transport: $(SDK)/.ready
	sh telegram/build-transport.sh $(OUTPUT)/transport "$(ARM_PREFIX)"

$(SDK)/.ready: tools/install-arm-toolchain.sh
	sh tools/install-arm-toolchain.sh build/toolchain

# Host-only regression test: synthetic process memory, no robot access.
HOST_CC ?= cc
.PHONY: test-puron-settings
test-puron-settings:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_puron_settings.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-puron-settings
	build/tests/test-puron-settings

.PHONY: test-navigation
test-navigation:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_navigation.c $(CJSON) -lm -o build/tests/test-navigation
	build/tests/test-navigation

.PHONY: test-puron-zones
test-puron-zones:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_puron_zones.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-puron-zones
	build/tests/test-puron-zones

.PHONY: test-zone-commands
test-zone-commands:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_zone_commands.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-zone-commands
	build/tests/test-zone-commands

.PHONY: test-map-formats
test-map-formats:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_map_formats.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-map-formats
	build/tests/test-map-formats

.PHONY: test-map-native
test-map-native:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_map_native.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-map-native
	build/tests/test-map-native

.PHONY: test-navigation-profiles
test-navigation-profiles:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_navigation_profiles.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-navigation-profiles
	build/tests/test-navigation-profiles

.PHONY: test-app-profiles
test-app-profiles:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_app_profiles.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-app-profiles
	build/tests/test-app-profiles

.PHONY: test-robojet
test-robojet:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_robojet.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-robojet
	build/tests/test-robojet

.PHONY: test-setfan
test-setfan:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_setfan.c $(CJSON) -lm -o build/tests/test-setfan
	build/tests/test-setfan

.PHONY: test-release
test-release:
	mkdir -p build/tests
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -DNOR_EMBED tests/test_release.c src/zigma_nor_backup.c $(CJSON) $(CRYPTO) -lm -o build/tests/test-release
	build/tests/test-release
