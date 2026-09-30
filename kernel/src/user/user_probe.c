#include <stddef.h>
#include <stdint.h>

#include <aurora/user_probe.h>

/*
 * x86-64 user probe:
 *
 *   movabs $0, %rax
 *   movabs $AURORA_USER_PROBE_MAGIC, %rdi
 *   syscall
 * loop:
 *   pause
 *   jmp loop
 *
 * Syscall 0 is the bootstrap signal used only while bringing up Ring 3.
 */
static const uint8_t probe_image[] = {
    0x48, 0xB8,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x48, 0xBF,
    0x53, 0x4F, 0x41, 0x52,
    0x4F, 0x52, 0x55, 0x41,

    0x0F, 0x05,

    0xF3, 0x90,
    0xEB, 0xFC
};

const uint8_t *user_probe_image(void) {
    return probe_image;
}

size_t user_probe_image_size(void) {
    return sizeof(probe_image);
}
