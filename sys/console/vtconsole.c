/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <console/vtconsole.h>

#define VT_STATE_GROUND       0u
#define VT_STATE_ESC          1u
#define VT_STATE_ESC_CHARSET  2u
#define VT_STATE_CSI          3u
#define VT_STATE_OSC          4u
#define VT_STATE_OSC_ESC      5u

static unsigned char *
vt_cell(struct vtconsole *vc, unsigned col, unsigned row)
{
    return vc->vc_cells + row * vc->vc_stride + col;
}

static unsigned char *
vt_attr(struct vtconsole *vc, unsigned col, unsigned row)
{
    return vc->vc_attrs + row * vc->vc_stride + col;
}

static void
vt_render_cell(struct vtconsole *vc, unsigned col, unsigned row)
{
    if (vc->vc_ops != 0 && vc->vc_ops->render_cell != 0)
        vc->vc_ops->render_cell(vc->vc_arg, col, row, 0);
}

static void
vt_render_all(struct vtconsole *vc)
{
    if (vc->vc_ops != 0 && vc->vc_ops->render_all != 0)
        vc->vc_ops->render_all(vc->vc_arg);
}

static void
vt_cursor(struct vtconsole *vc, int visible)
{
    if (vc->vc_ops != 0 && vc->vc_ops->cursor != 0 &&
        vc->vc_cols != 0 && vc->vc_rows != 0)
        vc->vc_ops->cursor(vc->vc_arg, vc->vc_col, vc->vc_row, visible);
}

static void
vt_clamp_cursor(struct vtconsole *vc)
{
    if (vc->vc_cols == 0 || vc->vc_rows == 0) {
        vc->vc_col = 0;
        vc->vc_row = 0;
        return;
    }
    if (vc->vc_col >= vc->vc_cols)
        vc->vc_col = vc->vc_cols - 1;
    if (vc->vc_row >= vc->vc_rows)
        vc->vc_row = vc->vc_rows - 1;
}

static void
vt_clear_range(struct vtconsole *vc, unsigned row, unsigned first,
    unsigned end)
{
    unsigned col;

    if (row >= vc->vc_rows || first >= vc->vc_cols)
        return;
    if (end > vc->vc_cols)
        end = vc->vc_cols;
    for (col = first; col < end; ++col) {
        *vt_cell(vc, col, row) = ' ';
        *vt_attr(vc, col, row) = (unsigned char)vc->vc_attr;
        vt_render_cell(vc, col, row);
    }
}

static void
vt_scroll(struct vtconsole *vc)
{
    unsigned row;
    unsigned col;

    if (vc->vc_cols == 0 || vc->vc_rows == 0)
        return;
    for (row = 0; row + 1 < vc->vc_rows; ++row) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) = *vt_cell(vc, col, row + 1);
            *vt_attr(vc, col, row) = *vt_attr(vc, col, row + 1);
        }
    }
    for (col = 0; col < vc->vc_cols; ++col) {
        *vt_cell(vc, col, vc->vc_rows - 1) = ' ';
        *vt_attr(vc, col, vc->vc_rows - 1) =
            (unsigned char)vc->vc_attr;
    }
    vt_render_all(vc);
}

static void
vt_reverse_index(struct vtconsole *vc)
{
    unsigned row;
    unsigned col;

    if (vc->vc_cols == 0 || vc->vc_rows == 0)
        return;
    if (vc->vc_row != 0) {
        --vc->vc_row;
        return;
    }
    row = vc->vc_rows;
    while (row-- > 1) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) = *vt_cell(vc, col, row - 1);
            *vt_attr(vc, col, row) = *vt_attr(vc, col, row - 1);
        }
    }
    for (col = 0; col < vc->vc_cols; ++col) {
        *vt_cell(vc, col, 0) = ' ';
        *vt_attr(vc, col, 0) = (unsigned char)vc->vc_attr;
    }
    vt_render_all(vc);
}

static void
vt_newline(struct vtconsole *vc)
{
    vc->vc_col = 0;
    if (vc->vc_rows == 0)
        return;
    if (++vc->vc_row >= vc->vc_rows) {
        vt_scroll(vc);
        vc->vc_row = vc->vc_rows - 1;
    }
}

static void
vt_put_printable(struct vtconsole *vc, int ch)
{
    if (vc->vc_cols == 0 || vc->vc_rows == 0)
        return;
    *vt_cell(vc, vc->vc_col, vc->vc_row) = (unsigned char)ch;
    *vt_attr(vc, vc->vc_col, vc->vc_row) = (unsigned char)vc->vc_attr;
    vt_render_cell(vc, vc->vc_col, vc->vc_row);
    if (++vc->vc_col >= vc->vc_cols)
        vt_newline(vc);
}

static void
vt_control(struct vtconsole *vc, int ch)
{
    switch (ch) {
    case '\r':
        vc->vc_col = 0;
        break;
    case '\n':
    case '\013':
    case '\014':
        vt_newline(vc);
        break;
    case '\t':
        do {
            vt_put_printable(vc, ' ');
        } while (vc->vc_cols != 0 && (vc->vc_col & 7u) != 0);
        break;
    case '\b':
        if (vc->vc_col != 0)
            --vc->vc_col;
        break;
    case '\007':
        if (vc->vc_ops != 0 && vc->vc_ops->bell != 0)
            vc->vc_ops->bell(vc->vc_arg);
        break;
    case 0x7f:
        break;
    default:
        if (ch >= 0x20 && ch < 0x7f)
            vt_put_printable(vc, ch);
        break;
    }
}

static unsigned
vt_param(const struct vtconsole *vc, unsigned index, unsigned default_value)
{
    if (index >= VTCONSOLE_MAX_PARAMS || vc->vc_params[index] == 0)
        return default_value;
    return vc->vc_params[index];
}

static void
vt_erase_display(struct vtconsole *vc, unsigned mode)
{
    unsigned row;

    switch (mode) {
    case 1:
        for (row = 0; row < vc->vc_row && row < vc->vc_rows; ++row)
            vt_clear_range(vc, row, 0, vc->vc_cols);
        vt_clear_range(vc, vc->vc_row, 0, vc->vc_col + 1);
        break;
    case 2:
    case 3:
        for (row = 0; row < vc->vc_rows; ++row)
            vt_clear_range(vc, row, 0, vc->vc_cols);
        break;
    default:
        vt_clear_range(vc, vc->vc_row, vc->vc_col, vc->vc_cols);
        for (row = vc->vc_row + 1; row < vc->vc_rows; ++row)
            vt_clear_range(vc, row, 0, vc->vc_cols);
        break;
    }
}

static void
vt_erase_line(struct vtconsole *vc, unsigned mode)
{
    if (mode == 1)
        vt_clear_range(vc, vc->vc_row, 0, vc->vc_col + 1);
    else if (mode == 2)
        vt_clear_range(vc, vc->vc_row, 0, vc->vc_cols);
    else
        vt_clear_range(vc, vc->vc_row, vc->vc_col, vc->vc_cols);
}

static void
vt_insert_chars(struct vtconsole *vc, unsigned count)
{
    unsigned col;

    if (vc->vc_cols == 0 || vc->vc_col >= vc->vc_cols)
        return;
    if (count == 0)
        count = 1;
    if (count > vc->vc_cols - vc->vc_col)
        count = vc->vc_cols - vc->vc_col;
    col = vc->vc_cols;
    while (col-- > vc->vc_col + count) {
        *vt_cell(vc, col, vc->vc_row) =
            *vt_cell(vc, col - count, vc->vc_row);
        *vt_attr(vc, col, vc->vc_row) =
            *vt_attr(vc, col - count, vc->vc_row);
    }
    for (col = vc->vc_col; col < vc->vc_col + count; ++col) {
        *vt_cell(vc, col, vc->vc_row) = ' ';
        *vt_attr(vc, col, vc->vc_row) = (unsigned char)vc->vc_attr;
    }
    vt_render_all(vc);
}

static void
vt_delete_chars(struct vtconsole *vc, unsigned count)
{
    unsigned col;

    if (vc->vc_cols == 0 || vc->vc_col >= vc->vc_cols)
        return;
    if (count == 0)
        count = 1;
    if (count > vc->vc_cols - vc->vc_col)
        count = vc->vc_cols - vc->vc_col;
    for (col = vc->vc_col; col + count < vc->vc_cols; ++col) {
        *vt_cell(vc, col, vc->vc_row) =
            *vt_cell(vc, col + count, vc->vc_row);
        *vt_attr(vc, col, vc->vc_row) =
            *vt_attr(vc, col + count, vc->vc_row);
    }
    for (; col < vc->vc_cols; ++col) {
        *vt_cell(vc, col, vc->vc_row) = ' ';
        *vt_attr(vc, col, vc->vc_row) = (unsigned char)vc->vc_attr;
    }
    vt_render_all(vc);
}

static void
vt_erase_chars(struct vtconsole *vc, unsigned count)
{
    if (vc->vc_cols == 0 || vc->vc_col >= vc->vc_cols)
        return;
    if (count == 0)
        count = 1;
    if (count > vc->vc_cols - vc->vc_col)
        count = vc->vc_cols - vc->vc_col;
    vt_clear_range(vc, vc->vc_row, vc->vc_col, vc->vc_col + count);
}

static void
vt_insert_lines(struct vtconsole *vc, unsigned count)
{
    unsigned row;
    unsigned col;
    unsigned limit;

    if (vc->vc_rows == 0 || vc->vc_row >= vc->vc_rows)
        return;
    if (count == 0)
        count = 1;
    limit = vc->vc_rows - vc->vc_row;
    if (count > limit)
        count = limit;
    row = vc->vc_rows;
    while (row-- > vc->vc_row + count) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) =
                *vt_cell(vc, col, row - count);
            *vt_attr(vc, col, row) =
                *vt_attr(vc, col, row - count);
        }
    }
    for (row = vc->vc_row; row < vc->vc_row + count; ++row) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) = ' ';
            *vt_attr(vc, col, row) = (unsigned char)vc->vc_attr;
        }
    }
    vt_render_all(vc);
}

static void
vt_delete_lines(struct vtconsole *vc, unsigned count)
{
    unsigned row;
    unsigned col;
    unsigned limit;

    if (vc->vc_rows == 0 || vc->vc_row >= vc->vc_rows)
        return;
    if (count == 0)
        count = 1;
    limit = vc->vc_rows - vc->vc_row;
    if (count > limit)
        count = limit;
    for (row = vc->vc_row; row + count < vc->vc_rows; ++row) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) =
                *vt_cell(vc, col, row + count);
            *vt_attr(vc, col, row) =
                *vt_attr(vc, col, row + count);
        }
    }
    for (; row < vc->vc_rows; ++row) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) = ' ';
            *vt_attr(vc, col, row) = (unsigned char)vc->vc_attr;
        }
    }
    vt_render_all(vc);
}

static void
vt_sgr(struct vtconsole *vc)
{
    unsigned i;
    unsigned parameter;

    for (i = 0; i <= vc->vc_param_index; ++i) {
        parameter = vc->vc_params[i];
        switch (parameter) {
        case 0:
            vc->vc_attr = 0;
            break;
        case 1:
            vc->vc_attr |= VTCONSOLE_ATTR_BOLD;
            break;
        case 4:
            vc->vc_attr |= VTCONSOLE_ATTR_UNDERLINE;
            break;
        case 7:
            vc->vc_attr |= VTCONSOLE_ATTR_REVERSE;
            break;
        case 22:
            vc->vc_attr &= ~VTCONSOLE_ATTR_BOLD;
            break;
        case 24:
            vc->vc_attr &= ~VTCONSOLE_ATTR_UNDERLINE;
            break;
        case 27:
            vc->vc_attr &= ~VTCONSOLE_ATTR_REVERSE;
            break;
        default:
            break;
        }
    }
}

static void
vt_csi_dispatch(struct vtconsole *vc, int ch)
{
    unsigned count;
    unsigned row;
    unsigned col;

    if (vc->vc_csi_private) {
        if ((ch == 'h' || ch == 'l') && vc->vc_params[0] == 25)
            vc->vc_cursor_visible = ch == 'h';
        return;
    }
    count = vt_param(vc, 0, 1);
    switch (ch) {
    case '@':
        vt_insert_chars(vc, count);
        break;
    case 'A':
        vc->vc_row = count > vc->vc_row ? 0 : vc->vc_row - count;
        break;
    case 'B':
    case 'e':
        vc->vc_row += count;
        vt_clamp_cursor(vc);
        break;
    case 'C':
    case 'a':
        vc->vc_col += count;
        vt_clamp_cursor(vc);
        break;
    case 'D':
        vc->vc_col = count > vc->vc_col ? 0 : vc->vc_col - count;
        break;
    case 'E':
        vc->vc_row += count;
        vc->vc_col = 0;
        vt_clamp_cursor(vc);
        break;
    case 'F':
        vc->vc_row = count > vc->vc_row ? 0 : vc->vc_row - count;
        vc->vc_col = 0;
        break;
    case 'G':
    case '`':
        col = vt_param(vc, 0, 1);
        vc->vc_col = col == 0 ? 0 : col - 1;
        vt_clamp_cursor(vc);
        break;
    case 'H':
    case 'f':
        row = vt_param(vc, 0, 1);
        col = vt_param(vc, 1, 1);
        vc->vc_row = row == 0 ? 0 : row - 1;
        vc->vc_col = col == 0 ? 0 : col - 1;
        vt_clamp_cursor(vc);
        break;
    case 'J':
        vt_erase_display(vc, vc->vc_params[0]);
        break;
    case 'K':
        vt_erase_line(vc, vc->vc_params[0]);
        break;
    case 'L':
        vt_insert_lines(vc, count);
        break;
    case 'M':
        vt_delete_lines(vc, count);
        break;
    case 'P':
        vt_delete_chars(vc, count);
        break;
    case 'X':
        vt_erase_chars(vc, count);
        break;
    case 'd':
        row = vt_param(vc, 0, 1);
        vc->vc_row = row == 0 ? 0 : row - 1;
        vt_clamp_cursor(vc);
        break;
    case 'm':
        vt_sgr(vc);
        break;
    case 's':
        vc->vc_saved_col = vc->vc_col;
        vc->vc_saved_row = vc->vc_row;
        break;
    case 'u':
        vc->vc_col = vc->vc_saved_col;
        vc->vc_row = vc->vc_saved_row;
        vt_clamp_cursor(vc);
        break;
    default:
        break;
    }
}

static void
vt_csi_reset(struct vtconsole *vc)
{
    unsigned i;

    vc->vc_csi_private = 0;
    vc->vc_param_index = 0;
    for (i = 0; i < VTCONSOLE_MAX_PARAMS; ++i)
        vc->vc_params[i] = 0;
}

static void
vt_esc_dispatch(struct vtconsole *vc, int ch)
{
    switch (ch) {
    case '[':
        vt_csi_reset(vc);
        vc->vc_state = VT_STATE_CSI;
        return;
    case ']':
        vc->vc_state = VT_STATE_OSC;
        return;
    case '7':
        vc->vc_saved_col = vc->vc_col;
        vc->vc_saved_row = vc->vc_row;
        break;
    case '8':
        vc->vc_col = vc->vc_saved_col;
        vc->vc_row = vc->vc_saved_row;
        vt_clamp_cursor(vc);
        break;
    case 'c':
        vtconsole_reset(vc);
        break;
    case 'D':
        if (vc->vc_row + 1 >= vc->vc_rows)
            vt_scroll(vc);
        else
            ++vc->vc_row;
        break;
    case 'E':
        vt_newline(vc);
        break;
    case 'M':
        vt_reverse_index(vc);
        break;
    case '(':
    case ')':
    case '*':
    case '+':
        vc->vc_state = VT_STATE_ESC_CHARSET;
        return;
    default:
        break;
    }
    vc->vc_state = VT_STATE_GROUND;
}

void
vtconsole_init(struct vtconsole *vc, unsigned char *cells,
    unsigned char *attrs, unsigned stride, unsigned max_cols,
    unsigned max_rows, const struct vtconsole_ops *ops, void *arg)
{
    unsigned i;

    vc->vc_cells = cells;
    vc->vc_attrs = attrs;
    vc->vc_stride = stride;
    vc->vc_max_cols = max_cols;
    vc->vc_max_rows = max_rows;
    vc->vc_cols = 0;
    vc->vc_rows = 0;
    vc->vc_col = 0;
    vc->vc_row = 0;
    vc->vc_saved_col = 0;
    vc->vc_saved_row = 0;
    vc->vc_state = VT_STATE_GROUND;
    vc->vc_attr = 0;
    vc->vc_param_index = 0;
    vc->vc_csi_private = 0;
    vc->vc_cursor_visible = 1;
    vc->vc_ops = ops;
    vc->vc_arg = arg;
    for (i = 0; i < VTCONSOLE_MAX_PARAMS; ++i)
        vc->vc_params[i] = 0;
}

void
vtconsole_set_geometry(struct vtconsole *vc, unsigned cols, unsigned rows)
{
    if (cols > vc->vc_max_cols)
        cols = vc->vc_max_cols;
    if (rows > vc->vc_max_rows)
        rows = vc->vc_max_rows;
    if (vc->vc_cols == cols && vc->vc_rows == rows)
        return;
    vt_cursor(vc, 0);
    vc->vc_cols = cols;
    vc->vc_rows = rows;
    vtconsole_reset(vc);
}

void
vtconsole_reset(struct vtconsole *vc)
{
    unsigned row;
    unsigned col;

    vc->vc_col = 0;
    vc->vc_row = 0;
    vc->vc_saved_col = 0;
    vc->vc_saved_row = 0;
    vc->vc_state = VT_STATE_GROUND;
    vc->vc_attr = 0;
    vc->vc_cursor_visible = 1;
    vt_csi_reset(vc);
    for (row = 0; row < vc->vc_rows; ++row) {
        for (col = 0; col < vc->vc_cols; ++col) {
            *vt_cell(vc, col, row) = ' ';
            *vt_attr(vc, col, row) = 0;
        }
    }
    vt_render_all(vc);
    vt_cursor(vc, vc->vc_cursor_visible);
}

void
vtconsole_putc(struct vtconsole *vc, int ch)
{
    if (vc->vc_cols == 0 || vc->vc_rows == 0)
        return;
    if (ch == 0x18 || ch == 0x1a) {
        vc->vc_state = VT_STATE_GROUND;
        return;
    }
    vt_cursor(vc, 0);
    switch (vc->vc_state) {
    case VT_STATE_ESC:
        vt_esc_dispatch(vc, ch);
        break;
    case VT_STATE_ESC_CHARSET:
        vc->vc_state = VT_STATE_GROUND;
        break;
    case VT_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            vc->vc_params[vc->vc_param_index] =
                vc->vc_params[vc->vc_param_index] * 10u +
                (unsigned)(ch - '0');
        } else if (ch == ';') {
            if (vc->vc_param_index + 1 < VTCONSOLE_MAX_PARAMS)
                ++vc->vc_param_index;
        } else if (ch == '?') {
            vc->vc_csi_private = 1;
        } else if (ch >= 0x40 && ch <= 0x7e) {
            vt_csi_dispatch(vc, ch);
            vc->vc_state = VT_STATE_GROUND;
        } else if (ch < 0x20) {
            vt_control(vc, ch);
        }
        break;
    case VT_STATE_OSC:
        if (ch == '\007')
            vc->vc_state = VT_STATE_GROUND;
        else if (ch == 0x1b)
            vc->vc_state = VT_STATE_OSC_ESC;
        break;
    case VT_STATE_OSC_ESC:
        vc->vc_state = ch == '\\' ? VT_STATE_GROUND : VT_STATE_OSC;
        break;
    default:
        if (ch == 0x1b)
            vc->vc_state = VT_STATE_ESC;
        else
            vt_control(vc, ch);
        break;
    }
    vt_cursor(vc, vc->vc_cursor_visible);
}
