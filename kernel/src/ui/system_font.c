#include <stddef.h>
#include <stdint.h>
#include <aurora/system_font.h>
#include <aurora/celestia_atlas_generated.h>

/* Atlas input is a host-rasterized, hash-verified release build product.
 * The kernel draws bounded bitmaps, never parsing TrueType at runtime. */
struct font_face {
    const struct aurora_celestia_glyph *glyphs;
    const uint8_t *pixels;
    uint32_t count;
    uint32_t px;
};

static struct font_face face_for(enum aurora_system_font_weight weight) {
    if (weight == AURORA_SYSTEM_FONT_BOLD) {
        return (struct font_face){
            aurora_celestia_bold_glyphs, aurora_celestia_bold_pixels,
            AURORA_CELESTIA_BOLD_COUNT, AURORA_CELESTIA_BOLD_PX
        };
    }
    return (struct font_face){
        aurora_celestia_regular_glyphs, aurora_celestia_regular_pixels,
        AURORA_CELESTIA_REGULAR_COUNT, AURORA_CELESTIA_REGULAR_PX
    };
}

bool aurora_system_font_ready(void) {
    return AURORA_CELESTIA_REGULAR_COUNT >= 190u &&
           AURORA_CELESTIA_BOLD_COUNT >= 190u;
}

static const struct aurora_celestia_glyph *find_glyph(
    const struct font_face *face, uint32_t codepoint
) {
    for (uint32_t i = 0u; i < face->count; ++i) {
        if (face->glyphs[i].codepoint == codepoint)
            return &face->glyphs[i];
    }
    /* Missing or unsupported Unicode never changes the identity buffer.
     * Use a visible U+003F fallback and advance the layout normally. */
    for (uint32_t i = 0u; i < face->count; ++i) {
        if (face->glyphs[i].codepoint == (uint32_t)'?')
            return &face->glyphs[i];
    }
    return NULL;
}

/* Reject overlong, surrogate and out-of-range UTF-8 sequences. The
 * caller always advances at least one byte on malformed input. */
static uint32_t next_codepoint(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = *s;
    if (code < 0x80u) {
        ++*cursor;
        return code;
    }
    uint32_t count = (code >= 0xC2u && code <= 0xDFu) ? 2u :
                     (code >= 0xE0u && code <= 0xEFu) ? 3u :
                     (code >= 0xF0u && code <= 0xF4u) ? 4u : 0u;
    if (count == 0u) {
        ++*cursor;
        return '?';
    }
    /* Never read past the NUL terminator; reject invalid continuation. */
    for (uint32_t i = 1u; i < count; ++i) {
        if (s[i] == 0u || (s[i] & 0xC0u) != 0x80u) {
            ++*cursor;
            return '?';
        }
    }
    code &= count == 2u ? 0x1Fu : count == 3u ? 0x0Fu : 0x07u;
    for (uint32_t i = 1u; i < count; ++i)
        code = (code << 6u) | (uint32_t)(s[i] & 0x3Fu);
    *cursor += count;
    if ((count == 2u && code < 0x80u) ||
        (count == 3u && code < 0x800u) ||
        (count == 4u && code < 0x10000u) ||
        (code >= 0xD800u && code <= 0xDFFFu) ||
        code > 0x10FFFFu)
        return '?';
    return code;
}

uint64_t aurora_system_font_text_width(
    const char *utf8, enum aurora_system_font_weight weight, uint32_t scale
) {
    if (!utf8 || !scale || scale > 2u) return 0u;
    const struct font_face face = face_for(weight);
    uint64_t width = 0u;
    /* Bound all text input: this is a small built-in Identity UI renderer. */
    for (uint32_t i = 0u; i < 512u && *utf8; ++i) {
        uint32_t code = next_codepoint(&utf8);
        const struct aurora_celestia_glyph *g = find_glyph(&face, code);
        if (g) width += (uint64_t)g->advance * scale;
    }
    return width;
}

/* Blend directly in the framebuffer's component masks. Only the
 * already validated, kernel-owned 32bpp buffer is read/written; no font
 * buffer is writable by the renderer. Non-32bpp fallback is conservative. */
static uint32_t blend_component(
    uint32_t background, uint32_t foreground,
    uint8_t bits, uint8_t shift, uint8_t alpha
) {
    if (bits == 0u || bits > 16u || shift >= 32u ||
        (uint32_t)bits + (uint32_t)shift > 32u)
        return 0u;
    uint32_t max = (1u << bits) - 1u;
    uint32_t old = (background >> shift) & max;
    uint32_t next = (foreground >> shift) & max;
    return ((old * (255u - alpha) + next * alpha + 127u) / 255u) << shift;
}

static void draw_coverage(
    const struct aurora_framebuffer *fb,
    uint64_t x, uint64_t y, uint32_t scale, uint32_t color,
    uint8_t alpha
) {
    if (alpha == 0u) return;
    if (fb->bpp != 32u || !fb->address ||
        fb->pitch < fb->width * sizeof(uint32_t) ||
        fb->red_mask_size > 16u || fb->green_mask_size > 16u ||
        fb->blue_mask_size > 16u) {
        if (alpha >= 112u)
            framebuffer_fill_rect(fb, x, y, scale, scale, color);
        return;
    }
    uint32_t red_mask=((1u<<fb->red_mask_size)-1u)<<fb->red_mask_shift;
    uint32_t green_mask=((1u<<fb->green_mask_size)-1u)<<fb->green_mask_shift;
    uint32_t blue_mask=((1u<<fb->blue_mask_size)-1u)<<fb->blue_mask_shift;
    uint32_t rgb_mask=red_mask|green_mask|blue_mask;
    volatile uint8_t *base=(volatile uint8_t *)fb->address;
    for (uint32_t yy=0u;yy<scale && y+yy<fb->height;++yy) {
        for (uint32_t xx=0u;xx<scale && x+xx<fb->width;++xx) {
            volatile uint32_t *destination=(volatile uint32_t *)
                (void *)(base+(y+yy)*fb->pitch+(x+xx)*sizeof(uint32_t));
            uint32_t bg=*destination;
            uint32_t mixed=(bg&~rgb_mask) |
                blend_component(bg,color,fb->red_mask_size,
                                fb->red_mask_shift,alpha) |
                blend_component(bg,color,fb->green_mask_size,
                                fb->green_mask_shift,alpha) |
                blend_component(bg,color,fb->blue_mask_size,
                                fb->blue_mask_shift,alpha);
            *destination=mixed;
        }
    }
}

void aurora_system_font_draw_text(
    const struct aurora_framebuffer *fb, uint64_t x, uint64_t y,
    const char *utf8, enum aurora_system_font_weight weight,
    uint32_t scale, uint32_t color
) {
    if (!fb || !utf8 || !scale || scale > 2u || !fb->width || !fb->height)
        return;
    const struct font_face face = face_for(weight);
    uint64_t cursor = x;
    for (uint32_t i = 0u; i < 512u && *utf8; ++i) {
        const struct aurora_celestia_glyph *g =
            find_glyph(&face, next_codepoint(&utf8));
        if (!g) continue;
        int64_t gx = (int64_t)cursor + (int64_t)g->left * scale;
        int64_t gy = (int64_t)y +
                     ((int64_t)face.px - (int64_t)g->top) * scale;
        for (uint32_t row = 0u; row < g->height; ++row) {
            for (uint32_t col = 0u; col < g->width; ++col) {
                uint8_t alpha = face.pixels[g->offset + row * g->width + col];
                if (alpha == 0u) continue;
                int64_t dx = gx + (int64_t)col * scale;
                int64_t dy = gy + (int64_t)row * scale;
                if (dx < 0 || dy < 0 ||
                    (uint64_t)dx >= fb->width ||
                    (uint64_t)dy >= fb->height)
                    continue;
                draw_coverage(fb, (uint64_t)dx, (uint64_t)dy,
                              scale, color, alpha);
            }
        }
        cursor += (uint64_t)g->advance * scale;
        if (cursor >= fb->width + 128u)
            break;
    }
}
