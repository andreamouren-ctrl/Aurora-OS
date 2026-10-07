#include <stddef.h>
#include <stdint.h>

#include <aurora/framebuffer.h>
#include <aurora/heap.h>
#include <aurora/identity_graphics.h>
#include <aurora/png.h>

extern const uint8_t aurora_identity_background_png_start[];
extern const uint8_t aurora_identity_background_png_end[];
extern const uint8_t aurora_identity_mark_png_start[];
extern const uint8_t aurora_identity_mark_png_end[];

static struct aurora_framebuffer identity_fb;
static uint32_t *identity_surface;
static size_t identity_surface_bytes;
static bool identity_ready;

static uint64_t min_u64(uint64_t first, uint64_t second) {
    return first < second ? first : second;
}

static uint8_t lerp_u8(uint8_t first, uint8_t second, uint32_t fraction) {
    uint32_t inverse = 65536u - fraction;
    return (uint8_t)(((uint32_t)first * inverse +
                      (uint32_t)second * fraction) >> 16);
}

static uint8_t unpack_channel(uint32_t pixel, uint8_t size, uint8_t shift) {
    if (size == 0u) return 0u;
    uint64_t max_value = size >= 32u
        ? UINT64_C(0xFFFFFFFF)
        : ((UINT64_C(1) << size) - 1u);
    uint64_t raw = ((uint64_t)pixel >> shift) & max_value;
    return (uint8_t)((raw * 255u + max_value / 2u) / max_value);
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
        size_t p00 = ((size_t)y0 * image->width + (size_t)x0) * image->channels + channel;
        size_t p10 = ((size_t)y0 * image->width + (size_t)x1) * image->channels + channel;
        size_t p01 = ((size_t)y1 * image->width + (size_t)x0) * image->channels + channel;
        size_t p11 = ((size_t)y1 * image->width + (size_t)x1) * image->channels + channel;
        uint8_t top = lerp_u8(image->pixels[p00], image->pixels[p10], fx);
        uint8_t bottom = lerp_u8(image->pixels[p01], image->pixels[p11], fx);
        rgba[channel] = lerp_u8(top, bottom, fy);
    }
    if (image->channels == 3u) rgba[3] = 255u;
}

static void cover_geometry(
    const struct aurora_png_image *image,
    uint64_t *crop_x_fp,
    uint64_t *crop_y_fp,
    uint64_t *step_x_fp,
    uint64_t *step_y_fp
) {
    uint64_t crop_x = 0u;
    uint64_t crop_y = 0u;
    uint64_t crop_width = (uint64_t)image->width << 16;
    uint64_t crop_height = (uint64_t)image->height << 16;
    uint64_t target_cross = identity_fb.width * image->height;
    uint64_t source_cross = identity_fb.height * image->width;

    if (target_cross > source_cross) {
        crop_height = ((uint64_t)image->width * identity_fb.height << 16) /
            identity_fb.width;
        crop_y = (((uint64_t)image->height << 16) - crop_height) / 2u;
    } else if (target_cross < source_cross) {
        crop_width = ((uint64_t)image->height * identity_fb.width << 16) /
            identity_fb.height;
        crop_x = (((uint64_t)image->width << 16) - crop_width) / 2u;
    }

    *crop_x_fp = crop_x;
    *crop_y_fp = crop_y;
    *step_x_fp = crop_width / identity_fb.width;
    *step_y_fp = crop_height / identity_fb.height;
}

static void render_background(const struct aurora_png_image *image) {
    if (image->width == identity_fb.width &&
        image->height == identity_fb.height &&
        (image->channels == 3u || image->channels == 4u)) {
        const uint8_t *source = image->pixels;
        size_t pixel_count =
            (size_t)identity_fb.width * (size_t)identity_fb.height;

        for (size_t i = 0u; i < pixel_count; ++i) {
            identity_surface[i] = framebuffer_rgb(
                &identity_fb,
                source[0],
                source[1],
                source[2]);
            source += image->channels;
        }
        return;
    }

    uint64_t crop_x_fp;
    uint64_t crop_y_fp;
    uint64_t step_x_fp;
    uint64_t step_y_fp;
    cover_geometry(image, &crop_x_fp, &crop_y_fp, &step_x_fp, &step_y_fp);

    for (uint64_t y = 0u; y < identity_fb.height; ++y) {
        uint64_t sy_fp = crop_y_fp + y * step_y_fp;
        uint64_t sx_fp = crop_x_fp;
        for (uint64_t x = 0u; x < identity_fb.width; ++x) {
            uint8_t rgba[4];
            sample_bilinear(image, sx_fp, sy_fp, rgba);
            identity_surface[y * identity_fb.width + x] =
                framebuffer_rgb(&identity_fb, rgba[0], rgba[1], rgba[2]);
            sx_fp += step_x_fp;
        }
    }
}

static void render_mark(const struct aurora_png_image *mark) {
    uint64_t shorter = min_u64(identity_fb.width, identity_fb.height);
    uint64_t size = shorter * 18u / 100u;
    if (size < 132u) size = 132u;
    if (size > 224u) size = 224u;
    if (size > identity_fb.width) size = identity_fb.width;
    if (size > identity_fb.height) size = identity_fb.height;

    uint64_t x0 = (identity_fb.width - size) / 2u;
    uint64_t y0 = identity_fb.height * 5u / 100u;
    if (y0 + size > identity_fb.height) y0 = identity_fb.height - size;

    uint64_t step_x_fp = size > 1u
        ? (((uint64_t)mark->width - 1u) << 16) / (size - 1u)
        : 0u;
    uint64_t step_y_fp = size > 1u
        ? (((uint64_t)mark->height - 1u) << 16) / (size - 1u)
        : 0u;

    for (uint64_t y = 0u; y < size; ++y) {
        uint64_t sy_fp = y * step_y_fp;
        for (uint64_t x = 0u; x < size; ++x) {
            uint8_t rgba[4];
            sample_bilinear(mark, x * step_x_fp, sy_fp, rgba);
            uint32_t alpha = rgba[3];
            if (alpha == 0u) continue;

            size_t index = (size_t)(y0 + y) * identity_fb.width + (size_t)(x0 + x);
            uint32_t destination = identity_surface[index];
            uint8_t dr = unpack_channel(
                destination, identity_fb.red_mask_size, identity_fb.red_mask_shift);
            uint8_t dg = unpack_channel(
                destination, identity_fb.green_mask_size, identity_fb.green_mask_shift);
            uint8_t db = unpack_channel(
                destination, identity_fb.blue_mask_size, identity_fb.blue_mask_shift);
            uint32_t inverse = 255u - alpha;
            uint8_t red = (uint8_t)(((uint32_t)rgba[0] * alpha +
                                     (uint32_t)dr * inverse + 127u) / 255u);
            uint8_t green = (uint8_t)(((uint32_t)rgba[1] * alpha +
                                       (uint32_t)dg * inverse + 127u) / 255u);
            uint8_t blue = (uint8_t)(((uint32_t)rgba[2] * alpha +
                                      (uint32_t)db * inverse + 127u) / 255u);
            identity_surface[index] = framebuffer_rgb(
                &identity_fb, red, green, blue);
        }
    }
}

void identity_graphics_release(void) {
    if (identity_surface != NULL && identity_surface_bytes != 0u) {
        (void)kheap_free_sized(identity_surface, identity_surface_bytes);
    }
    identity_surface = NULL;
    identity_surface_bytes = 0u;
    identity_ready = false;
}

bool identity_graphics_init(const struct aurora_framebuffer *framebuffer) {
    identity_graphics_release();
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->bpp != 32u || framebuffer->width == 0u ||
        framebuffer->height == 0u || framebuffer->width > 8192u ||
        framebuffer->height > 8192u) {
        return false;
    }
    if (framebuffer->width > SIZE_MAX / framebuffer->height ||
        framebuffer->width * framebuffer->height > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }
    identity_fb = *framebuffer;
    identity_surface_bytes = (size_t)identity_fb.width *
        (size_t)identity_fb.height * sizeof(uint32_t);
    identity_surface = (uint32_t *)kheap_alloc(identity_surface_bytes, 16u);
    if (identity_surface == NULL) {
        identity_surface_bytes = 0u;
        return false;
    }

    uintptr_t background_start = (uintptr_t)aurora_identity_background_png_start;
    uintptr_t background_end = (uintptr_t)aurora_identity_background_png_end;
    uintptr_t mark_start = (uintptr_t)aurora_identity_mark_png_start;
    uintptr_t mark_end = (uintptr_t)aurora_identity_mark_png_end;
    if (background_end <= background_start || mark_end <= mark_start) {
        identity_graphics_release();
        return false;
    }

    struct aurora_png_image background;
    struct aurora_png_image mark;
    if (!aurora_png_decode(
            aurora_identity_background_png_start,
            (size_t)(background_end - background_start),
            &background)) {
        identity_graphics_release();
        return false;
    }
    if (!aurora_png_decode(
            aurora_identity_mark_png_start,
            (size_t)(mark_end - mark_start),
            &mark)) {
        aurora_png_release(&background);
        identity_graphics_release();
        return false;
    }

    render_background(&background);
    render_mark(&mark);
    aurora_png_release(&mark);
    aurora_png_release(&background);
    identity_ready = true;
    return true;
}

bool identity_graphics_ready(void) {
    return identity_ready;
}

void identity_graphics_draw_base(void) {
    if (!identity_ready || identity_surface == NULL || identity_fb.address == NULL) {
        return;
    }
    volatile uint8_t *base = (volatile uint8_t *)identity_fb.address;
    for (uint64_t y = 0u; y < identity_fb.height; ++y) {
        volatile uint32_t *line =
            (volatile uint32_t *)(base + y * identity_fb.pitch);
        const uint32_t *source = identity_surface + y * identity_fb.width;
        for (uint64_t x = 0u; x < identity_fb.width; ++x) {
            line[x] = source[x];
        }
    }
}
