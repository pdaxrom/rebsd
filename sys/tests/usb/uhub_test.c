/*
 * Host tests for deferred root-hub exploration.
 */

#include <stdio.h>
#include <string.h>
#include <dev/usb/uhub.h>
#include <dev/usb/usb_mock_hcd.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

struct event_state {
    unsigned attach_count;
    unsigned detach_count;
    unsigned error_count;
    unsigned last_port;
};

static void
hub_event(void *arg, unsigned port, enum usb_root_hub_event event,
    struct usb_device *device, usb_error_t status)
{
    struct event_state *events;

    events = arg;
    events->last_port = port;
    if (event == USB_ROOT_HUB_EVENT_ATTACH) {
        ++events->attach_count;
        if (device == 0 || status != USB_STATUS_NORMAL_COMPLETION)
            ++events->error_count;
    } else if (event == USB_ROOT_HUB_EVENT_DETACH) {
        ++events->detach_count;
    } else {
        ++events->error_count;
    }
}

static void
hub_delay(void *arg, unsigned milliseconds)
{
    unsigned *total;

    total = arg;
    *total += milliseconds;
}

static int
test_initial_and_reconnect(void)
{
    struct usb_mock_hcd mock;
    struct usb_root_hub hub;
    struct usb_core core;
    struct usb_bus bus;
    struct event_state events;
    struct usb_device *first;
    unsigned delay_total;

    memset(&bus, 0, sizeof(bus));
    memset(&events, 0, sizeof(events));
    delay_total = 0;
    usb_task_system_init();
    usb_core_init(&core);
    usb_mock_hcd_init(&mock);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_bus_start(&core, &bus, &mock.um_hcd,
        hub_delay, &delay_total) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_start(&hub, &bus, hub_event, &events) ==
        USB_STATUS_NORMAL_COMPLETION);
    first = usb_root_hub_device(&hub, 1);
    CHECK(first != 0 && first->ud_address == 1);
    CHECK(events.attach_count == 1 && events.detach_count == 0);
    CHECK(mock.um_root_intr_enabled && delay_total == 100);

    usb_mock_hcd_set_connected(&mock, 0);
    CHECK(usb_task_pending(&hub.urh_task));
    CHECK(usb_root_hub_device(&hub, 1) == first);
    usb_task_run_pending();
    CHECK(usb_root_hub_device(&hub, 1) == 0);
    CHECK(events.detach_count == 1 && mock.um_root_intr_enabled);

    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_task_pending(&hub.urh_task));
    usb_task_run_pending();
    CHECK(usb_root_hub_device(&hub, 1) != 0);
    CHECK(usb_root_hub_device(&hub, 1)->ud_address == 1);
    CHECK(events.attach_count == 2 && delay_total == 200);

    usb_mock_hcd_set_connected(&mock, 0);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_task_pending(&hub.urh_task));
    usb_task_run_pending();
    CHECK(usb_root_hub_device(&hub, 1) != 0);
    CHECK(usb_root_hub_device(&hub, 1)->ud_address == 1);
    CHECK(events.attach_count == 3 && events.detach_count == 2);
    CHECK(events.error_count == 0 && events.last_port == 1);

    usb_root_hub_stop(&hub);
    CHECK(events.detach_count == 3 && !mock.um_root_intr_enabled);
    usb_bus_stop(&bus);
    return 0;
}

static int
test_late_connect(void)
{
    struct usb_mock_hcd mock;
    struct usb_root_hub hub;
    struct usb_core core;
    struct usb_bus bus;
    struct event_state events;
    unsigned delay_total;

    memset(&bus, 0, sizeof(bus));
    memset(&events, 0, sizeof(events));
    delay_total = 0;
    usb_task_system_init();
    usb_core_init(&core);
    usb_mock_hcd_init(&mock);
    CHECK(usb_bus_start(&core, &bus, &mock.um_hcd,
        hub_delay, &delay_total) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_start(&hub, &bus, hub_event, &events) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_device(&hub, 1) == 0 && delay_total == 0);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_task_pending(&hub.urh_task));
    usb_task_run_pending();
    CHECK(usb_root_hub_device(&hub, 1) != 0);
    CHECK(events.attach_count == 1 && events.error_count == 0);
    usb_root_hub_stop(&hub);
    usb_bus_stop(&bus);
    return 0;
}

int
main(void)
{
    CHECK(test_initial_and_reconnect() == 0);
    CHECK(test_late_connect() == 0);
    puts("uhub_test: all tests passed");
    return 0;
}
