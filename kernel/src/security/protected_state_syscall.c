#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/process.h>
#include <aurora/protected_state.h>
#include <aurora/protected_state_syscall.h>
#include <aurora/syscall.h>
#include <aurora/usercopy.h>

_Static_assert(
    AURORA_SYS_PROTECTED_STATE_IO_MAX <= AURORA_PROTECTED_STATE_RECORD_MAX,
    "Ring 3 Protected State I/O bound exceeds kernel record bound"
);

static void secure_zero(void *buffer, size_t length) {
    volatile uint8_t *bytes = buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static bool copy_record_name(
    struct aurora_process *process,
    uint64_t user_name,
    uint64_t name_length,
    char out[AURORA_SYS_PROTECTED_STATE_NAME_MAX + 1u]
) {
    if (process == NULL || user_name == 0u || name_length == 0u ||
        name_length > AURORA_SYS_PROTECTED_STATE_NAME_MAX) {
        return false;
    }

    if (!copy_from_user(
            process,
            out,
            user_name,
            (size_t)name_length)) {
        return false;
    }

    for (size_t i = 0u; i < (size_t)name_length; ++i) {
        if (out[i] == '\0') return false;
    }

    out[name_length] = '\0';
    return true;
}

static struct aurora_protected_state_namespace *lookup_namespace(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t required_rights
) {
    struct aurora_capability_view view;
    if (process == NULL ||
        !cap_lookup(
            &process->capabilities,
            (aurora_cap_handle)handle,
            AURORA_CAP_PROTECTED_STATE,
            required_rights,
            &view) ||
        view.object == NULL) {
        return NULL;
    }
    return (struct aurora_protected_state_namespace *)view.object;
}

uint64_t protected_state_syscall_read(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t user_name,
    uint64_t name_length,
    uint64_t user_buffer,
    uint64_t capacity
) {
    char name[AURORA_SYS_PROTECTED_STATE_NAME_MAX + 1u];
    uint8_t buffer[AURORA_SYS_PROTECTED_STATE_IO_MAX];
    size_t length = 0u;
    uint64_t result = AURORA_SYS_RESULT_ERROR;

    if (capacity == 0u || capacity > AURORA_SYS_PROTECTED_STATE_IO_MAX ||
        user_buffer == 0u ||
        !copy_record_name(process, user_name, name_length, name)) {
        return AURORA_SYS_RESULT_ERROR;
    }

    struct aurora_protected_state_namespace *state =
        lookup_namespace(process, handle, AURORA_RIGHT_READ);
    if (state == NULL) return AURORA_SYS_RESULT_ERROR;

    enum aurora_protected_state_read_result read_result =
        protected_state_read_record(
            &process->capabilities,
            (aurora_cap_handle)handle,
            state,
            name,
            buffer,
            (size_t)capacity,
            &length);

    if (read_result == AURORA_PROTECTED_STATE_READ_NOT_FOUND) {
        result = AURORA_SYS_RESULT_NOT_FOUND;
        goto out;
    }
    if (read_result != AURORA_PROTECTED_STATE_READ_OK ||
        length > AURORA_SYS_PROTECTED_STATE_IO_MAX) {
        goto out;
    }

    if (length != 0u &&
        !copy_to_user(process, user_buffer, buffer, length)) {
        goto out;
    }

    result = (uint64_t)length;

out:
    secure_zero(buffer, sizeof(buffer));
    return result;
}

uint64_t protected_state_syscall_create_once(
    struct aurora_process *process,
    uint64_t handle,
    uint64_t user_name,
    uint64_t name_length,
    uint64_t user_data,
    uint64_t length
) {
    char name[AURORA_SYS_PROTECTED_STATE_NAME_MAX + 1u];
    uint8_t buffer[AURORA_SYS_PROTECTED_STATE_IO_MAX];
    uint64_t result = AURORA_SYS_RESULT_ERROR;

    if (length == 0u || length > AURORA_SYS_PROTECTED_STATE_IO_MAX ||
        user_data == 0u ||
        !copy_record_name(process, user_name, name_length, name)) {
        return AURORA_SYS_RESULT_ERROR;
    }

    struct aurora_protected_state_namespace *state =
        lookup_namespace(process, handle, AURORA_RIGHT_WRITE);
    if (state == NULL) return AURORA_SYS_RESULT_ERROR;

    if (!copy_from_user(
            process,
            buffer,
            user_data,
            (size_t)length)) {
        goto out;
    }

    enum aurora_protected_state_create_once_result create_result =
        protected_state_create_record_once_durable(
            &process->capabilities,
            (aurora_cap_handle)handle,
            state,
            name,
            buffer,
            (size_t)length);

    if (create_result == AURORA_PROTECTED_STATE_CREATE_ONCE_OK) {
        result = 0u;
    } else if (create_result == AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS) {
        result = AURORA_SYS_RESULT_EXISTS;
    }

out:
    secure_zero(buffer, sizeof(buffer));
    return result;
}
