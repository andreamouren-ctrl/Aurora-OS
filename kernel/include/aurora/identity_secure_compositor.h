#ifndef AURORA_IDENTITY_SECURE_COMPOSITOR_H
#define AURORA_IDENTITY_SECURE_COMPOSITOR_H

#include <stdbool.h>
#include <aurora/boot.h>

/* Transitional trusted, kernel-owned compositor presentation for Identity.
 * This bridge does not transfer credential input to Ring 3 or grant any
 * profile/file capability. The independent framebuffer renderer remains the
 * fail-closed fallback and the long-term Ring 3 host is still separate work. */
bool identity_secure_compositor_begin(const struct aurora_framebuffer *native);
const struct aurora_framebuffer *identity_secure_compositor_target(void);
bool identity_secure_compositor_present(void);
bool identity_secure_compositor_active(void);
void identity_secure_compositor_end(void);

#endif
