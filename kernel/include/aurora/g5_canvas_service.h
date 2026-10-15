#ifndef AURORA_G5_CANVAS_SERVICE_H
#define AURORA_G5_CANVAS_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
struct aurora_process;
struct g5_canvas_service {
    struct aurora_process *process;
    uint64_t thread_id;
    uint64_t session_generation;
};
/* Creates an isolated address space with no delegated Shell capabilities.
 * The caller owns teardown and must not mark the session READY on spawn. */
bool g5_canvas_service_prepare(struct g5_canvas_service *service,
                               uint64_t generation);
bool g5_canvas_service_start(struct g5_canvas_service *service);
/* Abort only a prepared process that never acquired a scheduler thread. */
bool g5_canvas_service_abort_unstarted(struct g5_canvas_service *service);
bool g5_canvas_service_reap(struct g5_canvas_service *service);
#endif
