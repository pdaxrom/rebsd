/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <input/ps2.h>

static unsigned
ps2kbd_set1_usage(unsigned code, int extended)
{
    static const unsigned char base[128] = {
        [0x01] = 41,
        [0x02] = 30, [0x03] = 31, [0x04] = 32, [0x05] = 33,
        [0x06] = 34, [0x07] = 35, [0x08] = 36, [0x09] = 37,
        [0x0a] = 38, [0x0b] = 39, [0x0c] = 45, [0x0d] = 46,
        [0x0e] = 42, [0x0f] = 43,
        [0x10] = 20, [0x11] = 26, [0x12] = 8,  [0x13] = 21,
        [0x14] = 23, [0x15] = 28, [0x16] = 24, [0x17] = 12,
        [0x18] = 18, [0x19] = 19, [0x1a] = 47, [0x1b] = 48,
        [0x1c] = 40,
        [0x1e] = 4,  [0x1f] = 22, [0x20] = 7,  [0x21] = 9,
        [0x22] = 10, [0x23] = 11, [0x24] = 13, [0x25] = 14,
        [0x26] = 15, [0x27] = 51, [0x28] = 52, [0x29] = 53,
        [0x2b] = 49,
        [0x2c] = 29, [0x2d] = 27, [0x2e] = 6,  [0x2f] = 25,
        [0x30] = 5,  [0x31] = 17, [0x32] = 16, [0x33] = 54,
        [0x34] = 55, [0x35] = 56, [0x39] = 44, [0x3a] = 57
    };

    if (extended) {
        switch (code) {
        case 0x1c:
            return 88;
        case 0x47:
            return 74;
        case 0x49:
            return 75;
        case 0x53:
            return 76;
        case 0x4f:
            return 77;
        case 0x51:
            return 78;
        case 0x4d:
            return 79;
        case 0x4b:
            return 80;
        case 0x50:
            return 81;
        case 0x48:
            return 82;
        default:
            return 0;
        }
    }
    return code < sizeof(base) ? base[code] : 0;
}

static unsigned
ps2kbd_modifier(unsigned code, int extended)
{
    if (code == 0x1d)
        return extended ? KBD_MOD_RCTRL : KBD_MOD_LCTRL;
    if (code == 0x2a && !extended)
        return KBD_MOD_LSHIFT;
    if (code == 0x36 && !extended)
        return KBD_MOD_RSHIFT;
    if (code == 0x38)
        return extended ? KBD_MOD_RALT : KBD_MOD_LALT;
    return 0;
}

void
ps2kbd_decoder_init(struct ps2kbd_decoder *decoder)
{
    if (decoder == 0)
        return;
    kbd_mapper_init(&decoder->pk_mapper);
    decoder->pk_modifiers = 0;
    decoder->pk_extended = 0;
    decoder->pk_pause_bytes = 0;
}

void
ps2kbd_decode_byte(struct ps2kbd_decoder *decoder, unsigned char byte,
    kbd_emit_t emit, void *arg)
{
    unsigned code;
    unsigned modifier;
    unsigned usage;
    int released;

    if (decoder == 0 || emit == 0)
        return;
    if (decoder->pk_pause_bytes != 0) {
        --decoder->pk_pause_bytes;
        return;
    }
    if (byte == 0xe1) {
        decoder->pk_extended = 0;
        decoder->pk_pause_bytes = 5;
        return;
    }
    if (byte == 0xe0) {
        decoder->pk_extended = 1;
        return;
    }
    released = (byte & 0x80u) != 0;
    code = byte & 0x7fu;
    modifier = ps2kbd_modifier(code, decoder->pk_extended != 0);
    if (modifier != 0) {
        if (released)
            decoder->pk_modifiers &= ~modifier;
        else
            decoder->pk_modifiers |= modifier;
    } else if (!released) {
        usage = ps2kbd_set1_usage(code, decoder->pk_extended != 0);
        if (usage != 0)
            kbd_mapper_key_down(&decoder->pk_mapper,
                decoder->pk_modifiers, usage, emit, arg);
    }
    decoder->pk_extended = 0;
}

void
ps2mouse_decoder_init(struct ps2mouse_decoder *decoder)
{
    if (decoder == 0)
        return;
    decoder->pm_index = 0;
    decoder->pm_packet[0] = 0;
    decoder->pm_packet[1] = 0;
    decoder->pm_packet[2] = 0;
}

void
ps2mouse_decode_byte(struct ps2mouse_decoder *decoder, unsigned char byte,
    mouse_emit_t emit, void *arg)
{
    struct mouse_event event;
    unsigned buttons;

    if (decoder == 0 || emit == 0)
        return;
    if (decoder->pm_index == 0 && (byte & 0x08u) == 0)
        return;
    decoder->pm_packet[decoder->pm_index++] = byte;
    if (decoder->pm_index != 3)
        return;
    decoder->pm_index = 0;
    if ((decoder->pm_packet[0] & 0xc0u) != 0)
        return;
    buttons = 0;
    if ((decoder->pm_packet[0] & 0x01u) != 0)
        buttons |= MOUSE_BUTTON_LEFT;
    if ((decoder->pm_packet[0] & 0x02u) != 0)
        buttons |= MOUSE_BUTTON_RIGHT;
    if ((decoder->pm_packet[0] & 0x04u) != 0)
        buttons |= MOUSE_BUTTON_MIDDLE;
    event.me_dx = (signed char)decoder->pm_packet[1];
    event.me_dy = -(int)(signed char)decoder->pm_packet[2];
    event.me_dz = 0;
    event.me_buttons = buttons;
    event.me_flags = 0;
    emit(arg, &event);
}
