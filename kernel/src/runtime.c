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


/*
 * Clang lowers unsigned 128-bit division to __udivti3 on x86_64 when no
 * hardware instruction can perform the full operation directly.
 * Aurora is freestanding, so provide the compiler ABI helper internally
 * instead of pulling in a hosted runtime library.
 */
unsigned __int128 __udivti3(
    unsigned __int128 numerator,
    unsigned __int128 denominator
) {
    if (denominator == 0) {
        /*
         * Division by zero here indicates a kernel arithmetic invariant
         * violation. Returning the maximum value keeps this low-level ABI
         * helper self-contained; callers must already reject zero divisors.
         */
        return ~(unsigned __int128)0;
    }

    if (numerator < denominator) {
        return 0;
    }

    unsigned __int128 quotient = 0;
    unsigned __int128 remainder = 0;

    for (int bit = 127; bit >= 0; --bit) {
        remainder =
            (remainder << 1) |
            ((numerator >> (unsigned)bit) & 1u);

        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |=
                (unsigned __int128)1 <<
                (unsigned)bit;
        }
    }

    return quotient;
}
