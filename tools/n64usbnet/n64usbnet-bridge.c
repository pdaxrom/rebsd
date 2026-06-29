#include <errno.h>
#include <fcntl.h>
#include <libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif

#include "n64usbnet_proto.h"

#define N64USB_VID              0x1209
#define N64USB_PID              0x6800
#define N64USB_EP_OUT           0x01
#define N64USB_EP_IN            0x82
#define N64USB_IFACE            0
#define USB_TIMEOUT_MS          10
#define USB_TRANSFER_RETRIES    8

static volatile sig_atomic_t stop_requested;

struct bridge {
    libusb_context *usb_ctx;
    libusb_device_handle *usb_dev;
    int tap_fd;
    int verbose;
    unsigned seq;
    struct n64usbnet_tx tx;
    struct n64usbnet_rx rx;
};

static void
usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s --tap TAP [--vid hex] [--pid hex] [-v]\n"
        "       Linux TAP can be a device name such as tap-retrobsd.\n"
        "       BSD/macOS TAP should usually be a path such as /dev/tap0.\n",
        prog);
}

static void
on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int
parse_hex(const char *arg, unsigned *value)
{
    char *end;
    unsigned long v;

    errno = 0;
    v = strtoul(arg, &end, 0);
    if (errno || end == arg || *end != 0 || v > 0xffff)
        return -1;
    *value = (unsigned)v;
    return 0;
}

static int
set_nonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int
open_tap(const char *tap)
{
#ifdef __linux__
    struct ifreq ifr;
    int fd;

    if (tap[0] != '/') {
        fd = open("/dev/net/tun", O_RDWR);
        if (fd < 0)
            return -1;
        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", tap);
        if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
            close(fd);
            return -1;
        }
        set_nonblock(fd);
        return fd;
    }
#endif
    {
        int fd = open(tap, O_RDWR);
        if (fd < 0)
            return -1;
        set_nonblock(fd);
        return fd;
    }
}

static int
usb_bulk(libusb_device_handle *dev, unsigned char ep, unsigned char *data,
    int len, int *actual, unsigned timeout_ms)
{
    int ret, tries;

    for (tries = 0; tries < USB_TRANSFER_RETRIES; tries++) {
        ret = libusb_bulk_transfer(dev, ep, data, len, actual, timeout_ms);
        if (ret != LIBUSB_ERROR_PIPE)
            return ret;
        libusb_clear_halt(dev, ep);
    }
    return ret;
}

static int
open_usb(struct bridge *br, unsigned vid, unsigned pid)
{
    int ret;

    ret = libusb_init(&br->usb_ctx);
    if (ret != 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(ret));
        return -1;
    }
    br->usb_dev = libusb_open_device_with_vid_pid(br->usb_ctx, vid, pid);
    if (br->usb_dev == 0) {
        fprintf(stderr, "USB device %04x:%04x not found\n", vid, pid);
        return -1;
    }
    ret = libusb_set_configuration(br->usb_dev, 1);
    if (ret != 0 && ret != LIBUSB_ERROR_BUSY)
        fprintf(stderr, "libusb_set_configuration: %s\n",
            libusb_error_name(ret));
#ifdef __linux__
    if (libusb_kernel_driver_active(br->usb_dev, N64USB_IFACE) == 1)
        libusb_detach_kernel_driver(br->usb_dev, N64USB_IFACE);
#endif
    ret = libusb_claim_interface(br->usb_dev, N64USB_IFACE);
    if (ret != 0) {
        fprintf(stderr, "libusb_claim_interface: %s\n",
            libusb_error_name(ret));
        return -1;
    }
    return 0;
}

static void
close_bridge(struct bridge *br)
{
    if (br->usb_dev != 0) {
        libusb_release_interface(br->usb_dev, N64USB_IFACE);
        libusb_close(br->usb_dev);
    }
    if (br->usb_ctx != 0)
        libusb_exit(br->usb_ctx);
    if (br->tap_fd >= 0)
        close(br->tap_fd);
}

static int
send_frame_usb(struct bridge *br, const unsigned char *frame, unsigned len)
{
    unsigned char packet[N64USBNET_USB_PACKET];
    unsigned packet_len;
    int actual, ret;

    if (n64usbnet_tx_begin(&br->tx, frame, len, ++br->seq) !=
        N64USBNET_MORE)
        return -1;
    while (br->tx.active) {
        ret = n64usbnet_tx_next(&br->tx, packet, &packet_len);
        if (ret == N64USBNET_ERROR)
            return -1;
        actual = 0;
        ret = usb_bulk(br->usb_dev, N64USB_EP_OUT, packet, packet_len,
            &actual, 1000);
        if (ret != 0) {
            fprintf(stderr, "USB OUT: %s\n", libusb_error_name(ret));
            return -1;
        }
        if ((unsigned)actual != packet_len) {
            fprintf(stderr, "USB OUT short write %d/%u\n", actual,
                packet_len);
            return -1;
        }
    }
    if (br->verbose)
        fprintf(stderr, "tap -> usb %u bytes\n", len);
    return 0;
}

static int
read_tap(struct bridge *br)
{
    unsigned char frame[N64USBNET_FRAME_MAX];
    ssize_t n;

    for (;;) {
        n = read(br->tap_fd, frame, sizeof(frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            perror("tap read");
            return -1;
        }
        if (n == 0)
            return 0;
        if (send_frame_usb(br, frame, (unsigned)n) < 0)
            return -1;
    }
}

static int
write_tap_frame(struct bridge *br, const unsigned char *frame, unsigned len)
{
    ssize_t n;

    n = write(br->tap_fd, frame, len);
    if (n < 0) {
        perror("tap write");
        return -1;
    }
    if ((unsigned)n != len) {
        fprintf(stderr, "tap short write %zd/%u\n", n, len);
        return -1;
    }
    if (br->verbose)
        fprintf(stderr, "usb -> tap %u bytes\n", len);
    return 0;
}

static int
poll_usb(struct bridge *br)
{
    unsigned char packet[N64USBNET_USB_PACKET];
    int actual, ret, rxret;

    actual = 0;
    ret = usb_bulk(br->usb_dev, N64USB_EP_IN, packet, sizeof(packet),
        &actual, USB_TIMEOUT_MS);
    if (ret == LIBUSB_ERROR_TIMEOUT)
        return 0;
    if (ret != 0) {
        fprintf(stderr, "USB IN: %s\n", libusb_error_name(ret));
        return -1;
    }
    if (actual <= 0)
        return 0;
    rxret = n64usbnet_rx_push(&br->rx, packet, (unsigned)actual);
    if (rxret == N64USBNET_ERROR) {
        fprintf(stderr, "USB IN framing error\n");
        n64usbnet_rx_reset(&br->rx);
        return 0;
    }
    if (rxret == N64USBNET_DONE) {
        ret = write_tap_frame(br, br->rx.frame, br->rx.len);
        n64usbnet_rx_reset(&br->rx);
        return ret;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    struct bridge br;
    const char *tap = 0;
    unsigned vid = N64USB_VID;
    unsigned pid = N64USB_PID;
    int i;

    memset(&br, 0, sizeof(br));
    br.tap_fd = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc)
            tap = argv[++i];
        else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
            if (parse_hex(argv[++i], &vid) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            if (parse_hex(argv[++i], &pid) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--verbose") == 0)
            br.verbose = 1;
        else {
            usage(argv[0]);
            return 2;
        }
    }

    if (tap == 0) {
        usage(argv[0]);
        return 2;
    }

    br.tap_fd = open_tap(tap);
    if (br.tap_fd < 0) {
        perror(tap);
        return 1;
    }
    if (open_usb(&br, vid, pid) < 0) {
        close_bridge(&br);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    n64usbnet_rx_reset(&br.rx);

    fprintf(stderr, "n64usbnet-bridge: %s <-> USB %04x:%04x\n", tap,
        vid, pid);
    while (!stop_requested) {
        if (read_tap(&br) < 0)
            break;
        if (poll_usb(&br) < 0)
            break;
    }

    close_bridge(&br);
    return 0;
}
