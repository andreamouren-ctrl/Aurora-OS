#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/capability.h>
#include <aurora/clock.h>
#include <aurora/display.h>
#include <aurora/graphics_buffer.h>
#include <aurora/graphics_ring3_probe.h>
#include <aurora/graphics_ring3_user_probe.h>
#include <aurora/graphics_surface.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/syscall_abi.h>
#include <aurora/usercopy.h>

#define GRAPHICS_RING3_TIMEOUT_NS 1500000000ull

static bool preload_probe(
    struct aurora_process *process,
    aurora_cap_handle own_buffer,
    aurora_cap_handle own_surface,
    aurora_cap_handle foreign_buffer,
    aurora_cap_handle foreign_surface,
    uint64_t pattern,
    uint64_t request_id
) {
    if (process == NULL) return false;

    const uint64_t values[] = {
        (uint64_t)own_buffer,
        (uint64_t)own_surface,
        (uint64_t)foreign_buffer,
        (uint64_t)foreign_surface,
        0u,
        pattern,
        request_id
    };

    for (uint32_t i = 0u; i < 7u; ++i) {
        if (!copy_to_user(
                process,
                process->user_stack_top - (uint64_t)(i + 1u) * 8u,
                &values[i],
                sizeof(values[i]))) {
            return false;
        }
    }

    struct aurora_sys_graphics_frame_callback empty = {0};
    return copy_to_user(
        process,
        process->user_stack_top - 96u,
        &empty,
        sizeof(empty)
    );
}

static bool read_u64(
    struct aurora_process *process,
    uint64_t offset,
    uint64_t *out
) {
    return process != NULL &&
        out != NULL &&
        copy_from_user(
            process,
            out,
            process->user_stack_top - offset,
            sizeof(*out)
        );
}

static bool verify_pattern(
    struct aurora_graphics_buffer *buffer,
    uint64_t expected
) {
    if (buffer == NULL || buffer->memory == NULL) return false;

    uint64_t physical = 0u;
    if (!memory_object_page_at(buffer->memory, 0u, &physical)) {
        return false;
    }

    return *(volatile uint64_t *)pmm_phys_to_virt(physical) == expected;
}

bool graphics_ring3_two_client_self_test(void) {
    const struct aurora_display_mode *mode =
        display_mode_at(0u, 0u);

    if (mode == NULL) return false;

    struct aurora_process *client_a =
        process_create_image(
            "g2-ring3-client-a",
            graphics_ring3_user_probe_image(),
            graphics_ring3_user_probe_image_size()
        );
    struct aurora_process *client_b =
        process_create_image(
            "g2-ring3-client-b",
            graphics_ring3_user_probe_image(),
            graphics_ring3_user_probe_image_size()
        );

    struct aurora_graphics_buffer *buffer_a =
        graphics_buffer_create(48u, 48u, &mode->format);
    struct aurora_graphics_buffer *buffer_b =
        graphics_buffer_create(48u, 48u, &mode->format);
    struct aurora_graphics_surface *surface_a =
        graphics_surface_create();
    struct aurora_graphics_surface *surface_b =
        graphics_surface_create();

    if (client_a == NULL || client_b == NULL ||
        buffer_a == NULL || buffer_b == NULL ||
        surface_a == NULL || surface_b == NULL) {
        return false;
    }

    aurora_cap_handle a_buffer =
        graphics_buffer_grant(
            &client_a->capabilities,
            buffer_a,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP
        );
    aurora_cap_handle a_surface =
        graphics_surface_grant(
            &client_a->capabilities,
            surface_a,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE
        );

    /* Reverse order in B so foreign numeric handles resolve to wrong types. */
    aurora_cap_handle b_surface =
        graphics_surface_grant(
            &client_b->capabilities,
            surface_b,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE
        );
    aurora_cap_handle b_buffer =
        graphics_buffer_grant(
            &client_b->capabilities,
            buffer_b,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_MAP
        );

    if (a_buffer == AURORA_CAP_INVALID ||
        a_surface == AURORA_CAP_INVALID ||
        b_buffer == AURORA_CAP_INVALID ||
        b_surface == AURORA_CAP_INVALID) {
        return false;
    }

    const uint64_t pattern_a = UINT64_C(0xA1A2A3A4A5A6A7A8);
    const uint64_t pattern_b = UINT64_C(0xB1B2B3B4B5B6B7B8);
    const uint64_t request_a = UINT64_C(0xA001);
    const uint64_t request_b = UINT64_C(0xB001);

    if (!preload_probe(
            client_a,
            a_buffer,
            a_surface,
            b_buffer,
            b_surface,
            pattern_a,
            request_a) ||
        !preload_probe(
            client_b,
            b_buffer,
            b_surface,
            a_buffer,
            a_surface,
            pattern_b,
            request_b)) {
        return false;
    }

    aurora_thread_id thread_a =
        scheduler_create_user_thread(
            "g2-ring3-client-a-main",
            client_a
        );
    aurora_thread_id thread_b =
        scheduler_create_user_thread(
            "g2-ring3-client-b-main",
            client_b
        );

    if (thread_a == 0u || thread_b == 0u) {
        return false;
    }

    bool completed_a = false;
    bool completed_b = false;
    uint64_t commit_a = 0u;
    uint64_t commit_b = 0u;
    uint64_t deadline =
        clock_now_ns() + GRAPHICS_RING3_TIMEOUT_NS;

    while (clock_now_ns() < deadline) {
        if (!completed_a &&
            read_u64(client_a, 40u, &commit_a) &&
            commit_a != 0u) {
            completed_a =
                graphics_surface_complete_frame(
                    surface_a,
                    commit_a,
                    UINT64_C(0xCA001)
                );
        }

        if (!completed_b &&
            read_u64(client_b, 40u, &commit_b) &&
            commit_b != 0u) {
            completed_b =
                graphics_surface_complete_frame(
                    surface_b,
                    commit_b,
                    UINT64_C(0xCB001)
                );
        }

        if (completed_a && completed_b &&
            scheduler_thread_finished(thread_a) &&
            scheduler_thread_finished(thread_b)) {
            break;
        }

        arch_idle();
    }

    if (!completed_a || !completed_b ||
        !scheduler_thread_finished(thread_a) ||
        !scheduler_thread_finished(thread_b) ||
        process_state(client_a) != AURORA_PROCESS_EXITED ||
        process_state(client_b) != AURORA_PROCESS_EXITED ||
        client_a->exit_code != 0 ||
        client_b->exit_code != 0 ||
        commit_a == 0u ||
        commit_b == 0u ||
        commit_a == commit_b ||
        !verify_pattern(buffer_a, pattern_a) ||
        !verify_pattern(buffer_b, pattern_b)) {
        return false;
    }

    struct aurora_sys_graphics_frame_callback callback_a = {0};
    struct aurora_sys_graphics_frame_callback callback_b = {0};

    if (!copy_from_user(
            client_a,
            &callback_a,
            client_a->user_stack_top - 96u,
            sizeof(callback_a)) ||
        !copy_from_user(
            client_b,
            &callback_b,
            client_b->user_stack_top - 96u,
            sizeof(callback_b)) ||
        callback_a.request_id != request_a ||
        callback_b.request_id != request_b ||
        callback_a.commit_serial != commit_a ||
        callback_b.commit_serial != commit_b ||
        callback_a.presentation_serial != UINT64_C(0xCA001) ||
        callback_b.presentation_serial != UINT64_C(0xCB001)) {
        return false;
    }

    if (!graphics_surface_detach_buffers(
            &client_a->capabilities,
            a_surface) ||
        !graphics_surface_detach_buffers(
            &client_b->capabilities,
            b_surface)) {
        return false;
    }

    uint32_t generation_a = buffer_a->generation;
    uint32_t generation_b = buffer_b->generation;

    if (!graphics_buffer_release_owner(
            buffer_a,
            generation_a) ||
        !graphics_buffer_release_owner(
            buffer_b,
            generation_b)) {
        return false;
    }

    if (!scheduler_reap_thread(thread_a) ||
        !scheduler_reap_thread(thread_b) ||
        process_live_thread_count(client_a) != 0u ||
        process_live_thread_count(client_b) != 0u ||
        !process_reap(client_a, NULL) ||
        !process_release(client_a) ||
        !process_reap(client_b, NULL) ||
        !process_release(client_b)) {
        return false;
    }

    return true;
}
