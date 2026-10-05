#ifndef AURORA_PROTECTED_STATE_H
#define AURORA_PROTECTED_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/vfs.h>

#define AURORA_PROTECTED_STATE_SCOPE_MAX 31u
#define AURORA_PROTECTED_STATE_PREFIX "/system/.protected"

#define AURORA_PROTECTED_STATE_RIGHTS \
    (AURORA_RIGHT_READ | AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL)

struct aurora_protected_state_namespace {
    char scope[AURORA_PROTECTED_STATE_SCOPE_MAX + 1u];
    char root[AURORA_VFS_PATH_MAX];
    bool initialized;
};

bool protected_state_namespace_init(
    struct aurora_protected_state_namespace *state,
    const char *scope
);

bool protected_state_prepare(
    struct aurora_protected_state_namespace *state
);

aurora_cap_handle protected_state_grant(
    struct aurora_cap_table *table,
    struct aurora_protected_state_namespace *state,
    uint64_t rights
);

bool protected_state_create_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
);

bool protected_state_read_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    void *buffer,
    size_t capacity,
    size_t *out_length
);

bool protected_state_write_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
);

bool protected_state_truncate_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    uint64_t size
);

bool protected_state_remove(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
);

bool protected_state_rename(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *old_relative_path,
    const char *new_relative_path
);

bool protected_state_fsync(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
);

bool protected_state_self_test(void);

#endif
