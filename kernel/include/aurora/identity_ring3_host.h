#ifndef AURORA_IDENTITY_RING3_HOST_H
#define AURORA_IDENTITY_RING3_HOST_H
#include <stdbool.h>

/* G2 lifecycle foundation: a separate least-authority Ring3 process owns
 * only an exclusive IPC control endpoint. It does NOT receive Aurora Keys,
 * framebuffer rights, font data, file/profile grants or display control.
 * The existing trusted compositor is still the presentation owner. */
bool identity_ring3_host_start(void);
bool identity_ring3_host_alive(void);
bool identity_ring3_host_health_check(void);
bool identity_ring3_host_stop(void);
void identity_ring3_host_poll(void);
bool identity_ring3_host_self_test(void);
#endif
