#include <stddef.h>
#include <stdint.h>

#include <aurora/user_probe.h>

/*
 * x86-64 user probe:
 *
 *   movabs $0, %rax
 *   movabs $AURORA_USER_PROBE_MAGIC, %rdi
 *   syscall
 *
 *   mov $3, %eax
 *   xor %edi, %edi
 *   syscall
 *
 *   ud2
 *
 * Syscall 0 publishes the bootstrap signature. Syscall 3 exits. Reaching
 * UD2 therefore means the kernel incorrectly returned from process exit.
 */
static const uint8_t probe_image[] = {
    0x48, 0xB8,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x48, 0xBF,
    0x53, 0x4F, 0x41, 0x52,
    0x4F, 0x52, 0x55, 0x41,

    0x0F, 0x05,

    0xB8, 0x03, 0x00, 0x00, 0x00,
    0x31, 0xFF,
    0x0F, 0x05,

    0x0F, 0x0B
};

const uint8_t *user_probe_image(void) {
    return probe_image;
}

size_t user_probe_image_size(void) {
    return sizeof(probe_image);
}
