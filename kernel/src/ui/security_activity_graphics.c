#include <stddef.h>
#include <stdint.h>

#include <aurora/framebuffer.h>
#include <aurora/png.h>
#include <aurora/security_activity_graphics.h>

#define DECLARE_ASSET(name) \
    extern const uint8_t aurora_security_activity_##name##_png_start[]; \
    extern const uint8_t aurora_security_activity_##name##_png_end[]

DECLARE_ASSET(panel);
DECLARE_ASSET(event_card);
DECLARE_ASSET(status_info);
DECLARE_ASSET(status_warning);
DECLARE_ASSET(status_critical);
DECLARE_ASSET(category_auth);
DECLARE_ASSET(category_session);
DECLARE_ASSET(category_credential);
DECLARE_ASSET(header);
DECLARE_ASSET(empty);
DECLARE_ASSET(error);
DECLARE_ASSET(loading);
DECLARE_ASSET(load_more);
DECLARE_ASSET(filter_button);
DECLARE_ASSET(filter_active);
DECLARE_ASSET(end);

struct activity_asset_bytes {
    const uint8_t *start;
    const uint8_t *end;
};

static struct aurora_png_image activity_images[
    AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT
];
static bool activity_loaded[AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT];
static bool activity_ready;

static const struct activity_asset_bytes activity_assets[
    AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT
] = {
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_PANEL] = {
        aurora_security_activity_panel_png_start,
        aurora_security_activity_panel_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_EVENT_CARD] = {
        aurora_security_activity_event_card_png_start,
        aurora_security_activity_event_card_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_INFO] = {
        aurora_security_activity_status_info_png_start,
        aurora_security_activity_status_info_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_WARNING] = {
        aurora_security_activity_status_warning_png_start,
        aurora_security_activity_status_warning_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_CRITICAL] = {
        aurora_security_activity_status_critical_png_start,
        aurora_security_activity_status_critical_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_AUTH] = {
        aurora_security_activity_category_auth_png_start,
        aurora_security_activity_category_auth_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_SESSION] = {
        aurora_security_activity_category_session_png_start,
        aurora_security_activity_category_session_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_CREDENTIAL] = {
        aurora_security_activity_category_credential_png_start,
        aurora_security_activity_category_credential_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_HEADER] = {
        aurora_security_activity_header_png_start,
        aurora_security_activity_header_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_EMPTY] = {
        aurora_security_activity_empty_png_start,
        aurora_security_activity_empty_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_ERROR] = {
        aurora_security_activity_error_png_start,
        aurora_security_activity_error_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_LOADING] = {
        aurora_security_activity_loading_png_start,
        aurora_security_activity_loading_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_LOAD_MORE] = {
        aurora_security_activity_load_more_png_start,
        aurora_security_activity_load_more_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_BUTTON] = {
        aurora_security_activity_filter_button_png_start,
        aurora_security_activity_filter_button_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_FILTER_ACTIVE] = {
        aurora_security_activity_filter_active_png_start,
        aurora_security_activity_filter_active_png_end
    },
    [AURORA_SECURITY_ACTIVITY_GRAPHIC_END] = {
        aurora_security_activity_end_png_start,
        aurora_security_activity_end_png_end
    }
};

static uint8_t unpack_channel(uint32_t pixel, uint8_t size, uint8_t shift) {
    if (size == 0u) return 0u;
    uint64_t max_value = size >= 32u
        ? UINT64_C(0xFFFFFFFF)
        : ((UINT64_C(1) << size) - 1u);
    uint64_t raw = ((uint64_t)pixel >> shift) & max_value;
    return (uint8_t)((raw * 255u + max_value / 2u) / max_value);
}

static uint8_t lerp_u8(uint8_t first, uint8_t second, uint32_t fraction) {
    uint32_t inverse = 65536u - fraction;
    return (uint8_t)(((uint32_t)first * inverse +
                      (uint32_t)second * fraction) >> 16);
}

static void sample_bilinear(
    const struct aurora_png_image *image,
    uint64_t x_fp,
    uint64_t y_fp,
    uint8_t rgba[4]
) {
    uint64_t x0 = x_fp >> 16;
    uint64_t y0 = y_fp >> 16;
    if (x0 >= image->width) x0 = image->width - 1u;
    if (y0 >= image->height) y0 = image->height - 1u;
    uint64_t x1 = x0 + 1u < image->width ? x0 + 1u : x0;
    uint64_t y1 = y0 + 1u < image->height ? y0 + 1u : y0;
    uint32_t fx = (uint32_t)(x_fp & 0xFFFFu);
    uint32_t fy = (uint32_t)(y_fp & 0xFFFFu);

    for (uint8_t channel = 0u; channel < 4u; ++channel) {
        if (channel >= image->channels) {
            rgba[channel] = channel == 3u ? 255u : 0u;
            continue;
        }
        size_t p00 = ((size_t)y0 * image->width + (size_t)x0) *
            image->channels + channel;
        size_t p10 = ((size_t)y0 * image->width + (size_t)x1) *
            image->channels + channel;
        size_t p01 = ((size_t)y1 * image->width + (size_t)x0) *
            image->channels + channel;
        size_t p11 = ((size_t)y1 * image->width + (size_t)x1) *
            image->channels + channel;
        uint8_t top = lerp_u8(image->pixels[p00], image->pixels[p10], fx);
        uint8_t bottom = lerp_u8(image->pixels[p01], image->pixels[p11], fx);
        rgba[channel] = lerp_u8(top, bottom, fy);
    }
    if (image->channels == 3u) rgba[3] = 255u;
}

static bool valid_graphic(enum aurora_security_activity_graphic graphic) {
    return graphic >= AURORA_SECURITY_ACTIVITY_GRAPHIC_PANEL &&
        graphic < AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT;
}

void security_activity_graphics_release(void) {
    for (size_t i = 0u; i < AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT; ++i) {
        if (activity_loaded[i]) {
            aurora_png_release(&activity_images[i]);
            activity_loaded[i] = false;
        }
    }
    activity_ready = false;
}

bool security_activity_graphics_init(void) {
    security_activity_graphics_release();

    for (size_t i = 0u; i < AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT; ++i) {
        const uint8_t *start = activity_assets[i].start;
        const uint8_t *end = activity_assets[i].end;
        if (start == NULL || end == NULL || end <= start ||
            !aurora_png_decode(
                start,
                (size_t)(end - start),
                &activity_images[i]) ||
            activity_images[i].width == 0u ||
            activity_images[i].height == 0u ||
            (activity_images[i].channels != 3u &&
             activity_images[i].channels != 4u)) {
            security_activity_graphics_release();
            return false;
        }
        activity_loaded[i] = true;
    }

    activity_ready = true;
    return true;
}

bool security_activity_graphics_ready(void) {
    return activity_ready;
}

bool security_activity_graphics_dimensions(
    enum aurora_security_activity_graphic graphic,
    uint32_t *out_width,
    uint32_t *out_height
) {
    if (!activity_ready || !valid_graphic(graphic) ||
        out_width == NULL || out_height == NULL ||
        !activity_loaded[graphic]) {
        return false;
    }
    *out_width = activity_images[graphic].width;
    *out_height = activity_images[graphic].height;
    return true;
}

bool security_activity_graphics_draw(
    const struct aurora_framebuffer *framebuffer,
    enum aurora_security_activity_graphic graphic,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height
) {
    if (!activity_ready || !valid_graphic(graphic) ||
        !activity_loaded[graphic] || framebuffer == NULL ||
        framebuffer->address == NULL || framebuffer->bpp != 32u ||
        framebuffer->width == 0u || framebuffer->height == 0u ||
        width == 0u || height == 0u ||
        x >= framebuffer->width || y >= framebuffer->height ||
        width > framebuffer->width - x ||
        height > framebuffer->height - y) {
        return false;
    }

    const struct aurora_png_image *image = &activity_images[graphic];
    uint64_t step_x_fp = width > 1u
        ? (((uint64_t)image->width - 1u) << 16) / (width - 1u)
        : 0u;
    uint64_t step_y_fp = height > 1u
        ? (((uint64_t)image->height - 1u) << 16) / (height - 1u)
        : 0u;
    volatile uint8_t *base = (volatile uint8_t *)framebuffer->address;

    for (uint64_t dy = 0u; dy < height; ++dy) {
        volatile uint32_t *line =
            (volatile uint32_t *)(base + (y + dy) * framebuffer->pitch);
        uint64_t sy_fp = dy * step_y_fp;

        for (uint64_t dx = 0u; dx < width; ++dx) {
            uint8_t rgba[4];
            sample_bilinear(image, dx * step_x_fp, sy_fp, rgba);
            uint32_t alpha = rgba[3];
            if (alpha == 0u) continue;

            uint32_t destination = line[x + dx];
            uint8_t dr = unpack_channel(
                destination,
                framebuffer->red_mask_size,
                framebuffer->red_mask_shift);
            uint8_t dg = unpack_channel(
                destination,
                framebuffer->green_mask_size,
                framebuffer->green_mask_shift);
            uint8_t db = unpack_channel(
                destination,
                framebuffer->blue_mask_size,
                framebuffer->blue_mask_shift);
            uint32_t inverse = 255u - alpha;
            uint8_t red = (uint8_t)(((uint32_t)rgba[0] * alpha +
                                     (uint32_t)dr * inverse + 127u) / 255u);
            uint8_t green = (uint8_t)(((uint32_t)rgba[1] * alpha +
                                       (uint32_t)dg * inverse + 127u) / 255u);
            uint8_t blue = (uint8_t)(((uint32_t)rgba[2] * alpha +
                                      (uint32_t)db * inverse + 127u) / 255u);
            line[x + dx] = framebuffer_rgb(
                framebuffer, red, green, blue);
        }
    }

    return true;
}

enum aurora_security_activity_graphic
security_activity_graphic_for_category(uint32_t category) {
    switch (category) {
        case AURORA_SECURITY_ACTIVITY_CATEGORY_AUTH:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_AUTH;
        case AURORA_SECURITY_ACTIVITY_CATEGORY_SESSION:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_SESSION;
        case AURORA_SECURITY_ACTIVITY_CATEGORY_CREDENTIAL:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_CATEGORY_CREDENTIAL;
        default:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT;
    }
}

enum aurora_security_activity_graphic
security_activity_graphic_for_severity(uint32_t severity) {
    switch (severity) {
        case AURORA_SECURITY_ACTIVITY_SEVERITY_INFO:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_INFO;
        case AURORA_SECURITY_ACTIVITY_SEVERITY_WARNING:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_WARNING;
        case AURORA_SECURITY_ACTIVITY_SEVERITY_CRITICAL:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_STATUS_CRITICAL;
        default:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT;
    }
}

enum aurora_security_activity_graphic
security_activity_graphic_for_view_state(
    enum aurora_security_activity_view_state state
) {
    switch (state) {
        case AURORA_SECURITY_ACTIVITY_VIEW_LOADING:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_LOADING;
        case AURORA_SECURITY_ACTIVITY_VIEW_EMPTY:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_EMPTY;
        case AURORA_SECURITY_ACTIVITY_VIEW_END:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_END;
        case AURORA_SECURITY_ACTIVITY_VIEW_ERROR:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_ERROR;
        default:
            return AURORA_SECURITY_ACTIVITY_GRAPHIC_COUNT;
    }
}
