#ifndef AURORA_SESSION_PROFILE_LEASE_H
#define AURORA_SESSION_PROFILE_LEASE_H

#include <stdbool.h>
#include <stdint.h>

#include <aurora/capability.h>
#include <aurora/process.h>

#define AURORA_SESSION_PROFILE_LEASE_MAX 16u

bool session_profile_lease_begin(
    struct aurora_cap_table *source_table,
    aurora_cap_handle source_handle,
    const uint8_t user_id[AURORA_PROFILE_USER_ID_SIZE],
    uint64_t session_generation
);

aurora_cap_handle session_profile_lease_delegate(
    struct aurora_process *process,
    uint64_t rights
);

bool session_profile_lease_revoke_process(
    struct aurora_process *process
);

void session_profile_lease_end(void);

bool session_profile_lease_active(void);

uint32_t session_profile_lease_count(void);

#endif
