ARCH ?= x86_64
BOOT_VALIDATION ?= 0
NVME_RW_PROBE_ONLY ?= 0
CC := clang
LD := ld
PYTHON ?= python3

ROOT := ..
BUILD_DIR := $(ROOT)/build/$(ARCH)/kernel
OUTPUT := $(ROOT)/build/$(ARCH)/aurora-kernel.elf
LIMINE_PROTOCOL := $(ROOT)/third_party/limine-protocol

BOOT_ASSET_SOURCES := $(sort $(wildcard assets/boot/hd/part*.b64))
BOOT_ASSET := assets/boot/aurora_boot.idx8
BOOT_ASSET_TOOL := $(ROOT)/scripts/prepare-boot-asset.py
BOOT_ASSET_OBJ := $(BUILD_DIR)/src/ui/boot_asset.o

IDENTITY_BACKGROUND_SOURCES := $(sort $(wildcard assets/identity/background/part*.b64))
IDENTITY_MARK_SOURCES := $(sort $(wildcard assets/identity/mark/part*.b64))
IDENTITY_BACKGROUND := assets/identity/aurora_identity_background.png
IDENTITY_MARK := assets/identity/aurora_mark.png
IDENTITY_ASSET_TOOL := $(ROOT)/scripts/prepare-identity-assets.py
IDENTITY_ASSET_OBJ := $(BUILD_DIR)/src/ui/identity_asset.o

SECURITY_ACTIVITY_SOURCE_DIR := assets/identity/security_activity
SECURITY_ACTIVITY_RUNTIME_DIR := $(SECURITY_ACTIVITY_SOURCE_DIR)/runtime
SECURITY_ACTIVITY_ASSET_TOOL := $(ROOT)/scripts/prepare-security-activity-runtime-assets.py
SECURITY_ACTIVITY_SOURCE_FILES := \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_panel.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_event_card.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_status_info.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_status_warning.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_status_critical.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_category_auth.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_category_session.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_category_credential.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_header.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_empty.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_error.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_loading.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_load_more.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_filter_button.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_filter_active.png \
	$(SECURITY_ACTIVITY_SOURCE_DIR)/aurora_security_activity_end.png
SECURITY_ACTIVITY_RUNTIME_FILES := $(patsubst $(SECURITY_ACTIVITY_SOURCE_DIR)/%,$(SECURITY_ACTIVITY_RUNTIME_DIR)/%,$(SECURITY_ACTIVITY_SOURCE_FILES))
SECURITY_ACTIVITY_ASSET_OBJ := $(BUILD_DIR)/src/ui/security_activity_asset.o

COLOR_LUT_TOOL := $(ROOT)/scripts/generate-color-luts.py
COLOR_LUT_HEADER := include/aurora/color_luts_generated.h

CELESTIA_GENERATOR := $(ROOT)/scripts/generate-celestia-atlas.py
CELESTIA_FONT_SOURCES := \
    $(ROOT)/assets/fonts/aurora-celestia/ui/v1.0/AuroraCelestiaUI-Regular.ttf \
    $(ROOT)/assets/fonts/aurora-celestia/ui/v1.0/AuroraCelestiaUI-Bold.ttf
CELESTIA_ATLAS_HEADER := include/aurora/celestia_atlas_generated.h

IDENTITY_RUNTIME_DIR := $(ROOT)/services/identity/runtime
IDENTITY_RUNTIME_BIN := $(ROOT)/build/$(ARCH)/identity-runtime/identity-service.bin
IDENTITY_RUNTIME_OBJ := $(BUILD_DIR)/identity-service.bin.o

SESSION_RUNTIME_DIR := $(ROOT)/services/session/runtime
SESSION_RUNTIME_BIN := $(ROOT)/build/$(ARCH)/session-runtime/session-manager.bin
SESSION_RUNTIME_OBJ := $(BUILD_DIR)/session-manager.bin.o

USER_SESSION_RUNTIME_DIR := $(ROOT)/services/user_session/runtime
USER_SESSION_RUNTIME_BIN := $(ROOT)/build/$(ARCH)/user-session-runtime/user-session-host.bin
USER_SESSION_RUNTIME_OBJ := $(BUILD_DIR)/user-session-host.bin.o

C_SRC := $(shell find src -type f -name '*.c' | LC_ALL=C sort)
ASM_SRC := $(shell find src -type f -name '*.S' | LC_ALL=C sort)

OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRC)) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRC)) \
       $(IDENTITY_RUNTIME_OBJ) \
       $(SESSION_RUNTIME_OBJ) \
       $(USER_SESSION_RUNTIME_OBJ)

DEP := $(patsubst %.c,$(BUILD_DIR)/%.d,$(C_SRC))

CFLAGS := \
    -target x86_64-unknown-none-elf \
    -std=c11 \
    -DAURORA_BOOT_VALIDATION=$(BOOT_VALIDATION) \
    -DAURORA_NVME_RW_PROBE_ONLY=$(NVME_RW_PROBE_ONLY) \
    -O2 \
    -g \
    -Wall \
    -Wextra \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-pic \
    -fno-pie \
    -fno-common \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mcmodel=kernel \
    -Iinclude \
    -I$(LIMINE_PROTOCOL)/include \
    -MMD \
    -MP

LDFLAGS := \
    -target x86_64-unknown-none-elf \
    -fuse-ld=lld \
    -nostdlib \
    -static \
    -no-pie \
    -Wl,--build-id=sha1 \
    -Wl,-z,max-page-size=0x1000 \
    -Wl,-z,noexecstack \
    -Wl,--gc-sections \
    -Wl,-T,$(ROOT)/linker/x86_64.ld

.PHONY: all clean

all: $(OUTPUT)

$(COLOR_LUT_HEADER): $(COLOR_LUT_TOOL)
	$(PYTHON) "$(COLOR_LUT_TOOL)" --output "$@"

$(BUILD_DIR)/src/graphics/color_management.o: $(COLOR_LUT_HEADER)

$(CELESTIA_ATLAS_HEADER): $(CELESTIA_GENERATOR) $(CELESTIA_FONT_SOURCES)
	cd "$(ROOT)" && $(PYTHON) scripts/generate-celestia-atlas.py \
		--fonts assets/fonts/aurora-celestia/ui/v1.0 \
		--output kernel/include/aurora/celestia_atlas_generated.h

$(BUILD_DIR)/src/ui/system_font.o: $(CELESTIA_ATLAS_HEADER)

$(BOOT_ASSET): $(BOOT_ASSET_SOURCES) $(BOOT_ASSET_TOOL)
	@test -n "$(BOOT_ASSET_SOURCES)" || (echo "No HD boot asset chunks found" >&2; exit 1)
	$(PYTHON) "$(BOOT_ASSET_TOOL)" \
		--input $(BOOT_ASSET_SOURCES) \
		--output "$@"

$(BOOT_ASSET_OBJ): $(BOOT_ASSET)

$(IDENTITY_BACKGROUND): $(IDENTITY_BACKGROUND_SOURCES) $(IDENTITY_MARK_SOURCES) $(IDENTITY_ASSET_TOOL)
	@test -n "$(IDENTITY_BACKGROUND_SOURCES)" || (echo "No Identity background asset chunks found" >&2; exit 1)
	@test -n "$(IDENTITY_MARK_SOURCES)" || (echo "No Identity mark asset chunks found" >&2; exit 1)
	$(PYTHON) "$(IDENTITY_ASSET_TOOL)" \
		--background-input $(IDENTITY_BACKGROUND_SOURCES) \
		--mark-input $(IDENTITY_MARK_SOURCES) \
		--background-output "$(IDENTITY_BACKGROUND)" \
		--mark-output "$(IDENTITY_MARK)"

$(IDENTITY_MARK): $(IDENTITY_BACKGROUND)

$(IDENTITY_ASSET_OBJ): $(IDENTITY_BACKGROUND) $(IDENTITY_MARK)

$(SECURITY_ACTIVITY_RUNTIME_DIR)/aurora_security_activity_panel.png: $(SECURITY_ACTIVITY_SOURCE_FILES) $(SECURITY_ACTIVITY_ASSET_TOOL)
	$(PYTHON) "$(SECURITY_ACTIVITY_ASSET_TOOL)" \
		--source-dir "$(SECURITY_ACTIVITY_SOURCE_DIR)" \
		--output-dir "$(SECURITY_ACTIVITY_RUNTIME_DIR)"

$(filter-out $(SECURITY_ACTIVITY_RUNTIME_DIR)/aurora_security_activity_panel.png,$(SECURITY_ACTIVITY_RUNTIME_FILES)): $(SECURITY_ACTIVITY_RUNTIME_DIR)/aurora_security_activity_panel.png

$(SECURITY_ACTIVITY_ASSET_OBJ): $(SECURITY_ACTIVITY_RUNTIME_FILES)

$(IDENTITY_RUNTIME_BIN):
	$(MAKE) -C "$(IDENTITY_RUNTIME_DIR)" ARCH=$(ARCH)

$(IDENTITY_RUNTIME_OBJ): $(IDENTITY_RUNTIME_BIN)
	@mkdir -p "$(dir $@)"
	cd "$(dir $(IDENTITY_RUNTIME_BIN))" && \
		$(LD) -r -b binary "$(notdir $(IDENTITY_RUNTIME_BIN))" -o "$(abspath $@)"

$(SESSION_RUNTIME_BIN):
	$(MAKE) -C "$(SESSION_RUNTIME_DIR)" ARCH=$(ARCH)

$(SESSION_RUNTIME_OBJ): $(SESSION_RUNTIME_BIN)
	@mkdir -p "$(dir $@)"
	cd "$(dir $(SESSION_RUNTIME_BIN))" && \
		$(LD) -r -b binary "$(notdir $(SESSION_RUNTIME_BIN))" -o "$(abspath $@)"

$(USER_SESSION_RUNTIME_BIN):
	$(MAKE) -C "$(USER_SESSION_RUNTIME_DIR)" ARCH=$(ARCH)

$(USER_SESSION_RUNTIME_OBJ): $(USER_SESSION_RUNTIME_BIN)
	@mkdir -p "$(dir $@)"
	cd "$(dir $(USER_SESSION_RUNTIME_BIN))" && \
		$(LD) -r -b binary "$(notdir $(USER_SESSION_RUNTIME_BIN))" -o "$(abspath $@)"

$(OUTPUT): $(OBJ) $(ROOT)/linker/x86_64.ld
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o "$@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/%.o: %.S
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c "$<" -o "$@"

-include $(DEP)

clean:
	$(MAKE) -C "$(IDENTITY_RUNTIME_DIR)" ARCH=$(ARCH) clean
	$(MAKE) -C "$(SESSION_RUNTIME_DIR)" ARCH=$(ARCH) clean
	$(MAKE) -C "$(USER_SESSION_RUNTIME_DIR)" ARCH=$(ARCH) clean
	rm -rf "$(ROOT)/build/$(ARCH)/kernel" "$(OUTPUT)" "$(BOOT_ASSET)" \
		"$(IDENTITY_BACKGROUND)" "$(IDENTITY_MARK)" "$(COLOR_LUT_HEADER)" \
		"$(CELESTIA_ATLAS_HEADER)" \
		"$(SECURITY_ACTIVITY_RUNTIME_DIR)"
