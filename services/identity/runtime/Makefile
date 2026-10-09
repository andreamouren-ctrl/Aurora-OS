ARCH ?= x86_64
CC := clang
OBJCOPY ?= objcopy

ROOT := ../../..
BUILD_DIR := $(ROOT)/build/$(ARCH)/identity-runtime
KERNEL_INCLUDE := $(ROOT)/kernel/include
IDENTITY_INCLUDE := $(ROOT)/services/identity/include
IDENTITY_SRC := $(ROOT)/services/identity/src
ARGON2_VENDOR_DIR := $(ROOT)/services/identity/vendor/argon2
ARGON2_VENDOR_INCLUDE := $(ARGON2_VENDOR_DIR)/include
ARGON2_VENDOR_SRC := $(ARGON2_VENDOR_DIR)/src
RUNTIME_INCLUDE := include
ELF := $(BUILD_DIR)/identity-service.elf
BIN := $(BUILD_DIR)/identity-service.bin
C_OBJECT := $(BUILD_DIR)/main.o
MEMORY_OBJECT := $(BUILD_DIR)/memory.o
PROTECTED_STATE_TRANSPORT_OBJECT := $(BUILD_DIR)/protected_state_transport.o
CORE_OBJECT := $(BUILD_DIR)/identity_core.o
CREATION_ACCESS_POLICY_OBJECT := $(BUILD_DIR)/creation_access_policy.o
BOOTSTRAP_CREATE_GATE_OBJECT := $(BUILD_DIR)/bootstrap_create_gate.o
SESSION_GRANT_OBJECT := $(BUILD_DIR)/session_grant.o
SESSION_GRANT_MEMORY_OBJECT := $(BUILD_DIR)/session_grant_memory.o
REAUTH_PROOF_OBJECT := $(BUILD_DIR)/reauth_proof.o
REAUTH_PROOF_MEMORY_OBJECT := $(BUILD_DIR)/reauth_proof_memory.o
CRYPTO_FOUNDATION_OBJECT := $(BUILD_DIR)/crypto_foundation.o
CRYPTO_PROVIDER_OBJECT := $(BUILD_DIR)/crypto_provider.o
ARGON2ID_PROVIDER_OBJECT := $(BUILD_DIR)/argon2id_provider.o
ARGON2_REF_ARGON2_OBJECT := $(BUILD_DIR)/argon2_ref_argon2.o
ARGON2_REF_CORE_OBJECT := $(BUILD_DIR)/argon2_ref_core.o
ARGON2_REF_REF_OBJECT := $(BUILD_DIR)/argon2_ref_ref.o
ARGON2_REF_BLAKE2B_OBJECT := $(BUILD_DIR)/argon2_ref_blake2b.o
ARGON2_REF_OBJECTS := $(ARGON2_REF_ARGON2_OBJECT) $(ARGON2_REF_CORE_OBJECT) $(ARGON2_REF_REF_OBJECT) $(ARGON2_REF_BLAKE2B_OBJECT)
MACHINE_SECRET_OBJECT := $(BUILD_DIR)/machine_secret.o
MACHINE_SECRET_CODEC_OBJECT := $(BUILD_DIR)/machine_secret_codec.o
MACHINE_SECRET_PROTECTED_STATE_OBJECT := $(BUILD_DIR)/machine_secret_protected_state.o
PERSISTENT_STORE_OBJECT := $(BUILD_DIR)/persistent_store.o
PERSISTENT_STORE_PROTECTED_STATE_OBJECT := $(BUILD_DIR)/persistent_store_protected_state.o
ENTRY_OBJECT := $(BUILD_DIR)/entry.o

CPPFLAGS := -I$(RUNTIME_INCLUDE) -I$(KERNEL_INCLUDE) -I$(IDENTITY_INCLUDE) -I$(IDENTITY_SRC)
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

ARGON2_CPPFLAGS := -DARGON2_NO_THREADS -I$(RUNTIME_INCLUDE) -I$(ARGON2_VENDOR_INCLUDE) -I$(ARGON2_VENDOR_SRC)
ARGON2_CFLAGS := \
    -target x86_64-unknown-none-elf \
    -std=c11 \
    -O2 \
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
    -Wl,--wrap=aurora_identity_create_with_key \
    -Wl,-z,max-page-size=0x1000 \
    -Wl,-T,linker.ld

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(C_OBJECT): main.c protected_state_transport.h $(KERNEL_INCLUDE)/aurora/capability_abi.h $(KERNEL_INCLUDE)/aurora/identity_service_protocol.h $(KERNEL_INCLUDE)/aurora/service_abi.h $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(IDENTITY_INCLUDE)/aurora/identity/core.h $(IDENTITY_INCLUDE)/aurora/identity/creation_access_policy.h $(IDENTITY_INCLUDE)/aurora/identity/argon2id_provider.h $(IDENTITY_INCLUDE)/aurora/identity/crypto_foundation.h $(IDENTITY_INCLUDE)/aurora/identity/crypto_provider.h $(IDENTITY_INCLUDE)/aurora/identity/machine_secret.h $(IDENTITY_INCLUDE)/aurora/identity/machine_secret_protected_state.h $(IDENTITY_INCLUDE)/aurora/identity/persistent_store.h $(IDENTITY_INCLUDE)/aurora/identity/persistent_store_protected_state.h $(IDENTITY_INCLUDE)/aurora/identity/session_grant.h $(IDENTITY_INCLUDE)/aurora/identity/session_grant_memory.h $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof.h $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof_memory.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I$(ARGON2_VENDOR_INCLUDE) $(CFLAGS) -c main.c -o $@

$(MEMORY_OBJECT): memory.c $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(RUNTIME_INCLUDE)/string.h $(RUNTIME_INCLUDE)/stdlib.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c memory.c -o $@

$(PROTECTED_STATE_TRANSPORT_OBJECT): protected_state_transport.c protected_state_transport.h $(KERNEL_INCLUDE)/aurora/syscall_abi.h $(IDENTITY_INCLUDE)/aurora/identity/protected_state_transport.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c protected_state_transport.c -o $@

$(CORE_OBJECT): $(IDENTITY_SRC)/core.c $(IDENTITY_INCLUDE)/aurora/identity/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/core.c -o $@

$(CREATION_ACCESS_POLICY_OBJECT): $(IDENTITY_SRC)/creation_access_policy.c $(IDENTITY_INCLUDE)/aurora/identity/creation_access_policy.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/creation_access_policy.c -o $@

$(BOOTSTRAP_CREATE_GATE_OBJECT): bootstrap_create_gate.c $(IDENTITY_INCLUDE)/aurora/identity/core.h $(IDENTITY_INCLUDE)/aurora/identity/creation_access_policy.h $(IDENTITY_INCLUDE)/aurora/identity/persistent_store.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c bootstrap_create_gate.c -o $@

$(SESSION_GRANT_OBJECT): $(IDENTITY_SRC)/session_grant.c $(IDENTITY_INCLUDE)/aurora/identity/session_grant.h $(IDENTITY_INCLUDE)/aurora/identity/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/session_grant.c -o $@

$(SESSION_GRANT_MEMORY_OBJECT): $(IDENTITY_SRC)/session_grant_memory.c $(IDENTITY_INCLUDE)/aurora/identity/session_grant_memory.h $(IDENTITY_INCLUDE)/aurora/identity/session_grant.h $(IDENTITY_INCLUDE)/aurora/identity/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/session_grant_memory.c -o $@

$(REAUTH_PROOF_OBJECT): $(IDENTITY_SRC)/reauth_proof.c $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof.h $(IDENTITY_INCLUDE)/aurora/identity/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/reauth_proof.c -o $@

$(REAUTH_PROOF_MEMORY_OBJECT): $(IDENTITY_SRC)/reauth_proof_memory.c $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof_memory.h $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof.h $(IDENTITY_INCLUDE)/aurora/identity/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/reauth_proof_memory.c -o $@

$(CRYPTO_FOUNDATION_OBJECT): $(IDENTITY_SRC)/crypto_foundation.c $(IDENTITY_INCLUDE)/aurora/identity/crypto_foundation.h $(IDENTITY_INCLUDE)/aurora/identity/reauth_proof.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/crypto_foundation.c -o $@

$(CRYPTO_PROVIDER_OBJECT): $(IDENTITY_SRC)/crypto_provider.c $(IDENTITY_INCLUDE)/aurora/identity/crypto_provider.h $(IDENTITY_INCLUDE)/aurora/identity/crypto_foundation.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/crypto_provider.c -o $@

$(ARGON2ID_PROVIDER_OBJECT): $(IDENTITY_SRC)/argon2id_provider.c $(IDENTITY_INCLUDE)/aurora/identity/argon2id_provider.h $(ARGON2_VENDOR_INCLUDE)/argon2.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I$(ARGON2_VENDOR_INCLUDE) $(CFLAGS) -c $(IDENTITY_SRC)/argon2id_provider.c -o $@

$(ARGON2_REF_ARGON2_OBJECT): $(ARGON2_VENDOR_SRC)/argon2.c $(ARGON2_VENDOR_INCLUDE)/argon2.h $(RUNTIME_INCLUDE)/stdlib.h $(RUNTIME_INCLUDE)/stdio.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(ARGON2_CPPFLAGS) $(ARGON2_CFLAGS) -c $(ARGON2_VENDOR_SRC)/argon2.c -o $@

$(ARGON2_REF_CORE_OBJECT): $(ARGON2_VENDOR_SRC)/core.c $(ARGON2_VENDOR_SRC)/core.h $(RUNTIME_INCLUDE)/stdlib.h $(RUNTIME_INCLUDE)/stdio.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(ARGON2_CPPFLAGS) $(ARGON2_CFLAGS) -c $(ARGON2_VENDOR_SRC)/core.c -o $@

$(ARGON2_REF_REF_OBJECT): $(ARGON2_VENDOR_SRC)/ref.c $(ARGON2_VENDOR_SRC)/core.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(ARGON2_CPPFLAGS) $(ARGON2_CFLAGS) -c $(ARGON2_VENDOR_SRC)/ref.c -o $@

$(ARGON2_REF_BLAKE2B_OBJECT): $(ARGON2_VENDOR_SRC)/blake2/blake2b.c $(ARGON2_VENDOR_SRC)/blake2/blake2.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(ARGON2_CPPFLAGS) $(ARGON2_CFLAGS) -c $(ARGON2_VENDOR_SRC)/blake2/blake2b.c -o $@

$(MACHINE_SECRET_OBJECT): $(IDENTITY_SRC)/machine_secret.c $(IDENTITY_INCLUDE)/aurora/identity/machine_secret.h $(IDENTITY_INCLUDE)/aurora/identity/crypto_foundation.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/machine_secret.c -o $@

$(MACHINE_SECRET_CODEC_OBJECT): $(IDENTITY_SRC)/machine_secret_codec.c $(IDENTITY_SRC)/machine_secret_codec.h $(IDENTITY_INCLUDE)/aurora/identity/machine_secret.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/machine_secret_codec.c -o $@

$(MACHINE_SECRET_PROTECTED_STATE_OBJECT): $(IDENTITY_SRC)/machine_secret_protected_state.c $(IDENTITY_INCLUDE)/aurora/identity/machine_secret_protected_state.h $(IDENTITY_SRC)/machine_secret_codec.h $(IDENTITY_INCLUDE)/aurora/identity/protected_state_transport.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/machine_secret_protected_state.c -o $@

$(PERSISTENT_STORE_OBJECT): $(IDENTITY_SRC)/persistent_store.c $(IDENTITY_INCLUDE)/aurora/identity/persistent_store.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/persistent_store.c -o $@

$(PERSISTENT_STORE_PROTECTED_STATE_OBJECT): $(IDENTITY_SRC)/persistent_store_protected_state.c $(IDENTITY_INCLUDE)/aurora/identity/persistent_store_protected_state.h $(IDENTITY_INCLUDE)/aurora/identity/protected_state_transport.h $(RUNTIME_INCLUDE)/string.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(IDENTITY_SRC)/persistent_store_protected_state.c -o $@

$(ENTRY_OBJECT): entry.S | $(BUILD_DIR)
	$(CC) $(ASMFLAGS) -c entry.S -o $@

$(ELF): $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) $(PROTECTED_STATE_TRANSPORT_OBJECT) $(CORE_OBJECT) $(CREATION_ACCESS_POLICY_OBJECT) $(BOOTSTRAP_CREATE_GATE_OBJECT) $(SESSION_GRANT_OBJECT) $(SESSION_GRANT_MEMORY_OBJECT) $(REAUTH_PROOF_OBJECT) $(REAUTH_PROOF_MEMORY_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) $(ARGON2ID_PROVIDER_OBJECT) $(ARGON2_REF_OBJECTS) $(MACHINE_SECRET_OBJECT) $(MACHINE_SECRET_CODEC_OBJECT) $(MACHINE_SECRET_PROTECTED_STATE_OBJECT) $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_PROTECTED_STATE_OBJECT) linker.ld
	$(CC) $(LDFLAGS) $(ENTRY_OBJECT) $(C_OBJECT) $(MEMORY_OBJECT) $(PROTECTED_STATE_TRANSPORT_OBJECT) $(CORE_OBJECT) $(CREATION_ACCESS_POLICY_OBJECT) $(BOOTSTRAP_CREATE_GATE_OBJECT) $(SESSION_GRANT_OBJECT) $(SESSION_GRANT_MEMORY_OBJECT) $(REAUTH_PROOF_OBJECT) $(REAUTH_PROOF_MEMORY_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) $(ARGON2ID_PROVIDER_OBJECT) $(ARGON2_REF_OBJECTS) $(MACHINE_SECRET_OBJECT) $(MACHINE_SECRET_CODEC_OBJECT) $(MACHINE_SECRET_PROTECTED_STATE_OBJECT) $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_PROTECTED_STATE_OBJECT) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)
