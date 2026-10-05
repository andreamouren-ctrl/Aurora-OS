#ifndef AURORA_IDENTITY_MACHINE_SECRET_H
#define AURORA_IDENTITY_MACHINE_SECRET_H

#include "aurora/identity/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_IDENTITY_MACHINE_SECRET_SIZE 32u
#define AURORA_IDENTITY_MACHINE_SECRET_CHECKSUM_SIZE 32u
#define AURORA_IDENTITY_MACHINE_SECRET_REPLICA_COUNT 2u
#define AURORA_IDENTITY_MACHINE_SECRET_RECORD_VERSION 1u
#define AURORA_IDENTITY_MACHINE_SECRET_GENERATION_ATTEMPTS 4u

enum aurora_identity_machine_secret_result {
    AURORA_IDENTITY_MACHINE_SECRET_OK = 0,
    AURORA_IDENTITY_MACHINE_SECRET_INVALID_ARGUMENT,
    AURORA_IDENTITY_MACHINE_SECRET_NOT_PROVISIONED,
    AURORA_IDENTITY_MACHINE_SECRET_CORRUPT,
    AURORA_IDENTITY_MACHINE_SECRET_CONFLICT,
    AURORA_IDENTITY_MACHINE_SECRET_RANDOM_ERROR,
    AURORA_IDENTITY_MACHINE_SECRET_BACKEND_ERROR
};

enum aurora_identity_machine_secret_load_result {
    AURORA_IDENTITY_MACHINE_SECRET_LOAD_OK = 0,
    AURORA_IDENTITY_MACHINE_SECRET_LOAD_ABSENT,
    AURORA_IDENTITY_MACHINE_SECRET_LOAD_ERROR
};

enum aurora_identity_machine_secret_publish_result {
    AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_OK = 0,
    AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_CONFLICT,
    AURORA_IDENTITY_MACHINE_SECRET_PUBLISH_ERROR
};

struct aurora_identity_machine_secret {
    uint8_t bytes[AURORA_IDENTITY_MACHINE_SECRET_SIZE];
};

struct aurora_identity_machine_secret_record {
    uint32_t record_version;
    uint64_t generation;
    struct aurora_identity_machine_secret secret;
    uint8_t checksum[AURORA_IDENTITY_MACHINE_SECRET_CHECKSUM_SIZE];
};

struct aurora_identity_machine_secret_store_ops {
    void *context;

    enum aurora_identity_machine_secret_load_result (*load_replica)(
        void *context,
        uint32_t replica_index,
        struct aurora_identity_machine_secret_record *out_record);

    /*
     * Publish is create-once. It must never replace an existing replica.
     * CONFLICT means another publisher already created that replica.
     */
    enum aurora_identity_machine_secret_publish_result (*publish_replica)(
        void *context,
        uint32_t replica_index,
        const struct aurora_identity_machine_secret_record *record);
};

struct aurora_identity_machine_secret_core {
    struct aurora_identity_random_ops random;
    struct aurora_identity_machine_secret_store_ops store;
};

/* Load only. Never generates or repairs persistent state. */
enum aurora_identity_machine_secret_result aurora_identity_machine_secret_load(
    const struct aurora_identity_machine_secret_store_ops *store,
    struct aurora_identity_machine_secret *out_secret);

/*
 * Load an existing secret or provision the first one only when every replica
 * is absent. Corrupt/conflicting state is never replaced automatically.
 */
enum aurora_identity_machine_secret_result
    aurora_identity_machine_secret_load_or_provision(
        const struct aurora_identity_machine_secret_core *core,
        struct aurora_identity_machine_secret *out_secret);

/* Derive the stable Aurora Key lookup-HMAC key from the machine root secret. */
bool aurora_identity_machine_secret_derive_lookup_key(
    const struct aurora_identity_machine_secret *secret,
    uint8_t out_lookup_key[32u]);

bool aurora_identity_machine_secret_is_zero(
    const struct aurora_identity_machine_secret *secret);

void aurora_identity_machine_secret_clear(
    struct aurora_identity_machine_secret *secret);

#endif
