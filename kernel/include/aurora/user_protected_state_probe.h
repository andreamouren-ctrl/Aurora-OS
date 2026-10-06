#ifndef AURORA_USER_PROTECTED_STATE_PROBE_H
#define AURORA_USER_PROTECTED_STATE_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define AURORA_USER_PROTECTED_STATE_PROBE_MAGIC 0x4155525053523342ull

const uint8_t *user_protected_state_probe_image(void);
size_t user_protected_state_probe_image_size(void);

#endif
