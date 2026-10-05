#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/cpu_local.h>
#include <aurora/gdt.h>
#include <aurora/heap.h>
#include <aurora/interrupts.h>
#include <aurora/panic.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/spinlock.h>
#include <aurora/syscall.h>
#include <aurora/timer.h>
#include <aurora/vmm.h>

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
    bool user;
    char name[32];
    kernel_thread_entry entry;
    void *argument;
    struct aurora_process *process;
    struct vmm_address_space *address_space;
    void *stack_base;
    size_t stack_size;
    struct interrupt_frame *saved_frame;
};

static struct scheduler_thread threads[SCHEDULER_MAX_THREADS];
static aurora_thread_id next_id;
static bool initialized;
static bool started;
static aurora_spinlock scheduler_lock = AURORA_SPINLOCK_INIT;

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

static void clear_bytes(void *address, size_t length) {
    uint8_t *bytes = address;
    for (size_t i = 0u; i < length; ++i) bytes[i] = 0u;
}

/* scheduler_lock must be held by every helper suffixed with _locked. */
static struct scheduler_thread *find_thread_locked(aurora_thread_id id) {
    if (id == 0u) return NULL;
    for (uint32_t i = 0u; i < SCHEDULER_MAX_THREADS; ++i) {
        if (threads[i].state != THREAD_UNUSED && threads[i].id == id)
            return &threads[i];
    }
    return NULL;
}

static struct aurora_cpu_local *current_cpu_locked(void) {
    struct aurora_cpu_local *cpu = cpu_local_current();
    if (cpu == NULL) kernel_panic("Scheduler has no CPU-local state");
    return cpu;
}

static uint64_t thread_kernel_stack_top(const struct scheduler_thread *thread) {
    if (thread == NULL || thread->stack_base == NULL || thread->stack_size == 0u)
        return 0u;
    return (uint64_t)(uintptr_t)thread->stack_base + thread->stack_size;
}

static void thread_trampoline(struct scheduler_thread *thread) __attribute__((noreturn));

static struct interrupt_frame *build_kernel_frame(struct scheduler_thread *thread) {
    uintptr_t stack_top = (uintptr_t)thread_kernel_stack_top(thread);
    stack_top &= ~(uintptr_t)0xFu;
    uint64_t initial_rsp = (uint64_t)(stack_top - 8u);
    uintptr_t frame_address = stack_top - sizeof(struct interrupt_frame);
    struct interrupt_frame *frame = (struct interrupt_frame *)frame_address;
    clear_bytes(frame, sizeof(*frame));
    frame->rdi = (uint64_t)(uintptr_t)thread;
    frame->rip = (uint64_t)(uintptr_t)thread_trampoline;
    frame->cs = AURORA_KERNEL_CODE_SELECTOR;
    frame->rflags = 0x202ull;
    frame->rsp = initial_rsp;
    frame->ss = AURORA_KERNEL_DATA_SELECTOR;
    return frame;
}

static struct interrupt_frame *build_user_frame(
    struct scheduler_thread *thread,
    struct aurora_process *process
) {
    uintptr_t stack_top = (uintptr_t)thread_kernel_stack_top(thread);
    stack_top &= ~(uintptr_t)0xFu;
    uintptr_t frame_address = stack_top - sizeof(struct interrupt_frame);
    struct interrupt_frame *frame = (struct interrupt_frame *)frame_address;
    clear_bytes(frame, sizeof(*frame));
    frame->rip = process->entry_point;
    frame->cs = gdt_user_code_selector();
    frame->rflags = 0x202ull;
    frame->rsp = process->user_stack_top;
    frame->ss = gdt_user_data_selector();
    return frame;
}

static uint32_t find_free_slot_locked(void) {
    for (uint32_t i = 0u; i < SCHEDULER_MAX_THREADS; ++i)
        if (threads[i].state == THREAD_UNUSED) return i;
    return SCHEDULER_MAX_THREADS;
}

static bool allocate_thread_stack(struct scheduler_thread *thread) {
    thread->stack_base = kheap_alloc(SCHEDULER_STACK_SIZE, 16u);
    if (thread->stack_base == NULL) return false;
    thread->stack_size = SCHEDULER_STACK_SIZE;
    return true;
}

static aurora_thread_id create_kernel_thread_locked(
    const char *name,
    kernel_thread_entry entry,
    void *argument,
    bool idle
) {
    if (entry == NULL) return 0u;
    uint32_t slot = find_free_slot_locked();
    if (slot == SCHEDULER_MAX_THREADS) return 0u;

    struct scheduler_thread *thread = &threads[slot];
    clear_bytes(thread, sizeof(*thread));
    if (!allocate_thread_stack(thread)) return 0u;

    thread->id = next_id++;
    thread->state = THREAD_RUNNABLE;
    thread->idle = idle;
    thread->user = false;
    copy_name(thread->name, name);
    thread->entry = entry;
    thread->argument = argument;
    thread->process = NULL;
    thread->address_space = vmm_kernel_space();
    thread->saved_frame = build_kernel_frame(thread);

    if (started && !idle) (void)timer_arm_ns(1u);
    return thread->id;
}

static aurora_thread_id create_user_thread_locked(
    const char *name,
    struct aurora_process *process
) {
    if (process == NULL) return 0u;
    uint32_t slot = find_free_slot_locked();
    if (slot == SCHEDULER_MAX_THREADS) return 0u;

    struct scheduler_thread *thread = &threads[slot];
    clear_bytes(thread, sizeof(*thread));
    if (!allocate_thread_stack(thread)) return 0u;

    thread->id = next_id++;
    thread->state = THREAD_RUNNABLE;
    thread->idle = false;
    thread->user = true;
    copy_name(thread->name, name);
    thread->process = process;
    thread->address_space = &process->address_space;
    thread->saved_frame = build_user_frame(thread, process);

    if (started) (void)timer_arm_ns(1u);
    return thread->id;
}

static void idle_thread(void *argument) {
    (void)argument;
    for (;;) arch_idle();
}

static void thread_trampoline(struct scheduler_thread *thread) {
    if (thread != NULL && thread->entry != NULL)
        thread->entry(thread->argument);

    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    if (thread != NULL) thread->state = THREAD_TERMINATED;
    spinlock_unlock_irqrestore(&scheduler_lock, irq);

    for (;;) arch_idle();
}

static uint32_t find_next_thread_locked(struct aurora_cpu_local *cpu) {
    uint32_t current_index = cpu->scheduler_current_index;
    for (uint32_t offset = 1u; offset <= SCHEDULER_MAX_THREADS; ++offset) {
        uint32_t index = (current_index + offset) % SCHEDULER_MAX_THREADS;
        if (index == current_index) continue;
        if (threads[index].state == THREAD_RUNNABLE && !threads[index].idle)
            return index;
    }

    if (current_index < SCHEDULER_MAX_THREADS &&
        threads[current_index].state == THREAD_RUNNABLE)
        return current_index;

    uint32_t idle_index = cpu->scheduler_idle_index;
    if (idle_index < SCHEDULER_MAX_THREADS &&
        threads[idle_index].state == THREAD_RUNNABLE)
        return idle_index;

    return SCHEDULER_MAX_THREADS;
}

static bool has_other_useful_runnable_locked(uint32_t active_index) {
    for (uint32_t i = 0u; i < SCHEDULER_MAX_THREADS; ++i) {
        if (i == active_index) continue;
        if (threads[i].state == THREAD_RUNNABLE && !threads[i].idle) return true;
    }
    return false;
}

static void prepare_thread(struct scheduler_thread *thread) {
    if (thread == NULL || thread->address_space == NULL ||
        !vmm_activate(thread->address_space))
        kernel_panic("Scheduler could not activate address space");

    if (thread->user) {
        uint64_t kernel_stack_top = thread_kernel_stack_top(thread);
        if (kernel_stack_top == 0u)
            kernel_panic("User thread has no kernel stack");
        gdt_set_current_kernel_stack(kernel_stack_top);
        syscall_set_kernel_stack(kernel_stack_top);
    }
}

static struct interrupt_frame *select_after_current_stops_locked(
    struct aurora_cpu_local *cpu
) {
    uint32_t next_index = find_next_thread_locked(cpu);
    if (next_index == SCHEDULER_MAX_THREADS)
        kernel_panic("Scheduler has no runnable thread");

    struct scheduler_thread *next = &threads[next_index];
    if (next->saved_frame == NULL)
        kernel_panic("Runnable thread has no saved frame");

    next->state = THREAD_RUNNING;
    if (next_index != cpu->scheduler_current_index) {
        cpu->scheduler_current_index = next_index;
        ++cpu->scheduler_context_switches;
    }

    prepare_thread(next);
    if (has_other_useful_runnable_locked(cpu->scheduler_current_index))
        (void)timer_arm_ns(SCHEDULER_QUANTUM_NS);
    else
        timer_cancel();

    return next->saved_frame;
}

static struct interrupt_frame *scheduler_on_timer(struct interrupt_frame *frame) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();
    uint32_t current_index = cpu->scheduler_current_index;

    if (!started || current_index >= SCHEDULER_MAX_THREADS) {
        spinlock_unlock_irqrestore(&scheduler_lock, irq);
        return frame;
    }

    struct scheduler_thread *current = &threads[current_index];
    current->saved_frame = frame;
    if (current->state == THREAD_RUNNING) current->state = THREAD_RUNNABLE;
    struct interrupt_frame *next = select_after_current_stops_locked(cpu);

    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return next;
}

bool scheduler_init(void) {
    if (!spinlock_self_test()) return false;

    spinlock_init(&scheduler_lock);
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();

    for (uint32_t i = 0u; i < SCHEDULER_MAX_THREADS; ++i)
        clear_bytes(&threads[i], sizeof(threads[i]));

    cpu->scheduler_current_index = 0u;
    cpu->scheduler_idle_index = SCHEDULER_MAX_THREADS;
    cpu->scheduler_context_switches = 0u;
    next_id = 1u;
    initialized = false;
    started = false;

    struct scheduler_thread *bootstrap = &threads[0];
    bootstrap->id = next_id++;
    bootstrap->state = THREAD_RUNNING;
    bootstrap->idle = false;
    bootstrap->user = false;
    bootstrap->process = NULL;
    bootstrap->address_space = vmm_kernel_space();
    copy_name(bootstrap->name, "bootstrap");

    aurora_thread_id idle_id = create_kernel_thread_locked(
        "idle", idle_thread, NULL, true
    );
    if (idle_id == 0u) {
        spinlock_unlock_irqrestore(&scheduler_lock, irq);
        return false;
    }

    for (uint32_t i = 0u; i < SCHEDULER_MAX_THREADS; ++i) {
        if (threads[i].id == idle_id) {
            cpu->scheduler_idle_index = i;
            break;
        }
    }
    if (cpu->scheduler_idle_index == SCHEDULER_MAX_THREADS) {
        spinlock_unlock_irqrestore(&scheduler_lock, irq);
        return false;
    }

    timer_set_callback(scheduler_on_timer);
    initialized = true;
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return true;
}

aurora_thread_id scheduler_create_kernel_thread(
    const char *name,
    kernel_thread_entry entry,
    void *argument
) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    aurora_thread_id id = 0u;
    if (initialized)
        id = create_kernel_thread_locked(name, entry, argument, false);
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return id;
}

aurora_thread_id scheduler_create_user_thread(
    const char *name,
    struct aurora_process *process
) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    aurora_thread_id id = 0u;
    if (initialized) id = create_user_thread_locked(name, process);
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return id;
}

bool scheduler_start(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();
    if (!initialized || started) {
        spinlock_unlock_irqrestore(&scheduler_lock, irq);
        return false;
    }

    started = true;
    if (has_other_useful_runnable_locked(cpu->scheduler_current_index) &&
        !timer_arm_ns(SCHEDULER_QUANTUM_NS)) {
        started = false;
        spinlock_unlock_irqrestore(&scheduler_lock, irq);
        return false;
    }

    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    arch_enable_interrupts();
    return true;
}

bool scheduler_thread_finished(aurora_thread_id id) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct scheduler_thread *thread = find_thread_locked(id);
    bool finished = thread != NULL && thread->state == THREAD_TERMINATED;
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return finished;
}

aurora_thread_id scheduler_current_thread_id(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();
    uint32_t current_index = cpu->scheduler_current_index;
    aurora_thread_id id = current_index < SCHEDULER_MAX_THREADS
        ? threads[current_index].id : 0u;
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return id;
}

struct aurora_process *scheduler_current_process(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();
    uint32_t current_index = cpu->scheduler_current_index;
    struct aurora_process *process = current_index < SCHEDULER_MAX_THREADS
        ? threads[current_index].process : NULL;
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return process;
}

struct interrupt_frame *scheduler_terminate_current(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    struct aurora_cpu_local *cpu = current_cpu_locked();
    uint32_t current_index = cpu->scheduler_current_index;
    if (!started || current_index >= SCHEDULER_MAX_THREADS)
        kernel_panic("Invalid scheduler termination request");

    struct scheduler_thread *current = &threads[current_index];
    if (!current->user)
        kernel_panic("Kernel thread termination through user path");

    current->state = THREAD_TERMINATED;
    struct interrupt_frame *next = select_after_current_stops_locked(cpu);
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return next;
}

uint64_t scheduler_context_switch_count(void) {
    aurora_spinlock_irq_state irq = spinlock_lock_irqsave(&scheduler_lock);
    uint64_t count = 0u;
    for (uint32_t i = 0u; i < AURORA_MAX_SMP_CPUS; ++i) {
        struct aurora_cpu_local *cpu = cpu_local_at(i);
        if (cpu != NULL) count += cpu->scheduler_context_switches;
    }
    spinlock_unlock_irqrestore(&scheduler_lock, irq);
    return count;
}
