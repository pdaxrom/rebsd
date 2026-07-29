/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/user.h>

#include <input/mousevar.h>

#define MOUSE_EVENT_QUEUE_SIZE  32u

struct mouse_softc {
    unsigned ms_attached;
    unsigned ms_unit;
    const char *ms_name;
    unsigned ms_buttons;
    unsigned ms_head;
    unsigned ms_tail;
    unsigned ms_active_reported;
    struct proc *ms_selector;
    struct mouse_event ms_queue[MOUSE_EVENT_QUEUE_SIZE];
};

static struct mouse_softc mouse_softc[MOUSE_MAX_DEVICES];

static struct mouse_softc *
mouse_lookup(unsigned unit)
{
    if (unit >= MOUSE_MAX_DEVICES || !mouse_softc[unit].ms_attached)
        return 0;
    return &mouse_softc[unit];
}

int
mouse_attach(const char *name)
{
    struct mouse_softc *sc;
    unsigned unit;
    int s;

    s = spltty();
    for (unit = 0; unit < MOUSE_MAX_DEVICES; ++unit) {
        sc = &mouse_softc[unit];
        if (!sc->ms_attached) {
            bzero(sc, sizeof(*sc));
            sc->ms_attached = 1;
            sc->ms_unit = unit;
            sc->ms_name = name;
            splx(s);
            printf("mouse%u: source %s\n", unit,
                name != 0 ? name : "unknown");
            return (int)unit;
        }
    }
    splx(s);
    return -1;
}

void
mouse_detach(unsigned unit)
{
    struct mouse_softc *sc;
    struct proc *selector;
    int s;

    s = spltty();
    sc = mouse_lookup(unit);
    if (sc == 0) {
        splx(s);
        return;
    }
    sc->ms_attached = 0;
    selector = sc->ms_selector;
    sc->ms_selector = 0;
    wakeup((caddr_t)sc);
    splx(s);
    if (selector != 0)
        selwakeup(selector, 0);
    printf("mouse%u: detached\n", unit);
}

void
mouse_input(unsigned unit, const struct mouse_event *event)
{
    struct mouse_softc *sc;
    struct proc *selector;
    unsigned next;
    unsigned flags;
    int report_active;
    int s;

    if (event == 0)
        return;
    s = spltty();
    sc = mouse_lookup(unit);
    if (sc == 0) {
        splx(s);
        return;
    }
    flags = event->me_flags;
    if (event->me_dx != 0 || event->me_dy != 0 || event->me_dz != 0)
        flags |= MOUSE_EVENT_MOTION;
    if (event->me_buttons != sc->ms_buttons)
        flags |= MOUSE_EVENT_BUTTONS;
    if (flags == 0) {
        splx(s);
        return;
    }
    next = (sc->ms_head + 1u) % MOUSE_EVENT_QUEUE_SIZE;
    if (next == sc->ms_tail)
        sc->ms_tail = (sc->ms_tail + 1u) % MOUSE_EVENT_QUEUE_SIZE;
    sc->ms_queue[sc->ms_head] = *event;
    sc->ms_queue[sc->ms_head].me_flags = flags;
    sc->ms_head = next;
    sc->ms_buttons = event->me_buttons;
    report_active = !sc->ms_active_reported;
    sc->ms_active_reported = 1;
    selector = sc->ms_selector;
    sc->ms_selector = 0;
    wakeup((caddr_t)sc);
    splx(s);
    if (selector != 0)
        selwakeup(selector, 0);
    if (report_active)
        printf("mouse%u: input active\n", unit);
}

int
mouse_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return mouse_lookup(minor(dev)) != 0 ? 0 : ENXIO;
}

int
mouse_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return minor(dev) < MOUSE_MAX_DEVICES ? 0 : ENXIO;
}

int
mouse_read(dev_t dev, struct uio *uio, int flag)
{
    struct mouse_event event;
    struct mouse_softc *sc;
    unsigned unit;
    int error;
    int s;

    unit = minor(dev);
    if (uio->uio_resid < sizeof(event))
        return EINVAL;
    s = spltty();
    for (;;) {
        sc = mouse_lookup(unit);
        if (sc == 0) {
            splx(s);
            return ENXIO;
        }
        if (sc->ms_head != sc->ms_tail)
            break;
        if ((flag & IO_NDELAY) != 0) {
            splx(s);
            return EWOULDBLOCK;
        }
        error = tsleep((caddr_t)sc, TTIPRI | PCATCH, 0);
        if (error != 0) {
            splx(s);
            return error;
        }
    }
    event = sc->ms_queue[sc->ms_tail];
    sc->ms_tail = (sc->ms_tail + 1u) % MOUSE_EVENT_QUEUE_SIZE;
    splx(s);
    return uiomove((caddr_t)&event, sizeof(event), uio);
}

int
mouse_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    (void)dev;
    (void)cmd;
    (void)data;
    (void)flag;
    return ENOTTY;
}

int
mouse_select(dev_t dev, int rw)
{
    struct mouse_softc *sc;
    int ready;
    int s;

    if (rw != FREAD)
        return 0;
    s = spltty();
    sc = mouse_lookup(minor(dev));
    ready = sc == 0 || sc->ms_head != sc->ms_tail;
    if (!ready)
        sc->ms_selector = u.u_procp;
    splx(s);
    return ready;
}
