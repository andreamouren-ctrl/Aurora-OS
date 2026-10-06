#ifndef AURORA_IDENTITY_GRAPHICS_H
#define AURORA_IDENTITY_GRAPHICS_H

#include <stdbool.h>

#include <aurora/boot.h>

bool identity_graphics_init(const struct aurora_framebuffer *framebuffer);
bool identity_graphics_ready(void);
void identity_graphics_draw_base(void);
void identity_graphics_release(void);

#endif
