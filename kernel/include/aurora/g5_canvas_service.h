#ifndef AURORA_G5_CANVAS_SERVICE_H
#define AURORA_G5_CANVAS_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include <aurora/ipc.h>
struct aurora_process;
struct g5_canvas_service {
    struct aurora_process *process;
    uint64_t thread_id;
    uint64_t session_generation;
    /* Dedicated private Canvas channel, never shared with Shell/G5 dispatch. */
    struct aurora_ipc_channel control_channel;
    struct aurora_ipc_endpoint *kernel_endpoint;
    aurora_cap_handle control_handle;
};
/* Creates an isolated address space with no delegated Shell capabilities.
 * The caller owns teardown and must not mark the session READY on spawn. */
bool g5_canvas_service_prepare(struct g5_canvas_service *service,
                               uint64_t generation);
/* Provision a non-transferable bidirectional endpoint in the Canvas process.
 * The kernel peer is held exclusively by the session host. */
bool g5_canvas_service_provision_control(struct g5_canvas_service *service);
bool g5_canvas_service_start(struct g5_canvas_service *service);
/* Abort only a prepared process that never acquired a scheduler thread. */
bool g5_canvas_service_abort_unstarted(struct g5_canvas_service *service);
bool g5_canvas_service_reap(struct g5_canvas_service *service);
/* Cleanup terminal faults/nonzero exits without reporting a successful probe. */
bool g5_canvas_service_reap_failed(struct g5_canvas_service *service);
#endif
