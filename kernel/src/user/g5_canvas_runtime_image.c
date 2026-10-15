#include <aurora/g5_canvas_runtime_image.h>

extern const uint8_t _binary_canvas_engine_bin_start[];
extern const uint8_t _binary_canvas_engine_bin_end[];

const uint8_t *g5_canvas_runtime_image(void) {
    return _binary_canvas_engine_bin_start;
}

size_t g5_canvas_runtime_image_size(void) {
    return (size_t)(_binary_canvas_engine_bin_end -
                    _binary_canvas_engine_bin_start);
}
