#ifndef AURORA_G5_PRESENTATION_QUEUE_H
#define AURORA_G5_PRESENTATION_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
#define G5_PRESENTATION_QUEUE_CAPACITY 16u
struct g5_presentation_ticket { uint64_t session_generation; uint64_t request_id; uint64_t surface_id; uint64_t configure_serial; bool active; };
struct g5_presentation_queue { struct g5_presentation_ticket entries[G5_PRESENTATION_QUEUE_CAPACITY]; uint64_t generation; uint64_t last_request_id; bool active; };
bool g5_presentation_queue_begin(struct g5_presentation_queue *q,uint64_t generation);
bool g5_presentation_queue_submit(struct g5_presentation_queue *q,uint64_t generation,uint64_t request_id,uint64_t surface_id,uint64_t configure_serial);
bool g5_presentation_queue_complete(struct g5_presentation_queue *q,uint64_t generation,uint64_t request_id,uint64_t surface_id,uint64_t configure_serial);
bool g5_presentation_queue_cancel(struct g5_presentation_queue *q,uint64_t generation,uint64_t request_id);
void g5_presentation_queue_revoke(struct g5_presentation_queue *q);
#endif
