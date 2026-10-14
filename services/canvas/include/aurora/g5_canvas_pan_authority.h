#ifndef AURORA_G5_CANVAS_PAN_AUTHORITY_H
#define AURORA_G5_CANVAS_PAN_AUTHORITY_H
/* Host-only integration contract prototype: caller supplies already verified
 * identities. This module DOES NOT authenticate an IPC message or grant rights.
 * Production must bind these fields to actual kernel-owned objects. */
#include <aurora/g5_canvas_integer_pan.h>
struct g5_canvas_pan_owner {
    uint64_t session_generation;
    uint64_t process_generation;
    uint64_t surface_generation;
    uint64_t owner_token;
    bool active;
};
struct g5_canvas_pan_request {
    uint64_t session_generation;
    uint64_t process_generation;
    uint64_t surface_generation;
    uint64_t owner_token;
    uint64_t expected_camera_revision;
    int32_t dx_pixels,dy_pixels;
};
static inline bool g5_canvas_pan_authorized(
    struct g5_integer_pan *camera,
    const struct g5_canvas_pan_owner *owner,
    const struct g5_canvas_pan_request *request) {
    if (!camera || !owner || !request || !owner->active ||
        !owner->session_generation || !owner->process_generation ||
        !owner->surface_generation || !owner->owner_token ||
        request->session_generation != owner->session_generation ||
        request->process_generation != owner->process_generation ||
        request->surface_generation != owner->surface_generation ||
        request->owner_token != owner->owner_token)
        return false;
    return g5_integer_pan_apply(camera,request->dx_pixels,
                                request->dy_pixels,
                                request->expected_camera_revision);
}
#endif
