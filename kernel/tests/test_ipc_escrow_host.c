/* Regression for endpoint capabilities transferred through IPC escrow. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "../src/security/capability.c"
#include "../src/ipc/ipc.c"

bool scheduler_wake_thread(aurora_thread_id id) {
    (void)id;
    return true;
}

int main(void) {
    static struct aurora_ipc_channel channel;
    static struct aurora_cap_table sender, receiver;
    ipc_channel_init(&channel);
    cap_table_init(&sender);
    cap_table_init(&receiver);
    assert(ipc_endpoint_lifecycle_register());
    aurora_cap_handle source = cap_grant(
        &sender, ipc_channel_endpoint(&channel, 1u),
        AURORA_CAP_IPC_ENDPOINT,
        AURORA_RIGHT_READ | AURORA_RIGHT_TRANSFER);
    assert(source != AURORA_CAP_INVALID);
    struct aurora_ipc_transfer transfer = {
        .handle = source, .rights = AURORA_RIGHT_READ
    };
    uint8_t payload = 7u;
    assert(ipc_send(ipc_channel_endpoint(&channel, 0u), &sender,
                    &payload, 1u, &transfer, 1u));
    assert(channel.active_refs == 2u);
    struct aurora_ipc_received received = {0};
    assert(ipc_receive(ipc_channel_endpoint(&channel, 1u), &receiver, &received));
    assert(received.capability_count == 1u);
    assert(channel.active_refs == 2u);
    assert(cap_revoke(&receiver, received.capabilities[0]));
    assert(ipc_send(ipc_channel_endpoint(&channel, 0u), &sender,
                    &payload, 1u, &transfer, 1u));
    ipc_channel_close(&channel);
    assert(channel.teardown_complete);
    assert(!ipc_channel_quiescent(&channel));
    assert(cap_revoke(&sender, source));
    assert(ipc_channel_quiescent(&channel));
    return 0;
}
