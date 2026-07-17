/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H_

#include <sys/types.h>

#define PROT_NONE       0x00
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define PROT_EXEC       0x04

#define MAP_SHARED      0x0001
#define MAP_PRIVATE     0x0002
#define MAP_FIXED       0x0010
#define MAP_ANON        0x1000
#define MAP_ANONYMOUS   MAP_ANON

#define MAP_FAILED      ((void *)-1)

#define MS_ASYNC        0x0001
#define MS_INVALIDATE   0x0002
#define MS_SYNC         0x0004

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4
#define MADV_FREE       5

#define MINCORE_INCORE  0x01

#ifndef KERNEL
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int msync(void *, size_t, int);
int madvise(void *, size_t, int);
int mincore(void *, size_t, unsigned char *);
int mlock(const void *, size_t);
int munlock(const void *, size_t);
int shm_open(const char *, int, mode_t);
int shm_unlink(const char *);
#endif

#endif /* _SYS_MMAN_H_ */
