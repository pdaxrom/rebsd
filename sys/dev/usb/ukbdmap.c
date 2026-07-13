/*	$NetBSD: ukbdmap.c,v 1.13.10.1 2005/05/09 17:21:48 tron Exp $	*/

/*
 * Copyright (c) 1999, 2001 The NetBSD Foundation, Inc.
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

#include <dev/usb/ukbd.h>

#define UKBD_MOD_LCTRL          0x01u
#define UKBD_MOD_LSHIFT         0x02u
#define UKBD_MOD_RCTRL          0x10u
#define UKBD_MOD_RSHIFT         0x20u
#define UKBD_USAGE_CAPS_LOCK    57u

static void
ukbd_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

void
ukbd_decoder_init(struct ukbd_decoder *decoder)
{
    if (decoder != 0)
        ukbd_zero(decoder, sizeof(*decoder));
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

static void
ukbd_emit_sequence(ukbd_emit_t emit, void *arg, const char *sequence)
{
    while (*sequence != '\0')
        emit(arg, (unsigned char)*sequence++);
}

static int
ukbd_punctuation(uByte usage, int shifted)
{
    switch (usage) {
    case 45:
        return shifted ? '_' : '-';
    case 46:
        return shifted ? '+' : '=';
    case 47:
        return shifted ? '{' : '[';
    case 48:
        return shifted ? '}' : ']';
    case 49:
    case 50:
        return shifted ? '|' : '\\';
    case 51:
        return shifted ? ':' : ';';
    case 52:
        return shifted ? '"' : '\'';
    case 53:
        return shifted ? '~' : 0x60;
    case 54:
        return shifted ? '<' : ',';
    case 55:
        return shifted ? '>' : '.';
    case 56:
        return shifted ? '?' : '/';
    default:
        return -1;
    }
}

static int
ukbd_ascii(uByte usage, int shifted, int caps_lock, int control)
{
    static const char digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";
    int letter;

    if (usage >= 4 && usage <= 29) {
        letter = 'a' + (int)(usage - 4);
        if (control)
            return letter - 'a' + 1;
        if (shifted != caps_lock)
            letter -= 'a' - 'A';
        return letter;
    }
    if (usage >= 30 && usage <= 39)
        return shifted ? shifted_digits[usage - 30] : digits[usage - 30];
    if (usage >= 45 && usage <= 56)
        return ukbd_punctuation(usage, shifted);
    switch (usage) {
    case 40:
    case 88:
        return '\r';
    case 41:
        return '\033';
    case 42:
        return '\177';
    case 43:
        return '\t';
    case 44:
        return control ? 0 : ' ';
    default:
        return -1;
    }
}

static void
ukbd_emit_usage(struct ukbd_decoder *decoder, uByte modifiers, uByte usage,
    ukbd_emit_t emit, void *arg)
{
    int character;
    int control;
    int shifted;

    if (usage == UKBD_USAGE_CAPS_LOCK) {
        decoder->ukd_caps_lock = !decoder->ukd_caps_lock;
        return;
    }
    switch (usage) {
    case 74:
        ukbd_emit_sequence(emit, arg, "\033[H");
        return;
    case 75:
        ukbd_emit_sequence(emit, arg, "\033[5~");
        return;
    case 76:
        ukbd_emit_sequence(emit, arg, "\033[3~");
        return;
    case 77:
        ukbd_emit_sequence(emit, arg, "\033[F");
        return;
    case 78:
        ukbd_emit_sequence(emit, arg, "\033[6~");
        return;
    case 79:
        ukbd_emit_sequence(emit, arg, "\033[C");
        return;
    case 80:
        ukbd_emit_sequence(emit, arg, "\033[D");
        return;
    case 81:
        ukbd_emit_sequence(emit, arg, "\033[B");
        return;
    case 82:
        ukbd_emit_sequence(emit, arg, "\033[A");
        return;
    default:
        break;
    }
    shifted = (modifiers & (UKBD_MOD_LSHIFT | UKBD_MOD_RSHIFT)) != 0;
    control = (modifiers & (UKBD_MOD_LCTRL | UKBD_MOD_RCTRL)) != 0;
    character = ukbd_ascii(usage, shifted, decoder->ukd_caps_lock,
        control);
    if (character >= 0)
        emit(arg, character);
}

void
ukbd_decode_boot_report(struct ukbd_decoder *decoder, const uByte *report,
    size_t length, ukbd_emit_t emit, void *arg)
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
        ukbd_emit_usage(decoder, report[0], usage, emit, arg);
    }
    for (i = 0; i < UKBD_BOOT_KEY_COUNT; ++i)
        decoder->ukd_keys[i] = report[2 + i];
}
