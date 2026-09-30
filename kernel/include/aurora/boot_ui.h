#ifndef AURORA_BOOT_UI_H
#define AURORA_BOOT_UI_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/boot.h>

enum aurora_boot_stage {
    AURORA_BOOT_STAGE_FRAMEBUFFER = 0,
    AURORA_BOOT_STAGE_MEMORY,
    AURORA_BOOT_STAGE_SECURITY,
    AURORA_BOOT_STAGE_PLATFORM,
    AURORA_BOOT_STAGE_CLOCK,
    AURORA_BOOT_STAGE_SMP,
    AURORA_BOOT_STAGE_TIMER,
    AURORA_BOOT_STAGE_HEAP,
    AURORA_BOOT_STAGE_CAPABILITIES,
    AURORA_BOOT_STAGE_IPC,
    AURORA_BOOT_STAGE_SCHEDULER,
    AURORA_BOOT_STAGE_USERSPACE,
    AURORA_BOOT_STAGE_READY
};

void boot_ui_init(
    const struct aurora_framebuffer *framebuffer
);

bool boot_ui_is_initialized(void);

void boot_ui_stage(
    enum aurora_boot_stage stage
);

void boot_ui_complete(void);

void boot_ui_panic(
    const char *reason
);

#endif
