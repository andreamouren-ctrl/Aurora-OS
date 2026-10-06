#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/clock.h>
#include <aurora/ipc.h>
#include <aurora/ipc_wait_probe.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/user_ipc_wait_probe.h>
#include <aurora/usercopy.h>

static struct aurora_ipc_channel wait_probe_channel;
static struct aurora_cap_table wait_probe_kernel_caps;

static bool bytes_equal(const uint8_t *left, const uint8_t *right, size_t length) {
    for (size_t i = 0u; i < length; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

bool ipc_wait_ring3_self_test(void) {
    ipc_channel_init(&wait_probe_channel);
    cap_table_init(&wait_probe_kernel_caps);

    struct aurora_ipc_endpoint *user_endpoint =
        ipc_channel_endpoint(&wait_probe_channel, 0u);
    struct aurora_ipc_endpoint *kernel_endpoint =
        ipc_channel_endpoint(&wait_probe_channel, 1u);
    if (user_endpoint == NULL || kernel_endpoint == NULL) return false;

    struct aurora_process *process = process_create_image(
        "ring3-ipc-wait-probe",
        user_ipc_wait_probe_image(),
        user_ipc_wait_probe_image_size()
    );
    if (process == NULL) return false;

    aurora_cap_handle handle = cap_grant(
        &process->capabilities,
        user_endpoint,
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE
    );
    if (handle == AURORA_CAP_INVALID) return false;

    uint64_t handle_value = (uint64_t)handle;
    if (!copy_to_user(
            process,
            process->user_stack_top - 8ull,
            &handle_value,
            sizeof(handle_value))) {
        return false;
    }

    aurora_thread_id thread = scheduler_create_user_thread(
        "ring3-ipc-wait-probe-main",
        process
    );
    if (thread == 0u) return false;

    uint64_t deadline = clock_now_ns() + 500000000ull;
    while ((process_bootstrap_signal(process) != AURORA_USER_IPC_WAITING_MAGIC ||
            !scheduler_thread_blocked(thread)) &&
           !scheduler_thread_finished(thread) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(process) != AURORA_USER_IPC_WAITING_MAGIC ||
        !scheduler_thread_blocked(thread) || scheduler_thread_finished(thread)) {
        return false;
    }

    static const uint8_t payload[] = {
        'A', 'U', 'R', 'O', 'R', 'A', '-', 'W', 'A', 'K', 'E'
    };

    if (!ipc_send(
            kernel_endpoint,
            NULL,
            payload,
            (uint32_t)sizeof(payload),
            NULL,
            0u)) {
        return false;
    }

    deadline = clock_now_ns() + 500000000ull;
    while ((process_bootstrap_signal(process) != AURORA_USER_IPC_WAIT_DONE_MAGIC ||
            !scheduler_thread_finished(thread)) &&
           clock_now_ns() < deadline) {
        arch_idle();
    }

    if (process_bootstrap_signal(process) != AURORA_USER_IPC_WAIT_DONE_MAGIC ||
        !scheduler_thread_finished(thread) ||
        process_state(process) != AURORA_PROCESS_EXITED ||
        process->exit_code != 0) {
        return false;
    }

    struct aurora_ipc_received echo;
    if (!ipc_receive(kernel_endpoint, &wait_probe_kernel_caps, &echo) ||
        echo.length != sizeof(payload) || echo.capability_count != 0u ||
        !bytes_equal(echo.data, payload, sizeof(payload))) {
        return false;
    }

    return true;
}
