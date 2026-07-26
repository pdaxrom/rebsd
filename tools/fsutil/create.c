/*
 * Create new 2.xBSD filesystem.
 *
 * Copyright (C) 2006-2014 Serge Vakulenko, <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "bsdfs.h"

extern int verbose;

static void put_dirent (fs_t *fs, unsigned char *buf, unsigned offset,
    unsigned ino, unsigned reclen, const char *name)
{
    fs_dirent_t dirent;

    memset (&dirent, 0, sizeof (dirent));
    dirent.ino = ino;
    dirent.reclen = reclen;
    dirent.namlen = strlen (name);
    strncpy (dirent.name, name, sizeof (dirent.name) - 1);
    fs_dirent_pack (fs, &buf[offset], &dirent);
}

int inode_build_list (fs_t *fs)
{
    fs_inode_t inode;
    unsigned int inum, total_inodes;

    total_inodes = (fs->isize - 1) * BSDFS_INODES_PER_BLOCK;
    for (inum = 1; inum <= total_inodes; inum++) {
        if (! fs_inode_get (fs, &inode, inum))
            return 0;
        if (inode.mode == 0) {
            fs->inode [fs->ninode++] = inum;
            if (fs->ninode >= NICINOD)
                break;
        }
    }
    return 1;
}

static int create_inode1 (fs_t *fs)
{
    fs_inode_t inode;

    memset (&inode, 0, sizeof(inode));
    inode.mode = INODE_MODE_FREG;
    inode.fs = fs;
    inode.number = 1;
    if (! fs_inode_save (&inode, 1))
        return 0;
    fs->tinode--;
    return 1;
}

static int create_root_directory (fs_t *fs)
{
    fs_inode_t inode;
    unsigned char buf [BSDFS_BSIZE];
    unsigned int bno;

    memset (&inode, 0, sizeof(inode));
    inode.mode = INODE_MODE_FDIR | 0777;
    inode.fs = fs;
    inode.number = BSDFS_ROOT_INODE;
    inode.size = BSDFS_BSIZE;
    inode.flags = 0;

    inode.ctime = fsutil_now();
    inode.atime = fsutil_now();
    inode.mtime = fsutil_now();

    /* directory - put in extra links */
    memset (buf, 0, sizeof(buf));
    put_dirent (fs, buf, 0, inode.number, 12, ".");
    put_dirent (fs, buf, 12, BSDFS_ROOT_INODE, 12, "..");
    put_dirent (fs, buf, 24, BSDFS_LOSTFOUND_INODE,
        BSDFS_BSIZE - 12 - 12, "lost+found");

    if (fs->swapsz != 0) {
        put_dirent (fs, buf, 24, BSDFS_LOSTFOUND_INODE, 20,
            "lost+found");
        put_dirent (fs, buf, 44, BSDFS_SWAP_INODE,
            BSDFS_BSIZE - 12 - 12 - 20, "swap");
    }
    inode.nlink = 3;

    if (! fs_block_alloc (fs, &bno))
        return 0;
    if (! fs_write_block (fs, bno, buf))
        return 0;
    inode.addr[0] = bno;

    if (! fs_inode_save (&inode, 1))
        return 0;
    fs->tinode--;
    return 1;
}

static int create_lost_found_directory (fs_t *fs)
{
    fs_inode_t inode;
    unsigned char buf [BSDFS_BSIZE];
    unsigned int bno;

    memset (&inode, 0, sizeof(inode));
    inode.mode = INODE_MODE_FDIR | 0777;
    inode.fs = fs;
    inode.number = BSDFS_LOSTFOUND_INODE;
    inode.size = BSDFS_BSIZE;
    inode.flags = 0;

    inode.ctime = fsutil_now();
    inode.atime = fsutil_now();
    inode.mtime = fsutil_now();

    /* directory - put in extra links */
    memset (buf, 0, sizeof(buf));
    put_dirent (fs, buf, 0, inode.number, 12, ".");
    put_dirent (fs, buf, 12, BSDFS_ROOT_INODE, BSDFS_BSIZE - 12,
        "..");

    inode.nlink = 2;

    if (! fs_block_alloc (fs, &bno))
        return 0;
    if (! fs_write_block (fs, bno, buf))
        return 0;
    inode.addr[0] = bno;

    if (! fs_inode_save (&inode, 1))
        return 0;
    fs->tinode--;
    return 1;
}

static void map_block_swap (fs_inode_t *inode, unsigned lbn)
{
    unsigned char block [BSDFS_BSIZE];
    unsigned int bn, indir, newb, shift, i, j;

    /*
     * Blocks 0..NADDR-3 are direct blocks.
     */
    if (lbn < NADDR-3) {
        /* small file algorithm */
        inode->addr[lbn] = inode->fs->isize + lbn;
        return;
    }

    /*
     * Addresses NADDR-3, NADDR-2, and NADDR-1
     * have single, double, triple indirect blocks.
     * The first step is to determine
     * how many levels of indirection.
     */
    shift = 0;
    i = 1;
    bn = lbn - (NADDR-3);
    for (j=3; ; j--) {
        if (j == 0) {
            fprintf (stderr, "swap: too large size\n");
            exit (-1);
        }
        shift += NSHIFT;
        i <<= NSHIFT;
        if (bn < i)
            break;
        bn -= i;
    }

    /*
     * Fetch the first indirect block.
     */
    indir = inode->addr [NADDR-j];
    if (indir == 0) {
        if (! fs_block_alloc (inode->fs, &indir)) {
alloc_error:
            fprintf (stderr, "swap: cannot allocate indirect block\n");
            exit (-1);
        }
        if (verbose)
            printf ("swap: allocate indirect block %d (j=%d)\n", indir, j);
        memset (block, 0, BSDFS_BSIZE);
        if (! fs_write_block (inode->fs, indir, block)) {
write_error:
            fprintf (stderr, "swap: cannot write indirect block %d\n", indir);
            exit (-1);
        }
        inode->addr [NADDR-j] = indir;
    }

    /*
     * Fetch through the indirect blocks
     */
    for (; ; j++) {
        if (! fs_read_block (inode->fs, indir, block)) {
            fprintf (stderr, "swap: cannot read indirect block %d\n", indir);
            exit (-1);
        }
        shift -= NSHIFT;
        i = (bn >> shift) & NMASK;
        if (j == 3) {
            fs_put32 (inode->fs, &block[i * 4], inode->fs->isize + lbn);
            if (! fs_write_block (inode->fs, indir, block))
                goto write_error;
            return;
        }
        if (fs_get32 (inode->fs, &block[i * 4]) != 0) {
               indir = fs_get32 (inode->fs, &block[i * 4]);
               continue;
        }
        /* Allocate new indirect block. */
        if (! fs_block_alloc (inode->fs, &newb))
            goto alloc_error;
        if (verbose)
            printf ("swap: allocate new block %d (j=%d)\n", newb, j);
        fs_put32 (inode->fs, &block[i * 4], newb);
        if (! fs_write_block (inode->fs, indir, block))
            goto write_error;
        memset (block, 0, BSDFS_BSIZE);
        if (! fs_write_block (inode->fs, newb, block)) {
            fprintf (stderr, "swap: cannot write block %d\n", newb);
            exit (-1);
        }
        indir = newb;
    }
}

static int create_swap_file (fs_t *fs)
{
    fs_inode_t inode;
    unsigned lbn;

    memset (&inode, 0, sizeof(inode));
    inode.mode = INODE_MODE_FREG | 0400;
    inode.fs = fs;
    inode.number = BSDFS_SWAP_INODE;
    inode.size = fs->swapsz * BSDFS_BSIZE;
    inode.flags = /*SYS_IMMUTABLE |*/ USER_IMMUTABLE | USER_NODUMP;
    inode.nlink = 1;
    inode.dirty = 1;

    inode.ctime = fsutil_now();
    inode.atime = fsutil_now();
    inode.mtime = fsutil_now();

    for (lbn=0; lbn<fs->swapsz; lbn++)
        map_block_swap (&inode, lbn);

    if (! fs_inode_save (&inode, 0)) {
        fprintf (stderr, "swap: cannot save file inode\n");
        return 0;
    }
    return 1;
}

int fs_create (fs_t *fs, const char *filename, int kbytes,
    unsigned swap_kbytes, int big_endian)
{
    int n;
    unsigned char buf [BSDFS_BSIZE];
    off_t bytes, offset;

    memset (fs, 0, sizeof (*fs));
    fs->filename = filename;
    fs->seek = 0;
    fs->big_endian = big_endian;

    if (kbytes < 0) {
        fs->fd = open (fs->filename, O_RDWR);
        if (fs->fd < 0)
            return 0;

        /* Get size and offset from partition table. */
        if (! fs_set_partition (fs, -kbytes))
            return 0;
        kbytes = fs->part_nsectors / 2;
    } else {
        fs->fd = open (fs->filename, O_CREAT | O_RDWR, 0666);
        if (fs->fd < 0)
            return 0;
    }
    fs->writable = 1;

    /* get total disk size
     * and inode block size */
    bytes = (off_t) kbytes * 1024ULL;
    fs->fsize = bytes / BSDFS_BSIZE;
    fs->isize = 1 + (fs->fsize / 16 + BSDFS_INODES_PER_BLOCK - 1) /
        BSDFS_INODES_PER_BLOCK;
    if (fs->isize < 2)
        return 0;

    /* make sure the file is of proper size */
    offset = lseek (fs->fd, fs->part_offset + bytes-1, SEEK_SET);
    if (offset != fs->part_offset + bytes-1)
        return 0;
    if (write (fs->fd, "", 1) != 1) {
        perror ("write");
        return 0;
    }
    lseek (fs->fd, fs->part_offset, SEEK_SET);

    /* build a list of free blocks */
    fs->swapsz = swap_kbytes * 1024 / BSDFS_BSIZE;
    fs_block_free (fs, 0);
    for (n = fs->fsize - 1; n >= fs->isize + fs->swapsz; n--)
        if (! fs_block_free (fs, n))
            return 0;

    /* initialize inodes */
    memset (buf, 0, BSDFS_BSIZE);
    if (! fs_seek (fs, BSDFS_BSIZE))
        return 0;
    for (n=1; n < fs->isize; n++) {
        if (! fs_write (fs, buf, BSDFS_BSIZE))
            return 0;
        fs->tinode += BSDFS_INODES_PER_BLOCK;
    }

    /* legacy empty inode 1 */
    if (! create_inode1 (fs))
        return 0;

    /* lost+found directory */
    if (! create_lost_found_directory (fs))
        return 0;

    /* root directory */
    if (! create_root_directory (fs))
        return 0;

    /* swap file */
    if (fs->swapsz != 0 && ! create_swap_file (fs))
        return 0;

    /* build a list of free inodes */
    if (! inode_build_list (fs))
        return 0;

    /* write out super block */
    return fs_sync (fs, 1);
}
