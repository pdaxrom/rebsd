/*
 * Machine-dependent constants for the 32-bit i686 port.
 *
 * NBPG remains the historical ReBSD accounting unit.  Hardware VM pages
 * use VM_PAGE_SIZE (4096) from vm/vm_param.h.
 */
#ifndef _I386_MACHPARAM_H_
#define _I386_MACHPARAM_H_

#define LITTLE          1234
#define BIG             4321
#define PDP             3412
#define ENDIAN          LITTLE

#define MAXSLP          20
#define HZ              100
#define NBUF            16
#define MAXUSERS        1
#define NPROC           25
#define NINODE          64
#define NFILE           32
#define NMOUNT          8
#define NNAMECACHE      (NINODE * 11 / 10)
#define NCALL           (16 + 2 * MAXUSERS)
#define NCLIST          32
#define SMAPSIZ         NPROC

#define DEV_BSIZE       1024
#define DEV_BSHIFT      10
#define DEV_BMASK       (DEV_BSIZE - 1)
#define btod(x)         (((x) + DEV_BSIZE - 1) >> DEV_BSHIFT)

#define NBPG            1024
#define PGOFSET         (NBPG - 1)
#define CLSIZE          1
#define CLSHIFT         10
#define CLBYTES         (CLSIZE * NBPG)
#define CLOFSET         (CLBYTES - 1)
#define btoc(x)         (((x) + NBPG - 1) / NBPG)
#define ctob(x)         ((x) * NBPG)

#include <machine/layout.h>

#define USER_DATA_START I386_USER_VADDR_START
#define USER_DATA_END   I386_USER_VADDR_END

#define stacktop(siz)   (USER_DATA_END)
#define stackbas(siz)   (USER_DATA_END - (siz))

#define MAXMEM          (96 * 1024)
#define USIZE           16384
#define SSIZE           2048

#if !defined(UCB_METER) && !defined(NO_UCB_METER)
#define UCB_METER
#endif

#endif
