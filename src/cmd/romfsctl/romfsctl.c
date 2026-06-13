#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/n64cart_flash.h>

#include "romfs.h"

#define CARTFLASH_DEV "/dev/cartflash0"

static int flash_fd = -1;
static struct n64cart_flash_info flash_info;
static uint16_t *flash_map;
static uint8_t *flash_list;
static uint8_t io_buffer[N64CART_FLASH_SECTOR];

static void
usage(void)
{
    fprintf(stderr, "usage: romfsctl info\n");
    fprintf(stderr, "       romfsctl free\n");
    fprintf(stderr, "       romfsctl list [-h] [path]\n");
    fprintf(stderr, "       romfsctl cat path\n");
    fprintf(stderr, "       romfsctl write path text ...\n");
    fprintf(stderr, "       romfsctl rm path\n");
    fprintf(stderr, "       romfsctl mkdir path\n");
    fprintf(stderr, "       romfsctl rmdir path\n");
    fprintf(stderr, "       romfsctl rename oldpath newpath\n");
    exit(1);
}

static void
die_errno(const char *what)
{
    fprintf(stderr, "romfsctl: %s: %s\n", what, strerror(errno));
    exit(1);
}

static void
die_romfs(const char *what, uint32_t err)
{
    fprintf(stderr, "romfsctl: %s: %s\n", what, romfs_strerror(err));
    exit(1);
}

static void
open_flash(void)
{
    flash_fd = open(CARTFLASH_DEV, O_RDWR);
    if (flash_fd < 0)
        die_errno(CARTFLASH_DEV);
    if (ioctl(flash_fd, N64CARTFLASHIOC_GETINFO, &flash_info) < 0)
        die_errno("get flash info");
}

bool
romfs_flash_sector_read(uint32_t offset, uint8_t *buffer, uint32_t need)
{
    struct n64cart_flash_io io;

    io.offset = offset;
    io.size = need;
    io.buffer = (char *)buffer;
    return ioctl(flash_fd, N64CARTFLASHIOC_READ, &io) == 0;
}

bool
romfs_flash_sector_write(uint32_t offset, uint8_t *buffer)
{
    struct n64cart_flash_io io;

    io.offset = offset;
    io.size = N64CART_FLASH_SECTOR;
    io.buffer = (char *)buffer;
    return ioctl(flash_fd, N64CARTFLASHIOC_WRITE, &io) == 0;
}

bool
romfs_flash_sector_erase(uint32_t offset)
{
    unsigned sector = offset;

    return ioctl(flash_fd, N64CARTFLASHIOC_ERASE, &sector) == 0;
}

static void
load_romfs(void)
{
    uint32_t map_size;
    uint32_t list_size;

    open_flash();
    if (flash_info.rom_size == 0) {
        fprintf(stderr, "romfsctl: unknown flash JEDEC 0x%06x\n",
            flash_info.jedec_id);
        exit(1);
    }
    romfs_get_buffers_sizes(flash_info.rom_size, &map_size, &list_size);
    flash_map = malloc(map_size);
    flash_list = malloc(list_size);
    if (flash_map == NULL || flash_list == NULL) {
        fprintf(stderr, "romfsctl: no memory for romfs tables\n");
        exit(1);
    }
    if (!romfs_start(flash_info.fw_size, flash_info.rom_size,
        flash_map, flash_list)) {
        fprintf(stderr, "romfsctl: cannot start romfs\n");
        exit(1);
    }
}

static void
cmd_info(void)
{
    open_flash();
    printf("jedec=0x%06x\n", flash_info.jedec_id);
    if (flash_info.rom_size == 0)
        printf("rom_size=0 (unknown)\n");
    else
        printf("rom_size=%u\n", flash_info.rom_size);
    printf("fw_size=%u\n", flash_info.fw_size);
    printf("romfs_offset=%u\n", flash_info.romfs_offset);
    printf("sector_size=%u\n", flash_info.sector_size);
}

static void
cmd_free(void)
{
    load_romfs();
    printf("%u\n", romfs_free());
}

static const char *
human_readable_size(unsigned bytes, char *buf, size_t bufsize)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit_index = 0;
    unsigned denom = 1;
    unsigned whole;
    unsigned frac;
    unsigned rem;

    while (bytes >= denom * 1024 && unit_index < 2) {
        denom *= 1024;
        unit_index++;
    }

    whole = bytes / denom;
    rem = bytes % denom;
    frac = denom == 1 ? 0 : (rem * 100 + denom / 2) / denom;
    if (frac >= 100) {
        whole++;
        frac = 0;
    }

    snprintf(buf, bufsize, "%u.%02u %s", whole, frac, units[unit_index]);
    return buf;
}

static int
segment_equal(const char *name, const char *segment, size_t len)
{
    return strlen(name) == len && strncmp(name, segment, len) == 0;
}

static uint32_t
open_child_dir_by_iterator(const romfs_dir *parent, const char *name,
    size_t len, romfs_dir *dir)
{
    romfs_file entry;
    uint32_t err;

    if (len == 0 || len >= ROMFS_MAX_NAME_LEN)
        return ROMFS_ERR_FILE_DATA_TOO_BIG;

    bzero(&entry, sizeof(entry));
    err = romfs_list_dir(&entry, true, parent, true);
    while (err == ROMFS_NOERR) {
        if (entry.entry.attr.names.type == ROMFS_TYPE_DIR &&
            segment_equal(entry.entry.name, name, len)) {
            dir->id = entry.entry.attr.names.current;
            dir->entry_index = entry.nentry - 1;
            return ROMFS_NOERR;
        }
        err = romfs_list_dir(&entry, false, parent, true);
    }

    if (err == ROMFS_ERR_NO_FREE_ENTRIES)
        return ROMFS_ERR_NO_ENTRY;
    return err;
}

static uint32_t
open_file_by_iterator(const romfs_dir *parent, const char *name, size_t len,
    romfs_file *file, uint8_t *buffer)
{
    romfs_file entry;
    uint32_t err;

    if (len == 0 || len >= ROMFS_MAX_NAME_LEN)
        return ROMFS_ERR_FILE_DATA_TOO_BIG;

    bzero(&entry, sizeof(entry));
    err = romfs_list_dir(&entry, true, parent, true);
    while (err == ROMFS_NOERR) {
        if (entry.entry.attr.names.type != ROMFS_TYPE_DIR &&
            segment_equal(entry.entry.name, name, len)) {
            *file = entry;
            file->op = ROMFS_OP_READ;
            file->nentry = entry.nentry - 1;
            file->pos = file->entry.start;
            file->offset = 0;
            file->read_offset = 0;
            file->err = ROMFS_NOERR;
            file->io_buffer = buffer;
            file->parent_dir_id = parent->id;
            file->dir_id = 0;
            file->buffer_base = 0xffffffffu;
            file->write_offset = 0;
            file->buffer_from_flash = false;
            file->buffer_dirty = false;
            return ROMFS_NOERR;
        }
        err = romfs_list_dir(&entry, false, parent, true);
    }

    if (err == ROMFS_ERR_NO_FREE_ENTRIES)
        return ROMFS_ERR_NO_ENTRY;
    return err;
}

static uint32_t
open_dir_by_iterator(const char *path, romfs_dir *dir)
{
    const char *p;
    romfs_dir cur;
    uint32_t err;

    if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0)
        return romfs_dir_root(dir);

    err = romfs_dir_root(&cur);
    if (err != ROMFS_NOERR)
        return err;

    p = path;
    while (*p == '/')
        p++;

    while (*p != '\0') {
        const char *start = p;
        size_t len;

        while (*p != '\0' && *p != '/')
            p++;
        len = (size_t)(p - start);
        while (*p == '/')
            p++;
        if (len == 0)
            continue;
        if (len >= ROMFS_MAX_NAME_LEN)
            return ROMFS_ERR_FILE_DATA_TOO_BIG;

        err = open_child_dir_by_iterator(&cur, start, len, &cur);
        if (err != ROMFS_NOERR)
            return err;
    }

    *dir = cur;
    return ROMFS_NOERR;
}

static uint32_t
resolve_parent_by_iterator(const char *path, int create_dirs, romfs_dir *parent,
    char *leaf, size_t leaf_size)
{
    const char *p;
    romfs_dir cur;
    uint32_t err;

    if (path == NULL || leaf == NULL || leaf_size == 0)
        return ROMFS_ERR_DIR_INVALID;

    err = romfs_dir_root(&cur);
    if (err != ROMFS_NOERR)
        return err;

    p = path;
    while (*p == '/')
        p++;

    for (;;) {
        char segment[ROMFS_MAX_NAME_LEN];
        size_t len;
        size_t i;
        int last;
        romfs_dir next;

        while (*p == '/')
            p++;
        if (*p == '\0')
            return ROMFS_ERR_DIR_INVALID;

        len = 0;
        while (*p != '\0' && *p != '/') {
            if (len + 1 >= ROMFS_MAX_NAME_LEN)
                return ROMFS_ERR_FILE_DATA_TOO_BIG;
            segment[len++] = *p++;
        }
        segment[len] = '\0';

        if (len == 0)
            continue;
        if ((len == 1 && segment[0] == '.') ||
            (len == 2 && segment[0] == '.' && segment[1] == '.'))
            return ROMFS_ERR_DIR_INVALID;

        while (*p == '/')
            p++;
        last = (*p == '\0');

        if (len >= leaf_size || len >= ROMFS_MAX_NAME_LEN)
            return ROMFS_ERR_FILE_DATA_TOO_BIG;

        if (last) {
            for (i = 0; i < len; i++)
                leaf[i] = segment[i];
            leaf[len] = '\0';
            *parent = cur;
            return ROMFS_NOERR;
        }

        err = open_child_dir_by_iterator(&cur, segment, len, &next);
        if (err == ROMFS_ERR_NO_ENTRY && create_dirs) {
            err = romfs_dir_create(&cur, segment, &next);
        }
        if (err != ROMFS_NOERR)
            return err;
        cur = next;
    }
}

static void
cmd_list(const char *path, int conv)
{
    romfs_dir dir;
    romfs_file entry;
    uint32_t err;
    unsigned free_bytes;
    char num_buf[128];

    load_romfs();
    err = open_dir_by_iterator(path, &dir);
    if (err != ROMFS_NOERR) {
        die_romfs(path ? path : "/", err);
    }

    bzero(&entry, sizeof(entry));
    printf("\n");
    err = romfs_list_dir(&entry, true, &dir, true);
    if (err == ROMFS_ERR_NO_FREE_ENTRIES) {
        printf("(empty)\n");
    } else if (err != ROMFS_NOERR) {
        die_romfs("list", err);
    } else {
        do {
            int is_dir = (entry.entry.attr.names.type == ROMFS_TYPE_DIR);
            const char *size_txt = conv ?
                human_readable_size(entry.entry.size, num_buf,
                    sizeof(num_buf)) : NULL;
            if (conv) {
                printf("%02X %03X %10s %s%s\n",
                    entry.entry.attr.names.mode,
                    entry.entry.attr.names.type,
                    is_dir ? "-" : size_txt,
                    entry.entry.name,
                    is_dir ? "/" : "");
            } else {
                printf("%02X %03X %10u %s%s\n",
                    entry.entry.attr.names.mode,
                    entry.entry.attr.names.type,
                    is_dir ? 0u : entry.entry.size,
                    entry.entry.name,
                    is_dir ? "/" : "");
            }
        } while (romfs_list_dir(&entry, false, &dir, true) == ROMFS_NOERR);
    }

    free_bytes = romfs_free();
    printf("\nFree %u bytes (%s)\n", free_bytes,
        human_readable_size(free_bytes, num_buf, sizeof(num_buf)));
}

static void
cmd_cat(const char *path)
{
    romfs_file file;
    romfs_dir parent;
    char leaf[ROMFS_MAX_NAME_LEN];
    uint32_t err;
    uint32_t got;

    load_romfs();
    bzero(&file, sizeof(file));
    err = resolve_parent_by_iterator(path, 0, &parent, leaf, sizeof(leaf));
    if (err == ROMFS_NOERR)
        err = open_file_by_iterator(&parent, leaf, strlen(leaf), &file,
            io_buffer);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);

    for (;;) {
        got = romfs_read_file(io_buffer, sizeof(io_buffer), &file);
        if (got != 0)
            fwrite(io_buffer, 1, got, stdout);
        if (file.err == ROMFS_ERR_EOF)
            break;
        if (file.err != ROMFS_NOERR)
            die_romfs(path, file.err);
    }
}

static char *
join_text(int argc, char **argv)
{
    size_t len = 0;
    char *out;
    char *p;
    int i;

    for (i = 0; i < argc; ++i)
        len += strlen(argv[i]) + (i != 0);
    out = malloc(len + 1);
    if (out == NULL) {
        fprintf(stderr, "romfsctl: no memory\n");
        exit(1);
    }
    p = out;
    for (i = 0; i < argc; ++i) {
        if (i != 0)
            *p++ = ' ';
        strcpy(p, argv[i]);
        p += strlen(argv[i]);
    }
    *p = '\0';
    return out;
}

static void
cmd_write(const char *path, int argc, char **argv)
{
    romfs_file file;
    romfs_dir parent;
    char leaf[ROMFS_MAX_NAME_LEN];
    char *text;
    size_t len;
    uint32_t err;

    load_romfs();
    text = join_text(argc, argv);
    len = strlen(text);

    err = resolve_parent_by_iterator(path, 1, &parent, leaf, sizeof(leaf));
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
    err = romfs_delete_in_dir(&parent, leaf);
    if (err != ROMFS_NOERR && err != ROMFS_ERR_NO_ENTRY)
        die_romfs(path, err);
    bzero(&file, sizeof(file));
    err = romfs_create_file_in_dir(&parent, leaf, &file, ROMFS_MODE_READWRITE,
        ROMFS_TYPE_MISC, io_buffer);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
    if (romfs_write_file(text, len, &file) != len)
        die_romfs(path, file.err);
    err = romfs_close_file(&file);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
    free(text);
}

static void
cmd_rm(const char *path)
{
    romfs_dir parent;
    char leaf[ROMFS_MAX_NAME_LEN];
    uint32_t err;

    load_romfs();
    err = resolve_parent_by_iterator(path, 0, &parent, leaf, sizeof(leaf));
    if (err == ROMFS_NOERR)
        err = romfs_delete_in_dir(&parent, leaf);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
}

static void
cmd_mkdir(const char *path)
{
    romfs_dir parent;
    char leaf[ROMFS_MAX_NAME_LEN];
    uint32_t err;

    load_romfs();
    err = resolve_parent_by_iterator(path, 1, &parent, leaf, sizeof(leaf));
    if (err == ROMFS_NOERR)
        err = romfs_dir_create(&parent, leaf, NULL);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
}

static void
cmd_rmdir(const char *path)
{
    romfs_dir dir;
    uint32_t err;

    load_romfs();
    err = open_dir_by_iterator(path, &dir);
    if (err == ROMFS_NOERR)
        err = romfs_dir_remove(&dir);
    if (err != ROMFS_NOERR)
        die_romfs(path, err);
}

static void
cmd_rename(const char *src, const char *dst)
{
    romfs_dir src_parent;
    romfs_dir dst_parent;
    char src_leaf[ROMFS_MAX_NAME_LEN];
    char dst_leaf[ROMFS_MAX_NAME_LEN];
    uint32_t err;

    load_romfs();
    err = resolve_parent_by_iterator(src, 0, &src_parent, src_leaf,
        sizeof(src_leaf));
    if (err == ROMFS_NOERR)
        err = resolve_parent_by_iterator(dst, 1, &dst_parent, dst_leaf,
            sizeof(dst_leaf));
    if (err == ROMFS_NOERR)
        err = romfs_rename_in_dir(&src_parent, src_leaf, &dst_parent,
            dst_leaf);
    if (err != ROMFS_NOERR)
        die_romfs(src, err);
}

int
main(int argc, char **argv)
{
    if (argc < 2)
        usage();

    if (strcmp(argv[1], "info") == 0) {
        if (argc != 2)
            usage();
        cmd_info();
    } else if (strcmp(argv[1], "free") == 0) {
        if (argc != 2)
            usage();
        cmd_free();
    } else if (strcmp(argv[1], "list") == 0) {
        int conv = 0;
        const char *path = NULL;
        int i;

        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0) {
                conv = 1;
            } else {
                if (path != NULL)
                    usage();
                path = argv[i];
            }
        }
        cmd_list(path, conv);
    } else if (strcmp(argv[1], "cat") == 0) {
        if (argc != 3)
            usage();
        cmd_cat(argv[2]);
    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4)
            usage();
        cmd_write(argv[2], argc - 3, &argv[3]);
    } else if (strcmp(argv[1], "rm") == 0) {
        if (argc != 3)
            usage();
        cmd_rm(argv[2]);
    } else if (strcmp(argv[1], "mkdir") == 0) {
        if (argc != 3)
            usage();
        cmd_mkdir(argv[2]);
    } else if (strcmp(argv[1], "rmdir") == 0) {
        if (argc != 3)
            usage();
        cmd_rmdir(argv[2]);
    } else if (strcmp(argv[1], "rename") == 0) {
        if (argc != 4)
            usage();
        cmd_rename(argv[2], argv[3]);
    } else {
        usage();
    }

    return 0;
}
