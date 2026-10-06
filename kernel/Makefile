ARCH ?= x86_64
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

IDENTITY_RUNTIME_DIR := $(ROOT)/services/identity/runtime
IDENTITY_RUNTIME_BIN := $(ROOT)/build/$(ARCH)/identity-runtime/identity-service.bin
IDENTITY_RUNTIME_OBJ := $(BUILD_DIR)/identity-service.bin.o

C_SRC := $(shell find src -type f -name '*.c' | LC_ALL=C sort)
ASM_SRC := $(shell find src -type f -name '*.S' | LC_ALL=C sort)

OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRC)) \
       $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SRC)) \
       $(IDENTITY_RUNTIME_OBJ)

DEP := $(patsubst %.c,$(BUILD_DIR)/%.d,$(C_SRC))

CFLAGS := \
    -target x86_64-unknown-none-elf \
    -std=c11 \
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

$(IDENTITY_RUNTIME_BIN):
	$(MAKE) -C "$(IDENTITY_RUNTIME_DIR)" ARCH=$(ARCH)

$(IDENTITY_RUNTIME_OBJ): $(IDENTITY_RUNTIME_BIN)
	@mkdir -p "$(dir $@)"
	cd "$(dir $(IDENTITY_RUNTIME_BIN))" && \
		$(LD) -r -b binary "$(notdir $(IDENTITY_RUNTIME_BIN))" -o "$(abspath $@)"

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
	rm -rf "$(ROOT)/build/$(ARCH)/kernel" "$(OUTPUT)" "$(BOOT_ASSET)" \
		"$(IDENTITY_BACKGROUND)" "$(IDENTITY_MARK)"
