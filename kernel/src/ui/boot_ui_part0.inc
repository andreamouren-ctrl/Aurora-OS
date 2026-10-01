#include <stddef.h>
#include <stdint.h>

#include <aurora/boot_ui.h>
#include <aurora/framebuffer.h>

#define FONT_WIDTH  5u
#define FONT_HEIGHT 7u

#define AURORA_BOOT_IMAGE_WIDTH   256u
#define AURORA_BOOT_IMAGE_HEIGHT  144u
#define AURORA_BOOT_PALETTE_COLORS 128u
#define AURORA_BOOT_PALETTE_BYTES (AURORA_BOOT_PALETTE_COLORS * 3u)
#define AURORA_BOOT_IMAGE_BYTES   (AURORA_BOOT_PALETTE_BYTES + \
                                   AURORA_BOOT_IMAGE_WIDTH * \
                                   AURORA_BOOT_IMAGE_HEIGHT)

extern const uint8_t aurora_boot_asset_start[];
extern const uint8_t aurora_boot_asset_end[];

struct glyph5x7 {
    char character;
    uint8_t rows[FONT_HEIGHT];
};

struct boot_stage_visual {
    uint8_t percent;
    uint8_t phrase_index;
};

static const struct glyph5x7 glyphs[] = {
    { ' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00} },
    { 'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },
    { 'C', {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F} },
    { 'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E} },
    { 'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} },
    { 'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },
    { 'G', {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F} },
    { 'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },
    { 'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E} },
    { 'J', {0x01,0x01,0x01,0x01,0x11,0x11,0x0E} },
    { 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} },
    { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },
    { 'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11} },
    { 'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11} },
    { 'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },
    { 'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D} },
    { 'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11} },
    { 'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E} },
    { 'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },
    { 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E} },
    { 'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04} },
    { 'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A} },
    { 'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11} },
    { 'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04} },
    { 'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },
    { '0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} },
    { '1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },
    { '2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F} },
    { '3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E} },
    { '4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} },
    { '5', {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E} },
    { '6', {0x0F,0x10,0x10,0x1E,0x11,0x11,0x0E} },
    { '7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08} },
    { '8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} },
    { '9', {0x0E,0x11,0x11,0x0F,0x01,0x01,0x1E} },
    { '.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C} },
    { ':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00} },
    { '-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '/', {0x01,0x02,0x02,0x04,0x08,0x08,0x10} },
    { '%', {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13} },
    { '\'',{0x04,0x04,0x08,0x00,0x00,0x00,0x00} }
};

static const struct boot_stage_visual stage_visuals[] = {
    [AURORA_BOOT_STAGE_FRAMEBUFFER]  = {  4, 0 },
    [AURORA_BOOT_STAGE_MEMORY]       = { 16, 0 },
    [AURORA_BOOT_STAGE_SECURITY]     = { 27, 1 },
    [AURORA_BOOT_STAGE_PLATFORM]     = { 39, 2 },
    [AURORA_BOOT_STAGE_CLOCK]        = { 49, 3 },
    [AURORA_BOOT_STAGE_SMP]          = { 58, 3 },
    [AURORA_BOOT_STAGE_TIMER]        = { 66, 4 },
    [AURORA_BOOT_STAGE_HEAP]         = { 74, 4 },
    [AURORA_BOOT_STAGE_CAPABILITIES] = { 82, 5 },
    [AURORA_BOOT_STAGE_IPC]          = { 88, 5 },
    [AURORA_BOOT_STAGE_SCHEDULER]    = { 93, 5 },
    [AURORA_BOOT_STAGE_USERSPACE]    = { 98, 6 },
    [AURORA_BOOT_STAGE_READY]        = {100, 6 }
};

static const char *const phrases[] = {
    "CREAZIONE DELL'UNIVERSO...",
    "ACCENSIONE DELLE PRIME STELLE...",
    "SORGERE DELL'AURORA...",
    "ALLINEAMENTO DEI FLUSSI COSMICI...",
    "RISVEGLIO DEL NUCLEO DI AURORA...",
    "SINCRONIZZAZIONE DEGLI ORIZZONTI...",
    "BENVENUTO NELL'INFINITO..."
};

static struct aurora_framebuffer active_fb;
static uint8_t boot_palette_rgb[AURORA_BOOT_PALETTE_COLORS][3];
static bool initialized;
static bool image_asset_ready;
static uint8_t current_percent;
static uint8_t current_phrase;

static uint32_t rgb(
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    return framebuffer_rgb(
        &active_fb,
        red,
        green,
        blue
    );
}

static void put_pixel(
    uint64_t x,
    uint64_t y,
    uint32_t color
) {
    if (!initialized ||
        x >= active_fb.width ||
        y >= active_fb.height) {
        return;
    }

    volatile uint8_t *base =
        (volatile uint8_t *)active_fb.address;

    volatile uint32_t *line =
        (volatile uint32_t *)(
            base + y * active_fb.pitch
        );

    line[x] = color;
}

static const uint8_t *glyph_for(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }

    for (size_t i = 0;
         i < sizeof(glyphs) / sizeof(glyphs[0]);
         ++i) {
        if (glyphs[i].character == character) {
            return glyphs[i].rows;
        }
    }

    return glyphs[0].rows;
}

static uint64_t text_width(
    const char *text,
    uint32_t scale
) {
    uint64_t count = 0;

    if (text == NULL) {
        return 0;
    }

    while (text[count] != '\0') {
        ++count;
    }

    if (count == 0) {
        return 0;
    }

    return count * ((FONT_WIDTH + 1u) * scale) - scale;
}

static void draw_char(
    uint64_t x,
    uint64_t y,
    char character,
    uint32_t scale,
    uint32_t color
) {
    const uint8_t *rows = glyph_for(character);

    for (uint32_t row = 0; row < FONT_HEIGHT; ++row) {
        for (uint32_t column = 0; column < FONT_WIDTH; ++column) {
            uint8_t mask =
                (uint8_t)(1u << (FONT_WIDTH - 1u - column));

            if ((rows[row] & mask) == 0) {
                continue;
            }

            framebuffer_fill_rect(
                &active_fb,
                x + column * scale,
                y + row * scale,
                scale,
                scale,
                color
            );
        }
    }
}

static void draw_text(
    uint64_t x,
    uint64_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
) {
    if (text == NULL) {
        return;
    }

    uint64_t cursor = x;

    while (*text != '\0') {
        draw_char(cursor, y, *text, scale, color);
        cursor += (FONT_WIDTH + 1u) * scale;
        ++text;
    }
}

static void draw_text_centered(
    uint64_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
) {
    uint64_t width = text_width(text, scale);
    uint64_t x =
        active_fb.width > width
            ? (active_fb.width - width) / 2u
            : 0u;

    draw_text(x, y, text, scale, color);
}

static void draw_text_shadowed_centered(
    uint64_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
) {
    uint64_t width = text_width(text, scale);
    uint64_t x =
        active_fb.width > width
            ? (active_fb.width - width) / 2u
            : 0u;

    draw_text(
        x + scale,
        y + scale,
        text,
        scale,
        rgb(1, 5, 14)
    );

    draw_text(x, y, text, scale, color);
}
