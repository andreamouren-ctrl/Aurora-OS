#include <stddef.h>
#include <stdint.h>

#include <aurora/security_activity_renderer.h>
#include <aurora/security_activity_renderer_probe.h>

bool security_activity_renderer_self_test(void) {
    static uint32_t pixels[64u * 48u];
    struct aurora_framebuffer fb = {
        .address = pixels,
        .width = 64u,
        .height = 48u,
        .pitch = 64u * sizeof(uint32_t),
        .bpp = 32u,
        .red_mask_size = 8u,
        .red_mask_shift = 16u,
        .green_mask_size = 8u,
        .green_mask_shift = 8u,
        .blue_mask_size = 8u,
        .blue_mask_shift = 0u
    };
    struct aurora_security_activity_controller controller = {0};
    struct aurora_security_activity_render_state state = {
        .filter = AURORA_SECURITY_ACTIVITY_FILTER_ALL,
        .focused_load_more = false
    };

    if (!security_activity_renderer_validate_framebuffer(&fb)) return false;

    /*
     * No native asset object is linked in this safety-stage branch. The
     * primary renderer must therefore detect absent weak asset symbols and
     * transparently produce the procedural fallback.
     */
    security_activity_renderer_reset_native();
    controller.state = AURORA_SECURITY_ACTIVITY_VIEW_EMPTY;
    security_activity_renderer_draw(&fb, &controller, &state);

    uint32_t aggregate = 0u;
    for (size_t i = 0u; i < 64u * 48u; ++i) aggregate |= pixels[i];
    if (aggregate == 0u) return false;

    controller.state = AURORA_SECURITY_ACTIVITY_VIEW_READY;
    controller.page.count = 1u;
    controller.page.items[0].sequence = 1u;
    controller.page.items[0].category = AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
    controller.page.items[0].severity = AURORA_SECURITY_ACTIVITY_SEVERITY_INFO;
    security_activity_renderer_draw(&fb, &controller, &state);

    bool ok = pixels[0] != 0u;
    security_activity_renderer_reset_native();
    return ok;
}
