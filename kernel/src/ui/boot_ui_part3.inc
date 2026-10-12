#include <aurora/login_ui.h>

static void render_progress(void) {
    if (!initialized) {
        return;
    }

    /*
     * Emergency-only status renderer. The image is first drawn in the fatal
     * recovery path; it is never published during a healthy bootstrap.
     */
    redraw_status_background();

    draw_text_shadowed_centered(
        active_fb.height * 68u / 100u,
        "INIZIALIZZAZIONE SISTEMA",
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(146, 218, 242)
    );

    draw_bar(current_percent);
    draw_phrases(current_phrase);
}

void boot_ui_init(
    const struct aurora_framebuffer *framebuffer
) {
    if (framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->bpp != 32u ||
        framebuffer->width == 0u ||
        framebuffer->height == 0u ||
        framebuffer->width > 8192u ||
        framebuffer->height > 8192u ||
        framebuffer->pitch < framebuffer->width * 4u ||
        framebuffer->pitch == 0u) {
        return;
    }

    active_fb = *framebuffer;
    initialized = true;
    current_percent = 0;
    current_phrase = 0;

    /*
     * Normal boot stays visually quiet until the complete trusted Identity
     * presentation becomes available. The primitive embedded splash is a
     * fallback for fatal boot failures, NOT the official Aurora boot logo.
     * Clear any firmware/bootloader residue once and keep its early code,
     * glyph atlas and artwork decoder dormant on the healthy path.
     */
    image_asset_ready = false;
    framebuffer_clear(&active_fb, rgb(2u, 7u, 18u));
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

    /* Track real milestones for serial diagnostics and, if required,
     * the emergency renderer. Do not paint the provisional splash. */
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

    /* Preserve the quiet framebuffer until Identity commits its entire
     * first frame; do not expose the primitive emergency artwork. */

    /*
     * Bootstrap hand-off: until Aurora has its compositor/session service,
     * transition directly from the native boot framebuffer to the dedicated
     * Aurora Identity login surface. The authentication backend remains a
     * future user-space service; this call only changes presentation state.
     */
    login_ui_init(&active_fb);
}

void boot_ui_panic(const char *reason) {
    if (!initialized) return;

    /*
     * Fatal path only: activate the independent early framebuffer renderer.
     * No filesystem, service, heap, compositor or user-profile capability is
     * needed. Embedded artwork can fail independently; its simple geometric
     * fallback is still readable in recovery mode.
     */
    image_asset_ready = prepare_image_asset();
    draw_scene();
    redraw_status_background();

    draw_text_shadowed_centered(
        active_fb.height * 68u / 100u,
        "AVVIO DI EMERGENZA",
        active_fb.width >= 1200u ? 2u : 1u,
        rgb(255u, 175u, 202u)
    );

    /* Retain the actual last completed initialization milestone. */
    draw_bar(current_percent);

    if (reason != NULL) {
        draw_text_centered(
            active_fb.height * 85u / 100u,
            reason, 1u, rgb(255u, 205u, 224u)
        );
    }

    draw_text_centered(
        active_fb.height * 89u / 100u,
        "DETTAGLI TECNICI SULLA PORTA SERIALE",
        1u, rgb(169u, 201u, 217u)
    );
}
