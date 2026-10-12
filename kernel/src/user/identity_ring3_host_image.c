#include <stddef.h>
#include <stdint.h>
extern const uint8_t _binary_identity_ring3_bin_start[];
extern const uint8_t _binary_identity_ring3_bin_end[];
const uint8_t *identity_ring3_host_image(void) {
    return _binary_identity_ring3_bin_start;
}
size_t identity_ring3_host_image_size(void) {
    return (size_t)(_binary_identity_ring3_bin_end -
                    _binary_identity_ring3_bin_start);
}
