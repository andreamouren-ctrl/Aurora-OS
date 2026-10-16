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

bool scheduler_wake_thread(aurora_thread_id id) {
    (void)id;
    return true;
}

static void test_wait_registration_and_delivery(void) {
    static struct aurora_ipc_channel channel;
    static struct aurora_cap_table receiver;
    struct aurora_ipc_received message = {0};
    static const uint8_t payload[] = {0x41u, 0x42u, 0x43u};

    cap_table_init(&receiver);
    ipc_channel_init(&channel);
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
}

int main(void) {
    assert(ipc_self_test());
    test_wait_registration_and_delivery();
    return 0;
}
