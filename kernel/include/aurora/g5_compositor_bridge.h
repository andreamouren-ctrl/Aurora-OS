#ifndef AURORA_G5_COMPOSITOR_BRIDGE_H
#define AURORA_G5_COMPOSITOR_BRIDGE_H
#include <aurora/g5_frame_delivery.h>
#include <aurora/software_compositor.h>
/* Serialized, trusted compositor coordinator; not a Ring3 service.
 * Three integration gates: capability-backed scene attach, validated frame
 * composition with receipt ACK, and node cleanup on teardown. */
struct g5_compositor_bridge {
 struct g5_frame_delivery *delivery;
 struct aurora_software_compositor *compositor;
 uint64_t generation;
 uint64_t node_ids[G5_SURFACE_REGISTRY_CAPACITY];
 uint64_t attached_object_ids[G5_SURFACE_REGISTRY_CAPACITY];
 uint32_t attached_object_generations[G5_SURFACE_REGISTRY_CAPACITY];
};
bool g5_compositor_bridge_bind(struct g5_compositor_bridge *b,
 struct g5_frame_delivery *delivery,struct aurora_software_compositor *compositor);
bool g5_compositor_bridge_attach(struct g5_compositor_bridge *b,uint32_t slot,
 int32_t x,int32_t y,int32_t z,uint64_t *node_id);
bool g5_compositor_bridge_present(struct g5_compositor_bridge *b,uint32_t slot,
 uint64_t request_id,uint64_t configure_serial,uint64_t *display_serial);
bool g5_compositor_bridge_detach(struct g5_compositor_bridge *b,uint32_t slot);
void g5_compositor_bridge_revoke(struct g5_compositor_bridge *b);
#endif
