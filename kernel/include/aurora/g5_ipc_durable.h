#ifndef AURORA_G5_IPC_DURABLE_H
#define AURORA_G5_IPC_DURABLE_H
#include <aurora/protected_state.h>
#include <stdint.h>
#include <stdbool.h>
/* Trusted kernel/state-broker durability adapter: after verified restore,
 * write a generation/request high-water mark to durable protected state
 * BEFORE executing any side effect. A failed write fails closed.
 * This guarantees at-most-once admission, not exactly-once completion.
 */
struct g5_ipc_durable_ledger {
 struct aurora_cap_table *capabilities;
 aurora_cap_handle handle;
 struct aurora_protected_state_namespace *state;
 const char *record_name;
 uint64_t generation;
 uint64_t last_request_id;
 bool loaded;
};
bool g5_durable_restore(struct g5_ipc_durable_ledger *ledger,
 uint64_t active_session_generation);
bool g5_durable_reserve(struct g5_ipc_durable_ledger *ledger,
 uint64_t generation,uint64_t request_id);
/* Suitable for g5_dispatch_context.reserve after authenticated restore. */
bool g5_durable_reserve_callback(void *context,
 uint64_t generation,uint64_t request_id);
/* Kernel-only QEMU cold-reboot probe; recovered=true after a prior ledger. */
bool g5_ipc_durable_boot_probe(bool *recovered);
#endif
