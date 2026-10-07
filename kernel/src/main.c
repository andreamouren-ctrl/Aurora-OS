#include <stdint.h>

#include <aurora/acpi.h>
#include <aurora/apic.h>
#include <aurora/arch.h>
#include <aurora/boot.h>
#include <aurora/boot_ui.h>
#include <aurora/clock.h>
#include <aurora/capability.h>
#include <aurora/color_management.h>
#include <aurora/cpu_local.h>
#include <aurora/display.h>
#include <aurora/display_ddc.h>
#include <aurora/gpu_display_driver.h>
#include <aurora/display_phy.h>
#include <aurora/display_controller.h>
#include <aurora/display_vrr_backend.h>
#include <aurora/display_link_training.h>
#include <aurora/display_dsc.h>
#include <aurora/display_identification.h>
#include <aurora/displayport_link.h>
#include <aurora/display_backbuffer.h>
#include <aurora/framebuffer.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_surface.h>
#include <aurora/gdt.h>
#include <aurora/heap.h>
#include <aurora/hpet.h>
#include <aurora/hdmi_link.h>
#include <aurora/input.h>
#include <aurora/interrupts.h>
#include <aurora/ioapic.h>
#include <aurora/ipc.h>
#include <aurora/log.h>
#include <aurora/login_input.h>
#include <aurora/memory_object.h>
#include <aurora/madt.h>
#include <aurora/panic.h>
#include <aurora/pci.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/qemu_std_vga.h>
#include <aurora/ps2_keyboard.h>
#include <aurora/scheduler.h>
#include <aurora/smp.h>
#include <aurora/software_compositor.h>
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

    if (!display_identification_selftest()) {
        kernel_panic("EDID/CTA/DisplayID parser self-test failed");
    }

    log_line("[display] EDID base parser passed");
    log_line("[display] CTA-861 HDR/color parser passed");
    log_line("[display] DisplayID block parser passed");

    if (!display_ddc_selftest()) {
        kernel_panic("DDC/E-DDC transport self-test failed");
    }

    if (!display_dp_link_selftest()) {
        kernel_panic("DisplayPort DPCD link self-test failed");
    }

    if (!display_hdmi_link_selftest()) {
        kernel_panic("HDMI sink capability self-test failed");
    }

    if (!display_vrr_selftest()) {
        kernel_panic("VRR policy self-test failed");
    }

    if (!display_dsc_selftest()) {
        kernel_panic("DSC capability/config self-test failed");
    }

    if (!display_vrr_backend_selftest()) {
        kernel_panic("VRR backend programming self-test failed");
    }

    if (!display_link_training_selftest()) {
        kernel_panic("Display link training self-test failed");
    }

    if (!display_controller_selftest()) {
        kernel_panic("Display controller/scanout self-test failed");
    }

    if (!display_phy_selftest()) {
        kernel_panic("Display PHY/link backend self-test failed");
    }

    if (!gpu_display_driver_selftest()) {
        kernel_panic("GPU display driver registry self-test failed");
    }

    if (!qemu_std_vga_selftest()) {
        kernel_panic("QEMU Standard VGA driver self-test failed");
    }

    gpu_display_driver_registry_init();

    if (!gpu_display_driver_register(
            qemu_std_vga_driver())) {
        kernel_panic("QEMU Standard VGA driver registration failed");
    }

    struct aurora_gpu_display_device qemu_gpu = {0};

    if (qemu_std_vga_probe_pci(&qemu_gpu)) {
        uint64_t qemu_lfb_physical = 0u;

        if (!qemu_std_vga_bound_info(
                &qemu_gpu,
                &qemu_lfb_physical) ||
            qemu_lfb_physical == 0u ||
            !display_attach_native_gpu(&qemu_gpu) ||
            !display_native_gpu_ready() ||
            display_native_gpu_device() == NULL) {
            kernel_panic("QEMU Standard VGA native candidate attach failed");
        }

        log_write("[display] QEMU std VGA BAR0 LFB: ");
        log_hex64(qemu_lfb_physical);
        log_line("");
        log_line("[display] QEMU std VGA native backend candidate passed");
    } else {
        log_line("[display] QEMU std VGA not present; boot framebuffer fallback retained");
    }

    log_line("[display] DDC/E-DDC EDID transport passed");
    log_line("[display] DisplayPort/eDP DPCD capability path passed");
    log_line("[display] HDMI EDID/CTA capability path passed");
    log_line("[display] VRR range and presentation policy passed");
    log_line("[display] DSC capability/config validation passed");
    log_line("[display] VRR hardware programming contract passed");
    log_line("[display] DP/HDMI bounded link training passed");
    log_line("[display] controller modeset/scanout contract passed");
    log_line("[display] PHY/link backend contract passed");
    log_line("[display] GPU display driver registry passed");
    log_line("[display] native GPU candidate/fallback policy passed");
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

    aurora_cap_handle recycle_surface =
        graphics_surface_grant(
            &recycle_target_caps,
            graphics_probe_surface,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
        );

    struct aurora_graphics_rect recycle_damage = {
        .x = 0u,
        .y = 0u,
        .width = 32u,
        .height = 32u
    };

    uint64_t recycle_commit = 0u;

    if (recycle_surface == AURORA_CAP_INVALID ||
        !graphics_surface_attach(
            &recycle_target_caps,
            recycle_surface,
            recycle_target) ||
        !graphics_surface_damage(
            &recycle_target_caps,
            recycle_surface,
            &recycle_damage) ||
        !graphics_surface_commit(
            &recycle_target_caps,
            recycle_surface,
            &recycle_commit) ||
        recycle_commit == 0u ||
        !graphics_surface_detach_buffers(
            &recycle_target_caps,
            recycle_surface) ||
        !cap_revoke(
            &recycle_target_caps,
            recycle_surface)) {
        kernel_panic("Graphics existing capability functional survival probe failed");
    }

    log_line("[graphics] existing buffer capability remains usable after owner release");

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

    log_line("[graphics] capability lifetime accounting passed");
    log_line("[graphics] deferred destroy and stale-handle rejection passed");
    log_line("[graphics] capability-safe buffer slot reuse passed");

    if (!graphics_surface_request_frame_callback(
            &graphics_probe_caps,
            graphics_surface_handle,
            UINT64_C(0xF001))) {
        kernel_panic("Graphics frame callback request probe failed");
    }

    if (!graphics_surface_attach(
            &graphics_probe_caps,
            graphics_surface_handle,
            graphics_buffer_handle) ||
        !graphics_surface_damage(
            &graphics_probe_caps,
            graphics_surface_handle,
            &graphics_damage)) {
        kernel_panic("Graphics frame callback attach probe failed");
    }

    uint64_t callback_commit_serial = 0u;

    if (!graphics_surface_commit(
            &graphics_probe_caps,
            graphics_surface_handle,
            &callback_commit_serial) ||
        callback_commit_serial == 0u ||
        !graphics_surface_complete_frame(
            graphics_probe_surface,
            callback_commit_serial,
            UINT64_C(0x9001))) {
        kernel_panic("Graphics frame callback completion probe failed");
    }

    struct aurora_graphics_frame_callback callback_result;

    if (!graphics_surface_take_frame_callback(
            &graphics_probe_caps,
            graphics_surface_handle,
            &callback_result) ||
        callback_result.request_id != UINT64_C(0xF001) ||
        callback_result.commit_serial != callback_commit_serial ||
        callback_result.presentation_serial != UINT64_C(0x9001) ||
        callback_result.state != AURORA_GRAPHICS_FRAME_CALLBACK_READY) {
        kernel_panic("Graphics frame callback delivery probe failed");
    }

    log_line("[graphics] bounded frame callback lifecycle passed");

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

    uint32_t graphics_probe_surface_generation =
        graphics_probe_surface->generation;

    if (!cap_revoke(
            &graphics_probe_caps,
            graphics_readonly_surface_handle) ||
        !cap_revoke(
            &graphics_probe_caps,
            graphics_surface_handle) ||
        !graphics_surface_release_owner(
            graphics_probe_surface,
            graphics_probe_surface_generation) ||
        graphics_probe_surface->state != AURORA_GRAPHICS_SURFACE_FREE) {
        kernel_panic("Graphics primary surface lifetime cleanup failed");
    }

    static struct aurora_cap_table surface_recycle_caps;
    cap_table_init(&surface_recycle_caps);

    struct aurora_graphics_buffer *surface_recycle_buffer =
        graphics_buffer_create(
            24u,
            24u,
            &display_probe_mode->format
        );
    struct aurora_graphics_surface *surface_recycle =
        graphics_surface_create();

    if (surface_recycle_buffer == NULL ||
        surface_recycle == NULL) {
        kernel_panic("Graphics surface recycle allocation failed");
    }

    uint32_t surface_recycle_generation =
        surface_recycle->generation;
    uint32_t surface_recycle_buffer_generation =
        surface_recycle_buffer->generation;

    aurora_cap_handle surface_recycle_buffer_handle =
        graphics_buffer_grant(
            &surface_recycle_caps,
            surface_recycle_buffer,
            AURORA_RIGHT_READ
        );
    aurora_cap_handle surface_recycle_handle =
        graphics_surface_grant(
            &surface_recycle_caps,
            surface_recycle,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
        );

    struct aurora_graphics_rect surface_recycle_damage = {
        .x = 0u,
        .y = 0u,
        .width = 24u,
        .height = 24u
    };

    uint64_t surface_recycle_commit = 0u;

    if (surface_recycle_buffer_handle == AURORA_CAP_INVALID ||
        surface_recycle_handle == AURORA_CAP_INVALID ||
        !graphics_surface_attach(
            &surface_recycle_caps,
            surface_recycle_handle,
            surface_recycle_buffer_handle) ||
        !graphics_surface_damage(
            &surface_recycle_caps,
            surface_recycle_handle,
            &surface_recycle_damage) ||
        !graphics_surface_request_frame_callback(
            &surface_recycle_caps,
            surface_recycle_handle,
            UINT64_C(0xD001)) ||
        !graphics_surface_commit(
            &surface_recycle_caps,
            surface_recycle_handle,
            &surface_recycle_commit) ||
        surface_recycle_commit == 0u ||
        !graphics_surface_request_frame_callback(
            &surface_recycle_caps,
            surface_recycle_handle,
            UINT64_C(0xD002))) {
        kernel_panic("Graphics surface callback cleanup setup failed");
    }

    uint32_t surface_refs_before_destroy =
        surface_recycle_buffer->surface_refs;

    if (surface_refs_before_destroy < 2u ||
        !surface_recycle->pending_frame_callback) {
        kernel_panic("Graphics surface destruction precondition failed");
    }

    if (!graphics_surface_release_owner(
            surface_recycle,
            surface_recycle_generation) ||
        !surface_recycle->destroy_requested ||
        surface_recycle->state == AURORA_GRAPHICS_SURFACE_FREE ||
        graphics_surface_grant(
            &surface_recycle_caps,
            surface_recycle,
            AURORA_RIGHT_READ) != AURORA_CAP_INVALID) {
        kernel_panic("Graphics surface deferred destroy policy failed");
    }

    struct aurora_graphics_surface *surviving_surface = NULL;
    if (!graphics_surface_lookup(
            &surface_recycle_caps,
            surface_recycle_handle,
            AURORA_RIGHT_READ,
            &surviving_surface) ||
        surviving_surface != surface_recycle) {
        kernel_panic("Graphics existing surface capability survival failed");
    }

    if (!cap_revoke(
            &surface_recycle_caps,
            surface_recycle_handle) ||
        surface_recycle->state != AURORA_GRAPHICS_SURFACE_FREE ||
        surface_recycle->generation == surface_recycle_generation ||
        surface_recycle->pending_frame_callback ||
        surface_recycle->pending_frame_request_id != 0u ||
        surface_recycle_buffer->surface_refs != 0u) {
        kernel_panic("Graphics final surface destruction cleanup failed");
    }

    for (uint32_t i = 0u;
         i < AURORA_GRAPHICS_SURFACE_MAX_FRAME_CALLBACKS;
         ++i) {
        if (surface_recycle->frame_callbacks[i].state !=
                AURORA_GRAPHICS_FRAME_CALLBACK_FREE ||
            surface_recycle->frame_callbacks[i].request_id != 0u ||
            surface_recycle->frame_callbacks[i].commit_serial != 0u ||
            surface_recycle->frame_callbacks[i].presentation_serial != 0u) {
            kernel_panic("Graphics destroyed surface retained frame callback state");
        }
    }

    struct aurora_graphics_surface *surface_reused =
        graphics_surface_create();

    if (surface_reused == NULL ||
        surface_reused != surface_recycle ||
        surface_reused->generation == surface_recycle_generation ||
        surface_reused->pending_frame_callback) {
        kernel_panic("Graphics surface slot recycling probe failed");
    }

    if (graphics_surface_release_owner(
            surface_reused,
            surface_recycle_generation) ||
        !graphics_surface_release_owner(
            surface_reused,
            surface_reused->generation) ||
        !cap_revoke(
            &surface_recycle_caps,
            surface_recycle_buffer_handle) ||
        !graphics_buffer_release_owner(
            surface_recycle_buffer,
            surface_recycle_buffer_generation)) {
        kernel_panic("Graphics surface recycle cleanup failed");
    }

    log_line("[graphics] capability-aware surface lifetime passed");
    log_line("[graphics] surface destroy/recycle stale-generation rejection passed");
    log_line("[graphics] destroyed surface callback and buffer cleanup passed");

    struct aurora_display_pixel_format hdr10_format = {
        .encoding = AURORA_PIXEL_ENCODING_UNORM_PACKED,
        .bits_per_pixel = 32u,
        .red_mask_size = 10u,
        .red_mask_shift = 0u,
        .green_mask_size = 10u,
        .green_mask_shift = 10u,
        .blue_mask_size = 10u,
        .blue_mask_shift = 20u,
        .alpha_mask_size = 2u,
        .alpha_mask_shift = 30u
    };

    struct aurora_color_description hdr10_color = {
        .primaries = AURORA_COLOR_PRIMARIES_BT2020,
        .transfer = AURORA_COLOR_TRANSFER_PQ_ST2084,
        .range = AURORA_COLOR_RANGE_FULL,
        .hdr_static = {
            .valid = true,
            .mastering_max_luminance_millinit = 1000000u,
            .mastering_min_luminance_micrinit = 50u,
            .max_cll_nits = 1000u,
            .max_fall_nits = 400u
        }
    };

    struct aurora_display_pixel_format rgb12_format = {
        .encoding = AURORA_PIXEL_ENCODING_UNORM_PACKED,
        .bits_per_pixel = 48u,
        .red_mask_size = 12u,
        .red_mask_shift = 0u,
        .green_mask_size = 12u,
        .green_mask_shift = 12u,
        .blue_mask_size = 12u,
        .blue_mask_shift = 24u,
        .alpha_mask_size = 0u,
        .alpha_mask_shift = 0u
    };

    struct aurora_display_pixel_format fp16_format = {
        .encoding = AURORA_PIXEL_ENCODING_FLOAT16,
        .bits_per_pixel = 64u,
        .red_mask_size = 16u,
        .red_mask_shift = 0u,
        .green_mask_size = 16u,
        .green_mask_shift = 16u,
        .blue_mask_size = 16u,
        .blue_mask_shift = 32u,
        .alpha_mask_size = 16u,
        .alpha_mask_shift = 48u
    };

    struct aurora_color_description linear_p3 = {
        .primaries = AURORA_COLOR_PRIMARIES_DISPLAY_P3_D65,
        .transfer = AURORA_COLOR_TRANSFER_LINEAR,
        .range = AURORA_COLOR_RANGE_FULL,
        .hdr_static = { .valid = false }
    };

    struct aurora_graphics_buffer *hdr10_probe =
        graphics_buffer_create_ex(16u, 16u, &hdr10_format, &hdr10_color);
    struct aurora_graphics_buffer *rgb12_probe =
        graphics_buffer_create_ex(16u, 16u, &rgb12_format, &linear_p3);
    struct aurora_graphics_buffer *fp16_probe =
        graphics_buffer_create_ex(16u, 16u, &fp16_format, &linear_p3);

    if (hdr10_probe == NULL ||
        rgb12_probe == NULL ||
        fp16_probe == NULL ||
        !graphics_buffer_metadata_valid(hdr10_probe) ||
        !graphics_buffer_metadata_valid(rgb12_probe) ||
        !graphics_buffer_metadata_valid(fp16_probe)) {
        kernel_panic("Extended color/HDR graphics format probe failed");
    }

    const struct aurora_display_capabilities *boot_caps =
        display_output_capabilities(boot_output);

    if (boot_caps == NULL ||
        (boot_caps->flags & AURORA_DISPLAY_CAP_SDR) == 0u ||
        (boot_caps->flags & (AURORA_DISPLAY_CAP_HDR_STATIC |
                             AURORA_DISPLAY_CAP_PQ |
                             AURORA_DISPLAY_CAP_HLG |
                             AURORA_DISPLAY_CAP_VRR |
                             AURORA_DISPLAY_CAP_DSC)) != 0u ||
        boot_caps->min_bits_per_component != 8u ||
        boot_caps->max_bits_per_component != 8u) {
        kernel_panic("Boot display capability fail-closed probe failed");
    }

    if (!graphics_buffer_release_owner(hdr10_probe, hdr10_probe->generation) ||
        !graphics_buffer_release_owner(rgb12_probe, rgb12_probe->generation) ||
        !graphics_buffer_release_owner(fp16_probe, fp16_probe->generation)) {
        kernel_panic("Extended color/HDR probe cleanup failed");
    }

    log_line("[graphics] HDR10/RGB12/RGBA16F format foundation passed");
    log_line("[display] explicit SDR/HDR/VRR capability model passed");


    struct aurora_process *graphics_client_a =
        process_create_image(
            "graphics-client-a",
            user_probe_image(),
            user_probe_image_size()
        );

    struct aurora_process *graphics_client_b =
        process_create_image(
            "graphics-client-b",
            user_probe_image(),
            user_probe_image_size()
        );

    struct aurora_graphics_buffer *client_a_buffer =
        graphics_buffer_create(
            48u,
            48u,
            &display_probe_mode->format
        );

    struct aurora_graphics_buffer *client_b_buffer =
        graphics_buffer_create(
            48u,
            48u,
            &display_probe_mode->format
        );

    struct aurora_graphics_surface *client_a_surface =
        graphics_surface_create();

    struct aurora_graphics_surface *client_b_surface =
        graphics_surface_create();

    if (graphics_client_a == NULL ||
        graphics_client_b == NULL ||
        client_a_buffer == NULL ||
        client_b_buffer == NULL ||
        client_a_surface == NULL ||
        client_b_surface == NULL) {
        kernel_panic("Graphics two-client acceptance allocation failed");
    }

    aurora_cap_handle a_buffer_handle =
        graphics_buffer_grant(
            &graphics_client_a->capabilities,
            client_a_buffer,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP
        );

    aurora_cap_handle a_surface_handle =
        graphics_surface_grant(
            &graphics_client_a->capabilities,
            client_a_surface,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE
        );

    /*
     * Reverse grant order in client B so handle values cannot accidentally
     * identify the same capability type across tables.
     */
    aurora_cap_handle b_surface_handle =
        graphics_surface_grant(
            &graphics_client_b->capabilities,
            client_b_surface,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE
        );

    aurora_cap_handle b_buffer_handle =
        graphics_buffer_grant(
            &graphics_client_b->capabilities,
            client_b_buffer,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP
        );

    if (a_buffer_handle == AURORA_CAP_INVALID ||
        a_surface_handle == AURORA_CAP_INVALID ||
        b_buffer_handle == AURORA_CAP_INVALID ||
        b_surface_handle == AURORA_CAP_INVALID) {
        kernel_panic("Graphics two-client capability setup failed");
    }

    uint64_t a_map = 0u;
    uint64_t b_map = 0u;

    if (!graphics_buffer_map_process(
            graphics_client_a,
            a_buffer_handle,
            true,
            &a_map) ||
        !graphics_buffer_map_process(
            graphics_client_b,
            b_buffer_handle,
            true,
            &b_map) ||
        a_map == 0u ||
        b_map == 0u) {
        kernel_panic("Graphics two-client independent mapping failed");
    }

    uint64_t forbidden_map = 0u;

    if (graphics_buffer_map_process(
            graphics_client_b,
            a_buffer_handle,
            true,
            &forbidden_map) ||
        graphics_surface_attach(
            &graphics_client_b->capabilities,
            b_surface_handle,
            a_buffer_handle) ||
        graphics_surface_attach(
            &graphics_client_a->capabilities,
            a_surface_handle,
            b_buffer_handle)) {
        kernel_panic("Graphics cross-client isolation failed");
    }

    struct aurora_graphics_rect client_damage = {
        .x = 0u,
        .y = 0u,
        .width = 48u,
        .height = 48u
    };

    uint64_t a_commit = 0u;
    uint64_t b_commit = 0u;

    if (!graphics_surface_attach(
            &graphics_client_a->capabilities,
            a_surface_handle,
            a_buffer_handle) ||
        !graphics_surface_damage(
            &graphics_client_a->capabilities,
            a_surface_handle,
            &client_damage) ||
        !graphics_surface_commit(
            &graphics_client_a->capabilities,
            a_surface_handle,
            &a_commit) ||
        !graphics_surface_attach(
            &graphics_client_b->capabilities,
            b_surface_handle,
            b_buffer_handle) ||
        !graphics_surface_damage(
            &graphics_client_b->capabilities,
            b_surface_handle,
            &client_damage) ||
        !graphics_surface_commit(
            &graphics_client_b->capabilities,
            b_surface_handle,
            &b_commit) ||
        a_commit == 0u ||
        b_commit == 0u ||
        a_commit == b_commit) {
        kernel_panic("Graphics two-client independent commit failed");
    }

    if (!graphics_buffer_unmap_process(
            graphics_client_a,
            a_map) ||
        !graphics_buffer_unmap_process(
            graphics_client_b,
            b_map) ||
        !graphics_surface_detach_buffers(
            &graphics_client_a->capabilities,
            a_surface_handle) ||
        !graphics_surface_detach_buffers(
            &graphics_client_b->capabilities,
            b_surface_handle)) {
        kernel_panic("Graphics two-client cleanup failed");
    }

    uint32_t client_a_generation =
        client_a_buffer->generation;
    uint32_t client_b_generation =
        client_b_buffer->generation;
    uint32_t client_a_surface_generation =
        client_a_surface->generation;
    uint32_t client_b_surface_generation =
        client_b_surface->generation;

    if (!graphics_buffer_release_owner(
            client_a_buffer,
            client_a_generation) ||
        !graphics_buffer_release_owner(
            client_b_buffer,
            client_b_generation) ||
        !graphics_surface_release_owner(
            client_a_surface,
            client_a_surface_generation) ||
        !graphics_surface_release_owner(
            client_b_surface,
            client_b_surface_generation)) {
        kernel_panic("Graphics two-client owner release failed");
    }

    process_mark_exited(graphics_client_a, 0);
    process_mark_exited(graphics_client_b, 0);

    if (!process_reap(graphics_client_a, NULL) ||
        !process_release(graphics_client_a) ||
        !process_reap(graphics_client_b, NULL) ||
        !process_release(graphics_client_b)) {
        kernel_panic("Graphics two-client process cleanup failed");
    }

    log_line("[graphics] two isolated Ring 3 client capability gate passed");

    if (!color_management_selftest()) {
        kernel_panic("G3 mastering color-management self-test failed");
    }

    log_line("[color-management] ST2084/ICC/calibration/perceptual-tone-map self-test passed");

    if (!software_compositor_selftest()) {
        kernel_panic("G3 software compositor deterministic self-test failed");
    }

    log_line("[compositor] scene/z-order/clipping/alpha/damage self-test passed");

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