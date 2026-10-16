ARCH ?= x86_64
CC := clang
LD := ld.lld
OBJCOPY ?= objcopy
ROOT := ../../..
BUILD := $(ROOT)/build/$(ARCH)/canvas-runtime
ELF := $(BUILD)/canvas-engine.elf
BIN := $(BUILD)/canvas-engine.bin
CFLAGS := -DG5_CANVAS_FREESTANDING -target x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-common -ffunction-sections -fdata-sections -m64 -march=x86-64 -mno-red-zone -msse2 -fno-vectorize -fno-slp-vectorize
ASFLAGS := -target x86_64-unknown-none-elf -ffreestanding -fno-pic -fno-pie -m64 -march=x86-64 -mno-red-zone
LDFLAGS := -target x86_64-unknown-none-elf -fuse-ld=lld -nostdlib -static -Wl,--build-id=none -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,-T,linker.ld
.PHONY: all clean
all: $(BIN)
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) -I../include -I../../../kernel/include $(CFLAGS) -mno-sse -mno-sse2 -c $< -o $@
$(BUILD)/camera.o: ../src/g5_canvas_camera.c | $(BUILD)
	$(CC) -I../include -I../../../kernel/include $(CFLAGS) -c $< -o $@
$(BUILD)/session.o: ../src/g5_canvas_session_camera.c | $(BUILD)
	$(CC) -I../include -I../../../kernel/include $(CFLAGS) -c $< -o $@
$(BUILD)/entry.o: entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(ELF): $(BUILD)/entry.o $(BUILD)/main.o $(BUILD)/camera.o $(BUILD)/session.o linker.ld
	$(LD) -static --build-id=none --gc-sections -z max-page-size=0x1000 -T linker.ld $(BUILD)/entry.o $(BUILD)/main.o $(BUILD)/camera.o $(BUILD)/session.o -o $@
	@if nm -u $@ | grep .; then echo 'ERROR: unresolved Canvas runtime symbols'; exit 1; fi
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(BUILD)
