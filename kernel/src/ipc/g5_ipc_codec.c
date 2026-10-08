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

bool g5_ipc_opcode_known(uint32_t operation) {
    switch (operation) {
        case G5_OP_SHELL_READY:
        case G5_OP_SHELL_HEALTH:
        case G5_OP_WINDOW_CONFIGURE:
        case G5_OP_WINDOW_CONFIGURE_ACK:
        case G5_OP_WINDOW_PLACE:
        case G5_OP_WINDOW_CLOSE:
        case G5_OP_SCENE_PREPARE:
        case G5_OP_SCENE_PUBLISH:
            return true;
        default: return false;
    }
}

enum g5_ipc_status g5_ipc_validate_caps(
    const struct aurora_sys_ipc_received *received,
    const struct g5_ipc_cap_requirement *requirements,
    uint32_t count,
    g5_ipc_cap_check_fn checker,
    void *context
) {
    if (received == NULL || count > G5_IPC_WIRE_MAX_CAPS ||
        received->capability_count > G5_IPC_WIRE_MAX_CAPS)
        return G5_IPC_BAD_ARGUMENT;
    if (received->capability_count != count)
        return G5_IPC_BAD_CAPABILITIES;
    if (count != 0 && (requirements == NULL || checker == NULL))
        return G5_IPC_DENIED;
    for (uint32_t i = 0; i < count; ++i) {
        if (received->capabilities[i] == 0 ||
            requirements[i].type <= AURORA_CAP_NONE ||
            requirements[i].type >= AURORA_CAP_TYPE_COUNT)
            return G5_IPC_BAD_CAPABILITIES;
        for (uint32_t j = 0; j < i; ++j)
            if (received->capabilities[i] == received->capabilities[j])
                return G5_IPC_BAD_CAPABILITIES;
        if (!checker(context, received->capabilities[i],
                     requirements[i].type, requirements[i].rights))
            return G5_IPC_DENIED;
    }
    return G5_IPC_OK;
}

enum g5_ipc_status g5_ipc_validate_schema(const struct g5_ipc_header *h, uint32_t caps) {
 if (!h) return G5_IPC_BAD_ARGUMENT;
 enum g5_ipc_status s=check_header(h); if(s!=G5_IPC_OK)return s;
 if(!g5_ipc_opcode_known(h->operation))return G5_IPC_UNSUPPORTED_OPERATION;
 if(caps) return G5_IPC_BAD_CAPABILITIES;
 uint32_t n=0; uint16_t k=G5_IPC_REQUEST;
 switch(h->operation){
 case G5_OP_SHELL_READY:k=G5_IPC_EVENT;break;
 case G5_OP_SHELL_HEALTH:break;
 case G5_OP_WINDOW_CONFIGURE:case G5_OP_WINDOW_CONFIGURE_ACK:case G5_OP_SCENE_PREPARE:case G5_OP_SCENE_PUBLISH:n=16;break;
 case G5_OP_WINDOW_PLACE:n=24;break;
 case G5_OP_WINDOW_CLOSE:n=8;break;
 default:return G5_IPC_UNSUPPORTED_OPERATION;
 }
 return (h->payload_bytes==n && h->kind==k)?G5_IPC_OK:G5_IPC_BAD_FORMAT;
}

/* First-pass v1 payload semantics; retain explicit little-endian decoding. */
enum g5_ipc_status g5_ipc_validate_semantics(
    const struct g5_ipc_header *h,const uint8_t *p
) {
 if(h==NULL)return G5_IPC_BAD_ARGUMENT;
 enum g5_ipc_status shape=g5_ipc_validate_schema(h,0);
 if(shape!=G5_IPC_OK)return shape;
 if(h->payload_bytes && p==NULL)return G5_IPC_BAD_ARGUMENT;
 switch(h->operation) {
 case G5_OP_SHELL_READY:
 case G5_OP_SHELL_HEALTH:
  return h->object_generation==0?G5_IPC_OK:G5_IPC_BAD_FORMAT;
 case G5_OP_WINDOW_CONFIGURE:
  if(h->object_generation==0 || get64(p)==0 ||
     get32(p+8)==0 || get32(p+8)>8192 ||
     get32(p+12)==0 || get32(p+12)>8192)
   return G5_IPC_BAD_FORMAT;
  break;
 case G5_OP_WINDOW_CONFIGURE_ACK:
  if(h->object_generation==0 || get64(p)==0 ||
     get64(p+8)!=h->object_generation)
   return G5_IPC_BAD_FORMAT;
  break;
 case G5_OP_WINDOW_PLACE: {
  int64_t x=(int32_t)get32(p), y=(int32_t)get32(p+4);
  if(h->object_generation==0 || x < -1000000 || x > 1000000 ||
     y < -1000000 || y > 1000000 ||
     get32(p+8)==0 || get32(p+8)>8192 ||
     get32(p+12)==0 || get32(p+12)>8192 ||
     get64(p+16)==0)
   return G5_IPC_BAD_FORMAT;
  break;
 }
 case G5_OP_WINDOW_CLOSE:
  if(h->object_generation==0 || get64(p)>3)
   return G5_IPC_BAD_FORMAT;
  break;
 case G5_OP_SCENE_PREPARE:
 case G5_OP_SCENE_PUBLISH:
  if(h->object_generation==0 || get64(p)==0 || get64(p+8)==0)
   return G5_IPC_BAD_FORMAT;
  break;
 default:
  return G5_IPC_UNSUPPORTED_OPERATION;
 }
 return G5_IPC_OK;
}

uint64_t g5_ipc_opcode_required_rights(uint32_t operation) {
 switch(operation) {
 case G5_OP_SHELL_READY:
 case G5_OP_SHELL_HEALTH:
 case G5_OP_WINDOW_CONFIGURE_ACK:
  return AURORA_RIGHT_READ;
 case G5_OP_WINDOW_CONFIGURE:
 case G5_OP_WINDOW_PLACE:
 case G5_OP_WINDOW_CLOSE:
  return AURORA_RIGHT_CONTROL;
 case G5_OP_SCENE_PREPARE:
 case G5_OP_SCENE_PUBLISH:
  return AURORA_RIGHT_CONTROL|AURORA_RIGHT_WRITE;
 default:
  return 0;
 }
}
