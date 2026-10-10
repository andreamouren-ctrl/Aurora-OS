#ifndef AURORA_SYSTEM_FONT_H
#define AURORA_SYSTEM_FONT_H

#include <stdbool.h>
#include <stdint.h>
#include <aurora/framebuffer.h>

/* Official Aurora OS V1 UI font family. Early-boot bitmap recovery
 * typography remains separate and does not parse font files. */
#define AURORA_SYSTEM_FONT_FAMILY "Aurora Celestia UI"
#define AURORA_SYSTEM_FONT_VERSION "1.0"

enum aurora_system_font_weight {
    AURORA_SYSTEM_FONT_REGULAR = 400,
    AURORA_SYSTEM_FONT_BOLD = 700
};

/* No dynamic allocation, font-file parsing or credential access. */
bool aurora_system_font_ready(void);
uint64_t aurora_system_font_text_width(
    const char *utf8, enum aurora_system_font_weight weight, uint32_t scale);
void aurora_system_font_draw_text(
    const struct aurora_framebuffer *fb, uint64_t x, uint64_t y,
    const char *utf8, enum aurora_system_font_weight weight,
    uint32_t scale, uint32_t color);

#endif
