#ifndef AURORA_IDENTITY_PERSISTENT_STORE_POSIX_H
#define AURORA_IDENTITY_PERSISTENT_STORE_POSIX_H

#include "aurora/identity/persistent_store.h"

#include <stdbool.h>

#define AURORA_IDENTITY_POSIX_BASE_PATH_MAX 512u

struct aurora_identity_posix_store {
    char base_path[AURORA_IDENTITY_POSIX_BASE_PATH_MAX];
};

bool aurora_identity_posix_store_init(
    struct aurora_identity_posix_store *store,
    const char *base_path);

struct aurora_identity_persistent_io_ops aurora_identity_posix_store_io(
    struct aurora_identity_posix_store *store);

#endif
