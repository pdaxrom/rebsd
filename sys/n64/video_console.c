#include <sys/param.h>
#include <machine/console.h>
#include <machine/joybus.h>
#include <machine/video.h>

#define N64_CONSOLE_CELL_W      6
#define N64_CONSOLE_CELL_H      10
#define N64_CONSOLE_GLYPH_H     8
#define N64_CONSOLE_FG          0xffff
#define N64_CONSOLE_BG          0x0001

static unsigned console_mode = ~0u;
static unsigned console_width;
static unsigned console_height;
static unsigned console_x0;
static unsigned console_y0;
static unsigned console_cols;
static unsigned console_rows;
static unsigned console_col;
static unsigned console_row;

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
    struct n64fb_info info;

    n64_video_get_info(&info);
    if (info.mode == console_mode)
        return;

    console_mode = info.mode;
    console_width = info.width;
    console_height = info.height;
    console_x0 = console_width / 20;
    console_y0 = console_height / 20;
    console_cols = (console_width - 2 * console_x0) / N64_CONSOLE_CELL_W;
    console_rows = (console_height - 2 * console_y0) / N64_CONSOLE_CELL_H;
    console_col = 0;
    console_row = 0;
}

static void
n64_console_clear_cell(unsigned col, unsigned row)
{
    volatile unsigned short *fb;
    unsigned x0;
    unsigned y0;
    unsigned x;
    unsigned y;

    fb = n64_video_framebuffer();
    x0 = console_x0 + col * N64_CONSOLE_CELL_W;
    y0 = console_y0 + row * N64_CONSOLE_CELL_H;
    for (y = 0; y < N64_CONSOLE_CELL_H; ++y)
        for (x = 0; x < N64_CONSOLE_CELL_W; ++x)
            fb[(y0 + y) * console_width + x0 + x] = N64_CONSOLE_BG;
}

static void
n64_console_scroll(void)
{
    volatile unsigned short *fb;
    unsigned x;
    unsigned y;

    fb = n64_video_framebuffer();
    for (y = console_y0;
        y < console_y0 + (console_rows - 1) * N64_CONSOLE_CELL_H;
        ++y) {
        for (x = console_x0; x < console_x0 + console_cols *
            N64_CONSOLE_CELL_W; ++x) {
            fb[y * console_width + x] =
                fb[(y + N64_CONSOLE_CELL_H) * console_width + x];
        }
    }

    for (; y < console_y0 + console_rows * N64_CONSOLE_CELL_H; ++y)
        for (x = console_x0; x < console_x0 + console_cols *
            N64_CONSOLE_CELL_W; ++x)
            fb[y * console_width + x] = N64_CONSOLE_BG;
}

static void
n64_console_newline(void)
{
    console_col = 0;
    if (++console_row >= console_rows) {
        n64_console_scroll();
        console_row = console_rows - 1;
    }
}

static void
n64_console_draw_char(int ch)
{
    volatile unsigned short *fb;
    unsigned char glyph[5];
    unsigned x0;
    unsigned y0;
    unsigned x;
    unsigned y;

    n64_console_clear_cell(console_col, console_row);
    n64_console_glyph(ch, glyph);

    fb = n64_video_framebuffer();
    x0 = console_x0 + console_col * N64_CONSOLE_CELL_W;
    y0 = console_y0 + console_row * N64_CONSOLE_CELL_H;
    for (x = 0; x < 5; ++x)
        for (y = 0; y < N64_CONSOLE_GLYPH_H; ++y)
            if (glyph[x] & (1u << y))
                fb[(y0 + y) * console_width + x0 + x] =
                    N64_CONSOLE_FG;

    if (++console_col >= console_cols)
        n64_console_newline();
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
    n64_console_geometry();

    switch (ch) {
    case '\r':
        console_col = 0;
        break;
    case '\n':
        n64_console_newline();
        break;
    case '\t':
        do {
            n64_console_draw_char(' ');
        } while (console_col & 7u);
        break;
    case '\b':
        if (console_col > 0) {
            console_col--;
            n64_console_clear_cell(console_col, console_row);
        }
        break;
    default:
        if (ch >= 0x20 && ch < 0x7f)
            n64_console_draw_char(ch);
        break;
    }
}
