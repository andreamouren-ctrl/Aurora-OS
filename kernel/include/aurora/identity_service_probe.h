#ifndef AURORA_IDENTITY_SERVICE_PROBE_H
#define AURORA_IDENTITY_SERVICE_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_SERVICE_READY_SIZE 8u

const uint8_t *identity_service_probe_image(void);
size_t identity_service_probe_image_size(void);
const uint8_t *identity_service_ready_payload(void);

#endif
