#include "boot.h"
#include "ide.h"
#include "io.h"

#include <sys/errno.h>

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
#define IDE_MAX_READ_SECTORS    128u

static i386_u16 i386_ide_identify_words[IDE_SECTOR_WORDS];
static i386_u8 i386_ide_sector_data[DISK_SECTOR_SIZE * 2u];
static i386_u32 i386_ide_sectors;
static int i386_ide_available;

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
i386_ide_read_identify_words(void)
{
    unsigned index;

    for (index = 0; index < IDE_SECTOR_WORDS; ++index)
        i386_ide_identify_words[index] =
            i386_inw(IDE_PRIMARY_BASE + IDE_REG_DATA);
}

static void
i386_ide_read_sector_words(i386_u8 *data)
{
    i386_u16 word;
    unsigned index;

    for (index = 0; index < IDE_SECTOR_WORDS; ++index) {
        word = i386_inw(IDE_PRIMARY_BASE + IDE_REG_DATA);
        data[index * 2u] = (i386_u8)word;
        data[index * 2u + 1u] = (i386_u8)(word >> 8);
    }
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

    i386_ide_read_identify_words();
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
        word = i386_ide_identify_words[
            IDE_IDENTIFY_MODEL_FIRST + index];
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
i386_ide_read_one(i386_u32 lba, i386_u8 *data)
{
    i386_u8 status;

    i386_outb(IDE_PRIMARY_BASE + IDE_REG_DRIVE,
        (i386_u8)(0xe0u | ((lba >> 24) & 0x0fu)));
    i386_ide_delay_400ns();
    if (i386_ide_wait_not_busy(&status) != 0 ||
        status == 0 || status == 0xffu)
        return EIO;

    i386_outb(IDE_PRIMARY_BASE + IDE_REG_SECTOR_COUNT, 1);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_LOW, (i386_u8)lba);
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_MID, (i386_u8)(lba >> 8));
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_LBA_HIGH, (i386_u8)(lba >> 16));
    i386_outb(IDE_PRIMARY_BASE + IDE_REG_COMMAND, IDE_COMMAND_READ);
    if (i386_ide_wait_data() != 0)
        return EIO;

    i386_ide_read_sector_words(data);
    if (i386_ide_wait_not_busy(&status) != 0 ||
        (status & (IDE_STATUS_ERROR | IDE_STATUS_DEVICE_FAULT)) != 0)
        return EIO;
    return 0;
}

static int
i386_ide_backend_read(void *arg, disk_sector_t lba, unsigned count,
    void *data_arg)
{
    i386_u8 *data;
    unsigned index;

    (void)arg;
    if (!i386_ide_available)
        return ENXIO;
    if (data_arg == 0 || count == 0 || count > IDE_MAX_READ_SECTORS ||
        lba >= i386_ide_sectors ||
        (disk_sector_t)count > (disk_sector_t)i386_ide_sectors - lba)
        return EINVAL;

    data = (i386_u8 *)data_arg;
    for (index = 0; index < count; ++index) {
        if (i386_ide_read_one((i386_u32)lba + index,
            data + index * DISK_SECTOR_SIZE) != 0)
            return EIO;
    }
    return 0;
}

static int
i386_ide_backend_present(void *arg)
{
    (void)arg;
    return i386_ide_available;
}

static const struct disk_backend_ops i386_ide_ops = {
    i386_ide_backend_read,
    0,
    0,
    i386_ide_backend_present
};

const struct disk_backend_ops *
i386_ide_backend_ops(void)
{
    return &i386_ide_ops;
}

disk_sector_t
i386_ide_sector_count(void)
{
    return i386_ide_sectors;
}

static int
i386_ide_data_has(const i386_u8 *data, unsigned offset, const char *wanted)
{
    while (*wanted != '\0') {
        if (offset >= DISK_SECTOR_SIZE || data[offset] != (i386_u8)*wanted)
            return 0;
        ++offset;
        ++wanted;
    }
    return 1;
}

static unsigned
i386_ide_fat_type(const i386_u8 *data)
{
    if (data[510] != 0x55u || data[511] != 0xaau ||
        !i386_ide_data_has(data, 3, "REBSD   "))
        return 0;
    if (i386_ide_data_has(data, 54, "FAT16   "))
        return 16;
    if (i386_ide_data_has(data, 82, "FAT32   "))
        return 32;
    return 0;
}

static void
i386_ide_print_sector(const char *label, disk_sector_t value)
{
    i386_early_puts(label);
    i386_early_put_hex64((i386_u32)(value >> 32), (i386_u32)value);
    i386_early_putc('\n');
}

static int
i386_ide_partition_probe(void)
{
    struct disk_mbr mbr;
    struct disk_table table;
    disk_sector_t count;
    disk_sector_t start;
    const struct disk_partition *part;
    int error;

    disk_mbr_parse(&mbr, i386_ide_sector_data, i386_ide_sectors);
    if (!mbr.dm_valid) {
        i386_early_puts("ide-mbr-table: invalid\n");
        return 0;
    }
    if (disk_mbr_is_protective(&mbr)) {
        i386_early_puts("ide-mbr-table: protective\n");
        return 1;
    }
    disk_table_from_mbr(&table, &mbr);
    if (disk_table_region(&table, i386_ide_sectors,
        DISK_MINOR_PARTITION(0), &start, &count) != 0) {
        i386_early_puts("ide-mbr-table: empty\n");
        return 1;
    }

    part = &table.dt_partitions[0];
    i386_early_puts("ide-mbr-table: ok\n");
    i386_early_puts("ide-part0-type: ");
    i386_early_put_hex32(part->dp_type);
    i386_early_putc('\n');
    i386_ide_print_sector("ide-part0-start: ", start);
    i386_ide_print_sector("ide-part0-sectors: ", count);

    error = i386_ide_ops.dbo_read(0, start, count >= 2u ? 2u : 1u,
        i386_ide_sector_data);
    if (error != 0) {
        i386_early_puts("ide-partition-read: failed\n");
        return 0;
    }
    if (i386_ide_fat_type(i386_ide_sector_data) == 16)
        i386_early_puts("ide-partition-read: rebsd-fat16\n");
    else if (i386_ide_fat_type(i386_ide_sector_data) == 32)
        i386_early_puts("ide-partition-read: rebsd-fat32\n");
    else
        i386_early_puts("ide-partition-read: external\n");

    error = i386_ide_ops.dbo_read(0, start + count - 1u, 1u,
        i386_ide_sector_data);
    if (error == 0 && i386_ide_data_has(i386_ide_sector_data, 0,
        "REBSDEND"))
        i386_early_puts("ide-last-lba: rebsd-smoke\n");
    else if (error == 0)
        i386_early_puts("ide-last-lba: external\n");
    else
        i386_early_puts("ide-last-lba: failed\n");
    return 1;
}

int
i386_ide_probe(void)
{
    int error;

    i386_ide_available = 0;
    i386_ide_sectors = 0;
    if (i386_ide_identify() != 0) {
        i386_early_puts("ide-primary-master: none\n");
        return 0;
    }

    i386_early_puts("ide-primary-master: ata\n");
    i386_ide_print_model();
    i386_ide_sectors = (i386_u32)i386_ide_identify_words[
        IDE_IDENTIFY_LBA28_LOW] |
        ((i386_u32)i386_ide_identify_words[
        IDE_IDENTIFY_LBA28_HIGH] << 16);
    i386_early_puts("ide-sectors: ");
    i386_early_put_hex32(i386_ide_sectors);
    i386_early_putc('\n');

    if ((i386_ide_identify_words[IDE_IDENTIFY_CAPABILITIES] &
        IDE_CAPABILITY_LBA) == 0 || i386_ide_sectors == 0) {
        i386_early_puts("ide-lba28: unsupported\n");
        return 0;
    }
    i386_early_puts("ide-lba28: ok\n");
    i386_ide_available = 1;
    error = i386_ide_ops.dbo_read(0, 0, 1, i386_ide_sector_data);
    if (error != 0) {
        i386_early_puts("ide-lba0: failed\n");
        i386_ide_available = 0;
        return 0;
    }
    i386_early_puts("ide-backend-read: ok\n");
    i386_early_puts("ide-lba0: ok\n");
    i386_early_puts("ide-image: ");
    i386_early_puts(i386_ide_data_has(i386_ide_sector_data, 0,
        "REBSDIDE") ? "rebsd-smoke\n" : "external\n");
    i386_early_puts("ide-mbr: ");
    i386_early_puts(i386_ide_sector_data[510] == 0x55u &&
        i386_ide_sector_data[511] == 0xaau ?
        "present\n" : "absent\n");
    (void)i386_ide_partition_probe();
    if (i386_ide_ops.dbo_read(0, i386_ide_sectors, 1,
        i386_ide_sector_data) == EINVAL &&
        i386_ide_ops.dbo_read(0, 0, IDE_MAX_READ_SECTORS + 1u,
        i386_ide_sector_data) == EINVAL &&
        i386_ide_ops.dbo_read(0, 0, 1, 0) == EINVAL &&
        i386_ide_ops.dbo_read(0, 0, 0, i386_ide_sector_data) == EINVAL)
        i386_early_puts("ide-bounds: ok\n");
    else
        i386_early_puts("ide-bounds: failed\n");
    return 1;
}
