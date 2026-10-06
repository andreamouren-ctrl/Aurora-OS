ARCH ?= x86_64
CC := clang
OBJCOPY ?= objcopy

ROOT := ../../..
BUILD_DIR := $(ROOT)/build/$(ARCH)/identity-runtime
KERNEL_INCLUDE := $(ROOT)/kernel/include
ELF := $(BUILD_DIR)/identity-service.elf
BIN := $(BUILD_DIR)/identity-service.bin
C_OBJECT := $(BUILD_DIR)/main.o
MEMORY_OBJECT := $(BUILD_DIR)/memory.o
ENTRY_OBJECT := $(BUILD_DIR)/entry.o

CPPFLAGS := -I$(KERNEL_INCLUDE)
CFLAGS := \
    -target x86_64-unknown-none-elf \
    -std=c11 \
    -O2 \
    -Wall \
    -Wextra \
    -Werror \
    -ffreestanding \
    -fno-builtin \
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
    -mno-sse2

ASMFLAGS := \
    -target x86_64-unknown-none-elf \
    -ffreestanding \
    -fno-pic \
    -fno-pie \
    -m64 \
    -march=x86-64 \
    -mno-red-zone

LDFLAGS := \
    -target x86_64-unknown-none-elf \
    -fuse-ld=lld \
    -nostdlib \
    -static \
    -Wl,--build-id=none \
    -Wl,--gc-sections \
    -Wl,-z,max-page-size=0x1000 \
    -Wl,-T,linker.ld

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(C_OBJECT): main.c $(KERNEL_INCLUDE)/aurora/capability_abi.h $(KERNEL_INCLUDE)/aurora/identity_service_protocol.h $(KERNEL_INCLUDE)/aurora/service_abi.h $(KERNEL_INCLUDE)/aurora/syscall_abi.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c main.c -o $@

$(MEMORY_OBJECT): memory.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c memory.c -o $@

$(ENTRY_OBJECT): entry.S | $(BUILD_DIR)
	$(CC) $(ASMFLAGS) -c entry.S -o $@

$(ELF): $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) linker.ld
	$(CC) $(LDFLAGS) $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)
