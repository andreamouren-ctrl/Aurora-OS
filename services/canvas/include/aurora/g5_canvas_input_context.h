#ifndef AURORA_G5_CANVAS_INPUT_CONTEXT_H
#define AURORA_G5_CANVAS_INPUT_CONTEXT_H

#include <stdint.h>

/* Pure contextual command resolution. No IPC, compositor or FP dependency.
 * The caller supplies trusted hit-test/focus state; untrusted applications
 * cannot claim global canvas commands by forging an input event. */
enum g5_canvas_input_action {
    G5_CANVAS_INPUT_APP=0,
    G5_CANVAS_INPUT_ZOOM_IN,
    G5_CANVAS_INPUT_ZOOM_OUT,
    G5_CANVAS_INPUT_PAN_DRAG,
    G5_CANVAS_INPUT_PAN_KEYS,
    G5_CANVAS_INPUT_SWITCH_NEXT,
    G5_CANVAS_INPUT_SWITCH_PREVIOUS,
    G5_CANVAS_INPUT_SWITCH_COMMIT
};
/* Fixed-point wheel factors: 5/4 zoom in and 4/5 zoom out.
 * Reciprocal ratios avoid floating point and preserve deterministic bounds. */
#define G5_CANVAS_WHEEL_ZOOM_IN_Q16 UINT32_C(81920)
#define G5_CANVAS_WHEEL_ZOOM_OUT_Q16 UINT32_C(52429)
static inline uint32_t g5_canvas_zoom_factor(
    enum g5_canvas_input_action action) {
    if (action==G5_CANVAS_INPUT_ZOOM_IN)
        return G5_CANVAS_WHEEL_ZOOM_IN_Q16;
    if (action==G5_CANVAS_INPUT_ZOOM_OUT)
        return G5_CANVAS_WHEEL_ZOOM_OUT_Q16;
    return 0u;
}
struct g5_canvas_input_context {
    uint8_t canvas_navigation;
    uint8_t app_control_active;
    uint8_t modal_active;
    uint8_t pointer_over_window;
    uint8_t right_held;
    uint8_t right_wheel_used;
    uint8_t left_drag_on_background;
};
static inline enum g5_canvas_input_action g5_canvas_resolve_wheel(
    const struct g5_canvas_input_context *ctx,int32_t wheel_y) {
    if (!ctx || wheel_y==0 || ctx->modal_active ||
        ctx->app_control_active) return G5_CANVAS_INPUT_APP;
    if (ctx->right_held)
        return wheel_y>0 ? G5_CANVAS_INPUT_SWITCH_PREVIOUS :
                           G5_CANVAS_INPUT_SWITCH_NEXT;
    if (!ctx->canvas_navigation || ctx->pointer_over_window)
        return G5_CANVAS_INPUT_APP;
    return wheel_y>0 ? G5_CANVAS_INPUT_ZOOM_IN :
                       G5_CANVAS_INPUT_ZOOM_OUT;
}
static inline enum g5_canvas_input_action g5_canvas_resolve_keys(
    const struct g5_canvas_input_context *ctx,uint8_t navigation_key) {
    if (!ctx || !navigation_key || !ctx->canvas_navigation ||
        ctx->modal_active || ctx->app_control_active)
        return G5_CANVAS_INPUT_APP;
    return G5_CANVAS_INPUT_PAN_KEYS;
}
static inline enum g5_canvas_input_action g5_canvas_resolve_drag(
    const struct g5_canvas_input_context *ctx) {
    if (!ctx || !ctx->canvas_navigation || ctx->modal_active ||
        ctx->app_control_active || ctx->pointer_over_window ||
        !ctx->left_drag_on_background)
        return G5_CANVAS_INPUT_APP;
    return G5_CANVAS_INPUT_PAN_DRAG;
}
static inline enum g5_canvas_input_action g5_canvas_resolve_right_release(
    const struct g5_canvas_input_context *ctx) {
    if (!ctx || !ctx->right_wheel_used || ctx->modal_active ||
        ctx->app_control_active) return G5_CANVAS_INPUT_APP;
    return G5_CANVAS_INPUT_SWITCH_COMMIT;
}
#endif
