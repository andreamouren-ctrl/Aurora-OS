#ifndef AURORA_G5_IPC_PENDING_H
#define AURORA_G5_IPC_PENDING_H
#include <stdbool.h>
#include <stdint.h>
#define G5_IPC_PENDING_LIMIT 16u
enum g5_pending_result {
 G5_PENDING_OK=0, G5_PENDING_FULL, G5_PENDING_DUPLICATE,
 G5_PENDING_STALE, G5_PENDING_NOT_FOUND, G5_PENDING_INVALID
};
struct g5_pending_entry {
 uint64_t request_id;
 uint64_t generation;
 bool occupied;
};
struct g5_pending_queue {
 struct g5_pending_entry entries[G5_IPC_PENDING_LIMIT];
 uint64_t active_generation;
 uint32_t count;
};
/* Single-thread owner: synchronize externally before use. */
void g5_pending_reset(struct g5_pending_queue *queue, uint64_t generation);
enum g5_pending_result g5_pending_add(struct g5_pending_queue *queue,
 uint64_t generation, uint64_t request_id);
enum g5_pending_result g5_pending_remove(struct g5_pending_queue *queue,
 uint64_t generation, uint64_t request_id);
#endif
