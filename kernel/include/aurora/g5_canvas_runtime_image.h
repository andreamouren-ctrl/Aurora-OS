#ifndef AURORA_G5_CANVAS_RUNTIME_IMAGE_H
#define AURORA_G5_CANVAS_RUNTIME_IMAGE_H
#include <stddef.h>
#include <stdint.h>
/* Packaged separately from the primary Shell image; no process is launched
 * merely by accessing this immutable binary. */
const uint8_t *g5_canvas_runtime_image(void);
size_t g5_canvas_runtime_image_size(void);
#endif
