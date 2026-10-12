#include <aurora/g5_canvas_projection.h>
#include <math.h>
#include <limits.h>

/* Safe opposite-extreme world subtraction before conversion to double. */
__extension__ typedef __int128 g5_proj_wide_t;

static bool request_valid(const struct g5_camera *camera,
                          uint64_t camera_revision,
                          uint64_t scene_revision,
                          uint64_t expected_scene,
                          uint64_t session_generation) {
    return g5_camera_valid(camera) &&
        camera_revision == camera->revision &&
        scene_revision != 0u && scene_revision == expected_scene &&
        session_generation != 0u;
}

static double project(int64_t position,int64_t anchor,double offset,
                      double magnification,double midpoint) {
    const g5_proj_wide_t delta=(g5_proj_wide_t)position-
                                (g5_proj_wide_t)anchor;
    return midpoint+((double)delta/1024.0-offset)*magnification;
}

static bool axis_clip(double first,double last,uint32_t viewport,
                      int32_t *start,uint32_t *length,bool *overlaps) {
    if (!isfinite(first)||!isfinite(last)||!start||!length||!overlaps||
        first>last) return false;
    if (last < -1.0 || first > (double)viewport+1.0) {
        *overlaps=false; return true;
    }
    /* Inflated AABB prevents false negatives when rounding a tiny panel.
     * Clip in double domain BEFORE converting to compositor int32 pixels. */
    const double clipped_first=fmax(0.0,first-1.0);
    const double clipped_last=fmin((double)viewport,last+1.0);
    if (clipped_last < clipped_first) {
        *overlaps=false; return true;
    }
    const double low=floor(clipped_first);
    const double high=ceil(clipped_last);
    if (low<0.0 || high>(double)viewport || high<low ||
        high>(double)INT32_MAX) return false;
    *start=(int32_t)low;
    *length=(uint32_t)(high-low);
    /* A single world point at pixel boundary gets a 1px rectangle. */
    if (*length==0u) {
        if (*start==(int32_t)viewport && *start>0) {
            --*start;
        }
        *length=1u;
    }
    *overlaps=true;
    return true;
}

bool g5_canvas_project_panel(const struct g5_camera *camera,
    uint64_t expected_camera_revision,uint64_t scene_revision,
    uint64_t expected_scene_revision,uint64_t session_generation,
    const struct g5_canvas_panel *panel,
    struct g5_canvas_projected_panel *out,bool *out_visible) {
    if (!request_valid(camera,expected_camera_revision,scene_revision,
                       expected_scene_revision,session_generation) ||
        !panel || !out || !out_visible ||
        panel->object_id==0u || panel->object_generation==0u ||
        !g5_world_aabb_valid(&panel->bounds)) return false;
    const double factor=camera->zoom*camera->device_scale;
    const double mid_x=(double)camera->width_px*0.5;
    const double mid_y=(double)camera->height_px*0.5;
    const double left=project(panel->bounds.min.x,camera->anchor.x,
                             camera->offset_x_units,factor,mid_x);
    const double right=project(panel->bounds.max.x,camera->anchor.x,
                              camera->offset_x_units,factor,mid_x);
    const double top=project(panel->bounds.min.y,camera->anchor.y,
                            camera->offset_y_units,factor,mid_y);
    const double bottom=project(panel->bounds.max.y,camera->anchor.y,
                               camera->offset_y_units,factor,mid_y);
    struct g5_canvas_projected_panel result={0};
    bool horizontal=false,vertical=false;
    if (!axis_clip(left,right,camera->width_px,
                   &result.x_px,&result.width_px,&horizontal) ||
        !axis_clip(top,bottom,camera->height_px,
                   &result.y_px,&result.height_px,&vertical))
        return false;
    if (!horizontal || !vertical) {
        *out_visible=false;
        return true;
    }
    result.object_id=panel->object_id;
    result.object_generation=panel->object_generation;
    *out=result;
    *out_visible=true;
    return true;
}
bool g5_canvas_project_batch(const struct g5_camera *camera,
    uint64_t expected_camera_revision,uint64_t scene_revision,
    uint64_t expected_scene_revision,uint64_t session_generation,
    const struct g5_canvas_panel *panels,size_t count,
    struct g5_canvas_projected_panel *out,size_t capacity,
    size_t *out_count) {
    if (!request_valid(camera,expected_camera_revision,scene_revision,
                       expected_scene_revision,session_generation) ||
        count>G5_CANVAS_PROJECTION_MAX ||
        capacity>G5_CANVAS_PROJECTION_MAX || capacity<count ||
        !out_count || (count && (!panels || !out)))
        return false;

    /* Local staging ensures no partial publication; 64 is a hard bound. */
    struct g5_canvas_projected_panel staged[G5_CANVAS_PROJECTION_MAX];
    size_t used=0u;
    for (size_t i=0u;i<count;++i) {
        for (size_t j=0u;j<i;++j)
            if (panels[i].object_id==panels[j].object_id)
                return false;
        struct g5_canvas_projected_panel projected;
        bool visible=false;
        if (!g5_canvas_project_panel(camera,expected_camera_revision,
                scene_revision,expected_scene_revision,session_generation,
                &panels[i],&projected,&visible))
            return false;
        if (visible) staged[used++]=projected;
    }
    for (size_t i=0u;i<used;++i) out[i]=staged[i];
    *out_count=used;
    return true;
}
