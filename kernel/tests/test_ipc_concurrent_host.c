/* Concurrent endpoint lifecycle regression using production implementations. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/security/capability.c"
#include "../src/ipc/ipc.c"

bool scheduler_wake_thread(aurora_thread_id id) {
    (void)id;
    return true;
}

static struct aurora_ipc_channel channel;
static struct aurora_cap_table owner;
static struct aurora_cap_table receiver;
static aurora_cap_handle writer_handle;
static aurora_cap_handle reader_handle;
static atomic_bool start;

static void *sender(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&start, memory_order_acquire)) { }
    for (unsigned i = 0; i < 2000u; ++i) {
        struct aurora_capability_view view = {0};
        if (!cap_lookup_retain(&owner, writer_handle, AURORA_CAP_IPC_ENDPOINT,
                               AURORA_RIGHT_WRITE, &view)) break;
        const uint8_t payload = (uint8_t)i;
        (void)ipc_send(view.object, NULL, &payload, 1u, NULL, 0u);
        cap_view_release(&view);
    }
    return NULL;
}

static void *reader(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&start, memory_order_acquire)) { }
    for (unsigned i = 0; i < 2000u; ++i) {
        struct aurora_capability_view view = {0};
        if (!cap_lookup_retain(&owner, reader_handle, AURORA_CAP_IPC_ENDPOINT,
                               AURORA_RIGHT_READ, &view)) break;
        struct aurora_ipc_received message = {0};
        (void)ipc_receive(view.object, &receiver, &message);
        cap_view_release(&view);
    }
    return NULL;
}

static void *closer(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&start, memory_order_acquire)) { }
    ipc_channel_close(&channel);
    return NULL;
}

int main(void) {
    ipc_channel_init(&channel);
    cap_table_init(&owner);
    cap_table_init(&receiver);
    assert(ipc_endpoint_lifecycle_register());
    writer_handle = cap_grant(&owner, ipc_channel_endpoint(&channel, 0u),
                              AURORA_CAP_IPC_ENDPOINT, AURORA_RIGHT_WRITE);
    reader_handle = cap_grant(&owner, ipc_channel_endpoint(&channel, 1u),
                              AURORA_CAP_IPC_ENDPOINT, AURORA_RIGHT_READ);
    assert(writer_handle != AURORA_CAP_INVALID);
    assert(reader_handle != AURORA_CAP_INVALID);
    pthread_t threads[3];
    assert(pthread_create(&threads[0], NULL, sender, NULL) == 0);
    assert(pthread_create(&threads[1], NULL, reader, NULL) == 0);
    assert(pthread_create(&threads[2], NULL, closer, NULL) == 0);
    atomic_store_explicit(&start, true, memory_order_release);
    for (unsigned i = 0; i < 3u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(cap_revoke(&owner, writer_handle));
    assert(cap_revoke(&owner, reader_handle));
    assert(ipc_channel_quiescent(&channel));
    return 0;
}
