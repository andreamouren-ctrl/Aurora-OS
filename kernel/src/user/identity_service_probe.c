#include <stddef.h>
#include <stdint.h>

#include <aurora/identity_service_probe.h>

/*
 * The trusted Identity runtime is now compiled from services/identity/runtime
 * as a freestanding x86-64 program, converted to a flat image and embedded in
 * the kernel link as a binary object. The historical probe accessors remain as
 * a narrow bootstrap interface while callers migrate to final service naming.
 */
extern const uint8_t _binary_identity_service_bin_start[];
extern const uint8_t _binary_identity_service_bin_end[];

_Static_assert(
    sizeof(struct aurora_identity_service_message) ==
        AURORA_IDENTITY_SERVICE_MESSAGE_SIZE,
    "Identity service protocol message must remain 16 bytes"
);

const uint8_t *identity_service_probe_image(void) {
    return _binary_identity_service_bin_start;
}

size_t identity_service_probe_image_size(void) {
    return (size_t)(
        _binary_identity_service_bin_end -
        _binary_identity_service_bin_start
    );
}
