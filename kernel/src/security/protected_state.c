#include <stddef.h>
#include <stdint.h>

#include <aurora/protected_state.h>

#define PROTECTED_STATE_DIRECTORY_MODE 0700u
#define PROTECTED_STATE_FILE_MODE      0600u

static void protected_zero(void *buffer, size_t size) {
    uint8_t *bytes = buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static size_t protected_length(const char *text) {
    size_t length = 0u;
    if (text == NULL) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static bool scope_valid(const char *scope) {
    size_t length = protected_length(scope);
    if (length == 0u || length > AURORA_PROTECTED_STATE_SCOPE_MAX) return false;

    for (size_t i = 0u; i < length; ++i) {
        char ch = scope[i];
        bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '-' && ch != '_') return false;
    }

    return true;
}

static bool relative_name_valid(const char *name) {
    size_t length = protected_length(name);
    if (length == 0u || length >= AURORA_VFS_PATH_MAX) return false;
    if ((length == 1u && name[0] == '.') ||
        (length == 2u && name[0] == '.' && name[1] == '.')) return false;

    for (size_t i = 0u; i < length; ++i) {
        char ch = name[i];
        if (ch == '/' || ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') return false;
        if ((unsigned char)ch < 0x20u || (unsigned char)ch == 0x7Fu) return false;
    }

    return true;
}

static bool build_root(
    struct aurora_protected_state_namespace *state,
    const char *scope
) {
    static const char prefix[] = AURORA_PROTECTED_STATE_PREFIX;
    size_t prefix_length = sizeof(prefix) - 1u;
    size_t scope_length = protected_length(scope);
    size_t total = prefix_length + 1u + scope_length;

    if (total >= sizeof(state->root)) return false;

    for (size_t i = 0u; i < prefix_length; ++i) state->root[i] = prefix[i];
    state->root[prefix_length] = '/';
    for (size_t i = 0u; i < scope_length; ++i)
        state->root[prefix_length + 1u + i] = scope[i];
    state->root[total] = '\0';
    return true;
}

static bool build_path(
    const struct aurora_protected_state_namespace *state,
    const char *relative_path,
    char out[AURORA_VFS_PATH_MAX]
) {
    if (state == NULL || !state->initialized || !relative_name_valid(relative_path)) return false;

    size_t root_length = protected_length(state->root);
    size_t relative_length = protected_length(relative_path);
    size_t total = root_length + 1u + relative_length;
    if (total >= AURORA_VFS_PATH_MAX) return false;

    for (size_t i = 0u; i < root_length; ++i) out[i] = state->root[i];
    out[root_length] = '/';
    for (size_t i = 0u; i < relative_length; ++i)
        out[root_length + 1u + i] = relative_path[i];
    out[total] = '\0';
    return true;
}

static bool ensure_directory(const char *path) {
    struct aurora_vfs_stat stat;
    enum aurora_vfs_lookup_result result = vfs_stat_result(path, &stat);

    if (result == AURORA_VFS_LOOKUP_FOUND) {
        if (stat.type != AURORA_VFS_NODE_DIRECTORY) return false;
    } else if (result == AURORA_VFS_LOOKUP_NOT_FOUND) {
        if (!vfs_create_directory(path)) return false;
    } else {
        return false;
    }

    return vfs_chown(path, 0u, 0u) &&
        vfs_chmod(path, PROTECTED_STATE_DIRECTORY_MODE);
}

static bool authorize(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    uint64_t required_rights
) {
    struct aurora_capability_view view;

    if (table == NULL || state == NULL || !state->initialized) return false;
    if (!cap_lookup(
            table,
            handle,
            AURORA_CAP_PROTECTED_STATE,
            required_rights,
            &view)) {
        return false;
    }

    return view.object == state;
}

bool protected_state_namespace_init(
    struct aurora_protected_state_namespace *state,
    const char *scope
) {
    if (state == NULL || !scope_valid(scope)) return false;

    protected_zero(state, sizeof(*state));
    size_t scope_length = protected_length(scope);
    for (size_t i = 0u; i <= scope_length; ++i) state->scope[i] = scope[i];

    if (!build_root(state, scope)) {
        protected_zero(state, sizeof(*state));
        return false;
    }

    state->initialized = true;
    return true;
}

bool protected_state_prepare(
    struct aurora_protected_state_namespace *state
) {
    if (state == NULL || !state->initialized) return false;

    if (!ensure_directory(AURORA_PROTECTED_STATE_PREFIX) ||
        !ensure_directory(state->root)) {
        return false;
    }

    return vfs_sync("/system");
}

aurora_cap_handle protected_state_grant(
    struct aurora_cap_table *table,
    struct aurora_protected_state_namespace *state,
    uint64_t rights
) {
    if (table == NULL || state == NULL || !state->initialized ||
        rights == 0u || (rights & ~AURORA_PROTECTED_STATE_RIGHTS) != 0u) {
        return AURORA_CAP_INVALID;
    }

    return cap_grant(table, state, AURORA_CAP_PROTECTED_STATE, rights);
}

bool protected_state_create_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
) {
    char path[AURORA_VFS_PATH_MAX];

    if (!authorize(table, handle, state, AURORA_RIGHT_WRITE) ||
        !build_path(state, relative_path, path) ||
        !vfs_create_file(path)) {
        return false;
    }

    if (!vfs_chown(path, 0u, 0u) ||
        !vfs_chmod(path, PROTECTED_STATE_FILE_MODE) ||
        !vfs_fsync(path)) {
        (void)vfs_remove(path);
        return false;
    }

    return true;
}

bool protected_state_read_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    void *buffer,
    size_t capacity,
    size_t *out_length
) {
    char path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_READ) ||
        !build_path(state, relative_path, path)) return false;
    return vfs_read_file(path, buffer, capacity, out_length);
}

bool protected_state_write_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    const void *data,
    size_t length
) {
    char path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_WRITE) ||
        !build_path(state, relative_path, path) ||
        !vfs_write_file(path, data, length)) return false;
    return vfs_fdatasync(path);
}

bool protected_state_truncate_file(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path,
    uint64_t size
) {
    char path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_WRITE) ||
        !build_path(state, relative_path, path) ||
        !vfs_truncate_file(path, size)) return false;
    return vfs_fsync(path);
}

bool protected_state_remove(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
) {
    char path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_CONTROL) ||
        !build_path(state, relative_path, path) ||
        !vfs_remove(path)) return false;
    return vfs_sync(state->root);
}

bool protected_state_rename(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *old_relative_path,
    const char *new_relative_path
) {
    char old_path[AURORA_VFS_PATH_MAX];
    char new_path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_CONTROL) ||
        !build_path(state, old_relative_path, old_path) ||
        !build_path(state, new_relative_path, new_path) ||
        !vfs_rename(old_path, new_path)) return false;
    return vfs_sync(state->root);
}

bool protected_state_fsync(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    struct aurora_protected_state_namespace *state,
    const char *relative_path
) {
    char path[AURORA_VFS_PATH_MAX];
    if (!authorize(table, handle, state, AURORA_RIGHT_WRITE) ||
        !build_path(state, relative_path, path)) return false;
    return vfs_fsync(path);
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right, size_t length) {
    for (size_t i = 0u; i < length; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

bool protected_state_self_test(void) {
    static struct aurora_protected_state_namespace state;
    static struct aurora_protected_state_namespace other_state;
    static struct aurora_cap_table full_table;
    static struct aurora_cap_table read_table;
    static struct aurora_cap_table other_table;
    static const uint8_t payload[] = { 0x41u, 0x55u, 0x52u, 0x50u, 0x53u, 0x53u };
    uint8_t readback[sizeof(payload)];
    size_t read_length = 0u;

    if (!protected_state_namespace_init(&state, "selftest") ||
        !protected_state_namespace_init(&other_state, "other") ||
        !protected_state_prepare(&state)) return false;

    cap_table_init(&full_table);
    cap_table_init(&read_table);
    cap_table_init(&other_table);

    aurora_cap_handle full = protected_state_grant(
        &full_table,
        &state,
        AURORA_RIGHT_READ | AURORA_RIGHT_WRITE | AURORA_RIGHT_CONTROL);
    aurora_cap_handle read_only = protected_state_grant(
        &read_table,
        &state,
        AURORA_RIGHT_READ);
    aurora_cap_handle wrong_namespace = protected_state_grant(
        &other_table,
        &other_state,
        AURORA_RIGHT_READ);

    if (full == AURORA_CAP_INVALID || read_only == AURORA_CAP_INVALID ||
        wrong_namespace == AURORA_CAP_INVALID) return false;

    /* Clean a stale probe from an interrupted prior boot, then start fresh. */
    (void)protected_state_remove(&full_table, full, &state, "probe-renamed");
    (void)protected_state_remove(&full_table, full, &state, "probe");

    if (!protected_state_create_file(&full_table, full, &state, "probe") ||
        !protected_state_write_file(
            &full_table, full, &state, "probe", payload, sizeof(payload)) ||
        !protected_state_read_file(
            &read_table, read_only, &state, "probe",
            readback, sizeof(readback), &read_length) ||
        read_length != sizeof(payload) ||
        !bytes_equal(readback, payload, sizeof(payload))) {
        return false;
    }

    if (protected_state_write_file(
            &read_table, read_only, &state, "probe", payload, sizeof(payload))) {
        return false;
    }

    if (protected_state_read_file(
            &other_table, wrong_namespace, &state, "probe",
            readback, sizeof(readback), &read_length)) {
        return false;
    }

    if (protected_state_read_file(
            &full_table, AURORA_CAP_INVALID, &state, "probe",
            readback, sizeof(readback), &read_length)) {
        return false;
    }

    if (protected_state_create_file(&full_table, full, &state, "../escape") ||
        protected_state_create_file(&full_table, full, &state, "/absolute")) {
        return false;
    }

    if (!protected_state_rename(
            &full_table, full, &state, "probe", "probe-renamed") ||
        !protected_state_truncate_file(
            &full_table, full, &state, "probe-renamed", 3u) ||
        !protected_state_fsync(
            &full_table, full, &state, "probe-renamed")) {
        return false;
    }

    if (!protected_state_remove(
            &full_table, full, &state, "probe-renamed")) return false;

    if (!cap_revoke(&full_table, full)) return false;
    if (protected_state_create_file(&full_table, full, &state, "revoked")) return false;

    /* Remove only the self-test namespace; the canonical parent remains. */
    if (!vfs_remove(state.root) || !vfs_sync("/system")) return false;
    return true;
}
