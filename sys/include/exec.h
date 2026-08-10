/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef _SYS_EXEC_H_
#define _SYS_EXEC_H_
#ifdef KERNEL

#define SHSIZE      64
#define SHPATHLEN   64
#define STRLEN      32
#include "exec_elf.h"

struct vmspace;

#define NO_ADDR     ((caddr_t)(~0U)) /* Indicates addr. not yet filled in */

struct memsect {
    caddr_t vaddr;
    unsigned len;
};

struct exec_params {
    char *userfname;            /* The arguments to the exec() call */
    char **userargp;
    char **userenvp;
    union {
        char sh[SHSIZE];
        struct elf_ehdr elf;
    } hdr;                      /* head of file to exec */
    int hdr_len;                /* number of bytes valid in image_header */
    char **argp, **envp;
    u_short argc, envc;         /* count of argument and environment strings */
    u_short argbc, envbc;       /* total number of chars in argc and envc string pool */
    union {
        struct {
            char interpname[20];        /* real name of the script interpreter */
            char interparg[SHPATHLEN];  /* interpreter arg */
            char interpreted;           /* flag - this executable is interpreted */
        } sh;
        struct {
            struct buf *stbp;   /* String table buffer pointer */
            int stbpos;         /* String table pos in buffer */
            int stsize;         /* String table size */
            int stoffset;       /* String table file pos */
            char str[STRLEN];
        } elf;
    };

    gid_t gid;
    uid_t uid;
#define MAXALLOCBUF 6
    struct {
        struct buf *bp;         /* Memory allocator buffer */
        u_short fill;           /* Memory allocator "free" pointer */
    } alloc[MAXALLOCBUF];
    u_long ep_taddr, ep_tsize, ep_daddr, ep_dsize;
    struct inode *ip;           /* executable file ip */
    struct memsect text, data, bss, heap, stack;
    struct vmspace *vmspace;    /* uncommitted replacement address space */
    unsigned entry;             /* staged initial register state */
    unsigned stack_pointer;
    unsigned arg_pointer;
    unsigned env_pointer;
};

struct execsw {
    int (*es_check)(struct exec_params *epp);
    const char* es_name;
};
extern const struct execsw execsw[];
extern int nexecs, exec_maxhdrsz;


struct buf *exec_copy_args(char **argp, struct exec_params *epp, int isargv, int *argc, int *argbc);
int exec_check(struct exec_params *epp);
int exec_setupstack(unsigned entryaddr, struct exec_params *epp);
void exec_alloc_freeall(struct exec_params *epp);
void *exec_alloc(int size, int ru, struct exec_params *epp);
int exec_save_args(struct exec_params *epp);
int exec_stack_size(struct exec_params *epp, unsigned *size);
void exec_clear(struct exec_params *epp);
int exec_commit(struct exec_params *epp);
void execv(void);
void execve(void);

#else /* KERNEL */
#include <sys/exec_elf.h>
#endif
#endif
