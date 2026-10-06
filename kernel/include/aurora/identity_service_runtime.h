#ifndef AURORA_IDENTITY_SERVICE_RUNTIME_H
#define AURORA_IDENTITY_SERVICE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

const uint8_t *identity_service_runtime_image(void);
size_t identity_service_runtime_image_size(void);

/* Runtime proof for the long-lived blocking Identity request loop. */
bool identity_service_runtime_self_test(void);

#endif
