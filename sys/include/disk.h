/*
 * Ioctl definitions for skeleton driver.
 *
 * Copyright (C) 2015 Serge Vakulenko
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
#ifndef _SYS_DISK_H_
#define _SYS_DISK_H_

/*
 * IBM PC compatible partition table.
 */
#define MAXPARTITIONS   4

struct diskpart {                   /* the partition table */
    u_char      dp_status;          /* active (bootable) flag */
#define DP_ACTIVE 0x80
    u_char      dp_start_chs[3];    /* ignored */
    u_char      dp_type;            /* type of partition */
    u_char      dp_end_chs[3];      /* ignored */
    u_int       dp_offset;          /* starting sector */
    u_int       dp_nsectors;        /* number of sectors in partition */
};

/*
 * Partition types.
 */
#define PTYPE_UNUSED    0           /* unused */
#define PTYPE_BSDFFS    0xb7        /* 4.2BSD fast file system */
#define PTYPE_SWAP      0xb8        /* swap */

/*
 * Transport-independent partition information.  The legacy diskpart ABI is
 * retained above for old MBR-only programs; new code must use diskpart64.
 */
#define DISK_SCHEME_NONE 0
#define DISK_SCHEME_MBR  1
#define DISK_SCHEME_GPT  2

#ifndef _DISK_SECTOR_T_DEFINED
#define _DISK_SECTOR_T_DEFINED
typedef unsigned long long disk_sector_t;
#endif

struct diskpart64 {
    u_int       dp_scheme;
    u_char      dp_status;          /* MBR active flag, zero for GPT */
    u_char      dp_type;            /* MBR type byte, zero for GPT */
    u_char      dp_reserved[2];
    disk_sector_t dp_offset;
    disk_sector_t dp_nsectors;
    disk_sector_t dp_attributes;    /* GPT attributes, zero for MBR */
    u_char      dp_type_guid[16];
    u_char      dp_unique_guid[16];
};

/*
 * Disk-specific ioctls.
 */
#define DIOCGETMEDIASIZE _IOR('d', 1, int)              /* get size in kbytes */
#define DIOCREINIT       _IO ('d', 2)                   /* re-initialize device */
#define DIOCGETPART      _IOR('d', 3, struct diskpart)  /* get partition */
#define DIOCGETSECTORS   _IOR('d', 4, unsigned)         /* exact 512-byte count */
#define DIOCFLUSH        _IO ('d', 5)                   /* flush media cache */
#define DIOCGETSECTORS64 _IOR('d', 6, disk_sector_t)    /* 64-bit sector count */
#define DIOCGETPART64    _IOR('d', 7, struct diskpart64)/* 64-bit partition */
#define DIOCGETSCHEME    _IOR('d', 8, unsigned)         /* DISK_SCHEME_* */

#endif /* _SYS_DISK_H_ */
