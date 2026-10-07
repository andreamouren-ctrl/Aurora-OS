#ifndef AURORA_DISPLAY_BOOT_FRAMEBUFFER_H
#define AURORA_DISPLAY_BOOT_FRAMEBUFFER_H

#include <stdbool.h>

#include <aurora/boot.h>
#include <aurora/display_output.h>
#include <aurora/display_backbuffer.h>

#define AURORA_BOOT_FRAMEBUFFER_OUTPUT_ID UINT64_C(1)

struct aurora_boot_framebuffer_backend {
    struct aurora_framebuffer framebuffer;
    struct aurora_display_output output;
    bool ready;
};

bool display_boot_framebuffer_init(
    struct aurora_boot_framebuffer_backend *backend,
    const struct aurora_framebuffer *framebuffer
);

const struct aurora_display_output *display_boot_framebuffer_output(
    const struct aurora_boot_framebuffer_backend *backend
);

const struct aurora_framebuffer *display_boot_framebuffer_native(
    const struct aurora_boot_framebuffer_backend *backend
);

bool display_boot_framebuffer_present(
    const struct aurora_boot_framebuffer_backend *backend,
    const struct aurora_display_backbuffer *buffer
);

#endif
