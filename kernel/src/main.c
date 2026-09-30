#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/clock.h>
#include <aurora/framebuffer.h>
#include <aurora/heap.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/log.h>
#include <aurora/madt.h>
#include <aurora/panic.h>
#include <aurora/pmm.h>
#include <aurora/timer.h>
#include <aurora/version.h>
#include <aurora/vmm.h>

static const char *lapic_mode_name(void) {
    switch (lapic_current_mode()) {
        case LAPIC_MODE_XAPIC:
            return "xAPIC";

        case LAPIC_MODE_X2APIC:
            return "x2APIC";

        default:
            return "none";
    }
}

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

    if (!acpi_init()) {
        kernel_panic("ACPI initialization failed");
    }

    log_line("[acpi] RSDT/XSDT validated");

    if (!madt_init()) {
        kernel_panic("ACPI MADT topology initialization failed");
    }

    log_write("[madt] CPUs available: ");
    log_u64(madt_cpu_count());
    log_line("");

    log_write("[madt] I/O APICs: ");
    log_u64(madt_ioapic_count());
    log_line("");

    if (!interrupts_init()) {
        kernel_panic(
            "Interrupt descriptor table initialization failed"
        );
    }

    log_line("[idt] CPU exception handlers installed");

    if (!lapic_init()) {
        kernel_panic("Local APIC initialization failed");
    }

    log_write("[apic] mode: ");
    log_line(lapic_mode_name());

    log_write("[apic] bootstrap CPU APIC ID: ");
    log_u64(lapic_id());
    log_line("");

    if (!ioapic_init()) {
        kernel_panic("I/O APIC initialization failed");
    }

    log_line("[ioapic] external interrupts masked by default");

    if (!clock_init()) {
        kernel_panic("No reliable monotonic clock source");
    }

    log_write("[clock] source: ");
    log_line(clock_source_name());

    if (clock_tsc_frequency_hz() != 0) {
        log_write("[clock] calibrated TSC Hz: ");
        log_u64(clock_tsc_frequency_hz());
        log_line("");
    }

    if (!timer_init()) {
        kernel_panic("Tickless timer initialization failed");
    }

    log_write("[timer] mode: ");
    log_line(timer_mode_name());

    uint64_t timer_before =
        timer_interrupt_count();

    if (!timer_arm_ns(1000000ull)) {
        kernel_panic("Could not arm timer probe");
    }

    uint64_t timer_probe_deadline =
        clock_now_ns() + 100000000ull;

    arch_enable_interrupts();

    while (timer_interrupt_count() == timer_before &&
           clock_now_ns() < timer_probe_deadline) {
        arch_idle();
    }

    arch_disable_interrupts();

    if (timer_interrupt_count() == timer_before) {
        kernel_panic("Local APIC timer probe timed out");
    }

    log_line("[timer] one-shot interrupt probe passed");

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

    uint64_t clock_probe_start = clock_now_ns();
    clock_busy_wait_ns(1000000ull);
    uint64_t clock_probe_end = clock_now_ns();

    if (clock_probe_end <= clock_probe_start) {
        kernel_panic("Monotonic clock probe failed");
    }

    log_line("[clock] 1 ms monotonic probe passed");
    log_line("[kernel] M1 platform bootstrap reached successfully");

    arch_halt();
}
