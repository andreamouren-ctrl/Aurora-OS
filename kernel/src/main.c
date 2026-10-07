#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/boot_ui.h>
#include <aurora/clock.h>
#include <aurora/capability.h>
#include <aurora/cpu_local.h>
#include <aurora/display.h>
#include <aurora/display_backbuffer.h>
#include <aurora/framebuffer.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_surface.h>
#include <aurora/gdt.h>
#include <aurora/heap.h>
#include <aurora/hpet.h>
#include <aurora/input.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/ipc.h>
#include <aurora/log.h>
#include <aurora/login_input.h>
#include <aurora/memory_object.h>
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

static void boot_perf_log(const char *stage) {
    log_write("[boot-perf] ");
    log_write(stage);
    log_write(" at ");
    log_u64(clock_now_ns() / UINT64_C(1000000));
    log_line(" ms");
}

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

    if (!display_init_bootstrap(&framebuffer)) {
        kernel_panic("Display bootstrap initialization failed");
    }

#if AURORA_BOOT_VALIDATION
    if (display_output_count() != 1u) {
        kernel_panic("Display output discovery count mismatch");
    }

    const struct aurora_display_output *boot_output =
        display_output_at(0u);
    const struct aurora_display_mode *boot_mode =
        display_mode_at(0u, 0u);

    if (boot_output == NULL ||
        boot_mode == NULL ||
        boot_output->id != UINT64_C(1) ||
        boot_output->backend != AURORA_DISPLAY_BACKEND_BOOT_FRAMEBUFFER ||
        !boot_output->primary ||
        boot_output->mode_count != 1u ||
        boot_mode->width != framebuffer.width ||
        boot_mode->height != framebuffer.height ||
        boot_mode->pitch != framebuffer.pitch ||
        boot_mode->format.bits_per_pixel != framebuffer.bpp) {
        kernel_panic("Display mode/geometry discovery mismatch");
    }

    log_write("[display] outputs discovered: ");
    log_u64(display_output_count());
    log_line("");
    log_write("[display] bootstrap mode: ");
    log_u64(boot_mode->width);
    log_write("x");
    log_u64(boot_mode->height);
    log_write(" pitch=");
    log_u64(boot_mode->pitch);
    log_line("");
    log_line("[display] boot framebuffer geometry discovery passed");
#endif

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
    boot_perf_log("heap ready");

    if (!memory_object_system_init()) {
        kernel_panic("Shared memory object system initialization failed");
    }

    if (!graphics_buffer_system_init() ||
        !graphics_surface_system_init()) {
        kernel_panic("Graphics surface/buffer core initialization failed");
    }

#if AURORA_BOOT_VALIDATION
    const struct aurora_display_mode *display_probe_mode =
        display_mode_at(0u, 0u);
    struct aurora_display_backbuffer display_probe_buffer;

    if (display_probe_mode == NULL ||
        !display_backbuffer_init(
            &display_probe_buffer,
            display_probe_mode)) {
        kernel_panic("Display backbuffer allocation probe failed");
    }

    uint64_t display_bytes_per_pixel =
        (uint64_t)display_probe_mode->format.bits_per_pixel / 8u;

    if (display_bytes_per_pixel == 0u ||
        display_probe_mode->width > UINT64_MAX / display_bytes_per_pixel) {
        kernel_panic("Display probe row geometry overflow");
    }

    uint64_t display_visible_row_bytes =
        display_probe_mode->width * display_bytes_per_pixel;

    if (display_visible_row_bytes > display_probe_buffer.pitch) {
        kernel_panic("Display probe row exceeds backbuffer pitch");
    }

    volatile const uint8_t *display_probe_source =
        (volatile const uint8_t *)framebuffer.address;

    for (uint64_t row = 0u;
         row < display_probe_buffer.height;
         ++row) {
        uint8_t *destination_row =
            display_probe_buffer.pixels +
            row * display_probe_buffer.pitch;
        volatile const uint8_t *source_row =
            display_probe_source +
            row * framebuffer.pitch;

        for (uint64_t byte = 0u;
             byte < display_visible_row_bytes;
             ++byte) {
            destination_row[byte] = source_row[byte];
        }
    }

    uint64_t display_present_serial = 0u;

    if (!display_present(
            0u,
            &display_probe_buffer,
            &display_present_serial)) {
        kernel_panic("Display safe present probe failed");
    }

    struct aurora_display_present_state display_state;

    if (display_present_serial == 0u ||
        !display_present_state(0u, &display_state) ||
        display_state.last_presented_serial != display_present_serial ||
        display_state.last_released_serial != display_present_serial ||
        display_probe_buffer.in_flight ||
        display_probe_buffer.generation != 2u) {
        kernel_panic("Display presentation/release signaling probe failed");
    }

    if (!display_backbuffer_release(&display_probe_buffer)) {
        kernel_panic("Display backbuffer release probe failed");
    }

    log_line("[display] compositor backbuffer allocation passed");
    log_line("[display] bounded framebuffer present passed");
    log_line("[display] presentation/release signaling passed");

    static struct aurora_cap_table graphics_probe_caps;
    cap_table_init(&graphics_probe_caps);

    struct aurora_graphics_buffer *graphics_probe_buffer =
        graphics_buffer_create(
            64u,
            64u,
            &display_probe_mode->format
        );

    struct aurora_graphics_surface *graphics_probe_surface =
        graphics_surface_create();

    if (graphics_probe_buffer == NULL ||
        graphics_probe_surface == NULL) {
        kernel_panic("Graphics G2 object allocation probe failed");
    }

    aurora_cap_handle graphics_buffer_handle =
        graphics_buffer_grant(
            &graphics_probe_caps,
            graphics_probe_buffer,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP |
            AURORA_RIGHT_TRANSFER
        );

    aurora_cap_handle graphics_surface_handle =
        graphics_surface_grant(
            &graphics_probe_caps,
            graphics_probe_surface,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_CONTROL |
            AURORA_RIGHT_TRANSFER
        );

    aurora_cap_handle graphics_readonly_surface_handle =
        graphics_surface_grant(
            &graphics_probe_caps,
            graphics_probe_surface,
            AURORA_RIGHT_READ
        );

    if (graphics_buffer_handle == AURORA_CAP_INVALID ||
        graphics_surface_handle == AURORA_CAP_INVALID ||
        graphics_readonly_surface_handle == AURORA_CAP_INVALID) {
        kernel_panic("Graphics G2 capability grant probe failed");
    }

    struct aurora_graphics_rect graphics_damage = {
        .x = 4u,
        .y = 5u,
        .width = 32u,
        .height = 24u
    };

    if (graphics_surface_attach(
            &graphics_probe_caps,
            graphics_readonly_surface_handle,
            graphics_buffer_handle)) {
        kernel_panic("Graphics surface rights isolation probe failed");
    }

    if (!graphics_surface_attach(
            &graphics_probe_caps,
            graphics_surface_handle,
            graphics_buffer_handle) ||
        !graphics_surface_damage(
            &graphics_probe_caps,
            graphics_surface_handle,
            &graphics_damage)) {
        kernel_panic("Graphics attach/damage probe failed");
    }

    uint64_t graphics_commit_serial = 0u;

    if (!graphics_surface_commit(
            &graphics_probe_caps,
            graphics_surface_handle,
            &graphics_commit_serial) ||
        graphics_commit_serial == 0u ||
        graphics_probe_surface->committed.buffer != graphics_probe_buffer ||
        graphics_probe_surface->committed.damage_count != 1u ||
        graphics_probe_surface->committed.commit_serial != graphics_commit_serial ||
        graphics_probe_surface->state != AURORA_GRAPHICS_SURFACE_MAPPED ||
        graphics_probe_buffer->state != AURORA_GRAPHICS_BUFFER_COMMITTED) {
        kernel_panic("Graphics atomic surface commit probe failed");
    }

    struct aurora_graphics_rect invalid_damage = {
        .x = 63u,
        .y = 63u,
        .width = 2u,
        .height = 2u
    };

    if (!graphics_surface_attach(
            &graphics_probe_caps,
            graphics_surface_handle,
            graphics_buffer_handle) ||
        graphics_surface_damage(
            &graphics_probe_caps,
            graphics_surface_handle,
            &invalid_damage)) {
        kernel_panic("Graphics damage bounds rejection probe failed");
    }

    log_line("[graphics] bounded graphics buffer capability passed");
    log_line("[graphics] capability-backed surface rights passed");
    log_line("[graphics] attach/damage/atomic commit passed");

    struct aurora_process *graphics_map_probe =
        process_create_image(
            "graphics-map-probe",
            user_probe_image(),
            user_probe_image_size()
        );

    if (graphics_map_probe == NULL) {
        kernel_panic("Graphics shared mapping process probe failed");
    }

    aurora_cap_handle process_graphics_buffer_handle =
        graphics_buffer_grant(
            &graphics_map_probe->capabilities,
            graphics_probe_buffer,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP
        );

    uint64_t graphics_map_address = 0u;

    if (process_graphics_buffer_handle == AURORA_CAP_INVALID ||
        !graphics_buffer_map_process(
            graphics_map_probe,
            process_graphics_buffer_handle,
            true,
            &graphics_map_address) ||
        graphics_map_address == 0u ||
        graphics_probe_buffer->memory == NULL ||
        graphics_probe_buffer->memory->mapping_refs != 1u) {
        kernel_panic("Graphics memory-object mapping probe failed");
    }

    uint64_t graphics_mapped_physical = 0u;
    uint64_t graphics_backing_physical = 0u;

    if (!vmm_translate_in(
            &graphics_map_probe->address_space,
            graphics_map_address,
            &graphics_mapped_physical) ||
        !memory_object_page_at(
            graphics_probe_buffer->memory,
            0u,
            &graphics_backing_physical) ||
        (graphics_mapped_physical &
            ~(AURORA_PAGE_SIZE - 1u)) !=
            graphics_backing_physical) {
        kernel_panic("Graphics shared backing identity probe failed");
    }

    *(volatile uint64_t *)pmm_phys_to_virt(
        graphics_backing_physical
    ) = UINT64_C(0x4752415048494353);

    if (*(volatile uint64_t *)pmm_phys_to_virt(
            graphics_mapped_physical &
            ~(AURORA_PAGE_SIZE - 1u)) !=
            UINT64_C(0x4752415048494353)) {
        kernel_panic("Graphics shared backing visibility probe failed");
    }

    if (!graphics_buffer_unmap_process(
            graphics_map_probe,
            graphics_map_address) ||
        graphics_probe_buffer->memory->mapping_refs != 0u) {
        kernel_panic("Graphics shared buffer unmap probe failed");
    }

    process_mark_exited(graphics_map_probe, 0);

    if (!process_reap(graphics_map_probe, NULL) ||
        !process_release(graphics_map_probe)) {
        kernel_panic("Graphics mapping process lifecycle probe failed");
    }

    log_line("[graphics] memory-object backed buffer mapping passed");

    static struct aurora_cap_table recycle_source_caps;
    static struct aurora_cap_table recycle_target_caps;

    cap_table_init(&recycle_source_caps);
    cap_table_init(&recycle_target_caps);

    struct aurora_graphics_buffer *recycle_buffer =
        graphics_buffer_create(
            32u,
            32u,
            &display_probe_mode->format
        );

    if (recycle_buffer == NULL) {
        kernel_panic("Graphics recycle buffer allocation failed");
    }

    uint32_t recycle_generation =
        recycle_buffer->generation;

    aurora_cap_handle recycle_source =
        graphics_buffer_grant(
            &recycle_source_caps,
            recycle_buffer,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_TRANSFER
        );

    aurora_cap_handle recycle_target =
        cap_delegate(
            &recycle_source_caps,
            recycle_source,
            &recycle_target_caps,
            AURORA_RIGHT_READ
        );

    if (recycle_source == AURORA_CAP_INVALID ||
        recycle_target == AURORA_CAP_INVALID ||
        recycle_buffer->capability_refs != 2u ||
        recycle_buffer->owner_refs != 1u) {
        kernel_panic("Graphics capability refcount probe failed");
    }

    if (!graphics_buffer_release_owner(
            recycle_buffer,
            recycle_generation) ||
        !recycle_buffer->destroy_requested ||
        recycle_buffer->owner_refs != 0u ||
        recycle_buffer->state == AURORA_GRAPHICS_BUFFER_FREE) {
        kernel_panic("Graphics deferred owner release probe failed");
    }

    struct aurora_graphics_buffer *existing_holder_view = NULL;

    if (!graphics_buffer_lookup(
            &recycle_target_caps,
            recycle_target,
            AURORA_RIGHT_READ,
            &existing_holder_view) ||
        existing_holder_view != recycle_buffer) {
        kernel_panic("Graphics existing capability survival probe failed");
    }

    if (graphics_buffer_grant(
            &recycle_source_caps,
            recycle_buffer,
            AURORA_RIGHT_READ) != AURORA_CAP_INVALID) {
        kernel_panic("Graphics destroy-pending new grant rejection failed");
    }

    if (!cap_revoke(
            &recycle_source_caps,
            recycle_source) ||
        recycle_buffer->capability_refs != 1u ||
        recycle_buffer->state == AURORA_GRAPHICS_BUFFER_FREE) {
        kernel_panic("Graphics partial capability release probe failed");
    }

    if (!cap_revoke(
            &recycle_target_caps,
            recycle_target) ||
        recycle_buffer->state != AURORA_GRAPHICS_BUFFER_FREE ||
        recycle_buffer->generation == recycle_generation) {
        kernel_panic("Graphics final recycle probe failed");
    }

    struct aurora_capability_view stale_view;

    if (cap_lookup(
            &recycle_source_caps,
            recycle_source,
            AURORA_CAP_GRAPHICS_BUFFER,
            AURORA_RIGHT_READ,
            &stale_view) ||
        cap_lookup(
            &recycle_target_caps,
            recycle_target,
            AURORA_CAP_GRAPHICS_BUFFER,
            AURORA_RIGHT_READ,
            &stale_view)) {
        kernel_panic("Graphics stale capability invalidation probe failed");
    }

    struct aurora_graphics_buffer *reused_buffer =
        graphics_buffer_create(
            32u,
            32u,
            &display_probe_mode->format
        );

    if (reused_buffer == NULL ||
        reused_buffer != recycle_buffer ||
        reused_buffer->generation == recycle_generation) {
        kernel_panic("Graphics capability-safe slot reuse probe failed");
    }

    if (graphics_buffer_release_owner(
            reused_buffer,
            recycle_generation)) {
        kernel_panic("Graphics stale owner generation rejection failed");
    }

    if (!graphics_buffer_release_owner(
            reused_buffer,
            reused_buffer->generation)) {
        kernel_panic("Graphics recycled buffer cleanup failed");
    }

    if (!graphics_surface_detach_buffers(
            &graphics_probe_caps,
            graphics_surface_handle)) {
        kernel_panic("Graphics surface buffer detach probe failed");
    }

    if (!cap_revoke(
            &graphics_probe_caps,
            graphics_buffer_handle) ||
        !graphics_buffer_release_owner(
            graphics_probe_buffer,
            graphics_probe_buffer->generation)) {
        kernel_panic("Graphics primary buffer lifetime cleanup failed");
    }

    log_line("[graphics] capability lifetime accounting passed");
    log_line("[graphics] deferred destroy and stale-handle rejection passed");
    log_line("[graphics] capability-safe buffer slot reuse passed");

    struct aurora_memory_object *shared_probe =
        memory_object_create(2u);

    struct aurora_process *shared_left =
        process_create_image(
            "shared-left",
            user_probe_image(),
            user_probe_image_size()
        );

    struct aurora_process *shared_right =
        process_create_image(
            "shared-right",
            user_probe_image(),
            user_probe_image_size()
        );

    if (shared_probe == NULL ||
        shared_left == NULL ||
        shared_right == NULL) {
        kernel_panic("Shared memory lifecycle allocation probe failed");
    }

    uint64_t shared_left_address = 0u;
    uint64_t shared_right_address = 0u;

    if (!process_shared_memory_map(
            shared_left,
            shared_probe,
            true,
            &shared_left_address) ||
        !process_shared_memory_map(
            shared_right,
            shared_probe,
            true,
            &shared_right_address) ||
        shared_probe->mapping_refs != 2u) {
        kernel_panic("Shared memory dual mapping probe failed");
    }

    for (uint32_t page = 0u; page < 2u; ++page) {
        uint64_t left_physical = 0u;
        uint64_t right_physical = 0u;
        uint64_t object_physical = 0u;

        if (!vmm_translate_in(
                &shared_left->address_space,
                shared_left_address +
                    (uint64_t)page * AURORA_PAGE_SIZE,
                &left_physical) ||
            !vmm_translate_in(
                &shared_right->address_space,
                shared_right_address +
                    (uint64_t)page * AURORA_PAGE_SIZE,
                &right_physical) ||
            !memory_object_page_at(
                shared_probe,
                page,
                &object_physical) ||
            (left_physical & ~(AURORA_PAGE_SIZE - 1u)) != object_physical ||
            (right_physical & ~(AURORA_PAGE_SIZE - 1u)) != object_physical) {
            kernel_panic("Shared memory physical alias probe failed");
        }
    }

    uint64_t shared_physical = 0u;

    if (!memory_object_page_at(
            shared_probe,
            0u,
            &shared_physical)) {
        kernel_panic("Shared memory page lookup probe failed");
    }

    volatile uint64_t *shared_word =
        (volatile uint64_t *)pmm_phys_to_virt(shared_physical);

    *shared_word = UINT64_C(0x4155524F52415348);

    uint64_t left_alias = 0u;
    uint64_t right_alias = 0u;

    if (!vmm_translate_in(
            &shared_left->address_space,
            shared_left_address,
            &left_alias) ||
        !vmm_translate_in(
            &shared_right->address_space,
            shared_right_address,
            &right_alias) ||
        *(volatile uint64_t *)pmm_phys_to_virt(
            left_alias & ~(AURORA_PAGE_SIZE - 1u)) !=
            UINT64_C(0x4155524F52415348) ||
        *(volatile uint64_t *)pmm_phys_to_virt(
            right_alias & ~(AURORA_PAGE_SIZE - 1u)) !=
            UINT64_C(0x4155524F52415348)) {
        kernel_panic("Shared memory visibility probe failed");
    }

    if (!process_shared_memory_unmap(
            shared_left,
            shared_left_address) ||
        shared_probe->mapping_refs != 1u) {
        kernel_panic("Shared memory explicit unmap probe failed");
    }

    process_mark_exited(shared_left, 0);
    process_mark_exited(shared_right, 0);

    if (!process_reap(shared_left, NULL) ||
        !process_release(shared_left) ||
        !process_reap(shared_right, NULL) ||
        shared_probe->mapping_refs != 0u ||
        !process_release(shared_right)) {
        kernel_panic("Shared memory process reap probe failed");
    }

    if (!memory_object_release_owner(shared_probe)) {
        kernel_panic("Shared memory owner release probe failed");
    }

    log_line("[vm] refcounted shared memory object passed");
    log_line("[vm] cross-process shared mapping passed");
    log_line("[vm] shared unmap/reap ownership passed");

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
    boot_perf_log("scheduler initialized");

    for (uint32_t i = 0u; i < smp_cpu_count(); ++i) {
        const struct aurora_cpu_runtime *cpu = smp_cpu_at(i);
        if (cpu == 0 || cpu->bootstrap) continue;

        if (cpu->state != AURORA_CPU_ONLINE ||
            !scheduler_prepare_ap(cpu->logical_id)) {
            kernel_panic("Could not prepare AP scheduler idle ownership");
        }
    }
    boot_perf_log("AP scheduler contexts prepared");

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
    boot_perf_log("scheduler started");

    smp_release_scheduler_aps();

    uint64_t smp_scheduler_deadline =
        clock_now_ns() + 250000000ull;

    /*
     * Do not HLT here. With no non-idle BSP thread yet, scheduler_start()
     * intentionally leaves the BSP timer disarmed. AP ownership completion is
     * a shared-memory handoff and does not itself raise an interrupt, so HLT
     * could sleep forever after the APs have already published completion.
     * This is a short, bounded bootstrap rendezvous; PAUSE is the correct wait.
     */
    while (smp_scheduler_owned_cpu_count() != smp_online_cpu_count() &&
           clock_now_ns() < smp_scheduler_deadline) {
        __asm__ volatile ("pause");
    }

    if (smp_scheduler_owned_cpu_count() != smp_online_cpu_count()) {
        kernel_panic("AP scheduler ownership handoff timed out");
    }

    log_write("[sched] scheduler-owned CPUs: ");
    log_u64(smp_scheduler_owned_cpu_count());
    log_line("");
    boot_perf_log("all CPUs scheduler-owned");

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
    boot_perf_log("boot UI handoff");
    login_input_init();
    boot_perf_log("login input initialized");

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