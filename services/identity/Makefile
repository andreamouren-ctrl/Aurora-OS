CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -Iinclude

BUILD_DIR := build
CORE_OBJECT := $(BUILD_DIR)/core.o
ROTATION_OBJECT := $(BUILD_DIR)/rotation.o
SESSION_GRANT_OBJECT := $(BUILD_DIR)/session_grant.o
PERSISTENT_STORE_OBJECT := $(BUILD_DIR)/persistent_store.o
PERSISTENT_STORE_POSIX_OBJECT := $(BUILD_DIR)/persistent_store_posix.o
CORE_TEST_BINARY := $(BUILD_DIR)/identity-core-tests
CREATION_TEST_BINARY := $(BUILD_DIR)/identity-creation-tests
ROTATION_TEST_BINARY := $(BUILD_DIR)/identity-rotation-tests
SESSION_GRANT_TEST_BINARY := $(BUILD_DIR)/identity-session-grant-tests
PERSISTENT_STORE_TEST_BINARY := $(BUILD_DIR)/identity-persistent-store-tests

.PHONY: all test clean

all: test

test: $(CORE_TEST_BINARY) $(CREATION_TEST_BINARY) $(ROTATION_TEST_BINARY) $(SESSION_GRANT_TEST_BINARY) $(PERSISTENT_STORE_TEST_BINARY)
	$(CORE_TEST_BINARY)
	$(CREATION_TEST_BINARY)
	$(ROTATION_TEST_BINARY)
	$(SESSION_GRANT_TEST_BINARY)
	$(PERSISTENT_STORE_TEST_BINARY)

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

clean:
	rm -rf $(BUILD_DIR)
