#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/boot_ui.h>
#include <aurora/clock.h>
#include <aurora/capability.h>
#include <aurora/cpu_local.h>
#include <aurora/framebuffer.h>
#include <aurora/gdt.h>
#include <aurora/heap.h>
#include <aurora/hpet.h>
#include <aurora/input.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/ipc.h>
#include <aurora/log.h>
#include <aurora/login_input.h>
#include <aurora/madt.h>
#include <aurora/panic.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/ps2_keyboard.h>
#include <aurora/scheduler.h>
#include <aurora/smp.h>
#include <aurora/syscall.h>
#include <aurora/timer.h>
#include <aurora/user_ipc_probe.h>
#include <aurora/user_probe.h>
#include <aurora/usercopy.h>
#include <aurora/version.h>
#include <aurora/vmm.h>

#if AURORA_BOOT_VALIDATION
static volatile uint64_t scheduler_probe_value;
static struct aurora_ipc_channel ring3_ipc_probe_channel;
static struct aurora_cap_table ring3_ipc_kernel_caps;
#endif

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

#if AURORA_BOOT_VALIDATION
static void scheduler_probe_thread(
    void *argument
) {
    volatile uint64_t *value =
        argument;

    if (value != 0) {
        *value =
            0x4155524F52414F53ull;
    }
}
#endif

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

    boot_ui_init(&framebuffer);
    boot_ui_stage(
        AURORA_BOOT_STAGE_FRAMEBUFFER
    );

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

    uint64_t boot_cpu_count =
        boot_smp_cpu_count();

    uint32_t bsp_slot = 0;
    uint32_t bsp_lapic_id = 0;
    bool bsp_found = false;

    for (uint64_t i = 0;
         i < boot_cpu_count;
         ++i) {
        struct aurora_boot_cpu cpu;

        if (boot_smp_cpu_at(i, &cpu) &&
            cpu.bootstrap) {
            bsp_slot = (uint32_t)i;
            bsp_lapic_id = cpu.lapic_id;
            bsp_found = true;
            break;
        }
    }

    if (!bsp_found ||
        !cpu_local_init_bootstrap(
            bsp_slot,
            bsp_lapic_id)) {
        kernel_panic("Bootstrap CPU-local initialization failed");
    }

    log_line("[cpu] bootstrap CPU-local state initialized");

    if (!vmm_init()) {
        kernel_panic("Virtual memory manager initialization failed");
    }

    log_line("[vmm] current x86_64 page tables attached");

    boot_ui_stage(
        AURORA_BOOT_STAGE_MEMORY
    );

    if (!gdt_init_bsp(bsp_slot)) {
        kernel_panic("Aurora GDT/TSS initialization failed");
    }

    log_line("[gdt] Aurora Ring 0 / Ring 3 segments installed");

    struct aurora_arch_hardening hardening =
        arch_enable_hardening();

    log_write("[security] CR0.WP: ");
    log_line(hardening.write_protect ? "on" : "off");

    log_write("[security] SMEP: ");
    log_line(hardening.smep ? "on" : "unsupported");

    log_write("[security] SMAP: ");
    log_line(hardening.smap ? "on" : "unsupported");

    log_write("[security] UMIP: ");
    log_line(hardening.umip ? "on" : "unsupported");

    if (!syscall_init()) {
        kernel_panic("x86_64 SYSCALL initialization failed");
    }

    log_line("[syscall] SYSCALL/SYSRET ABI installed");

    boot_ui_stage(
        AURORA_BOOT_STAGE_SECURITY
    );

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

    boot_ui_stage(
        AURORA_BOOT_STAGE_PLATFORM
    );

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

    boot_ui_stage(
        AURORA_BOOT_STAGE_CLOCK
    );

    if (!smp_init()) {
        kernel_panic("SMP bootstrap initialization failed");
    }

    log_write("[smp] CPUs reported: ");
    log_u64(smp_cpu_count());
    log_line("");

    log_write("[smp] CPUs online: ");
    log_u64(smp_online_cpu_count());
    log_line("");

    log_line("[smp] CPU-local execution state ready; APs awaiting scheduler release");

    boot_ui_stage(
        AURORA_BOOT_STAGE_SMP
    );

    if (!timer_init()) {
        kernel_panic("Tickless timer initialization failed");
    }

    log_write("[timer] mode: ");
    log_line(timer_mode_name());

#if AURORA_BOOT_VALIDATION
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
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_TIMER
    );

    if (!kheap_init()) {
        kernel_panic("Kernel heap initialization failed");
    }

#if AURORA_BOOT_VALIDATION
    void *probe = kheap_alloc(128, 16);

    if (probe == 0) {
        kernel_panic("Kernel heap probe allocation failed");
    }

    ((volatile uint8_t *)probe)[0] = 0xA5;
    ((volatile uint8_t *)probe)[127] = 0x5A;

    log_write("[heap] probe allocation at ");
    log_hex64((uint64_t)(uintptr_t)probe);
    log_line("");

    uint64_t clock_probe_start =
        clock_now_ns();

    clock_busy_wait_ns(1000000ull);

    uint64_t clock_probe_end =
        clock_now_ns();

    if (clock_probe_end <= clock_probe_start) {
        kernel_panic("Monotonic clock probe failed");
    }

    log_line("[clock] 1 ms monotonic probe passed");
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_HEAP
    );

#if AURORA_BOOT_VALIDATION
    if (!capability_self_test()) {
        kernel_panic("Capability security self-test failed");
    }

    log_line("[cap] typed capability self-test passed");
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_CAPABILITIES
    );

#if AURORA_BOOT_VALIDATION
    if (!ipc_self_test()) {
        kernel_panic("IPC capability-transfer self-test failed");
    }

    log_line("[ipc] bounded capability-transfer self-test passed");
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_IPC
    );

    if (!scheduler_init()) {
        kernel_panic("Scheduler initialization failed");
    }

    for (uint32_t i = 0u; i < smp_cpu_count(); ++i) {
        const struct aurora_cpu_runtime *cpu = smp_cpu_at(i);
        if (cpu == 0 || cpu->bootstrap) continue;

        if (cpu->state != AURORA_CPU_ONLINE ||
            !scheduler_prepare_ap(cpu->logical_id)) {
            kernel_panic("Could not prepare AP scheduler idle ownership");
        }
    }

#if AURORA_BOOT_VALIDATION
    scheduler_probe_value = 0;

    aurora_thread_id probe_thread =
        scheduler_create_kernel_thread(
            "scheduler-probe",
            scheduler_probe_thread,
            (void *)&scheduler_probe_value
        );

    if (probe_thread == 0) {
        kernel_panic(
            "Could not create scheduler probe thread"
        );
    }
#endif

    if (!scheduler_start()) {
        kernel_panic("Could not start scheduler");
    }

    smp_release_scheduler_aps();

    uint64_t smp_scheduler_deadline =
        clock_now_ns() + 250000000ull;

    while (smp_scheduler_owned_cpu_count() != smp_online_cpu_count() &&
           clock_now_ns() < smp_scheduler_deadline) {
        arch_idle();
    }

    if (smp_scheduler_owned_cpu_count() != smp_online_cpu_count()) {
        kernel_panic("AP scheduler ownership handoff timed out");
    }

    log_write("[sched] scheduler-owned CPUs: ");
    log_u64(smp_scheduler_owned_cpu_count());
    log_line("");

#if AURORA_BOOT_VALIDATION
    uint32_t expected_ap_timer_cpus =
        smp_online_cpu_count() > 0u
            ? smp_online_cpu_count() - 1u
            : 0u;
    uint32_t ap_timer_cpus = 0u;
    uint64_t ap_timer_deadline =
        clock_now_ns() + 250000000ull;

    do {
        ap_timer_cpus = 0u;

        for (uint32_t i = 0u; i < smp_cpu_count(); ++i) {
            const struct aurora_cpu_runtime *cpu = smp_cpu_at(i);
            if (cpu == 0 || cpu->bootstrap || cpu->state != AURORA_CPU_ONLINE) {
                continue;
            }

            if (timer_interrupt_count_cpu(cpu->logical_id) > 0u) {
                ++ap_timer_cpus;
            }
        }

        if (ap_timer_cpus == expected_ap_timer_cpus) {
            break;
        }

        arch_idle();
    } while (clock_now_ns() < ap_timer_deadline);

    if (ap_timer_cpus != expected_ap_timer_cpus) {
        kernel_panic("AP Local APIC timer preemption probe timed out");
    }

    log_write("[sched] AP local timer preemption CPUs: ");
    log_u64(ap_timer_cpus);
    log_line("");

    uint64_t scheduler_deadline =
        clock_now_ns() + 250000000ull;

    while (!scheduler_thread_finished(
                probe_thread) &&
           clock_now_ns() <
                scheduler_deadline) {
        arch_idle();
    }

    if (!scheduler_thread_finished(
            probe_thread) ||
        scheduler_probe_value !=
            0x4155524F52414F53ull) {
        kernel_panic(
            "Preemptive scheduler probe failed"
        );
    }

    log_write("[sched] context switches: ");
    log_u64(
        scheduler_context_switch_count()
    );
    log_line("");

    log_line("[sched] preemptive kernel thread probe passed");
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_SCHEDULER
    );

#if AURORA_BOOT_VALIDATION
    struct aurora_process *user_process =
        process_create_image(
            "ring3-probe",
            user_probe_image(),
            user_probe_image_size()
        );

    if (user_process == NULL) {
        kernel_panic("Could not create Ring 3 probe process");
    }

    uint64_t usercopy_source =
        0x5543455250524F42ull;

    uint64_t usercopy_result = 0;

    uint64_t usercopy_address =
        user_process->user_stack_top - 64ull;

    if (!copy_to_user(
            user_process,
            usercopy_address,
            &usercopy_source,
            sizeof(usercopy_source)) ||
        !copy_from_user(
            user_process,
            &usercopy_result,
            usercopy_address,
            sizeof(usercopy_result)) ||
        usercopy_result != usercopy_source) {
        kernel_panic(
            "Permission-checked usercopy self-test failed"
        );
    }

    log_line("[usercopy] checked user memory copy passed");

    aurora_thread_id user_thread =
        scheduler_create_user_thread(
            "ring3-probe-main",
            user_process
        );

    if (user_thread == 0) {
        kernel_panic("Could not create Ring 3 probe thread");
    }

    uint64_t ring3_deadline =
        clock_now_ns() + 500000000ull;

    while ((process_bootstrap_signal(
                user_process) !=
                AURORA_USER_PROBE_MAGIC ||
            !scheduler_thread_finished(
                user_thread)) &&
           clock_now_ns() <
                ring3_deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(
            user_process) !=
            AURORA_USER_PROBE_MAGIC ||
        !scheduler_thread_finished(
            user_thread) ||
        process_state(user_process) !=
            AURORA_PROCESS_EXITED) {
        kernel_panic(
            "Ring 3 SYSCALL/EXIT probe timed out"
        );
    }

    log_line("[ring3] isolated user process reached SYSCALL");
    log_line("[ring3] SYSRET returned to Ring 3 and EXIT switched back safely");
    log_line("[ring3] private CR3 + user stack + kernel stack path passed");

    ipc_channel_init(&ring3_ipc_probe_channel);
    cap_table_init(&ring3_ipc_kernel_caps);

    struct aurora_ipc_endpoint *ring3_endpoint =
        ipc_channel_endpoint(&ring3_ipc_probe_channel, 0u);
    struct aurora_ipc_endpoint *kernel_endpoint =
        ipc_channel_endpoint(&ring3_ipc_probe_channel, 1u);

    if (ring3_endpoint == NULL || kernel_endpoint == NULL) {
        kernel_panic("Could not create Ring 3 IPC probe channel");
    }

    struct aurora_process *ipc_process =
        process_create_image(
            "ring3-ipc-probe",
            user_ipc_probe_image(),
            user_ipc_probe_image_size()
        );

    if (ipc_process == NULL) {
        kernel_panic("Could not create Ring 3 IPC probe process");
    }

    aurora_cap_handle ipc_handle = cap_grant(
        &ipc_process->capabilities,
        ring3_endpoint,
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
    );

    if (ipc_handle == AURORA_CAP_INVALID) {
        kernel_panic("Could not grant Ring 3 IPC endpoint capability");
    }

    uint64_t ipc_handle_value = (uint64_t)ipc_handle;
    uint64_t ipc_handle_address = ipc_process->user_stack_top - 8ull;

    if (!copy_to_user(
            ipc_process,
            ipc_handle_address,
            &ipc_handle_value,
            sizeof(ipc_handle_value))) {
        kernel_panic("Could not publish Ring 3 IPC endpoint handle");
    }

    static const uint8_t ipc_probe_payload[] = {
        'A', 'U', 'R', 'O', 'R', 'A', '-', 'R',
        'I', 'N', 'G', '3', '-', 'I', 'P', 'C'
    };

    if (!ipc_send(
            kernel_endpoint,
            NULL,
            ipc_probe_payload,
            (uint32_t)sizeof(ipc_probe_payload),
            NULL,
            0u)) {
        kernel_panic("Could not queue Ring 3 IPC probe message");
    }

    aurora_thread_id ipc_thread =
        scheduler_create_user_thread(
            "ring3-ipc-probe-main",
            ipc_process
        );

    if (ipc_thread == 0u) {
        kernel_panic("Could not create Ring 3 IPC probe thread");
    }

    uint64_t ipc_deadline = clock_now_ns() + 500000000ull;

    while ((process_bootstrap_signal(ipc_process) != AURORA_USER_IPC_PROBE_MAGIC ||
            !scheduler_thread_finished(ipc_thread)) &&
           clock_now_ns() < ipc_deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(ipc_process) != AURORA_USER_IPC_PROBE_MAGIC ||
        !scheduler_thread_finished(ipc_thread) ||
        process_state(ipc_process) != AURORA_PROCESS_EXITED) {
        kernel_panic("Ring 3 IPC syscall probe timed out");
    }

    struct aurora_ipc_received ipc_echo;

    if (!ipc_receive(
            kernel_endpoint,
            &ring3_ipc_kernel_caps,
            &ipc_echo) ||
        ipc_echo.length != sizeof(ipc_probe_payload) ||
        ipc_echo.capability_count != 0u) {
        kernel_panic("Ring 3 IPC syscall echo metadata mismatch");
    }

    for (uint32_t i = 0u; i < ipc_echo.length; ++i) {
        if (ipc_echo.data[i] != ipc_probe_payload[i]) {
            kernel_panic("Ring 3 IPC syscall echo payload mismatch");
        }
    }

    log_line("[ring3-ipc] capability-gated send/receive syscall round-trip passed");
#endif

    boot_ui_stage(
        AURORA_BOOT_STAGE_USERSPACE
    );

    input_init();

    if (ps2_keyboard_init(lapic_id())) {
        log_line("[input] PS/2 keyboard IRQ path online");
    } else {
        log_line("[input] PS/2 keyboard unavailable; waiting for another input driver");
    }

    boot_ui_complete();
    login_input_init();

    log_line("[kernel] M1 user-space bootstrap reached successfully");

    /*
     * Aurora is now interrupt-driven. The bootstrap thread temporarily pumps
     * the native login input controller until the compositor/session service
     * moves this policy into user space.
     */
    for (;;) {
        login_input_pump();
        arch_idle();
    }
}