#ifndef AURORA_IDENTITY_SERVICE_PROBE_H
#define AURORA_IDENTITY_SERVICE_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include <aurora/identity_service_protocol.h>

const uint8_t *identity_service_probe_image(void);
size_t identity_service_probe_image_size(void);

#endif
