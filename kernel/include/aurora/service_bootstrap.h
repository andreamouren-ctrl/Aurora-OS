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

#define AURORA_SERVICE_STARTUP_ABI_VERSION 2u
#define AURORA_SERVICE_STARTUP_STACK_OFFSET 64u

struct aurora_service_startup_block {
    uint32_t abi_version;
    uint32_t flags;
    aurora_cap_handle ipc_endpoint;
    aurora_cap_handle protected_state;
    aurora_cap_handle entropy_seed;
};

struct aurora_trusted_service_manifest {
    const char *name;
    const uint8_t *image;
    size_t image_size;
    const char *protected_state_scope;
    uint64_t protected_state_rights;
    bool grant_entropy_seed;
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

    bool started;
};

/*
 * Bootstrap one explicitly trusted Ring 3 service.
 *
 * The service receives a process-local IPC endpoint, one non-transferable
 * Protected System State capability, and optionally one read-only entropy-seed
 * capability. Ordinary user processes are not granted these authorities by
 * this API implicitly.
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
