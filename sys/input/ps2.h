/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _INPUT_PS2_H_
#define _INPUT_PS2_H_

#include <input/kbd.h>
#include <input/mousevar.h>

struct ps2kbd_decoder {
    struct kbd_mapper pk_mapper;
    unsigned pk_modifiers;
    unsigned pk_extended;
    unsigned pk_pause_bytes;
};

struct ps2mouse_decoder {
    unsigned pm_index;
    unsigned char pm_packet[3];
};

void ps2kbd_decoder_init(struct ps2kbd_decoder *);
void ps2kbd_decode_byte(struct ps2kbd_decoder *, unsigned char,
    kbd_emit_t, void *);

void ps2mouse_decoder_init(struct ps2mouse_decoder *);
void ps2mouse_decode_byte(struct ps2mouse_decoder *, unsigned char,
    mouse_emit_t, void *);

#endif /* _INPUT_PS2_H_ */
