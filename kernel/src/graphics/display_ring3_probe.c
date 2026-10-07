#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/display.h>
#include <aurora/display_backbuffer.h>
#include <aurora/display_ring3_probe.h>
#include <aurora/display_user_probe.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/syscall_abi.h>
#include <aurora/usercopy.h>

#define DISPLAY_RING3_TEST_TIMEOUT_NS 1000000000ull

static bool copy_probe_frame_to_user(
    struct aurora_process *process,
    uint64_t user_address,
    const struct aurora_display_mode *mode
) {
    struct aurora_display_backbuffer staging;

    if (process == NULL ||
        user_address == 0u ||
        mode == NULL ||
        !display_backbuffer_init(&staging, mode)) {
        return false;
    }

    for (uint64_t row = 0u; row < staging.height; ++row) {
        uint8_t *line =
            staging.pixels + row * staging.pitch;

        for (uint64_t byte = 0u; byte < staging.pitch; ++byte) {
            line[byte] =
                (uint8_t)((row * 17u + byte * 29u) & 0xFFu);
        }
    }

    bool copied =
        copy_to_user(
            process,
            user_address,
            staging.pixels,
            (size_t)staging.byte_length
        );

    bool released =
        display_backbuffer_release(&staging);

    return copied && released;
}

bool display_ring3_self_test(void) {
    const struct aurora_display_output *output =
        display_output_at(0u);
    const struct aurora_display_mode *mode =
        display_mode_at(0u, 0u);

    if (output == NULL ||
        mode == NULL ||
        mode->height == 0u ||
        mode->pitch == 0u ||
        mode->height > UINT64_MAX / mode->pitch) {
        return false;
    }

    uint64_t frame_bytes =
        mode->height * mode->pitch;

    if (frame_bytes == 0u ||
        frame_bytes > AURORA_SYS_USER_MEMORY_MAX_ALLOCATION_BYTES ||
        frame_bytes > (uint64_t)SIZE_MAX) {
        return false;
    }

    struct aurora_process *process =
        process_create_image(
            "ring3-display-service-probe",
            display_user_probe_image(),
            display_user_probe_image_size()
        );

    if (process == NULL) {
        return false;
    }

    aurora_cap_handle display_handle =
        cap_grant(
            &process->capabilities,
            (void *)output,
            AURORA_CAP_DISPLAY,
            AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL
        );

    if (display_handle == AURORA_CAP_INVALID) {
        return false;
    }

    struct aurora_capability_view view;
    if (!cap_lookup(
            &process->capabilities,
            display_handle,
            AURORA_CAP_DISPLAY,
            AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL,
            &view) ||
        cap_lookup(
            &process->capabilities,
            display_handle,
            AURORA_CAP_DISPLAY,
            AURORA_RIGHT_MAP,
            &view) ||
        cap_lookup(
            &process->capabilities,
            display_handle,
            AURORA_CAP_DISPLAY,
            AURORA_RIGHT_DEVICE_IO,
            &view)) {
        return false;
    }

    uint64_t user_frame = 0u;
    if (!process_user_memory_allocate(
            process,
            frame_bytes,
            &user_frame) ||
        user_frame == 0u ||
        !copy_probe_frame_to_user(
            process,
            user_frame,
            mode)) {
        return false;
    }

    uint64_t stack_values[5] = {
        (uint64_t)display_handle,
        user_frame,
        frame_bytes,
        0u,
        0u
    };

    if (!copy_to_user(
            process,
            process->user_stack_top - 40u,
            &stack_values[4],
            sizeof(uint64_t)) ||
        !copy_to_user(
            process,
            process->user_stack_top - 32u,
            &stack_values[3],
            sizeof(uint64_t)) ||
        !copy_to_user(
            process,
            process->user_stack_top - 24u,
            &stack_values[2],
            sizeof(uint64_t)) ||
        !copy_to_user(
            process,
            process->user_stack_top - 16u,
            &stack_values[1],
            sizeof(uint64_t)) ||
        !copy_to_user(
            process,
            process->user_stack_top - 8u,
            &stack_values[0],
            sizeof(uint64_t))) {
        return false;
    }

    struct aurora_display_present_state before;
    if (!display_present_state(0u, &before)) {
        return false;
    }

    aurora_thread_id thread =
        scheduler_create_user_thread(
            "ring3-display-service-main",
            process
        );

    if (thread == 0u) {
        return false;
    }

    uint64_t deadline =
        clock_now_ns() + DISPLAY_RING3_TEST_TIMEOUT_NS;

    while (!scheduler_thread_finished(thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (!scheduler_thread_finished(thread) ||
        process_state(process) != AURORA_PROCESS_EXITED ||
        process->exit_code != 0) {
        return false;
    }

    uint64_t first_serial = 0u;
    uint64_t second_serial = 0u;

    if (!copy_from_user(
            process,
            &first_serial,
            process->user_stack_top - 32u,
            sizeof(first_serial)) ||
        !copy_from_user(
            process,
            &second_serial,
            process->user_stack_top - 40u,
            sizeof(second_serial)) ||
        first_serial == 0u ||
        second_serial <= first_serial) {
        return false;
    }

    struct aurora_display_present_state after;
    if (!display_present_state(0u, &after) ||
        after.last_presented_serial != second_serial ||
        after.last_released_serial != second_serial ||
        after.last_presented_serial == before.last_presented_serial) {
        return false;
    }

    if (!scheduler_reap_thread(thread) ||
        process_live_thread_count(process) != 0u ||
        !process_reap(process, NULL) ||
        !process_release(process)) {
        return false;
    }

    return true;
}
