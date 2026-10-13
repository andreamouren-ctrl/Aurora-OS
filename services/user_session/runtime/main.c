#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aurora/capability_abi.h>
#include <aurora/syscall_abi.h>
#include <aurora/user_session_host_abi.h>
#include <aurora/g5_ipc_abi.h>
#include <aurora/g5_canvas_integer_camera.h>

/* This translation unit sees both IPC and camera headers. Detect ABI
 * drift without introducing a kernel-header dependency in host camera tests. */
_Static_assert(G5_ICAM_G5_INLINE_BYTES == G5_IPC_WIRE_INLINE_MAX,
               "canvas camera and G5 IPC inline capacities diverged");
_Static_assert(G5_ICAM_WIRE_BATCH_CAPACITY <= G5_IPC_WIRE_INLINE_MAX,
               "canvas scene packet cannot fit the G5 IPC envelope");

static void clear_bytes(void *buffer, size_t size) {
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

static uint64_t syscall1(uint64_t number, uint64_t a1) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall2(uint64_t number, uint64_t a1, uint64_t a2) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall3(
    uint64_t number,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
) {
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a1;
    register uint64_t rsi __asm__("rsi") = a2;
    register uint64_t rdx __asm__("rdx") = a3;
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "rcx", "r11", "memory");
    return rax;
}

static uint64_t syscall5(
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
    __asm__ volatile(
        "syscall"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return rax;
}

static bool capability_has(
    uint64_t handle,
    enum aurora_cap_type type,
    uint64_t rights
) {
    return handle != 0u &&
        syscall3(AURORA_SYS_CAP_CHECK, handle, (uint64_t)type, rights) == 1u;
}

static bool user_id_valid(
    const uint8_t user_id[AURORA_USER_SESSION_HOST_USER_ID_SIZE]
) {
    uint8_t combined = 0u;
    if (user_id == NULL) return false;
    for (size_t i = 0u; i < AURORA_USER_SESSION_HOST_USER_ID_SIZE; ++i) {
        combined |= user_id[i];
    }
    return combined != 0u;
}

static bool send_message(
    uint64_t endpoint,
    uint32_t type,
    uint64_t request_id
) {
    const struct aurora_user_session_host_message message = {
        .version = AURORA_USER_SESSION_HOST_PROTOCOL_VERSION,
        .type = type,
        .request_id = request_id
    };

    return syscall5(
        AURORA_SYS_IPC_SEND,
        endpoint,
        (uint64_t)(uintptr_t)&message,
        sizeof(message),
        0u,
        0u
    ) == 0u;
}

static bool send_g5_ready(uint64_t endpoint,uint64_t generation) {
    /* Fixed-width G5 v1 frame, little-endian, no capability transfer.
     * The kernel validates the sender only via exclusive endpoint grants. */
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_EVENT;
    const uint32_t op=G5_OP_SHELL_READY;
    for(unsigned i=0;i<4u;++i)wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[24]=1u; /* request_id 1 */
    for(unsigned i=0;i<8u;++i)
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}
static bool send_g5_health(uint64_t endpoint,uint64_t generation,
                           uint64_t request_id) {
    /* A second authenticated Ring 3 IPC request, checked by kernel dispatch. */
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_REQUEST;
    const uint32_t op=G5_OP_SHELL_HEALTH;
    for(unsigned i=0;i<4u;++i)wire[12u+i]=(uint8_t)(op>>(8u*i));
    for(unsigned i=0;i<8u;++i)
        wire[24u+i]=(uint8_t)(request_id>>(8u*i));
    for(unsigned i=0;i<8u;++i)
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}
static uint64_t commit_ring3_shell_surface(
    const struct aurora_user_session_host_startup *startup,
    uint64_t *failure_stage
) {
    if (failure_stage) *failure_stage=20u;
    if (startup==NULL || startup->graphics_width==0u ||
        startup->graphics_height==0u ||
        startup->graphics_width>8192u || startup->graphics_height>8192u)
        return 0u;
    uint64_t address=syscall2(AURORA_SYS_GRAPHICS_BUFFER_MAP,
                              startup->graphics_buffer,1u);
    if (address==0u || address==AURORA_SYS_RESULT_ERROR) {
        if (failure_stage) *failure_stage=21u;
        return 0u;
    }
    /* Ring3 owns the actual pixel writes; only the compositor may present. */
    volatile uint32_t *pixels=(volatile uint32_t *)(uintptr_t)address;
    for (uint64_t y=0u;y<startup->graphics_height;++y)
        for (uint64_t x=0u;x<startup->graphics_width;++x) {
            uint32_t r=(uint32_t)((startup->flags ==
                AURORA_USER_SESSION_HOST_FLAG_RENDER_CLIENT ? 130u : 30u)
                +(x*100u/startup->graphics_width));
            uint32_t g=(uint32_t)((startup->flags ==
                AURORA_USER_SESSION_HOST_FLAG_RENDER_CLIENT ? 25u : 48u)
                +(y*110u/startup->graphics_height));
            pixels[y*startup->graphics_width+x]=
                UINT32_C(0xff000000)|(r<<16u)|(g<<8u)|UINT32_C(0x9c);
        }
    if (syscall1(AURORA_SYS_GRAPHICS_BUFFER_UNMAP,address)!=0u) {
        if (failure_stage) *failure_stage=22u;
        return 0u;
    }
    if (syscall2(AURORA_SYS_GRAPHICS_SURFACE_ATTACH,
                 startup->graphics_surface,startup->graphics_buffer)!=0u) {
        if (failure_stage) *failure_stage=23u;
        return 0u;
    }
    if (syscall5(AURORA_SYS_GRAPHICS_SURFACE_DAMAGE,
                 startup->graphics_surface,0u,0u,
                 startup->graphics_width,startup->graphics_height)!=0u) {
        if (failure_stage) *failure_stage=24u;
        return 0u;
    }
    uint64_t commit=syscall1(AURORA_SYS_GRAPHICS_SURFACE_COMMIT,
                             startup->graphics_surface);
    if (commit==0u || commit==AURORA_SYS_RESULT_ERROR) {
        if (failure_stage) *failure_stage=25u;
        return 0u;
    }
    return commit;
}

static bool send_g5_scene_publish(
    uint64_t endpoint,uint64_t generation,uint64_t object_id,
    uint64_t object_generation,uint64_t commit_serial
) {
    if (!endpoint || !generation || !object_id ||
        !object_generation || !commit_serial)return false;
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES+16u]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_REQUEST;
    uint32_t op=G5_OP_SCENE_PUBLISH;
    for (unsigned i=0u;i<4u;++i)
        wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[20]=16u; /* payload_bytes at v1 byte offset 20; flags remain zero */
    wire[24]=3u;
    for (unsigned i=0u;i<8u;++i) {
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
        wire[40u+i]=(uint8_t)(object_generation>>(8u*i));
        wire[48u+i]=(uint8_t)(object_id>>(8u*i));
        wire[56u+i]=(uint8_t)(commit_serial>>(8u*i));
    }
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}

static bool send_g5_window_place(
    uint64_t endpoint,uint64_t generation,
    uint64_t object_id,uint64_t object_generation
) {
    if (!endpoint || !generation || !object_id || !object_generation)
        return false;
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES+24u]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;
    wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_REQUEST;
    uint32_t op=G5_OP_WINDOW_PLACE;
    for (unsigned i=0u;i<4u;++i) wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[20]=24u; /* payload_bytes at v1 byte offset 20; flags remain zero */
    wire[24]=4u; /* monotonic after READY(1), HEALTH(2), PUBLISH(3) */
    for (unsigned i=0u;i<8u;++i) {
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
        wire[40u+i]=(uint8_t)(object_generation>>(8u*i));
        wire[64u+i]=(uint8_t)(object_id>>(8u*i));
    }
    wire[48]=80u; /* position x */
    wire[52]=72u; /* position y */
    wire[56]=160u; /* width */
    wire[60]=96u; /* height */
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}


/* Primary Ring 3 Shell sends a native authenticated control request.
 * Wire layout is little-endian, no C struct cast or capability transfer. */
static bool send_g5_canvas_pan(uint64_t endpoint,uint64_t generation,
    uint64_t object_generation,uint64_t request_id,
    int32_t dx,int32_t dy,uint64_t camera_revision,uint64_t scene_revision) {
    if (!endpoint || !generation || !object_generation || request_id<=4u ||
        !camera_revision || !scene_revision ||
        ((dx==0)&&(dy==0)) || dx>32 || dx< -32 || dy>32 || dy< -32)
        return false;
    uint8_t wire[G5_IPC_WIRE_HEADER_BYTES+24u]={0};
    wire[0]='G';wire[1]='5';wire[2]='I';wire[3]='P';
    wire[4]=G5_IPC_WIRE_MAJOR;wire[8]=G5_IPC_WIRE_HEADER_BYTES;
    wire[10]=G5_IPC_REQUEST;
    const uint32_t op=G5_OP_CANVAS_PAN;
    for(unsigned i=0u;i<4u;++i) wire[12u+i]=(uint8_t)(op>>(8u*i));
    wire[20]=24u;
    for(unsigned i=0u;i<8u;++i) {
        wire[24u+i]=(uint8_t)(request_id>>(8u*i));
        wire[32u+i]=(uint8_t)(generation>>(8u*i));
        wire[40u+i]=(uint8_t)(object_generation>>(8u*i));
        wire[56u+i]=(uint8_t)(camera_revision>>(8u*i));
        wire[64u+i]=(uint8_t)(scene_revision>>(8u*i));
    }
    const uint32_t ux=(uint32_t)dx,uy=(uint32_t)dy;
    for(unsigned i=0u;i<4u;++i) {
        wire[48u+i]=(uint8_t)(ux>>(8u*i));
        wire[52u+i]=(uint8_t)(uy>>(8u*i));
    }
    return syscall5(AURORA_SYS_IPC_SEND,endpoint,
        (uint64_t)(uintptr_t)wire,sizeof(wire),0u,0u)==0u;
}

static bool wait_message(uint64_t endpoint) {
    return syscall2(AURORA_SYS_IPC_WAIT, endpoint, 0u) == 0u;
}

static bool receive_message(
    uint64_t endpoint,
    struct aurora_sys_ipc_received *received
) {
    clear_bytes(received, sizeof(*received));
    return syscall2(
        AURORA_SYS_IPC_RECEIVE,
        endpoint,
        (uint64_t)(uintptr_t)received
    ) == 0u;
}

int64_t user_session_host_main(uint64_t initial_rsp) {
    if (initial_rsp < AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET) return 1;

    const struct aurora_user_session_host_startup *startup =
        (const struct aurora_user_session_host_startup *)(uintptr_t)(
            initial_rsp - AURORA_USER_SESSION_HOST_STARTUP_STACK_OFFSET);
    /* The launcher placed startup below the entry RSP. Copy it into this
     * frame before syscall helpers reuse the stack below the original RSP. */
    struct aurora_user_session_host_startup owned_startup=*startup;
    startup=&owned_startup;
    const bool secondary =
        startup->flags == AURORA_USER_SESSION_HOST_FLAG_RENDER_CLIENT;

    if (startup->abi_version != AURORA_USER_SESSION_HOST_ABI_VERSION ||
        (startup->flags != 0u && !secondary) ||
        (secondary && (startup->g5_endpoint != 0u ||
                       startup->profile_handle != 0u)) ||
        (startup->g5_endpoint != 0u &&
         (!capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_WRITE) ||
          capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_TRANSFER) ||
          capability_has(startup->g5_endpoint,AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ))) ||
        startup->reserved1 != 0u ||
        ((startup->g5_endpoint != 0u || secondary) &&
         (startup->graphics_buffer == 0u ||
          startup->graphics_surface == 0u ||
          startup->graphics_object_id == 0u ||
          startup->graphics_object_generation == 0u ||
          startup->graphics_width != 160u ||
          startup->graphics_height != 96u ||
          !capability_has(startup->graphics_buffer,
               AURORA_CAP_GRAPHICS_BUFFER,
               AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP) ||
          capability_has(startup->graphics_buffer,
               AURORA_CAP_GRAPHICS_BUFFER,AURORA_RIGHT_TRANSFER) ||
          !capability_has(startup->graphics_surface,AURORA_CAP_SURFACE,
               AURORA_RIGHT_READ|AURORA_RIGHT_WRITE) ||
          capability_has(startup->graphics_surface,
               AURORA_CAP_SURFACE,AURORA_RIGHT_TRANSFER))) ||
        startup->control_endpoint == 0u ||
        (!secondary && startup->profile_handle == 0u) ||
        startup->session_generation == 0u ||
        !user_id_valid(startup->user_id)) {
        return 11;
    }

    if (!capability_has(
            startup->control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_READ | AURORA_RIGHT_WRITE) ||
        capability_has(
            startup->control_endpoint,
            AURORA_CAP_IPC_ENDPOINT,
            AURORA_RIGHT_TRANSFER) ||
        (!secondary && !capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_READ |
            AURORA_RIGHT_WRITE |
            AURORA_RIGHT_ENUMERATE)) ||
        (!secondary && capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_TRANSFER)) ||
        (!secondary && capability_has(
            startup->profile_handle,
            AURORA_CAP_FILE,
            AURORA_RIGHT_CONTROL))) {
        return 12;
    }

    /* Per-process Ring 3 camera state. No kernel privilege or FP state. */
    struct g5_integer_camera canvas_camera={0};
    if ((startup->g5_endpoint != 0u || secondary) &&
        !g5_icam_init(&canvas_camera,0,0,G5_ICAM_ONE,
                      startup->graphics_width,startup->graphics_height))
        return 19;

    uint64_t shell_commit=0u;
    if (startup->g5_endpoint != 0u || secondary) {
        uint64_t failure_stage=20u;
        shell_commit=commit_ring3_shell_surface(startup,&failure_stage);
        if (!shell_commit)return (int64_t)failure_stage;
        if (!secondary &&
            !send_g5_ready(startup->g5_endpoint,startup->session_generation))
            return 26;
    }

    /* Publish bootstrap proof before READY: the kernel can consume READY
     * immediately without racing a later BOOTSTRAP_SIGNAL syscall. */
    if (syscall1(AURORA_SYS_BOOTSTRAP_SIGNAL,
                 AURORA_USER_SESSION_HOST_READY_MAGIC) != 0u) return 27;

    /* Queue both G5 messages before publishing control READY, so
     * kernel admission is deterministic rather than a scheduler race. */
    if (startup->g5_endpoint != 0u &&
        !send_g5_health(startup->g5_endpoint,startup->session_generation,2u))
        return 28;
    if (startup->g5_endpoint != 0u &&
        !send_g5_scene_publish(startup->g5_endpoint,
            startup->session_generation,startup->graphics_object_id,
            startup->graphics_object_generation,shell_commit))
        return 29;
    if (startup->g5_endpoint != 0u &&
        !send_g5_window_place(startup->g5_endpoint,
            startup->session_generation,startup->graphics_object_id,
            startup->graphics_object_generation))
        return 30;
    if (!send_message(startup->control_endpoint, AURORA_USER_SESSION_HOST_READY, 0u))
        return 31;
    /* A separate Ring3 process independently wrote and committed its
     * pixels. Its exclusive control endpoint authenticates the receipt. */
    if (secondary && !send_message(startup->control_endpoint,
                                    AURORA_USER_SESSION_HOST_FRAME_COMMITTED,
                                    shell_commit))
        return 32;

    uint64_t last_canvas_pan_request=4u;
    uint64_t pending_canvas_pan_request=0u;
    uint64_t pending_canvas_camera_revision=0u;
    uint64_t pending_canvas_scene_revision=0u;
    struct g5_integer_camera pending_canvas_camera={0};
    uint64_t last_resize_serial=0u;
    uint64_t current_graphics_buffer=startup->graphics_buffer;
    for (;;) {
        if (!wait_message(startup->control_endpoint)) return 1;

        struct aurora_sys_ipc_received received;
        if (!receive_message(startup->control_endpoint, &received) ||
            received.capability_count != 0u) {
            clear_bytes(&received, sizeof(received));
            return 1;
        }
        /* The kernel sends this only after G5 IPC and compositor commit.
         * A forwarded ACK never changes local camera state. */
        if (!secondary && startup->g5_endpoint != 0u &&
            received.length ==
                sizeof(struct aurora_user_session_host_camera_committed)) {
            struct aurora_user_session_host_camera_committed committed={0};
            copy_bytes(&committed,received.data,sizeof(committed));
            if (committed.header.type ==
                    AURORA_USER_SESSION_HOST_CANVAS_PAN_COMMITTED ||
                committed.header.type ==
                    AURORA_USER_SESSION_HOST_CANVAS_PAN_REJECTED) {
                clear_bytes(&received,sizeof(received));
                if (committed.header.version !=
                        AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
                    pending_canvas_pan_request==0u ||
                    committed.header.request_id!=pending_canvas_pan_request ||
                    committed.scene_revision!=pending_canvas_scene_revision)
                    return 1;
                if (committed.header.type ==
                        AURORA_USER_SESSION_HOST_CANVAS_PAN_COMMITTED) {
                    if (committed.camera_revision!=pending_canvas_camera.revision ||
                        !g5_icam_accept_snapshot(&canvas_camera,
                            pending_canvas_camera_revision,
                            &pending_canvas_camera))
                        return 1;
                } else {
                    /* A rejected stale request reports the authoritative
                     * kernel revision, not the stale revision supplied by
                     * the requester. Keep the committed center/zoom and
                     * resynchronize only the revision. Never apply the
                     * speculative pan on rejection. */
                    if (!g5_icam_reconcile_rejected(
                            &canvas_camera,committed.camera_revision))
                        return 1;
                }
                pending_canvas_pan_request=0u;
                continue;
            }
        }
        /* Resize and camera control messages happen to have the same size.
         * Identify the type before treating the bytes as resize metadata. */
        if (!secondary && startup->g5_endpoint != 0u &&
            received.length == sizeof(struct aurora_user_session_host_camera_message)) {
            struct aurora_user_session_host_camera_message pan;
            clear_bytes(&pan,sizeof(pan));
            copy_bytes(&pan,received.data,sizeof(pan));
            if (pan.header.type == AURORA_USER_SESSION_HOST_CANVAS_PAN_REQUEST) {
                clear_bytes(&received,sizeof(received));
                /* Preflight the integer camera transaction without committing
                 * speculative state. Kernel scene policy remains authoritative:
                 * the control ACK only acknowledges forwarding, not acceptance. */
                struct g5_integer_camera proposed_camera=canvas_camera;
                if (pan.header.version != AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
                    pan.header.request_id<=last_canvas_pan_request ||
                    pending_canvas_pan_request!=0u ||
                    pan.expected_camera_revision==0u ||
                    pan.expected_scene_revision==0u ||
                    pan.delta_x_px>32 || pan.delta_x_px< -32 ||
                    pan.delta_y_px>32 || pan.delta_y_px< -32 ||
                    (pan.delta_x_px==0 && pan.delta_y_px==0) ||
                    !g5_icam_pan(&proposed_camera,pan.delta_x_px,pan.delta_y_px) ||
                    !send_g5_canvas_pan(startup->g5_endpoint,
                                        startup->session_generation,
                                        startup->graphics_object_generation,
                                        pan.header.request_id,
                                        pan.delta_x_px,pan.delta_y_px,
                                        pan.expected_camera_revision,
                                        pan.expected_scene_revision) ||
                    !send_message(startup->control_endpoint,
                                  AURORA_USER_SESSION_HOST_CANVAS_PAN_ACK,
                                  pan.header.request_id))
                    return 1;
                last_canvas_pan_request=pan.header.request_id;
                pending_canvas_pan_request=pan.header.request_id;
                pending_canvas_camera_revision=pan.expected_camera_revision;
                pending_canvas_scene_revision=pan.expected_scene_revision;
                pending_canvas_camera=proposed_camera;
                continue;
            }
        }
        if ((secondary || startup->g5_endpoint != 0u) &&
            received.length ==
            sizeof(struct aurora_user_session_host_resize_message)) {
            struct aurora_user_session_host_resize_message request;
            clear_bytes(&request,sizeof(request));
            copy_bytes(&request,received.data,sizeof(request));
            clear_bytes(&received,sizeof(received));
            if (request.header.version !=
                    AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
                request.header.type !=
                    AURORA_USER_SESSION_HOST_RESIZE_PREPARE ||
                request.header.request_id == 0u ||
                request.configure_serial == 0u ||
                request.configure_serial <= last_resize_serial ||
                request.width == 0u || request.height == 0u ||
                request.width > 8192u || request.height > 8192u ||
                request.graphics_buffer == current_graphics_buffer ||
                !capability_has(request.graphics_buffer,
                    AURORA_CAP_GRAPHICS_BUFFER,
                    AURORA_RIGHT_READ|AURORA_RIGHT_WRITE|AURORA_RIGHT_MAP) ||
                capability_has(request.graphics_buffer,
                    AURORA_CAP_GRAPHICS_BUFFER,AURORA_RIGHT_TRANSFER))
                return 1;
            /* The ACK proves the actual Ring3 client accepted the exact
             * configure serial. The subsequent receipt proves its new
             * mapped buffer has been attached, damaged and committed. */
            if (!send_message(startup->control_endpoint,
                              AURORA_USER_SESSION_HOST_RESIZE_ACK,
                              request.configure_serial))
                return 1;
            /* Do not publish the replacement surface before the trusted
             * Window Policy has recorded this ACK. Otherwise scheduling
             * can race the commit baseline and make the new frame unfocusable. */
            if (!wait_message(startup->control_endpoint))
                return 1;
            struct aurora_sys_ipc_received granted;
            if (!receive_message(startup->control_endpoint,&granted) ||
                granted.capability_count!=0u ||
                granted.length!=
                    sizeof(struct aurora_user_session_host_message))
                return 1;
            struct aurora_user_session_host_message grant;
            clear_bytes(&grant,sizeof(grant));
            copy_bytes(&grant,granted.data,sizeof(grant));
            clear_bytes(&granted,sizeof(granted));
            if (grant.version!=AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
                grant.type!=AURORA_USER_SESSION_HOST_RESIZE_COMMIT_GRANT ||
                grant.request_id!=request.configure_serial)
                return 1;
            struct aurora_user_session_host_startup resized=*startup;
            resized.graphics_buffer=request.graphics_buffer;
            resized.graphics_width=request.width;
            resized.graphics_height=request.height;
            uint64_t stage=0u;
            uint64_t commit=commit_ring3_shell_surface(&resized,&stage);
            if (commit==0u ||
                !send_message(startup->control_endpoint,
                              AURORA_USER_SESSION_HOST_FRAME_COMMITTED,
                              commit))
                return 1;
            if (!g5_icam_set_viewport(&canvas_camera,request.width,request.height))
                return 1;
            last_resize_serial=request.configure_serial;
            current_graphics_buffer=request.graphics_buffer;
            continue;
        }
        if (received.length ==
            sizeof(struct aurora_user_session_host_input_message)) {
            struct aurora_user_session_host_input_message input;
            clear_bytes(&input,sizeof(input));
            copy_bytes(&input,received.data,sizeof(input));
            clear_bytes(&received,sizeof(received));
            if (input.header.version !=
                    AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
                input.header.type != AURORA_USER_SESSION_HOST_INPUT_EVENT ||
                input.header.request_id == 0u ||
                input.event.type <= AURORA_INPUT_EVENT_NONE ||
                input.event.type > AURORA_INPUT_EVENT_SCROLL)
                return 1;
            /* This independent Ring3 client has consumed its own input.
             * No reply is sent into the health/control response queue. */
            continue;
        }
        if (received.length !=
            sizeof(struct aurora_user_session_host_message)) {
            clear_bytes(&received,sizeof(received));
            return 1;
        }

        struct aurora_user_session_host_message message;
        clear_bytes(&message, sizeof(message));
        copy_bytes(&message, received.data, sizeof(message));
        clear_bytes(&received, sizeof(received));

        if (message.version != AURORA_USER_SESSION_HOST_PROTOCOL_VERSION ||
            (message.type != AURORA_USER_SESSION_HOST_SHUTDOWN &&
             message.type != AURORA_USER_SESSION_HOST_HEALTH_POLL) ||
            message.request_id == 0u) {
            clear_bytes(&message, sizeof(message));
            return 1;
        }

        uint64_t request_id = message.request_id;
        uint32_t type = message.type;
        clear_bytes(&message, sizeof(message));
        if (type == AURORA_USER_SESSION_HOST_HEALTH_POLL) {
            if ((!secondary && startup->g5_endpoint == 0u) ||
                request_id == UINT64_MAX ||
                (!secondary && !send_g5_health(
                     startup->g5_endpoint,
                     startup->session_generation,request_id)))
                return 1;
            if (!send_message(startup->control_endpoint,
                              AURORA_USER_SESSION_HOST_HEALTH_ACK,request_id))
                return 1;
            continue;
        }
        return send_message(
            startup->control_endpoint,
            AURORA_USER_SESSION_HOST_SHUTDOWN_ACK,
            request_id) ? 0 : 1;
    }
}
