#include <aurora/g5_ipc_dispatch.h>

enum g5_ipc_status g5_ipc_dispatch(
    struct g5_dispatch_context *d,
    const struct aurora_sys_ipc_received *message
) {
    if (d == NULL || message == NULL || d->authorize == NULL ||
        d->handler == NULL || d->active_session_generation == 0)
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
    if (!d->authorize(d->context, header.operation,
                      header.session_generation))
        return G5_IPC_DENIED;
    /* Reserve the accepted request before invoking side-effecting code.
     * If the handler fails after a partial side effect, an identical retry
     * must not execute it again in this dispatcher lifetime. This is NOT
     * crash-durable exactly-once: journalled operations still need durable
     * deduplication and recovery through the State Broker. */
    d->last_request_id = header.request_id;
    if (!d->handler(d->context, &header, payload))
        return G5_IPC_DENIED;
    return G5_IPC_OK;
}
