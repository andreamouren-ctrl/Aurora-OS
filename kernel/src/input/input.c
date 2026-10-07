#include <stddef.h>
#include <stdint.h>

#include <aurora/input.h>

#define INPUT_QUEUE_CAPACITY 64u

static struct aurora_input_event input_queue[INPUT_QUEUE_CAPACITY];
static volatile uint32_t input_head;
static volatile uint32_t input_tail;
static uint64_t input_sequence;

void input_init(void) {
    input_head = 0u;
    input_tail = 0u;
    input_sequence = 0u;
}

static bool push_event_internal(
    const struct aurora_input_event *event
) {
    if (event == NULL ||
        event->type <= AURORA_INPUT_EVENT_NONE ||
        event->type > AURORA_INPUT_EVENT_DEVICE_REMOVED) {
        return false;
    }

    uint32_t head = input_head;
    uint32_t next = (head + 1u) % INPUT_QUEUE_CAPACITY;

    if (next == input_tail) {
        return false;
    }

    struct aurora_input_event normalized = *event;
    normalized.sequence = ++input_sequence;
    if (normalized.sequence == 0u) {
        normalized.sequence = ++input_sequence;
    }

    input_queue[head] = normalized;

    __asm__ volatile ("" ::: "memory");
    input_head = next;
    return true;
}

bool input_push_event_from_irq(
    const struct aurora_input_event *event
) {
    return push_event_internal(event);
}

bool input_push_event(
    const struct aurora_input_event *event
) {
    return push_event_internal(event);
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

uint64_t input_last_sequence(void) {
    return input_sequence;
}

bool input_selftest(void) {
    input_init();

    const struct aurora_input_event key = {
        .type = AURORA_INPUT_EVENT_KEY,
        .source = AURORA_INPUT_SOURCE_PS2_KEYBOARD,
        .key = AURORA_KEY_A,
        .pressed = true
    };
    const struct aurora_input_event motion = {
        .type = AURORA_INPUT_EVENT_POINTER_RELATIVE,
        .source = AURORA_INPUT_SOURCE_PS2_MOUSE,
        .delta_x = 7,
        .delta_y = -3
    };

    if (!input_push_event(&key) ||
        !input_push_event(&motion)) {
        return false;
    }

    struct aurora_input_event a = {0};
    struct aurora_input_event b = {0};

    return
        input_poll_event(&a) &&
        input_poll_event(&b) &&
        a.type == AURORA_INPUT_EVENT_KEY &&
        a.key == AURORA_KEY_A &&
        a.pressed &&
        b.type == AURORA_INPUT_EVENT_POINTER_RELATIVE &&
        b.delta_x == 7 &&
        b.delta_y == -3 &&
        b.sequence > a.sequence &&
        !b.synthetic &&
        !input_poll_event(&a);
}
