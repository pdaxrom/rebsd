#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <machine/io.h>
#include <machine/n64int.h>
#include <machine/n64cart_uart.h>
#include <machine/n64cart_flash.h>
#include <machine/n64pi.h>

#define N64_PI_STATUS_DMA_BUSY         0x01u
#define N64_PI_STATUS_IO_BUSY          0x02u
#define N64_PI_STATUS_BUSY             (N64_PI_STATUS_DMA_BUSY | \
                                        N64_PI_STATUS_IO_BUSY)

#define N64CART_FLASH_CMD_WREN         0x06u
#define N64CART_FLASH_CMD_RDSR         0x05u
#define N64CART_FLASH_CMD_JEDEC        0x9fu
#define N64CART_FLASH_CMD_ERASE4K      0x21u
#define N64CART_FLASH_CMD_PROGRAM      0x12u
#define N64CART_FLASH_CMD_READ         0x0cu
#define N64CART_FLASH_CMD_ADDR_LEN     5u
#define N64CART_FLASH_CMD_DUMMY_LEN    1u
#define N64CART_FLASH_PAGE             256u
#define N64CART_FLASH_READAHEAD_SECTORS 8u
#define N64CART_FLASH_READAHEAD_SIZE   \
    (N64CART_FLASH_SECTOR * N64CART_FLASH_READAHEAD_SECTORS)

struct n64cart_flash_chip {
    unsigned mf;
    unsigned id;
    unsigned mbytes;
};

static const struct n64cart_flash_chip n64cart_flash_chips[] = {
    { 0xc2, 0x201b, 128 },
    { 0xef, 0x4020, 64 },
    { 0xef, 0x4019, 32 },
    { 0xef, 0x4018, 16 },
    { 0xef, 0x4017, 8 },
    { 0xef, 0x4016, 4 },
    { 0xef, 0x4015, 2 },
};

static unsigned char n64cart_flash_buf[N64CART_FLASH_SECTOR];
static unsigned char n64cart_flash_read_cache[N64CART_FLASH_READAHEAD_SIZE];
static unsigned n64cart_flash_read_cache_base;
static unsigned n64cart_flash_read_cache_len;
static int n64cart_flash_read_cache_valid;
static int n64cart_flash_access_depth;
static struct n64cart_flash_info n64cart_flash_cached_info;
static int n64cart_flash_info_cached;
static char n64cart_flash_pi_owner;

#ifndef N64CART_FLASH_WRITE_ENABLE
#define N64CART_FLASH_WRITE_ENABLE 1
#endif

static volatile u_int *
n64cart_flash_reg(u_int offset)
{
    return (volatile u_int *)(N64CART_REG_BASE + offset);
}

static void
n64cart_flash_pi_wait(void)
{
    volatile u_int *status = (volatile u_int *)N64_PI_STATUS_ADDR;

    while (*status & N64_PI_STATUS_BUSY)
        ;
}

static u_int
n64cart_flash_read_reg(u_int offset)
{
    u_int value;

    n64cart_flash_pi_wait();
    asm volatile ("" ::: "memory");
    value = *n64cart_flash_reg(offset);
    asm volatile ("" ::: "memory");
    return value;
}

static void
n64cart_flash_write_reg(u_int offset, u_int value)
{
    n64cart_flash_pi_wait();
    asm volatile ("" ::: "memory");
    *n64cart_flash_reg(offset) = value;
    asm volatile ("" ::: "memory");
    n64cart_flash_pi_wait();
}

static void
n64cart_flash_cs_force(int high)
{
    u_int ctrl = n64cart_flash_read_reg(N64CART_SYS_CTRL);

    if (high)
        ctrl |= N64CART_FLASH_CS_HIGH;
    else
        ctrl &= ~N64CART_FLASH_CS_HIGH;
    n64cart_flash_write_reg(N64CART_SYS_CTRL, ctrl);
}

static void
n64cart_flash_mode(int quad)
{
    u_int ctrl = n64cart_flash_read_reg(N64CART_SYS_CTRL);

    if (quad)
        ctrl |= N64CART_FLASH_MODE_QUAD;
    else
        ctrl &= ~N64CART_FLASH_MODE_QUAD;
    n64cart_flash_write_reg(N64CART_SYS_CTRL, ctrl);
}

static void
n64cart_flash_enter_spi_command_mode(void)
{
    n64cart_flash_mode(0);
}

static void
n64cart_flash_restore_quad_rom_mode(void)
{
    n64cart_flash_mode(1);
}

static void
n64cart_flash_access_lock(void)
{
    if (n64cart_flash_access_depth != 0) {
        ++n64cart_flash_access_depth;
        return;
    }
    if (n64pi_bus_enter(&n64cart_flash_pi_owner) != 0)
        panic("n64cart flash PI lock");
    n64cart_flash_access_depth = 1;
    n64cart_flash_cs_force(1);
    n64cart_flash_enter_spi_command_mode();
}

static void
n64cart_flash_access_unlock(void)
{
    if (n64cart_flash_access_depth <= 0) {
        n64cart_flash_access_depth = 0;
        return;
    }
    if (--n64cart_flash_access_depth == 0) {
        n64cart_flash_cs_force(1);
        n64cart_flash_restore_quad_rom_mode();
        n64pi_bus_leave(&n64cart_flash_pi_owner);
    }
}

void
n64cart_flash_shutdown(void)
{
    /*
     * sync() holds the PI arbiter and restores quad-ROM mode before
     * returning.
     */
    n64cart_flash_sync();
}

static void
n64cart_flash_put_get(const unsigned char *tx, unsigned char *rx,
    unsigned count, unsigned rxskip)
{
    unsigned tx_remaining = count;
    unsigned rx_remaining = count;
    const unsigned max_in_flight = 14;

    while (tx_remaining || rx_remaining || rxskip) {
        u_int flags = n64cart_flash_read_reg(N64CART_SSI_SR);
        int can_put = (flags & N64CART_SSI_SR_TFNF) != 0;
        int can_get = (flags & N64CART_SSI_SR_RFNE) != 0;

        if (can_put && tx_remaining &&
            rx_remaining - tx_remaining < max_in_flight) {
            n64cart_flash_write_reg(N64CART_SSI_DR0,
                tx ? *tx++ : 0);
            --tx_remaining;
        }
        if (can_get && (rx_remaining || rxskip)) {
            unsigned char value =
                n64cart_flash_read_reg(N64CART_SSI_DR0) & 0xff;
            if (rxskip) {
                --rxskip;
            } else {
                if (rx)
                    *rx++ = value;
                --rx_remaining;
            }
        }
    }
    n64cart_flash_cs_force(1);
}

static void
n64cart_flash_do_cmd(unsigned cmd, const unsigned char *tx,
    unsigned char *rx, unsigned count)
{
    n64cart_flash_cs_force(0);
    n64cart_flash_write_reg(N64CART_SSI_DR0, cmd);
    n64cart_flash_put_get(tx, rx, count, 1);
}

static void
n64cart_flash_wait_ready(void)
{
    unsigned char status;

    do {
        n64cart_flash_do_cmd(N64CART_FLASH_CMD_RDSR, 0, &status, 1);
    } while (status & 1);
}

int
n64cart_flash_sync(void)
{
    n64cart_flash_access_lock();
    n64cart_flash_wait_ready();
    n64cart_flash_access_unlock();
    return 0;
}

static void
n64cart_flash_put_cmd_addr(unsigned cmd, unsigned addr)
{
    int i;

    n64cart_flash_cs_force(0);
    n64cart_flash_write_reg(N64CART_SSI_DR0, cmd);
    for (i = 0; i < 4; ++i) {
        n64cart_flash_write_reg(N64CART_SSI_DR0, addr >> 24);
        addr <<= 8;
    }
}

static void
n64cart_flash_read(unsigned addr, unsigned char *buffer, unsigned len)
{
    n64cart_flash_put_cmd_addr(N64CART_FLASH_CMD_READ, addr);
    n64cart_flash_write_reg(N64CART_SSI_DR0, 0);
    n64cart_flash_put_get(0, buffer, len,
        N64CART_FLASH_CMD_ADDR_LEN + N64CART_FLASH_CMD_DUMMY_LEN);
}

static void
n64cart_flash_read_cache_invalidate(void)
{
    n64cart_flash_read_cache_valid = 0;
    n64cart_flash_read_cache_base = 0;
    n64cart_flash_read_cache_len = 0;
}

static int
n64cart_flash_read_cache_contains(unsigned offset, unsigned size)
{
    unsigned end;
    unsigned cache_end;

    if (!n64cart_flash_read_cache_valid)
        return 0;
    if (offset < n64cart_flash_read_cache_base)
        return 0;
    end = offset + size;
    cache_end = n64cart_flash_read_cache_base + n64cart_flash_read_cache_len;
    if (end < offset)
        return 0;
    return end <= cache_end;
}

static unsigned
n64cart_flash_read_cache_size(const struct n64cart_flash_info *info,
    unsigned base)
{
    unsigned size = N64CART_FLASH_READAHEAD_SIZE;

    if (base >= info->rom_size)
        return 0;
    if (size > info->rom_size - base)
        size = info->rom_size - base;
    return size;
}

static int
n64cart_flash_read_cached(const struct n64cart_flash_info *info,
    unsigned offset, void *buffer, unsigned size)
{
    unsigned base;
    unsigned len;

    if (n64cart_flash_read_cache_contains(offset, size)) {
        bcopy((caddr_t)&n64cart_flash_read_cache[
            offset - n64cart_flash_read_cache_base], (caddr_t)buffer, size);
        return 0;
    }

    base = offset & ~(N64CART_FLASH_SECTOR - 1);
    len = n64cart_flash_read_cache_size(info, base);
    if (len == 0 || offset + size > base + len)
        return EINVAL;

    n64cart_flash_access_lock();
    n64cart_flash_read(base, n64cart_flash_read_cache, len);
    n64cart_flash_access_unlock();

    n64cart_flash_read_cache_base = base;
    n64cart_flash_read_cache_len = len;
    n64cart_flash_read_cache_valid = 1;
    bcopy((caddr_t)&n64cart_flash_read_cache[offset - base],
        (caddr_t)buffer, size);
    return 0;
}

static void
n64cart_flash_write_sector(unsigned addr, const unsigned char *buffer)
{
    unsigned offset;

    for (offset = 0; offset < N64CART_FLASH_SECTOR;
         offset += N64CART_FLASH_PAGE) {
        n64cart_flash_do_cmd(N64CART_FLASH_CMD_WREN, 0, 0, 0);
        n64cart_flash_put_cmd_addr(N64CART_FLASH_CMD_PROGRAM,
            addr + offset);
        n64cart_flash_put_get(&buffer[offset], 0, N64CART_FLASH_PAGE,
            N64CART_FLASH_CMD_ADDR_LEN);
        n64cart_flash_wait_ready();
    }
}

static void
n64cart_flash_erase_sector(unsigned addr)
{
    n64cart_flash_do_cmd(N64CART_FLASH_CMD_WREN, 0, 0, 0);
    n64cart_flash_put_cmd_addr(N64CART_FLASH_CMD_ERASE4K, addr);
    n64cart_flash_put_get(0, 0, 0, N64CART_FLASH_CMD_ADDR_LEN);
    n64cart_flash_wait_ready();
}

static unsigned
n64cart_flash_fw_size(void)
{
    return n64cart_flash_read_reg(N64CART_FW_SIZE);
}

static int
n64cart_flash_probe_info(struct n64cart_flash_info *info)
{
    unsigned char jedec[4];
    unsigned mf;
    unsigned id;
    unsigned i;

    bzero(info, sizeof(*info));

    n64cart_flash_access_lock();
    n64cart_flash_do_cmd(N64CART_FLASH_CMD_JEDEC, 0, jedec, sizeof(jedec));
    info->fw_size = n64cart_flash_fw_size();
    n64cart_flash_access_unlock();

    mf = jedec[0];
    id = ((unsigned)jedec[1] << 8) | jedec[2];
    info->jedec_id = (mf << 16) | id;
    info->sector_size = N64CART_FLASH_SECTOR;
    info->romfs_offset = (info->fw_size + 0x7fffu) & ~0x7fffu;

    for (i = 0; i < sizeof(n64cart_flash_chips) /
        sizeof(n64cart_flash_chips[0]); ++i) {
        if (n64cart_flash_chips[i].mf == mf &&
            n64cart_flash_chips[i].id == id) {
            info->rom_size = n64cart_flash_chips[i].mbytes *
                1024u * 1024u;
            return 0;
        }
    }
    return ENODEV;
}

static int
n64cart_flash_info(struct n64cart_flash_info *info)
{
    struct n64cart_flash_info probed;
    int error;

    if (n64cart_flash_info_cached) {
        bcopy(&n64cart_flash_cached_info, info, sizeof(*info));
        return 0;
    }

    error = n64cart_flash_probe_info(&probed);
    if (error != 0)
        return error;

    bcopy(&probed, &n64cart_flash_cached_info, sizeof(probed));
    n64cart_flash_info_cached = 1;
    bcopy(&probed, info, sizeof(*info));
    return 0;
}

static int
n64cart_flash_check_range(const struct n64cart_flash_info *info,
    unsigned offset, unsigned size, const void *buffer)
{
    if (info->rom_size == 0)
        return ENODEV;
    if (size == 0 || size > N64CART_FLASH_MAX_TRANSFER)
        return EINVAL;
    if (buffer == 0)
        return EFAULT;
    if (offset > info->rom_size || size > info->rom_size - offset)
        return EINVAL;
    return 0;
}

static int
n64cart_flash_check_io(const struct n64cart_flash_info *info,
    const struct n64cart_flash_io *io)
{
    return n64cart_flash_check_range(info, io->offset, io->size,
        io->buffer);
}

static int
n64cart_flash_check_write_range(const struct n64cart_flash_info *info,
    unsigned offset, unsigned size, const void *buffer)
{
    int error;

#if !N64CART_FLASH_WRITE_ENABLE
    (void)info;
    (void)offset;
    (void)size;
    (void)buffer;
    return EROFS;
#endif
    error = n64cart_flash_check_range(info, offset, size, buffer);
    if (error != 0)
        return error;
    if (offset < info->romfs_offset)
        return EROFS;
    return 0;
}

static int
n64cart_flash_check_erase_range(const struct n64cart_flash_info *info,
    unsigned offset)
{
#if !N64CART_FLASH_WRITE_ENABLE
    (void)info;
    (void)offset;
    return EROFS;
#endif
    if (info->rom_size == 0)
        return ENODEV;
    if ((offset & (N64CART_FLASH_SECTOR - 1)) != 0 ||
        offset >= info->rom_size)
        return EINVAL;
    if (offset < info->romfs_offset)
        return EROFS;
    return 0;
}

int
n64cart_flash_getinfo(struct n64cart_flash_info *info)
{
    return n64cart_flash_info(info);
}

int
n64cart_flash_read_raw(unsigned offset, void *buffer, unsigned size)
{
    struct n64cart_flash_info info;
    int error;

    error = n64cart_flash_info(&info);
    if (error != 0)
        return error;
    error = n64cart_flash_check_range(&info, offset, size, buffer);
    if (error != 0)
        return error;

    return n64cart_flash_read_cached(&info, offset, buffer, size);
}

int
n64cart_flash_write_sector_raw(unsigned offset, const void *buffer)
{
    struct n64cart_flash_info info;
    int error;

    error = n64cart_flash_info(&info);
    if (error != 0)
        return error;
    error = n64cart_flash_check_write_range(&info, offset, N64CART_FLASH_SECTOR,
        buffer);
    if (error != 0)
        return error;
    if ((offset & (N64CART_FLASH_SECTOR - 1)) != 0)
        return EINVAL;

    n64cart_flash_read_cache_invalidate();
    n64cart_flash_access_lock();
    n64cart_flash_write_sector(offset, buffer);
    n64cart_flash_access_unlock();
    return 0;
}

int
n64cart_flash_erase_sector_raw(unsigned offset)
{
    struct n64cart_flash_info info;
    int error;

    error = n64cart_flash_info(&info);
    if (error != 0)
        return error;
    error = n64cart_flash_check_erase_range(&info, offset);
    if (error != 0)
        return error;

    n64cart_flash_read_cache_invalidate();
    n64cart_flash_access_lock();
    n64cart_flash_erase_sector(offset);
    n64cart_flash_access_unlock();
    return 0;
}

int
n64cart_flash_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    if (minor(dev) != 0)
        return ENXIO;
    return 0;
}

int
n64cart_flash_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    if (minor(dev) != 0)
        return ENXIO;
    return 0;
}

int
n64cart_flash_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct n64cart_flash_info info;
    struct n64cart_flash_io io;
    unsigned offset;
    int error;

    (void)flag;
    if (minor(dev) != 0)
        return ENXIO;

    error = n64cart_flash_getinfo(&info);
    if (cmd == N64CARTFLASHIOC_GETINFO) {
        error = copyout((caddr_t)&info, data, sizeof(info));
        return error;
    }
    if (error != 0)
        return error;

    switch (cmd) {
    case N64CARTFLASHIOC_READ:
        error = copyin(data, (caddr_t)&io, sizeof(io));
        if (error != 0)
            return error;
        error = n64cart_flash_check_io(&info, &io);
        if (error != 0)
            return error;
        error = n64cart_flash_read_raw(io.offset, n64cart_flash_buf,
            io.size);
        if (error != 0)
            return error;
        return copyout((caddr_t)n64cart_flash_buf, io.buffer, io.size);

    case N64CARTFLASHIOC_WRITE:
        error = copyin(data, (caddr_t)&io, sizeof(io));
        if (error != 0)
            return error;
        error = n64cart_flash_check_write_range(&info, io.offset,
            io.size, io.buffer);
        if (error != 0)
            return error;
        if ((io.offset & (N64CART_FLASH_SECTOR - 1)) != 0 ||
            io.size != N64CART_FLASH_SECTOR)
            return EINVAL;
        error = copyin(io.buffer, (caddr_t)n64cart_flash_buf, io.size);
        if (error != 0)
            return error;
        return n64cart_flash_write_sector_raw(io.offset, n64cart_flash_buf);

    case N64CARTFLASHIOC_ERASE:
        error = copyin(data, (caddr_t)&offset, sizeof(offset));
        if (error != 0)
            return error;
        error = n64cart_flash_check_erase_range(&info, offset);
        if (error != 0)
            return error;
        return n64cart_flash_erase_sector_raw(offset);

    default:
        return ENOTTY;
    }
}
