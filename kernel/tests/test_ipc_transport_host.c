/* Host executable regression test using the production IPC and capability
 * implementations. Compile with:
 * cc -std=c11 -O1 -Wall -Wextra -Werror -Ikernel/include \
 *   kernel/tests/test_ipc_transport_host.c -o /tmp/aurora-ipc-host
 * /tmp/aurora-ipc-host
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/security/capability.c"
#include "../src/ipc/ipc.c"

static aurora_thread_id last_woken_thread;
static uint32_t wake_count;

bool scheduler_wake_thread(aurora_thread_id id) {
    last_woken_thread = id;
    ++wake_count;
    return true;
}

static void test_wait_registration_and_delivery(void) {
    static struct aurora_ipc_channel channel;
    static struct aurora_cap_table receiver;
    struct aurora_ipc_received message = {0};
    static const uint8_t payload[] = {0x41u, 0x42u, 0x43u};

    cap_table_init(&receiver);
    ipc_channel_init(&channel);
    assert(ipc_endpoint_lifecycle_register());
    struct aurora_ipc_endpoint *reader = ipc_channel_endpoint(&channel, 1u);
    struct aurora_ipc_endpoint *writer = ipc_channel_endpoint(&channel, 0u);
    assert(reader != NULL && writer != NULL);
    assert(ipc_wait_register(reader, 17u) == AURORA_IPC_WAIT_REGISTERED);
    assert(!ipc_wait_cancel(reader, 18u));
    assert(ipc_send(writer, NULL, payload, sizeof(payload), NULL, 0u));
    assert(channel.waiter_thread[1] == 0u);
    assert(ipc_wait_register(reader, 19u) == AURORA_IPC_WAIT_READY);
    assert(ipc_receive(reader, &receiver, &message));
    assert(message.length == sizeof(payload));
    assert(message.data[0] == payload[0] && message.data[2] == payload[2]);
    assert(ipc_wait_register(reader, 20u) == AURORA_IPC_WAIT_REGISTERED);
    assert(ipc_wait_cancel(reader, 20u));
    assert(!ipc_wait_cancel(reader, 20u));

    /* A full bounded queue must reject an additional message without
     * corrupting the FIFO or losing the payload already enqueued. */
    for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
        const uint8_t value = (uint8_t)i;
        assert(ipc_send(writer, NULL, &value, 1u, NULL, 0u));
    }
    assert(channel.inbound[1].count == AURORA_IPC_QUEUE_DEPTH);
    assert(!ipc_send(writer, NULL, payload, sizeof(payload), NULL, 0u));
    for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
        assert(ipc_receive(reader, &receiver, &message));
        assert(message.length == 1u);
        assert(message.capability_count == 0u);
        assert(message.data[0] == (uint8_t)i);
    }
    assert(channel.inbound[1].count == 0u);
    assert(!ipc_receive(reader, &receiver, &message));

    /* Wrapping the ring indices must not change FIFO ordering. */
    for (uint32_t round = 0u; round < 3u; ++round) {
        for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
            const uint8_t value = (uint8_t)(round * AURORA_IPC_QUEUE_DEPTH + i);
            assert(ipc_send(writer, NULL, &value, 1u, NULL, 0u));
        }
        for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
            assert(ipc_receive(reader, &receiver, &message));
            assert(message.length == 1u);
            assert(message.data[0] ==
                   (uint8_t)(round * AURORA_IPC_QUEUE_DEPTH + i));
        }
    }
    assert(channel.inbound[1].head == channel.inbound[1].tail);
    assert(channel.inbound[1].count == 0u);

    /* Reject malformed requests without altering either queue. */
    assert(!ipc_send(NULL, NULL, payload, sizeof(payload), NULL, 0u));
    assert(!ipc_send(writer, NULL, NULL, 1u, NULL, 0u));
    assert(!ipc_send(writer, NULL, payload,
                     AURORA_IPC_PAYLOAD_MAX + 1u, NULL, 0u));
    assert(!ipc_send(writer, NULL, payload, sizeof(payload),
                     NULL, 1u));
    assert(!ipc_send(writer, NULL, payload, sizeof(payload),
                     NULL, AURORA_IPC_CAPS_MAX + 1u));
    assert(!ipc_receive(NULL, &receiver, &message));
    assert(!ipc_receive(reader, NULL, &message));
    assert(!ipc_receive(reader, &receiver, NULL));
    assert(ipc_wait_register(reader, 0u) == AURORA_IPC_WAIT_ERROR);
    assert(!ipc_wait_cancel(reader, 0u));
    assert(channel.inbound[0].count == 0u);
    assert(channel.inbound[1].count == 0u);

    /* Both directions are independent and preserve their own FIFO. */
    const uint8_t left_to_right = 0xA5u;
    const uint8_t right_to_left = 0x5Au;
    assert(ipc_send(writer, NULL, &left_to_right, 1u, NULL, 0u));
    assert(ipc_send(reader, NULL, &right_to_left, 1u, NULL, 0u));
    assert(channel.inbound[0].count == 1u);
    assert(channel.inbound[1].count == 1u);
    assert(ipc_receive(writer, &receiver, &message));
    assert(message.length == 1u && message.data[0] == right_to_left);
    assert(ipc_receive(reader, &receiver, &message));
    assert(message.length == 1u && message.data[0] == left_to_right);
    assert(channel.inbound[0].count == 0u);
    assert(channel.inbound[1].count == 0u);
    assert(ipc_send(writer, NULL, payload, sizeof(payload), NULL, 0u));
    assert(ipc_wait_register(reader, 77u) == AURORA_IPC_WAIT_READY);
    assert(ipc_receive(reader, &receiver, &message));
    assert(ipc_wait_register(reader, 77u) == AURORA_IPC_WAIT_REGISTERED);
    const uint32_t wake_before_close = wake_count;
    ipc_channel_close(&channel);
    assert(ipc_channel_quiescent(&channel));
    assert(wake_count == wake_before_close + 1u);
    assert(last_woken_thread == 77u);
    assert(channel.closed);
    assert(channel.inbound[1].count == 0u);
    assert(channel.waiter_thread[0] == 0u);
    assert(channel.waiter_thread[1] == 0u);
    for (uint32_t side = 0u; side < 2u; ++side) {
        for (uint32_t i = 0u; i < AURORA_IPC_QUEUE_DEPTH; ++i) {
            const struct aurora_ipc_message_slot *slot =
                &channel.inbound[side].slots[i];
            assert(slot->length == 0u);
            assert(slot->capability_count == 0u);
            for (uint32_t j = 0u; j < AURORA_IPC_PAYLOAD_MAX; ++j)
                assert(slot->data[j] == 0u);
            for (uint32_t j = 0u; j < AURORA_IPC_CAPS_MAX; ++j) {
                assert(slot->escrow_handles[j] == AURORA_CAP_INVALID);
                assert(slot->final_rights[j] == 0u);
            }
        }
    }
    assert(!ipc_send(writer, NULL, payload, sizeof(payload), NULL, 0u));
    assert(!ipc_receive(reader, &receiver, &message));
    assert(ipc_wait_register(reader, 21u) == AURORA_IPC_WAIT_ERROR);
    ipc_channel_close(&channel);
}

static void test_endpoint_lifetime(void) {
    static struct aurora_ipc_channel channel;
    static struct aurora_cap_table owner;
    ipc_channel_init(&channel);
    cap_table_init(&owner);
    assert(ipc_endpoint_lifecycle_register());
    struct aurora_ipc_endpoint *endpoint = ipc_channel_endpoint(&channel, 0u);
    aurora_cap_handle handle = cap_grant(
        &owner, endpoint, AURORA_CAP_IPC_ENDPOINT, AURORA_RIGHT_READ);
    assert(handle != AURORA_CAP_INVALID);
    struct aurora_capability_view view = {0};
    assert(cap_lookup_retain(&owner, handle, AURORA_CAP_IPC_ENDPOINT,
                             AURORA_RIGHT_READ, &view));
    assert(channel.active_refs == 2u);
    assert(!ipc_channel_quiescent(&channel));
    ipc_channel_close(&channel);
    assert(channel.teardown_complete);
    assert(!ipc_channel_quiescent(&channel));
    struct aurora_capability_view rejected_view = {0};
    assert(!cap_lookup_retain(&owner, handle, AURORA_CAP_IPC_ENDPOINT,
                              AURORA_RIGHT_READ, &rejected_view));
    cap_view_release(&view);
    assert(cap_revoke(&owner, handle));
    assert(ipc_channel_quiescent(&channel));
}

int main(void) {
    assert(ipc_self_test());
    test_wait_registration_and_delivery();
    test_endpoint_lifetime();
    return 0;
}
