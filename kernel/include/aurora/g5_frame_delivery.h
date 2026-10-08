#ifndef AURORA_G5_FRAME_DELIVERY_H
#define AURORA_G5_FRAME_DELIVERY_H
#include <aurora/g5_frame_submission.h>
#define G5_FRAME_DELIVERY_CAPACITY 8u
/* One serialized service owner; borrowed metadata only, never retains snapshots. */
struct g5_frame_delivery_entry {
 uint64_t session_generation,request_id,presentation_serial;
 uint32_t slot;
 bool active;
};
struct g5_frame_delivery {
 struct g5_frame_submission *submission;
 struct g5_frame_delivery_entry entries[G5_FRAME_DELIVERY_CAPACITY];
 uint64_t last_delivered_serial;
};
bool g5_frame_delivery_bind(struct g5_frame_delivery *d,struct g5_frame_submission *f);
bool g5_frame_delivery_publish(struct g5_frame_delivery *d,uint32_t slot,
 uint64_t request_id,uint64_t configure_serial,
 struct aurora_graphics_surface_snapshot *snapshot,uint64_t *presentation_serial);
bool g5_frame_delivery_ack(struct g5_frame_delivery *d,uint64_t session_generation,
 uint64_t presentation_serial);
void g5_frame_delivery_revoke(struct g5_frame_delivery *d);
#endif
