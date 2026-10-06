#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/vmm.h>

static volatile uint32_t next_process_id = 1;

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *dst = destination;
    const uint8_t *src = source;
    for (size_t i = 0u; i < length; ++i) dst[i] = src[i];
}

static void clear_bytes(void *address, size_t length) {
    uint8_t *bytes = address;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_name(char destination[32], const char *source) {
    size_t i = 0u;
    if (source != NULL) {
        while (i < 31u && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    while (++i < 32u) destination[i] = '\0';
}

static bool map_image(
    struct aurora_process *process,
    const uint8_t *image,
    size_t image_size
) {
    size_t remaining = image_size;
    size_t offset = 0u;

    while (remaining != 0u) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u) return false;

        size_t chunk = remaining > AURORA_PAGE_SIZE
            ? (size_t)AURORA_PAGE_SIZE
            : remaining;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);
        copy_bytes(pmm_phys_to_virt(physical), image + offset, chunk);

        if (!vmm_map_page_in(
                &process->address_space,
                AURORA_USER_IMAGE_BASE + offset,
                physical,
                VMM_FLAG_USER | VMM_FLAG_EXECUTE)) {
            pmm_free_page(physical);
            return false;
        }

        ++process->image_page_count;
        offset += (size_t)AURORA_PAGE_SIZE;
        remaining -= chunk;
    }

    return true;
}

static bool map_user_stack(struct aurora_process *process) {
    for (uint32_t page = 0u; page < AURORA_USER_STACK_PAGES; ++page) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0u) return false;

        clear_bytes(pmm_phys_to_virt(physical), (size_t)AURORA_PAGE_SIZE);

        uint64_t virtual_address = AURORA_USER_STACK_TOP -
            ((uint64_t)page + 1ull) * AURORA_PAGE_SIZE;

        if (!vmm_map_page_in(
                &process->address_space,
                virtual_address,
                physical,
                VMM_FLAG_USER | VMM_FLAG_WRITE)) {
            pmm_free_page(physical);
            return false;
        }

        ++process->stack_page_count;
    }

    process->user_stack_top = AURORA_USER_STACK_TOP;
    return true;
}

static bool preflight_owned_pages(const struct aurora_process *process) {
    uint64_t physical = 0u;

    for (size_t page = 0u; page < process->image_page_count; ++page) {
        uint64_t virtual_address = AURORA_USER_IMAGE_BASE +
            (uint64_t)page * AURORA_PAGE_SIZE;
        if (!vmm_translate_in(&process->address_space, virtual_address, &physical) ||
            (physical & (AURORA_PAGE_SIZE - 1ull)) != 0u) {
            return false;
        }
    }

    for (uint32_t page = 0u; page < process->stack_page_count; ++page) {
        uint64_t virtual_address = AURORA_USER_STACK_TOP -
            ((uint64_t)page + 1ull) * AURORA_PAGE_SIZE;
        if (!vmm_translate_in(&process->address_space, virtual_address, &physical) ||
            (physical & (AURORA_PAGE_SIZE - 1ull)) != 0u) {
            return false;
        }
    }

    return true;
}

static void free_owned_pages(struct aurora_process *process) {
    uint64_t physical = 0u;

    for (size_t page = 0u; page < process->image_page_count; ++page) {
        uint64_t virtual_address = AURORA_USER_IMAGE_BASE +
            (uint64_t)page * AURORA_PAGE_SIZE;
        (void)vmm_translate_in(&process->address_space, virtual_address, &physical);
        pmm_free_page(physical);
    }

    for (uint32_t page = 0u; page < process->stack_page_count; ++page) {
        uint64_t virtual_address = AURORA_USER_STACK_TOP -
            ((uint64_t)page + 1ull) * AURORA_PAGE_SIZE;
        (void)vmm_translate_in(&process->address_space, virtual_address, &physical);
        pmm_free_page(physical);
    }

    process->image_page_count = 0u;
    process->stack_page_count = 0u;
}

static bool release_address_space(struct aurora_process *process) {
    if (process == NULL || process->address_space.root_physical == 0u)
        return true;

    if (!vmm_address_space_is_quiescent(&process->address_space) ||
        !preflight_owned_pages(process)) {
        return false;
    }

    free_owned_pages(process);
    return vmm_address_space_destroy(&process->address_space);
}

static void discard_new_process(struct aurora_process *process) {
    if (process == NULL) return;
    (void)release_address_space(process);
    clear_bytes(process, sizeof(*process));
    (void)kheap_free_sized(process, sizeof(*process));
}

struct aurora_process *process_create_image(
    const char *name,
    const uint8_t *image,
    size_t image_size
) {
    if (image == NULL || image_size == 0u) return NULL;

    struct aurora_process *process = kheap_alloc(sizeof(*process), 16u);
    if (process == NULL) return NULL;
    clear_bytes(process, sizeof(*process));

    process->id = __atomic_fetch_add(&next_process_id, 1u, __ATOMIC_RELAXED);
    copy_name(process->name, name);
    process->entry_point = AURORA_USER_IMAGE_BASE;
    process->state = AURORA_PROCESS_RUNNING;

    cap_table_init(&process->capabilities);

    if (!vmm_address_space_create(&process->address_space)) {
        discard_new_process(process);
        return NULL;
    }

    if (!map_image(process, image, image_size) || !map_user_stack(process)) {
        discard_new_process(process);
        return NULL;
    }

    return process;
}

void process_set_bootstrap_signal(struct aurora_process *process, uint64_t value) {
    if (process == NULL) return;
    __atomic_store_n(&process->bootstrap_signal, value, __ATOMIC_RELEASE);
}

uint64_t process_bootstrap_signal(const struct aurora_process *process) {
    if (process == NULL) return 0u;
    return __atomic_load_n(&process->bootstrap_signal, __ATOMIC_ACQUIRE);
}

void process_mark_exited(struct aurora_process *process, int64_t exit_code) {
    if (process == NULL) return;
    __atomic_store_n(&process->exit_code, exit_code, __ATOMIC_RELEASE);
    __atomic_store_n(&process->state, AURORA_PROCESS_EXITED, __ATOMIC_RELEASE);
}

void process_mark_faulted(struct aurora_process *process, uint64_t vector) {
    if (process == NULL) return;
    __atomic_store_n(&process->fault_vector, vector, __ATOMIC_RELEASE);
    __atomic_store_n(&process->state, AURORA_PROCESS_FAULTED, __ATOMIC_RELEASE);
}

enum aurora_process_state process_state(const struct aurora_process *process) {
    if (process == NULL) return AURORA_PROCESS_FAULTED;
    return __atomic_load_n(&process->state, __ATOMIC_ACQUIRE);
}

bool process_thread_attach(struct aurora_process *process) {
    if (process == NULL || process_state(process) != AURORA_PROCESS_RUNNING)
        return false;

    __atomic_fetch_add(&process->live_threads, 1u, __ATOMIC_ACQ_REL);

    if (process_state(process) != AURORA_PROCESS_RUNNING) {
        __atomic_fetch_sub(&process->live_threads, 1u, __ATOMIC_ACQ_REL);
        return false;
    }

    return true;
}

bool process_thread_detach(struct aurora_process *process) {
    if (process == NULL) return false;

    uint32_t current = __atomic_load_n(&process->live_threads, __ATOMIC_ACQUIRE);
    while (current != 0u) {
        if (__atomic_compare_exchange_n(
                &process->live_threads,
                &current,
                current - 1u,
                false,
                __ATOMIC_ACQ_REL,
                __ATOMIC_ACQUIRE)) {
            return true;
        }
    }

    return false;
}

uint32_t process_live_thread_count(const struct aurora_process *process) {
    if (process == NULL) return 0u;
    return __atomic_load_n(&process->live_threads, __ATOMIC_ACQUIRE);
}

bool process_reap(
    struct aurora_process *process,
    struct aurora_process_result *out_result
) {
    if (process == NULL || process_live_thread_count(process) != 0u) return false;

    enum aurora_process_state state = process_state(process);
    if (state != AURORA_PROCESS_EXITED && state != AURORA_PROCESS_FAULTED)
        return false;

    struct aurora_process_result result = {
        .id = process->id,
        .terminal_state = state,
        .exit_code = __atomic_load_n(&process->exit_code, __ATOMIC_ACQUIRE),
        .fault_vector = __atomic_load_n(&process->fault_vector, __ATOMIC_ACQUIRE)
    };

    if (!release_address_space(process)) return false;

    process->entry_point = 0u;
    process->user_stack_top = 0u;
    __atomic_store_n(&process->state, AURORA_PROCESS_REAPED, __ATOMIC_RELEASE);

    if (out_result != NULL) *out_result = result;
    return true;
}

bool process_release(struct aurora_process *process) {
    if (process == NULL || process_state(process) != AURORA_PROCESS_REAPED ||
        process_live_thread_count(process) != 0u ||
        process->address_space.root_physical != 0u) {
        return false;
    }

    clear_bytes(process, sizeof(*process));
    return kheap_free_sized(process, sizeof(*process));
}
