#include <aurora/g5_canvas_input_context.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct g5_canvas_input_context c={0};
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_APP)==0u);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_ZOOM_IN)==81920u);
    assert(g5_canvas_zoom_factor(G5_CANVAS_INPUT_ZOOM_OUT)==52429u);
    c.canvas_navigation=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_ZOOM_IN);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_ZOOM_OUT);
    assert(g5_canvas_resolve_wheel(&c,0)==G5_CANVAS_INPUT_APP);
    c.right_held=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_SWITCH_PREVIOUS);
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_SWITCH_NEXT);
    c.right_wheel_used=1;
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_SWITCH_COMMIT);
    c.app_control_active=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_keys(&c,1)==G5_CANVAS_INPUT_APP);
    assert(g5_canvas_resolve_right_release(&c)==G5_CANVAS_INPUT_APP);
    c.app_control_active=0;c.modal_active=1;
    assert(g5_canvas_resolve_wheel(&c,-1)==G5_CANVAS_INPUT_APP);
    c.modal_active=0;c.right_held=0;
    c.pointer_over_window=1;
    assert(g5_canvas_resolve_wheel(&c,1)==G5_CANVAS_INPUT_APP);
    c.left_drag_on_background=1;
    assert(g5_canvas_resolve_drag(&c)==G5_CANVAS_INPUT_APP);
    c.pointer_over_window=0;
    assert(g5_canvas_resolve_drag(&c)==G5_CANVAS_INPUT_PAN_DRAG);
    assert(g5_canvas_resolve_keys(&c,1)==G5_CANVAS_INPUT_PAN_KEYS);
    assert(g5_canvas_resolve_keys(&c,0)==G5_CANVAS_INPUT_APP);
    puts("G5 contextual input resolver: PASS");
    return 0;
}
