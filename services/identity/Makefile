CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -Iinclude

BUILD_DIR := build
CORE_OBJECT := $(BUILD_DIR)/core.o
ROTATION_OBJECT := $(BUILD_DIR)/rotation.o
CORE_TEST_BINARY := $(BUILD_DIR)/identity-core-tests
CREATION_TEST_BINARY := $(BUILD_DIR)/identity-creation-tests
ROTATION_TEST_BINARY := $(BUILD_DIR)/identity-rotation-tests

.PHONY: all test clean

all: test

test: $(CORE_TEST_BINARY) $(CREATION_TEST_BINARY) $(ROTATION_TEST_BINARY)
	$(CORE_TEST_BINARY)
	$(CREATION_TEST_BINARY)
	$(ROTATION_TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CORE_OBJECT): src/core.c include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/core.c -o $@

$(ROTATION_OBJECT): src/rotation.c include/aurora/identity/rotation.h include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/rotation.c -o $@

$(CORE_TEST_BINARY): tests/test_identity_core.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_core.c $(CORE_OBJECT) -o $@

$(CREATION_TEST_BINARY): tests/test_identity_creation.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_creation.c $(CORE_OBJECT) -o $@

$(ROTATION_TEST_BINARY): tests/test_identity_rotation.c $(CORE_OBJECT) $(ROTATION_OBJECT) include/aurora/identity/core.h include/aurora/identity/rotation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_rotation.c $(CORE_OBJECT) $(ROTATION_OBJECT) -o $@

clean:
	rm -rf $(BUILD_DIR)
