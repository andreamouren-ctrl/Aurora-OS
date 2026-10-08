#include <aurora/g5_ipc_dispatch.h>

enum g5_ipc_status g5_ipc_dispatch(
    struct g5_dispatch_context *d,
    const struct aurora_sys_ipc_received *message
) {
    if (d == NULL || message == NULL || d->authorize == NULL ||
        d->handler == NULL || d->active_session_generation == 0 ||
        d->dispatch_in_progress)
        return G5_IPC_DENIED;

    struct g5_ipc_header header;
    const uint8_t *payload = NULL;
    enum g5_ipc_status result = g5_ipc_decode_received(
        message, &header, &payload
    );
    if (result != G5_IPC_OK) return result;
    result = g5_ipc_validate_schema(&header, message->capability_count);
    if (result != G5_IPC_OK) return result;

    /* This first version admits only request and READY event schemas.
     * A session change requires an explicit dispatcher reset by its owner. */
    if (header.session_generation != d->active_session_generation ||
        header.request_id <= d->last_request_id)
        return G5_IPC_DENIED;
    d->dispatch_in_progress = true;
    if (!d->authorize(d->context, header.operation,
                      header.session_generation)) {
        d->dispatch_in_progress = false;
        return G5_IPC_DENIED;
    }
    if (header.session_generation != d->active_session_generation) {
        d->dispatch_in_progress = false;
        return G5_IPC_DENIED;
    }
    if (d->require_durable_reservation && d->reserve == NULL) {
        d->dispatch_in_progress = false;
        return G5_IPC_DENIED;
    }
    if (d->reserve != NULL &&
        !d->reserve(d->reserve_context,
                    header.session_generation,header.request_id)) {
        d->dispatch_in_progress = false;
        return G5_IPC_DENIED;
    }
    /* Reserve the accepted request before invoking side-effecting code.
     * If the handler fails after a partial side effect, an identical retry
     * must not execute it again in this dispatcher lifetime. This is NOT
     * crash-durable exactly-once: journalled operations still need durable
     * deduplication and recovery through the State Broker. */
    d->last_request_id = header.request_id;
    bool completed = d->handler(d->context, &header, payload);
    d->dispatch_in_progress = false;
    return completed ? G5_IPC_OK : G5_IPC_DENIED;
}

void g5_ipc_dispatch_revoke(struct g5_dispatch_context *d) {
    if (d == NULL) return;
    if (d->active_session_generation > d->last_revoked_generation)
        d->last_revoked_generation = d->active_session_generation;
    d->active_session_generation = 0;
    d->last_request_id = 0;
}

bool g5_ipc_dispatch_bind_session(struct g5_dispatch_context *d,
                                  uint64_t generation) {
    if (d == NULL || d->dispatch_in_progress || generation == 0 ||
        d->active_session_generation != 0 ||
        generation <= d->last_revoked_generation) return false;
    d->last_request_id = 0;
    d->active_session_generation = generation;
    return true;
}
