#ifndef AURORA_IDENTITY_SERVICE_PROTOCOL_H
#define AURORA_IDENTITY_SERVICE_PROTOCOL_H

#define AURORA_IDENTITY_PROTOCOL_MAGIC 0x31444941
#define AURORA_IDENTITY_PROTOCOL_VERSION 1
#define AURORA_IDENTITY_PROTOCOL_MESSAGE_SIZE 16

#define AURORA_IDENTITY_OP_PING 1
#define AURORA_IDENTITY_OP_SHUTDOWN 2
#define AURORA_IDENTITY_OP_RESPONSE_FLAG 0x8000

#define AURORA_IDENTITY_STATUS_OK 0

#ifndef __ASSEMBLER__

#include <stdint.h>

struct aurora_identity_service_message {
    uint32_t magic;
    uint16_t version;
    uint16_t opcode;
    uint32_t request_id;
    uint32_t value;
};

_Static_assert(
    sizeof(struct aurora_identity_service_message) ==
        AURORA_IDENTITY_PROTOCOL_MESSAGE_SIZE,
    "Identity service protocol message size must remain ABI-stable"
);

#endif

#endif
