#include <stddef.h>
#include <stdint.h>

#include <aurora/user_session_host_image.h>

extern const uint8_t _binary_user_session_host_bin_start[];
extern const uint8_t _binary_user_session_host_bin_end[];

const uint8_t *user_session_host_image(void) {
    return _binary_user_session_host_bin_start;
}

size_t user_session_host_image_size(void) {
    return (size_t)(
        _binary_user_session_host_bin_end -
        _binary_user_session_host_bin_start
    );
}
