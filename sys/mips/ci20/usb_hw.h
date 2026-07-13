/*
 * JZ4780 USB host clock, PHY, reset, and board-power sequencing.
 */

#ifndef _MIPS_CI20_USB_HW_H_
#define _MIPS_CI20_USB_HW_H_

#define CI20_CPM_CLKGR0             0x20u
#define CI20_CPM_OPCR               0x24u
#define CI20_CPM_USBPCR             0x3cu
#define CI20_CPM_USBPCR1            0x48u
#define CI20_CPM_UHCCDR             0x6cu
#define CI20_CPM_SRBC               0xc4u

#define CI20_CLKGR0_UHC             (1u << 24)

#define CI20_OPCR_SPENDN1           (1u << 6)

#define CI20_USBPCR_POR             (1u << 22)
#define CI20_USBPCR_SIDDQ           (1u << 21)
#define CI20_USBPCR_OTG_DISABLE     (1u << 20)

#define CI20_USBPCR1_REFCLKSEL_MASK (3u << 26)
#define CI20_USBPCR1_REFCLKSEL_CORE (2u << 26)
#define CI20_USBPCR1_REFCLKDIV_MASK (3u << 24)
#define CI20_USBPCR1_REFCLKDIV_48   (2u << 24)
#define CI20_USBPCR1_DMPD1          (1u << 23)
#define CI20_USBPCR1_DPPD1          (1u << 22)
#define CI20_USBPCR1_PORT1_RST      (1u << 20)
#define CI20_USBPCR1_WORD_IF1       (1u << 18)

#define CI20_UHCCDR_SOURCE_MASK     (3u << 30)
#define CI20_UHCCDR_OTG_PHY         (3u << 30)
#define CI20_UHCCDR_CHANGE_ENABLE   (1u << 29)
#define CI20_UHCCDR_BUSY            (1u << 28)
#define CI20_UHCCDR_STOP            (1u << 27)
#define CI20_UHCCDR_DIV_MASK        0xffu

#define CI20_SRBC_UHC_RESET         (1u << 14)

#define CI20_USB_HW_OK              0
#define CI20_USB_HW_INVALID         (-1)
#define CI20_USB_HW_CLOCK_TIMEOUT   (-2)

struct ci20_usb_hw_ops {
    unsigned (*cuo_read_cpm)(void *, unsigned);
    void (*cuo_write_cpm)(void *, unsigned, unsigned);
    void (*cuo_set_vbus)(void *, int);
    void (*cuo_delay_us)(void *, unsigned);
    void (*cuo_trace)(void *, const char *);
};

int ci20_usb_hw_start(const struct ci20_usb_hw_ops *, void *);

#endif /* _MIPS_CI20_USB_HW_H_ */
