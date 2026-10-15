#include <aurora/g5_canvas_service.h>
#include <aurora/g5_canvas_runtime_image.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>

bool g5_canvas_service_prepare(struct g5_canvas_service *service,
                               uint64_t generation) {
    if (!service || !generation || service->process ||
        service->thread_id || service->session_generation)
        return false;
    const uint8_t *image=g5_canvas_runtime_image();
    size_t size=g5_canvas_runtime_image_size();
    if (!image || !size) return false;
    struct aurora_process *process=process_create_image(
        "g5-canvas-engine",image,size);
    if (!process) return false;
    service->process=process;
    service->session_generation=generation;
    return true;
}

bool g5_canvas_service_start(struct g5_canvas_service *service) {
    if (!service || !service->process || !service->session_generation ||
        service->thread_id) return false;
    service->thread_id=scheduler_create_user_thread(
        "g5-canvas-engine",service->process);
    return service->thread_id!=0u;
}

bool g5_canvas_service_reap(struct g5_canvas_service *service) {
    if (!service || !service->process) return false;
    if (service->thread_id) {
        if (!scheduler_thread_finished(service->thread_id)) return false;
        if (!scheduler_reap_thread(service->thread_id)) return false;
        service->thread_id=0u;
    }
    if (process_live_thread_count(service->process)!=0u) return false;
    if (process_state(service->process)==AURORA_PROCESS_RUNNING)
        process_mark_exited(service->process,0);
    if (!process_reap(service->process,NULL) ||
        !process_release(service->process)) return false;
    service->process=NULL;
    service->session_generation=0u;
    return true;
}
