#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <libusb.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#endif
#ifdef __APPLE__
#include <net/if_utun.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#ifndef UTUN_CONTROL_NAME
#define UTUN_CONTROL_NAME       "com.apple.net.utun_control"
#endif
#ifndef UTUN_OPT_IFNAME
#define UTUN_OPT_IFNAME         2
#endif
#endif

#include "n64usbnet_proto.h"

#define N64USB_VID              0x1209
#define N64USB_PID              0x6800
#define N64USB_EP_OUT           0x01
#define N64USB_EP_IN            0x82
#define N64USB_IFACE            0
#define USB_TIMEOUT_MS          10
#define USB_TRANSFER_RETRIES    8
#define ETH_HDR_LEN             14
#define ETH_MIN_LEN             60
#define ETH_TYPE_IP             0x0800
#define ETH_TYPE_ARP            0x0806
#define ARP_LEN                 28
#define ARP_OP_REQUEST          1
#define ARP_OP_REPLY            2
#define ARP_HRD_ETHER           1

enum backend_mode {
    BACKEND_NONE,
    BACKEND_TAP,
    BACKEND_UTUN,
};

static volatile sig_atomic_t stop_requested;

struct bridge {
    libusb_context *usb_ctx;
    libusb_device_handle *usb_dev;
    int net_fd;
    enum backend_mode backend;
    char net_name[IFNAMSIZ];
    unsigned char host_ip[4];
    unsigned char peer_ip[4];
    unsigned char host_mac[6];
    unsigned char peer_mac[6];
    int peer_mac_valid;
    int no_config;
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
        "       %s --utun [--host ip] [--peer ip] [--no-config] "
        "[--vid hex] [--pid hex] [-v]\n"
        "       %s --list\n"
        "       Linux TAP can be a device name such as tap-retrobsd.\n"
        "       BSD/macOS TAP should usually be a path such as /dev/tap0.\n"
        "       macOS utun carries IPv4 packets and proxies ARP for usbn0.\n",
        prog, prog, prog);
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
parse_ip4(const char *arg, unsigned char ip[4])
{
    return inet_pton(AF_INET, arg, ip) == 1 ? 0 : -1;
}

static void
ip4_to_string(const unsigned char ip[4], char *buf, size_t len)
{
    if (inet_ntop(AF_INET, ip, buf, len) == 0)
        snprintf(buf, len, "0.0.0.0");
}

static void
debug_ip_packet(struct bridge *br, const char *tag,
    const unsigned char *packet, unsigned len)
{
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];

    if (!br->verbose || len < 20)
        return;
    ip4_to_string(packet + 12, src, sizeof(src));
    ip4_to_string(packet + 16, dst, sizeof(dst));
    fprintf(stderr, "%s ip proto=%u %s -> %s len=%u\n", tag,
        packet[9], src, dst, len);
}

static unsigned
get16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static void
put16(unsigned char *p, unsigned value)
{
    p[0] = (value >> 8) & 0xff;
    p[1] = value & 0xff;
}

static void
copy_mac(unsigned char dst[6], const unsigned char *src)
{
    memcpy(dst, src, 6);
}

static int
is_zero_mac(const unsigned char mac[6])
{
    return mac[0] == 0 && mac[1] == 0 && mac[2] == 0 &&
        mac[3] == 0 && mac[4] == 0 && mac[5] == 0;
}

static void
learn_peer_mac(struct bridge *br, const unsigned char *mac)
{
    if (is_zero_mac(mac) || (mac[0] & 1))
        return;
    copy_mac(br->peer_mac, mac);
    br->peer_mac_valid = 1;
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
configure_utun(const char *ifname, const unsigned char host_ip[4],
    const unsigned char peer_ip[4])
{
#ifdef __APPLE__
    char host[INET_ADDRSTRLEN], peer[INET_ADDRSTRLEN];
    pid_t pid;
    int status;

    ip4_to_string(host_ip, host, sizeof(host));
    ip4_to_string(peer_ip, peer, sizeof(peer));

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl("/sbin/ifconfig", "ifconfig", ifname, "inet", host, peer,
            "up", (char *)0);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
#else
    (void)ifname;
    (void)host_ip;
    (void)peer_ip;
    errno = ENOSYS;
    return -1;
#endif
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
open_utun(char *ifname, size_t ifname_size)
{
#ifdef __APPLE__
    struct ctl_info info;
    struct sockaddr_ctl addr;
    socklen_t optlen;
    int fd;

    fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0)
        return -1;

    memset(&info, 0, sizeof(info));
    snprintf(info.ctl_name, sizeof(info.ctl_name), "%s", UTUN_CONTROL_NAME);
    if (ioctl(fd, CTLIOCGINFO, &info) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    optlen = ifname_size;
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname,
        &optlen) < 0) {
        close(fd);
        return -1;
    }
    ifname[ifname_size - 1] = 0;
    set_nonblock(fd);
    return fd;
#else
    (void)ifname;
    (void)ifname_size;
    errno = ENOSYS;
    return -1;
#endif
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

static void
print_usb_string(libusb_device_handle *handle, unsigned char index)
{
    unsigned char buf[128];
    int ret;

    if (index == 0 || handle == 0)
        return;
    ret = libusb_get_string_descriptor_ascii(handle, index, buf,
        sizeof(buf));
    if (ret > 0)
        fprintf(stderr, " \"%s\"", buf);
}

static void
list_usb_devices(libusb_context *ctx)
{
    libusb_device **list;
    ssize_t count, i;

    count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        fprintf(stderr, "libusb_get_device_list: %s\n",
            libusb_error_name((int)count));
        return;
    }

    fprintf(stderr, "visible USB devices:\n");
    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        libusb_device_handle *handle = 0;
        libusb_device *dev = list[i];
        int ret;

        ret = libusb_get_device_descriptor(dev, &desc);
        if (ret != 0)
            continue;
        fprintf(stderr, "  %03u:%03u %04x:%04x class=%02x",
            libusb_get_bus_number(dev), libusb_get_device_address(dev),
            desc.idVendor, desc.idProduct, desc.bDeviceClass);
        if (libusb_open(dev, &handle) == 0) {
            print_usb_string(handle, desc.iManufacturer);
            print_usb_string(handle, desc.iProduct);
            libusb_close(handle);
        }
        fprintf(stderr, "\n");
    }
    libusb_free_device_list(list, 1);
}

static int
open_usb(struct bridge *br, unsigned vid, unsigned pid, int quiet)
{
    int ret;

    ret = libusb_init(&br->usb_ctx);
    if (ret != 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(ret));
        return -1;
    }
    br->usb_dev = libusb_open_device_with_vid_pid(br->usb_ctx, vid, pid);
    if (br->usb_dev == 0) {
        if (!quiet) {
            fprintf(stderr, "USB device %04x:%04x not found\n", vid, pid);
            list_usb_devices(br->usb_ctx);
        }
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
close_usb(struct bridge *br)
{
    if (br->usb_dev != 0) {
        libusb_release_interface(br->usb_dev, N64USB_IFACE);
        libusb_close(br->usb_dev);
        br->usb_dev = 0;
    }
    if (br->usb_ctx != 0) {
        libusb_exit(br->usb_ctx);
        br->usb_ctx = 0;
    }
    n64usbnet_rx_reset(&br->rx);
    br->tx.active = 0;
}

static void
close_bridge(struct bridge *br)
{
    close_usb(br);
    if (br->net_fd >= 0)
        close(br->net_fd);
}

static int
usb_disconnected(int ret)
{
    return ret == LIBUSB_ERROR_NO_DEVICE || ret == LIBUSB_ERROR_IO ||
        ret == LIBUSB_ERROR_OTHER;
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
            return usb_disconnected(ret) ? -2 : -1;
        }
        if ((unsigned)actual != packet_len) {
            fprintf(stderr, "USB OUT short write %d/%u\n", actual,
                packet_len);
            return -1;
        }
    }
    if (br->verbose)
        fprintf(stderr, "host -> usb %u bytes\n", len);
    return 0;
}

static int
write_utun_ip(struct bridge *br, const unsigned char *packet, unsigned len)
{
#ifdef __APPLE__
    unsigned char buf[4 + N64USBNET_FRAME_MAX];
    uint32_t family = htonl(AF_INET);
    ssize_t n;

    if (len + 4 > sizeof(buf))
        return 0;
    memcpy(buf, &family, sizeof(family));
    memcpy(buf + 4, packet, len);
    debug_ip_packet(br, "usb -> utun", packet, len);
    n = write(br->net_fd, buf, len + 4);
    if (n < 0) {
        perror("utun write");
        return -1;
    }
    if ((unsigned)n != len + 4) {
        fprintf(stderr, "utun short write %zd/%u\n", n, len + 4);
        return -1;
    }
    if (br->verbose)
        fprintf(stderr, "usb -> utun %u bytes\n", len);
    return 0;
#else
    (void)br;
    (void)packet;
    (void)len;
    errno = ENOSYS;
    return -1;
#endif
}

static int
send_arp_reply(struct bridge *br, const unsigned char *frame, unsigned len)
{
    unsigned char reply[ETH_MIN_LEN];
    const unsigned char *arp;

    if (len < ETH_HDR_LEN + ARP_LEN)
        return 0;
    arp = frame + ETH_HDR_LEN;
    if (get16(arp) != ARP_HRD_ETHER ||
        get16(arp + 2) != ETH_TYPE_IP ||
        arp[4] != 6 || arp[5] != 4 ||
        get16(arp + 6) != ARP_OP_REQUEST ||
        memcmp(arp + 24, br->host_ip, 4) != 0)
        return 0;

    learn_peer_mac(br, arp + 8);
    memset(reply, 0, sizeof(reply));
    copy_mac(reply, frame + 6);
    copy_mac(reply + 6, br->host_mac);
    put16(reply + 12, ETH_TYPE_ARP);

    put16(reply + ETH_HDR_LEN, ARP_HRD_ETHER);
    put16(reply + ETH_HDR_LEN + 2, ETH_TYPE_IP);
    reply[ETH_HDR_LEN + 4] = 6;
    reply[ETH_HDR_LEN + 5] = 4;
    put16(reply + ETH_HDR_LEN + 6, ARP_OP_REPLY);
    copy_mac(reply + ETH_HDR_LEN + 8, br->host_mac);
    memcpy(reply + ETH_HDR_LEN + 14, br->host_ip, 4);
    copy_mac(reply + ETH_HDR_LEN + 18, arp + 8);
    memcpy(reply + ETH_HDR_LEN + 24, arp + 14, 4);

    if (br->verbose)
        fprintf(stderr, "utun arp reply\n");
    return send_frame_usb(br, reply, sizeof(reply));
}

static int
write_backend_frame(struct bridge *br, const unsigned char *frame,
    unsigned len)
{
    ssize_t n;
    unsigned type, iplen;

    if (br->backend == BACKEND_TAP) {
        n = write(br->net_fd, frame, len);
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

    if (len < ETH_HDR_LEN)
        return 0;
    learn_peer_mac(br, frame + 6);
    type = get16(frame + 12);
    if (type == ETH_TYPE_ARP)
        return send_arp_reply(br, frame, len);
    if (type != ETH_TYPE_IP)
        return 0;
    if (len < ETH_HDR_LEN + 20)
        return 0;
    iplen = get16(frame + ETH_HDR_LEN + 2);
    if (iplen < 20 || iplen > len - ETH_HDR_LEN)
        iplen = len - ETH_HDR_LEN;
    return write_utun_ip(br, frame + ETH_HDR_LEN, iplen);
}

static int
read_tap(struct bridge *br)
{
    unsigned char frame[N64USBNET_FRAME_MAX];
    int ret;
    ssize_t n;

    for (;;) {
        n = read(br->net_fd, frame, sizeof(frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            perror("tap read");
            return -1;
        }
        if (n == 0)
            return 0;
        ret = send_frame_usb(br, frame, (unsigned)n);
        if (ret < 0)
            return ret;
    }
}

static int
send_utun_packet(struct bridge *br, const unsigned char *packet, unsigned len)
{
    unsigned char frame[N64USBNET_FRAME_MAX];

    if (!br->peer_mac_valid) {
        fprintf(stderr, "utun: peer MAC is unknown, drop %u bytes\n", len);
        return 0;
    }
    if (len + ETH_HDR_LEN > sizeof(frame))
        return 0;
    copy_mac(frame, br->peer_mac);
    copy_mac(frame + 6, br->host_mac);
    put16(frame + 12, ETH_TYPE_IP);
    memcpy(frame + ETH_HDR_LEN, packet, len);
    return send_frame_usb(br, frame, len + ETH_HDR_LEN);
}

static int
read_utun(struct bridge *br)
{
#ifdef __APPLE__
    unsigned char buf[4 + N64USBNET_FRAME_MAX];
    uint32_t family;
    int ret;
    ssize_t n;

    for (;;) {
        n = read(br->net_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            perror("utun read");
            return -1;
        }
        if (n <= 4)
            return 0;
        memcpy(&family, buf, sizeof(family));
        if (ntohl(family) != AF_INET)
            continue;
        debug_ip_packet(br, "utun -> usb", buf + 4, (unsigned)n - 4);
        ret = send_utun_packet(br, buf + 4, (unsigned)n - 4);
        if (ret < 0)
            return ret;
    }
#else
    (void)br;
    errno = ENOSYS;
    return -1;
#endif
}

static int
read_backend(struct bridge *br)
{
    if (br->backend == BACKEND_TAP)
        return read_tap(br);
    return read_utun(br);
}

static int
write_usb_frame(struct bridge *br, const unsigned char *frame, unsigned len)
{
    return write_backend_frame(br, frame, len);
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
        return usb_disconnected(ret) ? -2 : -1;
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
        ret = write_usb_frame(br, br->rx.frame, br->rx.len);
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
    int use_utun = 0;
    int list_only = 0;
    unsigned vid = N64USB_VID;
    unsigned pid = N64USB_PID;
    int i, ret;

    memset(&br, 0, sizeof(br));
    br.net_fd = -1;
    parse_ip4("10.64.0.1", br.host_ip);
    parse_ip4("10.64.0.2", br.peer_ip);
    br.host_mac[0] = 0x02;
    br.host_mac[1] = 0x64;
    br.host_mac[2] = 0x00;
    br.host_mac[3] = 0x00;
    br.host_mac[4] = 0x00;
    br.host_mac[5] = 0x02;
    br.peer_mac[0] = 0x02;
    br.peer_mac[1] = 0x64;
    br.peer_mac[2] = 0x00;
    br.peer_mac[3] = 0x00;
    br.peer_mac[4] = 0x00;
    br.peer_mac[5] = 0x10;
    br.peer_mac_valid = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc)
            tap = argv[++i];
        else if (strcmp(argv[i], "--utun") == 0)
            use_utun = 1;
        else if (strcmp(argv[i], "--list") == 0)
            list_only = 1;
        else if (strcmp(argv[i], "--no-config") == 0)
            br.no_config = 1;
        else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            if (parse_ip4(argv[++i], br.host_ip) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            if (parse_ip4(argv[++i], br.peer_ip) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
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

    if (list_only) {
        if (tap != 0 || use_utun) {
            usage(argv[0]);
            return 2;
        }
        if (libusb_init(&br.usb_ctx) != 0) {
            fprintf(stderr, "libusb_init failed\n");
            return 1;
        }
        list_usb_devices(br.usb_ctx);
        close_bridge(&br);
        return 0;
    }

    if ((tap == 0 && !use_utun) || (tap != 0 && use_utun)) {
        usage(argv[0]);
        return 2;
    }

    if (use_utun) {
        br.backend = BACKEND_UTUN;
        br.net_fd = open_utun(br.net_name, sizeof(br.net_name));
        if (br.net_fd < 0) {
            perror("utun");
            return 1;
        }
        if (!br.no_config &&
            configure_utun(br.net_name, br.host_ip, br.peer_ip) < 0) {
            perror("ifconfig utun");
            close_bridge(&br);
            return 1;
        }
    } else {
        br.backend = BACKEND_TAP;
        snprintf(br.net_name, sizeof(br.net_name), "%s", tap);
        br.net_fd = open_tap(tap);
        if (br.net_fd < 0) {
            perror(tap);
            return 1;
        }
    }
    if (open_usb(&br, vid, pid, 0) < 0) {
        close_bridge(&br);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    n64usbnet_rx_reset(&br.rx);

    fprintf(stderr, "n64usbnet-bridge: %s <-> USB %04x:%04x\n",
        br.net_name, vid, pid);
    if (br.backend == BACKEND_UTUN) {
        char host[INET_ADDRSTRLEN], peer[INET_ADDRSTRLEN];

        ip4_to_string(br.host_ip, host, sizeof(host));
        ip4_to_string(br.peer_ip, peer, sizeof(peer));
        if (br.no_config) {
            fprintf(stderr, "configure macOS utun with:\n");
            fprintf(stderr, "  sudo ifconfig %s inet %s %s up\n",
                br.net_name, host, peer);
        } else {
            fprintf(stderr, "configured macOS utun:\n");
            fprintf(stderr, "  %s inet %s %s up\n", br.net_name, host,
                peer);
        }
        fprintf(stderr, "configure N64 with:\n");
        fprintf(stderr, "  /sbin/ifconfig usbn0 inet %s netmask 255.255.255.0 up\n",
            peer);
        fprintf(stderr, "  /usr/bin/ping -c 1 %s\n", host);
    }
    while (!stop_requested) {
        if (br.usb_dev == 0) {
            if (open_usb(&br, vid, pid, 1) == 0) {
                if (br.verbose)
                    fprintf(stderr, "USB device reconnected\n");
            } else {
                close_usb(&br);
                usleep(250000);
            }
            continue;
        }

        ret = read_backend(&br);
        if (ret == -2) {
            close_usb(&br);
            continue;
        }
        if (ret < 0)
            break;

        ret = poll_usb(&br);
        if (ret == -2) {
            close_usb(&br);
            continue;
        }
        if (ret < 0)
            break;
    }

    close_bridge(&br);
    return 0;
}
