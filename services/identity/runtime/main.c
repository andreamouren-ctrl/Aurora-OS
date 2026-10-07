#include "protected_state_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/identity/argon2id_provider.h>
#include <aurora/identity/core.h>
#include <aurora/identity/crypto_foundation.h>
#include <aurora/identity/crypto_provider.h>
#include <aurora/identity/machine_secret.h>
#include <aurora/identity/machine_secret_protected_state.h>
#include <aurora/identity/persistent_store.h>
#include <aurora/identity/persistent_store_protected_state.h>
#include <aurora/identity/session_grant.h>
#include <aurora/identity/session_grant_memory.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/service_abi.h>
#include <aurora/syscall_abi.h>

#define IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE 32u
#define IDENTITY_RUNTIME_DRBG_NONCE_SIZE 16u
#define IDENTITY_RUNTIME_MEMORY_PROBE_SIZE (1024u * 1024u)
#define IDENTITY_RUNTIME_REAP_PROBE_SIZE (256u * 1024u)
#define IDENTITY_RUNTIME_PAGE_SIZE 4096u
#define IDENTITY_RUNTIME_SESSION_GRANT_TTL_MS UINT64_C(30000)

void *malloc(size_t size);
void free(void *pointer);

static const uint8_t identity_runtime_drbg_personalization[] =
    "AURORA.IDENTITY.RUNTIME.HMAC-DRBG.V1";
static const uint8_t identity_runtime_session_grant_key_domain[] =
    "AURORA.IDENTITY.SESSION-GRANT-KEY.V1";

struct identity_runtime_auth_job {
    bool occupied;
    uint64_t request_id;
    char key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
    size_t key_length;
};

struct identity_runtime_create_job {
    bool occupied;
    uint64_t request_id;
    char key[AURORA_IDENTITY_SERVICE_KEY_MAX_LEN];
    size_t key_length;
};

struct identity_runtime_persistent_context {
    struct identity_runtime_protected_state_context transport_context;
    struct aurora_identity_persistent_protected_state_store protected_state_store;
    struct aurora_identity_persistent_store persistent_store;
    struct aurora_identity_machine_secret_protected_state_store machine_secret_store;
    struct aurora_identity_machine_secret machine_secret;
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider hmac_provider;
    struct aurora_identity_argon2id_provider argon2id_provider;
    struct aurora_identity_core identity_core;
    struct aurora_identity_session_grant_memory_store session_grant_store;
    struct aurora_identity_session_grant_core session_grant_core;
    struct identity_runtime_auth_job auth_job;
    struct identity_runtime_create_job create_job;
    bool machine_secret_ready;
    bool drbg_ready;
    bool hmac_provider_ready;
    bool argon2id_provider_ready;
    bool identity_core_ready;
    bool session_grant_ready;
};

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0u; i < size; ++i) dst[i] = src[i];
}

static uint64_t aurora_syscall0(uint64_t number) {
    register uint64_t rax __asm__("rax") = number;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        :
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t aurora_syscall1(uint64_t number, uint64_t a1) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t aurora_syscall2(uint64_t number, uint64_t a1, uint64_t a2) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t aurora_syscall3(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static uint64_t aurora_syscall5(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8 __asm__("r8") = a5;
    __asm__ volatile (
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return rax;
}

static bool capability_has(
    uint64_t handle,
    enum aurora_cap_type type,
    uint64_t rights
) {
    return aurora_syscall3(
        AURORA_SYS_CAP_CHECK,
        handle,
        (uint64_t)type,
        rights
    ) == 1u;
}

static bool revoke_capability(uint64_t handle) {
    return handle != 0u &&
        aurora_syscall1(AURORA_SYS_CAP_REVOKE, handle) == 0u;
}

static void revoke_received_capabilities(
    const struct aurora_sys_ipc_received *received
) {
    if (received == NULL) return;
    for (uint32_t i = 0u;
         i < received->capability_count && i < AURORA_SYS_IPC_CAPS_MAX;
         ++i) {
        (void)revoke_capability(received->capabilities[i]);
    }
}

static bool send_payload(
    uint64_t endpoint,
    const void *payload,
    size_t payload_size
) {
    if (payload == NULL || payload_size == 0u ||
        payload_size > AURORA_SYS_IPC_PAYLOAD_MAX) {
        return false;
    }
    return aurora_syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)payload,
        payload_size,
        0u,
        0u
    ) == 0u;
}

static bool send_message(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id
) {
    struct aurora_identity_service_message message;
    message.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    message.type = type;
    message.request_id = request_id;
    return send_payload(endpoint, &message, sizeof(message));
}

static bool send_auth_result(
    uint64_t endpoint,
    uint64_t request_id,
    uint32_t state,
    uint32_t public_error,
    uint64_t retry_after_ms,
    const uint8_t *session_grant
) {
    struct aurora_identity_service_auth_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    result.header.type = AURORA_IDENTITY_SERVICE_AUTH_RESULT;
    result.header.request_id = request_id;
    result.state = state;
    result.public_error = public_error;
    result.retry_after_ms = retry_after_ms;
    if (session_grant != NULL) {
        copy_bytes(
            result.session_grant,
            session_grant,
            AURORA_IDENTITY_SERVICE_GRANT_TOKEN_SIZE);
    }
    bool sent = send_payload(endpoint, &result, sizeof(result));
    secure_zero(&result, sizeof(result));
    return sent;
}

static bool send_create_result(
    uint64_t endpoint,
    uint64_t request_id,
    uint32_t state,
    uint32_t public_error,
    const struct aurora_identity_user_id *user_id,
    const struct aurora_identity_credential_id *credential_id
) {
    struct aurora_identity_service_create_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    result.header.type = AURORA_IDENTITY_SERVICE_CREATE_RESULT;
    result.header.request_id = request_id;
    result.state = state;
    result.public_error = public_error;
    if (user_id != NULL) {
        copy_bytes(result.user_id, user_id->bytes, sizeof(result.user_id));
    }
    if (credential_id != NULL) {
        copy_bytes(
            result.credential_id,
            credential_id->bytes,
            sizeof(result.credential_id));
    }
    bool sent = send_payload(endpoint, &result, sizeof(result));
    secure_zero(&result, sizeof(result));
    return sent;
}

static bool send_session_grant_result(
    uint64_t endpoint,
    uint64_t request_id,
    uint32_t state,
    uint32_t public_error,
    const struct aurora_identity_user_id *user_id
) {
    struct aurora_identity_service_session_grant_result result;
    secure_zero(&result, sizeof(result));
    result.header.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    result.header.type = AURORA_IDENTITY_SERVICE_SESSION_GRANT_RESULT;
    result.header.request_id = request_id;
    result.state = state;
    result.public_error = public_error;
    if (user_id != NULL) {
        copy_bytes(result.user_id, user_id->bytes, sizeof(result.user_id));
    }
    bool sent = send_payload(endpoint, &result, sizeof(result));
    secure_zero(&result, sizeof(result));
    return sent;
}

static bool receive_message(
    uint64_t endpoint,
    struct aurora_sys_ipc_received *received
) {
    if (received == NULL) return false;
    secure_zero(received, sizeof(*received));
    return aurora_syscall2(
        AURORA_SYS_IPC_RECEIVE,
        endpoint,
        (uint64_t)(uintptr_t)received
    ) == 0u;
}

static bool wait_for_message(uint64_t endpoint) {
    return aurora_syscall2(AURORA_SYS_IPC_WAIT, endpoint, 0u) == 0u;
}

static bool validate_authority(
    const struct aurora_service_startup_block *startup
) {
    if (startup == NULL ||
        startup->ipc_endpoint == 0u ||
        startup->protected_state == 0u ||
        startup->entropy_seed == 0u) {
        return false;
    }

    if (!capability_has(
            startup->ipc_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE)) {
        return false;
    }

    if (!capability_has(
            startup->protected_state,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->protected_state,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_CONTROL) ||
        capability_has(
            startup->protected_state,
            AURORA_CAP_PROTECTED_STATE,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    if (!capability_has(
            startup->entropy_seed,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_READ) ||
        capability_has(
            startup->entropy_seed,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->entropy_seed,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_CONTROL) ||
        capability_has(
            startup->entropy_seed,
            AURORA_CAP_ENTROPY,
            AURORA_RIGHT_TRANSFER)) {
        return false;
    }

    return true;
}

static bool open_persistent_store(
    uint64_t protected_state_handle,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_protected_state_transport_ops transport;
    if (context == NULL) return false;

    secure_zero(&transport, sizeof(transport));
    if (!identity_runtime_protected_state_transport_init(
            &context->transport_context,
            protected_state_handle,
            &transport) ||
        !aurora_identity_persistent_protected_state_store_init(
            &context->protected_state_store,
            &transport) ||
        !aurora_identity_machine_secret_protected_state_store_init(
            &context->machine_secret_store,
            &transport)) {
        secure_zero(&transport, sizeof(transport));
        return false;
    }

    struct aurora_identity_persistent_io_ops io =
        aurora_identity_persistent_protected_state_io_ops(
            &context->protected_state_store);
    enum aurora_identity_persistent_open_result result =
        aurora_identity_persistent_store_open(
            &context->persistent_store,
            &io);

    secure_zero(&io, sizeof(io));
    secure_zero(&transport, sizeof(transport));
    return result == AURORA_IDENTITY_PERSISTENT_OPEN_OK ||
        result == AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY;
}

static bool entropy_fill_random(
    void *context,
    uint8_t *buffer,
    size_t size
) {
    uint64_t entropy_handle = (uint64_t)(uintptr_t)context;
    if (buffer == NULL || size == 0u || size > AURORA_SYS_ENTROPY_SEED_MAX) {
        return false;
    }
    return aurora_syscall3(
        AURORA_SYS_ENTROPY_SEED,
        entropy_handle,
        (uint64_t)(uintptr_t)buffer,
        size
    ) == 0u;
}

static bool runtime_monotonic_ms(void *context, uint64_t *out_now_ms) {
    (void)context;
    if (out_now_ms == NULL) return false;
    *out_now_ms = aurora_syscall0(AURORA_SYS_CLOCK_NS) / UINT64_C(1000000);
    return true;
}

static bool initialize_machine_secret(
    uint64_t entropy_handle,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_machine_secret_store_ops store;
    struct aurora_identity_machine_secret_core core;
    enum aurora_identity_machine_secret_result result;

    if (context == NULL) return false;
    secure_zero(&store, sizeof(store));
    secure_zero(&core, sizeof(core));
    context->machine_secret_ready = false;
    aurora_identity_machine_secret_clear(&context->machine_secret);

    store = aurora_identity_machine_secret_protected_state_store_ops(
        &context->machine_secret_store);
    result = aurora_identity_machine_secret_load(
        &store,
        &context->machine_secret);

    if (result == AURORA_IDENTITY_MACHINE_SECRET_OK) {
        context->machine_secret_ready = true;
        secure_zero(&store, sizeof(store));
        return true;
    }
    if (result != AURORA_IDENTITY_MACHINE_SECRET_NOT_PROVISIONED) {
        secure_zero(&store, sizeof(store));
        return false;
    }

    core.store = store;
    core.random.context = (void *)(uintptr_t)entropy_handle;
    core.random.fill_random = entropy_fill_random;
    result = aurora_identity_machine_secret_load_or_provision(
        &core,
        &context->machine_secret);

    secure_zero(&core, sizeof(core));
    secure_zero(&store, sizeof(store));
    if (result == AURORA_IDENTITY_MACHINE_SECRET_OK) {
        context->machine_secret_ready = true;
        return true;
    }
    if (result == AURORA_IDENTITY_MACHINE_SECRET_RANDOM_ERROR) {
        aurora_identity_machine_secret_clear(&context->machine_secret);
        return true;
    }
    aurora_identity_machine_secret_clear(&context->machine_secret);
    return false;
}

static bool initialize_drbg(
    uint64_t entropy_handle,
    struct identity_runtime_persistent_context *context
) {
    uint8_t seed_material[
        IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE + IDENTITY_RUNTIME_DRBG_NONCE_SIZE];
    bool instantiated;

    if (context == NULL) return false;
    context->drbg_ready = false;
    aurora_identity_hmac_drbg_clear(&context->drbg);
    secure_zero(seed_material, sizeof(seed_material));

    if (!entropy_fill_random(
            (void *)(uintptr_t)entropy_handle,
            seed_material,
            sizeof(seed_material))) {
        secure_zero(seed_material, sizeof(seed_material));
        return true;
    }

    instantiated = aurora_identity_hmac_drbg_instantiate(
        &context->drbg,
        seed_material,
        IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE,
        seed_material + IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE,
        IDENTITY_RUNTIME_DRBG_NONCE_SIZE,
        identity_runtime_drbg_personalization,
        sizeof(identity_runtime_drbg_personalization) - 1u);

    secure_zero(seed_material, sizeof(seed_material));
    if (!instantiated) {
        aurora_identity_hmac_drbg_clear(&context->drbg);
        return false;
    }
    context->drbg_ready = true;
    return true;
}

static bool initialize_hmac_provider(
    struct identity_runtime_persistent_context *context
) {
    uint8_t lookup_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    uint8_t session_grant_key[AURORA_IDENTITY_PROVIDER_KEY_SIZE];
    struct aurora_identity_hmac_drbg *drbg;
    bool initialized;

    if (context == NULL) return false;
    context->hmac_provider_ready = false;
    aurora_identity_hmac_provider_clear(&context->hmac_provider);
    secure_zero(lookup_key, sizeof(lookup_key));
    secure_zero(session_grant_key, sizeof(session_grant_key));

    if (!context->machine_secret_ready) return true;

    if (!aurora_identity_machine_secret_derive_lookup_key(
            &context->machine_secret,
            lookup_key) ||
        !aurora_identity_hmac_sha256(
            context->machine_secret.bytes,
            sizeof(context->machine_secret.bytes),
            identity_runtime_session_grant_key_domain,
            sizeof(identity_runtime_session_grant_key_domain) - 1u,
            session_grant_key)) {
        secure_zero(lookup_key, sizeof(lookup_key));
        secure_zero(session_grant_key, sizeof(session_grant_key));
        return false;
    }

    drbg = context->drbg_ready ? &context->drbg : NULL;
    initialized = aurora_identity_hmac_provider_init(
        &context->hmac_provider,
        lookup_key,
        session_grant_key,
        drbg);

    secure_zero(lookup_key, sizeof(lookup_key));
    secure_zero(session_grant_key, sizeof(session_grant_key));
    if (!initialized) {
        aurora_identity_hmac_provider_clear(&context->hmac_provider);
        return false;
    }
    context->hmac_provider_ready = true;
    return true;
}

static bool initialize_argon2id_provider(
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_argon2id_limits limits;
    if (context == NULL) return false;

    context->argon2id_provider_ready = false;
    aurora_identity_argon2id_provider_clear(&context->argon2id_provider);
    if (!context->hmac_provider_ready) return true;

    secure_zero(&limits, sizeof(limits));
    limits.minimum_memory_kib = 8u;
    limits.maximum_memory_kib = 65536u;
    limits.minimum_time_cost = 1u;
    limits.maximum_time_cost = 6u;
    limits.minimum_parallelism = 1u;
    limits.maximum_parallelism = 4u;
    limits.minimum_salt_size = 8u;
    limits.maximum_salt_size = AURORA_IDENTITY_SALT_MAX_SIZE;
    limits.minimum_verifier_size = 16u;
    limits.maximum_verifier_size = AURORA_IDENTITY_VERIFIER_MAX_SIZE;

    if (!aurora_identity_argon2id_provider_init(
            &context->argon2id_provider,
            &context->hmac_provider,
            &limits)) {
        secure_zero(&limits, sizeof(limits));
        return false;
    }
    secure_zero(&limits, sizeof(limits));
    context->argon2id_provider_ready = true;
    return true;
}

static bool initialize_identity_core(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return false;
    context->identity_core_ready = false;
    secure_zero(&context->identity_core, sizeof(context->identity_core));
    if (!context->argon2id_provider_ready) return true;

    context->identity_core.crypto =
        aurora_identity_argon2id_provider_crypto_ops(&context->argon2id_provider);
    context->identity_core.random =
        aurora_identity_hmac_provider_random_ops(&context->hmac_provider);
    context->identity_core.store =
        aurora_identity_persistent_store_core_ops(&context->persistent_store);
    context->identity_core.clock.context = NULL;
    context->identity_core.clock.monotonic_ms = runtime_monotonic_ms;
    context->identity_core.throttle_policy.free_failures = 2u;
    context->identity_core.throttle_policy.initial_delay_ms = 1000u;
    context->identity_core.throttle_policy.maximum_delay_ms = 8000u;
    context->identity_core.creation_policy.kdf.algorithm =
        AURORA_IDENTITY_KDF_ARGON2ID;
    context->identity_core.creation_policy.kdf.parameters_version =
        AURORA_IDENTITY_ARGON2ID_PARAMETERS_VERSION_1;
    context->identity_core.creation_policy.kdf.memory_kib = 65536u;
    context->identity_core.creation_policy.kdf.time_cost = 3u;
    context->identity_core.creation_policy.kdf.parallelism = 1u;
    context->identity_core.creation_policy.salt_size = 16u;
    context->identity_core.creation_policy.verifier_size = 32u;
    context->identity_core.creation_policy.identity_record_version = 1u;
    context->identity_core.creation_policy.policy_version = 1u;
    context->identity_core_ready = true;
    return true;
}

static bool initialize_session_grants(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return false;
    context->session_grant_ready = false;
    secure_zero(&context->session_grant_core, sizeof(context->session_grant_core));
    aurora_identity_session_grant_memory_init(&context->session_grant_store);

    if (!context->hmac_provider_ready || !context->drbg_ready) return true;

    context->session_grant_core.random =
        aurora_identity_hmac_provider_random_ops(&context->hmac_provider);
    context->session_grant_core.clock.context = NULL;
    context->session_grant_core.clock.monotonic_ms = runtime_monotonic_ms;
    context->session_grant_core.crypto =
        aurora_identity_hmac_provider_session_grant_crypto_ops(
            &context->hmac_provider);
    context->session_grant_core.store =
        aurora_identity_session_grant_memory_ops(&context->session_grant_store);
    context->session_grant_core.policy.ttl_ms =
        IDENTITY_RUNTIME_SESSION_GRANT_TTL_MS;
    context->session_grant_ready = true;
    return true;
}

static void clear_auth_job(struct identity_runtime_auth_job *job) {
    if (job != NULL) secure_zero(job, sizeof(*job));
}

static void clear_create_job(struct identity_runtime_create_job *job) {
    if (job != NULL) secure_zero(job, sizeof(*job));
}

static void release_persistent_context(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return;
    clear_auth_job(&context->auth_job);
    clear_create_job(&context->create_job);
    aurora_identity_session_grant_memory_clear(&context->session_grant_store);
    secure_zero(&context->session_grant_core, sizeof(context->session_grant_core));
    secure_zero(&context->identity_core, sizeof(context->identity_core));
    aurora_identity_argon2id_provider_clear(&context->argon2id_provider);
    aurora_identity_hmac_provider_clear(&context->hmac_provider);
    aurora_identity_hmac_drbg_clear(&context->drbg);
    aurora_identity_machine_secret_clear(&context->machine_secret);
    secure_zero(context, sizeof(*context));
    free(context);
}

static bool probe_user_memory(void) {
    volatile uint8_t *memory =
        (volatile uint8_t *)malloc(IDENTITY_RUNTIME_MEMORY_PROBE_SIZE);
    if (memory == NULL) return false;

    for (size_t offset = 0u;
         offset < IDENTITY_RUNTIME_MEMORY_PROBE_SIZE;
         offset += IDENTITY_RUNTIME_PAGE_SIZE) {
        if (memory[offset] != 0u) {
            free((void *)(uintptr_t)memory);
            return false;
        }
        memory[offset] = (uint8_t)(0xA5u ^ (uint8_t)(offset >> 12));
    }
    for (size_t offset = 0u;
         offset < IDENTITY_RUNTIME_MEMORY_PROBE_SIZE;
         offset += IDENTITY_RUNTIME_PAGE_SIZE) {
        uint8_t expected = (uint8_t)(0xA5u ^ (uint8_t)(offset >> 12));
        if (memory[offset] != expected) {
            free((void *)(uintptr_t)memory);
            return false;
        }
    }
    free((void *)(uintptr_t)memory);

    volatile uint8_t *reap_probe =
        (volatile uint8_t *)malloc(IDENTITY_RUNTIME_REAP_PROBE_SIZE);
    if (reap_probe == NULL) return false;
    for (size_t offset = 0u;
         offset < IDENTITY_RUNTIME_REAP_PROBE_SIZE;
         offset += IDENTITY_RUNTIME_PAGE_SIZE) {
        if (reap_probe[offset] != 0u) return false;
        reap_probe[offset] = 0x5Au;
    }
    return true;
}

static bool read_header(
    const struct aurora_sys_ipc_received *received,
    struct aurora_identity_service_message *out
) {
    if (received == NULL || out == NULL ||
        received->length < sizeof(*out)) {
        return false;
    }
    copy_bytes(out, received->data, sizeof(*out));
    return out->version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
}

static bool data_only_message_is_valid(
    const struct aurora_sys_ipc_received *received,
    const struct aurora_identity_service_message *header
) {
    return received != NULL && header != NULL &&
        received->length == AURORA_IDENTITY_SERVICE_MESSAGE_SIZE &&
        received->capability_count == 0u;
}

static bool auth_authority_is_valid(uint64_t handle) {
    return handle != 0u &&
        capability_has(handle, AURORA_CAP_IDENTITY_AUTH, AURORA_RIGHT_CONTROL) &&
        !capability_has(handle, AURORA_CAP_IDENTITY_AUTH, AURORA_RIGHT_TRANSFER);
}

static bool create_authority_is_valid(uint64_t handle) {
    return handle != 0u &&
        capability_has(handle, AURORA_CAP_IDENTITY_CREATE, AURORA_RIGHT_CONTROL) &&
        !capability_has(handle, AURORA_CAP_IDENTITY_CREATE, AURORA_RIGHT_TRANSFER);
}

static bool session_authority_is_valid(uint64_t handle) {
    return handle != 0u &&
        capability_has(handle, AURORA_CAP_IDENTITY_SESSION, AURORA_RIGHT_CONTROL) &&
        !capability_has(handle, AURORA_CAP_IDENTITY_SESSION, AURORA_RIGHT_TRANSFER);
}

static bool sensitive_job_busy(
    const struct identity_runtime_persistent_context *context
) {
    return context != NULL &&
        (context->auth_job.occupied || context->create_job.occupied);
}

static bool handle_consume_session_grant(
    uint64_t endpoint,
    const struct aurora_sys_ipc_received *received,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_service_consume_session_grant request;
    struct aurora_identity_session_grant_token token;
    secure_zero(&request, sizeof(request));
    secure_zero(&token, sizeof(token));

    if (received == NULL || context == NULL ||
        received->length != sizeof(request) ||
        received->capability_count != 1u) {
        revoke_received_capabilities(received);
        return send_session_grant_result(
            endpoint,
            0u,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL);
    }

    copy_bytes(&request, received->data, sizeof(request));
    uint64_t authority = received->capabilities[0];
    bool authorized = session_authority_is_valid(authority);
    bool revoked = revoke_capability(authority);
    uint64_t request_id = request.header.request_id;

    if (!authorized || !revoked) {
        secure_zero(&request, sizeof(request));
        return send_session_grant_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNAUTHORIZED,
            NULL);
    }

    if (request.header.version != AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION ||
        request.header.type != AURORA_IDENTITY_SERVICE_CONSUME_SESSION_GRANT ||
        request_id == 0u) {
        secure_zero(&request, sizeof(request));
        return send_session_grant_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL);
    }

    if (!context->session_grant_ready) {
        secure_zero(&request, sizeof(request));
        return send_session_grant_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE,
            NULL);
    }

    copy_bytes(token.bytes, request.session_grant, sizeof(token.bytes));
    secure_zero(&request, sizeof(request));

    struct aurora_identity_session_grant_consume_result consumed =
        aurora_identity_session_grant_consume(
            &context->session_grant_core,
            &token);
    secure_zero(&token, sizeof(token));

    if (consumed.result == AURORA_IDENTITY_SESSION_GRANT_OK) {
        bool sent = send_session_grant_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SUCCESS,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE,
            &consumed.user_id);
        secure_zero(&consumed, sizeof(consumed));
        return sent;
    }

    if (consumed.result == AURORA_IDENTITY_SESSION_GRANT_NOT_FOUND ||
        consumed.result == AURORA_IDENTITY_SESSION_GRANT_EXPIRED ||
        consumed.result == AURORA_IDENTITY_SESSION_GRANT_INVALID_ARGUMENT) {
        secure_zero(&consumed, sizeof(consumed));
        return send_session_grant_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_REJECTED,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_AUTH_FAILED,
            NULL);
    }

    uint32_t public_error =
        consumed.result == AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR
            ? AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_STORAGE_FAILURE
            : AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INTERNAL_FAILURE;
    secure_zero(&consumed, sizeof(consumed));
    return send_session_grant_result(
        endpoint,
        request_id,
        AURORA_IDENTITY_SERVICE_SESSION_GRANT_STATE_SERVICE_ERROR,
        public_error,
        NULL);
}

static bool handle_begin_key_auth(
    uint64_t endpoint,
    const struct aurora_sys_ipc_received *received,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_service_begin_key_auth request;
    secure_zero(&request, sizeof(request));

    if (received == NULL || context == NULL ||
        received->length != sizeof(request) ||
        received->capability_count != 1u) {
        revoke_received_capabilities(received);
        return send_auth_result(
            endpoint,
            0u,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            0u,
            NULL);
    }

    copy_bytes(&request, received->data, sizeof(request));
    uint64_t authority = received->capabilities[0];
    bool authorized = auth_authority_is_valid(authority);
    bool revoked = revoke_capability(authority);

    if (!authorized || !revoked) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_auth_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNAUTHORIZED,
            0u,
            NULL);
    }

    if (request.header.version != AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION ||
        request.header.type != AURORA_IDENTITY_SERVICE_BEGIN_KEY_AUTH ||
        request.header.request_id == 0u ||
        request.reserved != 0u ||
        request.key_length == 0u ||
        request.key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_auth_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            0u,
            NULL);
    }

    if (!context->identity_core_ready) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_auth_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE,
            0u,
            NULL);
    }

    if (sensitive_job_busy(context)) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_auth_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_BUSY,
            0u,
            NULL);
    }

    clear_auth_job(&context->auth_job);
    context->auth_job.occupied = true;
    context->auth_job.request_id = request.header.request_id;
    context->auth_job.key_length = request.key_length;
    copy_bytes(
        context->auth_job.key,
        request.key,
        context->auth_job.key_length);
    uint64_t request_id = request.header.request_id;
    secure_zero(&request, sizeof(request));
    return send_message(endpoint, AURORA_IDENTITY_SERVICE_AUTH_PENDING, request_id);
}

static bool execute_auth_job(
    uint64_t endpoint,
    struct identity_runtime_persistent_context *context
) {
    struct identity_runtime_auth_job *job = &context->auth_job;
    struct aurora_identity_auth_result auth = aurora_identity_authenticate_key(
        &context->identity_core,
        job->key,
        job->key_length);

    secure_zero(job->key, sizeof(job->key));
    job->key_length = 0u;

    if (auth.result == AURORA_IDENTITY_OK) {
        if (!context->session_grant_ready) {
            secure_zero(&auth, sizeof(auth));
            return send_auth_result(
                endpoint,
                job->request_id,
                AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE,
                0u,
                NULL);
        }

        struct aurora_identity_session_grant_issue_result grant =
            aurora_identity_session_grant_issue(
                &context->session_grant_core,
                &auth.user_id);
        secure_zero(&auth, sizeof(auth));

        if (grant.result == AURORA_IDENTITY_SESSION_GRANT_OK) {
            bool sent = send_auth_result(
                endpoint,
                job->request_id,
                AURORA_IDENTITY_SERVICE_AUTH_STATE_SUCCESS,
                AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE,
                0u,
                grant.token.bytes);
            secure_zero(&grant, sizeof(grant));
            return sent;
        }

        uint32_t public_error =
            grant.result == AURORA_IDENTITY_SESSION_GRANT_BACKEND_ERROR
                ? AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_STORAGE_FAILURE
                : AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INTERNAL_FAILURE;
        if (grant.result == AURORA_IDENTITY_SESSION_GRANT_RANDOM_ERROR) {
            public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE;
        }
        secure_zero(&grant, sizeof(grant));
        return send_auth_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            public_error,
            0u,
            NULL);
    }

    if (auth.result == AURORA_IDENTITY_THROTTLED) {
        uint64_t retry_after_ms = auth.retry_after_ms;
        secure_zero(&auth, sizeof(auth));
        return send_auth_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_THROTTLED,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_THROTTLED,
            retry_after_ms,
            NULL);
    }

    if (auth.result == AURORA_IDENTITY_BACKEND_ERROR) {
        secure_zero(&auth, sizeof(auth));
        return send_auth_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_STORAGE_FAILURE,
            0u,
            NULL);
    }

    if (auth.result == AURORA_IDENTITY_CRYPTO_ERROR ||
        auth.result == AURORA_IDENTITY_POLICY_ERROR ||
        auth.result == AURORA_IDENTITY_RANDOM_ERROR) {
        secure_zero(&auth, sizeof(auth));
        return send_auth_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INTERNAL_FAILURE,
            0u,
            NULL);
    }

    secure_zero(&auth, sizeof(auth));
    return send_auth_result(
        endpoint,
        job->request_id,
        AURORA_IDENTITY_SERVICE_AUTH_STATE_FAILED,
        AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_AUTH_FAILED,
        0u,
        NULL);
}

static bool handle_query_auth(
    uint64_t endpoint,
    const struct aurora_identity_service_message *request,
    struct identity_runtime_persistent_context *context
) {
    if (request == NULL || context == NULL ||
        request->request_id == 0u ||
        !context->auth_job.occupied ||
        context->auth_job.request_id != request->request_id) {
        return send_auth_result(
            endpoint,
            request == NULL ? 0u : request->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            0u,
            NULL);
    }

    bool sent = execute_auth_job(endpoint, context);
    clear_auth_job(&context->auth_job);
    return sent;
}

static bool handle_cancel_auth(
    uint64_t endpoint,
    const struct aurora_identity_service_message *request,
    struct identity_runtime_persistent_context *context
) {
    if (request == NULL || context == NULL ||
        request->request_id == 0u ||
        !context->auth_job.occupied ||
        context->auth_job.request_id != request->request_id) {
        return send_auth_result(
            endpoint,
            request == NULL ? 0u : request->request_id,
            AURORA_IDENTITY_SERVICE_AUTH_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            0u,
            NULL);
    }

    uint64_t request_id = context->auth_job.request_id;
    clear_auth_job(&context->auth_job);
    return send_message(
        endpoint,
        AURORA_IDENTITY_SERVICE_AUTH_CANCELLED,
        request_id);
}

static bool handle_begin_create(
    uint64_t endpoint,
    const struct aurora_sys_ipc_received *received,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_service_begin_create request;
    secure_zero(&request, sizeof(request));

    if (received == NULL || context == NULL ||
        received->length != sizeof(request) ||
        received->capability_count != 1u) {
        revoke_received_capabilities(received);
        return send_create_result(
            endpoint,
            0u,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL,
            NULL);
    }

    copy_bytes(&request, received->data, sizeof(request));
    uint64_t authority = received->capabilities[0];
    bool authorized = create_authority_is_valid(authority);
    bool revoked = revoke_capability(authority);

    if (!authorized || !revoked) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_create_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_UNAUTHORIZED,
            NULL,
            NULL);
    }

    if (request.header.version != AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION ||
        request.header.type != AURORA_IDENTITY_SERVICE_BEGIN_CREATE ||
        request.header.request_id == 0u ||
        request.reserved != 0u ||
        request.key_length == 0u ||
        request.key_length > AURORA_IDENTITY_SERVICE_KEY_MAX_LEN) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_create_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL,
            NULL);
    }

    if (!context->identity_core_ready || !context->drbg_ready) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_create_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE,
            NULL,
            NULL);
    }

    if (sensitive_job_busy(context)) {
        uint64_t request_id = request.header.request_id;
        secure_zero(&request, sizeof(request));
        return send_create_result(
            endpoint,
            request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_BUSY,
            NULL,
            NULL);
    }

    clear_create_job(&context->create_job);
    context->create_job.occupied = true;
    context->create_job.request_id = request.header.request_id;
    context->create_job.key_length = request.key_length;
    copy_bytes(
        context->create_job.key,
        request.key,
        context->create_job.key_length);
    uint64_t request_id = request.header.request_id;
    secure_zero(&request, sizeof(request));
    return send_message(
        endpoint,
        AURORA_IDENTITY_SERVICE_CREATE_PENDING,
        request_id);
}

static bool execute_create_job(
    uint64_t endpoint,
    struct identity_runtime_persistent_context *context
) {
    struct identity_runtime_create_job *job = &context->create_job;
    struct aurora_identity_create_result created =
        aurora_identity_create_with_key(
            &context->identity_core,
            job->key,
            job->key_length);

    secure_zero(job->key, sizeof(job->key));
    job->key_length = 0u;

    if (created.result == AURORA_IDENTITY_OK) {
        bool sent = send_create_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SUCCESS,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_NONE,
            &created.user_id,
            &created.credential_id);
        secure_zero(&created, sizeof(created));
        return sent;
    }

    if (created.result == AURORA_IDENTITY_ALREADY_EXISTS) {
        secure_zero(&created, sizeof(created));
        return send_create_result(
            endpoint,
            job->request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_ALREADY_EXISTS,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_POLICY_DENIED,
            NULL,
            NULL);
    }

    uint32_t public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INTERNAL_FAILURE;
    if (created.result == AURORA_IDENTITY_BACKEND_ERROR) {
        public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_STORAGE_FAILURE;
    } else if (created.result == AURORA_IDENTITY_RANDOM_ERROR) {
        public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_SERVICE_UNAVAILABLE;
    } else if (created.result == AURORA_IDENTITY_POLICY_ERROR) {
        public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_POLICY_DENIED;
    } else if (created.result == AURORA_IDENTITY_INVALID_ARGUMENT ||
               created.result == AURORA_IDENTITY_INVALID_KEY_FORMAT) {
        public_error = AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST;
    }

    secure_zero(&created, sizeof(created));
    return send_create_result(
        endpoint,
        job->request_id,
        AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
        public_error,
        NULL,
        NULL);
}

static bool handle_query_create(
    uint64_t endpoint,
    const struct aurora_identity_service_message *request,
    struct identity_runtime_persistent_context *context
) {
    if (request == NULL || context == NULL ||
        request->request_id == 0u ||
        !context->create_job.occupied ||
        context->create_job.request_id != request->request_id) {
        return send_create_result(
            endpoint,
            request == NULL ? 0u : request->request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL,
            NULL);
    }

    bool sent = execute_create_job(endpoint, context);
    clear_create_job(&context->create_job);
    return sent;
}

static bool handle_cancel_create(
    uint64_t endpoint,
    const struct aurora_identity_service_message *request,
    struct identity_runtime_persistent_context *context
) {
    if (request == NULL || context == NULL ||
        request->request_id == 0u ||
        !context->create_job.occupied ||
        context->create_job.request_id != request->request_id) {
        return send_create_result(
            endpoint,
            request == NULL ? 0u : request->request_id,
            AURORA_IDENTITY_SERVICE_CREATE_STATE_SERVICE_ERROR,
            AURORA_IDENTITY_SERVICE_PUBLIC_ERROR_INVALID_REQUEST,
            NULL,
            NULL);
    }

    uint64_t request_id = context->create_job.request_id;
    clear_create_job(&context->create_job);
    return send_message(
        endpoint,
        AURORA_IDENTITY_SERVICE_CREATE_CANCELLED,
        request_id);
}

int64_t identity_runtime_main(uint64_t initial_rsp) {
    if (initial_rsp < AURORA_SERVICE_STARTUP_STACK_OFFSET) return 1;

    const struct aurora_service_startup_block *startup =
        (const struct aurora_service_startup_block *)(uintptr_t)(
            initial_rsp - AURORA_SERVICE_STARTUP_STACK_OFFSET);

    if (startup->abi_version != AURORA_SERVICE_STARTUP_ABI_VERSION ||
        startup->flags != 0u ||
        startup->extra_capability_count != 0u ||
        startup->reserved != 0u ||
        !validate_authority(startup)) {
        return 1;
    }

    struct identity_runtime_persistent_context *persistent_context =
        (struct identity_runtime_persistent_context *)malloc(
            sizeof(struct identity_runtime_persistent_context));
    if (persistent_context == NULL) return 1;
    secure_zero(persistent_context, sizeof(*persistent_context));

    if (!open_persistent_store(
            startup->protected_state,
            persistent_context) ||
        !initialize_machine_secret(
            startup->entropy_seed,
            persistent_context) ||
        !initialize_drbg(
            startup->entropy_seed,
            persistent_context) ||
        !initialize_hmac_provider(persistent_context) ||
        !initialize_argon2id_provider(persistent_context) ||
        !initialize_identity_core(persistent_context) ||
        !initialize_session_grants(persistent_context)) {
        release_persistent_context(persistent_context);
        return 1;
    }

    /* Entropy is bootstrap-only. DRBG state is process-owned after this point. */
    if (!revoke_capability(startup->entropy_seed)) {
        release_persistent_context(persistent_context);
        return 1;
    }

    if (!probe_user_memory()) {
        release_persistent_context(persistent_context);
        return 1;
    }

    if (!send_message(
            startup->ipc_endpoint,
            AURORA_IDENTITY_SERVICE_READY,
            0u)) {
        release_persistent_context(persistent_context);
        return 1;
    }

    for (;;) {
        struct aurora_sys_ipc_received received;
        struct aurora_identity_service_message request;

        if (!wait_for_message(startup->ipc_endpoint) ||
            !receive_message(startup->ipc_endpoint, &received)) {
            release_persistent_context(persistent_context);
            return 1;
        }

        secure_zero(&request, sizeof(request));
        if (!read_header(&received, &request)) {
            revoke_received_capabilities(&received);
            if (!send_message(
                    startup->ipc_endpoint,
                    AURORA_IDENTITY_SERVICE_ERROR,
                    0u)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_BEGIN_KEY_AUTH) {
            if (!handle_begin_key_auth(
                    startup->ipc_endpoint,
                    &received,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_BEGIN_CREATE) {
            if (!handle_begin_create(
                    startup->ipc_endpoint,
                    &received,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_CONSUME_SESSION_GRANT) {
            if (!handle_consume_session_grant(
                    startup->ipc_endpoint,
                    &received,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (!data_only_message_is_valid(&received, &request)) {
            revoke_received_capabilities(&received);
            if (!send_message(
                    startup->ipc_endpoint,
                    AURORA_IDENTITY_SERVICE_ERROR,
                    request.request_id)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_QUERY_AUTH) {
            if (!handle_query_auth(
                    startup->ipc_endpoint,
                    &request,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_CANCEL_AUTH) {
            if (!handle_cancel_auth(
                    startup->ipc_endpoint,
                    &request,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_QUERY_CREATE) {
            if (!handle_query_create(
                    startup->ipc_endpoint,
                    &request,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_CANCEL_CREATE) {
            if (!handle_cancel_create(
                    startup->ipc_endpoint,
                    &request,
                    persistent_context)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_PING) {
            if (!send_message(
                    startup->ipc_endpoint,
                    AURORA_IDENTITY_SERVICE_PONG,
                    request.request_id)) {
                release_persistent_context(persistent_context);
                return 1;
            }
            continue;
        }

        if (request.type == AURORA_IDENTITY_SERVICE_SHUTDOWN) {
            bool sent = send_message(
                startup->ipc_endpoint,
                AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK,
                request.request_id);
            release_persistent_context(persistent_context);
            return sent ? 0 : 1;
        }

        if (!send_message(
                startup->ipc_endpoint,
                AURORA_IDENTITY_SERVICE_ERROR,
                request.request_id)) {
            release_persistent_context(persistent_context);
            return 1;
        }
    }
}
