static void render_progress(void) {
    if (!initialized) {
        return;
    }

    /*
     * Restore only the lower status area between milestones. The expensive
     * bilinear fullscreen artwork is drawn once during boot_ui_init().
     */
    redraw_status_background();

    draw_text_shadowed_centered(
        active_fb.height * 50u / 100u,
        "SYSTEM INITIALIZATION",
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(146, 218, 242)
    );

    draw_bar(current_percent);
    draw_phrases(current_phrase);
}

/*
 * Capture-only pacing. This branch is never merged to main: the delay exists
 * solely so CI can record each real boot milestone as a visible frame.
 */
static void boot_capture_delay(void) {
    for (volatile uint64_t spin = 0;
         spin < 8000000ull;
         ++spin) {
        __asm__ volatile("pause");
    }
}

void boot_ui_init(
    const struct aurora_framebuffer *framebuffer
) {
    if (framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->bpp != 32u ||
        framebuffer->width == 0 ||
        framebuffer->height == 0) {
        return;
    }

    active_fb = *framebuffer;
    initialized = true;
    current_percent = 0;
    current_phrase = 0;

    image_asset_ready =
        prepare_image_asset();

    draw_scene();
    render_progress();
    boot_capture_delay();
}

bool boot_ui_is_initialized(void) {
    return initialized;
}

void boot_ui_stage(enum aurora_boot_stage stage) {
    if (!initialized ||
        stage > AURORA_BOOT_STAGE_READY) {
        return;
    }

    current_percent =
        stage_visuals[stage].percent;

    current_phrase =
        stage_visuals[stage].phrase_index;

    render_progress();
    boot_capture_delay();
}

void boot_ui_complete(void) {
    if (!initialized) {
        return;
    }

    current_percent = 100;
    current_phrase =
        (uint8_t)(
            sizeof(phrases) /
            sizeof(phrases[0]) -
            1u
        );

    render_progress();

    draw_text_shadowed_centered(
        active_fb.height * 88u / 100u,
        "AURORA IS READY",
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(169, 244, 255)
    );

    boot_capture_delay();
}

void boot_ui_panic(const char *reason) {
    if (!initialized) {
        return;
    }

    framebuffer_clear(
        &active_fb,
        rgb(13, 3, 12)
    );

    uint64_t top =
        active_fb.height / 4u;

    draw_text_centered(
        top,
        "AURORA KERNEL PANIC",
        active_fb.width >= 1000u ? 3u : 2u,
        rgb(255, 103, 170)
    );

    draw_text_centered(
        top + 70u,
        "THE SYSTEM HAS BEEN HALTED SAFELY",
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(220, 188, 208)
    );

    if (reason != NULL) {
        draw_text_centered(
            top + 120u,
            reason,
            1u,
            rgb(255, 200, 224)
        );
    }

    draw_text_centered(
        top + 180u,
        "TECHNICAL DETAILS REMAIN AVAILABLE ON SERIAL",
        1u,
        rgb(120, 105, 132)
    );
}
