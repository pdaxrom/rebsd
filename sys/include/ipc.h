/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_IPC_H_
#define _SYS_IPC_H_

#include <sys/types.h>

#ifndef _KEY_T
#define _KEY_T
typedef int key_t;
#endif

#define IPC_PRIVATE     ((key_t)0)

#define IPC_CREAT       01000
#define IPC_EXCL        02000
#define IPC_NOWAIT      04000

#define IPC_RMID        0
#define IPC_SET         1
#define IPC_STAT        2

struct ipc_perm {
    uid_t       uid;
    gid_t       gid;
    uid_t       cuid;
    gid_t       cgid;
    mode_t      mode;
    unsigned    seq;
    key_t       key;
};

#endif /* _SYS_IPC_H_ */
