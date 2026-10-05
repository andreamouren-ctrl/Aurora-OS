CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -Iinclude

BUILD_DIR := build
ARGON2_VENDOR_DIR := vendor/argon2
ARGON2_VENDOR_SRC := $(ARGON2_VENDOR_DIR)/src
ARGON2_VENDOR_INCLUDE := $(ARGON2_VENDOR_DIR)/include
ARGON2_VENDOR_CPPFLAGS := -DARGON2_NO_THREADS -I$(ARGON2_VENDOR_INCLUDE) -I$(ARGON2_VENDOR_SRC)
ARGON2_VENDOR_CFLAGS := -std=c11 -O2

CORE_OBJECT := $(BUILD_DIR)/core.o
ROTATION_OBJECT := $(BUILD_DIR)/rotation.o
SESSION_GRANT_OBJECT := $(BUILD_DIR)/session_grant.o
PERSISTENT_STORE_OBJECT := $(BUILD_DIR)/persistent_store.o
PERSISTENT_STORE_POSIX_OBJECT := $(BUILD_DIR)/persistent_store_posix.o
CRYPTO_FOUNDATION_OBJECT := $(BUILD_DIR)/crypto_foundation.o
CRYPTO_PROVIDER_OBJECT := $(BUILD_DIR)/crypto_provider.o
ARGON2ID_PROVIDER_OBJECT := $(BUILD_DIR)/argon2id_provider.o
MACHINE_SECRET_OBJECT := $(BUILD_DIR)/machine_secret.o
MACHINE_SECRET_POSIX_OBJECT := $(BUILD_DIR)/machine_secret_posix.o
ARGON2_REF_ARGON2_OBJECT := $(BUILD_DIR)/argon2_ref_argon2.o
ARGON2_REF_CORE_OBJECT := $(BUILD_DIR)/argon2_ref_core.o
ARGON2_REF_REF_OBJECT := $(BUILD_DIR)/argon2_ref_ref.o
ARGON2_REF_ENCODING_OBJECT := $(BUILD_DIR)/argon2_ref_encoding.o
ARGON2_REF_BLAKE2B_OBJECT := $(BUILD_DIR)/argon2_ref_blake2b.o
ARGON2_REF_OBJECTS := $(ARGON2_REF_ARGON2_OBJECT) $(ARGON2_REF_CORE_OBJECT) $(ARGON2_REF_REF_OBJECT) $(ARGON2_REF_ENCODING_OBJECT) $(ARGON2_REF_BLAKE2B_OBJECT)

CORE_TEST_BINARY := $(BUILD_DIR)/identity-core-tests
CREATION_TEST_BINARY := $(BUILD_DIR)/identity-creation-tests
ROTATION_TEST_BINARY := $(BUILD_DIR)/identity-rotation-tests
SESSION_GRANT_TEST_BINARY := $(BUILD_DIR)/identity-session-grant-tests
PERSISTENT_STORE_TEST_BINARY := $(BUILD_DIR)/identity-persistent-store-tests
CRYPTO_FOUNDATION_TEST_BINARY := $(BUILD_DIR)/identity-crypto-foundation-tests
ARGON2ID_PROVIDER_TEST_BINARY := $(BUILD_DIR)/identity-argon2id-provider-tests
MACHINE_SECRET_TEST_BINARY := $(BUILD_DIR)/identity-machine-secret-tests

.PHONY: all test clean

all: test

test: $(CORE_TEST_BINARY) $(CREATION_TEST_BINARY) $(ROTATION_TEST_BINARY) $(SESSION_GRANT_TEST_BINARY) $(PERSISTENT_STORE_TEST_BINARY) $(CRYPTO_FOUNDATION_TEST_BINARY) $(ARGON2ID_PROVIDER_TEST_BINARY) $(MACHINE_SECRET_TEST_BINARY)
	$(CORE_TEST_BINARY)
	$(CREATION_TEST_BINARY)
	$(ROTATION_TEST_BINARY)
	$(SESSION_GRANT_TEST_BINARY)
	$(PERSISTENT_STORE_TEST_BINARY)
	$(CRYPTO_FOUNDATION_TEST_BINARY)
	$(ARGON2ID_PROVIDER_TEST_BINARY)
	$(MACHINE_SECRET_TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CORE_OBJECT): src/core.c include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/core.c -o $@

$(ROTATION_OBJECT): src/rotation.c include/aurora/identity/rotation.h include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/rotation.c -o $@

$(SESSION_GRANT_OBJECT): src/session_grant.c include/aurora/identity/session_grant.h include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/session_grant.c -o $@

$(PERSISTENT_STORE_OBJECT): src/persistent_store.c include/aurora/identity/persistent_store.h include/aurora/identity/core.h include/aurora/identity/rotation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/persistent_store.c -o $@

$(PERSISTENT_STORE_POSIX_OBJECT): src/persistent_store_posix.c include/aurora/identity/persistent_store_posix.h include/aurora/identity/persistent_store.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/persistent_store_posix.c -o $@

$(CRYPTO_FOUNDATION_OBJECT): src/crypto_foundation.c include/aurora/identity/crypto_foundation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/crypto_foundation.c -o $@

$(CRYPTO_PROVIDER_OBJECT): src/crypto_provider.c include/aurora/identity/crypto_provider.h include/aurora/identity/crypto_foundation.h include/aurora/identity/core.h include/aurora/identity/session_grant.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/crypto_provider.c -o $@

$(ARGON2ID_PROVIDER_OBJECT): src/argon2id_provider.c include/aurora/identity/argon2id_provider.h include/aurora/identity/crypto_provider.h include/aurora/identity/crypto_foundation.h include/aurora/identity/core.h $(ARGON2_VENDOR_INCLUDE)/argon2.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -I$(ARGON2_VENDOR_INCLUDE) $(CFLAGS) -c src/argon2id_provider.c -o $@

$(MACHINE_SECRET_OBJECT): src/machine_secret.c include/aurora/identity/machine_secret.h include/aurora/identity/core.h include/aurora/identity/crypto_foundation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/machine_secret.c -o $@

$(MACHINE_SECRET_POSIX_OBJECT): src/machine_secret_posix.c include/aurora/identity/machine_secret_posix.h include/aurora/identity/machine_secret.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/machine_secret_posix.c -o $@

$(ARGON2_REF_ARGON2_OBJECT): $(ARGON2_VENDOR_SRC)/argon2.c $(ARGON2_VENDOR_INCLUDE)/argon2.h | $(BUILD_DIR)
	$(CC) $(ARGON2_VENDOR_CPPFLAGS) $(ARGON2_VENDOR_CFLAGS) -c $(ARGON2_VENDOR_SRC)/argon2.c -o $@

$(ARGON2_REF_CORE_OBJECT): $(ARGON2_VENDOR_SRC)/core.c $(ARGON2_VENDOR_SRC)/core.h $(ARGON2_VENDOR_SRC)/thread.h | $(BUILD_DIR)
	$(CC) $(ARGON2_VENDOR_CPPFLAGS) $(ARGON2_VENDOR_CFLAGS) -c $(ARGON2_VENDOR_SRC)/core.c -o $@

$(ARGON2_REF_REF_OBJECT): $(ARGON2_VENDOR_SRC)/ref.c $(ARGON2_VENDOR_SRC)/core.h $(ARGON2_VENDOR_SRC)/blake2/blamka-round-ref.h | $(BUILD_DIR)
	$(CC) $(ARGON2_VENDOR_CPPFLAGS) $(ARGON2_VENDOR_CFLAGS) -c $(ARGON2_VENDOR_SRC)/ref.c -o $@

$(ARGON2_REF_ENCODING_OBJECT): $(ARGON2_VENDOR_SRC)/encoding.c $(ARGON2_VENDOR_SRC)/encoding.h | $(BUILD_DIR)
	$(CC) $(ARGON2_VENDOR_CPPFLAGS) $(ARGON2_VENDOR_CFLAGS) -c $(ARGON2_VENDOR_SRC)/encoding.c -o $@

$(ARGON2_REF_BLAKE2B_OBJECT): $(ARGON2_VENDOR_SRC)/blake2/blake2b.c $(ARGON2_VENDOR_SRC)/blake2/blake2.h $(ARGON2_VENDOR_SRC)/blake2/blake2-impl.h | $(BUILD_DIR)
	$(CC) $(ARGON2_VENDOR_CPPFLAGS) $(ARGON2_VENDOR_CFLAGS) -c $(ARGON2_VENDOR_SRC)/blake2/blake2b.c -o $@

$(CORE_TEST_BINARY): tests/test_identity_core.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_core.c $(CORE_OBJECT) -o $@

$(CREATION_TEST_BINARY): tests/test_identity_creation.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_creation.c $(CORE_OBJECT) -o $@

$(ROTATION_TEST_BINARY): tests/test_identity_rotation.c $(CORE_OBJECT) $(ROTATION_OBJECT) include/aurora/identity/core.h include/aurora/identity/rotation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_rotation.c $(CORE_OBJECT) $(ROTATION_OBJECT) -o $@

$(SESSION_GRANT_TEST_BINARY): tests/test_identity_session_grant.c $(CORE_OBJECT) $(SESSION_GRANT_OBJECT) include/aurora/identity/core.h include/aurora/identity/session_grant.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_session_grant.c $(CORE_OBJECT) $(SESSION_GRANT_OBJECT) -o $@

$(PERSISTENT_STORE_TEST_BINARY): tests/test_identity_persistent_store.c $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_POSIX_OBJECT) include/aurora/identity/persistent_store.h include/aurora/identity/persistent_store_posix.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_persistent_store.c $(PERSISTENT_STORE_OBJECT) $(PERSISTENT_STORE_POSIX_OBJECT) -o $@

$(CRYPTO_FOUNDATION_TEST_BINARY): tests/test_identity_crypto_foundation.c $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) include/aurora/identity/crypto_foundation.h include/aurora/identity/crypto_provider.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_crypto_foundation.c $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) -o $@

$(ARGON2ID_PROVIDER_TEST_BINARY): tests/test_identity_argon2id_provider.c $(ARGON2ID_PROVIDER_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) $(ARGON2_REF_OBJECTS) include/aurora/identity/argon2id_provider.h
	$(CC) $(CPPFLAGS) -I$(ARGON2_VENDOR_INCLUDE) $(CFLAGS) tests/test_identity_argon2id_provider.c $(ARGON2ID_PROVIDER_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) $(CRYPTO_PROVIDER_OBJECT) $(ARGON2_REF_OBJECTS) -o $@

$(MACHINE_SECRET_TEST_BINARY): tests/test_identity_machine_secret.c $(MACHINE_SECRET_OBJECT) $(MACHINE_SECRET_POSIX_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) include/aurora/identity/machine_secret.h include/aurora/identity/machine_secret_posix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_machine_secret.c $(MACHINE_SECRET_OBJECT) $(MACHINE_SECRET_POSIX_OBJECT) $(CRYPTO_FOUNDATION_OBJECT) -o $@

clean:
	rm -rf $(BUILD_DIR)
