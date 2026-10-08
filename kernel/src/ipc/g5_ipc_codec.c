#include <aurora/g5_ipc_abi.h>

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8u * i);
    return v;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8u * i);
    return v;
}
static enum g5_ipc_status check_header(const struct g5_ipc_header *h) {
    if (h->major != G5_IPC_WIRE_MAJOR || h->minor != G5_IPC_WIRE_MINOR)
        return G5_IPC_UNSUPPORTED_VERSION;
    if (h->header_bytes != G5_IPC_WIRE_HEADER_BYTES ||
        h->kind < G5_IPC_REQUEST || h->kind > G5_IPC_CANCEL ||
        h->operation == 0 || h->request_id == 0 || h->session_generation == 0)
        return G5_IPC_BAD_FORMAT;
    if (h->flags != 0) return G5_IPC_UNSUPPORTED_FLAGS;
    if (h->payload_bytes > G5_IPC_WIRE_INLINE_MAX) return G5_IPC_TOO_LARGE;
    return G5_IPC_OK;
}
enum g5_ipc_status g5_ipc_encode(
    const struct g5_ipc_header *h, const uint8_t *payload,
    uint8_t *dst, size_t cap, size_t *out_len
) {
    if (out_len != NULL) *out_len = 0;
    if (h == NULL || dst == NULL || out_len == NULL ||
        (h->payload_bytes != 0 && payload == NULL)) return G5_IPC_BAD_ARGUMENT;
    enum g5_ipc_status status = check_header(h);
    if (status != G5_IPC_OK) return status;
    const size_t n = G5_IPC_WIRE_HEADER_BYTES + h->payload_bytes;
    if (cap < n) return G5_IPC_TOO_LARGE;
    dst[0]='G'; dst[1]='5'; dst[2]='I'; dst[3]='P';
    put16(dst+4,h->major); put16(dst+6,h->minor);
    put16(dst+8,h->header_bytes); put16(dst+10,h->kind);
    put32(dst+12,h->operation); put32(dst+16,h->flags);
    put32(dst+20,h->payload_bytes); put64(dst+24,h->request_id);
    put64(dst+32,h->session_generation); put64(dst+40,h->object_generation);
    for (uint32_t i = 0; i < h->payload_bytes; ++i) dst[48+i]=payload[i];
    *out_len=n;
    return G5_IPC_OK;
}
enum g5_ipc_status g5_ipc_decode(
    const uint8_t *src, size_t n,
    struct g5_ipc_header *out, const uint8_t **payload
) {
    if (src == NULL || out == NULL || payload == NULL) return G5_IPC_BAD_ARGUMENT;
    *payload=NULL;
    if (n < G5_IPC_WIRE_HEADER_BYTES) return G5_IPC_BAD_FORMAT;
    if (n > G5_IPC_WIRE_MAX_BYTES) return G5_IPC_TOO_LARGE;
    if (src[0]!='G'||src[1]!='5'||src[2]!='I'||src[3]!='P')
        return G5_IPC_BAD_FORMAT;
    struct g5_ipc_header tmp = {
        .major=get16(src+4), .minor=get16(src+6),
        .header_bytes=get16(src+8), .kind=get16(src+10),
        .operation=get32(src+12), .flags=get32(src+16),
        .payload_bytes=get32(src+20), .request_id=get64(src+24),
        .session_generation=get64(src+32), .object_generation=get64(src+40)
    };
    enum g5_ipc_status status=check_header(&tmp);
    if (status != G5_IPC_OK) return status;
    if (n != G5_IPC_WIRE_HEADER_BYTES + (size_t)tmp.payload_bytes)
        return G5_IPC_BAD_FORMAT;
    *out=tmp;
    *payload=src+G5_IPC_WIRE_HEADER_BYTES;
    return G5_IPC_OK;
}

enum g5_ipc_status g5_ipc_decode_received(
    const struct aurora_sys_ipc_received *msg,
    struct g5_ipc_header *out,
    const uint8_t **payload
) {
    if (msg == NULL || out == NULL || payload == NULL)
        return G5_IPC_BAD_ARGUMENT;
    *payload = NULL;
    if (msg->length > G5_IPC_WIRE_MAX_BYTES)
        return G5_IPC_TOO_LARGE;
    if (msg->capability_count > G5_IPC_WIRE_MAX_CAPS)
        return G5_IPC_BAD_CAPABILITIES;
    for (uint32_t i = 0; i < msg->capability_count; ++i) {
        if (msg->capabilities[i] == 0)
            return G5_IPC_BAD_CAPABILITIES;
        for (uint32_t j = 0; j < i; ++j)
            if (msg->capabilities[i] == msg->capabilities[j])
                return G5_IPC_BAD_CAPABILITIES;
    }
    return g5_ipc_decode(msg->data, msg->length, out, payload);
}
