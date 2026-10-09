#ifndef AURORA_SECURITY_ACTIVITY_RENDERER_H
#define AURORA_SECURITY_ACTIVITY_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/boot.h>
#include <aurora/security_activity_controller.h>

enum aurora_security_activity_filter {
    AURORA_SECURITY_ACTIVITY_FILTER_ALL = 0,
    AURORA_SECURITY_ACTIVITY_FILTER_AUTH,
    AURORA_SECURITY_ACTIVITY_FILTER_SESSION,
    AURORA_SECURITY_ACTIVITY_FILTER_CREDENTIAL
};

struct aurora_security_activity_render_state {
    enum aurora_security_activity_filter filter;
    bool focused_load_more;
};

bool security_activity_renderer_validate_framebuffer(
    const struct aurora_framebuffer *framebuffer);

void security_activity_renderer_reset_native(void);

void security_activity_renderer_draw(
    const struct aurora_framebuffer *framebuffer,
    const struct aurora_security_activity_controller *controller,
    const struct aurora_security_activity_render_state *render_state);

void security_activity_renderer_draw_fallback(
    const struct aurora_framebuffer *framebuffer,
    const struct aurora_security_activity_controller *controller,
    const struct aurora_security_activity_render_state *render_state);

#endif
