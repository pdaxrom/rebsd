/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "usb_hw.h"

#define CI20_USB_CLOCK_WAIT_US     10000u

static unsigned
ci20_usb_read(const struct ci20_usb_hw_ops *ops, void *arg, unsigned reg)
{
    return ops->cuo_read_cpm(arg, reg);
}

static void
ci20_usb_write(const struct ci20_usb_hw_ops *ops, void *arg, unsigned reg,
    unsigned value)
{
    ops->cuo_write_cpm(arg, reg, value);
}

static void
ci20_usb_trace(const struct ci20_usb_hw_ops *ops, void *arg,
    const char *stage)
{
    if (ops->cuo_trace != 0)
        ops->cuo_trace(arg, stage);
}

int
ci20_usb_hw_start(const struct ci20_usb_hw_ops *ops, void *arg)
{
    unsigned value;
    unsigned elapsed;

    if (ops == 0 || ops->cuo_read_cpm == 0 || ops->cuo_write_cpm == 0 ||
        ops->cuo_set_vbus == 0 || ops->cuo_delay_us == 0)
        return CI20_USB_HW_INVALID;

    ci20_usb_trace(ops, arg, "enable VBUS");
    /* Ci20 SYS_POWER_IND is GPF15 and also enables host-port VBUS. */
    ops->cuo_set_vbus(arg, 1);
    ci20_usb_trace(ops, arg, "settle VBUS");
    ops->cuo_delay_us(arg, 1000);

    ci20_usb_trace(ops, arg, "configure UHC clock");
    /* U-Boot selects the shared OTG PHY as the 48 MHz UHC source. */
    value = ci20_usb_read(ops, arg, CI20_CPM_UHCCDR);
    value &= ~(CI20_UHCCDR_SOURCE_MASK | CI20_UHCCDR_CHANGE_ENABLE |
        CI20_UHCCDR_BUSY | CI20_UHCCDR_STOP | CI20_UHCCDR_DIV_MASK);
    value |= CI20_UHCCDR_OTG_PHY | CI20_UHCCDR_CHANGE_ENABLE;
    ci20_usb_write(ops, arg, CI20_CPM_UHCCDR, value);
    for (elapsed = 0; elapsed < CI20_USB_CLOCK_WAIT_US; ++elapsed) {
        if ((ci20_usb_read(ops, arg, CI20_CPM_UHCCDR) &
            CI20_UHCCDR_BUSY) == 0)
            break;
        ops->cuo_delay_us(arg, 1);
    }
    if (elapsed == CI20_USB_CLOCK_WAIT_US)
        return CI20_USB_HW_CLOCK_TIMEOUT;

    ci20_usb_trace(ops, arg, "ungate UHC clock");
    value = ci20_usb_read(ops, arg, CI20_CPM_CLKGR0);
    ci20_usb_write(ops, arg, CI20_CPM_CLKGR0,
        value & ~CI20_CLKGR0_UHC);

    ci20_usb_trace(ops, arg, "configure host PHY");
    value = ci20_usb_read(ops, arg, CI20_CPM_USBPCR);
    value &= ~(CI20_USBPCR_SIDDQ | CI20_USBPCR_OTG_DISABLE);
    ci20_usb_write(ops, arg, CI20_CPM_USBPCR, value);

    value = ci20_usb_read(ops, arg, CI20_CPM_USBPCR1);
    value &= ~(CI20_USBPCR1_REFCLKSEL_MASK |
        CI20_USBPCR1_REFCLKDIV_MASK | CI20_USBPCR1_PORT1_RST);
    value |= CI20_USBPCR1_REFCLKSEL_CORE |
        CI20_USBPCR1_REFCLKDIV_48 | CI20_USBPCR1_DMPD1 |
        CI20_USBPCR1_DPPD1 | CI20_USBPCR1_WORD_IF1;
    ci20_usb_write(ops, arg, CI20_CPM_USBPCR1, value);

    value = ci20_usb_read(ops, arg, CI20_CPM_OPCR);
    ci20_usb_write(ops, arg, CI20_CPM_OPCR,
        value | CI20_OPCR_SPENDN1);

    ci20_usb_trace(ops, arg, "pulse PHY reset");
    value = ci20_usb_read(ops, arg, CI20_CPM_USBPCR);
    ci20_usb_write(ops, arg, CI20_CPM_USBPCR,
        value | CI20_USBPCR_POR);
    ops->cuo_delay_us(arg, 1000);
    value = ci20_usb_read(ops, arg, CI20_CPM_USBPCR);
    ci20_usb_write(ops, arg, CI20_CPM_USBPCR,
        value & ~CI20_USBPCR_POR);

    ci20_usb_trace(ops, arg, "pulse UHC reset");
    value = ci20_usb_read(ops, arg, CI20_CPM_SRBC);
    ci20_usb_write(ops, arg, CI20_CPM_SRBC,
        value | CI20_SRBC_UHC_RESET);
    ops->cuo_delay_us(arg, 300);
    value = ci20_usb_read(ops, arg, CI20_CPM_SRBC);
    ci20_usb_write(ops, arg, CI20_CPM_SRBC,
        value & ~CI20_SRBC_UHC_RESET);
    ops->cuo_delay_us(arg, 300);
    ci20_usb_trace(ops, arg, "hardware ready");
    return CI20_USB_HW_OK;
}
