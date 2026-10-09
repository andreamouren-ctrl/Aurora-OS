#include <stddef.h>
#include <stdint.h>

#include <aurora/security_activity_graphics.h>
#include <aurora/security_activity_graphics_probe.h>

#define PROBE_WIDTH 128u
#define PROBE_HEIGHT 128u

static uint32_t probe_pixels[PROBE_WIDTH * PROBE_HEIGHT];

static void fill_probe(uint32_t pixel) {
    for (size_t i = 0u; i < PROBE_WIDTH * PROBE_HEIGHT; ++i) {
        probe_pixels[i] = pixel;
    }
}

static bool probe_changed(uint32_t pixel) {
    for (size_t i = 0u; i < PROBE_WIDTH * PROBE_HEIGHT; ++i) {
        if (probe_pixels[i] != pixel) return true;
    }
    return false;
}

bool security_activity_graphics_self_test(void) {
    security_activity_graphics_release();
    if (!security_activity_graphics_init() ||
        !security_activity_graphics_ready()) {
        security_activity_graphics_release();
        return false;
    }

    for (uint32_t i = 0u;
         i < (uint32_t)AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT;
         ++i) {
        uint32_t width = 0u;
        uint32_t height = 0u;
        if (!security_activity_graphics_dimensions(
                (enum aurora_security_activity_graphic)i,
                &width,
                &height) ||
            width == 0u || height == 0u) {
            security_activity_graphics_release();
            return false;
        }
    }

    if (security_activity_graphic_for_category(
            AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_AUTH ||
        security_activity_graphic_for_category(
            AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_SESSION ||
        security_activity_graphic_for_category(
            AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_CREDENTIAL ||
        security_activity_graphic_for_severity(
            AURORA_SECURITY_ACTIVITY_SEVERITY_INFO) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_INFO ||
        security_activity_graphic_for_severity(
            AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_WARNING ||
        security_activity_graphic_for_severity(
            AURORA_SECURITY_ACTIVITY_SEVERITY_CRITICAL) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_CRITICAL ||
        security_activity_graphic_for_view_state(
            AURORA_SECURITY_ACTIVITY_VIEW_LOADING) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_LOADING ||
        security_activity_graphic_for_view_state(
            AURORA_SECURITY_ACTIVITY_VIEW_EMPTY) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_EMPTY ||
        security_activity_graphic_for_view_state(
            AURORA_SECURITY_ACTIVITY_VIEW_END) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_END ||
        security_activity_graphic_for_view_state(
            AURORA_SECURITY_ACTIVITY_VIEW_ERROR) !=
            AURORA_SECURITY_ACTIVITY_GRAPHIC_ERROR) {
        security_activity_graphics_release();
        return false;
    }

    const uint32_t background = UINT32_C(0x00102030);
    fill_probe(background);
    const struct aurora_framebuffer framebuffer = {
        .address = probe_pixels,
        .width = PROBE_WIDTH,
        .height = PROBE_HEIGHT,
        .pitch = PROBE_WIDTH * sizeof(uint32_t),
        .bpp = 32u,
        .red_mask_size = 8u,
        .red_mask_shift = 16u,
        .green_mask_size = 8u,
        .green_mask_shift = 8u,
        .blue_mask_size = 8u,
        .blue_mask_shift = 0u
    };

    bool drew =
        security_activity_graphics_draw(
            &framebuffer,
            AURORA_SECURITY_ACTIVITY_GRAPHIC_PANEL,
            4u, 8u, 120u, 76u) &&
        security_activity_graphics_draw(
            &framebuffer,
            AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_INFO,
            48u, 88u, 32u, 32u) &&
        probe_changed(background);

    security_activity_graphics_release();
    return drew && !security_activity_graphics_ready();
}
