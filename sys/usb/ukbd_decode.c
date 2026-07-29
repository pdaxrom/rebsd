/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <usb/ukbd.h>

static void
ukbd_decoder_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

void
ukbd_decoder_init(struct ukbd_decoder *decoder)
{
    if (decoder == 0)
        return;
    ukbd_decoder_zero(decoder, sizeof(*decoder));
    kbd_mapper_init(&decoder->ukd_mapper);
}

static int
ukbd_was_down(const struct ukbd_decoder *decoder, uByte usage)
{
    unsigned i;

    for (i = 0; i < UKBD_BOOT_KEY_COUNT; ++i)
        if (decoder->ukd_keys[i] == usage)
            return 1;
    return 0;
}

static int
ukbd_seen_in_report(const uByte *report, unsigned before, uByte usage)
{
    unsigned i;

    for (i = 0; i < before; ++i)
        if (report[2 + i] == usage)
            return 1;
    return 0;
}

void
ukbd_decode_boot_report(struct ukbd_decoder *decoder, const uByte *report,
    size_t length, kbd_emit_t emit, void *arg)
{
    unsigned i;
    uByte usage;

    if (decoder == 0 || report == 0 || emit == 0 ||
        length != UKBD_BOOT_REPORT_SIZE)
        return;
    for (i = 0; i < UKBD_BOOT_KEY_COUNT; ++i)
        if (report[2 + i] >= 1 && report[2 + i] <= 3)
            return;
    for (i = 0; i < UKBD_BOOT_KEY_COUNT; ++i) {
        usage = report[2 + i];
        if (usage == 0 || ukbd_was_down(decoder, usage) ||
            ukbd_seen_in_report(report, i, usage))
            continue;
        kbd_mapper_key_down(&decoder->ukd_mapper, report[0], usage,
            emit, arg);
    }
    for (i = 0; i < UKBD_BOOT_KEY_COUNT; ++i)
        decoder->ukd_keys[i] = report[2 + i];
}
