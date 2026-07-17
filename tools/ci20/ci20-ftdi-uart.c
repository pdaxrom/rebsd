#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <libusb.h>

#define FTDI_VID 0x0403
#define FTDI_PID 0x6010
#define FTDI_INTERFACE_B 1
#define FTDI_PORT_B_INDEX 2

#define SIO_SET_FLOW_CTRL_REQUEST 2
#define SIO_SET_BAUDRATE_REQUEST 3
#define SIO_SET_DATA_REQUEST 4
#define SIO_SET_LATENCY_TIMER_REQUEST 9
#define SIO_SET_BITMODE_REQUEST 11

static struct termios saved_termios;
static int termios_saved;
static volatile sig_atomic_t stopping;

static void
restore_terminal(void)
{
    if (termios_saved)
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

static void
handle_signal(int signo)
{
    (void)signo;
    stopping = 1;
}

static void
fatal_libusb(const char *what, int error)
{
    fprintf(stderr, "ci20-ftdi-uart: %s: %s\n", what,
        libusb_error_name(error));
    exit(EXIT_FAILURE);
}

static int
ftdi_control(libusb_device_handle *handle, unsigned char request,
    unsigned short value)
{
    return libusb_control_transfer(handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
        LIBUSB_RECIPIENT_DEVICE,
        request, value, FTDI_PORT_B_INDEX, NULL, 0, 1000);
}

static libusb_device_handle *
open_ft2232d(libusb_context *context)
{
    libusb_device **devices;
    libusb_device_handle *handle = NULL;
    ssize_t count;
    ssize_t i;

    count = libusb_get_device_list(context, &devices);
    if (count < 0)
        fatal_libusb("enumerate USB devices", (int)count);

    for (i = 0; i < count; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(devices[i], &descriptor) != 0)
            continue;
        if (descriptor.idVendor != FTDI_VID ||
            descriptor.idProduct != FTDI_PID)
            continue;
        if (handle != NULL) {
            fprintf(stderr,
                "ci20-ftdi-uart: multiple 0403:6010 devices found\n");
            libusb_close(handle);
            handle = NULL;
            break;
        }
        if (libusb_open(devices[i], &handle) != 0)
            handle = NULL;
    }

    libusb_free_device_list(devices, 1);
    if (handle == NULL) {
        fprintf(stderr,
            "ci20-ftdi-uart: cannot open the sole FT2232D; run with sudo\n");
        exit(EXIT_FAILURE);
    }
    return handle;
}

static void
find_bulk_endpoints(libusb_device_handle *handle, unsigned char *endpoint_in,
    unsigned char *endpoint_out, int *max_packet)
{
    struct libusb_config_descriptor *configuration;
    libusb_device *device;
    const struct libusb_interface_descriptor *interface;
    int error;
    unsigned int i;

    device = libusb_get_device(handle);
    error = libusb_get_active_config_descriptor(device, &configuration);
    if (error != 0)
        fatal_libusb("read active USB configuration", error);
    if (configuration->bNumInterfaces <= FTDI_INTERFACE_B ||
        configuration->interface[FTDI_INTERFACE_B].num_altsetting < 1) {
        fprintf(stderr, "ci20-ftdi-uart: channel B descriptor missing\n");
        exit(EXIT_FAILURE);
    }

    interface = &configuration->interface[FTDI_INTERFACE_B].altsetting[0];
    for (i = 0; i < interface->bNumEndpoints; ++i) {
        const struct libusb_endpoint_descriptor *endpoint =
            &interface->endpoint[i];

        if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
            LIBUSB_TRANSFER_TYPE_BULK)
            continue;
        if ((endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
            LIBUSB_ENDPOINT_IN) {
            *endpoint_in = endpoint->bEndpointAddress;
            *max_packet = endpoint->wMaxPacketSize;
        } else {
            *endpoint_out = endpoint->bEndpointAddress;
        }
    }
    libusb_free_config_descriptor(configuration);

    if (*endpoint_in == 0 || *endpoint_out == 0 || *max_packet < 3) {
        fprintf(stderr, "ci20-ftdi-uart: invalid channel B endpoints\n");
        exit(EXIT_FAILURE);
    }
}

static void
write_ftdi(libusb_device_handle *handle, unsigned char endpoint,
    const unsigned char *data, int length)
{
    while (length > 0) {
        int transferred = 0;
        int error = libusb_bulk_transfer(handle, endpoint,
            (unsigned char *)(uintptr_t)data, length, &transferred, 1000);

        if (error != 0)
            fatal_libusb("UART write", error);
        if (transferred <= 0) {
            fprintf(stderr, "ci20-ftdi-uart: zero-length UART write\n");
            exit(EXIT_FAILURE);
        }
        data += transferred;
        length -= transferred;
    }
}

static void
emit_ftdi_rx(const unsigned char *data, int length, int max_packet)
{
    int offset = 0;

    while (offset < length) {
        int packet = length - offset;
        int written = 0;

        if (packet > max_packet)
            packet = max_packet;
        if (packet <= 2) {
            offset += packet;
            continue;
        }
        while (written < packet - 2) {
            ssize_t count = write(STDOUT_FILENO, data + offset + 2 + written,
                (size_t)(packet - 2 - written));
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                stopping = 1;
                return;
            }
            written += (int)count;
        }
        offset += packet;
    }
}

int
main(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle;
    struct termios raw;
    unsigned char endpoint_in = 0;
    unsigned char endpoint_out = 0;
    unsigned char rx[4096];
    unsigned char tx[256];
    int max_packet = 0;
    int error;

    error = libusb_init(&context);
    if (error != 0)
        fatal_libusb("initialize libusb", error);
    handle = open_ft2232d(context);
    find_bulk_endpoints(handle, &endpoint_in, &endpoint_out, &max_packet);

    error = libusb_claim_interface(handle, FTDI_INTERFACE_B);
    if (error != 0)
        fatal_libusb("claim FT2232D channel B", error);

    /*
     * Do not reset the USB device, set its configuration, purge FIFOs, or
     * detach another interface. OpenOCD owns channel A concurrently.
     */
    error = ftdi_control(handle, SIO_SET_BITMODE_REQUEST, 0);
    if (error < 0)
        fatal_libusb("select asynchronous UART mode on B", error);
    error = ftdi_control(handle, SIO_SET_BAUDRATE_REQUEST, 26);
    if (error < 0)
        fatal_libusb("set 115200 baud on B", error);
    error = ftdi_control(handle, SIO_SET_DATA_REQUEST, 8);
    if (error < 0)
        fatal_libusb("set 8N1 on B", error);
    error = ftdi_control(handle, SIO_SET_FLOW_CTRL_REQUEST, 0);
    if (error < 0)
        fatal_libusb("disable flow control on B", error);
    error = ftdi_control(handle, SIO_SET_LATENCY_TIMER_REQUEST, 1);
    if (error < 0)
        fatal_libusb("set latency timer on B", error);

    if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
        termios_saved = 1;
        raw = saved_termios;
        cfmakeraw(&raw);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            fprintf(stderr, "ci20-ftdi-uart: tcsetattr: %s\n",
                strerror(errno));
            return EXIT_FAILURE;
        }
        atexit(restore_terminal);
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);
    fprintf(stderr,
        "ci20-ftdi-uart: raw USB channel B, 115200 8N1; Ctrl-] exits\r\n");

    while (!stopping) {
        fd_set readfds;
        struct timeval timeout;
        int ready;
        int transferred = 0;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0 && errno != EINTR) {
            fprintf(stderr, "ci20-ftdi-uart: select: %s\r\n",
                strerror(errno));
            break;
        }
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t count = read(STDIN_FILENO, tx, sizeof(tx));
            size_t start = 0;
            size_t i;

            if (count <= 0)
                break;
            for (i = 0; i < (size_t)count; ++i) {
                if (tx[i] == 0x1d) {
                    if (i > start)
                        write_ftdi(handle, endpoint_out, tx + start,
                            (int)(i - start));
                    stopping = 1;
                    start = i + 1;
                    break;
                }
            }
            if (!stopping && start < (size_t)count)
                write_ftdi(handle, endpoint_out, tx + start,
                    (int)((size_t)count - start));
        }

        error = libusb_bulk_transfer(handle, endpoint_in, rx, sizeof(rx),
            &transferred, 5);
        if (error == 0)
            emit_ftdi_rx(rx, transferred, max_packet);
        else if (error != LIBUSB_ERROR_TIMEOUT)
            fatal_libusb("UART read", error);
    }

    restore_terminal();
    termios_saved = 0;
    (void)libusb_release_interface(handle, FTDI_INTERFACE_B);
    libusb_close(handle);
    libusb_exit(context);
    fprintf(stderr, "\r\nci20-ftdi-uart: closed\n");
    return EXIT_SUCCESS;
}
