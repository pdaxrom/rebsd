/*
 * N64cart USB transport for the generic usbn(4) interface.
 *
 * The cartridge exposes an RP2040-style USB device controller through PI
 * address space.  By default this file builds the original vendor-specific
 * two-bulk-endpoint transport.  n64cart_usbecm.c includes this file with
 * N64USB_CDC_ECM set and exposes the same if_usbn lower-half API as a CDC ECM
 * USB Ethernet gadget.  n64cart_usbgdb.c selects CDC ACM instead and exposes
 * a small polling byte-stream API for the crash-safe GDB remote stub.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <machine/io.h>
#include <machine/n64int.h>
#include <machine/n64cart_uart.h>
#include <machine/n64pi.h>
#include <mips/common/if_usbn.h>
#ifndef N64USB_CDC_ECM
#define N64USB_CDC_ECM 0
#endif
#ifndef N64USB_GDB
#define N64USB_GDB 0
#endif
#if N64USB_CDC_ECM && N64USB_GDB
#error "CDC ECM and USB GDB modes are mutually exclusive"
#endif
#if !N64USB_CDC_ECM && !N64USB_GDB
#include <mips/common/n64usbnet_proto.h>
#endif

#define N64_PI_STATUS_DMA_BUSY          0x01u
#define N64_PI_STATUS_IO_BUSY           0x02u
#define N64_PI_STATUS_BUSY              (N64_PI_STATUS_DMA_BUSY | \
                                         N64_PI_STATUS_IO_BUSY)

#define N64CART_USBCFG_PHYS             0x1fd01020u
#define N64CART_USB_IRQ_ENABLE          0x0010u
#define N64CART_USB_BSWAP32             0x0080u
#define N64CART_USB_RESET               0x0001u

#define USBCTRL_DPRAM_BASE              0x1fe00000u
#define USBCTRL_REGS_BASE               0x1fe10000u
#define USB_REG_SET                     0x2000u
#define USB_REG_CLR                     0x3000u

#define USB_ADDR_ENDP                   0x00u
#define USB_MAIN_CTRL                   0x40u
#define USB_SIE_CTRL                    0x4cu
#define USB_SIE_STATUS                  0x50u
#define USB_BUFF_STATUS                 0x58u
#define USB_USB_MUXING                  0x74u
#define USB_USB_PWR                     0x78u
#define USB_INTE                        0x90u
#define USB_INTS                        0x98u

#define USB_DPRAM_SETUP                 0x000u
#define USB_DPRAM_EP1_OUT_CTRL          0x00cu
#define USB_DPRAM_EP2_IN_CTRL           0x010u
#define USB_DPRAM_EP3_IN_CTRL           0x018u
#define USB_DPRAM_EP0_IN_BUF_CTRL       0x080u
#define USB_DPRAM_EP0_OUT_BUF_CTRL      0x084u
#define USB_DPRAM_EP1_OUT_BUF_CTRL      0x08cu
#define USB_DPRAM_EP2_IN_BUF_CTRL       0x090u
#define USB_DPRAM_EP3_IN_BUF_CTRL       0x098u
#define USB_DPRAM_EP0_BUF               0x100u
#define USB_DPRAM_EP1_OUT_BUF           0x180u
#define USB_DPRAM_EP2_IN_BUF            0x1c0u
#define USB_DPRAM_EP3_IN_BUF            0x200u
#define USB_DPRAM_SIZE                  4096u

#define USB_BUF_CTRL_FULL               0x00008000u
#define USB_BUF_CTRL_DATA1_PID          0x00002000u
#define USB_BUF_CTRL_AVAIL              0x00000400u
#define USB_BUF_CTRL_LEN_MASK           0x000003ffu
#define USB_EP_CTRL_ENABLE              0x80000000u
#define USB_EP_CTRL_INT_PER_BUFFER      0x20000000u
#define USB_EP_CTRL_TYPE_SHIFT          26u

#define USB_DIR_IN                      0x80u
#define USB_DIR_OUT                     0x00u
#define USB_DT_DEVICE                   0x01u
#define USB_DT_CONFIG                   0x02u
#define USB_DT_STRING                   0x03u
#define USB_DT_INTERFACE                0x04u
#define USB_DT_ENDPOINT                 0x05u
#define USB_DT_INTERFACE_ASSOCIATION    0x0bu
#define USB_DT_CS_INTERFACE             0x24u
#define USB_REQUEST_GET_DESCRIPTOR      0x06u
#define USB_REQUEST_SET_ADDRESS         0x05u
#define USB_REQUEST_SET_CONFIGURATION   0x09u
#define USB_REQUEST_GET_INTERFACE       0x0au
#define USB_REQUEST_SET_INTERFACE       0x0bu
#define USB_CDC_SET_ETHERNET_PACKET_FILTER 0x43u
#define USB_CDC_GET_ETHERNET_STATISTIC  0x44u
#define USB_CDC_NOTIFY_NETWORK_CONNECTION 0x00u
#define USB_CDC_NOTIFY_CONNECTION_SPEED_CHANGE 0x2au
#define USB_CDC_SET_LINE_CODING         0x20u
#define USB_CDC_GET_LINE_CODING         0x21u
#define USB_CDC_SET_CONTROL_LINE_STATE  0x22u
#define USB_CDC_SEND_BREAK              0x23u

#define USB_MAIN_CTRL_CONTROLLER_EN     0x00000001u
#define USB_SIE_CTRL_EP0_INT_1BUF       0x20000000u
#define USB_SIE_CTRL_PULLUP_EN          0x00010000u
#define USB_SIE_STATUS_BUS_RESET        0x00080000u
#define USB_SIE_STATUS_SETUP_REC        0x00020000u
#define USB_SIE_STATUS_CONNECTED        0x00010000u
#define USB_INTS_SETUP_REQ              0x00010000u
#define USB_INTS_DEV_CONN_DIS           0x00002000u
#define USB_INTS_BUS_RESET              0x00001000u
#define USB_INTS_BUFF_STATUS            0x00000010u
#define USB_USB_MUXING_TO_PHY           0x00000001u
#define USB_USB_MUXING_SOFTCON          0x00000008u
#define USB_USB_PWR_VBUS_DETECT         0x00000004u
#define USB_USB_PWR_VBUS_DETECT_OVERRIDE 0x00000008u

#define EP0_OUT_ADDR                    0x00u
#define EP0_IN_ADDR                     0x80u
#define EP1_OUT_ADDR                    0x01u
#define EP2_IN_ADDR                     0x82u
#define EP3_IN_ADDR                     0x83u

#define USB_PACKET_SIZE                 64u
#define USB_NET_UNIT                    0
#define USB_NUM_ENDPOINTS               16u
#define N64USB_FRAME_MAX                1518u
#define N64USB_ETH_HEADER_LEN           14u
#define N64USB_GDB_RX_RING_SIZE         4096u

struct n64usb_ep {
    unsigned char addr;
    unsigned char attrs;
    unsigned ctrl_offset;
    unsigned buf_ctrl_offset;
    unsigned data_offset;
    unsigned char next_pid;
    void (*handler)(unsigned char *, unsigned);
};

static void n64usb_ep0_in(unsigned char *buf, unsigned len);
static void n64usb_ep0_out(unsigned char *buf, unsigned len);
static void n64usb_ep1_out(unsigned char *buf, unsigned len);
static void n64usb_ep2_in(unsigned char *buf, unsigned len);
#if N64USB_CDC_ECM || N64USB_GDB
static void n64usb_ep3_in(unsigned char *buf, unsigned len);
#endif

static struct n64usb_ep n64usb_eps[] = {
    { EP0_OUT_ADDR, 0, 0, USB_DPRAM_EP0_OUT_BUF_CTRL, USB_DPRAM_EP0_BUF,
        0, n64usb_ep0_out },
    { EP0_IN_ADDR, 0, 0, USB_DPRAM_EP0_IN_BUF_CTRL, USB_DPRAM_EP0_BUF,
        0, n64usb_ep0_in },
    { EP1_OUT_ADDR, 2, USB_DPRAM_EP1_OUT_CTRL, USB_DPRAM_EP1_OUT_BUF_CTRL,
        USB_DPRAM_EP1_OUT_BUF, 0, n64usb_ep1_out },
    { EP2_IN_ADDR, 2, USB_DPRAM_EP2_IN_CTRL, USB_DPRAM_EP2_IN_BUF_CTRL,
        USB_DPRAM_EP2_IN_BUF, 0, n64usb_ep2_in },
#if N64USB_CDC_ECM || N64USB_GDB
    { EP3_IN_ADDR, 3, USB_DPRAM_EP3_IN_CTRL, USB_DPRAM_EP3_IN_BUF_CTRL,
        USB_DPRAM_EP3_IN_BUF, 0, n64usb_ep3_in },
#endif
};

static const unsigned char n64usb_device_desc[] = {
    18, USB_DT_DEVICE,
    0x00, 0x02,
#if N64USB_CDC_ECM || N64USB_GDB
    0xef, 0x02, 0x01, USB_PACKET_SIZE,
#else
    0, 0, 0, USB_PACKET_SIZE,
#endif
    0x09, 0x12,
    0x00, 0x68,
#if N64USB_CDC_ECM
    0x01, 0x00,
#elif N64USB_GDB
    0x02, 0x00,
#else
    0x00, 0x00,
#endif
    1, 2, 0, 1
};

static const unsigned char n64usb_config_desc[] = {
#if N64USB_CDC_ECM
    9, USB_DT_CONFIG,
    79, 0,
    2, 1, 0, 0xc0, 0x32,

    8, USB_DT_INTERFACE_ASSOCIATION,
    0, 2, 0x02, 0x06, 0x00, 0,

    9, USB_DT_INTERFACE,
    0, 0, 1, 0x02, 0x06, 0x00, 0,
    5, USB_DT_CS_INTERFACE,
    0x00, 0x10, 0x01,
    5, USB_DT_CS_INTERFACE,
    0x06, 0, 1,
    13, USB_DT_CS_INTERFACE,
    0x0f, 3,
    0x00, 0x00, 0x00, 0x00,
    0xea, 0x05,
    0x00, 0x00, 0,
    7, USB_DT_ENDPOINT,
    EP3_IN_ADDR, 3, 16, 0, 16,

    9, USB_DT_INTERFACE,
    1, 0, 2, 0x0a, 0x00, 0x00, 0,
    7, USB_DT_ENDPOINT,
    EP1_OUT_ADDR, 2, 64, 0, 0,
    7, USB_DT_ENDPOINT,
    EP2_IN_ADDR, 2, 64, 0, 0
#elif N64USB_GDB
    9, USB_DT_CONFIG,
    75, 0,
    2, 1, 0, 0xc0, 0x32,

    8, USB_DT_INTERFACE_ASSOCIATION,
    0, 2, 0x02, 0x02, 0x01, 0,

    9, USB_DT_INTERFACE,
    0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, USB_DT_CS_INTERFACE,
    0x00, 0x10, 0x01,
    5, USB_DT_CS_INTERFACE,
    0x01, 0x00, 1,
    4, USB_DT_CS_INTERFACE,
    0x02, 0x02,
    5, USB_DT_CS_INTERFACE,
    0x06, 0, 1,
    7, USB_DT_ENDPOINT,
    EP3_IN_ADDR, 3, 16, 0, 16,

    9, USB_DT_INTERFACE,
    1, 0, 2, 0x0a, 0x00, 0x00, 0,
    7, USB_DT_ENDPOINT,
    EP1_OUT_ADDR, 2, 64, 0, 0,
    7, USB_DT_ENDPOINT,
    EP2_IN_ADDR, 2, 64, 0, 0
#else
    9, USB_DT_CONFIG,
    32, 0,
    1, 1, 0, 0xc0, 0x32,
    9, 4,
    0, 0, 2, 0xff, 0, 0, 0,
    7, 5,
    EP1_OUT_ADDR, 2, 64, 0, 0,
    7, 5,
    EP2_IN_ADDR, 2, 64, 0, 0
#endif
};

static const unsigned char n64usb_lang_desc[] = {
    4, USB_DT_STRING, 0x09, 0x04
};

#if !N64USB_CDC_ECM && !N64USB_GDB
static const unsigned char n64usb_ms_os_string[] = {
    0x12, USB_DT_STRING,
    'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
    0x69, 0x00
};

static const unsigned char n64usb_ms_winusb_desc[] = {
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x01,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00,
    0x01,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};
#endif

static const char n64usb_vendor[] = "pdaXrom.org";
#if N64USB_CDC_ECM
static const char n64usb_product[] = "N64cart CDC ECM";
static const char n64usb_host_mac_string[] = "026400000001";
#elif N64USB_GDB
static const char n64usb_product[] = "N64cart GDB";
#else
static const char n64usb_product[] = "N64cart USBNet";
#endif

#if !N64USB_GDB
static unsigned char n64usb_device_mac[6] =
    { 0x02, 0x64, 0x00, 0x00, 0x00, 0x10 };
#endif
static unsigned char n64usb_ep0_buf[USB_PACKET_SIZE];
static const unsigned char *n64usb_ep0_data;
static unsigned n64usb_ep0_len;
static unsigned n64usb_ep0_pos;
static unsigned n64usb_io_words[USB_PACKET_SIZE / sizeof(unsigned)];
#if N64USB_CDC_ECM
static const unsigned char *n64usb_tx_frame;
static unsigned n64usb_tx_len;
static unsigned n64usb_tx_pos;
static int n64usb_tx_zlp;
static unsigned char n64usb_rx_frame[N64USB_FRAME_MAX];
static unsigned n64usb_rx_pos;
static int n64usb_data_alt;
static unsigned char n64usb_ecm_notify_buf[16];
static int n64usb_ecm_notify_busy;
static int n64usb_ecm_speed_pending;
#elif N64USB_GDB
static unsigned char n64usb_gdb_rx_ring[N64USB_GDB_RX_RING_SIZE];
static unsigned n64usb_gdb_rx_get;
static unsigned n64usb_gdb_rx_put;
static volatile int n64usb_gdb_break_pending;
static volatile int n64usb_gdb_attach_pending;
static unsigned char n64usb_acm_line_coding[7] =
    { 0x00, 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08 };
static unsigned n64usb_ep0_out_pending;
volatile unsigned n64_gdb_usb_active;
#else
static struct n64usbnet_tx n64usb_tx;
static struct n64usbnet_rx n64usb_rx;
static unsigned n64usb_tx_seq;
#endif
static int n64usb_initialized;
static int n64usb_configured;
static int n64usb_should_set_addr;
static unsigned char n64usb_dev_addr;
static int n64usb_tx_usb_busy;
static char n64usb_pi_owner;
#ifdef N64_MINIMAL_USBNET_DEBUG
static unsigned n64usb_debug_last_ints;
static int n64usb_debug_poll_logged;
#endif

static void n64usb_hw_start(void);
static void n64usb_poll_controller(void);

static int
n64usb_pi_enter(int *saved_status)
{
    *saved_status = 0;
    if (n64pi_bus_enter(&n64usb_pi_owner) != 0)
        return 0;
    return 1;
}

static void
n64usb_pi_leave(int saved_status)
{
    (void)saved_status;
    n64pi_bus_leave(&n64usb_pi_owner);
}

static void
n64usb_pi_wait(void)
{
    volatile unsigned *status = (volatile unsigned *)N64_PI_STATUS_ADDR;

    while (*status & N64_PI_STATUS_BUSY)
        ;
}

static unsigned
n64usb_read_phys(unsigned phys)
{
    unsigned value;
    volatile unsigned *addr = (volatile unsigned *)(0xa0000000u | phys);

    n64usb_pi_wait();
    asm volatile ("" ::: "memory");
    value = *addr;
    asm volatile ("" ::: "memory");
    return value;
}

static void
n64usb_write_phys(unsigned phys, unsigned value)
{
    volatile unsigned *addr = (volatile unsigned *)(0xa0000000u | phys);

    n64usb_pi_wait();
    asm volatile ("" ::: "memory");
    *addr = value;
    asm volatile ("" ::: "memory");
    n64usb_pi_wait();
}

static unsigned
n64usb_reg_read(unsigned offset)
{
    return n64usb_read_phys(USBCTRL_REGS_BASE + offset);
}

static void
n64usb_reg_write(unsigned offset, unsigned value)
{
    n64usb_write_phys(USBCTRL_REGS_BASE + offset, value);
}

static void
n64usb_reg_clear(unsigned offset, unsigned value)
{
    n64usb_write_phys(USBCTRL_REGS_BASE + USB_REG_CLR + offset, value);
}

static unsigned
n64usb_dpram_read(unsigned offset)
{
    return n64usb_read_phys(USBCTRL_DPRAM_BASE + offset);
}

static void
n64usb_dpram_write(unsigned offset, unsigned value)
{
    n64usb_write_phys(USBCTRL_DPRAM_BASE + offset, value);
}

static void
n64usb_usb_mode(int bswap)
{
    n64usb_write_phys(N64CART_USBCFG_PHYS,
        N64CART_USB_IRQ_ENABLE | (bswap ? N64CART_USB_BSWAP32 : 0));
}

static void
n64usb_enable_cart_interrupt(void)
{
    unsigned status;

    status = mips_read_c0_register(C0_STATUS, 0);
    mips_write_c0_register(C0_STATUS, 0, status | ST_IM3);
}

static void
n64usb_reset_endpoint_toggles(void)
{
    unsigned i;

    for (i = 0; i < sizeof(n64usb_eps) / sizeof(n64usb_eps[0]); i++)
        n64usb_eps[i].next_pid = 0;
}

static void
n64usb_copyin_words(unsigned offset, unsigned char *buf, unsigned len)
{
    unsigned words, i;
    unsigned char *tmp;

    words = (len + 3) >> 2;
    tmp = (unsigned char *)n64usb_io_words;
    n64usb_usb_mode(1);
    for (i = 0; i < words; i++)
        n64usb_io_words[i] = n64usb_dpram_read(offset + i * 4);
    n64usb_usb_mode(0);
    for (i = 0; i < len; i++)
        buf[i] = tmp[i];
}

static void
n64usb_copyout_words(unsigned offset, const unsigned char *buf, unsigned len)
{
    unsigned words, i;
    unsigned char *tmp;

    words = (len + 3) >> 2;
    tmp = (unsigned char *)n64usb_io_words;
    for (i = 0; i < sizeof(n64usb_io_words); i++)
        tmp[i] = 0;
    for (i = 0; i < len; i++)
        tmp[i] = buf[i];
    n64usb_usb_mode(1);
    for (i = 0; i < words; i++)
        n64usb_dpram_write(offset + i * 4, n64usb_io_words[i]);
    n64usb_usb_mode(0);
}

static unsigned
n64usb_get16(const unsigned char *p)
{
    unsigned value;

    value = ((unsigned)p[0] << 8) | (unsigned)p[1];
    return ((value & 0xff00) >> 8) | ((value & 0x00ff) << 8);
}

static struct n64usb_ep *
n64usb_find_ep(unsigned char addr)
{
    unsigned i;

    for (i = 0; i < sizeof(n64usb_eps) / sizeof(n64usb_eps[0]); i++)
        if (n64usb_eps[i].addr == addr)
            return &n64usb_eps[i];
    return 0;
}

static void
n64usb_start_transfer(struct n64usb_ep *ep, const unsigned char *buf,
    unsigned len)
{
    unsigned value;

    if (ep == 0 || len > USB_PACKET_SIZE)
        return;

    value = len | USB_BUF_CTRL_AVAIL;
    if (ep->addr & USB_DIR_IN) {
        if (len)
            n64usb_copyout_words(ep->data_offset, buf, len);
        value |= USB_BUF_CTRL_FULL;
    }
    if (ep->next_pid)
        value |= USB_BUF_CTRL_DATA1_PID;
    ep->next_pid ^= 1;
    n64usb_dpram_write(ep->buf_ctrl_offset, value);
}

static void
n64usb_setup_endpoint(struct n64usb_ep *ep)
{
    unsigned value;

    if (ep->ctrl_offset == 0)
        return;
    value = USB_EP_CTRL_ENABLE | USB_EP_CTRL_INT_PER_BUFFER |
        (ep->attrs << USB_EP_CTRL_TYPE_SHIFT) | ep->data_offset;
    n64usb_dpram_write(ep->ctrl_offset, value);
}

static unsigned
n64usb_string_descriptor(const char *str, unsigned char *buf)
{
    unsigned len, i;

    len = 2;
    while (str[len / 2 - 1] != 0 && len + 2 <= USB_PACKET_SIZE)
        len += 2;
    buf[0] = len;
    buf[1] = USB_DT_STRING;
    for (i = 2; i < len; i += 2) {
        buf[i] = (unsigned char)*str++;
        buf[i + 1] = 0;
    }
    return len;
}

#if N64USB_CDC_ECM
static void
n64usb_put32le(unsigned char *p, unsigned value)
{

    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
}

static void
n64usb_ecm_notify_speed(void)
{

    n64usb_ecm_notify_buf[0] = 0xa1;
    n64usb_ecm_notify_buf[1] = USB_CDC_NOTIFY_CONNECTION_SPEED_CHANGE;
    n64usb_ecm_notify_buf[2] = 0;
    n64usb_ecm_notify_buf[3] = 0;
    n64usb_ecm_notify_buf[4] = 0;
    n64usb_ecm_notify_buf[5] = 0;
    n64usb_ecm_notify_buf[6] = 8;
    n64usb_ecm_notify_buf[7] = 0;
    n64usb_put32le(n64usb_ecm_notify_buf + 8, 12000000);
    n64usb_put32le(n64usb_ecm_notify_buf + 12, 12000000);
    n64usb_start_transfer(n64usb_find_ep(EP3_IN_ADDR),
        n64usb_ecm_notify_buf, 16);
}

static void
n64usb_ecm_notify_link_up(void)
{

    if (!n64usb_configured || n64usb_ecm_notify_busy) {
        n64usb_ecm_speed_pending = 1;
        return;
    }
    n64usb_ecm_notify_busy = 1;
    n64usb_ecm_speed_pending = 1;
    n64usb_ecm_notify_buf[0] = 0xa1;
    n64usb_ecm_notify_buf[1] = USB_CDC_NOTIFY_NETWORK_CONNECTION;
    n64usb_ecm_notify_buf[2] = 1;
    n64usb_ecm_notify_buf[3] = 0;
    n64usb_ecm_notify_buf[4] = 0;
    n64usb_ecm_notify_buf[5] = 0;
    n64usb_ecm_notify_buf[6] = 0;
    n64usb_ecm_notify_buf[7] = 0;
    n64usb_start_transfer(n64usb_find_ep(EP3_IN_ADDR),
        n64usb_ecm_notify_buf, 8);
}
#endif

static void
n64usb_ep0_send_next(void)
{
    struct n64usb_ep *ep;
    unsigned chunk;

    ep = n64usb_find_ep(EP0_IN_ADDR);
    if (ep == 0)
        return;
    chunk = n64usb_ep0_len - n64usb_ep0_pos;
    if (chunk > USB_PACKET_SIZE)
        chunk = USB_PACKET_SIZE;
    n64usb_start_transfer(ep,
        n64usb_ep0_data ? n64usb_ep0_data + n64usb_ep0_pos : 0,
        chunk);
    n64usb_ep0_pos += chunk;
}

static void
n64usb_ep0_send(const unsigned char *buf, unsigned len, unsigned wlength)
{
    struct n64usb_ep *ep;

    if (len > wlength)
        len = wlength;
    ep = n64usb_find_ep(EP0_IN_ADDR);
    if (ep == 0)
        return;
    ep->next_pid = 1;
    n64usb_ep0_data = buf;
    n64usb_ep0_len = len;
    n64usb_ep0_pos = 0;
    n64usb_ep0_send_next();
}

static void
n64usb_ep0_ack(void)
{
    n64usb_ep0_send(0, 0, 0);
}

static void
n64usb_reset_state(void)
{
    n64usb_dev_addr = 0;
    n64usb_should_set_addr = 0;
    n64usb_ep0_data = 0;
    n64usb_ep0_len = 0;
    n64usb_ep0_pos = 0;
    n64usb_configured = 0;
    n64usb_tx_usb_busy = 0;
#if N64USB_CDC_ECM
    n64usb_tx_frame = 0;
    n64usb_tx_len = 0;
    n64usb_tx_pos = 0;
    n64usb_tx_zlp = 0;
    n64usb_rx_pos = 0;
    n64usb_data_alt = 0;
    n64usb_ecm_notify_busy = 0;
    n64usb_ecm_speed_pending = 0;
#elif N64USB_GDB
    n64usb_gdb_rx_get = 0;
    n64usb_gdb_rx_put = 0;
    n64usb_gdb_break_pending = 0;
    n64usb_gdb_attach_pending = 0;
    n64usb_ep0_out_pending = 0;
    n64_gdb_usb_active = 0;
#else
    n64usb_tx.active = 0;
    n64usbnet_rx_reset(&n64usb_rx);
#endif
    n64usb_reset_endpoint_toggles();
#if !N64USB_GDB
    usbn_link_reset(USB_NET_UNIT);
#endif
}

static void
n64usb_bus_reset(void)
{
    n64usb_reset_state();
    n64usb_reg_write(USB_ADDR_ENDP, 0);
}

static int
n64usb_connection_change(void)
{
    unsigned sie_status;

    sie_status = n64usb_reg_read(USB_SIE_STATUS);
    n64usb_reg_clear(USB_SIE_STATUS, USB_SIE_STATUS_CONNECTED);
    if (sie_status & USB_SIE_STATUS_CONNECTED) {
        n64usb_bus_reset();
        return 1;
    }
    n64usb_hw_start();
    return 0;
}

static void
n64usb_handle_get_descriptor(const unsigned char *setup)
{
    unsigned value, dtype, dindex, wlength, len;

    value = n64usb_get16(setup + 2);
    wlength = n64usb_get16(setup + 6);
    dtype = value >> 8;
    dindex = value & 0xff;
    switch (dtype) {
    case USB_DT_DEVICE:
        n64usb_ep0_send(n64usb_device_desc, sizeof(n64usb_device_desc),
            wlength);
        break;

    case USB_DT_CONFIG:
        n64usb_ep0_send(n64usb_config_desc, sizeof(n64usb_config_desc),
            wlength);
        break;

    case USB_DT_STRING:
        if (dindex == 0)
            n64usb_ep0_send(n64usb_lang_desc, sizeof(n64usb_lang_desc),
                wlength);
#if !N64USB_CDC_ECM && !N64USB_GDB
        else if (dindex == 0xee)
            n64usb_ep0_send(n64usb_ms_os_string,
                sizeof(n64usb_ms_os_string), wlength);
#endif
        else if (dindex == 1) {
            len = n64usb_string_descriptor(n64usb_vendor,
                n64usb_ep0_buf);
            n64usb_ep0_send(n64usb_ep0_buf, len, wlength);
        } else if (dindex == 2) {
            len = n64usb_string_descriptor(n64usb_product,
                n64usb_ep0_buf);
            n64usb_ep0_send(n64usb_ep0_buf, len, wlength);
#if N64USB_CDC_ECM
        } else if (dindex == 3) {
            len = n64usb_string_descriptor(n64usb_host_mac_string,
                n64usb_ep0_buf);
            n64usb_ep0_send(n64usb_ep0_buf, len, wlength);
#endif
        } else
            n64usb_ep0_send(0, 0, wlength);
        break;

    default:
        n64usb_ep0_send(0, 0, wlength);
        break;
    }
}

static void
n64usb_handle_setup(void)
{
    unsigned char setup[8];
    unsigned reqtype, req, wvalue, windex, wlength;

    n64usb_copyin_words(USB_DPRAM_SETUP, setup, sizeof(setup));
    reqtype = setup[0];
    req = setup[1];
    wvalue = n64usb_get16(setup + 2);
    windex = n64usb_get16(setup + 4);
    wlength = n64usb_get16(setup + 6);
    (void)windex;

    n64usb_find_ep(EP0_IN_ADDR)->next_pid = 1;
    n64usb_find_ep(EP0_OUT_ADDR)->next_pid = 1;
#if N64USB_GDB
    n64usb_ep0_out_pending = 0;
#endif
    if (reqtype == USB_DIR_OUT) {
        if (req == USB_REQUEST_SET_ADDRESS) {
            n64usb_dev_addr = wvalue & 0x7f;
            n64usb_should_set_addr = 1;
            n64usb_ep0_ack();
        } else if (req == USB_REQUEST_SET_CONFIGURATION) {
            n64usb_configured = (wvalue & 0xff) != 0;
#if N64USB_CDC_ECM
            n64usb_data_alt = 1;
#endif
            n64usb_ep0_ack();
#if N64USB_CDC_ECM
            if (n64usb_configured) {
                n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
                    USB_PACKET_SIZE);
                n64usb_ecm_notify_link_up();
            }
#else
            if (n64usb_configured)
                n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
                    USB_PACKET_SIZE);
#endif
        } else if (req == USB_REQUEST_SET_INTERFACE) {
#if N64USB_CDC_ECM
            if (windex == 1) {
                n64usb_data_alt = (wvalue & 0xff) == 0;
                n64usb_ep0_ack();
                if (n64usb_configured && n64usb_data_alt)
                    n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR),
                        0, USB_PACKET_SIZE);
                if (n64usb_configured && n64usb_data_alt)
                    n64usb_ecm_notify_link_up();
            } else
                n64usb_ep0_ack();
#else
            n64usb_ep0_ack();
#endif
        } else
            n64usb_ep0_ack();
    } else if (reqtype == USB_DIR_IN) {
        if (req == USB_REQUEST_GET_DESCRIPTOR)
            n64usb_handle_get_descriptor(setup);
        else if (req == USB_REQUEST_GET_INTERFACE) {
#if N64USB_CDC_ECM
            n64usb_ep0_buf[0] = 0;
            n64usb_ep0_send(n64usb_ep0_buf, 1, wlength);
#else
            n64usb_ep0_send(0, 0, wlength);
#endif
        } else
            n64usb_ep0_send(0, 0, wlength);
#if N64USB_CDC_ECM
    } else if (reqtype == 0xa1 &&
        req == USB_CDC_GET_ETHERNET_STATISTIC) {
        n64usb_ep0_buf[0] = 0;
        n64usb_ep0_buf[1] = 0;
        n64usb_ep0_buf[2] = 0;
        n64usb_ep0_buf[3] = 0;
        n64usb_ep0_send(n64usb_ep0_buf, 4, wlength);
    } else if (reqtype == 0x21 &&
        req == USB_CDC_SET_ETHERNET_PACKET_FILTER) {
        n64usb_ep0_ack();
        n64usb_ecm_notify_link_up();
#endif
#if N64USB_GDB
    } else if (reqtype == 0xa1 && req == USB_CDC_GET_LINE_CODING) {
        n64usb_ep0_send(n64usb_acm_line_coding,
            sizeof(n64usb_acm_line_coding), wlength);
    } else if (reqtype == 0x21 && req == USB_CDC_SET_LINE_CODING) {
        n64usb_ep0_out_pending = USB_CDC_SET_LINE_CODING;
        n64usb_start_transfer(n64usb_find_ep(EP0_OUT_ADDR), 0,
            wlength > sizeof(n64usb_acm_line_coding) ?
            sizeof(n64usb_acm_line_coding) : wlength);
    } else if (reqtype == 0x21 &&
        req == USB_CDC_SET_CONTROL_LINE_STATE) {
        n64usb_ep0_ack();
    } else if (reqtype == 0x21 && req == USB_CDC_SEND_BREAK) {
        n64usb_gdb_break_pending = 1;
        n64usb_ep0_ack();
#endif
#if !N64USB_CDC_ECM && !N64USB_GDB
    } else if (reqtype == 0xc0 && req == n64usb_ms_os_string[16] &&
        windex == 0x04) {
        n64usb_ep0_send(n64usb_ms_winusb_desc,
            sizeof(n64usb_ms_winusb_desc), wlength);
#endif
    } else
        n64usb_ep0_ack();
}

#if N64USB_CDC_ECM
static void
n64usb_send_next_tx(void)
{
    unsigned char empty;
    unsigned chunk;

    if (!n64usb_configured || !n64usb_data_alt || n64usb_tx_frame == 0) {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT,
            (n64usb_configured && n64usb_data_alt) ? 0 : 1);
        return;
    }
    if (n64usb_tx_pos < n64usb_tx_len) {
        chunk = n64usb_tx_len - n64usb_tx_pos;
        if (chunk > USB_PACKET_SIZE)
            chunk = USB_PACKET_SIZE;
        n64usb_start_transfer(n64usb_find_ep(EP2_IN_ADDR),
            n64usb_tx_frame + n64usb_tx_pos, chunk);
        n64usb_tx_pos += chunk;
        return;
    }
    if (n64usb_tx_zlp) {
        n64usb_tx_zlp = 0;
        empty = 0;
        n64usb_start_transfer(n64usb_find_ep(EP2_IN_ADDR), &empty, 0);
        return;
    }
    n64usb_tx_usb_busy = 0;
    n64usb_tx_frame = 0;
    usbn_tx_done(USB_NET_UNIT, 0);
}
#elif !N64USB_GDB
static void
n64usb_send_next_tx(void)
{
    unsigned char packet[USB_PACKET_SIZE];
    unsigned packet_len;
    int ret;

    if (!n64usb_configured || !n64usb_tx.active) {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT, n64usb_configured ? 0 : 1);
        return;
    }
    ret = n64usbnet_tx_next(&n64usb_tx, packet, &packet_len);
    if (ret == N64USBNET_ERROR) {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT, 1);
        return;
    }
    n64usb_start_transfer(n64usb_find_ep(EP2_IN_ADDR), packet,
        packet_len);
}
#endif

static void
n64usb_ep0_in(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
    if (n64usb_should_set_addr) {
        n64usb_reg_write(USB_ADDR_ENDP, n64usb_dev_addr);
        n64usb_should_set_addr = 0;
    } else if (n64usb_ep0_pos < n64usb_ep0_len)
        n64usb_ep0_send_next();
    else {
        n64usb_ep0_data = 0;
        n64usb_ep0_len = 0;
        n64usb_ep0_pos = 0;
        n64usb_start_transfer(n64usb_find_ep(EP0_OUT_ADDR), 0, 0);
    }
}

static void
n64usb_ep0_out(unsigned char *buf, unsigned len)
{
#if N64USB_GDB
    unsigned i;

    if (n64usb_ep0_out_pending == USB_CDC_SET_LINE_CODING) {
        if (len > sizeof(n64usb_acm_line_coding))
            len = sizeof(n64usb_acm_line_coding);
        for (i = 0; i < len; i++)
            n64usb_acm_line_coding[i] = buf[i];
        n64usb_ep0_out_pending = 0;
        n64usb_ep0_ack();
        return;
    }
#endif
    (void)buf;
    (void)len;
}

static void
n64usb_ep1_out(unsigned char *buf, unsigned len)
{
#if N64USB_CDC_ECM
    if (!n64usb_configured || !n64usb_data_alt)
        return;
    if (n64usb_rx_pos + len > sizeof(n64usb_rx_frame)) {
        usbn_input_error(USB_NET_UNIT);
        n64usb_rx_pos = 0;
    } else if (len != 0) {
        bcopy((caddr_t)buf, (caddr_t)(n64usb_rx_frame + n64usb_rx_pos),
            len);
        n64usb_rx_pos += len;
    }
    if (len < USB_PACKET_SIZE) {
        if (n64usb_rx_pos >= N64USB_ETH_HEADER_LEN)
            usbn_input(USB_NET_UNIT, n64usb_rx_frame, n64usb_rx_pos);
        else if (n64usb_rx_pos != 0)
            usbn_input_error(USB_NET_UNIT);
        n64usb_rx_pos = 0;
    }
    if (n64usb_configured && n64usb_data_alt)
        n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
            USB_PACKET_SIZE);
#elif N64USB_GDB
    unsigned next;
    unsigned i;

    for (i = 0; i < len; i++) {
        if (buf[i] == 3) {
            n64usb_gdb_break_pending = 1;
            continue;
        }
        if (buf[i] == '$') {
            n64usb_gdb_attach_pending = 1;
            n64_gdb_usb_active = 1;
        }
        next = (n64usb_gdb_rx_put + 1) % N64USB_GDB_RX_RING_SIZE;
        if (next == n64usb_gdb_rx_get)
            break;
        n64usb_gdb_rx_ring[n64usb_gdb_rx_put] = buf[i];
        n64usb_gdb_rx_put = next;
    }
    if (n64usb_configured)
        n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
            USB_PACKET_SIZE);
#else
    int ret;

    ret = n64usbnet_rx_push(&n64usb_rx, buf, len);
    if (ret == N64USBNET_DONE) {
        usbn_input(USB_NET_UNIT, n64usb_rx.frame, n64usb_rx.len);
        n64usbnet_rx_reset(&n64usb_rx);
    } else if (ret == N64USBNET_ERROR) {
        usbn_input_error(USB_NET_UNIT);
        n64usbnet_rx_reset(&n64usb_rx);
    }
    if (n64usb_configured)
        n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
            USB_PACKET_SIZE);
#endif
}

static void
n64usb_ep2_in(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
#if N64USB_CDC_ECM
    if (n64usb_tx_frame != 0)
        n64usb_send_next_tx();
    else {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT, 0);
    }
#elif N64USB_GDB
    n64usb_tx_usb_busy = 0;
#else
    if (n64usb_tx.active)
        n64usb_send_next_tx();
    else {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT, 0);
    }
#endif
}

#if N64USB_CDC_ECM || N64USB_GDB
static void
n64usb_ep3_in(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
#if N64USB_CDC_ECM
    if (n64usb_ecm_speed_pending) {
        n64usb_ecm_speed_pending = 0;
        n64usb_ecm_notify_speed();
        return;
    }
    n64usb_ecm_notify_busy = 0;
#endif
}
#endif

static void
n64usb_handle_buff_done(unsigned epnum, int in)
{
    struct n64usb_ep *ep;
    unsigned char addr;
    unsigned control, len;
    unsigned char buf[USB_PACKET_SIZE];

    addr = epnum | (in ? USB_DIR_IN : USB_DIR_OUT);
    ep = n64usb_find_ep(addr);
    if (ep == 0 || ep->handler == 0) {
#if !N64USB_GDB
        usbn_input_error(USB_NET_UNIT);
#endif
        return;
    }
    control = n64usb_dpram_read(ep->buf_ctrl_offset);
    len = control & USB_BUF_CTRL_LEN_MASK;
    if (len > USB_PACKET_SIZE)
        len = USB_PACKET_SIZE;
    if (len)
        n64usb_copyin_words(ep->data_offset, buf, len);
    ep->handler(buf, len);
}

static void
n64usb_handle_buff_status(void)
{
    unsigned status, remaining, bit, i, handled;

    status = n64usb_reg_read(USB_BUFF_STATUS);
    remaining = status;
    bit = 1;
    handled = 0;
    for (i = 0; remaining && i < USB_NUM_ENDPOINTS * 2; i++) {
        if (remaining & bit) {
            n64usb_reg_clear(USB_BUFF_STATUS, bit);
            n64usb_handle_buff_done(i >> 1, (i & 1) == 0);
            handled |= bit;
            remaining &= ~bit;
        }
        bit <<= 1;
    }
    if (status & ~handled) {
        n64usb_reg_clear(USB_BUFF_STATUS, status & ~handled);
#if !N64USB_GDB
        usbn_input_error(USB_NET_UNIT);
#endif
    }
}

static void
n64usb_poll_controller(void)
{
    unsigned status, handled;

    status = n64usb_reg_read(USB_INTS);
#ifdef N64_MINIMAL_USBNET_DEBUG
    n64usb_debug_last_ints = status;
#endif
    if (status == 0) {
        (void)n64usb_read_phys(N64CART_USBCFG_PHYS);
        return;
    }
    handled = 0;
    if (status & USB_INTS_DEV_CONN_DIS) {
        handled |= USB_INTS_DEV_CONN_DIS;
        if (!n64usb_connection_change()) {
            (void)n64usb_read_phys(N64CART_USBCFG_PHYS);
            return;
        }
    }
    if (status & USB_INTS_SETUP_REQ) {
        handled |= USB_INTS_SETUP_REQ;
        n64usb_reg_clear(USB_SIE_STATUS, USB_SIE_STATUS_SETUP_REC);
        n64usb_handle_setup();
    }
    if (status & USB_INTS_BUFF_STATUS) {
        handled |= USB_INTS_BUFF_STATUS;
        n64usb_handle_buff_status();
    }
    if (status & USB_INTS_BUS_RESET) {
        handled |= USB_INTS_BUS_RESET;
        n64usb_reg_clear(USB_SIE_STATUS, USB_SIE_STATUS_BUS_RESET);
        n64usb_bus_reset();
    }
    if (status & ~handled) {
#if !N64USB_GDB
        usbn_input_error(USB_NET_UNIT);
#endif
    }
    (void)n64usb_read_phys(N64CART_USBCFG_PHYS);
}

static void
n64usb_hw_start(void)
{
    unsigned i;

    n64usb_write_phys(N64CART_USBCFG_PHYS, 0);
    n64usb_write_phys(N64CART_USBCFG_PHYS, N64CART_USB_RESET);
    n64usb_write_phys(N64CART_USBCFG_PHYS, 0);
    n64usb_reset_state();
    n64usb_reg_write(USB_ADDR_ENDP, 0);

    for (i = 0; i < USB_DPRAM_SIZE; i += 4)
        n64usb_dpram_write(i, 0);

    n64usb_reg_write(USB_USB_MUXING,
        USB_USB_MUXING_TO_PHY | USB_USB_MUXING_SOFTCON);
    n64usb_reg_write(USB_USB_PWR,
        USB_USB_PWR_VBUS_DETECT | USB_USB_PWR_VBUS_DETECT_OVERRIDE);
    n64usb_reg_write(USB_MAIN_CTRL, USB_MAIN_CTRL_CONTROLLER_EN);
    n64usb_reg_write(USB_SIE_CTRL, USB_SIE_CTRL_EP0_INT_1BUF);
    n64usb_reg_write(USB_INTE,
        USB_INTS_BUFF_STATUS | USB_INTS_BUS_RESET | USB_INTS_SETUP_REQ |
        USB_INTS_DEV_CONN_DIS);

    for (i = 0; i < sizeof(n64usb_eps) / sizeof(n64usb_eps[0]); i++)
        n64usb_setup_endpoint(&n64usb_eps[i]);

    n64usb_reg_write(USB_REG_SET + USB_SIE_CTRL,
        USB_SIE_CTRL_PULLUP_EN);
    n64usb_usb_mode(0);
}

void
n64cart_usb_shutdown(void)
{
    int saved_status;

    if (!n64usb_pi_enter(&saved_status))
        panic("n64cart USB PI lock");

    /*
     * Match the N64cart usb_device_finish() hardware contract.  Disabling
     * the cartridge interrupt before resetting the controller prevents the
     * old USB instance from remaining active while stage0 reloads the kernel.
     * The final zero releases reset but leaves both USB and CART/IP3 disabled;
     * the next kernel's n64usb_hw_start() performs a clean initialization.
     */
    n64usb_write_phys(N64CART_USBCFG_PHYS, 0);
    n64usb_write_phys(N64CART_USBCFG_PHYS, N64CART_USB_RESET);
    n64usb_write_phys(N64CART_USBCFG_PHYS, 0);
    n64usb_initialized = 0;
    n64usb_configured = 0;
    n64usb_tx_usb_busy = 0;

    n64usb_pi_leave(saved_status);
}

#if !N64USB_GDB
int
usbn_hw_init(int unit, unsigned char *enaddr)
{
    int saved_status;

    if (unit != USB_NET_UNIT)
        return 0;
#ifdef N64_MINIMAL_USBNET_DEBUG
    printf("n64usb: controller init begin\n");
#endif
    if (!n64usb_pi_enter(&saved_status))
        return 0;
    bcopy((caddr_t)n64usb_device_mac, (caddr_t)enaddr,
        sizeof(n64usb_device_mac));
    if (!n64usb_initialized) {
#if !N64USB_CDC_ECM
        n64usbnet_rx_reset(&n64usb_rx);
#endif
        n64usb_hw_start();
        n64usb_initialized = 1;
    }
    n64usb_pi_leave(saved_status);

    /*
     * n64pi_bus_leave() restores the CP0 status saved by splhigh().
     * Enabling CART/IP3 while the PI bus is owned would therefore be
     * undone on leave, after the USB pull-up was already asserted.  The
     * host would see a device but its control requests would never run.
     */
    n64usb_enable_cart_interrupt();
#ifdef N64_MINIMAL_USBNET_DEBUG
    printf("n64usb: controller init done status=%x\n",
        mips_read_c0_register(C0_STATUS, 0));
#endif
    return 1;
}

int
usbn_hw_send(int unit, const unsigned char *frame, unsigned len)
{
    int error;
    int saved_status;

    if (unit != USB_NET_UNIT || !n64usb_initialized ||
        !n64usb_configured || n64usb_tx_usb_busy)
        return EBUSY;
    if (!n64usb_pi_enter(&saved_status))
        return EBUSY;
    error = 0;
#if N64USB_CDC_ECM
    if (!n64usb_data_alt || len > N64USB_FRAME_MAX) {
        error = EBUSY;
        goto out;
    }
    n64usb_tx_frame = frame;
    n64usb_tx_len = len;
    n64usb_tx_pos = 0;
    n64usb_tx_zlp = (len & (USB_PACKET_SIZE - 1)) == 0;
#else
    if (n64usbnet_tx_begin(&n64usb_tx, frame, len, ++n64usb_tx_seq) !=
        N64USBNET_MORE) {
        error = EINVAL;
        goto out;
    }
#endif
    n64usb_tx_usb_busy = 1;
    n64usb_send_next_tx();
out:
    n64usb_pi_leave(saved_status);
    return error;
}

void
usbn_hw_poll(void)
{
    int saved_status;
#ifdef N64_MINIMAL_USBNET_DEBUG
    int polled;

    polled = 0;
#endif
    if (n64usb_initialized && n64usb_pi_enter(&saved_status)) {
        n64usb_poll_controller();
        n64usb_pi_leave(saved_status);
#ifdef N64_MINIMAL_USBNET_DEBUG
        polled = 1;
#endif
    }
#ifdef N64_MINIMAL_USBNET_DEBUG
    if (polled && !n64usb_debug_poll_logged) {
        n64usb_debug_poll_logged = 1;
        printf("n64usb: first poll ints=%x status=%x cause=%x\n",
            n64usb_debug_last_ints,
            mips_read_c0_register(C0_STATUS, 0),
            mips_read_c0_register(C0_CAUSE, 0));
    }
#endif
}
#else
void
n64_gdb_usb_init(void)
{
    int saved_status;

    if (n64usb_initialized)
        return;
    if (!n64usb_pi_enter(&saved_status))
        return;
    n64usb_hw_start();
    n64usb_initialized = 1;
    n64usb_pi_leave(saved_status);
    n64usb_enable_cart_interrupt();
}

int
n64_gdb_usb_ready(void)
{
    return n64usb_initialized && n64usb_configured;
}

int
n64_gdb_usb_attached(void)
{
    return n64_gdb_usb_active != 0;
}

void
n64_gdb_usb_detach(void)
{
    n64_gdb_usb_active = 0;
}

void
n64_gdb_usb_poll(void)
{
    int saved_status;

    if (n64usb_initialized && n64usb_pi_enter(&saved_status)) {
        n64usb_poll_controller();
        n64usb_pi_leave(saved_status);
    }
}

int
n64_gdb_usb_interrupt(void)
{
    int saved_status;
    int result;

    if (!n64usb_initialized)
        return 0;
    if (!n64usb_pi_enter(&saved_status))
        return 0;
    n64usb_poll_controller();
    result = 0;
    if (n64usb_gdb_break_pending) {
        n64usb_gdb_break_pending = 0;
        n64usb_gdb_attach_pending = 0;
        result = 1;
    } else if (n64usb_gdb_attach_pending) {
        n64usb_gdb_attach_pending = 0;
        result = 2;
    }
    n64usb_pi_leave(saved_status);
    return result;
}

void
n64_gdb_usb_clear_interrupt(void)
{
    n64usb_gdb_break_pending = 0;
    n64usb_gdb_attach_pending = 0;
}

int
n64_gdb_usb_getc(void)
{
    int ch;
    int saved_status;

    if (!n64usb_initialized)
        return -1;
    if (!n64usb_pi_enter(&saved_status))
        return -1;
    n64usb_poll_controller();
    if (n64usb_gdb_rx_get == n64usb_gdb_rx_put) {
        n64usb_pi_leave(saved_status);
        return -1;
    }
    ch = n64usb_gdb_rx_ring[n64usb_gdb_rx_get];
    n64usb_gdb_rx_get =
        (n64usb_gdb_rx_get + 1) % N64USB_GDB_RX_RING_SIZE;
    n64usb_pi_leave(saved_status);
    return ch;
}

int
n64_gdb_usb_write(const unsigned char *buf, unsigned len)
{
    unsigned chunk;
    int error;
    int saved_status;

    if (!n64_gdb_usb_ready())
        return EIO;
    if (!n64usb_pi_enter(&saved_status))
        return EBUSY;
    error = 0;
    while (len != 0) {
        while (n64usb_tx_usb_busy) {
            n64usb_poll_controller();
            if (!n64usb_configured) {
                error = EIO;
                goto out;
            }
        }
        chunk = len > USB_PACKET_SIZE ? USB_PACKET_SIZE : len;
        n64usb_tx_usb_busy = 1;
        n64usb_start_transfer(n64usb_find_ep(EP2_IN_ADDR), buf, chunk);
        buf += chunk;
        len -= chunk;
    }
    while (n64usb_tx_usb_busy) {
        n64usb_poll_controller();
        if (!n64usb_configured) {
            error = EIO;
            goto out;
        }
    }
out:
    n64usb_pi_leave(saved_status);
    return error;
}
#endif
