#include <stddef.h>
#include <stdint.h>

#include <aurora/arch.h>
#include <aurora/interrupts.h>
#include <aurora/log.h>
#include <aurora/panic.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void *isr_stub_table[32];
extern void isr_stub_timer(void);
extern void isr_stub_spurious(void);

static struct idt_entry idt[256];
static interrupt_handler_fn handlers[256];

static const char *const exception_names[32] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static void idt_set_gate(
    uint8_t vector,
    void *handler,
    uint16_t selector,
    uint8_t attributes
) {
    uint64_t address = (uint64_t)(uintptr_t)handler;

    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

bool interrupts_init(void) {
    uint16_t code_selector = arch_read_cs();

    for (uint16_t vector = 0; vector < 32; ++vector) {
        idt_set_gate(
            (uint8_t)vector,
            isr_stub_table[vector],
            code_selector,
            0x8E
        );
    }

    idt_set_gate(
        AURORA_VECTOR_TIMER,
        isr_stub_timer,
        code_selector,
        0x8E
    );

    idt_set_gate(
        AURORA_VECTOR_SPURIOUS,
        isr_stub_spurious,
        code_selector,
        0x8E
    );

    struct idt_descriptor descriptor = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)&idt[0]
    };

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(descriptor)
        : "memory"
    );

    return true;
}

bool interrupt_register_handler(
    uint8_t vector,
    interrupt_handler_fn handler
) {
    if (vector < 32 ||
        vector == AURORA_VECTOR_SPURIOUS ||
        handler == NULL) {
        return false;
    }

    handlers[vector] = handler;
    return true;
}

struct interrupt_frame *interrupt_dispatch(
    struct interrupt_frame *frame
) {
    if (frame == NULL) {
        kernel_panic("Null interrupt frame");
    }

    uint64_t vector = frame->vector;

    if (vector == AURORA_VECTOR_SPURIOUS) {
        return frame;
    }

    if (vector >= 32 && vector < 256) {
        interrupt_handler_fn handler =
            handlers[vector];

        if (handler != NULL) {
            struct interrupt_frame *next =
                handler(frame);

            if (next == NULL) {
                kernel_panic(
                    "Interrupt handler returned null CPU frame"
                );
            }

            return next;
        }
    }

    log_line("");
    log_line("--- Aurora interrupt/exception ---");

    log_write("Vector: ");
    log_u64(vector);

    if (vector < 32) {
        log_write(" (");
        log_write(exception_names[vector]);
        log_write(")");
    }

    log_line("");

    log_write("Error code: ");
    log_hex64(frame->error_code);
    log_line("");

    log_write("RIP: ");
    log_hex64(frame->rip);
    log_line("");

    if (vector == 14) {
        log_write("CR2: ");
        log_hex64(arch_read_cr2());
        log_line("");
    }

    kernel_panic("Unhandled interrupt or CPU exception");
}
