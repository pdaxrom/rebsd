/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_MOUSE_H_
#define _SYS_MOUSE_H_

#define MOUSE_BUTTON_LEFT       0x01u
#define MOUSE_BUTTON_RIGHT      0x02u
#define MOUSE_BUTTON_MIDDLE     0x04u
#define MOUSE_BUTTON_4          0x08u
#define MOUSE_BUTTON_5          0x10u

#define MOUSE_EVENT_MOTION      0x01u
#define MOUSE_EVENT_BUTTONS     0x02u

/*
 * One complete record is returned by each read from /dev/mouseN.
 * Positive X moves right; positive Y moves down; positive Z scrolls down.
 */
struct mouse_event {
    int me_dx;
    int me_dy;
    int me_dz;
    unsigned me_buttons;
    unsigned me_flags;
};

#endif /* _SYS_MOUSE_H_ */
