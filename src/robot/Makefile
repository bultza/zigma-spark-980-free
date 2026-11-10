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
$(OUTPUT)/zigma-area-arm: src/zigma_area.c
$(OUTPUT)/zigma-zones-arm: src/zigma_zones.c $(CJSON)
$(OUTPUT)/zigma-maps-arm: src/zigma_maps.c $(CJSON)
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
