#ifndef AURORA_CAPABILITY_H
#define AURORA_CAPABILITY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/spinlock.h>

#define AURORA_CAPABILITY_SLOTS 256u

struct aurora_capability_view {
    void *object;
    enum aurora_cap_type type;
    uint64_t rights;
};

struct aurora_cap_entry {
    void *object;
    uint64_t rights;
    uint32_t generation;
    uint16_t type;
    bool occupied;
};

struct aurora_cap_table {
    aurora_spinlock lock;

    struct aurora_cap_entry entries[
        AURORA_CAPABILITY_SLOTS
    ];
};

void cap_table_init(
    struct aurora_cap_table *table
);

aurora_cap_handle cap_grant(
    struct aurora_cap_table *table,
    void *object,
    enum aurora_cap_type type,
    uint64_t rights
);

bool cap_lookup(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    enum aurora_cap_type expected_type,
    uint64_t required_rights,
    struct aurora_capability_view *out
);

bool cap_revoke(
    struct aurora_cap_table *table,
    aurora_cap_handle handle
);

bool cap_reduce_rights(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    uint64_t new_rights
);

aurora_cap_handle cap_delegate(
    struct aurora_cap_table *source_table,
    aurora_cap_handle source_handle,
    struct aurora_cap_table *target_table,
    uint64_t delegated_rights
);

bool capability_self_test(void);

#endif
