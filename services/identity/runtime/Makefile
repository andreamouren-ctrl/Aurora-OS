ARCH ?= x86_64
CC := clang
OBJCOPY ?= objcopy

ROOT := ../../..
BUILD_DIR := $(ROOT)/build/$(ARCH)/identity-runtime
KERNEL_INCLUDE := $(ROOT)/kernel/include
IDENTITY_INCLUDE := $(ROOT)/services/identity/include
IDENTITY_SRC := $(ROOT)/services/identity/src
RUNTIME_INCLUDE := include
ELF := $(BUILD_DIR)/identity-service.elf
BIN := $(BUILD_DIR)/identity-service.bin
C_OBJECT := $(BUILD_DIR)/main.o
MEMORY_OBJECT := $(BUILD_DIR)/memory.o
PROTECTED_STATE_TRANSPORT_OBJECT := $(BUILD_DIR)/protected_state_transport.o
PERSISTENT_STORE_OBJECT := $(BUILD_DIR)/persistent_store.o
PERSISTENT_STORE_PROTECTED_STATE_OBJECT := $(BUILD_DIR)/persistent_store_protected_state.o
ENTRY_OBJECT := $(BUILD_DIR)/entry.o

CPPFLAGS := -I$(RUNTIME_INCLUDE) -I$(KERNEL_INCLUDE) -I$(IDENTITY_INCLUDE)
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

$(C_OBJECT): main.c protected_state_transport.h $(KERNEL_INCLUDE)/aurora/capability_abi.h $(KERNEL_INCLUDE)/aurora/identity_service_protocol.h $(KERNEL_INCLUDE)/aurora/service_abi.h $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(IDENTITY_INCLUDE)/aurora/identity/persistent_store.h $(IDENTITY_INCLUDE)/aurora/identity/persistent_store_protected_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c main.c -o $@

$(MEMORY_OBJECT): memory.c $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c memory.c -o $@

$(PROTECTED_STATE_TRANSPORT_OBJECT): protected_state_transport.c protected_state_transport.h $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(IDENTITY_INCLUDE)/aurora/identity/protected_state_transport.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c protected_state_transport.c -o $@

$(PERSISTENT_STORE_OBJECT): $(IDENTITY_SRC)/persistent_store.c $(IDENTITY_INCLUDE)/aurora/identity/persistent_store.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/persistent_store.c -o $@

$(PERSISTENT_STORE_PROTECTED_STATE_OBJECT): $(IDENTITY_SRC)/persistent_store_protected_state.c $(IDENTITY_INCLUDE)/aurora/identity/persistent_store_protected_state.h $(IDENTITY_INCLUDE)/aurora/identity/protected_state_transport.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/persistent_store_protected_state.c -o $@

$(ENTRY_OBJECT): entry.S | $(BUILD_DIR)
	$(CC) $(ASMFLAGS) -c entry.S -o $@

$(ELF): $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) $(PROTECTED_STATE_TRANSPORT_OBJECT) $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_PROTECTED_STATE_OBJECT) linker.ld
	$(CC) $(LDFLAGS) $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) $(PROTECTED_STATE_TRANSPORT_OBJECT) $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_PROTECTED_STATE_OBJECT) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)
