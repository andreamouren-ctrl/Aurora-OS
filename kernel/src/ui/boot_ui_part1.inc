static uint32_t blend_rgb(
    uint8_t ar,
    uint8_t ag,
    uint8_t ab,
    uint8_t br,
    uint8_t bg,
    uint8_t bb,
    uint32_t numerator,
    uint32_t denominator
) {
    if (denominator == 0) {
        denominator = 1;
    }

    uint32_t inverse = denominator - numerator;

    uint8_t red = (uint8_t)(
        ((uint32_t)ar * inverse +
         (uint32_t)br * numerator) /
        denominator
    );

    uint8_t green = (uint8_t)(
        ((uint32_t)ag * inverse +
         (uint32_t)bg * numerator) /
        denominator
    );

    uint8_t blue = (uint8_t)(
        ((uint32_t)ab * inverse +
         (uint32_t)bb * numerator) /
        denominator
    );

    return rgb(red, green, blue);
}

static bool prepare_image_asset(void) {
    uintptr_t start = (uintptr_t)aurora_boot_asset_start;
    uintptr_t end = (uintptr_t)aurora_boot_asset_end;

    if (end < start ||
        (end - start) != AURORA_BOOT_IMAGE_BYTES) {
        return false;
    }

    for (uint32_t i = 0;
         i < AURORA_BOOT_PALETTE_COLORS;
         ++i) {
        const uint8_t *entry =
            aurora_boot_asset_start + i * 3u;

        boot_palette_rgb[i][0] = entry[0];
        boot_palette_rgb[i][1] = entry[1];
        boot_palette_rgb[i][2] = entry[2];
    }

    return true;
}

/*
 * Compute the source crop used by CSS-style "cover" scaling. Aurora keeps
 * the artwork aspect ratio, fills the entire framebuffer, and crops excess
 * pixels symmetrically. No black bars and no stretching.
 */
static void image_cover_geometry(
    uint64_t *crop_x_fp,
    uint64_t *crop_y_fp,
    uint64_t *step_x_fp,
    uint64_t *step_y_fp
) {
    uint64_t x = 0;
    uint64_t y = 0;
    uint64_t width =
        (uint64_t)AURORA_BOOT_IMAGE_WIDTH << 16;
    uint64_t height =
        (uint64_t)AURORA_BOOT_IMAGE_HEIGHT << 16;

    uint64_t target_cross =
        active_fb.width * AURORA_BOOT_IMAGE_HEIGHT;
    uint64_t source_cross =
        active_fb.height * AURORA_BOOT_IMAGE_WIDTH;

    if (target_cross > source_cross) {
        height =
            ((uint64_t)AURORA_BOOT_IMAGE_WIDTH *
             active_fb.height << 16) /
            active_fb.width;

        y =
            (((uint64_t)AURORA_BOOT_IMAGE_HEIGHT << 16) -
             height) / 2u;
    } else if (target_cross < source_cross) {
        width =
            ((uint64_t)AURORA_BOOT_IMAGE_HEIGHT *
             active_fb.width << 16) /
            active_fb.height;

        x =
            (((uint64_t)AURORA_BOOT_IMAGE_WIDTH << 16) -
             width) / 2u;
    }

    *crop_x_fp = x;
    *crop_y_fp = y;
    *step_x_fp = width / active_fb.width;
    *step_y_fp = height / active_fb.height;
}

static void sample_image_rgb(
    uint64_t x,
    uint64_t y,
    uint8_t *red,
    uint8_t *green,
    uint8_t *blue
) {
    const uint8_t *indices =
        aurora_boot_asset_start + AURORA_BOOT_PALETTE_BYTES;

    if (x >= AURORA_BOOT_IMAGE_WIDTH) {
        x = AURORA_BOOT_IMAGE_WIDTH - 1u;
    }

    if (y >= AURORA_BOOT_IMAGE_HEIGHT) {
        y = AURORA_BOOT_IMAGE_HEIGHT - 1u;
    }

    uint8_t index =
        indices[y * AURORA_BOOT_IMAGE_WIDTH + x];

    if (index >= AURORA_BOOT_PALETTE_COLORS) {
        index = 0;
    }

    *red = boot_palette_rgb[index][0];
    *green = boot_palette_rgb[index][1];
    *blue = boot_palette_rgb[index][2];
}

static uint8_t lerp_channel(
    uint8_t first,
    uint8_t second,
    uint32_t fraction
) {
    uint32_t inverse = 65536u - fraction;

    return (uint8_t)(
        ((uint32_t)first * inverse +
         (uint32_t)second * fraction) >>
        16
    );
}

/*
 * Bilinear scaling lets the deliberately compact early-boot asset expand to
 * modern framebuffer sizes without a blocky nearest-neighbour appearance.
 */
static void draw_image_cover_region(
    uint64_t y_begin,
    uint64_t y_end
) {
    if (y_begin >= active_fb.height) {
        return;
    }

    if (y_end > active_fb.height) {
        y_end = active_fb.height;
    }

    if (y_end <= y_begin) {
        return;
    }

    uint64_t crop_x_fp;
    uint64_t crop_y_fp;
    uint64_t step_x_fp;
    uint64_t step_y_fp;

    image_cover_geometry(
        &crop_x_fp,
        &crop_y_fp,
        &step_x_fp,
        &step_y_fp
    );

    for (uint64_t y = y_begin;
         y < y_end;
         ++y) {
        uint64_t sy_fp =
            crop_y_fp + y * step_y_fp;

        uint64_t sy0 = sy_fp >> 16;
        uint64_t sy1 =
            sy0 + 1u < AURORA_BOOT_IMAGE_HEIGHT
                ? sy0 + 1u
                : sy0;

        uint32_t fy =
            (uint32_t)(sy_fp & 0xFFFFu);

        uint64_t sx_fp = crop_x_fp;

        for (uint64_t x = 0;
             x < active_fb.width;
             ++x) {
            uint64_t sx0 = sx_fp >> 16;
            uint64_t sx1 =
                sx0 + 1u < AURORA_BOOT_IMAGE_WIDTH
                    ? sx0 + 1u
                    : sx0;

            uint32_t fx =
                (uint32_t)(sx_fp & 0xFFFFu);

            uint8_t r00, g00, b00;
            uint8_t r10, g10, b10;
            uint8_t r01, g01, b01;
            uint8_t r11, g11, b11;

            sample_image_rgb(sx0, sy0, &r00, &g00, &b00);
            sample_image_rgb(sx1, sy0, &r10, &g10, &b10);
            sample_image_rgb(sx0, sy1, &r01, &g01, &b01);
            sample_image_rgb(sx1, sy1, &r11, &g11, &b11);

            uint8_t top_r = lerp_channel(r00, r10, fx);
            uint8_t top_g = lerp_channel(g00, g10, fx);
            uint8_t top_b = lerp_channel(b00, b10, fx);

            uint8_t bottom_r = lerp_channel(r01, r11, fx);
            uint8_t bottom_g = lerp_channel(g01, g11, fx);
            uint8_t bottom_b = lerp_channel(b01, b11, fx);

            put_pixel(
                x,
                y,
                rgb(
                    lerp_channel(top_r, bottom_r, fy),
                    lerp_channel(top_g, bottom_g, fy),
                    lerp_channel(top_b, bottom_b, fy)
                )
            );

            sx_fp += step_x_fp;
        }
    }
}
