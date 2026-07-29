/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _INPUT_MOUSEVAR_H_
#define _INPUT_MOUSEVAR_H_

#include <sys/mouse.h>

#define MOUSE_MAX_DEVICES       4u

typedef void (*mouse_emit_t)(void *, const struct mouse_event *);

#ifdef KERNEL
#include <sys/types.h>

struct uio;

int mouse_attach(const char *);
void mouse_detach(unsigned);
void mouse_input(unsigned, const struct mouse_event *);

int mouse_open(dev_t, int, int);
int mouse_close(dev_t, int, int);
int mouse_read(dev_t, struct uio *, int);
int mouse_ioctl(dev_t, u_int, caddr_t, int);
int mouse_select(dev_t, int);
#endif

#endif /* _INPUT_MOUSEVAR_H_ */
