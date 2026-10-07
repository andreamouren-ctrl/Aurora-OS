#include <stddef.h>
#include <stdint.h>

#include <aurora/framebuffer.h>
#include <aurora/identity_graphics.h>
#include <aurora/login_ui.h>

#define LOGIN_FONT_WIDTH  5u
#define LOGIN_FONT_HEIGHT 7u
#define LOGIN_MAX_KEY_LENGTH 32u

struct login_glyph5x7 {
    char character;
    uint8_t rows[LOGIN_FONT_HEIGHT];
};

static const struct login_glyph5x7 login_glyphs[] = {
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
    { '-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C} },
    { ':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00} },
    { '*', {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00} },
    { '?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04} }
};

static struct aurora_framebuffer login_fb;
static bool login_initialized;
static enum aurora_login_state login_state;
static size_t masked_key_length;

static uint32_t login_rgb(
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    return framebuffer_rgb(&login_fb, red, green, blue);
}

static const uint8_t *login_glyph_for(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }

    for (size_t i = 0;
         i < sizeof(login_glyphs) / sizeof(login_glyphs[0]);
         ++i) {
        if (login_glyphs[i].character == character) {
            return login_glyphs[i].rows;
        }
    }

    return login_glyphs[0].rows;
}

static uint64_t login_text_width(
    const char *text,
    uint32_t scale
) {
    uint64_t count = 0;

    if (text == NULL || scale == 0u) {
        return 0;
    }

    while (text[count] != '\0') {
        ++count;
    }

    if (count == 0u) {
        return 0;
    }

    return count * ((LOGIN_FONT_WIDTH + 1u) * scale) - scale;
}

static void login_draw_char(
    uint64_t x,
    uint64_t y,
    char character,
    uint32_t scale,
    uint32_t color
) {
    const uint8_t *rows = login_glyph_for(character);

    for (uint32_t row = 0; row < LOGIN_FONT_HEIGHT; ++row) {
        for (uint32_t column = 0; column < LOGIN_FONT_WIDTH; ++column) {
            uint8_t mask =
                (uint8_t)(1u << (LOGIN_FONT_WIDTH - 1u - column));

            if ((rows[row] & mask) == 0u) {
                continue;
            }

            framebuffer_fill_rect(
                &login_fb,
                x + column * scale,
                y + row * scale,
                scale,
                scale,
                color
            );
        }
    }
}

static void login_draw_text(
    uint64_t x,
    uint64_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
) {
    if (text == NULL || scale == 0u) {
        return;
    }

    uint64_t cursor = x;

    while (*text != '\0') {
        login_draw_char(cursor, y, *text, scale, color);
        cursor += (LOGIN_FONT_WIDTH + 1u) * scale;
        ++text;
    }
}

static void login_draw_text_centered(
    uint64_t y,
    const char *text,
    uint32_t scale,
    uint32_t color
) {
    uint64_t width = login_text_width(text, scale);
    uint64_t x = login_fb.width > width
        ? (login_fb.width - width) / 2u
        : 0u;

    login_draw_text(x, y, text, scale, color);
}

static uint64_t login_min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static void login_draw_frame(
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint64_t thickness,
    uint32_t color
) {
    if (width == 0u || height == 0u || thickness == 0u) {
        return;
    }

    thickness = login_min_u64(thickness, width);
    thickness = login_min_u64(thickness, height);

    framebuffer_fill_rect(&login_fb, x, y, width, thickness, color);
    framebuffer_fill_rect(
        &login_fb,
        x,
        y + height - thickness,
        width,
        thickness,
        color
    );
    framebuffer_fill_rect(&login_fb, x, y, thickness, height, color);
    framebuffer_fill_rect(
        &login_fb,
        x + width - thickness,
        y,
        thickness,
        height,
        color
    );
}

static void login_draw_background(void) {
    if (identity_graphics_ready()) {
        identity_graphics_draw_base();
        return;
    }

    uint32_t deep_navy = login_rgb(4, 8, 20);
    uint32_t lower_navy = login_rgb(7, 13, 30);
    uint32_t violet_haze = login_rgb(18, 15, 42);
    uint32_t cyan_haze = login_rgb(8, 31, 48);

    framebuffer_clear(&login_fb, deep_navy);

    framebuffer_fill_rect(
        &login_fb,
        0,
        login_fb.height * 62u / 100u,
        login_fb.width,
        login_fb.height * 38u / 100u,
        lower_navy
    );

    framebuffer_fill_rect(
        &login_fb,
        login_fb.width * 5u / 100u,
        login_fb.height * 14u / 100u,
        login_fb.width * 38u / 100u,
        login_fb.height * 2u / 100u,
        violet_haze
    );

    framebuffer_fill_rect(
        &login_fb,
        login_fb.width * 56u / 100u,
        login_fb.height * 19u / 100u,
        login_fb.width * 34u / 100u,
        login_fb.height * 2u / 100u,
        cyan_haze
    );

    uint32_t star = login_rgb(118, 170, 196);
    uint32_t bright_star = login_rgb(186, 231, 244);
    uint32_t seed = 0x4155524Fu;

    for (uint32_t i = 0; i < 72u; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint64_t x = (uint64_t)(seed % (uint32_t)login_fb.width);

        seed = seed * 1664525u + 1013904223u;
        uint64_t y =
            (uint64_t)(seed % (uint32_t)(login_fb.height * 55u / 100u + 1u));

        uint64_t size = (i % 13u == 0u) ? 2u : 1u;
        framebuffer_fill_rect(
            &login_fb,
            x,
            y,
            size,
            size,
            size == 2u ? bright_star : star
        );
    }
}

static void login_draw_aurora_mark(void) {
    if (identity_graphics_ready()) {
        return;
    }

    uint64_t cx = login_fb.width / 2u;
    uint64_t top = login_fb.height * 15u / 100u;
    uint64_t span = login_fb.width >= 1600u ? 180u : 132u;
    uint64_t line = login_fb.width >= 1600u ? 5u : 4u;

    uint32_t cyan = login_rgb(84, 232, 255);
    uint32_t blue = login_rgb(87, 139, 255);
    uint32_t violet = login_rgb(158, 92, 255);

    framebuffer_fill_rect(
        &login_fb,
        cx - span / 2u,
        top + 42u,
        span,
        line,
        blue
    );

    framebuffer_fill_rect(
        &login_fb,
        cx - span / 3u,
        top + 20u,
        span * 2u / 3u,
        line,
        cyan
    );

    framebuffer_fill_rect(
        &login_fb,
        cx - span / 5u,
        top,
        span * 2u / 5u,
        line,
        violet
    );
}

static void login_build_mask(char *buffer, size_t capacity) {
    if (buffer == NULL || capacity == 0u) {
        return;
    }

    size_t out = 0u;
    size_t count = masked_key_length;

    if (count > LOGIN_MAX_KEY_LENGTH) {
        count = LOGIN_MAX_KEY_LENGTH;
    }

    for (size_t i = 0u; i < count && out + 1u < capacity; ++i) {
        if (i != 0u && (i % 4u) == 0u) {
            if (out + 2u >= capacity) {
                break;
            }
            buffer[out++] = '-';
        }

        buffer[out++] = '*';
    }

    buffer[out] = '\0';
}

static const char *login_status_text(void) {
    switch (login_state) {
        case AURORA_LOGIN_AUTHENTICATING:
            return "VERIFYING AURORA KEY...";

        case AURORA_LOGIN_UNKNOWN_IDENTITY:
            return "AURORA KEY NOT ACCEPTED";

        case AURORA_LOGIN_CREATE_ENTRY:
            return "FIRST-USER CREATION";

        case AURORA_LOGIN_CREATING:
            return "CREATING AURORA IDENTITY...";

        case AURORA_LOGIN_CREATED:
            return "IDENTITY CREATED - SIGN IN WITH YOUR KEY";

        case AURORA_LOGIN_CREATE_DENIED:
            return "PROFILE CREATION IS NOT AVAILABLE";

        case AURORA_LOGIN_ERROR:
            return "AURORA KEY NOT ACCEPTED";

        case AURORA_LOGIN_THROTTLED:
            return "TOO MANY ATTEMPTS - TRY AGAIN LATER";

        case AURORA_LOGIN_SESSION_ACTIVE:
            return "AUTHENTICATED SESSION ACTIVE";

        case AURORA_LOGIN_IDLE:
        default:
            return "LOCAL OFFLINE ACCESS";
    }
}

static const char *login_prompt_text(void) {
    if (login_state == AURORA_LOGIN_SESSION_ACTIVE) {
        return "AURORA IDENTITY VERIFIED";
    }

    return login_state == AURORA_LOGIN_CREATE_ENTRY ||
           login_state == AURORA_LOGIN_CREATING
        ? "ENTER A NEW AURORA KEY"
        : "ENTER YOUR AURORA KEY";
}

static const char *login_instruction_text(void) {
    switch (login_state) {
        case AURORA_LOGIN_CREATE_ENTRY:
            return "PRESS ENTER TO CREATE - ESC TO CANCEL";

        case AURORA_LOGIN_CREATED:
        case AURORA_LOGIN_CREATE_DENIED:
            return "PRESS ENTER TO RETURN TO SIGN IN";

        case AURORA_LOGIN_UNKNOWN_IDENTITY:
            return "";

        case AURORA_LOGIN_SESSION_ACTIVE:
            return "SESSION MANAGER BOUND TO LOCAL IDENTITY";

        default:
            return "PRESS ENTER TO CONTINUE";
    }
}

static void login_draw_unknown_identity_dialog(void) {
    if (login_state != AURORA_LOGIN_UNKNOWN_IDENTITY) {
        return;
    }

    uint64_t width = login_fb.width * 48u / 100u;
    uint64_t max_width = 720u;

    if (width > max_width) {
        width = max_width;
    }

    uint64_t height = login_fb.height >= 900u ? 210u : 174u;
    uint64_t x = (login_fb.width - width) / 2u;
    uint64_t y = login_fb.height * 67u / 100u;

    if (y + height >= login_fb.height) {
        y = login_fb.height - height - 24u;
    }

    uint32_t panel = login_rgb(8, 15, 31);
    uint32_t border = login_rgb(96, 213, 239);
    uint32_t text = login_rgb(211, 239, 247);
    uint32_t dim = login_rgb(118, 153, 170);

    framebuffer_fill_rect(&login_fb, x, y, width, height, panel);
    login_draw_frame(x, y, width, height, 2u, border);

    uint32_t normal_scale = login_fb.width >= 1500u ? 2u : 1u;

    login_draw_text_centered(
        y + height * 20u / 100u,
        "AURORA KEY NOT ACCEPTED",
        normal_scale,
        text
    );

    login_draw_text_centered(
        y + height * 44u / 100u,
        "CREATE A NEW AURORA PROFILE?",
        normal_scale,
        dim
    );

    login_draw_text_centered(
        y + height * 69u / 100u,
        "ENTER CREATE     ESC CANCEL",
        normal_scale,
        border
    );
}

void login_ui_render(void) {
    if (!login_initialized) {
        return;
    }

    login_draw_background();
    login_draw_aurora_mark();

    uint32_t title_scale =
        login_fb.width >= 1700u ? 5u :
        login_fb.width >= 1200u ? 4u : 3u;

    uint32_t text_scale =
        login_fb.width >= 1500u ? 2u : 1u;

    uint32_t title = login_rgb(218, 246, 252);
    uint32_t accent = login_rgb(94, 224, 247);
    uint32_t muted = login_rgb(109, 143, 163);
    uint32_t field_fill = login_rgb(5, 12, 26);
    uint32_t field_border = login_rgb(71, 168, 205);
    uint32_t field_border_active = login_rgb(105, 232, 250);

    login_draw_text_centered(
        login_fb.height * 25u / 100u,
        "AURORA",
        title_scale,
        title
    );

    login_draw_text_centered(
        login_fb.height * 33u / 100u,
        "IDENTITY",
        text_scale,
        accent
    );

    login_draw_text_centered(
        login_fb.height * 42u / 100u,
        login_prompt_text(),
        text_scale,
        muted
    );

    uint64_t field_width = login_fb.width * 52u / 100u;
    uint64_t max_field_width = 820u;
    uint64_t min_field_width = login_fb.width * 72u / 100u;

    if (login_fb.width >= 1000u && field_width > max_field_width) {
        field_width = max_field_width;
    }

    if (login_fb.width < 1000u) {
        field_width = min_field_width;
    }

    uint64_t field_height = login_fb.height >= 900u ? 78u : 62u;
    uint64_t field_x = (login_fb.width - field_width) / 2u;
    uint64_t field_y = login_fb.height * 49u / 100u;

    framebuffer_fill_rect(
        &login_fb,
        field_x,
        field_y,
        field_width,
        field_height,
        field_fill
    );

    login_draw_frame(
        field_x,
        field_y,
        field_width,
        field_height,
        2u,
        login_state == AURORA_LOGIN_AUTHENTICATING ||
        login_state == AURORA_LOGIN_CREATING
            ? field_border_active
            : field_border
    );

    if (masked_key_length == 0u) {
        login_draw_text_centered(
            field_y + (field_height - LOGIN_FONT_HEIGHT * text_scale) / 2u,
            "AURORA KEY",
            text_scale,
            muted
        );
    } else {
        char mask_buffer[48];
        login_build_mask(mask_buffer, sizeof(mask_buffer));
        login_draw_text_centered(
            field_y + (field_height - LOGIN_FONT_HEIGHT * text_scale) / 2u,
            mask_buffer,
            text_scale,
            title
        );
    }

    login_draw_text_centered(
        field_y + field_height +
            (login_fb.height >= 900u ? 34u : 24u),
        login_instruction_text(),
        1u,
        muted
    );

    login_draw_text_centered(
        login_fb.height * 62u / 100u,
        login_status_text(),
        1u,
        login_state == AURORA_LOGIN_ERROR ||
        login_state == AURORA_LOGIN_THROTTLED ||
        login_state == AURORA_LOGIN_CREATE_DENIED
            ? login_rgb(255, 137, 175)
            : login_state == AURORA_LOGIN_CREATED
                ? login_rgb(125, 236, 192)
                : muted
    );

    login_draw_text_centered(
        login_fb.height * 92u / 100u,
        "AURORA OS - IDENTITY",
        1u,
        login_rgb(67, 94, 112)
    );

    login_draw_unknown_identity_dialog();
}

void login_ui_init(
    const struct aurora_framebuffer *framebuffer
) {
    if (framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->bpp != 32u ||
        framebuffer->width == 0u ||
        framebuffer->height == 0u) {
        return;
    }

    login_fb = *framebuffer;
    login_initialized = true;
    login_state = AURORA_LOGIN_IDLE;
    masked_key_length = 0u;

    /*
     * Keep the boot surface visible until native Identity artwork is decoded.
     * This prevents a visible flash of the emergency procedural fallback.
     */
}

bool login_ui_is_initialized(void) {
    return login_initialized;
}

bool login_ui_activate_native_artwork(void) {
    if (!login_initialized) {
        return false;
    }

    if (!identity_graphics_ready() &&
        !identity_graphics_init(&login_fb)) {
        return false;
    }

    login_ui_render();
    return true;
}

void login_ui_set_state(
    enum aurora_login_state state
) {
    if (!login_initialized) {
        return;
    }

    if (state > AURORA_LOGIN_THROTTLED) {
        state = AURORA_LOGIN_ERROR;
    }

    login_state = state;
    login_ui_render();
}

void login_ui_set_masked_length(
    size_t normalized_length
) {
    if (!login_initialized) {
        return;
    }

    if (normalized_length > LOGIN_MAX_KEY_LENGTH) {
        normalized_length = LOGIN_MAX_KEY_LENGTH;
    }

    masked_key_length = normalized_length;
    login_ui_render();
}
