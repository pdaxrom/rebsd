#include "boot.h"
#include "ide.h"
#include "io.h"

#define IDE_PRIMARY_BASE        0x01f0u
#define IDE_PRIMARY_CONTROL     0x03f6u

#define IDE_REG_DATA            0u
#define IDE_REG_ERROR           1u
#define IDE_REG_SECTOR_COUNT    2u
#define IDE_REG_LBA_LOW         3u
#define IDE_REG_LBA_MID         4u
#define IDE_REG_LBA_HIGH        5u
#define IDE_REG_DRIVE           6u
#define IDE_REG_STATUS          7u
#define IDE_REG_COMMAND         7u

#define IDE_STATUS_ERROR        0x01u
#define IDE_STATUS_DATA_REQUEST 0x08u
#define IDE_STATUS_DEVICE_FAULT 0x20u
#define IDE_STATUS_BUSY         0x80u

#define IDE_COMMAND_READ        0x20u
#define IDE_COMMAND_IDENTIFY    0xecu

#define IDE_IDENTIFY_MODEL_FIRST    27u
#define IDE_IDENTIFY_MODEL_WORDS    20u
#define IDE_IDENTIFY_CAPABILITIES   49u
#define IDE_IDENTIFY_LBA28_LOW      60u
#define IDE_IDENTIFY_LBA28_HIGH     61u
#define IDE_CAPABILITY_LBA          0x0200u

#define IDE_SECTOR_WORDS        256u
#define IDE_POLL_LIMIT          1000000u

static i386_u16 i386_ide_words[IDE_SECTOR_WORDS];

static void
i386_ide_print_register(const char *label, i386_u8 value)
{
    i386_early_puts(label);
    i386_early_put_hex32(value);
    i386_early_putc('\n');
}

static i386_u8
i386_ide_status(void)
{
    return i386_inb(IDE_PRIMARY_BASE + IDE_REG_STATUS);
}

static void
i386_ide_delay_400ns(void)
{
    unsigned index;

    for (index = 0; index < 4u; ++index)
        (void)i386_inb(IDE_PRIMARY_CONTROL);
}

static int
i386_ide_wait_not_busy(i386_u8 *last_status)
{
    i386_u8 status;
    unsigned count;

    status = 0xffu;
    for (count = 0; count < IDE_POLL_LIMIT; ++count) {
        status = i386_ide_status();
        if ((status & IDE_STATUS_BUSY) == 0) {
            *last_status = status;
            return 0;
        }
    }
    *last_status = status;
    return 1;
}

static int
i386_ide_wait_data(void)
{
    i386_u8 status;
    unsigned count;

    for (count = 0; count < IDE_POLL_LIMIT; ++count) {
        status = i386_ide_status();
        if ((status & IDE_STATUS_BUSY) != 0)
            continue;
        if ((status & (IDE_STATUS_ERROR | IDE_STATUS_DEVICE_FAULT)) != 0)
            return 1;
        if ((status & IDE_STATUS_DATA_REQUEST) != 0)
            return 0;
    }
    return 1;
}

static void
i386_ide_read_words(void)
{
    unsigned index;

    for (index = 0; index < IDE_SECTOR_WORDS; ++index)
        i386_ide_words[index] = i386_inw(IDE_PRIMARY_BASE + IDE_REG_DATA);
}

static int
i386_ide_identify(void)
{
    i386_u8 lba_high;
    i386_u8 lba_mid;
    i386_u8 status;

    /*
     * A BIOS may leave either device selected.  Select the master before
     * interpreting a zero status as "no device".
     */
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_DRIVE, 0xa0u);
    i386_ide_delay_400ns();
    status = i386_ide_status();
    i386_ide_print_register("ide-initial-status: ", status);
    if (status == 0 || status == 0xffu) {
        i386_early_puts("ide-identify-failure: no-status\n");
        return 1;
    }

    if (i386_ide_wait_not_busy(&status) != 0) {
        i386_early_puts("ide-identify-failure: select-timeout\n");
        return 1;
    }

    i386_outb(IDE_PRIMARY_BASE + IDE_REG_SECTOR_COUNT, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_LOW, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_MID, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_HIGH, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_COMMAND, IDE_COMMAND_IDENTIFY);

    status = i386_ide_status();
    if (status == 0 || status == 0xffu) {
        i386_early_puts("ide-identify-failure: command-status\n");
        return 1;
    }
    if (i386_ide_wait_not_busy(&status) != 0) {
        i386_early_puts("ide-identify-failure: command-timeout\n");
        return 1;
    }
    /*
     * ATAPI devices leave a non-zero signature here after IDENTIFY DEVICE.
     * The first bootstrap driver deliberately supports only ATA disks.
     */
    lba_mid = i386_inb(IDE_PRIMARY_BASE + IDE_REG_LBA_MID);
    lba_high = i386_inb(IDE_PRIMARY_BASE + IDE_REG_LBA_HIGH);
    if (lba_mid != 0 || lba_high != 0) {
        i386_ide_print_register("ide-signature-mid: ", lba_mid);
        i386_ide_print_register("ide-signature-high: ", lba_high);
        i386_early_puts("ide-identify-failure: non-ata\n");
        return 1;
    }
    if (i386_ide_wait_data() != 0) {
        i386_ide_print_register("ide-command-status: ",
            i386_ide_status());
        i386_ide_print_register("ide-command-error: ",
            i386_inb(IDE_PRIMARY_BASE + IDE_REG_ERROR));
        i386_early_puts("ide-identify-failure: no-data\n");
        return 1;
    }

    i386_ide_read_words();
    return 0;
}

static void
i386_ide_print_model(void)
{
    char model[IDE_IDENTIFY_MODEL_WORDS * 2u + 1u];
    i386_u16 word;
    unsigned end;
    unsigned index;

    for (index = 0; index < IDE_IDENTIFY_MODEL_WORDS; ++index) {
        word = i386_ide_words[IDE_IDENTIFY_MODEL_FIRST + index];
        model[index * 2u] = (char)(word >> 8);
        model[index * 2u + 1u] = (char)(word & 0xffu);
    }
    end = IDE_IDENTIFY_MODEL_WORDS * 2u;
    while (end != 0 && model[end - 1u] == ' ')
        --end;
    model[end] = '\0';

    i386_early_puts("ide-model: ");
    for (index = 0; index < end; ++index)
        i386_early_putc(model[index]);
    i386_early_putc('\n');
}

static int
i386_ide_read_lba0(void)
{
    i386_u8 status;

    i386_outb(IDE_PRIMARY_BASE + IDE_REG_DRIVE, 0xe0u);
    i386_ide_delay_400ns();
    if (i386_ide_wait_not_busy(&status) != 0)
        return 1;

    i386_outb(IDE_PRIMARY_BASE + IDE_REG_SECTOR_COUNT, 1);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_LOW, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_MID, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_HIGH, 0);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_COMMAND, IDE_COMMAND_READ);
    if (i386_ide_wait_data() != 0)
        return 1;

    i386_ide_read_words();
    if (i386_ide_wait_not_busy(&status) != 0 ||
        (status & (IDE_STATUS_ERROR | IDE_STATUS_DEVICE_FAULT)) != 0)
        return 1;
    return 0;
}

static int
i386_ide_lba0_has_marker(void)
{
    return i386_ide_words[0] == 0x4552u &&
        i386_ide_words[1] == 0x5342u &&
        i386_ide_words[2] == 0x4944u &&
        i386_ide_words[3] == 0x4544u;
}

int
i386_ide_probe(void)
{
    i386_u32 sectors;

    if (i386_ide_identify() != 0) {
        i386_early_puts("ide-primary-master: none\n");
        return 0;
    }

    i386_early_puts("ide-primary-master: ata\n");
    i386_ide_print_model();
    sectors = (i386_u32)i386_ide_words[IDE_IDENTIFY_LBA28_LOW] |
        ((i386_u32)i386_ide_words[IDE_IDENTIFY_LBA28_HIGH] << 16);
    i386_early_puts("ide-sectors: ");
    i386_early_put_hex32(sectors);
    i386_early_putc('\n');

    if ((i386_ide_words[IDE_IDENTIFY_CAPABILITIES] &
        IDE_CAPABILITY_LBA) == 0 || sectors == 0) {
        i386_early_puts("ide-lba28: unsupported\n");
        return 0;
    }
    i386_early_puts("ide-lba28: ok\n");
    if (i386_ide_read_lba0() != 0) {
        i386_early_puts("ide-lba0: failed\n");
        return 0;
    }
    i386_early_puts("ide-lba0: ok\n");
    i386_early_puts("ide-image: ");
    i386_early_puts(i386_ide_lba0_has_marker() ? "rebsd-smoke\n" :
        "external\n");
    i386_early_puts("ide-mbr: ");
    i386_early_puts(i386_ide_words[255] == 0xaa55u ?
        "present\n" : "absent\n");
    return 1;
}
