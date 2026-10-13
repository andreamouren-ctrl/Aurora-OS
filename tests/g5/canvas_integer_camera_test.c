#include <aurora/g5_canvas_integer_camera.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void) {
    struct g5_integer_camera c={0},saved;
    int32_t x=0,y=0;
    int64_t wx=0,wy=0;
    assert(!g5_icam_init(&c,0,0,0,800,600));
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_project(&c,0,0,&x,&y) && x==400 && y==300);
    assert(g5_icam_unproject(&c,c.revision,400,300,&wx,&wy) && wx==0 && wy==0);
    saved=c;
    assert(!g5_icam_pan(&c,65537,0));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_pan(&c,8,-4));
    assert(c.center_x_ticks==-8192 && c.center_y_ticks==4096 && c.revision==2);
    assert(!g5_icam_unproject(&c,1,400,300,&wx,&wy));
    assert(g5_icam_project(&c,0,0,&x,&y) && x==408 && y==296);
    assert(g5_icam_unproject(&c,2,x,y,&wx,&wy) && wx==0 && wy==0);
    c.revision=UINT64_MAX;
    saved=c;
    assert(!g5_icam_pan(&c,1,1) && c.revision==saved.revision);
    assert(g5_icam_init(&c,INT64_MAX,INT64_MIN,G5_ICAM_MIN_ZOOM,800,600));
    saved=c;
    assert(!g5_icam_pan(&c,-65536,65536));
    assert(c.center_x_ticks==saved.center_x_ticks &&
           c.center_y_ticks==saved.center_y_ticks && c.revision==saved.revision);
    assert(!g5_icam_project(&c,INT64_MIN,INT64_MAX,&x,&y));
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_zoom_at(&c,2*G5_ICAM_ONE,400,300));
    assert(c.zoom_q16==2*G5_ICAM_ONE && c.revision==2);
    assert(c.center_x_ticks==0 && c.center_y_ticks==0);
    assert(g5_icam_project(&c,1024,0,&x,&y) && x==402 && y==300);
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    assert(g5_icam_zoom_at(&c,2*G5_ICAM_ONE,500,350));
    assert(c.center_x_ticks==51200 && c.center_y_ticks==25600);
    assert(g5_icam_unproject(&c,c.revision,500,350,&wx,&wy));
    assert(wx==102400 && wy==51200);
    saved=c;
    assert(!g5_icam_zoom_at(&c,0,500,350));
    assert(!g5_icam_zoom_at(&c,2*G5_ICAM_ONE,-1,350));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_init(&c,INT64_MAX,0,G5_ICAM_ONE,800,600));
    saved=c;
    assert(!g5_icam_zoom_at(&c,2*G5_ICAM_ONE,800,300));
    assert(c.revision==saved.revision && c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    saved=c;
    assert(!g5_icam_set_viewport(&c,0,600));
    assert(!g5_icam_set_viewport(&c,16385,600));
    assert(c.revision==saved.revision && c.width_px==saved.width_px);
    assert(g5_icam_set_viewport(&c,801,601));
    assert(c.revision==2 && c.width_px==801 && c.height_px==601);
    assert(!g5_icam_unproject(&c,1,400,300,&wx,&wy));
    c.revision=UINT64_MAX;
    assert(!g5_icam_set_viewport(&c,800,600));
    /* Odd viewport midpoint: positive and negative half pixels must
     * round away from zero in screen coordinates, not in deltas. */
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,801,601));
    assert(g5_icam_project(&c,0,0,&x,&y) && x==401 && y==301);
    assert(g5_icam_project(&c,-1024,-1024,&x,&y) && x==400 && y==300);
    assert(g5_icam_project(&c,1024,1024,&x,&y) && x==402 && y==302);
    /* Deterministic matrix across even/odd viewports and zoom limits.
     * Projected screen coordinates must remain monotonic with world ticks. */
    const uint32_t zoom_matrix[]={
        G5_ICAM_MIN_ZOOM,G5_ICAM_ONE/2,G5_ICAM_ONE,
        G5_ICAM_ONE*2,G5_ICAM_MAX_ZOOM
    };
    for (unsigned zi=0;zi<sizeof(zoom_matrix)/sizeof(zoom_matrix[0]);++zi) {
        for (uint32_t extent=799;extent<=802;++extent) {
            assert(g5_icam_init(&c,123456,-654321,zoom_matrix[zi],
                                extent,extent-200));
            int32_t last=INT32_MIN;
            for (int32_t step=-200;step<=200;++step) {
                const int64_t wx0=c.center_x_ticks+(int64_t)step*1024;
                assert(g5_icam_project(&c,wx0,c.center_y_ticks,&x,&y));
                assert(x>=last);
                last=x;
            }
            assert(g5_icam_unproject(&c,c.revision,
                                     (int32_t)(extent/2),0,&wx,&wy));
            saved=c;
            assert(!g5_icam_zoom_at(&c,0,0,0));
            assert(c.revision==saved.revision &&
                   c.zoom_q16==saved.zoom_q16);
        }
    }
    /* Ring 3 forwarding is not a kernel commit: a staged pan must never
     * advance the local authoritative camera until confirmed state arrives. */
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,160,96));
    saved=c;
    struct g5_integer_camera staged=c;
    assert(g5_icam_pan(&staged,8,-8));
    assert(staged.revision==saved.revision+1u);
    assert(staged.center_x_ticks==-8192 && staged.center_y_ticks==8192);
    assert(c.revision==saved.revision &&
           c.center_x_ticks==saved.center_x_ticks &&
           c.center_y_ticks==saved.center_y_ticks);
    /* Failed staging must also leave the source unchanged. */
    staged=c;
    assert(!g5_icam_pan(&staged,65537,0));
    assert(staged.revision==c.revision &&
           staged.center_x_ticks==c.center_x_ticks);
    /* Full camera snapshot handoff is atomic and rejects stale responses. */
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    saved=c;
    staged=c;
    assert(g5_icam_zoom_at(&staged,2*G5_ICAM_ONE,500,350));
    assert(!g5_icam_accept_snapshot(&c,0,&staged));
    assert(c.zoom_q16==saved.zoom_q16 && c.revision==saved.revision);
    struct g5_integer_camera invalid=staged;
    invalid.zoom_q16=0;
    assert(!g5_icam_accept_snapshot(&c,saved.revision,&invalid));
    assert(c.center_x_ticks==saved.center_x_ticks);
    assert(g5_icam_accept_snapshot(&c,saved.revision,&staged));
    assert(c.zoom_q16==2*G5_ICAM_ONE && c.center_x_ticks==51200);
    assert(!g5_icam_accept_snapshot(&c,saved.revision,&staged));
    /* A valid-looking but out-of-order or resized snapshot must not
     * replace the committed viewport, even if its revision is newer. */
    invalid=c;
    invalid.revision=c.revision+2u;
    assert(!g5_icam_accept_snapshot(&c,c.revision,&invalid));
    invalid=c;
    invalid.revision=c.revision+1u;
    invalid.width_px=801u;
    assert(!g5_icam_accept_snapshot(&c,c.revision,&invalid));
    assert(c.width_px==800u && c.revision==staged.revision);
    invalid=c;
    invalid.revision=UINT64_MAX;
    assert(!g5_icam_accept_snapshot(&c,c.revision,&invalid));
    assert(c.zoom_q16==staged.zoom_q16 && c.revision==staged.revision);
    /* A world scene is projected as one transaction; pan never changes
     * world rectangles, and a rejected batch leaves outputs untouched. */
    assert(g5_icam_init(&c,0,0,G5_ICAM_ONE,800,600));
    const struct g5_icam_scene_item scene[2]={
        {{0,0,102400u,51200u},11u},
        {{1024000,0,102400u,51200u},12u}
    };
    struct g5_icam_projected_item frame[2]={0};
    assert(g5_icam_project_scene(&c,c.revision,scene,2u,frame));
    assert(frame[0].visible && frame[0].projected.x_px==400);
    assert(!frame[1].visible && frame[1].clip.width_px==0);
    const int32_t initial_x=frame[0].projected.x_px;
    assert(g5_icam_pan(&c,10,0));
    assert(g5_icam_project_scene(&c,c.revision,scene,2u,frame));
    assert(frame[0].projected.x_px==initial_x+10);
    assert(scene[0].world.x_ticks==0 && scene[1].world.x_ticks==1024000);
    const struct g5_icam_projected_item prior=frame[0];
    struct g5_icam_scene_item bad_scene[2]={scene[0],scene[1]};
    bad_scene[1].surface_id=11u;
    assert(!g5_icam_project_scene(&c,c.revision,bad_scene,2u,frame));
    assert(frame[0].projected.x_px==prior.projected.x_px);
    bad_scene[1].surface_id=12u;
    bad_scene[1].world.x_ticks=INT64_MAX;
    assert(!g5_icam_project_scene(&c,c.revision,bad_scene,2u,frame));
    assert(frame[0].projected.x_px==prior.projected.x_px);
    assert(!g5_icam_project_scene(&c,c.revision-1u,scene,2u,frame));
    assert(!g5_icam_project_scene(&c,c.revision,scene,17u,frame));
    /* Focus must use the presented clipped snapshot and topmost order. */
    uint64_t focus=UINT64_MAX;
    assert(g5_icam_hit_projected_scene(&c,c.revision,frame,2u,
                                        410,300,&focus));
    assert(focus==11u);
    assert(g5_icam_hit_projected_scene(&c,c.revision,frame,2u,
                                        0,0,&focus));
    assert(focus==0u);
    assert(!g5_icam_hit_projected_scene(&c,c.revision-1u,frame,2u,
                                         410,300,&focus));
    assert(focus==0u);
    assert(!g5_icam_hit_projected_scene(&c,c.revision,frame,2u,
                                         -1,300,&focus));
    struct g5_icam_projected_item overlapping[2]={frame[0],frame[0]};
    overlapping[1].surface_id=13u;
    assert(g5_icam_hit_projected_scene(&c,c.revision,overlapping,2u,
                                        410,300,&focus));
    assert(focus==13u);
    overlapping[1].clip.width_px=-1;
    assert(!g5_icam_hit_projected_scene(&c,c.revision,overlapping,2u,
                                         410,300,&focus));
    /* Submission/ACK pair must fence generation, scene, request and frame.
     * A malformed batch must leave the prior submission untouched. */
    struct g5_icam_scene_submission submitted={0};
    assert(g5_icam_prepare_submission(&c,c.revision,7u,9u,11u,
                                      scene,2u,&submitted));
    assert(submitted.count==2u && submitted.camera.revision==c.revision);
    assert(submitted.items[0].surface_id==11u);
    assert(g5_icam_match_presented(&submitted,7u,9u,11u,
                                    c.revision,101u,100u));
    assert(!g5_icam_match_presented(&submitted,8u,9u,11u,
                                     c.revision,101u,100u));
    assert(!g5_icam_match_presented(&submitted,7u,10u,11u,
                                     c.revision,101u,100u));
    assert(!g5_icam_match_presented(&submitted,7u,9u,12u,
                                     c.revision,101u,100u));
    assert(!g5_icam_match_presented(&submitted,7u,9u,11u,
                                     c.revision-1u,101u,100u));
    assert(!g5_icam_match_presented(&submitted,7u,9u,11u,
                                     c.revision,100u,100u));
    const uint64_t original_request=submitted.request_id;
    assert(!g5_icam_prepare_submission(&c,c.revision,7u,9u,12u,
                                        bad_scene,2u,&submitted));
    assert(submitted.request_id==original_request);
    assert(!g5_icam_prepare_submission(&c,c.revision-1u,7u,9u,12u,
                                        scene,2u,&submitted));
    assert(submitted.request_id==original_request);
    /* Focus is gated on the exact scene/camera snapshot displayed. */
    focus=UINT64_MAX;
    assert(g5_icam_hit_presented_submission(
        &submitted,&c,9u,101u,410,300,&focus));
    assert(focus==11u);
    assert(!g5_icam_hit_presented_submission(
        &submitted,&c,10u,101u,410,300,&focus));
    assert(!g5_icam_hit_presented_submission(
        &submitted,&c,9u,0u,410,300,&focus));
    struct g5_integer_camera moved_camera=c;
    assert(g5_icam_pan(&moved_camera,1,0));
    assert(!g5_icam_hit_presented_submission(
        &submitted,&moved_camera,9u,101u,410,300,&focus));
    moved_camera=c;
    moved_camera.center_x_ticks+=1024;
    assert(!g5_icam_hit_presented_submission(
        &submitted,&moved_camera,9u,101u,410,300,&focus));
    /* Presenting a staged camera advances the Shell camera and frame
     * serial together. Replays and failed acknowledgements are inert. */
    struct g5_integer_camera next_camera=c;
    assert(g5_icam_pan(&next_camera,8,0));
    struct g5_icam_scene_submission next_frame={0};
    assert(g5_icam_prepare_submission(&next_camera,next_camera.revision,
                                      7u,10u,12u,scene,2u,&next_frame));
    uint64_t last_serial=101u;
    assert(!g5_icam_commit_presented_camera(&c,&last_serial,&next_frame,
                                             7u,10u,12u,next_camera.revision,
                                             101u));
    assert(c.revision+1u==next_camera.revision && last_serial==101u);
    assert(g5_icam_commit_presented_camera(&c,&last_serial,&next_frame,
                                            7u,10u,12u,next_camera.revision,
                                            102u));
    assert(c.revision==next_camera.revision && last_serial==102u);
    assert(!g5_icam_commit_presented_camera(&c,&last_serial,&next_frame,
                                             7u,10u,12u,next_camera.revision,
                                             103u));
    assert(c.revision==next_camera.revision && last_serial==102u);
    /* Third integration slice: bounded LE scene serialization, roundtrip,
     * malformed length, replayed surface IDs and output atomicity. */
    uint8_t packet[G5_ICAM_WIRE_BATCH_CAPACITY]={0};
    uint32_t packet_size=0u;
    assert(g5_icam_encode_batch(&next_frame,packet,sizeof(packet),&packet_size));
    assert(packet_size==G5_ICAM_WIRE_BATCH_HEADER+
                        2u*G5_ICAM_WIRE_BATCH_ITEM);
    struct g5_icam_scene_submission decoded={0};
    assert(g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
    assert(decoded.session_generation==7u && decoded.request_id==12u);
    assert(decoded.camera.center_x_ticks==next_frame.camera.center_x_ticks);
    assert(decoded.items[0].surface_id==11u && decoded.count==2u);
    assert(!g5_icam_decode_batch(packet,packet_size-1u,7u,12u,&decoded));
    assert(decoded.count==2u);
    assert(!g5_icam_decode_batch(packet,packet_size,0u,12u,&decoded));
    const uint8_t original_id=packet[G5_ICAM_WIRE_BATCH_HEADER+
                                      G5_ICAM_WIRE_BATCH_ITEM];
    packet[G5_ICAM_WIRE_BATCH_HEADER+G5_ICAM_WIRE_BATCH_ITEM]=11u;
    assert(!g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
    packet[G5_ICAM_WIRE_BATCH_HEADER+G5_ICAM_WIRE_BATCH_ITEM]=original_id;
    assert(!g5_icam_encode_batch(&next_frame,packet,packet_size-1u,
                                 &packet_size));
    /* Malformed crop must fail without mutating the previous decoded frame. */
    assert(g5_icam_encode_batch(&next_frame,packet,sizeof(packet),&packet_size));
    const uint32_t item0=G5_ICAM_WIRE_BATCH_HEADER;
    uint8_t original_crop[4];
    for(unsigned k=0;k<4u;++k)original_crop[k]=packet[item0+40u+k];
    for(unsigned k=0;k<4u;++k)packet[item0+40u+k]=0xffu;
    assert(!g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
    assert(decoded.count==2u && decoded.request_id==12u);
    for(unsigned k=0;k<4u;++k)packet[item0+40u+k]=original_crop[k];
    /* A forged clip position cannot be detached from its source crop. */
    assert(g5_icam_encode_batch(&next_frame,packet,sizeof(packet),&packet_size));
    const uint32_t clip_x_offset=item0+24u;
    packet[clip_x_offset]^=1u;
    assert(!g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
    assert(decoded.count==2u && decoded.request_id==12u);
    packet[clip_x_offset]^=1u;
    /* Encoding over its own submission must not corrupt source state. */
    struct g5_icam_scene_submission alias_frame=next_frame;
    uint32_t alias_written=777u;
    assert(!g5_icam_encode_batch(&alias_frame,(uint8_t *)&alias_frame,
                                  sizeof(alias_frame),&alias_written));
    assert(alias_written==777u);
    assert(!g5_icam_encode_batch(&alias_frame,
                                  (uint8_t *)&alias_frame+8u,
                                  sizeof(alias_frame)-8u,&alias_written));
    assert(alias_written==777u);
    assert(alias_frame.count==next_frame.count);
    assert(alias_frame.items[0].surface_id==next_frame.items[0].surface_id);
    /* Decoder must not accept a wire packet inside its output frame. */
    struct g5_icam_scene_submission overlap_frame={0};
    uint8_t *overlap_bytes=(uint8_t *)&overlap_frame;
    for(uint32_t k=0;k<packet_size;++k)overlap_bytes[k]=packet[k];
    assert(!g5_icam_decode_batch(overlap_bytes,packet_size,7u,12u,
                                 &overlap_frame));
    assert(!g5_icam_decode_batch(overlap_bytes+8u,packet_size,7u,12u,
                                 &overlap_frame));
    /* Wire header integrity: reserved bits, protocol version, and camera
     * revision must fail closed without publishing a partial scene. */
    assert(g5_icam_encode_batch(&next_frame,packet,sizeof(packet),&packet_size));
    const uint32_t header_offsets[]={4u,8u,36u};
    const uint8_t header_values[]={2u,0u,1u};
    for(unsigned k=0;k<3u;++k) {
        const uint32_t off=header_offsets[k];
        const uint8_t saved=packet[off];
        packet[off]=header_values[k];
        assert(!g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
        assert(decoded.count==2u && decoded.request_id==12u);
        packet[off]=saved;
    }
    assert(g5_icam_decode_batch(packet,packet_size,7u,12u,&decoded));
    puts("g5 integer camera host arithmetic gate passed");
    return 0;
}
