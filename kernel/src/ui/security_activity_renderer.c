#include <stddef.h>
#include <stdint.h>

#include <aurora/framebuffer.h>
#include <aurora/security_activity_graphics.h>
#include <aurora/security_activity_renderer.h>

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static uint64_t percent(uint64_t value, uint64_t amount) {
    return value * amount / 100u;
}

bool security_activity_renderer_validate_framebuffer(
    const struct aurora_framebuffer *fb
) {
    if (fb == NULL || fb->address == NULL || fb->bpp != 32u ||
        fb->width < 64u || fb->height < 48u ||
        fb->pitch < fb->width * sizeof(uint32_t)) {
        return false;
    }

    return fb->red_mask_size != 0u &&
        fb->green_mask_size != 0u &&
        fb->blue_mask_size != 0u;
}

static void draw_frame(
    const struct aurora_framebuffer *fb,
    uint64_t x,
    uint64_t y,
    uint64_t w,
    uint64_t h,
    uint64_t thickness,
    uint32_t color
) {
    if (w == 0u || h == 0u || thickness == 0u) return;
    thickness = min_u64(thickness, min_u64(w, h));
    framebuffer_fill_rect(fb, x, y, w, thickness, color);
    framebuffer_fill_rect(fb, x, y + h - thickness, w, thickness, color);
    framebuffer_fill_rect(fb, x, y, thickness, h, color);
    framebuffer_fill_rect(fb, x + w - thickness, y, thickness, h, color);
}

static uint32_t category_color(
    const struct aurora_framebuffer *fb,
    uint32_t category
) {
    switch (category) {
        case AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH:
            return framebuffer_rgb(fb, 68u, 220u, 255u);
        case AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION:
            return framebuffer_rgb(fb, 112u, 126u, 255u);
        case AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL:
            return framebuffer_rgb(fb, 228u, 184u, 82u);
        default:
            return framebuffer_rgb(fb, 96u, 120u, 142u);
    }
}

static uint32_t severity_color(
    const struct aurora_framebuffer *fb,
    uint32_t severity
) {
    switch (severity) {
        case AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING:
            return framebuffer_rgb(fb, 245u, 174u, 50u);
        case AURORA_SECURITY_ACTIVITY_SEVERITY_CRITICAL:
            return framebuffer_rgb(fb, 242u, 76u, 84u);
        case AURORA_SECURITY_ACTIVITY_SEVERITY_INFO:
        default:
            return framebuffer_rgb(fb, 64u, 215u, 245u);
    }
}

static void draw_state_symbol(
    const struct aurora_framebuffer *fb,
    enum aurora_security_activity_view_state state
) {
    uint64_t size = min_u64(fb->width, fb->height) / 8u;
    if (size < 12u) size = 12u;
    uint64_t x = (fb->width - size) / 2u;
    uint64_t y = (fb->height - size) / 2u;

    uint32_t cyan = framebuffer_rgb(fb, 70u, 218u, 247u);
    uint32_t gold = framebuffer_rgb(fb, 224u, 184u, 82u);
    uint32_t red = framebuffer_rgb(fb, 236u, 77u, 88u);
    uint32_t dim = framebuffer_rgb(fb, 34u, 68u, 88u);

    switch (state) {
        case AURORA_SECURITY_ACTIVITY_VIEW_LOADING:
            draw_frame(fb, x, y, size, size, 2u, cyan);
            framebuffer_fill_rect(
                fb, x + size / 2u, y, 2u, size / 3u, gold);
            break;
        case AURORA_SECURITY_ACTIVITY_VIEW_EMPTY:
            draw_frame(fb, x, y, size, size, 1u, dim);
            framebuffer_fill_rect(
                fb, x + size / 4u, y + size / 2u, size / 2u, 2u, cyan);
            break;
        case AURORA_SECURITY_ACTIVITY_VIEW_END:
            framebuffer_fill_rect(
                fb, x + size / 5u, y + size / 2u, size * 3u / 5u, 2u, gold);
            break;
        case AURORA_SECURITY_ACTIVITY_VIEW_ERROR:
            draw_frame(fb, x, y, size, size, 2u, red);
            framebuffer_fill_rect(
                fb, x + size / 2u, y + size / 4u, 2u, size / 3u, red);
            framebuffer_fill_rect(
                fb, x + size / 2u, y + size * 3u / 4u, 2u, 2u, red);
            break;
        default:
            break;
    }
}

static bool item_matches_filter(
    const struct aurora_security_activity_item *item,
    enum aurora_security_activity_filter filter
) {
    if (filter == AURORA_SECURITY_ACTIVITY_FILTER_ALL) return true;
    if (item == NULL) return false;

    if (filter == AURORA_SECURITY_ACTIVITY_FILTER_AUTH) {
        return item->category == AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH;
    }
    if (filter == AURORA_SECURITY_ACTIVITY_FILTER_SESSION) {
        return item->category == AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION;
    }
    return item->category == AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL;
}


static bool native_attempted;
static bool native_available;

void security_activity_renderer_reset_native(void) {
    security_activity_graphics_release();
    native_attempted = false;
    native_available = false;
}

static bool ensure_native_graphics(void) {
    if (native_attempted) return native_available;
    native_attempted = true;
    native_available = security_activity_graphics_init();
    return native_available;
}

static bool draw_native(
    const struct aurora_framebuffer *fb,
    const struct aurora_security_activity_controller *controller,
    const struct aurora_security_activity_render_state *render_state
) {
    if (!security_activity_renderer_validate_framebuffer(fb) ||
        controller == NULL || render_state == NULL ||
        !ensure_native_graphics()) {
        return false;
    }

    const uint64_t margin_x = percent(fb->width, 5u);
    const uint64_t margin_y = percent(fb->height, 6u);
    const uint64_t panel_w = fb->width - margin_x * 2u;
    const uint64_t panel_h = fb->height - margin_y * 2u;
    const uint64_t header_h = min_u64(percent(panel_h, 11u), 72u);

    framebuffer_clear(fb, framebuffer_rgb(fb, 3u, 8u, 18u));
    if (!security_activity_graphics_draw(
            fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_PANEL,
            margin_x, margin_y, panel_w, panel_h) ||
        !security_activity_graphics_draw(
            fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_HEADER,
            margin_x + percent(panel_w, 4u),
            margin_y + percent(header_h, 15u),
            panel_w - percent(panel_w, 8u),
            header_h * 2u / 3u)) {
        return false;
    }

    uint64_t filter_w = panel_w / 9u;
    if (filter_w < 28u) filter_w = 28u;
    uint64_t filter_h = header_h / 3u;
    if (filter_h < 8u) filter_h = 8u;
    uint64_t filter_y = margin_y + header_h / 3u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint64_t x = margin_x + panel_w - (4u - i) * (filter_w + 6u);
        if (!security_activity_graphics_draw(
                fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_BUTTON,
                x, filter_y, filter_w, filter_h)) {
            return false;
        }
        if (render_state->filter ==
                (enum aurora_security_activity_filter)i) {
            uint64_t indicator = min_u64(filter_h, 20u);
            if (indicator < 4u) indicator = 4u;
            if (!security_activity_graphics_draw(
                    fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_ACTIVE,
                    x + (filter_w - indicator) / 2u,
                    filter_y + filter_h - indicator,
                    indicator, indicator)) {
                return false;
            }
        }
    }

    if (controller->state == AURORA_SECURITY_ACTIVITY_VIEW_LOADING ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_EMPTY ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_END ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_ERROR) {
        enum aurora_security_activity_graphic graphic =
            security_activity_graphic_for_view_state(controller->state);
        if (graphic == AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT) return false;
        uint64_t size = min_u64(fb->width, fb->height) / 5u;
        if (size < 40u) size = 40u;
        if (size > panel_w) size = panel_w;
        if (size > panel_h) size = panel_h;
        return security_activity_graphics_draw(
            fb, graphic,
            margin_x + (panel_w - size) / 2u,
            margin_y + (panel_h - size) / 2u,
            size, size);
    }

    if (controller->state != AURORA_SECURITY_ACTIVITY_VIEW_READY) {
        return true;
    }

    uint64_t list_x = margin_x + percent(panel_w, 4u);
    uint64_t list_w = panel_w - percent(panel_w, 8u);
    uint64_t list_y = margin_y + header_h + percent(panel_h, 4u);
    uint64_t available_h =
        margin_y + panel_h - list_y - percent(panel_h, 8u);
    uint64_t row_h = available_h / 6u;
    if (row_h < 12u) row_h = 12u;
    uint64_t gap = min_u64(row_h / 8u, 8u);

    size_t visible = 0u;
    for (size_t i = 0u;
         i < controller->page.count && visible < 6u;
         ++i) {
        const struct aurora_security_activity_item *item =
            &controller->page.items[i];
        if (!item_matches_filter(item, render_state->filter)) continue;

        uint64_t y = list_y + visible * (row_h + gap);
        if (y + row_h > margin_y + panel_h) break;

        if (!security_activity_graphics_draw(
                fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_EVENT_CARD,
                list_x, y, list_w, row_h)) {
            return false;
        }

        uint64_t icon_size = min_u64(row_h * 2u / 3u, 40u);
        if (icon_size < 4u) icon_size = 4u;
        enum aurora_security_activity_graphic category =
            security_activity_graphic_for_category(item->category);
        enum aurora_security_activity_graphic severity =
            security_activity_graphic_for_severity(item->severity);
        if (category == AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT ||
            severity == AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT ||
            !security_activity_graphics_draw(
                fb, category,
                list_x + icon_size / 2u,
                y + (row_h - icon_size) / 2u,
                icon_size, icon_size) ||
            !security_activity_graphics_draw(
                fb, severity,
                list_x + list_w - icon_size * 3u / 2u,
                y + (row_h - icon_size) / 2u,
                icon_size, icon_size)) {
            return false;
        }

        ++visible;
    }

    if (controller->page.has_more) {
        uint64_t button_w = panel_w / 4u;
        uint64_t button_h = min_u64(percent(panel_h, 6u), 42u);
        uint64_t x = margin_x + (panel_w - button_w) / 2u;
        uint64_t y =
            margin_y + panel_h - button_h - percent(panel_h, 2u);
        if (!security_activity_graphics_draw(
                fb, AURORA_SECURITY_ACTIVITY_GRAPHIC_LOAD_MORE,
                x, y, button_w, button_h)) {
            return false;
        }
    }

    return true;
}

void security_activity_renderer_draw(
    const struct aurora_framebuffer *fb,
    const struct aurora_security_activity_controller *controller,
    const struct aurora_security_activity_render_state *render_state
) {
    if (draw_native(fb, controller, render_state)) return;

    /*
     * Native artwork is presentation-only. Any missing/corrupt asset, decode
     * failure, memory pressure or draw failure falls back to the procedural
     * path and never affects Identity/session authority.
     */
    native_available = false;
    security_activity_graphics_release();
    security_activity_renderer_draw_fallback(
        fb, controller, render_state);
}

void security_activity_renderer_draw_fallback(
    const struct aurora_framebuffer *fb,
    const struct aurora_security_activity_controller *controller,
    const struct aurora_security_activity_render_state *render_state
) {
    if (!security_activity_renderer_validate_framebuffer(fb) ||
        controller == NULL || render_state == NULL) {
        return;
    }

    const uint32_t background = framebuffer_rgb(fb, 3u, 8u, 18u);
    const uint32_t panel = framebuffer_rgb(fb, 7u, 19u, 34u);
    const uint32_t panel_alt = framebuffer_rgb(fb, 10u, 27u, 46u);
    const uint32_t cyan = framebuffer_rgb(fb, 56u, 194u, 232u);
    const uint32_t cyan_dim = framebuffer_rgb(fb, 25u, 91u, 122u);
    const uint32_t gold = framebuffer_rgb(fb, 213u, 168u, 75u);

    framebuffer_clear(fb, background);

    uint64_t margin_x = percent(fb->width, 5u);
    uint64_t margin_y = percent(fb->height, 6u);
    uint64_t panel_w = fb->width - margin_x * 2u;
    uint64_t panel_h = fb->height - margin_y * 2u;
    framebuffer_fill_rect(fb, margin_x, margin_y, panel_w, panel_h, panel);
    draw_frame(fb, margin_x, margin_y, panel_w, panel_h, 2u, cyan_dim);

    uint64_t header_h = min_u64(percent(panel_h, 11u), 72u);
    framebuffer_fill_rect(
        fb, margin_x + 2u, margin_y + 2u, panel_w - 4u, header_h, panel_alt);
    framebuffer_fill_rect(
        fb, margin_x + 2u, margin_y + header_h, panel_w - 4u, 1u, gold);

    uint64_t filter_y = margin_y + header_h / 3u;
    uint64_t filter_w = panel_w / 9u;
    if (filter_w < 28u) filter_w = 28u;
    uint64_t filter_h = header_h / 3u;
    if (filter_h < 8u) filter_h = 8u;

    for (uint32_t i = 0u; i < 4u; ++i) {
        uint64_t x = margin_x + panel_w - (4u - i) * (filter_w + 6u);
        framebuffer_fill_rect(
            fb, x, filter_y, filter_w, filter_h, panel);
        draw_frame(
            fb, x, filter_y, filter_w, filter_h, 1u,
            render_state->filter == (enum aurora_security_activity_filter)i
                ? cyan
                : cyan_dim);
        if (render_state->filter == (enum aurora_security_activity_filter)i) {
            framebuffer_fill_rect(
                fb, x + filter_w / 2u, filter_y + filter_h - 2u, 2u, 2u, gold);
        }
    }

    if (controller->state == AURORA_SECURITY_ACTIVITY_VIEW_LOADING ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_EMPTY ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_END ||
        controller->state == AURORA_SECURITY_ACTIVITY_VIEW_ERROR) {
        draw_state_symbol(fb, controller->state);
        return;
    }

    if (controller->state != AURORA_SECURITY_ACTIVITY_VIEW_READY) return;

    uint64_t list_x = margin_x + percent(panel_w, 4u);
    uint64_t list_w = panel_w - percent(panel_w, 8u);
    uint64_t list_y = margin_y + header_h + percent(panel_h, 4u);
    uint64_t available_h = margin_y + panel_h - list_y - percent(panel_h, 8u);
    uint64_t row_h = available_h / 6u;
    if (row_h < 12u) row_h = 12u;
    uint64_t gap = min_u64(row_h / 8u, 8u);

    size_t visible = 0u;
    for (size_t i = 0u;
         i < controller->page.count && visible < 6u;
         ++i) {
        const struct aurora_security_activity_item *item =
            &controller->page.items[i];
        if (!item_matches_filter(item, render_state->filter)) continue;

        uint64_t y = list_y + visible * (row_h + gap);
        if (y + row_h > margin_y + panel_h) break;

        framebuffer_fill_rect(fb, list_x, y, list_w, row_h, panel_alt);
        draw_frame(fb, list_x, y, list_w, row_h, 1u, cyan_dim);

        uint64_t marker = min_u64(row_h / 2u, 18u);
        if (marker < 4u) marker = 4u;
        uint64_t marker_y = y + (row_h - marker) / 2u;
        framebuffer_fill_rect(
            fb,
            list_x + marker,
            marker_y,
            marker,
            marker,
            category_color(fb, item->category));

        uint64_t sev_x = list_x + list_w - marker * 2u;
        framebuffer_fill_rect(
            fb,
            sev_x,
            marker_y,
            marker,
            marker,
            severity_color(fb, item->severity));

        ++visible;
    }

    if (controller->page.has_more) {
        uint64_t button_w = panel_w / 4u;
        uint64_t button_h = min_u64(percent(panel_h, 6u), 42u);
        uint64_t x = margin_x + (panel_w - button_w) / 2u;
        uint64_t y = margin_y + panel_h - button_h - percent(panel_h, 2u);
        framebuffer_fill_rect(fb, x, y, button_w, button_h, panel_alt);
        draw_frame(
            fb, x, y, button_w, button_h, 1u,
            render_state->focused_load_more ? cyan : cyan_dim);
    }
}
