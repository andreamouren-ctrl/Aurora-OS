CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -I../include

BUILD_DIR := build
ENTROPY_TEST := $(BUILD_DIR)/entropy-policy-tests
IDENTITY_PRESENTATION_TEST := $(BUILD_DIR)/identity-presentation-tests

.PHONY: all test clean

all: test

test: $(ENTROPY_TEST) $(IDENTITY_PRESENTATION_TEST)
	$(ENTROPY_TEST)
	$(IDENTITY_PRESENTATION_TEST)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ENTROPY_TEST): entropy_test.c ../src/security/entropy.c ../include/aurora/entropy.h ../include/aurora/entropy_source.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) entropy_test.c ../src/security/entropy.c -o $@

$(IDENTITY_PRESENTATION_TEST): identity_presentation_test.c ../src/ui/identity_presentation.c ../include/aurora/identity_presentation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) identity_presentation_test.c ../src/ui/identity_presentation.c -o $@

clean:
	rm -rf $(BUILD_DIR)
