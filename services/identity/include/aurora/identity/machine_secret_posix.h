#ifndef AURORA_IDENTITY_MACHINE_SECRET_POSIX_H
#define AURORA_IDENTITY_MACHINE_SECRET_POSIX_H

#include "aurora/identity/machine_secret.h"

#include <stdbool.h>

#define AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX 512u

struct aurora_identity_machine_secret_posix_store {
    char directory[AURORA_IDENTITY_MACHINE_SECRET_POSIX_PATH_MAX];
    bool initialized;
};

bool aurora_identity_machine_secret_posix_store_init(
    struct aurora_identity_machine_secret_posix_store *store,
    const char *directory);

struct aurora_identity_machine_secret_store_ops
    aurora_identity_machine_secret_posix_store_ops(
        struct aurora_identity_machine_secret_posix_store *store);

#endif
