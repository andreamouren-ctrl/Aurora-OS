#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/framebuffer.h>
#include <aurora/heap.h>
#include <aurora/interrupts.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/pmm.h>
#include <aurora/version.h>
#include <aurora/vmm.h>

void kmain(void) {
    arch_early_init();
    log_init();

    log_line("");
    log_line("Aurora OS kernel");
    log_line("Stage: " AURORA_STAGE);

    if (!boot_protocol_supported()) {
        kernel_panic("Unsupported Limine base revision");
    }

    struct aurora_framebuffer framebuffer;

    if (!boot_get_framebuffer(&framebuffer)) {
        kernel_panic("No supported 32-bit RGB framebuffer");
    }

    framebuffer_draw_boot_splash(&framebuffer);

    if (!pmm_init()) {
        kernel_panic("Physical memory manager initialization failed");
    }

    struct pmm_stats memory = pmm_get_stats();

    log_write("[pmm] usable pages: ");
    log_u64(memory.total_pages);
    log_line("");

    log_write("[pmm] free pages: ");
    log_u64(memory.free_pages);
    log_line("");

    if (!vmm_init()) {
        kernel_panic("Virtual memory manager initialization failed");
    }

    log_line("[vmm] current x86_64 page tables attached");

    if (!interrupts_init()) {
        kernel_panic(
            "Interrupt descriptor table initialization failed"
        );
    }

    log_line("[idt] CPU exception handlers installed");

    if (!kheap_init()) {
        kernel_panic("Kernel heap initialization failed");
    }

    void *probe = kheap_alloc(128, 16);

    if (probe == 0) {
        kernel_panic("Kernel heap probe allocation failed");
    }

    ((volatile uint8_t *)probe)[0] = 0xA5;
    ((volatile uint8_t *)probe)[127] = 0x5A;

    log_write("[heap] probe allocation at ");
    log_hex64((uint64_t)(uintptr_t)probe);
    log_line("");

    log_line("[kernel] M1 memory bootstrap reached successfully");

    arch_halt();
}
