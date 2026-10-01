static void draw_image_cover(void) {
    draw_image_cover_region(0, active_fb.height);
}

static void draw_fallback_background(void) {
    for (uint64_t y = 0;
         y < active_fb.height;
         ++y) {
        uint32_t numerator = (uint32_t)y;
        uint32_t denominator =
            (uint32_t)(
                active_fb.height == 0
                    ? 1
                    : active_fb.height
            );

        uint32_t color =
            blend_rgb(
                2, 7, 18,
                8, 24, 48,
                numerator,
                denominator
            );

        framebuffer_fill_rect(
            &active_fb,
            0,
            y,
            active_fb.width,
            1,
            color
        );
    }

    draw_text_shadowed_centered(
        active_fb.height * 35u / 100u,
        "AURORA OS",
        active_fb.width >= 1000u ? 4u : 3u,
        rgb(202, 240, 255)
    );
}

static void draw_scene(void) {
    if (image_asset_ready) {
        draw_image_cover();
    } else {
        draw_fallback_background();
    }
}

static void redraw_status_background(void) {
    uint64_t y_begin =
        active_fb.height * 47u / 100u;

    uint64_t y_end =
        active_fb.height * 94u / 100u;

    if (image_asset_ready) {
        draw_image_cover_region(y_begin, y_end);
        return;
    }

    /*
     * The fallback is cheap enough to redraw in full and keeps its gradient
     * continuous if the embedded artwork ever fails validation.
     */
    draw_fallback_background();
}

static void draw_bar(uint8_t percent) {
    uint64_t bar_width =
        active_fb.width * 34u / 100u;

    if (bar_width < 220u) {
        bar_width = 220u;
    }

    if (bar_width > active_fb.width - 100u) {
        bar_width = active_fb.width - 100u;
    }

    uint64_t bar_height =
        active_fb.height / 55u;

    if (bar_height < 10u) {
        bar_height = 10u;
    }

    if (bar_height > 18u) {
        bar_height = 18u;
    }

    uint64_t x =
        (active_fb.width - bar_width) / 2u;

    uint64_t y =
        active_fb.height * 55u / 100u;

    framebuffer_fill_rect(
        &active_fb,
        x - 3u,
        y - 3u,
        bar_width + 6u,
        bar_height + 6u,
        rgb(2, 12, 28)
    );

    framebuffer_fill_rect(
        &active_fb,
        x - 1u,
        y - 1u,
        bar_width + 2u,
        bar_height + 2u,
        rgb(75, 207, 255)
    );

    framebuffer_fill_rect(
        &active_fb,
        x,
        y,
        bar_width,
        bar_height,
        rgb(3, 15, 31)
    );

    uint64_t fill =
        bar_width * percent / 100u;

    for (uint64_t column = 0;
         column < fill;
         ++column) {
        uint32_t color =
            blend_rgb(
                39, 228, 255,
                180, 72, 255,
                (uint32_t)column,
                (uint32_t)(fill == 0 ? 1 : fill)
            );

        framebuffer_fill_rect(
            &active_fb,
            x + column,
            y + 2u,
            1u,
            bar_height > 4u
                ? bar_height - 4u
                : bar_height,
            color
        );
    }

    char percent_text[5];

    if (percent >= 100u) {
        percent_text[0] = '1';
        percent_text[1] = '0';
        percent_text[2] = '0';
        percent_text[3] = '%';
        percent_text[4] = '\0';
    } else if (percent >= 10u) {
        percent_text[0] =
            (char)('0' + percent / 10u);
        percent_text[1] =
            (char)('0' + percent % 10u);
        percent_text[2] = '%';
        percent_text[3] = '\0';
    } else {
        percent_text[0] =
            (char)('0' + percent);
        percent_text[1] = '%';
        percent_text[2] = '\0';
    }

    draw_text(
        x + bar_width + 16u,
        y + (bar_height > 14u ? 1u : 0u),
        percent_text,
        2u,
        rgb(231, 248, 255)
    );
}

static void draw_phrases(uint8_t active_phrase) {
    const uint64_t count =
        sizeof(phrases) / sizeof(phrases[0]);

    uint32_t scale =
        active_fb.width >= 1200u ? 2u : 1u;

    uint64_t line_height =
        FONT_HEIGHT * scale + 8u;

    uint64_t start_y =
        active_fb.height * 62u / 100u;

    for (uint64_t i = 0;
         i < count;
         ++i) {
        uint32_t color;

        if (i < active_phrase) {
            color = rgb(90, 164, 195);
        } else if (i == active_phrase) {
            color = rgb(179, 245, 255);
        } else {
            color = rgb(45, 78, 104);
        }

        uint64_t width =
            text_width(phrases[i], scale);

        uint64_t x =
            active_fb.width > width
                ? (active_fb.width - width) / 2u
                : 0u;

        uint64_t y =
            start_y + i * line_height;

        uint64_t marker_x =
            x > 18u ? x - 18u : 2u;

        framebuffer_fill_rect(
            &active_fb,
            marker_x,
            y + 1u,
            6u,
            6u,
            i <= active_phrase
                ? rgb(61, 222, 255)
                : rgb(34, 65, 91)
        );

        draw_text(
            x + scale,
            y + scale,
            phrases[i],
            scale,
            rgb(1, 7, 17)
        );

        draw_text(
            x,
            y,
            phrases[i],
            scale,
            color
        );
    }
}
