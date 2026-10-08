#include <aurora/g5_session_context.h>
#include <stddef.h>
#include <stdint.h>

bool g5_session_context_begin(struct g5_session_context *ctx, uint64_t generation) {
    if (ctx == NULL || generation == 0u || ctx->active ||
        generation <= ctx->last_revoked_generation) return false;
    ctx->generation = generation;
    ctx->revision = 1u;
    ctx->active = true;
    return true;
}

void g5_session_context_revoke(struct g5_session_context *ctx) {
    if (ctx == NULL) return;
    if (ctx->generation > ctx->last_revoked_generation)
        ctx->last_revoked_generation = ctx->generation;
    ctx->generation = 0u;
    ctx->revision = 0u;
    ctx->active = false;
}

bool g5_session_context_authorized(const struct g5_session_context *ctx,
                                   uint64_t generation) {
    return ctx != NULL && ctx->active && generation != 0u &&
           ctx->generation == generation;
}

bool g5_session_context_advance(struct g5_session_context *ctx,
                                uint64_t generation, uint64_t expected_revision,
                                uint64_t *out_revision) {
    if (out_revision != NULL) *out_revision = 0u;
    if (!g5_session_context_authorized(ctx, generation) ||
        expected_revision == 0u || expected_revision != ctx->revision ||
        expected_revision == UINT64_MAX) return false;
    ++ctx->revision;
    if (out_revision != NULL) *out_revision = ctx->revision;
    return true;
}
