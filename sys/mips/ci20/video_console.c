/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <console/vtconsole.h>
#include <sys/param.h>
#include <sys/tty.h>
#include <machine/video.h>

#define CELL_WIDTH              6u
#define CELL_HEIGHT             10u
#define GLYPH_HEIGHT            8u
#define CELL_SCALE              2u
#define CELL_PIXEL_WIDTH        (CELL_WIDTH * CELL_SCALE)
#define CELL_PIXEL_HEIGHT       (CELL_HEIGHT * CELL_SCALE)
#define CONSOLE_MAX_COLS        154u
#define CONSOLE_MAX_ROWS        92u
#define CONSOLE_MARGIN_DIV      20u
#define CONSOLE_FG              0x00f0f0f0u
#define CONSOLE_BG              0x00000000u
static unsigned char cells[CONSOLE_MAX_ROWS][CONSOLE_MAX_COLS];
static unsigned char attrs[CONSOLE_MAX_ROWS][CONSOLE_MAX_COLS];
static struct vtconsole console_vt;
static unsigned console_width;
static unsigned console_height;
static unsigned console_stride;
static unsigned console_x0;
static unsigned console_y0;
static unsigned console_cols;
static unsigned console_rows;
static int console_initialized;

static void vt_render_cell(void *, unsigned, unsigned, int);
static void vt_render_all(void *);
static void vt_cursor(void *, unsigned, unsigned, int);

static const struct vtconsole_ops console_vt_ops = {
    vt_render_cell,
    vt_render_all,
    vt_cursor,
    0
};

static void
console_geometry(void)
{
    struct drmfb_info info;
    unsigned margin_x;
    unsigned margin_y;

    ci20_video_get_info(&info);
    if (info.width == console_width && info.height == console_height &&
        info.stride / sizeof(unsigned) == console_stride &&
        console_cols != 0 && console_rows != 0)
        return;
    console_width = info.width;
    console_height = info.height;
    console_stride = info.stride / sizeof(unsigned);
    margin_x = console_width / CONSOLE_MARGIN_DIV;
    margin_y = console_height / CONSOLE_MARGIN_DIV;
    console_cols = (console_width - 2u * margin_x) / CELL_PIXEL_WIDTH;
    console_rows = (console_height - 2u * margin_y) / CELL_PIXEL_HEIGHT;
    if (console_cols > CONSOLE_MAX_COLS)
        console_cols = CONSOLE_MAX_COLS;
    if (console_rows > CONSOLE_MAX_ROWS)
        console_rows = CONSOLE_MAX_ROWS;
    console_x0 = (console_width - console_cols * CELL_PIXEL_WIDTH) / 2u;
    console_y0 = (console_height - console_rows * CELL_PIXEL_HEIGHT) / 2u;
    if (console_initialized)
        vtconsole_set_geometry(&console_vt, console_cols, console_rows);
}

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
    unsigned sx;
    unsigned sy;
    int ch;

    if (col >= console_cols || row >= console_rows)
        return;
    ch = cells[row][col] ? cells[row][col] : ' ';
    fg = CONSOLE_FG;
    bg = CONSOLE_BG;
    if (((attrs[row][col] & VTCONSOLE_ATTR_REVERSE) != 0) ^ invert) {
        unsigned swap = fg;
        fg = bg;
        bg = swap;
    }
    fb = ci20_video_framebuffer();
    if (console_stride < console_width)
        return;
    x0 = console_x0 + col * CELL_PIXEL_WIDTH;
    y0 = console_y0 + row * CELL_PIXEL_HEIGHT;
    for (y = 0; y < CELL_PIXEL_HEIGHT; ++y)
        for (x = 0; x < CELL_PIXEL_WIDTH; ++x)
            fb[(y0 + y) * console_stride + x0 + x] = bg;
    if (ch == ' ')
        return;
    console_glyph(ch, glyph);
    for (x = 0; x < 5; ++x)
        for (y = 0; y < GLYPH_HEIGHT; ++y)
            if ((glyph[x] & (1u << y)) != 0)
                for (sy = 0; sy < CELL_SCALE; ++sy)
                    for (sx = 0; sx < CELL_SCALE; ++sx)
                        fb[(y0 + y * CELL_SCALE + sy) *
                            console_stride + x0 +
                            x * CELL_SCALE + sx] = fg;
}

static void
render_all(void)
{
    unsigned row;
    unsigned col;

    for (row = 0; row < console_rows; ++row)
        for (col = 0; col < console_cols; ++col)
            render_cell(col, row, 0);
}

static void
vt_render_cell(void *arg, unsigned col, unsigned row, int invert)
{
    (void)arg;
    render_cell(col, row, invert);
}

static void
vt_render_all(void *arg)
{
    (void)arg;
    render_all();
}

static void
vt_cursor(void *arg, unsigned col, unsigned row, int visible)
{
    (void)arg;
    render_cell(col, row, visible);
}

static void
console_init(void)
{
    if (console_initialized || !ci20_video_ready())
        return;
    console_geometry();
    ci20_video_clear(CONSOLE_BG);
    vtconsole_init(&console_vt, &cells[0][0], &attrs[0][0],
        CONSOLE_MAX_COLS, CONSOLE_MAX_COLS, CONSOLE_MAX_ROWS,
        &console_vt_ops, 0);
    console_initialized = 1;
    vtconsole_set_geometry(&console_vt, console_cols, console_rows);
}

void
ci20_video_console_putc(int ch)
{
    console_init();
    if (!console_initialized)
        return;
    console_geometry();
    vtconsole_putc(&console_vt, ch);
}

void
ci20_video_console_mode_changed(void)
{
    unsigned old_cols;
    unsigned old_rows;

    if (!console_initialized)
        return;
    old_cols = console_vt.vc_cols;
    old_rows = console_vt.vc_rows;
    console_width = 0;
    console_height = 0;
    console_stride = 0;
    ci20_video_clear(CONSOLE_BG);
    console_geometry();
    if (old_cols == console_cols && old_rows == console_rows)
        vtconsole_reset(&console_vt);
}

void
ci20_video_console_winsize(struct winsize *ws)
{
    console_geometry();
    ws->ws_row = console_rows;
    ws->ws_col = console_cols;
    ws->ws_xpixel = console_cols * CELL_PIXEL_WIDTH;
    ws->ws_ypixel = console_rows * CELL_PIXEL_HEIGHT;
}

void
ci20_video_console_tty_winsize(struct tty *tp)
{
    ci20_video_console_winsize(&tp->t_winsize);
}
