#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <machine/console.h>
#include <machine/joybus.h>
#include <machine/video.h>

#define N64_CONSOLE_CELL_W      6
#define N64_CONSOLE_CELL_H      10
#define N64_CONSOLE_GLYPH_H     8
#define N64_CONSOLE_FG16        0xffffu
#define N64_CONSOLE_BG16        0x0001u
#define N64_CONSOLE_FG32        0xffffffffu
#define N64_CONSOLE_BG32        0x000000ffu
#define N64_CONSOLE_MAX_COLS    96
#define N64_CONSOLE_MAX_ROWS    43
#define N64_CONSOLE_CSI_PARAMS  8

#define N64_CONSOLE_ATTR_BOLD      0x01
#define N64_CONSOLE_ATTR_UNDERLINE 0x02
#define N64_CONSOLE_ATTR_REVERSE   0x04

#define N64_CONSOLE_STATE_GROUND       0
#define N64_CONSOLE_STATE_ESC          1
#define N64_CONSOLE_STATE_ESC_CHARSET  2
#define N64_CONSOLE_STATE_CSI          3
#define N64_CONSOLE_STATE_OSC          4
#define N64_CONSOLE_STATE_OSC_ESC      5

static unsigned console_mode = ~0u;
static unsigned console_width;
static unsigned console_height;
static unsigned console_stride;
static unsigned console_bpp;
static unsigned console_fg;
static unsigned console_bg;
static unsigned console_x0;
static unsigned console_y0;
static unsigned console_cols;
static unsigned console_rows;
static unsigned console_col;
static unsigned console_row;
static unsigned console_cursor_col;
static unsigned console_cursor_row;
static int console_cursor_drawn;
static int console_cursor_enabled;
static unsigned console_saved_col;
static unsigned console_saved_row;
static unsigned console_state;
static unsigned console_attr;
static int console_csi_private;
static unsigned console_csi_count;
static unsigned console_csi_params[N64_CONSOLE_CSI_PARAMS];
static unsigned char console_cells[N64_CONSOLE_MAX_ROWS][N64_CONSOLE_MAX_COLS];
static unsigned char console_attrs[N64_CONSOLE_MAX_ROWS][N64_CONSOLE_MAX_COLS];
static int console_panic_mirror;

static void n64_console_reset_screen(void);
static void n64_console_render_all(void);
static void n64_console_render_cell(unsigned col, unsigned row, int invert);

void __attribute__((weak))
n64_console_debug_putc(int ch)
{
    (void)ch;
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
n64_console_glyph(int ch, unsigned char glyph[5])
{
    switch (ch) {
    case ' ': glyph_set(glyph, 0x00, 0x00, 0x00, 0x00, 0x00); return;
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
    case 'C': glyph_set(glyph, 0x7e, 0x81, 0x81, 0x81, 0x81); return;
    case 'D': glyph_set(glyph, 0xff, 0x81, 0x81, 0x81, 0x7e); return;
    case 'E': glyph_set(glyph, 0xff, 0x89, 0x89, 0x89, 0x89); return;
    case 'F': glyph_set(glyph, 0xff, 0x09, 0x09, 0x09, 0x09); return;
    case 'G': glyph_set(glyph, 0x7e, 0x81, 0x91, 0x51, 0xf1); return;
    case 'H': glyph_set(glyph, 0xff, 0x08, 0x08, 0x08, 0xff); return;
    case 'I': glyph_set(glyph, 0x00, 0x81, 0xff, 0x81, 0x00); return;
    case 'J': glyph_set(glyph, 0x40, 0x80, 0x80, 0x80, 0x7f); return;
    case 'K': glyph_set(glyph, 0xff, 0x08, 0x14, 0x22, 0xc1); return;
    case 'L': glyph_set(glyph, 0xff, 0x80, 0x80, 0x80, 0x80); return;
    case 'M': glyph_set(glyph, 0xff, 0x02, 0x04, 0x02, 0xff); return;
    case 'N': glyph_set(glyph, 0xff, 0x06, 0x18, 0x60, 0xff); return;
    case 'O': glyph_set(glyph, 0x7e, 0x81, 0x81, 0x81, 0x7e); return;
    case 'P': glyph_set(glyph, 0xff, 0x11, 0x11, 0x11, 0x0e); return;
    case 'Q': glyph_set(glyph, 0x7e, 0x81, 0xa1, 0xc1, 0xfe); return;
    case 'R': glyph_set(glyph, 0xff, 0x11, 0x11, 0x11, 0xee); return;
    case 'S': glyph_set(glyph, 0x86, 0x89, 0x89, 0x89, 0x71); return;
    case 'T': glyph_set(glyph, 0x01, 0x01, 0xff, 0x01, 0x01); return;
    case 'U': glyph_set(glyph, 0x7f, 0x80, 0x80, 0x80, 0x7f); return;
    case 'V': glyph_set(glyph, 0x1f, 0x60, 0x80, 0x60, 0x1f); return;
    case 'W': glyph_set(glyph, 0xff, 0x40, 0x20, 0x40, 0xff); return;
    case 'X': glyph_set(glyph, 0xc7, 0x28, 0x10, 0x28, 0xc7); return;
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
    case 'i': glyph_set(glyph, 0x00, 0x44, 0x7d, 0x40, 0x00); return;
    case 'j': glyph_set(glyph, 0x20, 0x40, 0x44, 0x3d, 0x00); return;
    case 'k': glyph_set(glyph, 0x7f, 0x10, 0x28, 0x44, 0x00); return;
    case 'l': glyph_set(glyph, 0x00, 0x41, 0x7f, 0x40, 0x00); return;
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
    case '.': glyph_set(glyph, 0x00, 0x60, 0x60, 0x00, 0x00); return;
    case ',': glyph_set(glyph, 0x00, 0xa0, 0x60, 0x00, 0x00); return;
    case ':': glyph_set(glyph, 0x00, 0x36, 0x36, 0x00, 0x00); return;
    case ';': glyph_set(glyph, 0x00, 0xb6, 0x76, 0x00, 0x00); return;
    case '!': glyph_set(glyph, 0x00, 0x00, 0x5f, 0x00, 0x00); return;
    case '?': glyph_set(glyph, 0x02, 0x01, 0xb1, 0x09, 0x06); return;
    case '-': glyph_set(glyph, 0x08, 0x08, 0x08, 0x08, 0x08); return;
    case '_': glyph_set(glyph, 0x80, 0x80, 0x80, 0x80, 0x80); return;
    case '+': glyph_set(glyph, 0x08, 0x08, 0x3e, 0x08, 0x08); return;
    case '=': glyph_set(glyph, 0x24, 0x24, 0x24, 0x24, 0x24); return;
    case '/': glyph_set(glyph, 0xc0, 0x30, 0x0c, 0x03, 0x00); return;
    case '\\': glyph_set(glyph, 0x03, 0x0c, 0x30, 0xc0, 0x00); return;
    case '|': glyph_set(glyph, 0x00, 0x00, 0xff, 0x00, 0x00); return;
    case '(': glyph_set(glyph, 0x00, 0x3c, 0x42, 0x81, 0x00); return;
    case ')': glyph_set(glyph, 0x00, 0x81, 0x42, 0x3c, 0x00); return;
    case '[': glyph_set(glyph, 0x00, 0xff, 0x81, 0x81, 0x00); return;
    case ']': glyph_set(glyph, 0x00, 0x81, 0x81, 0xff, 0x00); return;
    case '<': glyph_set(glyph, 0x08, 0x14, 0x22, 0x41, 0x80); return;
    case '>': glyph_set(glyph, 0x80, 0x41, 0x22, 0x14, 0x08); return;
    case '#': glyph_set(glyph, 0x24, 0xff, 0x24, 0xff, 0x24); return;
    case '*': glyph_set(glyph, 0x22, 0x14, 0x7f, 0x14, 0x22); return;
    case '^': glyph_set(glyph, 0x04, 0x02, 0x01, 0x02, 0x04); return;
    case '\'': glyph_set(glyph, 0x00, 0x00, 0x07, 0x00, 0x00); return;
    case '"': glyph_set(glyph, 0x00, 0x07, 0x00, 0x07, 0x00); return;
    case '@': glyph_set(glyph, 0x7e, 0x81, 0xbd, 0xa5, 0x1e); return;
    case '$': glyph_set(glyph, 0x44, 0x8a, 0xff, 0x91, 0x62); return;
    case '%': glyph_set(glyph, 0x47, 0x25, 0x18, 0xa4, 0xe2); return;
    case '&': glyph_set(glyph, 0x76, 0x89, 0x95, 0x62, 0xd0); return;
    default: glyph_set(glyph, 0x02, 0x01, 0xb1, 0x09, 0x06); return;
    }
}

static void
n64_console_geometry(void)
{
    struct n64_video_info info;

    n64_video_get_info(&info);
    if (info.width == 0 || info.height == 0 ||
        (info.bpp != 16 && info.bpp != 32)) {
        console_cols = 0;
        console_rows = 0;
        return;
    }
    if (info.mode == console_mode)
        return;

    console_mode = info.mode;
    console_width = info.width;
    console_height = info.height;
    console_stride = info.stride;
    console_bpp = info.bpp;
    console_fg = console_bpp == 32 ?
        N64_CONSOLE_FG32 : N64_CONSOLE_FG16;
    console_bg = console_bpp == 32 ?
        N64_CONSOLE_BG32 : N64_CONSOLE_BG16;
    console_x0 = console_width / 20;
    console_y0 = console_height / 20;
    console_cols = (console_width - 2 * console_x0) / N64_CONSOLE_CELL_W;
    console_rows = (console_height - 2 * console_y0) / N64_CONSOLE_CELL_H;
    if (console_cols > N64_CONSOLE_MAX_COLS)
        console_cols = N64_CONSOLE_MAX_COLS;
    if (console_rows > N64_CONSOLE_MAX_ROWS)
        console_rows = N64_CONSOLE_MAX_ROWS;
    n64_console_reset_screen();
}

static void
n64_console_put_pixel(unsigned x, unsigned y, unsigned color)
{
    volatile unsigned char *fb;

    fb = (volatile unsigned char *)n64_video_framebuffer();
    if (console_bpp == 32)
        *(volatile unsigned *)(fb + y * console_stride + x * 4u) = color;
    else
        *(volatile unsigned short *)(fb + y * console_stride + x * 2u) =
            (unsigned short)color;
}

static void
n64_console_fill_cell(unsigned col, unsigned row, unsigned color)
{
    unsigned x0;
    unsigned y0;
    unsigned x;
    unsigned y;

    if (col >= console_cols || row >= console_rows)
        return;

    x0 = console_x0 + col * N64_CONSOLE_CELL_W;
    y0 = console_y0 + row * N64_CONSOLE_CELL_H;
    for (y = 0; y < N64_CONSOLE_CELL_H; ++y)
        for (x = 0; x < N64_CONSOLE_CELL_W; ++x)
            n64_console_put_pixel(x0 + x, y0 + y, color);
}

static void
n64_console_clear_area(void)
{
    unsigned x;
    unsigned y;
    unsigned x1;
    unsigned y1;

    if (console_cols == 0 || console_rows == 0)
        return;

    x1 = console_x0 + console_cols * N64_CONSOLE_CELL_W;
    y1 = console_y0 + console_rows * N64_CONSOLE_CELL_H;
    for (y = console_y0; y < y1; ++y)
        for (x = console_x0; x < x1; ++x)
            n64_console_put_pixel(x, y, console_bg);
}

static void
n64_console_render_char(unsigned col, unsigned row, int ch, unsigned char attr,
    int invert)
{
    unsigned char glyph[5];
    unsigned fg;
    unsigned bg;
    unsigned tmp;
    unsigned x0;
    unsigned y0;
    unsigned x;
    unsigned y;

    if (col >= console_cols || row >= console_rows)
        return;

    fg = console_fg;
    bg = console_bg;
    if (((attr & N64_CONSOLE_ATTR_REVERSE) != 0) ^ (invert != 0)) {
        tmp = fg;
        fg = bg;
        bg = tmp;
    }

    n64_console_fill_cell(col, row, bg);
    if (ch == ' ')
        return;

    n64_console_glyph(ch, glyph);
    x0 = console_x0 + col * N64_CONSOLE_CELL_W;
    y0 = console_y0 + row * N64_CONSOLE_CELL_H;
    for (x = 0; x < 5; ++x) {
        for (y = 0; y < N64_CONSOLE_GLYPH_H; ++y) {
            if ((glyph[x] & (1u << y)) == 0)
                continue;
            n64_console_put_pixel(x0 + x, y0 + y, fg);
            if ((attr & N64_CONSOLE_ATTR_BOLD) != 0 && x + 1 < 5)
                n64_console_put_pixel(x0 + x + 1, y0 + y, fg);
        }
    }

    if ((attr & N64_CONSOLE_ATTR_UNDERLINE) != 0) {
        y = N64_CONSOLE_GLYPH_H;
        for (x = 0; x < N64_CONSOLE_CELL_W; ++x)
            n64_console_put_pixel(x0 + x, y0 + y, fg);
    }
}

static void
n64_console_render_cell(unsigned col, unsigned row, int invert)
{
    int ch;

    if (col >= console_cols || row >= console_rows)
        return;
    ch = console_cells[row][col];
    if (ch == 0)
        ch = ' ';
    n64_console_render_char(col, row, ch, console_attrs[row][col], invert);
}

static void
n64_console_render_row(unsigned row)
{
    unsigned col;

    if (row >= console_rows)
        return;
    for (col = 0; col < console_cols; ++col)
        n64_console_render_cell(col, row, 0);
}

static void
n64_console_render_all(void)
{
    unsigned row;

    for (row = 0; row < console_rows; ++row)
        n64_console_render_row(row);
}

static void
n64_console_reset_screen(void)
{
    unsigned row;
    unsigned col;

    console_col = 0;
    console_row = 0;
    console_saved_col = 0;
    console_saved_row = 0;
    console_cursor_drawn = 0;
    console_cursor_enabled = 1;
    console_state = N64_CONSOLE_STATE_GROUND;
    console_attr = 0;
    console_csi_private = 0;
    console_csi_count = 0;

    for (row = 0; row < N64_CONSOLE_MAX_ROWS; ++row) {
        for (col = 0; col < N64_CONSOLE_MAX_COLS; ++col) {
            console_cells[row][col] = ' ';
            console_attrs[row][col] = 0;
        }
    }
    n64_console_clear_area();
}

static void
n64_console_erase_cursor(void)
{
    if (!console_cursor_drawn)
        return;
    n64_console_render_cell(console_cursor_col, console_cursor_row, 0);
    console_cursor_drawn = 0;
}

static void
n64_console_draw_cursor(void)
{
    if (console_cursor_drawn || !console_cursor_enabled ||
        console_cols == 0 || console_rows == 0)
        return;
    console_cursor_col = console_col;
    console_cursor_row = console_row;
    n64_console_render_cell(console_cursor_col, console_cursor_row, 1);
    console_cursor_drawn = 1;
}

static void
n64_console_clamp_cursor(void)
{
    if (console_cols == 0 || console_rows == 0) {
        console_col = 0;
        console_row = 0;
        return;
    }
    if (console_col >= console_cols)
        console_col = console_cols - 1;
    if (console_row >= console_rows)
        console_row = console_rows - 1;
}

static void
n64_console_clear_range(unsigned row, unsigned first_col, unsigned last_col)
{
    unsigned col;

    if (row >= console_rows || first_col >= console_cols)
        return;
    if (last_col > console_cols)
        last_col = console_cols;
    for (col = first_col; col < last_col; ++col) {
        console_cells[row][col] = ' ';
        console_attrs[row][col] = console_attr;
        n64_console_render_cell(col, row, 0);
    }
}

static void
n64_console_scroll(void)
{
    unsigned row;
    unsigned col;

    if (console_rows == 0 || console_cols == 0)
        return;

    for (row = 0; row + 1 < console_rows; ++row) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = console_cells[row + 1][col];
            console_attrs[row][col] = console_attrs[row + 1][col];
        }
    }
    for (col = 0; col < console_cols; ++col) {
        console_cells[console_rows - 1][col] = ' ';
        console_attrs[console_rows - 1][col] = console_attr;
    }
    n64_console_render_all();
}

static void
n64_console_reverse_index(void)
{
    unsigned row;
    unsigned col;

    if (console_rows == 0 || console_cols == 0)
        return;
    if (console_row > 0) {
        --console_row;
        return;
    }

    row = console_rows;
    while (row-- > 1) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = console_cells[row - 1][col];
            console_attrs[row][col] = console_attrs[row - 1][col];
        }
    }
    for (col = 0; col < console_cols; ++col) {
        console_cells[0][col] = ' ';
        console_attrs[0][col] = console_attr;
    }
    n64_console_render_all();
}

static void
n64_console_newline(void)
{
    console_col = 0;
    if (console_rows == 0)
        return;
    if (++console_row >= console_rows) {
        n64_console_scroll();
        console_row = console_rows - 1;
    }
}

static void
n64_console_put_printable(int ch)
{
    if (console_cols == 0 || console_rows == 0)
        return;

    console_cells[console_row][console_col] = ch;
    console_attrs[console_row][console_col] = console_attr;
    n64_console_render_cell(console_col, console_row, 0);

    if (++console_col >= console_cols)
        n64_console_newline();
}

static void
n64_console_control(int ch)
{
    switch (ch) {
    case '\r':
        console_col = 0;
        break;
    case '\n':
    case '\013':
    case '\014':
        n64_console_newline();
        break;
    case '\t':
        do {
            n64_console_put_printable(' ');
        } while (console_cols != 0 && (console_col & 7u) != 0);
        break;
    case '\b':
        if (console_col > 0)
            console_col--;
        break;
    case '\007':
    case 0x7f:
        break;
    default:
        if (ch >= 0x20 && ch < 0x7f)
            n64_console_put_printable(ch);
        break;
    }
}

static void
n64_console_save_cursor(void)
{
    console_saved_col = console_col;
    console_saved_row = console_row;
}

static void
n64_console_restore_cursor(void)
{
    console_col = console_saved_col;
    console_row = console_saved_row;
    n64_console_clamp_cursor();
}

static void
n64_console_csi_reset(void)
{
    unsigned i;

    console_csi_private = 0;
    console_csi_count = 0;
    for (i = 0; i < N64_CONSOLE_CSI_PARAMS; ++i)
        console_csi_params[i] = 0;
}

static unsigned
n64_console_csi_param(unsigned idx, unsigned def)
{
    if (idx >= N64_CONSOLE_CSI_PARAMS || console_csi_params[idx] == 0)
        return def;
    return console_csi_params[idx];
}

static void
n64_console_move_cursor(unsigned row, unsigned col)
{
    console_row = row;
    console_col = col;
    n64_console_clamp_cursor();
}

static void
n64_console_erase_display(unsigned mode)
{
    unsigned row;

    switch (mode) {
    case 1:
        for (row = 0; row < console_row && row < console_rows; ++row)
            n64_console_clear_range(row, 0, console_cols);
        n64_console_clear_range(console_row, 0, console_col + 1);
        break;
    case 2:
    case 3:
        for (row = 0; row < console_rows; ++row)
            n64_console_clear_range(row, 0, console_cols);
        break;
    default:
        n64_console_clear_range(console_row, console_col, console_cols);
        for (row = console_row + 1; row < console_rows; ++row)
            n64_console_clear_range(row, 0, console_cols);
        break;
    }
}

static void
n64_console_erase_line(unsigned mode)
{
    switch (mode) {
    case 1:
        n64_console_clear_range(console_row, 0, console_col + 1);
        break;
    case 2:
        n64_console_clear_range(console_row, 0, console_cols);
        break;
    default:
        n64_console_clear_range(console_row, console_col, console_cols);
        break;
    }
}

static void
n64_console_insert_chars(unsigned count)
{
    unsigned col;

    if (console_cols == 0 || console_col >= console_cols)
        return;
    if (count == 0)
        count = 1;
    if (count > console_cols - console_col)
        count = console_cols - console_col;

    col = console_cols;
    while (col-- > console_col + count) {
        console_cells[console_row][col] =
            console_cells[console_row][col - count];
        console_attrs[console_row][col] =
            console_attrs[console_row][col - count];
    }
    for (col = console_col; col < console_col + count; ++col) {
        console_cells[console_row][col] = ' ';
        console_attrs[console_row][col] = console_attr;
    }
    n64_console_render_row(console_row);
}

static void
n64_console_delete_chars(unsigned count)
{
    unsigned col;

    if (console_cols == 0 || console_col >= console_cols)
        return;
    if (count == 0)
        count = 1;
    if (count > console_cols - console_col)
        count = console_cols - console_col;

    for (col = console_col; col + count < console_cols; ++col) {
        console_cells[console_row][col] =
            console_cells[console_row][col + count];
        console_attrs[console_row][col] =
            console_attrs[console_row][col + count];
    }
    for (; col < console_cols; ++col) {
        console_cells[console_row][col] = ' ';
        console_attrs[console_row][col] = console_attr;
    }
    n64_console_render_row(console_row);
}

static void
n64_console_erase_chars(unsigned count)
{
    if (console_cols == 0 || console_col >= console_cols)
        return;
    if (count == 0)
        count = 1;
    if (console_col + count > console_cols)
        count = console_cols - console_col;
    n64_console_clear_range(console_row, console_col, console_col + count);
}

static void
n64_console_insert_lines(unsigned count)
{
    unsigned row;
    unsigned col;
    unsigned limit;

    if (console_rows == 0 || console_row >= console_rows)
        return;
    if (count == 0)
        count = 1;
    limit = console_rows - console_row;
    if (count > limit)
        count = limit;

    row = console_rows;
    while (row-- > console_row + count) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = console_cells[row - count][col];
            console_attrs[row][col] = console_attrs[row - count][col];
        }
    }
    for (row = console_row; row < console_row + count; ++row) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = ' ';
            console_attrs[row][col] = console_attr;
        }
    }
    n64_console_render_all();
}

static void
n64_console_delete_lines(unsigned count)
{
    unsigned row;
    unsigned col;
    unsigned limit;

    if (console_rows == 0 || console_row >= console_rows)
        return;
    if (count == 0)
        count = 1;
    limit = console_rows - console_row;
    if (count > limit)
        count = limit;

    for (row = console_row; row + count < console_rows; ++row) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = console_cells[row + count][col];
            console_attrs[row][col] = console_attrs[row + count][col];
        }
    }
    for (; row < console_rows; ++row) {
        for (col = 0; col < console_cols; ++col) {
            console_cells[row][col] = ' ';
            console_attrs[row][col] = console_attr;
        }
    }
    n64_console_render_all();
}

static void
n64_console_sgr(void)
{
    unsigned i;
    unsigned nparams;
    unsigned param;

    nparams = console_csi_count + 1;
    for (i = 0; i < nparams; ++i) {
        param = console_csi_params[i];
        switch (param) {
        case 0:
            console_attr = 0;
            break;
        case 1:
            console_attr |= N64_CONSOLE_ATTR_BOLD;
            break;
        case 4:
            console_attr |= N64_CONSOLE_ATTR_UNDERLINE;
            break;
        case 7:
            console_attr |= N64_CONSOLE_ATTR_REVERSE;
            break;
        case 22:
            console_attr &= ~N64_CONSOLE_ATTR_BOLD;
            break;
        case 24:
            console_attr &= ~N64_CONSOLE_ATTR_UNDERLINE;
            break;
        case 27:
            console_attr &= ~N64_CONSOLE_ATTR_REVERSE;
            break;
        default:
            break;
        }
    }
}

static void
n64_console_csi_dispatch(int ch)
{
    unsigned n;
    unsigned row;
    unsigned col;

    if (console_csi_private) {
        if ((ch == 'h' || ch == 'l') && console_csi_params[0] == 25)
            console_cursor_enabled = (ch == 'h');
        return;
    }

    n = n64_console_csi_param(0, 1);
    switch (ch) {
    case '@':
        n64_console_insert_chars(n);
        break;
    case 'A':
        console_row = (n > console_row) ? 0 : console_row - n;
        break;
    case 'B':
    case 'e':
        console_row += n;
        n64_console_clamp_cursor();
        break;
    case 'C':
    case 'a':
        console_col += n;
        n64_console_clamp_cursor();
        break;
    case 'D':
        console_col = (n > console_col) ? 0 : console_col - n;
        break;
    case 'E':
        console_row += n;
        console_col = 0;
        n64_console_clamp_cursor();
        break;
    case 'F':
        console_row = (n > console_row) ? 0 : console_row - n;
        console_col = 0;
        break;
    case 'G':
    case '`':
        col = n64_console_csi_param(0, 1);
        console_col = (col == 0) ? 0 : col - 1;
        n64_console_clamp_cursor();
        break;
    case 'H':
    case 'f':
        row = n64_console_csi_param(0, 1);
        col = n64_console_csi_param(1, 1);
        n64_console_move_cursor(row == 0 ? 0 : row - 1,
            col == 0 ? 0 : col - 1);
        break;
    case 'J':
        n64_console_erase_display(console_csi_params[0]);
        break;
    case 'K':
        n64_console_erase_line(console_csi_params[0]);
        break;
    case 'L':
        n64_console_insert_lines(n);
        break;
    case 'M':
        n64_console_delete_lines(n);
        break;
    case 'P':
        n64_console_delete_chars(n);
        break;
    case 'X':
        n64_console_erase_chars(n);
        break;
    case 'd':
        row = n64_console_csi_param(0, 1);
        console_row = (row == 0) ? 0 : row - 1;
        n64_console_clamp_cursor();
        break;
    case 'm':
        n64_console_sgr();
        break;
    case 's':
        n64_console_save_cursor();
        break;
    case 'u':
        n64_console_restore_cursor();
        break;
    default:
        break;
    }
}

static void
n64_console_esc_dispatch(int ch)
{
    switch (ch) {
    case '[':
        n64_console_csi_reset();
        console_state = N64_CONSOLE_STATE_CSI;
        return;
    case ']':
        console_state = N64_CONSOLE_STATE_OSC;
        return;
    case '7':
        n64_console_save_cursor();
        break;
    case '8':
        n64_console_restore_cursor();
        break;
    case 'c':
        n64_console_reset_screen();
        break;
    case 'D':
        if (console_row + 1 >= console_rows)
            n64_console_scroll();
        else
            ++console_row;
        break;
    case 'E':
        n64_console_newline();
        break;
    case 'M':
        n64_console_reverse_index();
        break;
    case '(':
    case ')':
    case '*':
    case '+':
        console_state = N64_CONSOLE_STATE_ESC_CHARSET;
        return;
    default:
        break;
    }
    console_state = N64_CONSOLE_STATE_GROUND;
}

static void
n64_console_put_vt100(int ch)
{
    if (ch == 0x18 || ch == 0x1a) {
        console_state = N64_CONSOLE_STATE_GROUND;
        return;
    }

    switch (console_state) {
    case N64_CONSOLE_STATE_ESC:
        n64_console_esc_dispatch(ch);
        break;
    case N64_CONSOLE_STATE_ESC_CHARSET:
        console_state = N64_CONSOLE_STATE_GROUND;
        break;
    case N64_CONSOLE_STATE_CSI:
        if (ch >= '0' && ch <= '9') {
            if (console_csi_count < N64_CONSOLE_CSI_PARAMS)
                console_csi_params[console_csi_count] =
                    console_csi_params[console_csi_count] * 10 +
                    ch - '0';
        } else if (ch == ';') {
            if (console_csi_count + 1 < N64_CONSOLE_CSI_PARAMS)
                ++console_csi_count;
        } else if (ch == '?') {
            console_csi_private = 1;
        } else if (ch >= 0x40 && ch <= 0x7e) {
            n64_console_csi_dispatch(ch);
            console_state = N64_CONSOLE_STATE_GROUND;
        } else if (ch < 0x20) {
            n64_console_control(ch);
        }
        break;
    case N64_CONSOLE_STATE_OSC:
        if (ch == '\007')
            console_state = N64_CONSOLE_STATE_GROUND;
        else if (ch == 0x1b)
            console_state = N64_CONSOLE_STATE_OSC_ESC;
        break;
    case N64_CONSOLE_STATE_OSC_ESC:
        console_state = (ch == '\\') ? N64_CONSOLE_STATE_GROUND :
            N64_CONSOLE_STATE_OSC;
        break;
    default:
        if (ch == 0x1b)
            console_state = N64_CONSOLE_STATE_ESC;
        else
            n64_console_control(ch);
        break;
    }
}

int
n64_console_poll(void)
{
    return n64keyboard_console_poll();
}

int
n64_console_getc(void)
{
    return n64keyboard_console_getc();
}

void
n64_console_putc(int ch)
{
    if (console_panic_mirror)
        n64_console_debug_putc(ch);
    n64_console_geometry();
    n64_console_erase_cursor();
    n64_console_put_vt100(ch);
    n64_console_draw_cursor();
}

int
n64_console_debug_mirror(int enable)
{
    int previous = console_panic_mirror;

    console_panic_mirror = enable != 0;
    return previous;
}

void
n64_console_panic_mode(void)
{
    struct n64_video_info info;

    (void)n64_console_debug_mirror(1);
    n64_video_get_info(&info);
    if (info.mode != N64FB_MODE_320X240)
        (void)n64_video_set_mode(N64FB_MODE_320X240);
    console_mode = ~0u;
    n64_console_geometry();
    n64_console_reset_screen();
}

void
n64_console_winsize(struct winsize *ws)
{
    n64_console_geometry();
    ws->ws_row = console_rows;
    ws->ws_col = console_cols;
    ws->ws_xpixel = console_cols * N64_CONSOLE_CELL_W;
    ws->ws_ypixel = console_rows * N64_CONSOLE_CELL_H;
}

void
n64_console_tty_winsize(struct tty *tp)
{
    n64_console_winsize(&tp->t_winsize);
}
