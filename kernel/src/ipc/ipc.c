#include <stddef.h>
#include <stdint.h>

#include <aurora/ipc.h>

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

        channel->endpoints[side].channel =
            channel;

        channel->endpoints[side].side =
            (uint8_t)side;
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

    spinlock_unlock(
        &channel->lock
    );

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
    }

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

bool ipc_self_test(void) {
    struct aurora_ipc_channel channel;
    struct aurora_cap_table sender;
    struct aurora_cap_table receiver;

    uint64_t dummy_object = 0x1C0FFEEu;

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

    return true;
}
