/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_SHM_H_
#define _SYS_SHM_H_

#include <sys/ipc.h>

#define SHM_RDONLY      010000
#define SHM_RND         020000
#if defined(N64) || defined(TARGET_VR4300) || defined(__vr4300__) || \
    defined(MALTA_N64_8M_PROFILE)
#define SHMLBA          16384u
#else
#define SHMLBA          4096u
#endif

typedef unsigned shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t          shm_segsz;
    pid_t           shm_lpid;
    pid_t           shm_cpid;
    shmatt_t        shm_nattch;
    time_t          shm_atime;
    time_t          shm_dtime;
    time_t          shm_ctime;
};

#ifndef KERNEL
int shmget(key_t, size_t, int);
void *shmat(int, const void *, int);
int shmdt(const void *);
int shmctl(int, int, struct shmid_ds *);
#endif

#endif /* _SYS_SHM_H_ */
