#ifndef AURORA_ENTROPY_USER_PROBE_H
#define AURORA_ENTROPY_USER_PROBE_H

#include <stddef.h>
#include <stdint.h>

const uint8_t *entropy_user_probe_image(void);
size_t entropy_user_probe_image_size(void);

#endif
