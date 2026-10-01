#include <stddef.h>
#include <stdint.h>

#include <aurora/input.h>

#define INPUT_QUEUE_CAPACITY 64u

static struct aurora_input_event input_queue[INPUT_QUEUE_CAPACITY];
static volatile uint32_t input_head;
static volatile uint32_t input_tail;

void input_init(void) {
    input_head = 0u;
    input_tail = 0u;
}

bool input_push_event_from_irq(
    const struct aurora_input_event *event
) {
    if (event == NULL) {
        return false;
    }

    uint32_t head = input_head;
    uint32_t next = (head + 1u) % INPUT_QUEUE_CAPACITY;

    if (next == input_tail) {
        return false;
    }

    input_queue[head] = *event;

    __asm__ volatile ("" ::: "memory");
    input_head = next;
    return true;
}

bool input_poll_event(
    struct aurora_input_event *event
) {
    if (event == NULL) {
        return false;
    }

    uint32_t tail = input_tail;

    if (tail == input_head) {
        return false;
    }

    *event = input_queue[tail];

    __asm__ volatile ("" ::: "memory");
    input_tail = (tail + 1u) % INPUT_QUEUE_CAPACITY;
    return true;
}
