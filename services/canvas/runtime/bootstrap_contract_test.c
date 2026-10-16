#include <aurora/g5_canvas_bootstrap.h>
#include <stdint.h>

/* Host-side contract tests; no IPC capability is implied by a valid frame. */
int main(void) {
    const uint64_t generation = UINT64_C(0x1020304050607080);
    struct g5_canvas_bootstrap message = {
        G5_CANVAS_BOOTSTRAP_VERSION,
        G5_CANVAS_BOOTSTRAP_READY,
        generation,
        G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID
    };
    if (!g5_canvas_bootstrap_ready_valid(&message, generation)) return 1;
    if (g5_canvas_bootstrap_ready_valid(0, generation)) return 10;
    message.request_id = 2u;
    if (g5_canvas_bootstrap_ready_valid(&message, generation)) return 11;
    message.request_id = G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID;
    if (g5_canvas_bootstrap_ready_valid(&message, generation + 1u)) return 2;
    if (g5_canvas_bootstrap_ready_valid(&message, 0u)) return 3;
    message.version++;
    if (g5_canvas_bootstrap_ready_valid(&message, generation)) return 4;
    message.version = G5_CANVAS_BOOTSTRAP_VERSION;
    message.type = G5_CANVAS_BOOTSTRAP_HEALTH_ACK;
    if (g5_canvas_bootstrap_ready_valid(&message, generation)) return 5;
    message.request_id = 17u;
    if (!g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 17u)) return 6;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 18u)) return 7;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 0u)) return 8;
    message.type = G5_CANVAS_BOOTSTRAP_SHUTDOWN_ACK;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 17u)) return 12;
    message.type = G5_CANVAS_BOOTSTRAP_HEALTH_ACK;
    message.request_id = 0u;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 17u)) return 13;
    message.request_id = 17u;
    message.session_generation = generation + 1u;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 17u)) return 14;
    message.session_generation = 0u;
    if (g5_canvas_bootstrap_valid(&message,
            G5_CANVAS_BOOTSTRAP_HEALTH_ACK, generation, 17u)) return 9;
    message.session_generation = generation;
    message.type = 0u;
    if (g5_canvas_bootstrap_valid(&message, 0u, generation, 17u))
        return 15;
    message.type = UINT32_MAX;
    if (g5_canvas_bootstrap_valid(&message, UINT32_MAX, generation, 17u))
        return 16;
    if (g5_canvas_bootstrap_known_type(0u) ||
        g5_canvas_bootstrap_known_type(UINT32_MAX) ||
        !g5_canvas_bootstrap_known_type(G5_CANVAS_BOOTSTRAP_READY) ||
        !g5_canvas_bootstrap_known_type(G5_CANVAS_BOOTSTRAP_SHUTDOWN_ACK))
        return 17;
    return 0;
}
