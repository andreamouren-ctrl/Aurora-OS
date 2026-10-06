#include <stddef.h>
#include <stdint.h>

#include <aurora/syscall_abi.h>

static uint64_t runtime_syscall1(uint64_t number, uint64_t a1) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;

    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi)
        : "rcx", "r11", "memory"
    );

    return rax;
}

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

void *malloc(size_t size) {
    if (size == 0u ||
        (uint64_t)size > AURORA_SYS_USER_MEMORY_MAX_ALLOCATION_BYTES) {
        return NULL;
    }

    uint64_t result = runtime_syscall1(
        AURORA_SYS_USER_MEMORY_ALLOC,
        (uint64_t)size
    );

    if (result == AURORA_SYS_RESULT_ERROR || result == 0u) return NULL;
    return (void *)(uintptr_t)result;
}

void free(void *pointer) {
    if (pointer == NULL) return;
    (void)runtime_syscall1(
        AURORA_SYS_USER_MEMORY_FREE,
        (uint64_t)(uintptr_t)pointer
    );
}

void *calloc(size_t count, size_t size) {
    if (count == 0u || size == 0u || count > SIZE_MAX / size) return NULL;

    size_t total = count * size;
    void *memory = malloc(total);
    if (memory == NULL) return NULL;

    memset(memory, 0, total);
    return memory;
}
