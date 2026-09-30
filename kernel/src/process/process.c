#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/pmm.h>
#include <aurora/process.h>
#include <aurora/vmm.h>

static volatile uint32_t next_process_id = 1;

static void copy_bytes(
    void *destination,
    const void *source,
    size_t length
) {
    uint8_t *dst = destination;
    const uint8_t *src = source;

    for (size_t i = 0;
         i < length;
         ++i) {
        dst[i] = src[i];
    }
}

static void copy_name(
    char destination[32],
    const char *source
) {
    size_t i = 0;

    if (source != NULL) {
        while (i < 31 &&
               source[i] != '\0') {
            destination[i] =
                source[i];
            ++i;
        }
    }

    destination[i] = '\0';

    while (++i < 32) {
        destination[i] = '\0';
    }
}

static bool map_image(
    struct aurora_process *process,
    const uint8_t *image,
    size_t image_size
) {
    size_t remaining =
        image_size;

    size_t offset = 0;

    while (remaining != 0) {
        uint64_t physical =
            pmm_alloc_page();

        if (physical == 0) {
            return false;
        }

        size_t chunk =
            remaining >
                AURORA_PAGE_SIZE
            ? AURORA_PAGE_SIZE
            : remaining;

        copy_bytes(
            pmm_phys_to_virt(
                physical
            ),
            image + offset,
            chunk
        );

        if (!vmm_map_page_in(
                &process->address_space,
                AURORA_USER_IMAGE_BASE +
                    offset,
                physical,
                VMM_FLAG_USER |
                VMM_FLAG_EXECUTE)) {
            pmm_free_page(
                physical
            );

            return false;
        }

        offset += AURORA_PAGE_SIZE;
        remaining -= chunk;
    }

    return true;
}

static bool map_user_stack(
    struct aurora_process *process
) {
    for (uint32_t page = 0;
         page < AURORA_USER_STACK_PAGES;
         ++page) {
        uint64_t physical =
            pmm_alloc_page();

        if (physical == 0) {
            return false;
        }

        uint64_t virtual_address =
            AURORA_USER_STACK_TOP -
            ((uint64_t)page + 1ull) *
                AURORA_PAGE_SIZE;

        if (!vmm_map_page_in(
                &process->address_space,
                virtual_address,
                physical,
                VMM_FLAG_USER |
                VMM_FLAG_WRITE)) {
            pmm_free_page(
                physical
            );

            return false;
        }
    }

    process->user_stack_top =
        AURORA_USER_STACK_TOP;

    return true;
}

struct aurora_process *process_create_image(
    const char *name,
    const uint8_t *image,
    size_t image_size
) {
    if (image == NULL ||
        image_size == 0) {
        return NULL;
    }

    struct aurora_process *process =
        kheap_alloc(
            sizeof(*process),
            16
        );

    if (process == NULL) {
        return NULL;
    }

    process->id =
        __atomic_fetch_add(
            &next_process_id,
            1u,
            __ATOMIC_RELAXED
        );

    copy_name(
        process->name,
        name
    );

    process->entry_point =
        AURORA_USER_IMAGE_BASE;

    process->user_stack_top = 0;
    process->state =
        AURORA_PROCESS_RUNNING;
    process->exit_code = 0;
    process->fault_vector = 0;
    process->bootstrap_signal = 0;

    cap_table_init(
        &process->capabilities
    );

    if (!vmm_address_space_create(
            &process->address_space)) {
        return NULL;
    }

    if (!map_image(
            process,
            image,
            image_size)) {
        return NULL;
    }

    if (!map_user_stack(process)) {
        return NULL;
    }

    return process;
}

void process_set_bootstrap_signal(
    struct aurora_process *process,
    uint64_t value
) {
    if (process == NULL) {
        return;
    }

    __atomic_store_n(
        &process->bootstrap_signal,
        value,
        __ATOMIC_RELEASE
    );
}

uint64_t process_bootstrap_signal(
    const struct aurora_process *process
) {
    if (process == NULL) {
        return 0;
    }

    return __atomic_load_n(
        &process->bootstrap_signal,
        __ATOMIC_ACQUIRE
    );
}


void process_mark_exited(
    struct aurora_process *process,
    int64_t exit_code
) {
    if (process == NULL) {
        return;
    }

    __atomic_store_n(
        &process->exit_code,
        exit_code,
        __ATOMIC_RELEASE
    );

    __atomic_store_n(
        &process->state,
        AURORA_PROCESS_EXITED,
        __ATOMIC_RELEASE
    );
}

void process_mark_faulted(
    struct aurora_process *process,
    uint64_t vector
) {
    if (process == NULL) {
        return;
    }

    __atomic_store_n(
        &process->fault_vector,
        vector,
        __ATOMIC_RELEASE
    );

    __atomic_store_n(
        &process->state,
        AURORA_PROCESS_FAULTED,
        __ATOMIC_RELEASE
    );
}

enum aurora_process_state process_state(
    const struct aurora_process *process
) {
    if (process == NULL) {
        return AURORA_PROCESS_FAULTED;
    }

    return __atomic_load_n(
        &process->state,
        __ATOMIC_ACQUIRE
    );
}
