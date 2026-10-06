#include "protected_state_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/identity/crypto_foundation.h>
#include <aurora/identity/machine_secret.h>
#include <aurora/identity/machine_secret_protected_state.h>
#include <aurora/identity/persistent_store.h>
#include <aurora/identity/persistent_store_protected_state.h>
#include <aurora/identity_service_protocol.h>
#include <aurora/service_abi.h>
#include <aurora/syscall_abi.h>

#define IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE 32u
#define IDENTITY_RUNTIME_DRBG_NONCE_SIZE 16u
#define IDENTITY_RUNTIME_DRBG_SEED_MATERIAL_SIZE \
    (IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE + IDENTITY_RUNTIME_DRBG_NONCE_SIZE)
#define IDENTITY_RUNTIME_LOOKUP_KEY_SIZE 32u
#define IDENTITY_RUNTIME_MEMORY_PROBE_SIZE (1024u * 1024u)
#define IDENTITY_RUNTIME_REAP_PROBE_SIZE (256u * 1024u)
#define IDENTITY_RUNTIME_PAGE_SIZE 4096u

static const uint8_t identity_runtime_drbg_personalization[] =
    "AURORA.IDENTITY.RING3.DRBG.V1";

void *malloc(size_t size);
void free(void *pointer);

struct identity_runtime_persistent_context {
    struct identity_runtime_protected_state_context transport_context;
    struct aurora_identity_persistent_protected_state_store protected_state_store;
    struct aurora_identity_persistent_store persistent_store;
    struct aurora_identity_machine_secret_protected_state_store machine_secret_store;
    struct aurora_identity_hmac_drbg drbg;
    struct aurora_identity_machine_secret machine_secret;
    uint8_t lookup_key[IDENTITY_RUNTIME_LOOKUP_KEY_SIZE];
    bool persistent_store_ready;
    bool drbg_ready;
    bool machine_secret_ready;
    bool lookup_key_ready;
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

static bool send_status_response(
    uint64_t endpoint,
    uint64_t request_id,
    const struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return false;

    struct aurora_identity_service_status_response response;
    response.version = AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION;
    response.type = AURORA_IDENTITY_SERVICE_STATUS_RESPONSE;
    response.request_id = request_id;
    response.flags = 0u;

    if (context->persistent_store_ready) {
        response.flags |= AURORA_IDENTITY_SERVICE_STATUS_PERSISTENT_STORE_READY;
    }
    if (context->drbg_ready) {
        response.flags |= AURORA_IDENTITY_SERVICE_STATUS_DRBG_READY;
    }
    if (context->machine_secret_ready) {
        response.flags |= AURORA_IDENTITY_SERVICE_STATUS_MACHINE_SECRET_READY;
    }
    if (context->lookup_key_ready) {
        response.flags |= AURORA_IDENTITY_SERVICE_STATUS_LOOKUP_KEY_READY;
    }

    bool sent = aurora_syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)&response,
        sizeof(response),
        0u,
        0u
    ) == 0u;

    secure_zero(&response, sizeof(response));
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

static bool open_protected_state(
    uint64_t protected_state_handle,
    struct identity_runtime_persistent_context *context
) {
    struct aurora_identity_protected_state_transport_ops transport;
    if (context == NULL) return false;

    context->persistent_store_ready = false;
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

    context->persistent_store_ready =
        result == AURORA_IDENTITY_PERSISTENT_OPEN_OK ||
        result == AURORA_IDENTITY_PERSISTENT_OPEN_EMPTY;
    return context->persistent_store_ready;
}

static bool instantiate_runtime_drbg(
    uint64_t entropy_handle,
    struct identity_runtime_persistent_context *context
) {
    uint8_t seed_material[IDENTITY_RUNTIME_DRBG_SEED_MATERIAL_SIZE];
    if (context == NULL) return false;

    secure_zero(seed_material, sizeof(seed_material));
    aurora_identity_hmac_drbg_clear(&context->drbg);
    context->drbg_ready = false;

    bool seeded = aurora_syscall3(
        AURORA_SYS_ENTROPY_SEED,
        entropy_handle,
        (uint64_t)(uintptr_t)seed_material,
        sizeof(seed_material)
    ) == 0u;

    if (seeded) {
        context->drbg_ready = aurora_identity_hmac_drbg_instantiate(
            &context->drbg,
            seed_material,
            IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE,
            seed_material + IDENTITY_RUNTIME_DRBG_ENTROPY_SIZE,
            IDENTITY_RUNTIME_DRBG_NONCE_SIZE,
            identity_runtime_drbg_personalization,
            sizeof(identity_runtime_drbg_personalization) - 1u);
    }

    secure_zero(seed_material, sizeof(seed_material));
    return context->drbg_ready;
}

static bool runtime_random_fill(
    void *opaque,
    uint8_t *buffer,
    size_t size
) {
    struct identity_runtime_persistent_context *context = opaque;
    if (context == NULL || !context->drbg_ready ||
        (buffer == NULL && size != 0u)) {
        return false;
    }

    size_t offset = 0u;
    while (offset < size) {
        size_t remaining = size - offset;
        size_t request = remaining > AURORA_IDENTITY_HMAC_DRBG_MAX_REQUEST_SIZE
            ? AURORA_IDENTITY_HMAC_DRBG_MAX_REQUEST_SIZE
            : remaining;
        if (!aurora_identity_hmac_drbg_generate(
                &context->drbg,
                buffer + offset,
                request,
                NULL,
                0u)) {
            return false;
        }
        offset += request;
    }

    return true;
}

static bool initialize_machine_secret(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return false;

    struct aurora_identity_machine_secret_store_ops store =
        aurora_identity_machine_secret_protected_state_store_ops(
            &context->machine_secret_store);

    enum aurora_identity_machine_secret_result result =
        aurora_identity_machine_secret_load(
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

    /*
     * A machine with no qualified seed source may still run the service in a
     * degraded state so existing non-random operations remain diagnosable.
     * It must not fabricate a root secret. First provisioning waits for the
     * reviewed DRBG to be instantiated from AURORA_CAP_ENTROPY.
     */
    if (!context->drbg_ready) {
        secure_zero(&store, sizeof(store));
        return true;
    }

    struct aurora_identity_machine_secret_core core;
    secure_zero(&core, sizeof(core));
    core.random.context = context;
    core.random.fill_random = runtime_random_fill;
    core.store = store;

    result = aurora_identity_machine_secret_load_or_provision(
        &core,
        &context->machine_secret);

    secure_zero(&core, sizeof(core));
    secure_zero(&store, sizeof(store));

    if (result != AURORA_IDENTITY_MACHINE_SECRET_OK) {
        aurora_identity_machine_secret_clear(&context->machine_secret);
        return false;
    }

    context->machine_secret_ready = true;
    return true;
}

static bool initialize_lookup_key(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return false;

    secure_zero(context->lookup_key, sizeof(context->lookup_key));
    context->lookup_key_ready = false;

    if (!context->machine_secret_ready) {
        return true;
    }

    if (!aurora_identity_machine_secret_derive_lookup_key(
            &context->machine_secret,
            context->lookup_key)) {
        secure_zero(context->lookup_key, sizeof(context->lookup_key));
        return false;
    }

    context->lookup_key_ready = true;
    return true;
}

static void release_persistent_context(
    struct identity_runtime_persistent_context *context
) {
    if (context == NULL) return;
    secure_zero(context->lookup_key, sizeof(context->lookup_key));
    aurora_identity_machine_secret_clear(&context->machine_secret);
    aurora_identity_hmac_drbg_clear(&context->drbg);
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

    if (!open_protected_state(
            startup->protected_state,
            persistent_context)) {
        release_persistent_context(persistent_context);
        return 1;
    }

    /*
     * The entropy capability is a seed authority, not the runtime RNG itself.
     * Seed material is copied once into userspace, used to instantiate the
     * reviewed HMAC-DRBG, and immediately zeroized. If no qualified platform
     * seed is available, the service may stay alive but all random-producing
     * operations remain unavailable.
     */
    (void)instantiate_runtime_drbg(
        startup->entropy_seed,
        persistent_context);

    /*
     * Corrupt/conflicting Machine Root Secret state is fatal and never repaired
     * or regenerated. Only the all-absent state may be provisioned, and only
     * from the live DRBG. Restart therefore reconstructs the exact same root
     * secret from Protected State rather than changing stable Identity lookup.
     */
    if (!initialize_machine_secret(persistent_context) ||
        !initialize_lookup_key(persistent_context)) {
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

        if (request.type == AURORA_IDENTITY_SERVICE_STATUS) {
            if (!send_status_response(
                    startup->ipc_endpoint,
                    request.request_id,
                    persistent_context)) {
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
