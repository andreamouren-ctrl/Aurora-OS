#ifndef AURORA_CAPABILITY_H
#define AURORA_CAPABILITY_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/spinlock.h>

#define AURORA_CAPABILITY_SLOTS 256u

typedef uint64_t aurora_cap_handle;

#define AURORA_CAP_INVALID ((aurora_cap_handle)0)

enum aurora_cap_type {
    AURORA_CAP_NONE = 0,
    AURORA_CAP_MEMORY,
    AURORA_CAP_FILE,
    AURORA_CAP_DEVICE,
    AURORA_CAP_IPC_ENDPOINT,
    AURORA_CAP_PROCESS,
    AURORA_CAP_THREAD,
    AURORA_CAP_NETWORK,
    AURORA_CAP_MICROPHONE,
    AURORA_CAP_CAMERA,
    AURORA_CAP_DISPLAY,
    AURORA_CAP_CLIPBOARD,
    AURORA_CAP_LOCATION,
    AURORA_CAP_NOTIFICATION,
    AURORA_CAP_SYSTEM
};

enum aurora_cap_right {
    AURORA_RIGHT_READ       = 1ull << 0,
    AURORA_RIGHT_WRITE      = 1ull << 1,
    AURORA_RIGHT_EXECUTE    = 1ull << 2,
    AURORA_RIGHT_MAP        = 1ull << 3,
    AURORA_RIGHT_CONTROL    = 1ull << 4,
    AURORA_RIGHT_TRANSFER   = 1ull << 5,
    AURORA_RIGHT_CONNECT    = 1ull << 6,
    AURORA_RIGHT_LISTEN     = 1ull << 7,
    AURORA_RIGHT_ENUMERATE  = 1ull << 8,
    AURORA_RIGHT_BACKGROUND = 1ull << 9,
    AURORA_RIGHT_NOTIFY     = 1ull << 10,
    AURORA_RIGHT_DEVICE_IO  = 1ull << 11
};

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

#endif
