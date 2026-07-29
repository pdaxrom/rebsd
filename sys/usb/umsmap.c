/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <usb/ums.h>

void
ums_decode_boot_report(const unsigned char *report, size_t length,
    mouse_emit_t emit, void *arg)
{
    struct mouse_event event;
    unsigned buttons;

    if (report == 0 || emit == 0 || length < UMS_BOOT_REPORT_MIN_SIZE)
        return;
    buttons = 0;
    if ((report[0] & 0x01u) != 0)
        buttons |= MOUSE_BUTTON_LEFT;
    if ((report[0] & 0x02u) != 0)
        buttons |= MOUSE_BUTTON_RIGHT;
    if ((report[0] & 0x04u) != 0)
        buttons |= MOUSE_BUTTON_MIDDLE;
    if ((report[0] & 0x08u) != 0)
        buttons |= MOUSE_BUTTON_4;
    if ((report[0] & 0x10u) != 0)
        buttons |= MOUSE_BUTTON_5;
    event.me_dx = (signed char)report[1];
    event.me_dy = (signed char)report[2];
    event.me_dz = length >= 4 ? (signed char)report[3] : 0;
    event.me_buttons = buttons;
    event.me_flags = 0;
    emit(arg, &event);
}
