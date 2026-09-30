#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/heap.h>
#include <aurora/interrupts.h>
#include <aurora/scheduler.h>
#include <aurora/timer.h>

#define SCHEDULER_MAX_THREADS 64u
#define SCHEDULER_STACK_SIZE  (64u * 1024u)
#define SCHEDULER_QUANTUM_NS   4000000ull

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_RUNNABLE,
    THREAD_RUNNING,
    THREAD_TERMINATED
};

struct scheduler_thread {
    aurora_thread_id id;
    enum thread_state state;

    bool idle;

    char name[32];

    kernel_thread_entry entry;
    void *argument;

    void *stack_base;
    size_t stack_size;

    struct interrupt_frame *saved_frame;
};

static struct scheduler_thread threads[
    SCHEDULER_MAX_THREADS
];

static uint32_t current_index;
static uint32_t idle_index;
static aurora_thread_id next_id;

static bool initialized;
static bool started;

static uint64_t context_switches;

static void copy_name(
    char destination[32],
    const char *source
) {
    size_t i = 0;

    if (source != NULL) {
        while (i < 31 &&
               source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }

    destination[i] = '\0';

    while (++i < 32) {
        destination[i] = '\0';
    }
}

static void clear_bytes(
    void *address,
    size_t length
) {
    uint8_t *bytes = address;

    for (size_t i = 0; i < length; ++i) {
        bytes[i] = 0;
    }
}

static struct scheduler_thread *find_thread(
    aurora_thread_id id
) {
    if (id == 0) {
        return NULL;
    }

    for (uint32_t i = 0;
         i < SCHEDULER_MAX_THREADS;
         ++i) {
        if (threads[i].state != THREAD_UNUSED &&
            threads[i].id == id) {
            return &threads[i];
        }
    }

    return NULL;
}

static void thread_trampoline(
    struct scheduler_thread *thread
) __attribute__((noreturn));

static struct interrupt_frame *build_initial_frame(
    struct scheduler_thread *thread
) {
    uint8_t *base = thread->stack_base;

    uintptr_t top =
        (uintptr_t)base +
        thread->stack_size;

    top &= ~(uintptr_t)0xFu;

    /*
     * SysV x86-64 expects RSP % 16 == 8 at C function entry. IRETQ will
     * leave RSP immediately above the saved frame, so keep eight bytes of
     * alignment padding above it.
     */
    uintptr_t frame_address =
        top -
        sizeof(struct interrupt_frame) -
        8u;

    struct interrupt_frame *frame =
        (struct interrupt_frame *)frame_address;

    clear_bytes(
        frame,
        sizeof(*frame)
    );

    frame->rdi =
        (uint64_t)(uintptr_t)thread;

    frame->rip =
        (uint64_t)(uintptr_t)
            thread_trampoline;

    frame->cs =
        arch_read_cs();

    /*
     * Reserved bit 1 plus IF. New kernel threads become preemptible as
     * soon as IRETQ enters them.
     */
    frame->rflags = 0x202ull;

    return frame;
}

static aurora_thread_id create_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument,
    bool idle
) {
    if (entry == NULL) {
        return 0;
    }

    uint32_t slot =
        SCHEDULER_MAX_THREADS;

    for (uint32_t i = 0;
         i < SCHEDULER_MAX_THREADS;
         ++i) {
        if (threads[i].state ==
            THREAD_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot ==
        SCHEDULER_MAX_THREADS) {
        return 0;
    }

    void *stack =
        kheap_alloc(
            SCHEDULER_STACK_SIZE,
            16
        );

    if (stack == NULL) {
        return 0;
    }

    struct scheduler_thread *thread =
        &threads[slot];

    clear_bytes(
        thread,
        sizeof(*thread)
    );

    thread->id = next_id++;
    thread->state = THREAD_RUNNABLE;
    thread->idle = idle;

    copy_name(
        thread->name,
        name
    );

    thread->entry = entry;
    thread->argument = argument;
    thread->stack_base = stack;
    thread->stack_size =
        SCHEDULER_STACK_SIZE;

    thread->saved_frame =
        build_initial_frame(thread);

    if (started && !idle) {
        /*
         * A newly-created runnable thread must not wait indefinitely if the
         * scheduler had gone fully tickless with only one active thread.
         */
        (void)timer_arm_ns(1);
    }

    return thread->id;
}

static void idle_thread(
    void *argument
) {
    (void)argument;

    for (;;) {
        arch_idle();
    }
}

static void thread_trampoline(
    struct scheduler_thread *thread
) {
    if (thread != NULL &&
        thread->entry != NULL) {
        thread->entry(
            thread->argument
        );
    }

    arch_disable_interrupts();

    if (thread != NULL) {
        thread->state =
            THREAD_TERMINATED;
    }

    arch_enable_interrupts();

    /*
     * The timer already armed for this time slice will move execution away
     * from the terminated thread. It is never selected again.
     */
    for (;;) {
        arch_idle();
    }
}

static uint32_t find_next_thread(void) {
    /*
     * Prefer useful runnable work. The idle thread is selected only if no
     * normal thread can run.
     */
    for (uint32_t offset = 1;
         offset <= SCHEDULER_MAX_THREADS;
         ++offset) {
        uint32_t index =
            (current_index + offset) %
            SCHEDULER_MAX_THREADS;

        if (index == current_index) {
            continue;
        }

        if (threads[index].state ==
                THREAD_RUNNABLE &&
            !threads[index].idle) {
            return index;
        }
    }

    if (threads[current_index].state ==
        THREAD_RUNNABLE) {
        return current_index;
    }

    if (idle_index <
            SCHEDULER_MAX_THREADS &&
        threads[idle_index].state ==
            THREAD_RUNNABLE) {
        return idle_index;
    }

    return SCHEDULER_MAX_THREADS;
}

static bool has_other_useful_runnable(
    uint32_t active_index
) {
    for (uint32_t i = 0;
         i < SCHEDULER_MAX_THREADS;
         ++i) {
        if (i == active_index) {
            continue;
        }

        if (threads[i].state ==
                THREAD_RUNNABLE &&
            !threads[i].idle) {
            return true;
        }
    }

    return false;
}

static struct interrupt_frame *scheduler_on_timer(
    struct interrupt_frame *frame
) {
    if (!started ||
        current_index >=
            SCHEDULER_MAX_THREADS) {
        return frame;
    }

    struct scheduler_thread *current =
        &threads[current_index];

    current->saved_frame = frame;

    if (current->state ==
        THREAD_RUNNING) {
        current->state =
            THREAD_RUNNABLE;
    }

    uint32_t next_index =
        find_next_thread();

    if (next_index ==
        SCHEDULER_MAX_THREADS) {
        timer_cancel();
        return frame;
    }

    struct scheduler_thread *next =
        &threads[next_index];

    if (next->saved_frame == NULL) {
        timer_cancel();
        return frame;
    }

    next->state =
        THREAD_RUNNING;

    if (next_index != current_index) {
        current_index = next_index;
        ++context_switches;
    }

    if (has_other_useful_runnable(
            current_index)) {
        (void)timer_arm_ns(
            SCHEDULER_QUANTUM_NS
        );
    } else {
        timer_cancel();
    }

    return next->saved_frame;
}

bool scheduler_init(void) {
    for (uint32_t i = 0;
         i < SCHEDULER_MAX_THREADS;
         ++i) {
        clear_bytes(
            &threads[i],
            sizeof(threads[i])
        );
    }

    current_index = 0;
    idle_index =
        SCHEDULER_MAX_THREADS;

    next_id = 1;
    context_switches = 0;
    started = false;

    struct scheduler_thread *bootstrap =
        &threads[0];

    bootstrap->id = next_id++;
    bootstrap->state = THREAD_RUNNING;
    bootstrap->idle = false;

    copy_name(
        bootstrap->name,
        "bootstrap"
    );

    aurora_thread_id idle_id =
        create_thread(
            "idle",
            idle_thread,
            NULL,
            true
        );

    if (idle_id == 0) {
        return false;
    }

    for (uint32_t i = 0;
         i < SCHEDULER_MAX_THREADS;
         ++i) {
        if (threads[i].id == idle_id) {
            idle_index = i;
            break;
        }
    }

    if (idle_index ==
        SCHEDULER_MAX_THREADS) {
        return false;
    }

    timer_set_callback(
        scheduler_on_timer
    );

    initialized = true;
    return true;
}

aurora_thread_id scheduler_create_kernel_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument
) {
    if (!initialized) {
        return 0;
    }

    return create_thread(
        name,
        entry,
        argument,
        false
    );
}

bool scheduler_start(void) {
    if (!initialized ||
        started) {
        return false;
    }

    started = true;

    if (has_other_useful_runnable(
            current_index)) {
        if (!timer_arm_ns(
                SCHEDULER_QUANTUM_NS)) {
            started = false;
            return false;
        }
    }

    arch_enable_interrupts();
    return true;
}

bool scheduler_thread_finished(
    aurora_thread_id id
) {
    struct scheduler_thread *thread =
        find_thread(id);

    return thread != NULL &&
        thread->state ==
            THREAD_TERMINATED;
}

aurora_thread_id scheduler_current_thread_id(void) {
    if (current_index >=
        SCHEDULER_MAX_THREADS) {
        return 0;
    }

    return threads[current_index].id;
}

uint64_t scheduler_context_switch_count(void) {
    return context_switches;
}
