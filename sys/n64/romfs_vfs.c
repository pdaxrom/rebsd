#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/dir.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/kernel.h>

#include <machine/n64cart_flash.h>

#include "../../src/cmd/romfsctl/romfs.h"

#define N64ROMFS_MAX_ROM_SIZE   (128u * 1024u * 1024u)
#define N64ROMFS_MAX_MAP_SIZE   \
    (((N64ROMFS_MAX_ROM_SIZE / ROMFS_FLASH_SECTOR) * sizeof(uint16_t) + \
    (ROMFS_FLASH_SECTOR - 1)) & ~(ROMFS_FLASH_SECTOR - 1))
#define N64ROMFS_MAX_LIST_SIZE  \
    (((N64ROMFS_MAX_ROM_SIZE / ROMFS_MB) * sizeof(romfs_entry) + \
    (ROMFS_FLASH_SECTOR - 1)) & ~(ROMFS_FLASH_SECTOR - 1))

#define N64ROMFS_INO_BASE       16
#define N64ROMFS_ENTRY_INO(n)   ((ino_t)((n) + N64ROMFS_INO_BASE))
#define N64ROMFS_INO_ENTRY(i)   ((uint32_t)((i) - N64ROMFS_INO_BASE))

struct n64romfs_mount {
    struct n64cart_flash_info info;
    uint32_t map_size;
    uint32_t list_size;
    uint32_t files;
    uint32_t free_bytes;
};

static struct n64romfs_mount n64romfs_mount_state;
static uint16_t n64romfs_flash_map[N64ROMFS_MAX_MAP_SIZE / sizeof(uint16_t)];
static uint8_t n64romfs_flash_list[N64ROMFS_MAX_LIST_SIZE];
static uint8_t n64romfs_io_buffer[ROMFS_FLASH_SECTOR];
static uint8_t n64romfs_write_buffer[512];
static uint8_t n64romfs_dir_buffer[DIRBLKSIZ];

bool
romfs_flash_sector_read(uint32_t offset, uint8_t *buffer, uint32_t need)
{
    return n64cart_flash_read_raw(offset, buffer, need) == 0;
}

bool
romfs_flash_sector_write(uint32_t offset, uint8_t *buffer)
{
    return n64cart_flash_write_sector_raw(offset, buffer) == 0;
}

bool
romfs_flash_sector_erase(uint32_t offset)
{
    return n64cart_flash_erase_sector_raw(offset) == 0;
}

static int
n64romfs_error(uint32_t err)
{
    switch (err) {
    case ROMFS_NOERR:
        return 0;
    case ROMFS_ERR_NO_ENTRY:
        return ENOENT;
    case ROMFS_ERR_NO_FREE_ENTRIES:
        return ENOSPC;
    case ROMFS_ERR_NO_SPACE:
        return ENOSPC;
    case ROMFS_ERR_FILE_EXISTS:
        return EEXIST;
    case ROMFS_ERR_FILE_DATA_TOO_BIG:
        return ENAMETOOLONG;
    case ROMFS_ERR_BUFFER_TOO_SMALL:
    case ROMFS_ERR_NO_IO_BUFFER:
        return ENOMEM;
    case ROMFS_ERR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ROMFS_ERR_DIR_LIMIT:
    case ROMFS_ERR_DIR_INVALID:
        return ENOTDIR;
    default:
        return EIO;
    }
}

static int
n64romfs_entry_by_index(uint32_t index, romfs_file *file)
{
    uint32_t err;

    bzero(file, sizeof(*file));
    err = romfs_list(file, true);
    while (err == ROMFS_NOERR) {
        if (file->nentry != 0 && file->nentry - 1 == index)
            return 0;
        err = romfs_list(file, false);
    }
    return ENOENT;
}

static void
n64romfs_refresh_counts(struct mount *mp)
{
    struct n64romfs_mount *rmp = (struct n64romfs_mount *)mp->m_data;
    romfs_file file;
    uint32_t err;

    if (rmp == 0)
        return;
    rmp->free_bytes = romfs_free();
    rmp->files = 0;
    bzero(&file, sizeof(file));
    err = romfs_list(&file, true);
    while (err == ROMFS_NOERR) {
        rmp->files++;
        err = romfs_list(&file, false);
    }
}

static int
n64romfs_writable(struct mount *mp)
{
    if (mp->m_filsys.fs_ronly || (mp->m_flags & MNT_RDONLY))
        return EROFS;
    return 0;
}

static int
n64romfs_dir_by_inode(ino_t ino, romfs_dir *dir)
{
    romfs_file file;
    int error;

    if (ino == ROOTINO) {
        dir->id = ROMFS_ROOT_DIR_ID;
        dir->entry_index = ROMFS_INVALID_ENTRY_ID;
        return 0;
    }
    if (ino < N64ROMFS_INO_BASE)
        return ENOENT;
    error = n64romfs_entry_by_index(N64ROMFS_INO_ENTRY(ino), &file);
    if (error)
        return error;
    if (file.entry.attr.names.type != ROMFS_TYPE_DIR)
        return ENOTDIR;
    dir->id = file.entry.attr.names.current;
    dir->entry_index = file.nentry - 1;
    return 0;
}

static int
n64romfs_dir_parent_ino(uint8_t dir_id, ino_t *ino)
{
    romfs_file file;
    uint8_t parent;
    uint32_t err;

    if (dir_id == ROMFS_ROOT_DIR_ID) {
        *ino = ROOTINO;
        return 0;
    }
    bzero(&file, sizeof(file));
    err = romfs_list(&file, true);
    while (err == ROMFS_NOERR) {
        if (file.entry.attr.names.type == ROMFS_TYPE_DIR &&
            file.entry.attr.names.current == dir_id) {
            parent = file.entry.attr.names.parent;
            if (parent == ROMFS_ROOT_DIR_ID) {
                *ino = ROOTINO;
                return 0;
            }
            break;
        }
        err = romfs_list(&file, false);
    }
    if (err != ROMFS_NOERR)
        return ENOENT;

    err = romfs_list(&file, true);
    while (err == ROMFS_NOERR) {
        if (file.entry.attr.names.type == ROMFS_TYPE_DIR &&
            file.entry.attr.names.current == parent) {
            *ino = N64ROMFS_ENTRY_INO(file.nentry - 1);
            return 0;
        }
        err = romfs_list(&file, false);
    }
    return ENOENT;
}

static void
n64romfs_emit_dirent(char *block, off_t block_base, off_t *offp, ino_t ino,
    const char *name)
{
    struct direct d;
    unsigned reclen;
    unsigned inblock;

    d.d_ino = ino;
    d.d_namlen = strlen(name);
    reclen = DIRSIZ(&d);

    inblock = *offp & (DIRBLKSIZ - 1);
    if (inblock + reclen > DIRBLKSIZ) {
        if (block != 0 && *offp >= block_base &&
            *offp < block_base + DIRBLKSIZ) {
            struct direct *f = (struct direct *)(block + inblock);
            bzero(f, sizeof(*f));
            f->d_reclen = DIRBLKSIZ - inblock;
        }
        *offp += DIRBLKSIZ - inblock;
        inblock = 0;
    }

    if (block != 0 && *offp >= block_base && *offp < block_base + DIRBLKSIZ) {
        struct direct *dp = (struct direct *)(block + inblock);
        bzero(dp, reclen);
        dp->d_ino = ino;
        dp->d_reclen = reclen;
        dp->d_namlen = d.d_namlen;
        bcopy(name, dp->d_name, d.d_namlen);
        dp->d_name[d.d_namlen] = '\0';
    }
    *offp += reclen;
}

static off_t
n64romfs_dir_build(ino_t ino, char *block, off_t block_base)
{
    romfs_dir dir;
    romfs_file entry;
    off_t off = 0;
    uint32_t err;
    ino_t parent_ino = ROOTINO;

    if (block != 0)
        bzero(block, DIRBLKSIZ);
    if (n64romfs_dir_by_inode(ino, &dir))
        return 0;
    (void)n64romfs_dir_parent_ino(dir.id, &parent_ino);

    n64romfs_emit_dirent(block, block_base, &off, ino, ".");
    n64romfs_emit_dirent(block, block_base, &off, parent_ino, "..");

    bzero(&entry, sizeof(entry));
    err = romfs_list_dir(&entry, true, &dir, true);
    while (err == ROMFS_NOERR) {
        n64romfs_emit_dirent(block, block_base, &off,
            N64ROMFS_ENTRY_INO(entry.nentry - 1), entry.entry.name);
        err = romfs_list_dir(&entry, false, &dir, true);
    }

    if (off == 0 || (off & (DIRBLKSIZ - 1)) != 0) {
        unsigned inblock = off & (DIRBLKSIZ - 1);

        if (inblock == 0)
            inblock = DIRBLKSIZ;
        else if (block != 0 && off >= block_base &&
            off < block_base + DIRBLKSIZ) {
            struct direct *f =
                (struct direct *)(block + (off & (DIRBLKSIZ - 1)));
            bzero(f, sizeof(*f));
            f->d_reclen = DIRBLKSIZ - (off & (DIRBLKSIZ - 1));
        }
        off = roundup(off, DIRBLKSIZ);
    }
    return off;
}

static int
n64romfs_load_inode(struct mount *mp, struct inode *ip)
{
    romfs_file file;
    int error;

    bzero(&ip->i_ic1, sizeof(ip->i_ic1));
    bzero(&ip->i_ic2, sizeof(ip->i_ic2));
    bzero(ip->i_addr, sizeof(ip->i_addr));
    ip->i_flags = 0;
    ip->i_uid = 0;
    ip->i_gid = 0;
    ip->i_atime = time.tv_sec;
    ip->i_mtime = time.tv_sec;
    ip->i_ctime = time.tv_sec;

    if (ip->i_number == ROOTINO) {
        ip->i_mode = IFDIR | 0777;
        ip->i_nlink = 2;
        ip->i_size = n64romfs_dir_build(ROOTINO, 0, 0);
        return 0;
    }
    if (ip->i_number < N64ROMFS_INO_BASE)
        return ENOENT;

    error = n64romfs_entry_by_index(N64ROMFS_INO_ENTRY(ip->i_number), &file);
    if (error)
        return error;
    if (file.entry.attr.names.type == ROMFS_TYPE_DIR) {
        ip->i_mode = IFDIR | 0777;
        ip->i_nlink = 2;
        ip->i_size = n64romfs_dir_build(ip->i_number, 0, 0);
    } else {
        ip->i_mode = IFREG | 0666;
        ip->i_nlink = 1;
        ip->i_size = file.entry.size;
    }
    (void)mp;
    return 0;
}

static struct buf *
n64romfs_blkatoff(struct inode *ip, off_t offset, char **res)
{
    struct buf *bp;

    bp = geteblk();
    bzero(bp->b_addr, MAXBSIZE);
    n64romfs_dir_build(ip->i_number, bp->b_addr,
        offset & ~(DIRBLKSIZ - 1));
    bp->b_resid = 0;
    bp->b_flags |= B_DONE;
    if (res != 0)
        *res = bp->b_addr + (offset & (DIRBLKSIZ - 1));
    return bp;
}

static int
n64romfs_read_dir(struct inode *ip, struct uio *uio)
{
    off_t size;
    off_t block_base;
    unsigned on;
    unsigned n;
    int error;

    size = ip->i_size;
    while (uio->uio_resid != 0 && uio->uio_offset < size) {
        block_base = uio->uio_offset & ~(DIRBLKSIZ - 1);
        on = uio->uio_offset & (DIRBLKSIZ - 1);
        n = MIN((u_int)(DIRBLKSIZ - on), uio->uio_resid);
        if (uio->uio_offset + n > size)
            n = size - uio->uio_offset;
        bzero(n64romfs_dir_buffer, sizeof(n64romfs_dir_buffer));
        n64romfs_dir_build(ip->i_number, (char *)n64romfs_dir_buffer,
            block_base);
        error = uiomove((caddr_t)n64romfs_dir_buffer + on, n, uio);
        if (error)
            return error;
    }
    return 0;
}

static int
n64romfs_read_file(struct inode *ip, struct uio *uio)
{
    romfs_file file;
    int error;
    uint32_t got;
    unsigned n;

    error = n64romfs_entry_by_index(N64ROMFS_INO_ENTRY(ip->i_number), &file);
    if (error)
        return error;
    file.op = ROMFS_OP_READ;
    file.nentry = N64ROMFS_INO_ENTRY(ip->i_number);
    file.pos = file.entry.start;
    file.offset = 0;
    file.read_offset = 0;
    file.err = ROMFS_NOERR;
    file.io_buffer = n64romfs_io_buffer;
    file.buffer_base = 0xffffffffu;
    file.buffer_from_flash = false;
    file.buffer_dirty = false;
    if (uio->uio_offset != 0) {
        error = n64romfs_error(romfs_seek_file(&file, uio->uio_offset,
            SEEK_SET));
        if (error)
            return error;
    }

    while (uio->uio_resid != 0 && uio->uio_offset < ip->i_size) {
        n = MIN((u_int)sizeof(n64romfs_io_buffer), uio->uio_resid);
        if (uio->uio_offset + n > ip->i_size)
            n = ip->i_size - uio->uio_offset;
        got = romfs_read_file(n64romfs_io_buffer, n, &file);
        if (got != 0) {
            error = uiomove((caddr_t)n64romfs_io_buffer, got, uio);
            if (error)
                return error;
        }
        if (file.err == ROMFS_ERR_EOF)
            return 0;
        if (file.err != ROMFS_NOERR)
            return n64romfs_error(file.err);
        if (got == 0)
            return 0;
    }
    return 0;
}

static int
n64romfs_open_write_inode(struct inode *ip, romfs_file *file)
{
    romfs_dir dir;
    char name[ROMFS_MAX_NAME_LEN];
    int error;

    if ((ip->i_mode & IFMT) != IFREG)
        return EISDIR;
    error = n64romfs_entry_by_index(N64ROMFS_INO_ENTRY(ip->i_number), file);
    if (error)
        return error;
    if (file->entry.attr.names.type == ROMFS_TYPE_DIR)
        return EISDIR;
    bzero(&dir, sizeof(dir));
    dir.id = file->entry.attr.names.parent;
    dir.entry_index = ROMFS_INVALID_ENTRY_ID;
    bzero(name, sizeof(name));
    bcopy(file->entry.name, name, sizeof(name) - 1);
    error = n64romfs_error(romfs_open_append_in_dir(&dir, name, file,
        ROMFS_TYPE_MISC, n64romfs_io_buffer));
    return error;
}

static int
n64romfs_write_file(struct inode *ip, struct uio *uio, int ioflag)
{
    struct mount *mp = (struct mount *)
        ((int)ip->i_fs - offsetof(struct mount, m_filsys));
    romfs_file file;
    int error;
    unsigned n;
    uint32_t written;

    error = n64romfs_writable(mp);
    if (error)
        return error;
    if (ioflag & IO_APPEND)
        uio->uio_offset = ip->i_size;
    if (uio->uio_offset < 0)
        return EINVAL;
    error = n64romfs_open_write_inode(ip, &file);
    if (error)
        return error;
    error = n64romfs_error(romfs_seek_file(&file, uio->uio_offset, SEEK_SET));
    if (error)
        return error;

    while (uio->uio_resid != 0) {
        n = MIN((u_int)sizeof(n64romfs_write_buffer), uio->uio_resid);
        error = uiomove((caddr_t)n64romfs_write_buffer, n, uio);
        if (error)
            break;
        written = romfs_write_file(n64romfs_write_buffer, n, &file);
        if (written != n) {
            error = n64romfs_error(file.err);
            if (error == 0)
                error = EIO;
            break;
        }
    }
    if (error == 0)
        error = n64romfs_error(romfs_close_file(&file));
    ip->i_size = file.entry.size;
    ip->i_flag |= IUPD|ICHG;
    n64romfs_refresh_counts(mp);
    return error;
}

static int
n64romfs_rwip(struct inode *ip, struct uio *uio, int ioflag)
{
    int type;

    (void)ioflag;
    type = ip->i_mode & IFMT;
    if (uio->uio_rw != UIO_READ)
        return n64romfs_write_file(ip, uio, ioflag);
    if (type == IFDIR)
        return n64romfs_read_dir(ip, uio);
    if (type == IFREG)
        return n64romfs_read_file(ip, uio);
    return EFTYPE;
}

static int
n64romfs_create(struct inode *pdir, struct nameidata *ndp, int mode,
    struct inode **ipp)
{
    struct mount *mp = (struct mount *)
        ((int)pdir->i_fs - offsetof(struct mount, m_filsys));
    romfs_dir dir;
    romfs_file file;
    int error;
    uint32_t err;

    *ipp = 0;
    error = n64romfs_writable(mp);
    if (error)
        return error;
    if ((mode & IFMT) == 0)
        mode |= IFREG;
    if ((mode & IFMT) != IFREG)
        return EOPNOTSUPP;
    error = n64romfs_dir_by_inode(pdir->i_number, &dir);
    if (error)
        return error;
    bzero(&file, sizeof(file));
    err = romfs_create_file_in_dir(&dir, ndp->ni_dent.d_name, &file,
        ROMFS_MODE_READWRITE, ROMFS_TYPE_MISC, n64romfs_io_buffer);
    error = n64romfs_error(err);
    if (error)
        return error;
    error = n64romfs_error(romfs_close_file(&file));
    if (error)
        return error;
    n64romfs_refresh_counts(mp);
    nchinval(pdir->i_dev);
    *ipp = iget(pdir->i_dev, pdir->i_fs, N64ROMFS_ENTRY_INO(file.nentry));
    if (*ipp == 0)
        return u.u_error ? u.u_error : EIO;
    return 0;
}

static int
n64romfs_remove(struct inode *pdir, struct inode *ip, struct nameidata *ndp)
{
    struct mount *mp = (struct mount *)
        ((int)pdir->i_fs - offsetof(struct mount, m_filsys));
    romfs_dir dir;
    int error;

    error = n64romfs_writable(mp);
    if (error)
        return error;
    if ((ip->i_mode & IFMT) == IFDIR)
        return EISDIR;
    error = n64romfs_dir_by_inode(pdir->i_number, &dir);
    if (error)
        return error;
    error = n64romfs_error(romfs_delete_in_dir(&dir, ndp->ni_dent.d_name));
    if (error)
        return error;
    ip->i_nlink = 0;
    ip->i_size = 0;
    cacheinval(ip);
    n64romfs_refresh_counts(mp);
    nchinval(pdir->i_dev);
    return 0;
}

static int
n64romfs_mkdir(struct inode *pdir, struct nameidata *ndp, int mode)
{
    struct mount *mp = (struct mount *)
        ((int)pdir->i_fs - offsetof(struct mount, m_filsys));
    romfs_dir dir;
    romfs_dir newdir;
    int error;

    (void)mode;
    error = n64romfs_writable(mp);
    if (error)
        return error;
    error = n64romfs_dir_by_inode(pdir->i_number, &dir);
    if (error)
        return error;
    error = n64romfs_error(romfs_dir_create(&dir, ndp->ni_dent.d_name,
        &newdir));
    if (error)
        return error;
    n64romfs_refresh_counts(mp);
    nchinval(pdir->i_dev);
    return 0;
}

static int
n64romfs_rmdir(struct inode *pdir, struct inode *ip, struct nameidata *ndp)
{
    struct mount *mp = (struct mount *)
        ((int)pdir->i_fs - offsetof(struct mount, m_filsys));
    romfs_dir dir;
    int error;

    error = n64romfs_writable(mp);
    if (error)
        return error;
    if ((ip->i_mode & IFMT) != IFDIR)
        return ENOTDIR;
    error = n64romfs_dir_by_inode(pdir->i_number, &dir);
    if (error)
        return error;
    error = n64romfs_error(romfs_delete_in_dir(&dir, ndp->ni_dent.d_name));
    if (error)
        return error;
    ip->i_nlink = 0;
    ip->i_size = 0;
    cacheinval(ip);
    n64romfs_refresh_counts(mp);
    nchinval(pdir->i_dev);
    return 0;
}

static int
n64romfs_rename(struct inode *from_pdir, struct inode *from_ip,
    struct nameidata *from_ndp, struct inode *to_pdir, struct inode *to_ip,
    struct nameidata *to_ndp)
{
    struct mount *mp = (struct mount *)
        ((int)from_pdir->i_fs - offsetof(struct mount, m_filsys));
    romfs_dir from_dir;
    romfs_dir to_dir;
    int error;

    error = n64romfs_writable(mp);
    if (error)
        return error;
    if (from_pdir == from_ip)
        return EINVAL;
    if (to_ip != 0)
        return EEXIST;
    error = n64romfs_dir_by_inode(from_pdir->i_number, &from_dir);
    if (error)
        return error;
    error = n64romfs_dir_by_inode(to_pdir->i_number, &to_dir);
    if (error)
        return error;
    error = n64romfs_error(romfs_rename_in_dir(&from_dir,
        from_ndp->ni_dent.d_name, &to_dir, to_ndp->ni_dent.d_name));
    if (error)
        return error;
    cacheinval(from_ip);
    n64romfs_refresh_counts(mp);
    nchinval(from_pdir->i_dev);
    return 0;
}

static int
n64romfs_truncate(struct inode *ip, u_long length, int ioflags)
{
    struct mount *mp = (struct mount *)
        ((int)ip->i_fs - offsetof(struct mount, m_filsys));
    romfs_file file;
    int error;

    (void)ioflags;
    error = n64romfs_writable(mp);
    if (error)
        return error;
    if (length > 0xffffffffu)
        return EFBIG;
    error = n64romfs_open_write_inode(ip, &file);
    if (error)
        return error;
    error = n64romfs_error(romfs_truncate_file(&file, length));
    if (error)
        return error;
    ip->i_size = file.entry.size;
    ip->i_flag |= IUPD|ICHG;
    cacheinval(ip);
    n64romfs_refresh_counts(mp);
    return 0;
}

static int
n64romfs_statfs(struct mount *mp, struct statfs *sbp)
{
    struct n64romfs_mount *rmp = (struct n64romfs_mount *)mp->m_data;
    struct statfs sfs;

    bzero(&sfs, sizeof(sfs));
    sfs.f_type = MOUNT_ROMFS;
    sfs.f_flags = mp->m_flags & MNT_VISFLAGMASK;
    sfs.f_bsize = ROMFS_FLASH_SECTOR;
    sfs.f_iosize = ROMFS_FLASH_SECTOR;
    sfs.f_blocks = rmp->info.rom_size / ROMFS_FLASH_SECTOR;
    sfs.f_bfree = rmp->free_bytes / ROMFS_FLASH_SECTOR;
    sfs.f_bavail = sfs.f_bfree;
    sfs.f_files = rmp->list_size / sizeof(romfs_entry);
    sfs.f_ffree = sfs.f_files > rmp->files ? sfs.f_files - rmp->files : 0;
    bcopy(mp->m_mnton, sfs.f_mntonname, MNAMELEN);
    bcopy(mp->m_mntfrom, sfs.f_mntfromname, MNAMELEN);
    return copyout((caddr_t)&sfs, (caddr_t)sbp, sizeof(sfs));
}

static int
n64romfs_sync(struct mount *mp)
{
    (void)mp;
    return 0;
}

static int
n64romfs_mount(struct mount *mp, dev_t dev, int flags, struct inode *ip)
{
    struct n64romfs_mount *rmp = &n64romfs_mount_state;
    romfs_file file;
    uint32_t err;
    int error;

    (void)dev;
    (void)flags;
    (void)ip;
    bzero(rmp, sizeof(*rmp));
    error = n64cart_flash_getinfo(&rmp->info);
    if (error)
        return error;
    if (rmp->info.rom_size == 0 ||
        rmp->info.rom_size > N64ROMFS_MAX_ROM_SIZE)
        return ENXIO;

    romfs_get_buffers_sizes(rmp->info.rom_size, &rmp->map_size,
        &rmp->list_size);
    if (rmp->map_size > sizeof(n64romfs_flash_map) ||
        rmp->list_size > sizeof(n64romfs_flash_list))
        return ENOMEM;
    if (!romfs_start(rmp->info.fw_size, rmp->info.rom_size,
        n64romfs_flash_map, n64romfs_flash_list))
        return EIO;

    rmp->free_bytes = romfs_free();
    bzero(&file, sizeof(file));
    err = romfs_list(&file, true);
    while (err == ROMFS_NOERR) {
        rmp->files++;
        err = romfs_list(&file, false);
    }
    mp->m_data = (caddr_t)rmp;
    mp->m_filsys.fs_ronly = (flags & MNT_RDONLY) != 0;
    mp->m_filsys.fs_flags = flags;
    return 0;
}

struct vfsops n64romfs_vfsops = {
    n64romfs_mount,
    0,
    n64romfs_load_inode,
    n64romfs_blkatoff,
    n64romfs_rwip,
    n64romfs_create,
    n64romfs_remove,
    n64romfs_mkdir,
    n64romfs_rmdir,
    n64romfs_rename,
    n64romfs_truncate,
    n64romfs_statfs,
    n64romfs_sync,
};
