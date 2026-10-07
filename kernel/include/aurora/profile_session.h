#ifndef AURORA_PROFILE_SESSION_H
#define AURORA_PROFILE_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/process.h>

#define AURORA_PROFILE_USER_ID_SIZE 16u
#define AURORA_PROFILE_MAX 32u
#define AURORA_PROFILE_PATH_MAX 80u

struct aurora_profile_root_authority {
    uint64_t marker;
};

struct aurora_profile_object {
    bool occupied;
    uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE];
    char path[AURORA_PROFILE_PATH_MAX];
};

struct aurora_profile_root_authority *profile_root_authority(void);

aurora_cap_handle profile_open_or_create(
    struct aurora_process *process,
    aurora_cap_handle root_handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]
);

bool profile_capability_matches_user(
    struct aurora_cap_table *table,
    aurora_cap_handle handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE]
);

#endif
