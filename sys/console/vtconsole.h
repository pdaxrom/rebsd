/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYSCONSOLE_VTCONSOLE_H_
#define _SYSCONSOLE_VTCONSOLE_H_

#define VTCONSOLE_MAX_PARAMS       8u

#define VTCONSOLE_ATTR_BOLD        0x01u
#define VTCONSOLE_ATTR_UNDERLINE   0x02u
#define VTCONSOLE_ATTR_REVERSE     0x04u

struct vtconsole;

struct vtconsole_ops {
    void (*render_cell)(void *, unsigned, unsigned, int);
    void (*render_all)(void *);
    void (*cursor)(void *, unsigned, unsigned, int);
    void (*bell)(void *);
};

struct vtconsole {
    unsigned char *vc_cells;
    unsigned char *vc_attrs;
    unsigned vc_stride;
    unsigned vc_max_cols;
    unsigned vc_max_rows;
    unsigned vc_cols;
    unsigned vc_rows;
    unsigned vc_col;
    unsigned vc_row;
    unsigned vc_saved_col;
    unsigned vc_saved_row;
    unsigned vc_state;
    unsigned vc_attr;
    unsigned vc_params[VTCONSOLE_MAX_PARAMS];
    unsigned vc_param_index;
    int vc_csi_private;
    int vc_cursor_visible;
    const struct vtconsole_ops *vc_ops;
    void *vc_arg;
};

void vtconsole_init(struct vtconsole *, unsigned char *, unsigned char *,
    unsigned, unsigned, unsigned, const struct vtconsole_ops *, void *);
void vtconsole_set_geometry(struct vtconsole *, unsigned, unsigned);
void vtconsole_reset(struct vtconsole *);
void vtconsole_putc(struct vtconsole *, int);

#endif
