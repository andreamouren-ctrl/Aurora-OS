#ifndef AURORA_SERVICE_BOOTSTRAP_H
#define AURORA_SERVICE_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/ipc.h>
#include <aurora/process.h>
#include <aurora/protected_state.h>
#include <aurora/scheduler.h>
#include <aurora/service_abi.h>

struct aurora_service_bootstrap_capability {
    void *object;
    enum aurora_cap_type type;
    uint64_t rights;
};

struct aurora_trusted_service_manifest {
    const char *name;
    const uint8_t *image;
    size_t image_size;
    const char *protected_state_scope;
    uint64_t protected_state_rights;
    bool grant_entropy_seed;
    const struct aurora_service_bootstrap_capability *extra_capabilities;
    uint32_t extra_capability_count;
};

struct aurora_trusted_service {
    struct aurora_process *process;
    aurora_thread_id thread;

    struct aurora_ipc_channel ipc_channel;
    struct aurora_cap_table supervisor_caps;
    struct aurora_protected_state_namespace protected_state;

    struct aurora_ipc_endpoint *service_endpoint;
    struct aurora_ipc_endpoint *supervisor_endpoint;

    aurora_cap_handle service_endpoint_handle;
    aurora_cap_handle protected_state_handle;
    aurora_cap_handle entropy_seed_handle;
    aurora_cap_handle extra_capability_handles[
        AURORA_SERVICE_STARTUP_MAX_EXTRA_CAPABILITIES
    ];
    uint32_t extra_capability_count;

    bool started;
};

/*
 * Bootstrap one explicitly trusted Ring 3 service.
 *
 * The service receives a process-local IPC endpoint, one non-transferable
 * Protected System State capability, optionally one read-only entropy-seed
 * capability, and at most AURORA_SERVICE_STARTUP_MAX_EXTRA_CAPABILITIES
 * explicitly declared dependency capabilities. Ordinary user processes are
 * never granted these authorities implicitly.
 */
bool service_bootstrap_start_trusted(
    const struct aurora_trusted_service_manifest *manifest,
    struct aurora_trusted_service *service
);

/* Send one data-only IPC message from the bootstrap/supervisor side. */
bool service_bootstrap_send(
    struct aurora_trusted_service *service,
    const void *data,
    uint32_t length
);

bool service_bootstrap_receive(
    struct aurora_trusted_service *service,
    struct aurora_ipc_received *out
);

/* Runtime proof for the first trusted long-lived Identity service contract. */
bool service_bootstrap_self_test(void);

#endif
