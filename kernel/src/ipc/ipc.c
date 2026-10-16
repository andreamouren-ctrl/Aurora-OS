#include <stddef.h>
#include <stdint.h>

#include <aurora/ipc.h>
#include <aurora/scheduler.h>

static void copy_bytes(
    void *destination,
    const void *source,
    size_t length
) {
    uint8_t *dst = destination;
    const uint8_t *src = source;

    for (size_t i = 0; i < length; ++i) {
        dst[i] = src[i];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t length
) {
    const uint8_t *a = left;
    const uint8_t *b = right;

    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

void ipc_channel_init(
    struct aurora_ipc_channel *channel
) {
    if (channel == NULL) {
        return;
    }

    spinlock_init(
        &channel->lock
    );

    channel->closed = false;
    channel->active_refs = 0u;

    cap_table_init(
        &channel->escrow
    );

    for (uint32_t side = 0;
         side < 2;
         ++side) {
        struct aurora_ipc_queue *queue =
            &channel->inbound[side];

        queue->head = 0;
        queue->tail = 0;
        queue->count = 0;
        channel->waiter_thread[side] = 0u;

        channel->endpoints[side].channel =
            channel;

        channel->endpoints[side].side =
            (uint8_t)side;
    }
}

static bool endpoint_retain(void *object) {
    struct aurora_ipc_endpoint *endpoint = object;
    if (!endpoint || !endpoint->channel) return false;
    struct aurora_ipc_channel *channel = endpoint->channel;
    spinlock_lock(&channel->lock);
    bool ok = !channel->closed && channel->active_refs < UINT32_MAX;
    if (ok) ++channel->active_refs;
    spinlock_unlock(&channel->lock);
    return ok;
}

static void endpoint_release(void *object) {
    struct aurora_ipc_endpoint *endpoint = object;
    if (!endpoint || !endpoint->channel) return;
    struct aurora_ipc_channel *channel = endpoint->channel;
    spinlock_lock(&channel->lock);
    if (channel->active_refs) --channel->active_refs;
    spinlock_unlock(&channel->lock);
}

bool ipc_endpoint_lifecycle_register(void) {
    return cap_lifecycle_register(AURORA_CAP_IPC_ENDPOINT,
                                  endpoint_retain, endpoint_release);
}

bool ipc_channel_quiescent(struct aurora_ipc_channel *channel) {
    if (!channel) return false;
    spinlock_lock(&channel->lock);
    bool ready = channel->closed && channel->active_refs == 0u;
    spinlock_unlock(&channel->lock);
    return ready;
}

void ipc_channel_close(struct aurora_ipc_channel *channel) {
    if (channel == NULL) return;
    spinlock_lock(&channel->lock);
    if (channel->closed) {
        spinlock_unlock(&channel->lock);
        return;
    }
    channel->closed = true;
    aurora_thread_id waiters[2] = {
        channel->waiter_thread[0], channel->waiter_thread[1]
    };
    for (uint32_t side = 0u; side < 2u; ++side) {
        struct aurora_ipc_queue *queue = &channel->inbound[side];
        channel->waiter_thread[side] = 0u;
        for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
            struct aurora_ipc_message_slot *slot = &queue->slots[i];
            volatile uint8_t *bytes = (volatile uint8_t *)slot->data;
            for (uint32_t j = 0u; j < AURORA_IPC_PAYLOAD_MAX; ++j)
                bytes[j] = 0u;
            for (uint32_t j = 0u; j < AURORA_IPC_CAPS_MAX; ++j) {
                if (slot->escrow_handles[j] != AURORA_CAP_INVALID)
                    (void)cap_revoke(&channel->escrow, slot->escrow_handles[j]);
                slot->escrow_handles[j] = AURORA_CAP_INVALID;
                slot->final_rights[j] = 0u;
            }
            slot->length = 0u;
            slot->capability_count = 0u;
        }
        queue->head = queue->tail = queue->count = 0u;
    }
    /* The closed flag rejects subsequent IPC operations. Release the
     * channel lock before lifecycle callbacks from escrow destruction. */
    spinlock_unlock(&channel->lock);
    cap_table_destroy(&channel->escrow);
    for (uint32_t side = 0u; side < 2u; ++side) {
        if (waiters[side] != 0u)
            (void)scheduler_wake_thread(waiters[side]);
    }
}

struct aurora_ipc_endpoint *ipc_channel_endpoint(
    struct aurora_ipc_channel *channel,
    uint8_t side
) {
    if (channel == NULL ||
        side > 1) {
        return NULL;
    }

    return &channel->endpoints[side];
}

static void rollback_escrow(
    struct aurora_ipc_channel *channel,
    aurora_cap_handle *handles,
    uint32_t count
) {
    for (uint32_t i = 0; i < count; ++i) {
        if (handles[i] !=
            AURORA_CAP_INVALID) {
            (void)cap_revoke(
                &channel->escrow,
                handles[i]
            );
        }
    }
}

bool ipc_send(
    struct aurora_ipc_endpoint *endpoint,
    struct aurora_cap_table *sender_caps,
    const void *data,
    uint32_t length,
    const struct aurora_ipc_transfer *transfers,
    uint32_t transfer_count
) {
    if (endpoint == NULL ||
        endpoint->channel == NULL ||
        endpoint->side > 1 ||
        length > AURORA_IPC_PAYLOAD_MAX ||
        transfer_count > AURORA_IPC_CAPS_MAX ||
        (length != 0 && data == NULL) ||
        (transfer_count != 0 &&
         (transfers == NULL ||
          sender_caps == NULL))) {
        return false;
    }

    struct aurora_ipc_channel *channel =
        endpoint->channel;

    uint8_t destination_side =
        endpoint->side ^ 1u;

    spinlock_lock(
        &channel->lock
    );

    if (channel->closed) {
        spinlock_unlock(&channel->lock);
        return false;
    }

    struct aurora_ipc_queue *queue =
        &channel->inbound[
            destination_side
        ];

    if (queue->count >=
        AURORA_IPC_QUEUE_DEPTH) {
        spinlock_unlock(
            &channel->lock
        );

        return false;
    }

    struct aurora_ipc_message_slot *slot =
        &queue->slots[queue->tail];

    slot->length = length;
    slot->capability_count = 0;
    /* A reused slot must not expose metadata from an older message,
     * including when the first new delegation fails. */
    for (uint32_t i = 0u; i < AURORA_IPC_CAPS_MAX; ++i) {
        slot->escrow_handles[i] = AURORA_CAP_INVALID;
        slot->final_rights[i] = 0u;
    }

    if (length != 0) {
        copy_bytes(
            slot->data,
            data,
            length
        );
    }

    for (uint32_t i = 0;
         i < transfer_count;
         ++i) {
        uint64_t escrow_rights =
            transfers[i].rights |
            AURORA_RIGHT_TRANSFER;

        aurora_cap_handle escrow_handle =
            cap_delegate(
                sender_caps,
                transfers[i].handle,
                &channel->escrow,
                escrow_rights
            );

        if (escrow_handle ==
            AURORA_CAP_INVALID) {
            rollback_escrow(
                channel,
                slot->escrow_handles,
                slot->capability_count
            );

            /* Rollback must not retain a rejected message's payload. */
            volatile uint8_t *rejected = (volatile uint8_t *)slot->data;
            for (uint32_t j = 0u; j < slot->length; ++j) rejected[j] = 0u;
            slot->length = 0;
            for (uint32_t j = 0u; j < slot->capability_count; ++j) {
                slot->escrow_handles[j] = AURORA_CAP_INVALID;
                slot->final_rights[j] = 0u;
            }
            slot->capability_count = 0;

            spinlock_unlock(
                &channel->lock
            );

            return false;
        }

        slot->escrow_handles[i] =
            escrow_handle;

        slot->final_rights[i] =
            transfers[i].rights;

        ++slot->capability_count;
    }

    queue->tail =
        (queue->tail + 1u) %
        AURORA_IPC_QUEUE_DEPTH;

    ++queue->count;

    aurora_thread_id waiter = channel->waiter_thread[destination_side];
    channel->waiter_thread[destination_side] = 0u;

    spinlock_unlock(
        &channel->lock
    );

    if (waiter != 0u) {
        (void)scheduler_wake_thread(waiter);
    }

    return true;
}

bool ipc_receive(
    struct aurora_ipc_endpoint *endpoint,
    struct aurora_cap_table *receiver_caps,
    struct aurora_ipc_received *out
) {
    if (endpoint == NULL ||
        endpoint->channel == NULL ||
        endpoint->side > 1 ||
        receiver_caps == NULL ||
        out == NULL) {
        return false;
    }

    struct aurora_ipc_channel *channel =
        endpoint->channel;

    spinlock_lock(
        &channel->lock
    );

    if (channel->closed) {
        spinlock_unlock(&channel->lock);
        return false;
    }

    struct aurora_ipc_queue *queue =
        &channel->inbound[
            endpoint->side
        ];

    if (queue->count == 0) {
        spinlock_unlock(
            &channel->lock
        );

        return false;
    }

    struct aurora_ipc_message_slot *slot =
        &queue->slots[queue->head];

    aurora_cap_handle received[
        AURORA_IPC_CAPS_MAX
    ] = { 0 };

    uint32_t received_count = 0;

    for (uint32_t i = 0;
         i < slot->capability_count;
         ++i) {
        aurora_cap_handle handle =
            cap_delegate(
                &channel->escrow,
                slot->escrow_handles[i],
                receiver_caps,
                slot->final_rights[i]
            );

        if (handle ==
            AURORA_CAP_INVALID) {
            for (uint32_t rollback = 0;
                 rollback < received_count;
                 ++rollback) {
                (void)cap_revoke(
                    receiver_caps,
                    received[rollback]
                );
            }

            spinlock_unlock(
                &channel->lock
            );

            return false;
        }

        received[received_count++] =
            handle;
    }

    out->length = slot->length;
    out->capability_count =
        received_count;

    if (slot->length != 0) {
        copy_bytes(
            out->data,
            slot->data,
            slot->length
        );
    }

    for (uint32_t i = 0;
         i < received_count;
         ++i) {
        out->capabilities[i] =
            received[i];

        (void)cap_revoke(
            &channel->escrow,
            slot->escrow_handles[i]
        );

        slot->escrow_handles[i] =
            AURORA_CAP_INVALID;
        slot->final_rights[i] = 0u;
    }

    /* Erase delivered payload before making the slot reusable. */
    volatile uint8_t *payload = (volatile uint8_t *)slot->data;
    for (uint32_t i = 0u; i < slot->length; ++i) payload[i] = 0u;
    slot->length = 0;
    slot->capability_count = 0;

    queue->head =
        (queue->head + 1u) %
        AURORA_IPC_QUEUE_DEPTH;

    --queue->count;

    spinlock_unlock(
        &channel->lock
    );

    return true;
}

/* Undo only the handles created by a successful receive. */
void ipc_received_revoke_capabilities(
    struct aurora_cap_table *receiver_caps,
    const struct aurora_ipc_received *received
) {
    if (receiver_caps == NULL || received == NULL) return;
    uint32_t count = received->capability_count;
    if (count > AURORA_IPC_CAPS_MAX) count = AURORA_IPC_CAPS_MAX;
    for (uint32_t i = 0u; i < count; ++i) {
        (void)cap_revoke(receiver_caps, received->capabilities[i]);
    }
}

enum aurora_ipc_wait_result ipc_wait_register(
    struct aurora_ipc_endpoint *endpoint,
    aurora_thread_id thread_id
) {
    if (endpoint == NULL || endpoint->channel == NULL ||
        endpoint->side > 1u || thread_id == 0u) {
        return AURORA_IPC_WAIT_ERROR;
    }

    struct aurora_ipc_channel *channel = endpoint->channel;
    spinlock_lock(&channel->lock);

    if (channel->closed) {
        spinlock_unlock(&channel->lock);
        return AURORA_IPC_WAIT_ERROR;
    }
    struct aurora_ipc_queue *queue = &channel->inbound[endpoint->side];
    if (queue->count != 0u) {
        spinlock_unlock(&channel->lock);
        return AURORA_IPC_WAIT_READY;
    }

    if (channel->waiter_thread[endpoint->side] != 0u) {
        spinlock_unlock(&channel->lock);
        return AURORA_IPC_WAIT_ERROR;
    }

    channel->waiter_thread[endpoint->side] = thread_id;
    spinlock_unlock(&channel->lock);
    return AURORA_IPC_WAIT_REGISTERED;
}

bool ipc_wait_cancel(
    struct aurora_ipc_endpoint *endpoint,
    aurora_thread_id thread_id
) {
    if (endpoint == NULL || endpoint->channel == NULL ||
        endpoint->side > 1u || thread_id == 0u) {
        return false;
    }

    struct aurora_ipc_channel *channel = endpoint->channel;
    spinlock_lock(&channel->lock);
    bool cancelled = channel->waiter_thread[endpoint->side] == thread_id;
    if (cancelled) channel->waiter_thread[endpoint->side] = 0u;
    spinlock_unlock(&channel->lock);
    return cancelled;
}

bool ipc_self_test(void) {
    /*
     * The channel contains two bounded queues plus an escrow capability
     * table. Keep this bootstrap fixture out of the early boot stack.
     */
    static struct aurora_ipc_channel channel;
    static struct aurora_cap_table sender;
    static struct aurora_cap_table receiver;
    static uint64_t dummy_object;

    dummy_object = 0x1C0FFEEu;

    cap_table_init(&sender);
    cap_table_init(&receiver);
    ipc_channel_init(&channel);

    aurora_cap_handle source =
        cap_grant(
            &sender,
            &dummy_object,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_CONTROL |
            AURORA_RIGHT_TRANSFER
        );

    if (source == AURORA_CAP_INVALID) {
        return false;
    }

    static const char payload[] =
        "aurora-ipc";

    struct aurora_ipc_transfer transfer = {
        .handle = source,
        .rights = AURORA_RIGHT_READ
    };

    if (!ipc_send(
            ipc_channel_endpoint(
                &channel,
                0
            ),
            &sender,
            payload,
            (uint32_t)sizeof(payload),
            &transfer,
            1)) {
        return false;
    }

    struct aurora_ipc_received message;

    if (!ipc_receive(
            ipc_channel_endpoint(
                &channel,
                1
            ),
            &receiver,
            &message)) {
        return false;
    }

    if (message.length !=
            sizeof(payload) ||
        message.capability_count != 1 ||
        !bytes_equal(
            message.data,
            payload,
            sizeof(payload))) {
        return false;
    }

    struct aurora_capability_view view;

    if (!cap_lookup(
            &receiver,
            message.capabilities[0],
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ,
            &view)) {
        return false;
    }

    if (cap_lookup(
            &receiver,
            message.capabilities[0],
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_CONTROL,
            &view)) {
        return false;
    }

    /* Model failed final Ring 3 delivery: revoke the newly delegated
     * handle while retaining an unrelated receiver-owned capability. */
    aurora_cap_handle unrelated = cap_grant(
        &receiver, &dummy_object, AURORA_CAP_DEVICE, AURORA_RIGHT_CONTROL
    );
    if (unrelated == AURORA_CAP_INVALID ||
        unrelated == message.capabilities[0]) return false;

    ipc_received_revoke_capabilities(&receiver, &message);
    if (cap_lookup(
            &receiver, message.capabilities[0], AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ, &view)) return false;
    if (!cap_lookup(
            &receiver, unrelated, AURORA_CAP_DEVICE,
            AURORA_RIGHT_CONTROL, &view)) return false;
    if (channel.inbound[1].count != 0u) return false;
    if (channel.inbound[1].slots[0].escrow_handles[0] != AURORA_CAP_INVALID ||
        channel.inbound[1].slots[0].final_rights[0] != 0u) return false;
    /* The consumed queue slot must no longer retain its former payload. */
    for (size_t i = 0u; i < sizeof(payload); ++i) {
        if (channel.inbound[1].slots[0].data[i] != 0u) return false;
    }
    if (!cap_lookup(
            &sender, source, AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ | AURORA_RIGHT_TRANSFER, &view)) return false;

    /* Cleanup must be idempotent for already-revoked handles and must
     * not disturb capabilities allocated in unrelated table slots. */
    ipc_received_revoke_capabilities(&receiver, &message);
    if (!cap_lookup(
            &receiver, unrelated, AURORA_CAP_DEVICE,
            AURORA_RIGHT_CONTROL, &view)) return false;

    /* Failed delegation must erase the rejected payload and leave the
     * queue usable. A handle without TRANSFER rights forces rollback. */
    aurora_cap_handle nontransfer = cap_grant(
        &sender, &dummy_object, AURORA_CAP_DEVICE, AURORA_RIGHT_READ
    );
    if (nontransfer == AURORA_CAP_INVALID) return false;
    const struct aurora_ipc_transfer rejected_transfer = {
        .handle = nontransfer, .rights = AURORA_RIGHT_READ
    };
    const uint32_t rejected_slot = channel.inbound[1].tail;
    if (ipc_send(ipc_channel_endpoint(&channel, 0), &sender,
                 payload, (uint32_t)sizeof(payload),
                 &rejected_transfer, 1u)) return false;
    if (channel.inbound[1].count != 0u) return false;
    if (channel.inbound[1].slots[rejected_slot].length != 0u) return false;
    if (channel.inbound[1].slots[rejected_slot].capability_count != 0u ||
        channel.inbound[1].slots[rejected_slot].escrow_handles[0] !=
            AURORA_CAP_INVALID ||
        channel.inbound[1].slots[rejected_slot].final_rights[0] != 0u)
        return false;
    for (size_t i = 0u; i < sizeof(payload); ++i) {
        if (channel.inbound[1].slots[rejected_slot].data[i] != 0u)
            return false;
    }

    /* Roll back a partially staged message: the first delegation
     * succeeds, then the second fails due to missing TRANSFER rights. */
    const struct aurora_ipc_transfer partial_transfers[2] = {
        { .handle = source, .rights = AURORA_RIGHT_READ },
        { .handle = nontransfer, .rights = AURORA_RIGHT_READ }
    };
    const uint32_t partial_slot = channel.inbound[1].tail;
    if (ipc_send(ipc_channel_endpoint(&channel, 0), &sender,
                 payload, (uint32_t)sizeof(payload),
                 partial_transfers, 2u)) return false;
    if (channel.inbound[1].count != 0u ||
        channel.inbound[1].tail != partial_slot) return false;
    const struct aurora_ipc_message_slot *partial =
        &channel.inbound[1].slots[partial_slot];
    if (partial->length != 0u || partial->capability_count != 0u)
        return false;
    for (uint32_t i = 0u; i < AURORA_IPC_CAPS_MAX; ++i) {
        if (partial->escrow_handles[i] != AURORA_CAP_INVALID ||
            partial->final_rights[i] != 0u) return false;
    }
    for (size_t i = 0u; i < sizeof(payload); ++i) {
        if (partial->data[i] != 0u) return false;
    }
    if (!cap_lookup(&sender, source, AURORA_CAP_DEVICE,
                    AURORA_RIGHT_READ | AURORA_RIGHT_TRANSFER, &view))
        return false;

    /* A subsequent message must remain deliverable after cleanup. */
    if (!ipc_send(ipc_channel_endpoint(&channel, 0), NULL,
                  payload, (uint32_t)sizeof(payload), NULL, 0u)) return false;
    if (!ipc_receive(ipc_channel_endpoint(&channel, 1), &receiver, &message) ||
        message.length != sizeof(payload) ||
        message.capability_count != 0u ||
        !bytes_equal(message.data, payload, sizeof(payload))) return false;

    return true;
}
