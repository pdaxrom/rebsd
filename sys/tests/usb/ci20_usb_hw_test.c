/* Fake-register tests for the Ci20/JZ4780 USB host power-up sequence. */

#include <stdio.h>
#include <string.h>
#include <mips/ci20/usb_hw.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                 \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define FAKE_REG_WORDS  64u
#define FAKE_WRITES     32u

struct fake_write {
    unsigned reg;
    unsigned value;
};

struct fake_hw {
    unsigned regs[FAKE_REG_WORDS];
    unsigned ehci_regs[FAKE_REG_WORDS];
    struct fake_write writes[FAKE_WRITES];
    unsigned write_count;
    unsigned ehci_write_count;
    unsigned ignore_ehci_write;
    unsigned delay_us;
    unsigned busy_reads;
    unsigned force_busy;
    unsigned vbus_calls;
    int vbus_on;
    unsigned first_event;
    unsigned trace_count;
    const char *last_trace;
};

static unsigned
fake_ehci_read(void *arg, unsigned reg)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    return fake->ehci_regs[reg / 4u];
}

static void
fake_ehci_write(void *arg, unsigned reg, unsigned value)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    ++fake->ehci_write_count;
    if (!fake->ignore_ehci_write)
        fake->ehci_regs[reg / 4u] = value;
}

static unsigned
fake_read(void *arg, unsigned reg)
{
    struct fake_hw *fake;
    unsigned value;

    fake = (struct fake_hw *)arg;
    value = fake->regs[reg / 4u];
    if (reg == CI20_CPM_UHCCDR &&
        (fake->force_busy || fake->busy_reads != 0)) {
        if (!fake->force_busy)
            --fake->busy_reads;
        value |= CI20_UHCCDR_BUSY;
    }
    return value;
}

static void
fake_write(void *arg, unsigned reg, unsigned value)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    if (fake->first_event == 0)
        fake->first_event = 2;
    if (fake->write_count < FAKE_WRITES) {
        fake->writes[fake->write_count].reg = reg;
        fake->writes[fake->write_count].value = value;
    }
    ++fake->write_count;
    fake->regs[reg / 4u] = value;
    if (reg == CI20_CPM_UHCCDR && !fake->force_busy)
        fake->busy_reads = 2;
}

static void
fake_vbus(void *arg, int on)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    if (fake->first_event == 0)
        fake->first_event = 1;
    ++fake->vbus_calls;
    fake->vbus_on = on;
}

static void
fake_delay(void *arg, unsigned usec)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    fake->delay_us += usec;
}

static void
fake_trace(void *arg, const char *stage)
{
    struct fake_hw *fake;

    fake = (struct fake_hw *)arg;
    ++fake->trace_count;
    fake->last_trace = stage;
}

static const struct ci20_usb_hw_ops fake_ops = {
    .cuo_read_cpm = fake_read,
    .cuo_write_cpm = fake_write,
    .cuo_read_ehci = fake_ehci_read,
    .cuo_write_ehci = fake_ehci_write,
    .cuo_set_vbus = fake_vbus,
    .cuo_delay_us = fake_delay,
    .cuo_trace = fake_trace,
};

static int
saw_write(const struct fake_hw *fake, unsigned reg, unsigned mask,
    unsigned expected)
{
    unsigned i;

    for (i = 0; i < fake->write_count && i < FAKE_WRITES; ++i)
        if (fake->writes[i].reg == reg &&
            (fake->writes[i].value & mask) == expected)
            return 1;
    return 0;
}

static int
test_start(void)
{
    struct fake_hw fake;
    unsigned original_uhccdr;
    unsigned expected;

    memset(&fake, 0, sizeof(fake));
    fake.regs[CI20_CPM_CLKGR0 / 4u] = 0xffffffffu;
    fake.regs[CI20_CPM_OPCR / 4u] = 0x01020304u;
    fake.regs[CI20_CPM_USBPCR / 4u] =
        0xf0ffffffu & ~CI20_USBPCR_POR;
    fake.regs[CI20_CPM_USBPCR1 / 4u] = 0xffffffffu;
    original_uhccdr = 0x5a55a555u;
    fake.regs[CI20_CPM_UHCCDR / 4u] = original_uhccdr;
    fake.regs[CI20_CPM_SRBC / 4u] = 0x12340000u;
    fake.ehci_regs[CI20_EHCI_UTMI_BUS / 4u] = 0xa5a50000u;

    CHECK(ci20_usb_hw_start(&fake_ops, &fake) == CI20_USB_HW_OK);
    CHECK(fake.first_event == 1);
    CHECK(fake.vbus_calls == 1);
    CHECK(fake.vbus_on == 1);
    CHECK(fake.delay_us == 1002u + CI20_PHY_RESET_ASSERT_US +
        CI20_UHC_RESET_ASSERT_US +
        CI20_UHC_RESET_RECOVERY_US);
    CHECK(fake.trace_count == 9u);
    CHECK(strcmp(fake.last_trace, "hardware ready") == 0);

    expected = original_uhccdr & ~(CI20_UHCCDR_SOURCE_MASK |
        CI20_UHCCDR_CHANGE_ENABLE | CI20_UHCCDR_BUSY |
        CI20_UHCCDR_STOP | CI20_UHCCDR_DIV_MASK);
    expected |= CI20_UHCCDR_MPLL | CI20_UHCCDR_CHANGE_ENABLE |
        CI20_UHCCDR_MPLL_48_DIV;
    CHECK(fake.regs[CI20_CPM_UHCCDR / 4u] == expected);
    CHECK((fake.regs[CI20_CPM_CLKGR0 / 4u] & CI20_CLKGR0_UHC) == 0);
    CHECK((fake.regs[CI20_CPM_OPCR / 4u] & CI20_OPCR_SPENDN1) != 0);
    CHECK((fake.regs[CI20_CPM_USBPCR / 4u] &
        (CI20_USBPCR_POR | CI20_USBPCR_SIDDQ |
        CI20_USBPCR_OTG_DISABLE)) == 0);
    expected = fake.regs[CI20_CPM_USBPCR1 / 4u];
    CHECK((expected & CI20_USBPCR1_REFCLKSEL_MASK) ==
        CI20_USBPCR1_REFCLKSEL_CORE);
    CHECK((expected & CI20_USBPCR1_REFCLKDIV_MASK) ==
        CI20_USBPCR1_REFCLKDIV_48);
    CHECK((expected & (CI20_USBPCR1_DMPD1 | CI20_USBPCR1_DPPD1 |
        CI20_USBPCR1_WORD_IF0 | CI20_USBPCR1_WORD_IF1)) ==
        (CI20_USBPCR1_DMPD1 | CI20_USBPCR1_DPPD1 |
        CI20_USBPCR1_WORD_IF0 | CI20_USBPCR1_WORD_IF1));
    CHECK((expected & CI20_USBPCR1_PORT1_RST) == 0);
    CHECK((fake.regs[CI20_CPM_SRBC / 4u] &
        CI20_SRBC_UHC_RESET) == 0);
    CHECK((fake.ehci_regs[CI20_EHCI_UTMI_BUS / 4u] &
        CI20_EHCI_UTMI_BUS_WIDTH) != 0);
    CHECK(fake.ehci_write_count == 1u);
    CHECK(saw_write(&fake, CI20_CPM_USBPCR, CI20_USBPCR_POR,
        CI20_USBPCR_POR));
    CHECK(saw_write(&fake, CI20_CPM_SRBC, CI20_SRBC_UHC_RESET,
        CI20_SRBC_UHC_RESET));

    /* Simulate the generic EHCI reset clearing the vendor bit. */
    fake.ehci_regs[CI20_EHCI_UTMI_BUS / 4u] = 0;
    CHECK(ci20_usb_hw_ehci_utmi_width(&fake_ops, &fake) ==
        CI20_USB_HW_OK);
    CHECK((fake.ehci_regs[CI20_EHCI_UTMI_BUS / 4u] &
        CI20_EHCI_UTMI_BUS_WIDTH) != 0);
    CHECK(fake.ehci_write_count == 2u);
    return 0;
}

static int
test_failures(void)
{
    struct fake_hw fake;

    memset(&fake, 0, sizeof(fake));
    CHECK(ci20_usb_hw_start(0, &fake) == CI20_USB_HW_INVALID);
    fake.force_busy = 1;
    CHECK(ci20_usb_hw_start(&fake_ops, &fake) ==
        CI20_USB_HW_CLOCK_TIMEOUT);
    CHECK(fake.vbus_on == 1);
    CHECK(fake.delay_us == 11000u);
    CHECK(fake.trace_count == 3u);
    CHECK(strcmp(fake.last_trace, "configure UHC clock") == 0);
    CHECK((fake.regs[CI20_CPM_CLKGR0 / 4u] & CI20_CLKGR0_UHC) == 0);

    memset(&fake, 0, sizeof(fake));
    fake.ignore_ehci_write = 1;
    CHECK(ci20_usb_hw_ehci_utmi_width(&fake_ops, &fake) ==
        CI20_USB_HW_UTMI_ERROR);
    return 0;
}

int
main(void)
{
    if (test_start() != 0 || test_failures() != 0)
        return 1;
    puts("ci20 usb hw tests: ok");
    return 0;
}
