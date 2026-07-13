/*
 * Host-side tests for bounded USB core objects and the mock HCD.
 */

#include <stdio.h>
#include <string.h>
#include <usb/usb_mock_hcd.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static struct usb_core core;
static struct usb_bus bus;
static struct usb_mock_hcd mock;
static unsigned attach_count;
static unsigned detach_count;
static unsigned callback_count;
static usb_error_t callback_status;

static int
keyboard_match(struct usb_interface *interface)
{
    const usb_interface_descriptor_t *desc;

    desc = &interface->ui_desc;
    if (desc->bInterfaceClass == UICLASS_HID &&
        desc->bInterfaceSubClass == UISUBCLASS_BOOT &&
        desc->bInterfaceProtocol == UIPROTO_BOOT_KEYBOARD)
        return 100;
    return 0;
}

static usb_error_t
keyboard_attach(struct usb_interface *interface)
{
    ++attach_count;
    interface->ui_private = &attach_count;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
keyboard_detach(struct usb_interface *interface)
{
    ++detach_count;
    interface->ui_private = 0;
}

static const struct usb_driver keyboard_driver = {
    "mock-kbd",
    keyboard_match,
    keyboard_attach,
    keyboard_detach
};

static void
xfer_callback(struct usb_xfer *xfer, void *private, usb_error_t status)
{
    unsigned *marker;

    marker = (unsigned *)private;
    if (xfer == 0 || marker == 0 || *marker != 0x12345678u)
        callback_status = USB_STATUS_IO_ERROR;
    else
        callback_status = status;
    ++callback_count;
}

static void
delay_ms(void *arg, unsigned milliseconds)
{
    unsigned *total;

    total = (unsigned *)arg;
    *total += milliseconds;
}

static int
core_has_no_objects(struct usb_core *usb_core)
{
    unsigned i;

    for (i = 0; i < USB_MAX_DEVICES; ++i)
        if (usb_core->uc_devices[i].ud_used)
            return 0;
    for (i = 0; i < USB_MAX_CORE_INTERFACES; ++i)
        if (usb_core->uc_interfaces[i].ui_used)
            return 0;
    for (i = 0; i < USB_MAX_CORE_ENDPOINTS; ++i)
        if (usb_core->uc_endpoints[i].ue_used)
            return 0;
    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (usb_core->uc_pipes[i].up_used)
            return 0;
    for (i = 0; i < USB_MAX_XFERS; ++i)
        if (usb_core->uc_xfers[i].ux_used)
            return 0;
    return 1;
}

static int
start_fixture(unsigned *delay_total)
{
    usb_core_init(&core);
    memset(&bus, 0, sizeof(bus));
    usb_mock_hcd_init(&mock);
    CHECK(usb_driver_register(&core, &keyboard_driver) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_driver_register(&core, &keyboard_driver) ==
        USB_STATUS_INVALID);
    CHECK(usb_bus_start(&core, &bus, &mock.um_hcd, delay_ms,
        delay_total) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(mock.um_start_count == 1);
    return 0;
}

static int
test_enumerate_and_disconnect(void)
{
    struct usb_device *device;
    struct usb_interface *interface;
    struct usb_endpoint *endpoint;
    struct usb_pipe *pipe;
    struct usb_xfer *xfer;
    unsigned char report[8];
    unsigned marker;
    unsigned delay_total;

    attach_count = 0;
    detach_count = 0;
    callback_count = 0;
    callback_status = USB_STATUS_INVALID;
    delay_total = 0;
    CHECK(start_fixture(&delay_total) == 0);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(device != 0 && device->ud_address == 1);
    CHECK(device->ud_config == 1 && mock.um_configuration == 1);
    CHECK(mock.um_address == 1);
    CHECK(UGETW(device->ud_desc.idVendor) == 0x1234);
    CHECK(UGETW(device->ud_desc.idProduct) == 0x5678);
    CHECK(device->ud_config_length == 34);
    CHECK(attach_count == 1);

    interface = usb_device_interface(device, 0);
    CHECK(interface != 0 && interface->ui_driver == &keyboard_driver);
    CHECK(interface->ui_private == &attach_count);
    endpoint = usb_interface_endpoint(interface, 0);
    CHECK(endpoint != 0 && endpoint->ue_desc.bEndpointAddress == 0x81);
    CHECK(usb_open_pipe(interface, 0x81, &pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(mock.um_open_count == 2);
    CHECK(usb_clear_endpoint_halt(pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(mock.um_clear_halt_count == 1);
    CHECK(mock.um_clear_toggle_count == 1);
    CHECK(mock.um_last_clear_endpoint == 0x81);

    memset(report, 0, sizeof(report));
    marker = 0x12345678u;
    xfer = usb_alloc_xfer(device);
    CHECK(xfer != 0);
    usb_setup_xfer(xfer, pipe, &marker, report, sizeof(report), 0, 10,
        xfer_callback);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(callback_count == 1 && callback_status ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(xfer->ux_actlen == sizeof(report));
    CHECK(usb_free_xfer(xfer) == USB_STATUS_NORMAL_COMPLETION);

    callback_count = 0;
    usb_mock_hcd_hold_xfers(&mock, 1);
    xfer = usb_alloc_xfer(device);
    CHECK(xfer != 0);
    usb_setup_xfer(xfer, pipe, &marker, report, sizeof(report), 0, 10,
        xfer_callback);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(usb_abort_xfer(xfer, USB_STATUS_CANCELLED) ==
        USB_STATUS_CANCELLED);
    CHECK(callback_count == 1 && callback_status == USB_STATUS_CANCELLED);
    usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, sizeof(report));
    CHECK(callback_count == 1);
    CHECK(usb_free_xfer(xfer) == USB_STATUS_NORMAL_COMPLETION);

    callback_count = 0;
    xfer = usb_alloc_xfer(device);
    CHECK(xfer != 0);
    usb_setup_xfer(xfer, pipe, &marker, report, sizeof(report), 0, 10,
        xfer_callback);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    usb_mock_hcd_set_connected(&mock, 0);
    usb_device_disconnect(device);
    CHECK(callback_count == 1 && callback_status ==
        USB_STATUS_DISCONNECTED);
    CHECK(mock.um_abort_count == 2);
    CHECK(detach_count == 1);
    CHECK(usb_free_xfer(xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(core_has_no_objects(&core));
    CHECK(bus.ub_addresses[0] == 0);

    usb_mock_hcd_hold_xfers(&mock, 0);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(device->ud_address == 1);
    usb_device_disconnect(device);
    CHECK(detach_count == 2);
    usb_bus_stop(&bus);
    CHECK(mock.um_stop_count == 1);
    CHECK(core_has_no_objects(&core));
    return 0;
}

static int
test_address_reuse(void)
{
    struct usb_device *first;
    struct usb_device *second;
    struct usb_device *third;
    unsigned delay_total;

    delay_total = 0;
    CHECK(start_fixture(&delay_total) == 0);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &first) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_device_enumerate(&bus, 2, USB_SPEED_FULL, &second) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(first->ud_address == 1 && second->ud_address == 2);
    usb_device_disconnect(first);
    CHECK(usb_device_enumerate(&bus, 3, USB_SPEED_FULL, &third) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(third->ud_address == 1);
    usb_device_disconnect(second);
    usb_device_disconnect(third);
    usb_bus_stop(&bus);
    CHECK(core_has_no_objects(&core));
    return 0;
}

static int
test_control_failure_cleanup(void)
{
    struct usb_device *device;
    unsigned delay_total;

    delay_total = 0;
    CHECK(start_fixture(&delay_total) == 0);
    usb_mock_hcd_set_connected(&mock, 1);
    usb_mock_hcd_fail_next(&mock, USB_STATUS_STALLED);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_STALLED);
    CHECK(device == 0);
    CHECK(core_has_no_objects(&core));
    CHECK(bus.ub_addresses[0] == 0);
    usb_bus_stop(&bus);
    return 0;
}

static int
test_timeout_cleanup(void)
{
    struct usb_device *device;
    unsigned delay_total;

    delay_total = 0;
    CHECK(start_fixture(&delay_total) == 0);
    usb_mock_hcd_set_connected(&mock, 1);
    usb_mock_hcd_hold_xfers(&mock, 1);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_TIMEOUT);
    CHECK(device == 0);
    CHECK(mock.um_abort_count == 1);
    CHECK(mock.um_poll_count == USB_ENUM_TIMEOUT_MS);
    CHECK(delay_total == USB_ENUM_TIMEOUT_MS);
    CHECK(core_has_no_objects(&core));
    usb_bus_stop(&bus);
    return 0;
}

static int
test_malformed_config_cleanup(void)
{
    struct usb_device *device;
    unsigned char config[34] = {
        9, UDESC_CONFIG, 35, 0, 1, 1, 0, UC_BUS_POWERED, 50,
        9, UDESC_INTERFACE, 0, 0, 1,
            UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
        9, UDESC_HID, 0x11, 0x01, 0, 1, 0x22, 63, 0,
        7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
    };
    unsigned delay_total;

    delay_total = 0;
    CHECK(start_fixture(&delay_total) == 0);
    CHECK(usb_mock_hcd_set_config(&mock, config, sizeof(config)) ==
        USB_STATUS_NORMAL_COMPLETION);
    usb_mock_hcd_set_connected(&mock, 1);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_INVALID_DESCRIPTOR);
    CHECK(device == 0);
    CHECK(core_has_no_objects(&core));
    CHECK(bus.ub_addresses[0] == 0);
    usb_bus_stop(&bus);
    return 0;
}

int
main(void)
{
    CHECK(test_enumerate_and_disconnect() == 0);
    CHECK(test_address_reuse() == 0);
    CHECK(test_control_failure_cleanup() == 0);
    CHECK(test_timeout_cleanup() == 0);
    CHECK(test_malformed_config_cleanup() == 0);
    CHECK(strcmp(usb_status_string(USB_STATUS_DISCONNECTED),
        "disconnected") == 0);
    puts("usb_core_test: all tests passed");
    return 0;
}
