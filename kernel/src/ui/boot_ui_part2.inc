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
    /* The old 47%-94% full-width opaque strip hid the central Aurora mark.
     * Constrain the refreshed region to a compact lower status panel.
     * Repainting only this rectangle keeps milestone updates inexpensive. */
    const uint64_t x = active_fb.width * 12u / 100u;
    const uint64_t y = active_fb.height * 65u / 100u;
    const uint64_t width = active_fb.width - x * 2u;
    const uint64_t height = active_fb.height * 28u / 100u;
    framebuffer_fill_rect(&active_fb, x, y, width, height,
                          rgb(2, 9, 22));
    /* Quiet cyan top border ties the status card to the Aurora palette. */
    framebuffer_fill_rect(&active_fb, x, y, width, 2u,
                          rgb(34, 99, 138));
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
        active_fb.height * 76u / 100u;

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
    const uint64_t count = sizeof(phrases) / sizeof(phrases[0]);
    if (count == 0u) return;
    if (active_phrase >= count) active_phrase = (uint8_t)(count - 1u);

    /* One live milestone, not seven stacked lines and markers that
     * obscure the artwork and overrun short displays. */
    draw_text_shadowed_centered(
        active_fb.height * 85u / 100u,
        phrases[active_phrase],
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(169, 230, 246)
    );
}
