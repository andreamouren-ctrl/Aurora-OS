#ifndef AURORA_G5_CANVAS_PAN_TRANSACTION_H
#define AURORA_G5_CANVAS_PAN_TRANSACTION_H
/* Host-side staging contract only. Caller must authenticate IPC, validate
 * window ownership, stage compositor moves, present and commit atomically.
 * No kernel/runtime hook is implemented by this header. */
#include <aurora/g5_canvas_pan_authority.h>
struct g5_canvas_pan_transaction {
    struct g5_integer_pan camera;
    uint64_t scene_revision;
};
/* The presenter callback must provide an all-or-nothing display transaction. */
typedef bool (*g5_canvas_pan_present_fn)(void *context);
static inline bool g5_canvas_pan_prepare(
    const struct g5_canvas_pan_transaction *current,
    const struct g5_canvas_pan_owner *owner,
    const struct g5_canvas_pan_request *request,
    struct g5_canvas_pan_transaction *staged) {
    if (!current || !owner || !request || !staged ||
        current == staged ||
        !current->scene_revision ||
        current->scene_revision != owner->scene_revision ||
        current->scene_revision == UINT64_MAX)
        return false;
    struct g5_canvas_pan_transaction next=*current;
    if (!g5_canvas_pan_authorized(&next.camera,owner,request))
        return false;
    ++next.scene_revision;
    *staged=next;
    return true;
}
/* A successful presentation is the only commit point. If it fails, the
 * caller's current state remains unchanged. The presenter owns rollback. */
static inline bool g5_canvas_pan_present_commit(
    struct g5_canvas_pan_transaction *current,
    const struct g5_canvas_pan_owner *owner,
    const struct g5_canvas_pan_request *request,
    g5_canvas_pan_present_fn present, void *context) {
    if (!current || !present) return false;
    struct g5_canvas_pan_transaction staged;
    if (!g5_canvas_pan_prepare(current,owner,request,&staged)) return false;
    if (!present(context)) return false;
    *current=staged;
    return true;
}
#endif
