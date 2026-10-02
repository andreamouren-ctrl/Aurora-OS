CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -Iinclude

BUILD_DIR := build
CORE_OBJECT := $(BUILD_DIR)/core.o
CORE_TEST_BINARY := $(BUILD_DIR)/identity-core-tests
CREATION_TEST_BINARY := $(BUILD_DIR)/identity-creation-tests

.PHONY: all test clean

all: test

test: $(CORE_TEST_BINARY) $(CREATION_TEST_BINARY)
	$(CORE_TEST_BINARY)
	$(CREATION_TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CORE_OBJECT): src/core.c include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/core.c -o $@

$(CORE_TEST_BINARY): tests/test_identity_core.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_core.c $(CORE_OBJECT) -o $@

$(CREATION_TEST_BINARY): tests/test_identity_creation.c $(CORE_OBJECT) include/aurora/identity/core.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_identity_creation.c $(CORE_OBJECT) -o $@

clean:
	rm -rf $(BUILD_DIR)
