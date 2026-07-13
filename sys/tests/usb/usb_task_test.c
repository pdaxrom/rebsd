/*
 * Host tests for the bounded USB deferred task queue.
 */

#include <stdio.h>
#include <dev/usb/usb_task.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

struct task_state {
    struct usb_task *task;
    unsigned value;
    unsigned reschedule;
};

static unsigned output[USB_MAX_TASKS + 2];
static unsigned output_count;

static void
record_task(void *arg)
{
    struct task_state *state;

    state = arg;
    output[output_count++] = state->value;
    if (state->reschedule != 0) {
        state->reschedule = 0;
        (void)usb_task_schedule(state->task);
    }
}

static int
test_queue(void)
{
    struct usb_task tasks[USB_MAX_TASKS + 1];
    struct task_state states[USB_MAX_TASKS + 1];
    unsigned i;

    usb_task_system_init();
    CHECK(!usb_task_any_pending());
    output_count = 0;
    for (i = 0; i < USB_MAX_TASKS + 1; ++i) {
        states[i].task = &tasks[i];
        states[i].value = i + 1;
        states[i].reschedule = 0;
        usb_task_init(&tasks[i], record_task, &states[i]);
    }
    CHECK(usb_task_schedule(0) == -1);
    CHECK(usb_task_schedule(&tasks[0]) == 0);
    CHECK(usb_task_any_pending());
    CHECK(usb_task_schedule(&tasks[0]) == 0);
    CHECK(usb_task_pending(&tasks[0]));
    for (i = 1; i < USB_MAX_TASKS; ++i)
        CHECK(usb_task_schedule(&tasks[i]) == 0);
    CHECK(usb_task_schedule(&tasks[USB_MAX_TASKS]) == -1);
    usb_task_cancel(&tasks[3]);
    CHECK(!usb_task_pending(&tasks[3]));
    CHECK(usb_task_schedule(&tasks[USB_MAX_TASKS]) == 0);
    usb_task_run_pending();
    CHECK(!usb_task_any_pending());
    CHECK(output_count == USB_MAX_TASKS);
    CHECK(output[0] == 1 && output[1] == 2 && output[2] == 3);
    CHECK(output[3] == 5 && output[USB_MAX_TASKS - 1] ==
        USB_MAX_TASKS + 1);
    for (i = 0; i < USB_MAX_TASKS + 1; ++i)
        CHECK(!usb_task_pending(&tasks[i]));
    return 0;
}

static int
test_rearm(void)
{
    struct usb_task task;
    struct task_state state;

    usb_task_system_init();
    output_count = 0;
    state.task = &task;
    state.value = 42;
    state.reschedule = 1;
    usb_task_init(&task, record_task, &state);
    CHECK(usb_task_schedule(&task) == 0);
    CHECK(usb_task_any_pending());
    usb_task_run_pending();
    CHECK(output_count == 2 && output[0] == 42 && output[1] == 42);
    CHECK(!usb_task_pending(&task));
    CHECK(!usb_task_any_pending());
    return 0;
}

int
main(void)
{
    CHECK(test_queue() == 0);
    CHECK(test_rearm() == 0);
    puts("usb_task_test: all tests passed");
    return 0;
}
