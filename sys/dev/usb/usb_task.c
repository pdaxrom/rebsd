/*	$NetBSD: usb.c,v 1.81.6.1 2006/03/24 22:35:33 riz Exp $	*/

/*
 * Copyright (c) 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#endif

#include <dev/usb/usb_task.h>

static struct usb_task *usb_task_queue[USB_MAX_TASKS];
static unsigned usb_task_count;

#ifdef KERNEL
static int
usb_task_lock(void)
{
    return splhigh();
}

static void
usb_task_unlock(int s)
{
    splx(s);
}
#else
static int
usb_task_lock(void)
{
    return 0;
}

static void
usb_task_unlock(int s)
{
    (void)s;
}
#endif

void
usb_task_system_init(void)
{
    unsigned i;

    for (i = 0; i < USB_MAX_TASKS; ++i)
        usb_task_queue[i] = 0;
    usb_task_count = 0;
}

void
usb_task_init(struct usb_task *task, usb_task_fn_t func, void *arg)
{
    if (task == 0)
        return;
    task->ut_func = func;
    task->ut_arg = arg;
    task->ut_pending = 0;
}

int
usb_task_schedule(struct usb_task *task)
{
    int s;

    if (task == 0 || task->ut_func == 0)
        return -1;
    s = usb_task_lock();
    if (task->ut_pending) {
        usb_task_unlock(s);
        return 0;
    }
    if (usb_task_count == USB_MAX_TASKS) {
        usb_task_unlock(s);
        return -1;
    }
    usb_task_queue[usb_task_count++] = task;
    task->ut_pending = 1;
#ifdef KERNEL
    /*
     * ReBSD has no kernel-thread facility.  Deferred USB work is drained
     * by proc0's scheduler loop, which may be asleep on either channel.
     */
    wakeup((caddr_t)&runin);
    wakeup((caddr_t)&runout);
#endif
    usb_task_unlock(s);
    return 0;
}

void
usb_task_cancel(struct usb_task *task)
{
    unsigned i;
    unsigned j;
    int s;

    if (task == 0)
        return;
    s = usb_task_lock();
    for (i = 0; i < usb_task_count; ++i)
        if (usb_task_queue[i] == task) {
            for (j = i + 1; j < usb_task_count; ++j)
                usb_task_queue[j - 1] = usb_task_queue[j];
            --usb_task_count;
            usb_task_queue[usb_task_count] = 0;
            task->ut_pending = 0;
            break;
        }
    usb_task_unlock(s);
}

int
usb_task_pending(const struct usb_task *task)
{
    return task != 0 && task->ut_pending != 0;
}

int
usb_task_any_pending(void)
{
    int pending;
    int s;

    s = usb_task_lock();
    pending = usb_task_count != 0;
    usb_task_unlock(s);
    return pending;
}

static struct usb_task *
usb_task_take(void)
{
    struct usb_task *task;
    unsigned i;
    int s;

    s = usb_task_lock();
    if (usb_task_count == 0) {
        usb_task_unlock(s);
        return 0;
    }
    task = usb_task_queue[0];
    for (i = 1; i < usb_task_count; ++i)
        usb_task_queue[i - 1] = usb_task_queue[i];
    --usb_task_count;
    usb_task_queue[usb_task_count] = 0;
    task->ut_pending = 0;
    usb_task_unlock(s);
    return task;
}

void
usb_task_run_pending(void)
{
    struct usb_task *task;

    while ((task = usb_task_take()) != 0)
        task->ut_func(task->ut_arg);
}
