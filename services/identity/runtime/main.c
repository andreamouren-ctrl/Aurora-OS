#include "protected_state_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/identity/crypto_foundation.h>
#include <aurora/identity/crypto_provider.h>
#include <aurora/identity/machine_secret.h>
#include <aurora/identity/machine_secret_protected_state.h>
#include <aurora/identity/persistent_store.h>
#include <aurora/identity/persistent_store_protected_state.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/service_abi.h>
#include <aurora/syscall_abi.h>

#define IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE 32u
#define IDENTITY_RUNTIME_DRBG_NONCE_SIZE 16u
#define IDENTITY_RUNTIME_MEMORY_PROBE_SIZE (1024u * 1024u)
#define IDENTITY_RUNTIME_REAP_PROBE_SIZE (256u * 1024u)
#define IDENTITY_RUNTIME_PAGE_SIZE 4096u

void *malloc(size_t size);
void free(void *pointer);

static const uint8_t identity_runtime_drbg_personalization[] =
    "AURORA.IDENTITY.RUNTIME.HMAC-DRBG.V1";
static const uint8_t identity_runtime_session_grant_key_domain[] =
    "AURORA.IDENTITY.SESSION-GRANT-KEY.V1";

struct identity_runtime_persistent_context {
    struct identity_runtime_protected_state_context transport_context;
    struct aurora_identity_persistent_protected_state_store protected_state_store;
    struct aurora_identity_persistent_store persistent_store;
    struct aurora_identity_machine_secret_protected_state_store machine_secret_store;
    struct aurora_identity_machine_secret machine_secret;
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_hmac_provider hmac_provider;
    bool machine_secret_ready;
    bool drbg_ready;
    bool hmac_provider_ready;
};

static void secure_zero(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (buffer == NULL) return;
    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
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

static bool send_message(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id
) {
    struct aurora_identity_service_message message;
    message.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    message.type = type;
    message.request_id = request_id;

    return aurora_syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)&message,
        sizeof(message),
        0u,
        0u
    ) == 0u;
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
    return aurora_syscall2(
        AURORA_SYS_IPC_WAIT,
        endpoint,
        0u
    ) == 0u;
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
    bool initialized;

    if (context == NULL) return false;

    context->hmac_provider_ready = false;
    aurora_identity_hmac_provider_clear(&context->hmac_provider);
    secure_zero(lookup_key, sizeof(lookup_key));
    secure_zero(session_grant_key, sizeof(session_grant_key));

    if (!context->machine_secret_ready || !context->drbg_ready) {
        return true;
    }

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

    initialized = aurora_identity_hmac_provider_init(
        &context->hmac_provider,
        lookup_key,
        session_grant_key,
        &context->drbg);

    secure_zero(lookup_key, sizeof(lookup_key));
    secure_zero(session_grant_key, sizeof(session_grant_key));

    if (!initialized) {
        aurora_identity_hmac_provider_clear(&context->hmac_provider);
        return false;
    }

    context->hmac_provider_ready = true;
    return true;
}

static void release_persistent_context(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return;
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

    /*
     * Keep one mapping alive deliberately. The service-supervisor PMM baseline
     * check must prove that process_reap() scrubs and reclaims it after exit or
     * restart, even when userspace never calls free().
     */
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

static bool message_is_valid(
    const struct aurora_sys_ipc_received *received,
    struct aurora_identity_service_message *out
) {
    if (received == NULL || out == NULL ||
        received->length != AURORA_IDENTITY_SERVICE_MESSAGE_SIZE ||
        received->capability_count != 0u) {
        return false;
    }

    const uint8_t *source = received->data;
    uint8_t *destination = (uint8_t *)out;
    for (size_t i = 0u; i < sizeof(*out); ++i) {
        destination[i] = source[i];
    }

    return out->version == AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
}

int64_t identity_runtime_main(uint64_t initial_rsp) {
    if (initial_rsp < AURORA_SERVICE_STARTUP_STACK_OFFSET) return 1;

    const struct aurora_service_startup_block *startup =
        (const struct aurora_service_startup_block *)(uintptr_t)(
            initial_rsp - AURORA_SERVICE_STARTUP_STACK_OFFSET);

    if (startup->abi_version != AURORA_SERVICE_STARTUP_ABI_VERSION ||
        startup->flags != 0u ||
        !validate_authority(startup)) {
        return 1;
    }

    /*
     * Flat service image pages are executable and intentionally not writable.
     * Long-lived mutable Identity state therefore belongs in process-owned
     * anonymous RW memory, not in .data/.bss inside the RX image.
     */
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
        !initialize_hmac_provider(persistent_context)) {
        release_persistent_context(persistent_context);
        return 1;
    }

    /*
     * Service startup is allowed when the platform currently has no qualified
     * entropy source. Existing protected state is still opened fail-closed,
     * while random-producing operations remain unavailable when drbg_ready is
     * false. The HMAC provider becomes ready only after both the stable machine
     * secret and the live DRBG are available, so lookup/session keys are never
     * fabricated from volatile process state.
     */

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
        if (!message_is_valid(&received, &request)) {
            if (!send_message(
                    startup->ipc_endpoint,
                    AURORA_IDENTITY_SERVICE_ERROR,
                    0u)) {
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
