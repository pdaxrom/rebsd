/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/tty.h>
#include <machine/video.h>

#define CELL_WIDTH              6u
#define CELL_HEIGHT             10u
#define GLYPH_HEIGHT            8u
#define CONSOLE_COLS            96u
#define CONSOLE_ROWS            43u
#define CONSOLE_X               32u
#define CONSOLE_Y               25u
#define CONSOLE_FG              0x00f0f0f0u
#define CONSOLE_BG              0x00000000u
#define CSI_PARAMS              4u

#define STATE_GROUND            0
#define STATE_ESC               1
#define STATE_CSI               2
#define STATE_OSC               3
#define STATE_OSC_ESC           4

static unsigned char cells[CONSOLE_ROWS][CONSOLE_COLS];
static unsigned char attrs[CONSOLE_ROWS][CONSOLE_COLS];
static unsigned cursor_col;
static unsigned cursor_row;
static unsigned saved_col;
static unsigned saved_row;
static unsigned state;
static unsigned attr;
static unsigned params[CSI_PARAMS];
static unsigned param_index;
static int csi_private;
static int cursor_visible = 1;
static int cursor_drawn;
static int console_initialized;

static void
glyph_set(unsigned char glyph[5], unsigned char a, unsigned char b,
    unsigned char c, unsigned char d, unsigned char e)
{
    glyph[0] = a;
    glyph[1] = b;
    glyph[2] = c;
    glyph[3] = d;
    glyph[4] = e;
}

static void
console_glyph(int ch, unsigned char glyph[5])
{
    switch (ch) {
    case ' ': glyph_set(glyph, 0, 0, 0, 0, 0); return;
    case '0': glyph_set(glyph, 0x7e, 0xa1, 0x99, 0x85, 0x7e); return;
    case '1': glyph_set(glyph, 0x84, 0x82, 0xff, 0x80, 0x80); return;
    case '2': glyph_set(glyph, 0xc1, 0xa1, 0x91, 0x89, 0x86); return;
    case '3': glyph_set(glyph, 0x89, 0x89, 0x89, 0x89, 0x76); return;
    case '4': glyph_set(glyph, 0x18, 0x14, 0x12, 0xff, 0x10); return;
    case '5': glyph_set(glyph, 0x8f, 0x89, 0x89, 0x89, 0x71); return;
    case '6': glyph_set(glyph, 0x7e, 0x89, 0x89, 0x89, 0x72); return;
    case '7': glyph_set(glyph, 0x01, 0x81, 0x61, 0x19, 0x07); return;
    case '8': glyph_set(glyph, 0x62, 0x95, 0x89, 0x95, 0x62); return;
    case '9': glyph_set(glyph, 0x4e, 0x91, 0x91, 0x91, 0x7e); return;
    case 'A': glyph_set(glyph, 0xfe, 0x11, 0x11, 0x11, 0xfe); return;
    case 'B': glyph_set(glyph, 0xff, 0x89, 0x89, 0x89, 0x76); return;
    case 'C': glyph_set(glyph, 0x7e, 0x81, 0x81, 0x81, 0x42); return;
    case 'D': glyph_set(glyph, 0xff, 0x81, 0x81, 0x81, 0x7e); return;
    case 'E': glyph_set(glyph, 0xff, 0x89, 0x89, 0x89, 0x81); return;
    case 'F': glyph_set(glyph, 0xff, 0x09, 0x09, 0x09, 0x01); return;
    case 'G': glyph_set(glyph, 0x7e, 0x81, 0x91, 0x91, 0xf2); return;
    case 'H': glyph_set(glyph, 0xff, 0x08, 0x08, 0x08, 0xff); return;
    case 'I': glyph_set(glyph, 0, 0x81, 0xff, 0x81, 0); return;
    case 'J': glyph_set(glyph, 0x40, 0x80, 0x81, 0x7f, 0x01); return;
    case 'K': glyph_set(glyph, 0xff, 0x18, 0x24, 0x42, 0x81); return;
    case 'L': glyph_set(glyph, 0xff, 0x80, 0x80, 0x80, 0x80); return;
    case 'M': glyph_set(glyph, 0xff, 0x02, 0x0c, 0x02, 0xff); return;
    case 'N': glyph_set(glyph, 0xff, 0x06, 0x18, 0x60, 0xff); return;
    case 'O': glyph_set(glyph, 0x7e, 0x81, 0x81, 0x81, 0x7e); return;
    case 'P': glyph_set(glyph, 0xff, 0x11, 0x11, 0x11, 0x0e); return;
    case 'Q': glyph_set(glyph, 0x7e, 0x81, 0xa1, 0x41, 0xbe); return;
    case 'R': glyph_set(glyph, 0xff, 0x11, 0x31, 0x51, 0x8e); return;
    case 'S': glyph_set(glyph, 0x86, 0x89, 0x89, 0x91, 0x61); return;
    case 'T': glyph_set(glyph, 0x01, 0x01, 0xff, 0x01, 0x01); return;
    case 'U': glyph_set(glyph, 0x7f, 0x80, 0x80, 0x80, 0x7f); return;
    case 'V': glyph_set(glyph, 0x1f, 0x60, 0x80, 0x60, 0x1f); return;
    case 'W': glyph_set(glyph, 0xff, 0x40, 0x30, 0x40, 0xff); return;
    case 'X': glyph_set(glyph, 0xc3, 0x24, 0x18, 0x24, 0xc3); return;
    case 'Y': glyph_set(glyph, 0x07, 0x08, 0xf0, 0x08, 0x07); return;
    case 'Z': glyph_set(glyph, 0xc1, 0xa1, 0x99, 0x85, 0x83); return;
    case 'a': glyph_set(glyph, 0x20, 0x54, 0x54, 0x54, 0x78); return;
    case 'b': glyph_set(glyph, 0x7f, 0x48, 0x44, 0x44, 0x38); return;
    case 'c': glyph_set(glyph, 0x38, 0x44, 0x44, 0x44, 0x20); return;
    case 'd': glyph_set(glyph, 0x38, 0x44, 0x44, 0x48, 0x7f); return;
    case 'e': glyph_set(glyph, 0x38, 0x54, 0x54, 0x54, 0x18); return;
    case 'f': glyph_set(glyph, 0x08, 0x7e, 0x09, 0x01, 0x02); return;
    case 'g': glyph_set(glyph, 0x0c, 0x52, 0x52, 0x52, 0x3e); return;
    case 'h': glyph_set(glyph, 0x7f, 0x08, 0x04, 0x04, 0x78); return;
    case 'i': glyph_set(glyph, 0, 0x44, 0x7d, 0x40, 0); return;
    case 'j': glyph_set(glyph, 0x20, 0x40, 0x44, 0x3d, 0); return;
    case 'k': glyph_set(glyph, 0x7f, 0x10, 0x28, 0x44, 0); return;
    case 'l': glyph_set(glyph, 0, 0x41, 0x7f, 0x40, 0); return;
    case 'm': glyph_set(glyph, 0x7c, 0x04, 0x18, 0x04, 0x78); return;
    case 'n': glyph_set(glyph, 0x7c, 0x08, 0x04, 0x04, 0x78); return;
    case 'o': glyph_set(glyph, 0x38, 0x44, 0x44, 0x44, 0x38); return;
    case 'p': glyph_set(glyph, 0x7c, 0x14, 0x14, 0x14, 0x08); return;
    case 'q': glyph_set(glyph, 0x08, 0x14, 0x14, 0x18, 0x7c); return;
    case 'r': glyph_set(glyph, 0x7c, 0x08, 0x04, 0x04, 0x08); return;
    case 's': glyph_set(glyph, 0x48, 0x54, 0x54, 0x54, 0x20); return;
    case 't': glyph_set(glyph, 0x04, 0x3f, 0x44, 0x40, 0x20); return;
    case 'u': glyph_set(glyph, 0x3c, 0x40, 0x40, 0x20, 0x7c); return;
    case 'v': glyph_set(glyph, 0x1c, 0x20, 0x40, 0x20, 0x1c); return;
    case 'w': glyph_set(glyph, 0x3c, 0x40, 0x30, 0x40, 0x3c); return;
    case 'x': glyph_set(glyph, 0x44, 0x28, 0x10, 0x28, 0x44); return;
    case 'y': glyph_set(glyph, 0x0c, 0x50, 0x50, 0x50, 0x3c); return;
    case 'z': glyph_set(glyph, 0x44, 0x64, 0x54, 0x4c, 0x44); return;
    case '.': glyph_set(glyph, 0, 0x60, 0x60, 0, 0); return;
    case ',': glyph_set(glyph, 0, 0xa0, 0x60, 0, 0); return;
    case ':': glyph_set(glyph, 0, 0x36, 0x36, 0, 0); return;
    case ';': glyph_set(glyph, 0, 0xb6, 0x76, 0, 0); return;
    case '!': glyph_set(glyph, 0, 0, 0x5f, 0, 0); return;
    case '?': glyph_set(glyph, 0x02, 0x01, 0xb1, 0x09, 0x06); return;
    case '-': glyph_set(glyph, 0x08, 0x08, 0x08, 0x08, 0x08); return;
    case '_': glyph_set(glyph, 0x80, 0x80, 0x80, 0x80, 0x80); return;
    case '+': glyph_set(glyph, 0x08, 0x08, 0x3e, 0x08, 0x08); return;
    case '=': glyph_set(glyph, 0x24, 0x24, 0x24, 0x24, 0x24); return;
    case '/': glyph_set(glyph, 0xc0, 0x30, 0x0c, 0x03, 0); return;
    case '\\': glyph_set(glyph, 0x03, 0x0c, 0x30, 0xc0, 0); return;
    case '|': glyph_set(glyph, 0, 0, 0xff, 0, 0); return;
    case '(': glyph_set(glyph, 0, 0x3c, 0x42, 0x81, 0); return;
    case ')': glyph_set(glyph, 0, 0x81, 0x42, 0x3c, 0); return;
    case '[': glyph_set(glyph, 0, 0xff, 0x81, 0x81, 0); return;
    case ']': glyph_set(glyph, 0, 0x81, 0x81, 0xff, 0); return;
    case '<': glyph_set(glyph, 0x08, 0x14, 0x22, 0x41, 0x80); return;
    case '>': glyph_set(glyph, 0x80, 0x41, 0x22, 0x14, 0x08); return;
    case '#': glyph_set(glyph, 0x24, 0xff, 0x24, 0xff, 0x24); return;
    case '*': glyph_set(glyph, 0x22, 0x14, 0x7f, 0x14, 0x22); return;
    case '\'': glyph_set(glyph, 0, 0, 0x07, 0, 0); return;
    case '"': glyph_set(glyph, 0, 0x07, 0, 0x07, 0); return;
    case '@': glyph_set(glyph, 0x7e, 0x81, 0xbd, 0xa5, 0x1e); return;
    case '$': glyph_set(glyph, 0x44, 0x8a, 0xff, 0x91, 0x62); return;
    case '%': glyph_set(glyph, 0x47, 0x25, 0x18, 0xa4, 0xe2); return;
    case '&': glyph_set(glyph, 0x76, 0x89, 0x95, 0x62, 0xd0); return;
    default: glyph_set(glyph, 0x02, 0x01, 0xb1, 0x09, 0x06); return;
    }
}

static void
render_cell(unsigned col, unsigned row, int invert)
{
    volatile unsigned *fb;
    unsigned char glyph[5];
    unsigned fg;
    unsigned bg;
    unsigned x;
    unsigned y;
    unsigned x0;
    unsigned y0;
    int ch;

    if (col >= CONSOLE_COLS || row >= CONSOLE_ROWS)
        return;
    ch = cells[row][col] ? cells[row][col] : ' ';
    fg = CONSOLE_FG;
    bg = CONSOLE_BG;
    if (((attrs[row][col] & 1u) != 0) ^ invert) {
        unsigned swap = fg;
        fg = bg;
        bg = swap;
    }
    fb = ci20_video_framebuffer();
    x0 = CONSOLE_X + col * CELL_WIDTH;
    y0 = CONSOLE_Y + row * CELL_HEIGHT;
    for (y = 0; y < CELL_HEIGHT; ++y)
        for (x = 0; x < CELL_WIDTH; ++x)
            fb[(y0 + y) * 640u + x0 + x] = bg;
    if (ch == ' ')
        return;
    console_glyph(ch, glyph);
    for (x = 0; x < 5; ++x)
        for (y = 0; y < GLYPH_HEIGHT; ++y)
            if ((glyph[x] & (1u << y)) != 0)
                fb[(y0 + y) * 640u + x0 + x] = fg;
}

static void
render_all(void)
{
    unsigned row;
    unsigned col;

    for (row = 0; row < CONSOLE_ROWS; ++row)
        for (col = 0; col < CONSOLE_COLS; ++col)
            render_cell(col, row, 0);
}

static void
erase_cursor(void)
{
    if (cursor_drawn) {
        render_cell(cursor_col, cursor_row, 0);
        cursor_drawn = 0;
    }
}

static void
draw_cursor(void)
{
    if (cursor_visible && !cursor_drawn) {
        render_cell(cursor_col, cursor_row, 1);
        cursor_drawn = 1;
    }
}

static void
clear_range(unsigned row, unsigned first, unsigned end)
{
    unsigned col;

    if (row >= CONSOLE_ROWS || first >= CONSOLE_COLS)
        return;
    if (end > CONSOLE_COLS)
        end = CONSOLE_COLS;
    for (col = first; col < end; ++col) {
        cells[row][col] = ' ';
        attrs[row][col] = attr;
        render_cell(col, row, 0);
    }
}

static void
scroll(void)
{
    unsigned row;
    unsigned col;

    for (row = 0; row + 1 < CONSOLE_ROWS; ++row)
        for (col = 0; col < CONSOLE_COLS; ++col) {
            cells[row][col] = cells[row + 1][col];
            attrs[row][col] = attrs[row + 1][col];
        }
    for (col = 0; col < CONSOLE_COLS; ++col) {
        cells[CONSOLE_ROWS - 1][col] = ' ';
        attrs[CONSOLE_ROWS - 1][col] = attr;
    }
    render_all();
}

static void
newline(void)
{
    cursor_col = 0;
    if (++cursor_row >= CONSOLE_ROWS) {
        scroll();
        cursor_row = CONSOLE_ROWS - 1;
    }
}

static void
clamp_cursor(void)
{
    if (cursor_col >= CONSOLE_COLS)
        cursor_col = CONSOLE_COLS - 1;
    if (cursor_row >= CONSOLE_ROWS)
        cursor_row = CONSOLE_ROWS - 1;
}

static unsigned
csi_param(unsigned index, unsigned default_value)
{
    if (index >= CSI_PARAMS || params[index] == 0)
        return default_value;
    return params[index];
}

static void
csi_dispatch(int ch)
{
    unsigned count;
    unsigned row;

    if (csi_private) {
        if ((ch == 'h' || ch == 'l') && params[0] == 25)
            cursor_visible = ch == 'h';
        return;
    }
    count = csi_param(0, 1);
    switch (ch) {
    case 'A':
        cursor_row = count > cursor_row ? 0 : cursor_row - count;
        break;
    case 'B':
        cursor_row += count;
        clamp_cursor();
        break;
    case 'C':
        cursor_col += count;
        clamp_cursor();
        break;
    case 'D':
        cursor_col = count > cursor_col ? 0 : cursor_col - count;
        break;
    case 'H':
    case 'f':
        cursor_row = csi_param(0, 1) - 1;
        cursor_col = csi_param(1, 1) - 1;
        clamp_cursor();
        break;
    case 'J':
        if (params[0] == 2 || params[0] == 3) {
            for (row = 0; row < CONSOLE_ROWS; ++row)
                clear_range(row, 0, CONSOLE_COLS);
        } else {
            clear_range(cursor_row, cursor_col, CONSOLE_COLS);
            for (row = cursor_row + 1; row < CONSOLE_ROWS; ++row)
                clear_range(row, 0, CONSOLE_COLS);
        }
        break;
    case 'K':
        if (params[0] == 2)
            clear_range(cursor_row, 0, CONSOLE_COLS);
        else if (params[0] == 1)
            clear_range(cursor_row, 0, cursor_col + 1);
        else
            clear_range(cursor_row, cursor_col, CONSOLE_COLS);
        break;
    case 'm':
        attr = params[0] == 7 ? 1u : 0u;
        break;
    case 's':
        saved_col = cursor_col;
        saved_row = cursor_row;
        break;
    case 'u':
        cursor_col = saved_col;
        cursor_row = saved_row;
        clamp_cursor();
        break;
    default:
        break;
    }
}

static void
put_character(int ch)
{
    unsigned i;

    if (state == STATE_ESC) {
        if (ch == '[') {
            for (i = 0; i < CSI_PARAMS; ++i)
                params[i] = 0;
            param_index = 0;
            csi_private = 0;
            state = STATE_CSI;
        } else if (ch == ']') {
            state = STATE_OSC;
        } else {
            state = STATE_GROUND;
        }
        return;
    }
    if (state == STATE_CSI) {
        if (ch >= '0' && ch <= '9') {
            params[param_index] = params[param_index] * 10u +
                (unsigned)(ch - '0');
        } else if (ch == ';' && param_index + 1 < CSI_PARAMS) {
            ++param_index;
        } else if (ch == '?') {
            csi_private = 1;
        } else if (ch >= 0x40 && ch <= 0x7e) {
            csi_dispatch(ch);
            state = STATE_GROUND;
        }
        return;
    }
    if (state == STATE_OSC) {
        if (ch == '\007')
            state = STATE_GROUND;
        else if (ch == 0x1b)
            state = STATE_OSC_ESC;
        return;
    }
    if (state == STATE_OSC_ESC) {
        state = ch == '\\' ? STATE_GROUND : STATE_OSC;
        return;
    }
    if (ch == 0x1b) {
        state = STATE_ESC;
    } else if (ch == '\r') {
        cursor_col = 0;
    } else if (ch == '\n' || ch == '\013' || ch == '\014') {
        newline();
    } else if (ch == '\b') {
        if (cursor_col != 0)
            --cursor_col;
    } else if (ch == '\t') {
        do {
            put_character(' ');
        } while ((cursor_col & 7u) != 0);
    } else if (ch >= 0x20 && ch < 0x7f) {
        cells[cursor_row][cursor_col] = ch;
        attrs[cursor_row][cursor_col] = attr;
        render_cell(cursor_col, cursor_row, 0);
        if (++cursor_col >= CONSOLE_COLS)
            newline();
    }
}

static void
console_init(void)
{
    unsigned row;
    unsigned col;

    if (console_initialized || !ci20_video_ready())
        return;
    for (row = 0; row < CONSOLE_ROWS; ++row)
        for (col = 0; col < CONSOLE_COLS; ++col) {
            cells[row][col] = ' ';
            attrs[row][col] = 0;
        }
    ci20_video_clear(CONSOLE_BG);
    console_initialized = 1;
}

void
ci20_video_console_putc(int ch)
{
    console_init();
    if (!console_initialized)
        return;
    erase_cursor();
    put_character(ch);
    draw_cursor();
}

void
ci20_video_console_winsize(struct winsize *ws)
{
    ws->ws_row = CONSOLE_ROWS;
    ws->ws_col = CONSOLE_COLS;
    ws->ws_xpixel = CONSOLE_COLS * CELL_WIDTH;
    ws->ws_ypixel = CONSOLE_ROWS * CELL_HEIGHT;
}

void
ci20_video_console_tty_winsize(struct tty *tp)
{
    ci20_video_console_winsize(&tp->t_winsize);
}
