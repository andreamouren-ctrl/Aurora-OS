#include <stddef.h>

void *memset(void *destination, int value, size_t length) {
    unsigned char *dst = (unsigned char *)destination;
    for (size_t i = 0u; i < length; ++i) dst[i] = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    for (size_t i = 0u; i < length; ++i) dst[i] = src[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;

    if (dst == src || length == 0u) return destination;

    if (dst < src || dst >= src + length) {
        for (size_t i = 0u; i < length; ++i) dst[i] = src[i];
    } else {
        for (size_t i = length; i != 0u; --i) dst[i - 1u] = src[i - 1u];
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;

    for (size_t i = 0u; i < length; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }

    return 0;
}
