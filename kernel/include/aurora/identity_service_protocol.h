#ifndef AURORA_IDENTITY_SERVICE_PROTOCOL_H
#define AURORA_IDENTITY_SERVICE_PROTOCOL_H

#include <stdint.h>

#define AURORA_IDENTITY_SERVICE_PROTOCOL_VERSION 1u
#define AURORA_IDENTITY_SERVICE_MESSAGE_SIZE 16u

enum aurora_identity_service_message_type {
    AURORA_IDENTITY_SERVICE_READY = 1,
    AURORA_IDENTITY_SERVICE_PING = 2,
    AURORA_IDENTITY_SERVICE_PONG = 3,
    AURORA_IDENTITY_SERVICE_SHUTDOWN = 4,
    AURORA_IDENTITY_SERVICE_SHUTDOWN_ACK = 5,
    AURORA_IDENTITY_SERVICE_ERROR = 255
};

struct aurora_identity_service_message {
    uint32_t version;
    uint32_t type;
    uint64_t request_id;
};

#endif
