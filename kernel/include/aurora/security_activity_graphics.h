#ifndef AURORA_SECURITY_ACTIVITY_GRAPHICS_H
#define AURORA_SECURITY_ACTIVITY_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/boot.h>

enum aurora_security_activity_graphic {
    AURORA_SECURITY_ACTIVITY_GRAPHIC_PANEL = 0,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_EVENT_CARD,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_INFO,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_WARNING,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_CRITICAL,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_AUTH,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_SESSION,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_CREDENTIAL,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_HEADER,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_EMPTY,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_ERROR,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_LOADING,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_LOAD_MORE,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_BUTTON,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_ACTIVE,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_END,
    AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT
};

bool security_activity_graphics_init(void);
bool security_activity_graphics_ready(void);
void security_activity_graphics_release(void);

bool security_activity_graphics_draw(
    const struct aurora_framebuffer *framebuffer,
    enum aurora_security_activity_graphic graphic,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height);

bool security_activity_graphics_dimensions(
    enum aurora_security_activity_graphic graphic,
    uint32_t *out_width,
    uint32_t *out_height);

#endif
