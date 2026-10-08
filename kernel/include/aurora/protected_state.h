#ifndef AURORA_PROTECTED_STATE_H
#define AURORA_PROTECTED_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/vfs.h>

#define AURORA_PROTECTED_STATE_SCOPE_MAX 31u
#define AURORA_PROTECTED_STATE_PREFIX "/system/.protected"
#define AURORA_PROTECTED_STATE_RECORD_MAX 8192u

#define AURORA_PROTECTED_STATE_RIGHTS \
    (AURORA_RIGHT_READ | AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL)

struct aurora_protected_state_namespace {
    char scope[AURORA_PROTECTED_STATE_SCOPE_MAX + 1u];
    char root[AURORA_VFS_PATH_MAX];
    bool initialized;
};

enum aurora_protected_state_read_result {
    AURORA_PROTECTED_STATE_READ_OK = 0,
    AURORA_PROTECTED_STATE_READ_NOT_FOUND,
    AURORA_PROTECTED_STATE_READ_ERROR
};

enum aurora_protected_state_create_once_result {
    AURORA_PROTECTED_STATE_CREATE_ONCE_OK = 0,
    AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS,
    AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR
};

enum aurora_protected_state_replace_result {
    AURORA_PROTECTED_STATE_REPLACE_OK = 0,
    AURORA_PROTECTED_STATE_REPLACE_ERROR
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

/*
 * Security-sensitive bounded record read. NOT_FOUND is returned only after a
 * clean namespace lookup. Existing-but-unreadable, malformed, oversized or
 * otherwise ambiguous records fail closed as READ_ERROR.
 */
enum aurora_protected_state_read_result protected_state_read_record(
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

/*
 * Publish one complete immutable record only if relative_path is confirmed
 * absent. Ambiguous lookup/backend failures fail closed. Data is staged under
 * a hidden same-directory name, flushed, then published by the filesystem's
 * recoverable same-directory rename transaction.
 */
enum aurora_protected_state_create_once_result
protected_state_create_record_once_durable(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
);

/*
 * Publish a complete mutable record for redundancy protocols such as the
 * Aurora Identity dual-slot database. New bytes are written to a hidden
 * same-directory staging file and fully flushed before publication. If a
 * target already exists, AuroraFS v2 journals the namespace replacement and
 * superseded inode cleanup as one recoverable transaction. After crash/reboot
 * the target therefore resolves to either the previous complete record or the
 * new complete record, never a partial record and never an absence introduced
 * by replacement. Ambiguous lookup or unsupported filesystem semantics fail
 * closed.
 */
enum aurora_protected_state_replace_result
protected_state_replace_record_durable(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
);

/* Diagnostic stage of latest durable replacement failure (boot tests only). */
uint32_t protected_state_replace_debug_stage(void);
bool protected_state_self_test(void);

#endif
