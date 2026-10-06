#ifndef AURORA_SERVICE_SUPERVISOR_H
#define AURORA_SERVICE_SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/process.h>
#include <aurora/service_bootstrap.h>

enum aurora_service_restart_policy {
    AURORA_SERVICE_RESTART_NEVER = 0,
    AURORA_SERVICE_RESTART_ON_FAILURE,
    AURORA_SERVICE_RESTART_ALWAYS
};

enum aurora_service_supervisor_state {
    AURORA_SERVICE_SUPERVISOR_UNINITIALIZED = 0,
    AURORA_SERVICE_SUPERVISOR_STOPPED,
    AURORA_SERVICE_SUPERVISOR_RUNNING,
    AURORA_SERVICE_SUPERVISOR_EXITED,
    AURORA_SERVICE_SUPERVISOR_FAULTED,
    AURORA_SERVICE_SUPERVISOR_FAILED
};

struct aurora_service_supervisor {
    struct aurora_trusted_service_manifest manifest;
    struct aurora_trusted_service service;

    enum aurora_service_restart_policy restart_policy;
    enum aurora_service_supervisor_state state;

    uint32_t generation;
    uint32_t restart_count;
    uint32_t restart_limit;

    struct aurora_process_result last_result;
    bool has_last_result;
};

bool service_supervisor_init(
    struct aurora_service_supervisor *supervisor,
    const struct aurora_trusted_service_manifest *manifest,
    enum aurora_service_restart_policy restart_policy,
    uint32_t restart_limit
);

bool service_supervisor_start(
    struct aurora_service_supervisor *supervisor
);

/*
 * Observe one supervisor iteration. A running service is left untouched until
 * its primary Ring 3 thread terminates. Terminal instances are reaped in the
 * scheduler -> process -> VMM ownership order. When policy permits, a fresh
 * instance is started with a new process, IPC channel and capability table.
 */
bool service_supervisor_step(
    struct aurora_service_supervisor *supervisor
);

bool service_supervisor_send(
    struct aurora_service_supervisor *supervisor,
    const void *data,
    uint32_t length
);

bool service_supervisor_receive(
    struct aurora_service_supervisor *supervisor,
    struct aurora_ipc_received *out
);

/* Runtime proof for long-lived Identity IPC plus bounded service restart. */
bool service_supervisor_self_test(void);

#endif
