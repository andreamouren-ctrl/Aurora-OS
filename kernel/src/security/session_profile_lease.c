#include <stddef.h>
#include <stdint.h>

#include <aurora/profile_session.h>
#include <aurora/session_profile_lease.h>
#include <aurora/spinlock.h>

struct session_profile_lease_entry {
    bool occupied;
    struct aurora_process *process;
    aurora_cap_handle handle;
};

static aurora_spinlock lease_lock;
static bool initialized;
static bool active;
static struct aurora_cap_table *source_table;
static aurora_cap_handle source_handle;
static uint64_t active_generation;
static uint8_t active_user_id[AURORA_PROFILE_USER_ID_SIZE];
static struct session_profile_lease_entry leases[AURORA_SESSION_PROFILE_LEASE_MAX];

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static void ensure_initialized(void) {
    if (initialized) return;
    spinlock_init(&lease_lock);
    clear_bytes(active_user_id, sizeof(active_user_id));
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        leases[i].occupied = false;
        leases[i].process = NULL;
        leases[i].handle = AURORA_CAP_INVALID;
    }
    initialized = true;
}

static bool user_id_valid(
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]
) {
    if (user_id == NULL) return false;
    uint8_t combined = 0u;
    for (size_t i = 0u; i < AURORA_PROFILE_USER_ID_SIZE; ++i) {
        combined |= user_id[i];
    }
    return combined != 0u;
}

static bool rights_valid(uint64_t rights) {
    const uint64_t allowed =
        AURORA_RIGHT_READ |
        AURORA_RIGHT_WRITE |
        AURORA_RIGHT_ENUMERATE;

    return rights != 0u &&
        (rights & ~allowed) == 0u &&
        (rights & AURORA_RIGHT_TRANSFER) == 0u &&
        (rights & AURORA_RIGHT_CONTROL) == 0u;
}

bool session_profile_lease_begin(
    struct aurora_cap_table *new_source_table,
    aurora_cap_handle new_source_handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE],
    uint64_t session_generation
) {
    ensure_initialized();

    if (new_source_table == NULL ||
        new_source_handle == AURORA_CAP_INVALID ||
        session_generation == 0u ||
        !user_id_valid(user_id) ||
        !profile_capability_matches_user(
            new_source_table,
            new_source_handle,
            user_id)) {
        return false;
    }

    struct aurora_capability_view view;
    if (!cap_lookup(
            new_source_table,
            new_source_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE |
            AURORA_RIGHT_TRANSFER,
            &view)) {
        return false;
    }

    spinlock_lock(&lease_lock);
    if (active) {
        spinlock_unlock(&lease_lock);
        return false;
    }

    source_table = new_source_table;
    source_handle = new_source_handle;
    active_generation = session_generation;
    for (size_t i = 0u; i < AURORA_PROFILE_USER_ID_SIZE; ++i) {
        active_user_id[i] = user_id[i];
    }
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        leases[i].occupied = false;
        leases[i].process = NULL;
        leases[i].handle = AURORA_CAP_INVALID;
    }
    active = true;
    spinlock_unlock(&lease_lock);
    return true;
}

aurora_cap_handle session_profile_lease_delegate(
    struct aurora_process *process,
    uint64_t rights
) {
    ensure_initialized();

    if (process == NULL ||
        process_state(process) != AURORA_PROCESS_RUNNING ||
        !rights_valid(rights)) {
        return AURORA_CAP_INVALID;
    }

    spinlock_lock(&lease_lock);

    if (!active ||
        source_table == NULL ||
        source_handle == AURORA_CAP_INVALID ||
        active_generation == 0u) {
        spinlock_unlock(&lease_lock);
        return AURORA_CAP_INVALID;
    }

    size_t free_index = AURORA_SESSION_PROFILE_LEASE_MAX;
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        if (!leases[i].occupied && free_index == AURORA_SESSION_PROFILE_LEASE_MAX) {
            free_index = i;
        }
        if (leases[i].occupied && leases[i].process == process) {
            spinlock_unlock(&lease_lock);
            return AURORA_CAP_INVALID;
        }
    }

    if (free_index == AURORA_SESSION_PROFILE_LEASE_MAX) {
        spinlock_unlock(&lease_lock);
        return AURORA_CAP_INVALID;
    }

    aurora_cap_handle delegated = cap_delegate(
        source_table,
        source_handle,
        &process->capabilities,
        rights
    );

    if (delegated == AURORA_CAP_INVALID ||
        !profile_capability_matches_user(
            &process->capabilities,
            delegated,
            active_user_id)) {
        if (delegated != AURORA_CAP_INVALID) {
            (void)cap_revoke(&process->capabilities, delegated);
        }
        spinlock_unlock(&lease_lock);
        return AURORA_CAP_INVALID;
    }

    leases[free_index].occupied = true;
    leases[free_index].process = process;
    leases[free_index].handle = delegated;

    spinlock_unlock(&lease_lock);
    return delegated;
}

bool session_profile_lease_revoke_process(
    struct aurora_process *process
) {
    ensure_initialized();
    if (process == NULL) return false;

    bool found = false;
    bool revoked = true;

    spinlock_lock(&lease_lock);
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        if (!leases[i].occupied || leases[i].process != process) continue;

        found = true;
        if (leases[i].handle != AURORA_CAP_INVALID &&
            !cap_revoke(&process->capabilities, leases[i].handle)) {
            revoked = false;
        }

        leases[i].occupied = false;
        leases[i].process = NULL;
        leases[i].handle = AURORA_CAP_INVALID;
    }
    spinlock_unlock(&lease_lock);

    return found && revoked;
}

void session_profile_lease_end(void) {
    ensure_initialized();

    spinlock_lock(&lease_lock);
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        if (leases[i].occupied &&
            leases[i].process != NULL &&
            leases[i].handle != AURORA_CAP_INVALID) {
            (void)cap_revoke(
                &leases[i].process->capabilities,
                leases[i].handle);
        }

        leases[i].occupied = false;
        leases[i].process = NULL;
        leases[i].handle = AURORA_CAP_INVALID;
    }

    active = false;
    source_table = NULL;
    source_handle = AURORA_CAP_INVALID;
    active_generation = 0u;
    clear_bytes(active_user_id, sizeof(active_user_id));
    spinlock_unlock(&lease_lock);
}

bool session_profile_lease_active(void) {
    ensure_initialized();
    spinlock_lock(&lease_lock);
    bool value = active;
    spinlock_unlock(&lease_lock);
    return value;
}

uint32_t session_profile_lease_count(void) {
    ensure_initialized();
    uint32_t count = 0u;
    spinlock_lock(&lease_lock);
    for (size_t i = 0u; i < AURORA_SESSION_PROFILE_LEASE_MAX; ++i) {
        if (leases[i].occupied) ++count;
    }
    spinlock_unlock(&lease_lock);
    return count;
}
