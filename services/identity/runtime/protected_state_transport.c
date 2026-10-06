#include "protected_state_transport.h"

#include <stddef.h>
#include <stdint.h>

#include <aurora/syscall_abi.h>

static uint64_t runtime_syscall5(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;

    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );

    return rax;
}

static size_t bounded_name_length(const char *name) {
    if (name == NULL) return 0u;
    size_t length = 0u;
    while (length <= AURORA_SYS_PROTECTED_STATE_NAME_MAX && name[length] != '\0') {
        ++length;
    }
    if (length == 0u || length > AURORA_SYS_PROTECTED_STATE_NAME_MAX) return 0u;
    return length;
}

static enum aurora_identity_protected_state_read_result runtime_read_record(
    void *opaque,
    const char *name,
    uint8_t *buffer,
    size_t capacity,
    size_t *out_size
) {
    struct identity_runtime_protected_state_context *context = opaque;
    size_t name_length = bounded_name_length(name);
    if (context == NULL || context->handle == 0u || name_length == 0u ||
        buffer == NULL || out_size == NULL || capacity == 0u ||
        capacity > AURORA_SYS_PROTECTED_STATE_IO_MAX) {
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;
    }

    *out_size = 0u;
    uint64_t result = runtime_syscall5(
        AURORA_SYS_PROTECTED_STATE_READ,
        context->handle,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_length,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)capacity
    );

    if (result == AURORA_SYS_RESULT_NOT_FOUND)
        return AURORA_IDENTITY_PROTECTED_STATE_READ_NOT_FOUND;
    if (result == AURORA_SYS_RESULT_ERROR || result == 0u || result > capacity)
        return AURORA_IDENTITY_PROTECTED_STATE_READ_ERROR;

    *out_size = (size_t)result;
    return AURORA_IDENTITY_PROTECTED_STATE_READ_OK;
}

static enum aurora_identity_protected_state_create_result runtime_create_record_once(
    void *opaque,
    const char *name,
    const uint8_t *buffer,
    size_t size
) {
    struct identity_runtime_protected_state_context *context = opaque;
    size_t name_length = bounded_name_length(name);
    if (context == NULL || context->handle == 0u || name_length == 0u ||
        buffer == NULL || size == 0u || size > AURORA_SYS_PROTECTED_STATE_IO_MAX) {
        return AURORA_IDENTITY_PROTECTED_STATE_CREATE_ERROR;
    }

    uint64_t result = runtime_syscall5(
        AURORA_SYS_PROTECTED_STATE_CREATE_ONCE,
        context->handle,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_length,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size
    );

    if (result == 0u) return AURORA_IDENTITY_PROTECTED_STATE_CREATE_OK;
    if (result == AURORA_SYS_RESULT_EXISTS)
        return AURORA_IDENTITY_PROTECTED_STATE_CREATE_EXISTS;
    return AURORA_IDENTITY_PROTECTED_STATE_CREATE_ERROR;
}

static enum aurora_identity_protected_state_replace_result runtime_replace_record(
    void *opaque,
    const char *name,
    const uint8_t *buffer,
    size_t size
) {
    struct identity_runtime_protected_state_context *context = opaque;
    size_t name_length = bounded_name_length(name);
    if (context == NULL || context->handle == 0u || name_length == 0u ||
        buffer == NULL || size == 0u || size > AURORA_SYS_PROTECTED_STATE_IO_MAX) {
        return AURORA_IDENTITY_PROTECTED_STATE_REPLACE_ERROR;
    }

    return runtime_syscall5(
        AURORA_SYS_PROTECTED_STATE_REPLACE_DURABLE,
        context->handle,
        (uint64_t)(uintptr_t)name,
        (uint64_t)name_length,
        (uint64_t)(uintptr_t)buffer,
        (uint64_t)size
    ) == 0u
        ? AURORA_IDENTITY_PROTECTED_STATE_REPLACE_OK
        : AURORA_IDENTITY_PROTECTED_STATE_REPLACE_ERROR;
}

bool identity_runtime_protected_state_transport_init(
    struct identity_runtime_protected_state_context *context,
    uint64_t handle,
    struct aurora_identity_protected_state_transport_ops *out_ops
) {
    if (context == NULL || out_ops == NULL || handle == 0u) return false;

    context->handle = handle;
    out_ops->context = context;
    out_ops->read_record = runtime_read_record;
    out_ops->create_record_once_durable = runtime_create_record_once;
    out_ops->replace_record_durable = runtime_replace_record;
    return true;
}
