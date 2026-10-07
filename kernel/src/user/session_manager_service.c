#include <stddef.h>
#include <stdint.h>

#include <aurora/session_manager_service.h>

extern const uint8_t _binary_session_manager_bin_start[];
extern const uint8_t _binary_session_manager_bin_end[];

const uint8_t *session_manager_service_image(void) {
    return _binary_session_manager_bin_start;
}

size_t session_manager_service_image_size(void) {
    return (size_t)(
        _binary_session_manager_bin_end -
        _binary_session_manager_bin_start
    );
}
