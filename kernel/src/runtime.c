#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count) {
    unsigned char *dst = destination;
    const unsigned char *src = source;

    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }

    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *dst = destination;
    const unsigned char *src = source;

    if (dst < src) {
        for (size_t i = 0; i < count; ++i) {
            dst[i] = src[i];
        }
    } else if (dst > src) {
        for (size_t i = count; i != 0; --i) {
            dst[i - 1] = src[i - 1];
        }
    }

    return destination;
}

void *memset(void *destination, int value, size_t count) {
    unsigned char *dst = destination;

    for (size_t i = 0; i < count; ++i) {
        dst[i] = (unsigned char)value;
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;

    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }

    return 0;
}
