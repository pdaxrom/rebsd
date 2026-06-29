/*
 * N64cart USB bulk transport for the generic usbn(4) interface.
 *
 * The cartridge exposes an RP2040-style USB device controller through PI
 * address space.  This driver keeps the controller in a vendor-specific
 * two-bulk-endpoint mode compatible with the N64cart firmware descriptor,
 * but carries Ethernet frames using the shared n64usbnet framing.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <machine/io.h>
#include <machine/n64int.h>
#include <machine/n64cart_uart.h>
#include <mips/common/if_usbn.h>
#include <mips/common/n64usbnet_proto.h>

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
#define USB_DPRAM_EP0_IN_BUF_CTRL       0x080u
#define USB_DPRAM_EP0_OUT_BUF_CTRL      0x084u
#define USB_DPRAM_EP1_OUT_BUF_CTRL      0x08cu
#define USB_DPRAM_EP2_IN_BUF_CTRL       0x090u
#define USB_DPRAM_EP0_BUF               0x100u
#define USB_DPRAM_EP1_OUT_BUF           0x180u
#define USB_DPRAM_EP2_IN_BUF            0x1c0u
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
#define USB_REQUEST_GET_DESCRIPTOR      0x06u
#define USB_REQUEST_SET_ADDRESS         0x05u
#define USB_REQUEST_SET_CONFIGURATION   0x09u

#define USB_MAIN_CTRL_CONTROLLER_EN     0x00000001u
#define USB_SIE_CTRL_EP0_INT_1BUF       0x20000000u
#define USB_SIE_CTRL_PULLUP_EN          0x00010000u
#define USB_SIE_STATUS_BUS_RESET        0x00080000u
#define USB_SIE_STATUS_SETUP_REC        0x00020000u
#define USB_INTS_SETUP_REQ              0x00010000u
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

#define USB_PACKET_SIZE                 64u
#define USB_NET_UNIT                    0

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

static struct n64usb_ep n64usb_eps[] = {
    { EP0_OUT_ADDR, 0, 0, USB_DPRAM_EP0_OUT_BUF_CTRL, USB_DPRAM_EP0_BUF,
        0, n64usb_ep0_out },
    { EP0_IN_ADDR, 0, 0, USB_DPRAM_EP0_IN_BUF_CTRL, USB_DPRAM_EP0_BUF,
        0, n64usb_ep0_in },
    { EP1_OUT_ADDR, 2, USB_DPRAM_EP1_OUT_CTRL, USB_DPRAM_EP1_OUT_BUF_CTRL,
        USB_DPRAM_EP1_OUT_BUF, 0, n64usb_ep1_out },
    { EP2_IN_ADDR, 2, USB_DPRAM_EP2_IN_CTRL, USB_DPRAM_EP2_IN_BUF_CTRL,
        USB_DPRAM_EP2_IN_BUF, 0, n64usb_ep2_in },
};

static const unsigned char n64usb_device_desc[] = {
    18, USB_DT_DEVICE,
    0x00, 0x02,
    0, 0, 0, USB_PACKET_SIZE,
    0x09, 0x12,
    0x00, 0x68,
    0x00, 0x00,
    1, 2, 0, 1
};

static const unsigned char n64usb_config_desc[] = {
    9, USB_DT_CONFIG,
    32, 0,
    1, 1, 0, 0xc0, 0x32,
    9, 4,
    0, 0, 2, 0xff, 0, 0, 0,
    7, 5,
    EP1_OUT_ADDR, 2, 64, 0, 0,
    7, 5,
    EP2_IN_ADDR, 2, 64, 0, 0
};

static const unsigned char n64usb_lang_desc[] = {
    4, USB_DT_STRING, 0x09, 0x04
};

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

static const char n64usb_vendor[] = "pdaXrom.org";
static const char n64usb_product[] = "N64cart USBNet";

static unsigned char n64usb_mac[6] =
    { 0x02, 0x64, 0x00, 0x00, 0x00, 0x10 };
static unsigned char n64usb_ep0_buf[USB_PACKET_SIZE];
static unsigned n64usb_io_words[USB_PACKET_SIZE / sizeof(unsigned)];
static struct n64usbnet_tx n64usb_tx;
static struct n64usbnet_rx n64usb_rx;
static unsigned n64usb_tx_seq;
static int n64usb_initialized;
static int n64usb_configured;
static int n64usb_should_set_addr;
static unsigned char n64usb_dev_addr;
static int n64usb_tx_usb_busy;

static void n64usb_poll_controller(void);

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
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
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

static void
n64usb_ep0_send(const unsigned char *buf, unsigned len, unsigned wlength)
{
    struct n64usb_ep *ep;

    if (len > wlength)
        len = wlength;
    if (len > USB_PACKET_SIZE)
        len = USB_PACKET_SIZE;
    ep = n64usb_find_ep(EP0_IN_ADDR);
    if (ep == 0)
        return;
    ep->next_pid = 1;
    n64usb_start_transfer(ep, buf, len);
}

static void
n64usb_ep0_ack(void)
{
    n64usb_ep0_send(0, 0, 0);
}

static void
n64usb_bus_reset(void)
{
    n64usb_dev_addr = 0;
    n64usb_should_set_addr = 0;
    n64usb_configured = 0;
    n64usb_tx_usb_busy = 0;
    n64usbnet_rx_reset(&n64usb_rx);
    n64usb_reg_write(USB_ADDR_ENDP, 0);
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
        else if (dindex == 0xee)
            n64usb_ep0_send(n64usb_ms_os_string,
                sizeof(n64usb_ms_os_string), wlength);
        else if (dindex == 1) {
            len = n64usb_string_descriptor(n64usb_vendor,
                n64usb_ep0_buf);
            n64usb_ep0_send(n64usb_ep0_buf, len, wlength);
        } else if (dindex == 2) {
            len = n64usb_string_descriptor(n64usb_product,
                n64usb_ep0_buf);
            n64usb_ep0_send(n64usb_ep0_buf, len, wlength);
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
    if (reqtype == USB_DIR_OUT) {
        if (req == USB_REQUEST_SET_ADDRESS) {
            n64usb_dev_addr = wvalue & 0x7f;
            n64usb_should_set_addr = 1;
            n64usb_ep0_ack();
        } else if (req == USB_REQUEST_SET_CONFIGURATION) {
            n64usb_configured = 1;
            n64usb_ep0_ack();
            n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
                USB_PACKET_SIZE);
        } else
            n64usb_ep0_ack();
    } else if (reqtype == USB_DIR_IN) {
        if (req == USB_REQUEST_GET_DESCRIPTOR)
            n64usb_handle_get_descriptor(setup);
        else
            n64usb_ep0_send(0, 0, wlength);
    } else if (reqtype == 0xc0 && req == n64usb_ms_os_string[16] &&
        windex == 0x04) {
        n64usb_ep0_send(n64usb_ms_winusb_desc,
            sizeof(n64usb_ms_winusb_desc), wlength);
    } else
        n64usb_ep0_ack();
}

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

static void
n64usb_ep0_in(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
    if (n64usb_should_set_addr) {
        n64usb_reg_write(USB_ADDR_ENDP, n64usb_dev_addr);
        n64usb_should_set_addr = 0;
    } else
        n64usb_start_transfer(n64usb_find_ep(EP0_OUT_ADDR), 0, 0);
}

static void
n64usb_ep0_out(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
}

static void
n64usb_ep1_out(unsigned char *buf, unsigned len)
{
    int ret;

    ret = n64usbnet_rx_push(&n64usb_rx, buf, len);
    if (ret == N64USBNET_DONE) {
        usbn_input(USB_NET_UNIT, n64usb_rx.frame, n64usb_rx.len);
        n64usbnet_rx_reset(&n64usb_rx);
    } else if (ret == N64USBNET_ERROR)
        n64usbnet_rx_reset(&n64usb_rx);
    if (n64usb_configured)
        n64usb_start_transfer(n64usb_find_ep(EP1_OUT_ADDR), 0,
            USB_PACKET_SIZE);
}

static void
n64usb_ep2_in(unsigned char *buf, unsigned len)
{
    (void)buf;
    (void)len;
    if (n64usb_tx.active)
        n64usb_send_next_tx();
    else {
        n64usb_tx_usb_busy = 0;
        usbn_tx_done(USB_NET_UNIT, 0);
    }
}

static void
n64usb_handle_buff_done(unsigned epnum, int in)
{
    struct n64usb_ep *ep;
    unsigned char addr;
    unsigned control, len;
    unsigned char buf[USB_PACKET_SIZE];

    addr = epnum | (in ? USB_DIR_IN : USB_DIR_OUT);
    ep = n64usb_find_ep(addr);
    if (ep == 0 || ep->handler == 0)
        return;
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
    unsigned status, remaining, bit, i;

    status = n64usb_reg_read(USB_BUFF_STATUS);
    remaining = status;
    bit = 1;
    for (i = 0; remaining && i < 32; i++) {
        if (remaining & bit) {
            n64usb_reg_clear(USB_BUFF_STATUS, bit);
            n64usb_handle_buff_done(i >> 1, (i & 1) == 0);
            remaining &= ~bit;
        }
        bit <<= 1;
    }
}

static void
n64usb_poll_controller(void)
{
    unsigned status;

    status = n64usb_reg_read(USB_INTS);
    if (status == 0)
        return;
    if (status & USB_INTS_SETUP_REQ) {
        n64usb_reg_clear(USB_SIE_STATUS, USB_SIE_STATUS_SETUP_REC);
        n64usb_handle_setup();
    }
    if (status & USB_INTS_BUFF_STATUS)
        n64usb_handle_buff_status();
    if (status & USB_INTS_BUS_RESET) {
        n64usb_reg_clear(USB_SIE_STATUS, USB_SIE_STATUS_BUS_RESET);
        n64usb_bus_reset();
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

    for (i = 0; i < USB_DPRAM_SIZE; i += 4)
        n64usb_dpram_write(i, 0);

    n64usb_reg_write(USB_USB_MUXING,
        USB_USB_MUXING_TO_PHY | USB_USB_MUXING_SOFTCON);
    n64usb_reg_write(USB_USB_PWR,
        USB_USB_PWR_VBUS_DETECT | USB_USB_PWR_VBUS_DETECT_OVERRIDE);
    n64usb_reg_write(USB_MAIN_CTRL, USB_MAIN_CTRL_CONTROLLER_EN);
    n64usb_reg_write(USB_SIE_CTRL, USB_SIE_CTRL_EP0_INT_1BUF);
    n64usb_reg_write(USB_INTE,
        USB_INTS_BUFF_STATUS | USB_INTS_BUS_RESET | USB_INTS_SETUP_REQ);

    for (i = 0; i < sizeof(n64usb_eps) / sizeof(n64usb_eps[0]); i++)
        n64usb_setup_endpoint(&n64usb_eps[i]);

    n64usb_reg_write(USB_REG_SET + USB_SIE_CTRL,
        USB_SIE_CTRL_PULLUP_EN);
    n64usb_usb_mode(0);
}

int
usbn_hw_init(int unit, unsigned char *enaddr)
{
    if (unit != USB_NET_UNIT)
        return 0;
    bcopy((caddr_t)n64usb_mac, (caddr_t)enaddr, sizeof(n64usb_mac));
    if (!n64usb_initialized) {
        n64usbnet_rx_reset(&n64usb_rx);
        n64usb_hw_start();
        n64usb_initialized = 1;
    }
    return 1;
}

int
usbn_hw_send(int unit, const unsigned char *frame, unsigned len)
{
    if (unit != USB_NET_UNIT || !n64usb_initialized ||
        !n64usb_configured || n64usb_tx_usb_busy)
        return EBUSY;
    if (n64usbnet_tx_begin(&n64usb_tx, frame, len, ++n64usb_tx_seq) !=
        N64USBNET_MORE)
        return EINVAL;
    n64usb_tx_usb_busy = 1;
    n64usb_send_next_tx();
    return 0;
}

void
usbn_hw_poll(void)
{
    if (n64usb_initialized)
        n64usb_poll_controller();
}
