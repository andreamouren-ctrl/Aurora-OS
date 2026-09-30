#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/framebuffer.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/version.h>

void kmain(void) {
    arch_early_init();
    log_init();

    log_line("");
    log_line("Aurora OS kernel");
    log_line("Stage: " AURORA_STAGE);
    log_line("[boot] early architecture initialization complete");

    if (!boot_protocol_supported()) {
        kernel_panic("Unsupported Limine base revision");
    }

    log_line("[boot] boot protocol accepted");

    struct aurora_framebuffer framebuffer;

    if (!boot_get_framebuffer(&framebuffer)) {
        kernel_panic("No supported 32-bit RGB framebuffer");
    }

    log_line("[gfx] framebuffer acquired");

    framebuffer_draw_boot_splash(&framebuffer);

    log_line("[kernel] M0 bootstrap reached successfully");
    log_line("[kernel] entering idle halt");

    arch_halt();
}
