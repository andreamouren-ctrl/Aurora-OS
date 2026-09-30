ARCH ?= x86_64
CC := clang

ROOT := ..
BUILD_DIR := $(ROOT)/build/$(ARCH)/kernel
OUTPUT := $(ROOT)/build/$(ARCH)/aurora-kernel.elf
LIMINE_PROTOCOL := $(ROOT)/third_party/limine-protocol

SRC := $(shell find src -type f -name '*.c' | LC_ALL=C sort)
OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

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

$(OUTPUT): $(OBJ) $(ROOT)/linker/x86_64.ld
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -o "$@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -c "$<" -o "$@"

-include $(DEP)

clean:
	rm -rf "$(ROOT)/build/$(ARCH)/kernel" "$(OUTPUT)"
