#ifndef AURORA_G5_IPC_ABI_H
#define AURORA_G5_IPC_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <aurora/syscall_abi.h>

/* G5-D32: portable, fixed-width, little-endian wire format.
 * Never cast an untrusted byte buffer to a C struct. */
#define G5_IPC_WIRE_HEADER_BYTES 48u
#define G5_IPC_WIRE_MAX_BYTES AURORA_SYS_IPC_PAYLOAD_MAX
#define G5_IPC_WIRE_INLINE_MAX (G5_IPC_WIRE_MAX_BYTES - G5_IPC_WIRE_HEADER_BYTES)
#define G5_IPC_WIRE_MAX_CAPS AURORA_SYS_IPC_CAPS_MAX
#define G5_IPC_WIRE_MAJOR 1u
#define G5_IPC_WIRE_MINOR 0u

enum g5_ipc_kind {
    G5_IPC_REQUEST = 1,
    G5_IPC_RESPONSE = 2,
    G5_IPC_EVENT = 3,
    G5_IPC_CANCEL = 4
};

enum g5_ipc_status {
    G5_IPC_OK = 0,
    G5_IPC_BAD_ARGUMENT,
    G5_IPC_BAD_FORMAT,
    G5_IPC_UNSUPPORTED_VERSION,
    G5_IPC_TOO_LARGE,
    G5_IPC_UNSUPPORTED_FLAGS,
    G5_IPC_BAD_CAPABILITIES
};

/* Logical decoded representation only; this is NOT the on-wire layout. */
struct g5_ipc_header {
    uint16_t major;
    uint16_t minor;
    uint16_t header_bytes;
    uint16_t kind;
    uint32_t operation;
    uint32_t flags;
    uint32_t payload_bytes;
    uint64_t request_id;
    uint64_t session_generation;
    uint64_t object_generation;
};

/* Caller supplies buffers and parsed-header storage; no dynamic allocations. */
enum g5_ipc_status g5_ipc_encode(
    const struct g5_ipc_header *header,
    const uint8_t *payload,
    uint8_t *destination,
    size_t destination_capacity,
    size_t *out_length
);
enum g5_ipc_status g5_ipc_decode(
    const uint8_t *source,
    size_t source_length,
    struct g5_ipc_header *out_header,
    const uint8_t **out_payload
);

/* Checks only the shape of received capability handles. The receiving service
 * MUST separately check each handle's type, rights, owner and live generation
 * through CAP_CHECK/its authoritative capability table before any use.
 * Never interpret numeric values in message payload as capability handles. */
enum g5_ipc_status g5_ipc_decode_received(
    const struct aurora_sys_ipc_received *received,
    struct g5_ipc_header *out_header,
    const uint8_t **out_payload
);

#endif
