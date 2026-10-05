#ifndef AURORA_USER_IPC_PROBE_H
#define AURORA_USER_IPC_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define AURORA_USER_IPC_PROBE_MAGIC 0x4155524F52414950ull

const uint8_t *user_ipc_probe_image(void);
size_t user_ipc_probe_image_size(void);

#endif
