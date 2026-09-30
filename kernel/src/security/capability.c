#include <stddef.h>
#include <stdint.h>

#include <aurora/capability.h>

static aurora_cap_handle make_handle(
    uint32_t slot,
    uint32_t generation
) {
    return
        ((uint64_t)generation << 32) |
        ((uint64_t)slot + 1ull);
}

static bool decode_handle(
    aurora_cap_handle handle,
    uint32_t *slot,
    uint32_t *generation
) {
    if (handle == AURORA_CAP_INVALID ||
        slot == NULL ||
        generation == NULL) {
        return false;
    }

    uint32_t low =
        (uint32_t)handle;

    if (low == 0) {
        return false;
    }

    uint32_t decoded_slot =
        low - 1u;

    if (decoded_slot >=
        AURORA_CAPABILITY_SLOTS) {
        return false;
    }

    uint32_t decoded_generation =
        (uint32_t)(handle >> 32);

    if (decoded_generation == 0) {
        return false;
    }

    *slot = decoded_slot;
    *generation = decoded_generation;

    return true;
}

static uint32_t next_generation(
    uint32_t generation
) {
    ++generation;

    if (generation == 0) {
        generation = 1;
    }

    return generation;
}

void cap_table_init(
    struct aurora_cap_table *table
) {
    if (table == NULL) {
        return;
    }

    spinlock_init(
        &table->lock
    );

    for (uint32_t i = 0;
         i < AURORA_CAPABILITY_SLOTS;
         ++i) {
        struct aurora_cap_entry *entry =
            &table->entries[i];

        entry->object = NULL;
        entry->rights = 0;
        entry->generation = 1;
        entry->type = AURORA_CAP_NONE;
        entry->occupied = false;
    }
}

aurora_cap_handle cap_grant(
    struct aurora_cap_table *table,
    void *object,
    enum aurora_cap_type type,
    uint64_t rights
) {
    if (table == NULL ||
        object == NULL ||
        type == AURORA_CAP_NONE) {
        return AURORA_CAP_INVALID;
    }

    spinlock_lock(
        &table->lock
    );

    for (uint32_t i = 0;
         i < AURORA_CAPABILITY_SLOTS;
         ++i) {
        struct aurora_cap_entry *entry =
            &table->entries[i];

        if (entry->occupied) {
            continue;
        }

        if (entry->generation == 0) {
            entry->generation = 1;
        }

        entry->object = object;
        entry->rights = rights;
        entry->type = (uint16_t)type;
        entry->occupied = true;

        aurora_cap_handle handle =
            make_handle(
                i,
                entry->generation
            );

        spinlock_unlock(
            &table->lock
        );

        return handle;
    }

    spinlock_unlock(
        &table->lock
    );

    return AURORA_CAP_INVALID;
}

bool cap_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    enum aurora_cap_type expected_type,
    uint64_t required_rights,
    struct aurora_capability_view *out
) {
    if (table == NULL ||
        out == NULL) {
        return false;
    }

    uint32_t slot;
    uint32_t generation;

    if (!decode_handle(
            handle,
            &slot,
            &generation)) {
        return false;
    }

    spinlock_lock(
        &table->lock
    );

    struct aurora_cap_entry *entry =
        &table->entries[slot];

    bool valid =
        entry->occupied &&
        entry->generation == generation &&
        entry->object != NULL &&
        (expected_type == AURORA_CAP_NONE ||
         entry->type == (uint16_t)expected_type) &&
        (entry->rights & required_rights) ==
            required_rights;

    if (valid) {
        out->object = entry->object;
        out->type =
            (enum aurora_cap_type)
                entry->type;
        out->rights = entry->rights;
    }

    spinlock_unlock(
        &table->lock
    );

    return valid;
}

bool cap_revoke(
    struct aurora_cap_table *table,
    aurora_cap_handle handle
) {
    if (table == NULL) {
        return false;
    }

    uint32_t slot;
    uint32_t generation;

    if (!decode_handle(
            handle,
            &slot,
            &generation)) {
        return false;
    }

    spinlock_lock(
        &table->lock
    );

    struct aurora_cap_entry *entry =
        &table->entries[slot];

    if (!entry->occupied ||
        entry->generation != generation) {
        spinlock_unlock(
            &table->lock
        );

        return false;
    }

    entry->occupied = false;
    entry->object = NULL;
    entry->rights = 0;
    entry->type = AURORA_CAP_NONE;

    entry->generation =
        next_generation(
            entry->generation
        );

    spinlock_unlock(
        &table->lock
    );

    return true;
}

bool cap_reduce_rights(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t new_rights
) {
    if (table == NULL) {
        return false;
    }

    uint32_t slot;
    uint32_t generation;

    if (!decode_handle(
            handle,
            &slot,
            &generation)) {
        return false;
    }

    spinlock_lock(
        &table->lock
    );

    struct aurora_cap_entry *entry =
        &table->entries[slot];

    if (!entry->occupied ||
        entry->generation != generation ||
        (new_rights & ~entry->rights) != 0) {
        spinlock_unlock(
            &table->lock
        );

        return false;
    }

    entry->rights = new_rights;

    spinlock_unlock(
        &table->lock
    );

    return true;
}

aurora_cap_handle cap_delegate(
    struct aurora_cap_table *source_table,
    aurora_cap_handle source_handle,
    struct aurora_cap_table *target_table,
    uint64_t delegated_rights
) {
    if (source_table == NULL ||
        target_table == NULL) {
        return AURORA_CAP_INVALID;
    }

    uint32_t source_slot;
    uint32_t source_generation;

    if (!decode_handle(
            source_handle,
            &source_slot,
            &source_generation)) {
        return AURORA_CAP_INVALID;
    }

    /*
     * Delegation must validate the source and create the target capability
     * atomically. Using cap_lookup() followed by cap_grant() would leave a
     * TOCTOU window where another CPU could revoke or reduce the source.
     *
     * Cap tables are locked in address order so delegation between two
     * tables cannot deadlock with another cross-table delegation.
     */
    struct aurora_cap_table *first =
        source_table;
    struct aurora_cap_table *second =
        target_table;

    if (source_table != target_table &&
        (uintptr_t)first >
            (uintptr_t)second) {
        first = target_table;
        second = source_table;
    }

    spinlock_lock(&first->lock);

    if (second != first) {
        spinlock_lock(&second->lock);
    }

    struct aurora_cap_entry *source =
        &source_table->entries[source_slot];

    if (!source->occupied ||
        source->generation !=
            source_generation ||
        source->object == NULL ||
        (source->rights &
         AURORA_RIGHT_TRANSFER) == 0 ||
        (delegated_rights &
         ~source->rights) != 0) {
        if (second != first) {
            spinlock_unlock(&second->lock);
        }

        spinlock_unlock(&first->lock);
        return AURORA_CAP_INVALID;
    }

    aurora_cap_handle result =
        AURORA_CAP_INVALID;

    for (uint32_t i = 0;
         i < AURORA_CAPABILITY_SLOTS;
         ++i) {
        struct aurora_cap_entry *target =
            &target_table->entries[i];

        if (target->occupied) {
            continue;
        }

        if (target->generation == 0) {
            target->generation = 1;
        }

        target->object =
            source->object;

        target->rights =
            delegated_rights;

        target->type =
            source->type;

        target->occupied = true;

        result =
            make_handle(
                i,
                target->generation
            );

        break;
    }

    if (second != first) {
        spinlock_unlock(&second->lock);
    }

    spinlock_unlock(&first->lock);
    return result;
}


bool capability_self_test(void) {
    /*
     * Keep the large bootstrap test tables out of the bootloader-provided
     * stack. They are reset on every invocation, so static storage is safe.
     */
    static struct aurora_cap_table source;
    static struct aurora_cap_table target;
    static uint64_t dummy_device;

    dummy_device = 0xA11CEu;

    cap_table_init(&source);
    cap_table_init(&target);

    aurora_cap_handle source_handle =
        cap_grant(
            &source,
            &dummy_device,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_CONTROL |
            AURORA_RIGHT_TRANSFER
        );

    if (source_handle == AURORA_CAP_INVALID) {
        return false;
    }

    struct aurora_capability_view view;

    if (!cap_lookup(
            &source,
            source_handle,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ,
            &view)) {
        return false;
    }

    if (cap_lookup(
            &source,
            source_handle,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_WRITE,
            &view)) {
        return false;
    }

    aurora_cap_handle delegated =
        cap_delegate(
            &source,
            source_handle,
            &target,
            AURORA_RIGHT_READ
        );

    if (delegated == AURORA_CAP_INVALID) {
        return false;
    }

    if (!cap_lookup(
            &target,
            delegated,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ,
            &view)) {
        return false;
    }

    if (cap_lookup(
            &target,
            delegated,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_CONTROL,
            &view)) {
        return false;
    }

    if (cap_delegate(
            &source,
            source_handle,
            &target,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE) !=
        AURORA_CAP_INVALID) {
        return false;
    }

    if (!cap_revoke(
            &source,
            source_handle)) {
        return false;
    }

    if (cap_lookup(
            &source,
            source_handle,
            AURORA_CAP_DEVICE,
            0,
            &view)) {
        return false;
    }

    aurora_cap_handle replacement =
        cap_grant(
            &source,
            &dummy_device,
            AURORA_CAP_DEVICE,
            AURORA_RIGHT_READ
        );

    if (replacement == AURORA_CAP_INVALID ||
        replacement == source_handle) {
        return false;
    }

    return true;
}
