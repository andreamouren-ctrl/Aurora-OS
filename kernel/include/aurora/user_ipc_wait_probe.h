#ifndef AURORA_USER_IPC_WAIT_PROBE_H
#define AURORA_USER_IPC_WAIT_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define AURORA_USER_IPC_WAITING_MAGIC 0x4155524950435741ull
#define AURORA_USER_IPC_WAIT_DONE_MAGIC 0x4155524F52414950ull

const uint8_t *user_ipc_wait_probe_image(void);
size_t user_ipc_wait_probe_image_size(void);

#endif
