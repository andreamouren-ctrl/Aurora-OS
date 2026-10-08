#ifndef AURORA_G5_FRAME_SUBMISSION_H
#define AURORA_G5_FRAME_SUBMISSION_H
#include <aurora/g5_surface_registry.h>
#include <aurora/g5_presentation_queue.h>
/* Service-local integration; single owner must serialize all calls.
 * Snapshot ownership transfers to caller only on successful completion. */
struct g5_frame_submission {
 struct g5_surface_registry registry;
 struct g5_presentation_queue queue;
};
bool g5_frame_submission_begin(struct g5_frame_submission *f,uint64_t generation);
bool g5_frame_submission_request(struct g5_frame_submission *f,uint32_t slot,
 uint64_t request_id,uint64_t *configure_serial);
bool g5_frame_submission_complete(struct g5_frame_submission *f,uint32_t slot,
 uint64_t request_id,uint64_t configure_serial,
 struct aurora_graphics_surface_snapshot *snapshot,uint64_t *presentation_serial);
bool g5_frame_submission_cancel(struct g5_frame_submission *f,uint64_t request_id);
void g5_frame_submission_end(struct g5_frame_submission *f);
#endif
