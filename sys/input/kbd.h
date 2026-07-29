/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _INPUT_KBD_H_
#define _INPUT_KBD_H_

#define KBD_MOD_LCTRL          0x01u
#define KBD_MOD_LSHIFT         0x02u
#define KBD_MOD_LALT           0x04u
#define KBD_MOD_LGUI           0x08u
#define KBD_MOD_RCTRL          0x10u
#define KBD_MOD_RSHIFT         0x20u
#define KBD_MOD_RALT           0x40u
#define KBD_MOD_RGUI           0x80u

struct kbd_mapper {
    unsigned char km_caps_lock;
};

typedef void (*kbd_emit_t)(void *, int);

void kbd_mapper_init(struct kbd_mapper *);
void kbd_mapper_key_down(struct kbd_mapper *, unsigned, unsigned,
    kbd_emit_t, void *);

#endif /* _INPUT_KBD_H_ */
