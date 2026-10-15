ARCH ?= x86_64
CC := clang
OBJCOPY ?= objcopy
ROOT := ../../..
BUILD := $(ROOT)/build/$(ARCH)/canvas-runtime
ELF := $(BUILD)/canvas-engine.elf
BIN := $(BUILD)/canvas-engine.bin
CFLAGS := -DG5_CANVAS_FREESTANDING -target x86_64-unknown-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-common -ffunction-sections -fdata-sections -m64 -march=x86-64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2
ASFLAGS := -target x86_64-unknown-none-elf -ffreestanding -fno-pic -fno-pie -m64 -march=x86-64 -mno-red-zone
LDFLAGS := -target x86_64-unknown-none-elf -fuse-ld=lld -nostdlib -static -Wl,--build-id=none -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,-T,linker.ld
.PHONY: all clean
all: $(BIN)
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) -I../include $(CFLAGS) -c $< -o $@
$(BUILD)/camera.o: ../src/g5_canvas_camera.c | $(BUILD)
	$(CC) -I../include $(CFLAGS) -c $< -o $@
$(BUILD)/session.o: ../src/g5_canvas_session_camera.c | $(BUILD)
	$(CC) -I../include $(CFLAGS) -c $< -o $@
$(BUILD)/entry.o: entry.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(ELF): $(BUILD)/entry.o $(BUILD)/main.o $(BUILD)/camera.o $(BUILD)/session.o linker.ld
	$(CC) $(LDFLAGS) $(BUILD)/entry.o $(BUILD)/main.o $(BUILD)/camera.o $(BUILD)/session.o -o $@
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(BUILD)
