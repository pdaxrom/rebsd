#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <machine/romfs_flash.h>

#include "../common/romfs_backend.h"
#include "../../../src/cmd/romfsctl/romfs.h"

#define MALTA_CARTFLASH_SIZE        (8u * 1024u * 1024u)
#define MALTA_CARTFLASH_FW_SIZE     0x4000u
#define MALTA_CARTFLASH_ROMFS_OFF   0x8000u
#define MALTA_CARTFLASH_JEDEC       0xc22015u
/*
 * Sparse flash sectors kept in RAM for Malta tests.  384 sectors covers the
 * 1MB diskspeed smoke plus ROMFS metadata without consuming a full 8MB flash
 * image inside the 8MB guest.
 */
#define MALTA_CARTFLASH_SLOTS       384u
#define MALTA_CARTFLASH_MAP_SIZE    \
    (((MALTA_CARTFLASH_SIZE / ROMFS_FLASH_SECTOR) * sizeof(uint16_t) + \
    (ROMFS_FLASH_SECTOR - 1)) & ~(ROMFS_FLASH_SECTOR - 1))
#define MALTA_CARTFLASH_LIST_SIZE   \
    (((MALTA_CARTFLASH_SIZE / ROMFS_MB) * sizeof(romfs_entry) + \
    (ROMFS_FLASH_SECTOR - 1)) & ~(ROMFS_FLASH_SECTOR - 1))
#define MALTA_CARTFLASH_STORAGE     __attribute__((section(".cartflash_bss")))

struct malta_cartflash_slot {
    unsigned offset;
    int valid;
    unsigned char data[MIPS_ROMFS_FLASH_SECTOR];
};

static struct malta_cartflash_slot
    malta_cartflash_slots[MALTA_CARTFLASH_SLOTS] MALTA_CARTFLASH_STORAGE;
static unsigned char
    malta_cartflash_buf[MIPS_ROMFS_FLASH_SECTOR] MALTA_CARTFLASH_STORAGE;
static uint16_t
    malta_cartflash_map[MALTA_CARTFLASH_MAP_SIZE / sizeof(uint16_t)]
    MALTA_CARTFLASH_STORAGE;
static uint8_t
    malta_cartflash_list[MALTA_CARTFLASH_LIST_SIZE] MALTA_CARTFLASH_STORAGE;
static int malta_cartflash_ready;

static void
malta_cartflash_clear_slots(void)
{
    unsigned i;

    for (i = 0; i < MALTA_CARTFLASH_SLOTS; i++)
        malta_cartflash_slots[i].valid = 0;
}

static struct malta_cartflash_slot *
malta_cartflash_find_slot(unsigned offset)
{
    unsigned base = offset & ~(MIPS_ROMFS_FLASH_SECTOR - 1);
    unsigned i;

    for (i = 0; i < MALTA_CARTFLASH_SLOTS; i++) {
        if (malta_cartflash_slots[i].valid &&
            malta_cartflash_slots[i].offset == base)
            return &malta_cartflash_slots[i];
    }
    return 0;
}

static struct malta_cartflash_slot *
malta_cartflash_alloc_slot(unsigned offset)
{
    unsigned base = offset & ~(MIPS_ROMFS_FLASH_SECTOR - 1);
    unsigned i;

    for (i = 0; i < MALTA_CARTFLASH_SLOTS; i++) {
        if (!malta_cartflash_slots[i].valid) {
            malta_cartflash_slots[i].valid = 1;
            malta_cartflash_slots[i].offset = base;
            memset(malta_cartflash_slots[i].data, 0xff,
                sizeof(malta_cartflash_slots[i].data));
            return &malta_cartflash_slots[i];
        }
    }
    return 0;
}

static int
malta_cartflash_erased(const unsigned char *data)
{
    unsigned i;

    for (i = 0; i < MIPS_ROMFS_FLASH_SECTOR; i++) {
        if (data[i] != 0xff)
            return 0;
    }
    return 1;
}

static void
malta_cartflash_info(struct mipsromfs_flash_info *info)
{
    bzero(info, sizeof(*info));
    info->jedec_id = MALTA_CARTFLASH_JEDEC;
    info->rom_size = MALTA_CARTFLASH_SIZE;
    info->fw_size = MALTA_CARTFLASH_FW_SIZE;
    info->romfs_offset = MALTA_CARTFLASH_ROMFS_OFF;
    info->sector_size = MIPS_ROMFS_FLASH_SECTOR;
}

static int
malta_cartflash_check_range(const struct mipsromfs_flash_info *info,
    unsigned offset, unsigned size, const void *buffer)
{
    if (size == 0 || size > MIPS_ROMFS_FLASH_MAX_TRANSFER)
        return EINVAL;
    if (buffer == 0)
        return EFAULT;
    if (offset > info->rom_size || size > info->rom_size - offset)
        return EINVAL;
    return 0;
}

static int
malta_cartflash_check_write_range(const struct mipsromfs_flash_info *info,
    unsigned offset, unsigned size, const void *buffer)
{
    int error;

    error = malta_cartflash_check_range(info, offset, size, buffer);
    if (error)
        return error;
    if (offset < info->romfs_offset)
        return EROFS;
    return 0;
}

static int
malta_cartflash_check_erase_range(const struct mipsromfs_flash_info *info,
    unsigned offset)
{
    if ((offset & (MIPS_ROMFS_FLASH_SECTOR - 1)) != 0 ||
        offset >= info->rom_size)
        return EINVAL;
    if (offset < info->romfs_offset)
        return EROFS;
    return 0;
}

static void
malta_cartflash_init(void)
{
    uint32_t map_size;
    uint32_t list_size;

    if (malta_cartflash_ready)
        return;
    malta_cartflash_ready = 1;
    malta_cartflash_clear_slots();
    romfs_get_buffers_sizes(MALTA_CARTFLASH_SIZE, &map_size, &list_size);
    if (map_size > sizeof(malta_cartflash_map) ||
        list_size > sizeof(malta_cartflash_list))
        panic("malta cartflash map");
    if (!romfs_start(MALTA_CARTFLASH_ROMFS_OFF, MALTA_CARTFLASH_SIZE,
        malta_cartflash_map, malta_cartflash_list))
        panic("malta cartflash start");
    if (!romfs_format())
        panic("malta cartflash format");
}

static int
malta_cartflash_backend_getinfo(struct mipsromfs_flash_info *info)
{
    malta_cartflash_init();
    malta_cartflash_info(info);
    return 0;
}

static int
malta_cartflash_backend_read(unsigned offset, void *buffer, unsigned size)
{
    struct mipsromfs_flash_info info;
    struct malta_cartflash_slot *slot;
    unsigned char *dst;
    unsigned chunk;
    unsigned pos;
    int error;

    malta_cartflash_init();
    malta_cartflash_info(&info);
    error = malta_cartflash_check_range(&info, offset, size, buffer);
    if (error)
        return error;
    dst = (unsigned char *)buffer;
    while (size != 0) {
        pos = offset & (MIPS_ROMFS_FLASH_SECTOR - 1);
        chunk = MIPS_ROMFS_FLASH_SECTOR - pos;
        if (chunk > size)
            chunk = size;
        slot = malta_cartflash_find_slot(offset);
        if (slot != 0)
            bcopy((caddr_t)&slot->data[pos], (caddr_t)dst, chunk);
        else
            memset(dst, 0xff, chunk);
        offset += chunk;
        dst += chunk;
        size -= chunk;
    }
    return 0;
}

static int
malta_cartflash_backend_write_sector(unsigned offset, const void *buffer)
{
    struct mipsromfs_flash_info info;
    struct malta_cartflash_slot *slot;
    const unsigned char *src = (const unsigned char *)buffer;
    unsigned i;
    int error;

    malta_cartflash_init();
    malta_cartflash_info(&info);
    error = malta_cartflash_check_write_range(&info, offset,
        MIPS_ROMFS_FLASH_SECTOR, buffer);
    if (error)
        return error;
    if ((offset & (MIPS_ROMFS_FLASH_SECTOR - 1)) != 0)
        return EINVAL;

    slot = malta_cartflash_find_slot(offset);
    if (slot == 0) {
        if (malta_cartflash_erased(src))
            return 0;
        slot = malta_cartflash_alloc_slot(offset);
        if (slot == 0)
            return ENOSPC;
    }
    for (i = 0; i < MIPS_ROMFS_FLASH_SECTOR; i++) {
        if ((slot->data[i] & src[i]) != src[i])
            return EIO;
    }
    if (malta_cartflash_erased(src)) {
        slot->valid = 0;
        return 0;
    }
    for (i = 0; i < MIPS_ROMFS_FLASH_SECTOR; i++)
        slot->data[i] &= src[i];
    return 0;
}

static int
malta_cartflash_backend_erase_sector(unsigned offset)
{
    struct mipsromfs_flash_info info;
    struct malta_cartflash_slot *slot;
    int error;

    malta_cartflash_init();
    malta_cartflash_info(&info);
    error = malta_cartflash_check_erase_range(&info, offset);
    if (error)
        return error;
    slot = malta_cartflash_find_slot(offset);
    if (slot != 0)
        slot->valid = 0;
    return 0;
}

static int
malta_cartflash_backend_sync(void)
{
    return 0;
}

const struct mipsromfs_backend mipsromfs_backend = {
    malta_cartflash_backend_getinfo,
    malta_cartflash_backend_read,
    malta_cartflash_backend_write_sector,
    malta_cartflash_backend_erase_sector,
    malta_cartflash_backend_sync,
};

int
malta_cartflash_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    if (minor(dev) != 0)
        return ENXIO;
    malta_cartflash_init();
    return 0;
}

int
malta_cartflash_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    if (minor(dev) != 0)
        return ENXIO;
    return 0;
}

int
malta_cartflash_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct mipsromfs_flash_info minfo;
    struct mipsromfs_flash_info info;
    struct mipsromfs_flash_io io;
    unsigned offset;
    int error;

    (void)flag;
    if (minor(dev) != 0)
        return ENXIO;
    malta_cartflash_init();
    malta_cartflash_info(&minfo);

    if (cmd == MIPSROMFSFLASHIOC_GETINFO) {
        struct mipsromfs_flash_info *result;

        result = (struct mipsromfs_flash_info *)data;
        info.jedec_id = minfo.jedec_id;
        info.rom_size = minfo.rom_size;
        info.fw_size = minfo.fw_size;
        info.romfs_offset = minfo.romfs_offset;
        info.sector_size = minfo.sector_size;
        *result = info;
        return 0;
    }

    switch (cmd) {
    case MIPSROMFSFLASHIOC_READ:
        io = *(struct mipsromfs_flash_io *)data;
        error = malta_cartflash_check_range(&minfo, io.offset, io.size,
            io.buffer);
        if (error)
            return error;
        error = malta_cartflash_backend_read(io.offset, malta_cartflash_buf,
            io.size);
        if (error)
            return error;
        return copyout((caddr_t)malta_cartflash_buf, io.buffer, io.size);

    case MIPSROMFSFLASHIOC_WRITE:
        io = *(struct mipsromfs_flash_io *)data;
        error = malta_cartflash_check_write_range(&minfo, io.offset,
            io.size, io.buffer);
        if (error)
            return error;
        if ((io.offset & (MIPS_ROMFS_FLASH_SECTOR - 1)) != 0 ||
            io.size != MIPS_ROMFS_FLASH_SECTOR)
            return EINVAL;
        error = copyin(io.buffer, (caddr_t)malta_cartflash_buf, io.size);
        if (error)
            return error;
        return malta_cartflash_backend_write_sector(io.offset,
            malta_cartflash_buf);

    case MIPSROMFSFLASHIOC_ERASE:
        offset = *(unsigned *)data;
        return malta_cartflash_backend_erase_sector(offset);

    default:
        return ENOTTY;
    }
}
