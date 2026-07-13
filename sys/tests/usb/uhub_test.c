/*
 * Host tests for deferred root-hub exploration.
 */

#include <stdio.h>
#include <string.h>
#include <usb/uhub.h>
#include <usb/usb_mock_hcd.h>

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

static unsigned external_attach_count;
static unsigned external_detach_count;

static int
external_child_match(struct usb_interface *interface)
{
    return interface->ui_desc.bInterfaceClass == UICLASS_HID ? 100 : 0;
}

static usb_error_t
external_child_attach(struct usb_interface *interface)
{
    ++external_attach_count;
    interface->ui_private = &external_attach_count;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
external_child_detach(struct usb_interface *interface)
{
    ++external_detach_count;
    interface->ui_private = 0;
}

static const struct usb_driver external_child_driver = {
    "hub-test-child",
    external_child_match,
    external_child_attach,
    external_child_detach
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
    CHECK(mock.um_root_intr_enabled &&
        delay_total == 100 + USB_SET_ADDRESS_SETTLE);

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
    CHECK(events.attach_count == 2 &&
        delay_total == 2 * (100 + USB_SET_ADDRESS_SETTLE));

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

static int
test_external_hub(void)
{
    struct usb_mock_hcd mock;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *hub_device;
    struct usb_device *child;
    unsigned delay_total;

    memset(&bus, 0, sizeof(bus));
    delay_total = 0;
    external_attach_count = 0;
    external_detach_count = 0;
    usb_task_system_init();
    usb_core_init(&core);
    usb_mock_hcd_init(&mock);
    usb_mock_hcd_enable_hub(&mock);
    usb_mock_hcd_set_connected(&mock, 1);
    usb_mock_hcd_hub_port_connect(&mock, 1, USB_SPEED_FULL);
    CHECK(uhub_register(&core) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_driver_register(&core, &external_child_driver) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_bus_start(&core, &bus, &mock.um_hcd,
        hub_delay, &delay_total) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_HIGH, &hub_device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(hub_device->ud_address == 1 && usb_external_hub_count() == 1);
    CHECK(mock.um_hub_port_power && mock.um_pending_xfer != 0);
    CHECK(usb_external_hub_device(hub_device, 1) == 0);

    usb_task_run_pending();
    child = usb_external_hub_device(hub_device, 1);
    CHECK(child != 0 && child->ud_address == 2);
    CHECK(child->ud_parent_hub == hub_device && child->ud_port == 1);
    CHECK(child->ud_tt_hub_address == hub_device->ud_address &&
        child->ud_tt_port == 1);
    CHECK(mock.um_hub_port_enabled && external_attach_count == 1);

    usb_mock_hcd_hub_port_connect(&mock, 0, USB_SPEED_FULL);
    CHECK(usb_mock_hcd_hub_interrupt(&mock) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(mock.um_pending_xfer == 0);
    CHECK(usb_task_any_pending());
    usb_task_run_pending();
    CHECK(usb_external_hub_device(hub_device, 1) == 0);
    CHECK(external_detach_count == 1 && mock.um_pending_xfer != 0);

    usb_mock_hcd_hub_port_connect(&mock, 1, USB_SPEED_FULL);
    CHECK(usb_mock_hcd_hub_interrupt(&mock) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(mock.um_pending_xfer == 0);
    usb_task_run_pending();
    child = usb_external_hub_device(hub_device, 1);
    CHECK(child != 0 && child->ud_address == 2);
    CHECK(external_attach_count == 2 && mock.um_pending_xfer != 0);

    /* Detach and enumerate the complete hub again with its child present. */
    usb_device_disconnect(hub_device);
    CHECK(usb_external_hub_count() == 0);
    CHECK(external_detach_count == 2 && mock.um_pending_xfer == 0);
    hub_device = 0;
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_HIGH, &hub_device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(hub_device != 0 && hub_device->ud_address == 1 &&
        usb_external_hub_count() == 1 && mock.um_pending_xfer != 0);
    usb_task_run_pending();
    child = usb_external_hub_device(hub_device, 1);
    CHECK(child != 0 && child->ud_address == 2);
    CHECK(external_attach_count == 3 && external_detach_count == 2 &&
        mock.um_pending_xfer != 0);

    usb_device_disconnect(hub_device);
    CHECK(usb_external_hub_count() == 0);
    CHECK(external_detach_count == 3 && mock.um_pending_xfer == 0);
    usb_bus_stop(&bus);
    return 0;
}

int
main(void)
{
    CHECK(test_initial_and_reconnect() == 0);
    CHECK(test_late_connect() == 0);
    CHECK(test_external_hub() == 0);
    puts("uhub_test: all tests passed");
    return 0;
}
