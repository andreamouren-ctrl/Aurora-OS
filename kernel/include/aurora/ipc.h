#ifndef AURORA_IPC_H
#define AURORA_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/scheduler.h>
#include <aurora/spinlock.h>

#define AURORA_IPC_QUEUE_DEPTH 16u
#define AURORA_IPC_PAYLOAD_MAX 256u
#define AURORA_IPC_CAPS_MAX 4u

enum aurora_ipc_wait_result {
    AURORA_IPC_WAIT_ERROR = 0,
    AURORA_IPC_WAIT_READY,
    AURORA_IPC_WAIT_REGISTERED
};

struct aurora_ipc_transfer {
    aurora_cap_handle handle;
    uint64_t rights;
};

struct aurora_ipc_received {
    uint8_t data[AURORA_IPC_PAYLOAD_MAX];
    uint32_t length;

    aurora_cap_handle capabilities[
        AURORA_IPC_CAPS_MAX
    ];

    uint32_t capability_count;
};

struct aurora_ipc_message_slot {
    uint8_t data[AURORA_IPC_PAYLOAD_MAX];
    uint32_t length;

    aurora_cap_handle escrow_handles[
        AURORA_IPC_CAPS_MAX
    ];

    uint64_t final_rights[
        AURORA_IPC_CAPS_MAX
    ];

    uint32_t capability_count;
};

struct aurora_ipc_queue {
    struct aurora_ipc_message_slot slots[
        AURORA_IPC_QUEUE_DEPTH
    ];

    uint32_t head;
    uint32_t tail;
    uint32_t count;
};

struct aurora_ipc_channel;

struct aurora_ipc_endpoint {
    struct aurora_ipc_channel *channel;
    uint8_t side;
};

struct aurora_ipc_channel {
    aurora_spinlock lock;

    struct aurora_ipc_queue inbound[2];
    struct aurora_cap_table escrow;
    aurora_thread_id waiter_thread[2];

    struct aurora_ipc_endpoint endpoints[2];
};

void ipc_channel_init(
    struct aurora_ipc_channel *channel
);

struct aurora_ipc_endpoint *ipc_channel_endpoint(
    struct aurora_ipc_channel *channel,
    uint8_t side
);

bool ipc_send(
    struct aurora_ipc_endpoint *endpoint,
    struct aurora_cap_table *sender_caps,
    const void *data,
    uint32_t length,
    const struct aurora_ipc_transfer *transfers,
    uint32_t transfer_count
);

bool ipc_receive(
    struct aurora_ipc_endpoint *endpoint,
    struct aurora_cap_table *receiver_caps,
    struct aurora_ipc_received *out
);

/*
 * Register one scheduler thread to be woken when the endpoint becomes readable.
 * At most one waiter is accepted per endpoint in this foundation. READY means
 * data is already queued and the caller must not block.
 */
enum aurora_ipc_wait_result ipc_wait_register(
    struct aurora_ipc_endpoint *endpoint,
    aurora_thread_id thread_id
);

bool ipc_wait_cancel(
    struct aurora_ipc_endpoint *endpoint,
    aurora_thread_id thread_id
);

bool ipc_self_test(void);

#endif