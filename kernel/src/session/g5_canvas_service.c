#include <aurora/g5_canvas_service.h>
#include <aurora/g5_canvas_runtime_image.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/capability.h>
#include <aurora/g5_canvas_bootstrap.h>
#include <aurora/usercopy.h>

bool g5_canvas_service_prepare(struct g5_canvas_service *service,
                               uint64_t generation) {
    if (!service || !generation || service->process ||
        service->thread_id || service->session_generation ||
        service->kernel_endpoint || service->control_handle)
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

bool g5_canvas_service_provision_control(struct g5_canvas_service *service) {
    if (!service || !service->process || !service->session_generation ||
        service->thread_id || service->kernel_endpoint ||
        service->control_handle != AURORA_CAP_INVALID)
        return false;
    ipc_channel_init(&service->control_channel);
    struct aurora_ipc_endpoint *kernel_peer =
        ipc_channel_endpoint(&service->control_channel,0u);
    struct aurora_ipc_endpoint *user_peer =
        ipc_channel_endpoint(&service->control_channel,1u);
    if (!kernel_peer || !user_peer) return false;
    aurora_cap_handle handle=cap_grant(
        &service->process->capabilities,user_peer,
        AURORA_CAP_IPC_ENDPOINT,AURORA_RIGHT_READ|AURORA_RIGHT_WRITE);
    if (handle==AURORA_CAP_INVALID) return false;
    const struct g5_canvas_startup startup={
        .abi_version=G5_CANVAS_STARTUP_ABI_VERSION,
        .reserved=0u,
        .control_endpoint=handle,
        .session_generation=service->session_generation
    };
    if (service->process->user_stack_top < G5_CANVAS_STARTUP_STACK_OFFSET ||
        !copy_to_user(service->process,
            service->process->user_stack_top-G5_CANVAS_STARTUP_STACK_OFFSET,
            &startup,sizeof(startup))) {
        (void)cap_revoke(&service->process->capabilities,handle);
        return false;
    }
    service->control_handle=handle;
    service->kernel_endpoint=kernel_peer;
    return true;
}

static void revoke_control(struct g5_canvas_service *service) {
    if (service->control_handle!=AURORA_CAP_INVALID &&
        service->process!=NULL)
        (void)cap_revoke(&service->process->capabilities,
                         service->control_handle);
    service->control_handle=AURORA_CAP_INVALID;
    service->kernel_endpoint=NULL;
}

bool g5_canvas_service_health_probe(struct g5_canvas_service *service,
                                    struct aurora_cap_table *kernel_caps,
                                    uint64_t request_id) {
    if (!service || !service->kernel_endpoint || !kernel_caps ||
        !service->session_generation || !request_id ||
        request_id==G5_CANVAS_BOOTSTRAP_READY_REQUEST_ID)
        return false;
    const struct g5_canvas_bootstrap request={
        .version=G5_CANVAS_BOOTSTRAP_VERSION,
        .type=G5_CANVAS_BOOTSTRAP_HEALTH,
        .session_generation=service->session_generation,
        .request_id=request_id
    };
    return ipc_send(service->kernel_endpoint,kernel_caps,
                    &request,(uint32_t)sizeof(request),NULL,0u);
}

bool g5_canvas_service_receive_health_ack(
    struct g5_canvas_service *service,
    struct aurora_cap_table *kernel_caps,uint64_t request_id) {
    if (!service || !service->kernel_endpoint || !kernel_caps ||
        !request_id || !service->session_generation) return false;
    struct aurora_ipc_received received={0};
    if (!ipc_receive(service->kernel_endpoint,kernel_caps,&received) ||
        received.capability_count!=0u ||
        received.length!=sizeof(struct g5_canvas_bootstrap)) return false;
    struct g5_canvas_bootstrap ack={0};
    for (size_t i=0;i<sizeof(ack);++i)
        ((uint8_t *)&ack)[i]=received.data[i];
    return g5_canvas_bootstrap_valid(&ack,G5_CANVAS_BOOTSTRAP_HEALTH_ACK,
                                      service->session_generation,request_id)!=0;
}

bool g5_canvas_service_start(struct g5_canvas_service *service) {
    if (!service || !service->process || !service->session_generation ||
        service->thread_id || !service->kernel_endpoint ||
        service->control_handle==AURORA_CAP_INVALID) return false;
    service->thread_id=scheduler_create_user_thread(
        "g5-canvas-engine",service->process);
    return service->thread_id!=0u;
}

bool g5_canvas_service_abort_unstarted(struct g5_canvas_service *service) {
    if (!service || !service->process || service->thread_id ||
        process_live_thread_count(service->process)!=0u ||
        process_state(service->process)!=AURORA_PROCESS_RUNNING)
        return false;
    /* No thread was ever scheduled: this is cancellation, not a
     * fabricated successful Ring3 execution. */
    process_mark_exited(service->process,1);
    revoke_control(service);
    if (!process_reap(service->process,NULL) ||
        !process_release(service->process)) return false;
    service->process=NULL;
    service->session_generation=0u;
    return true;
}

bool g5_canvas_service_reap(struct g5_canvas_service *service) {
    if (!service || !service->process) return false;
    if (service->thread_id) {
        if (!scheduler_thread_finished(service->thread_id)) return false;
        if (!scheduler_reap_thread(service->thread_id)) return false;
        service->thread_id=0u;
    }
    if (process_live_thread_count(service->process)!=0u) return false;
    if (process_state(service->process)!=AURORA_PROCESS_EXITED ||
        service->process->exit_code!=0)
        return false; /* Faulted or nonzero exit cannot pass the smoke gate. */
    revoke_control(service);
    if (!process_reap(service->process,NULL) ||
        !process_release(service->process)) return false;
    service->process=NULL;
    service->session_generation=0u;
    return true;
}

bool g5_canvas_service_reap_failed(struct g5_canvas_service *service) {
    if (!service || !service->process) return false;
    if (service->thread_id) {
        if (!scheduler_thread_finished(service->thread_id)) return false;
        if (!scheduler_reap_thread(service->thread_id)) return false;
        service->thread_id=0u;
    }
    if (process_live_thread_count(service->process)!=0u ||
        process_state(service->process)==AURORA_PROCESS_RUNNING)
        return false;
    /* This is cleanup of a terminal process, not proof of a clean exit. */
    revoke_control(service);
    if (!process_reap(service->process,NULL) ||
        !process_release(service->process)) return false;
    service->process=NULL;
    service->session_generation=0u;
    return true;
}
