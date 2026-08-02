/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Machine-independent kernel message buffer.
 *
 * The live /dev/klog reader is implemented by subr_log.c.  The storage and
 * writer live here so every architecture can retain console diagnostics even
 * when no logging daemon or log character device is configured.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/msgbuf.h>
#include <sys/systm.h>

struct msgbuf msgbuf[MSG_NLOG];

/* Serialized by the top-level sysctl lock before oldp is supplied. */
static char msgbuf_snapshot[MSG_BSIZE];

/*
 * Initialize the ring before the first machine-independent printf().
 */
int
loginit(void)
{
    struct msgbuf *mp;

    for (mp = &msgbuf[0]; mp < &msgbuf[MSG_NLOG]; ++mp) {
        bzero(mp, sizeof(*mp));
        mp->msg_magic = MSG_MAGIC;
    }
    return 0;
}

/*
 * Append bytes to one of the kernel message rings.  msg_bufr remains the
 * independent /dev/klog cursor; msg_bufx is the ring write position used by
 * dmesg snapshots.
 */
int
logwrt(char *buf, int len, int log)
{
    struct msgbuf *mp;
    int chunk;
    int s;

    if (log < 0 || log >= MSG_NLOG || len < 0 || len > MSG_BSIZE)
        return -1;
    mp = &msgbuf[log];
    if (mp->msg_magic != MSG_MAGIC)
        return -1;

    s = splhigh();
    while (len != 0) {
        if (mp->msg_bufx == MSG_BSIZE)
            mp->msg_bufx = 0;
        chunk = MSG_BSIZE - mp->msg_bufx;
        if (chunk > len)
            chunk = len;
        bcopy(buf, &mp->msg_bufc[mp->msg_bufx], chunk);
        mp->msg_bufx += chunk;
        buf += chunk;
        len -= chunk;
    }
    splx(s);
    return 0;
}

/*
 * Return the current ring in chronological order for kern.msgbuf.  Console
 * printf never stores NUL bytes, so zero-filled cells distinguish the unused
 * part of a ring which has not wrapped yet.  Once full, msg_bufx identifies
 * the oldest retained byte exactly as in traditional BSD dmesg readers.
 */
int
msgbuf_sysctl(void *oldp, size_t *oldlenp, void *newp)
{
    struct msgbuf *mp;
    size_t length;
    int index;
    int offset;
    int s;

    if (newp != NULL)
        return EPERM;
    if (oldlenp == NULL)
        return EINVAL;
    if (oldp == NULL) {
        *oldlenp = MSG_BSIZE;
        return 0;
    }

    mp = &msgbuf[logMSG];
    length = 0;
    s = splhigh();
    if (mp->msg_magic == MSG_MAGIC) {
        index = mp->msg_bufx;
        if (index == MSG_BSIZE)
            index = 0;
        for (offset = 0; offset < MSG_BSIZE; ++offset) {
            char ch = mp->msg_bufc[index];

            if (ch != '\0')
                msgbuf_snapshot[length++] = ch;
            if (++index == MSG_BSIZE)
                index = 0;
        }
    }
    splx(s);

    if (*oldlenp < length) {
        *oldlenp = length;
        return ENOMEM;
    }
    *oldlenp = length;
    if (length == 0)
        return 0;
    return copyout(msgbuf_snapshot, oldp, length);
}
