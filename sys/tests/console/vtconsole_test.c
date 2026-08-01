/*
 * Host tests for the common VT100 cell-console core.
 */

#include <console/vtconsole.h>

#include <stdio.h>
#include <string.h>

#define TEST_COLS  20u
#define TEST_ROWS  4u

#define CHECK(condition) do {                                             \
    if (!(condition)) {                                                   \
        fprintf(stderr, "%s:%d: check failed: %s\n",                    \
            __FILE__, __LINE__, #condition);                              \
        return 1;                                                         \
    }                                                                     \
} while (0)

static unsigned char cells[TEST_ROWS][TEST_COLS];
static unsigned char attrs[TEST_ROWS][TEST_COLS];
static unsigned cursor_col;
static unsigned cursor_row;
static int cursor_visible;
static unsigned render_count;

static void
render_cell(void *arg, unsigned col, unsigned row, int cursor)
{
    (void)arg;
    (void)col;
    (void)row;
    (void)cursor;
    ++render_count;
}

static void
render_all(void *arg)
{
    (void)arg;
    ++render_count;
}

static void
cursor(void *arg, unsigned col, unsigned row, int visible)
{
    (void)arg;
    cursor_col = col;
    cursor_row = row;
    cursor_visible = visible;
}

static const struct vtconsole_ops ops = {
    render_cell,
    render_all,
    cursor,
    0
};

static void
put_string(struct vtconsole *console, const char *text)
{
    while (*text != '\0')
        vtconsole_putc(console, (unsigned char)*text++);
}

static int
line_equals(unsigned row, const char *text)
{
    size_t length;

    length = strlen(text);
    if (length > TEST_COLS ||
        memcmp(cells[row], text, length) != 0)
        return 0;
    while (length < TEST_COLS) {
        if (cells[row][length++] != ' ')
            return 0;
    }
    return 1;
}

static int
test_readline_backspace_refresh(void)
{
    struct vtconsole console;

    vtconsole_init(&console, &cells[0][0], &attrs[0][0], TEST_COLS,
        TEST_COLS, TEST_ROWS, &ops, 0);
    vtconsole_set_geometry(&console, TEST_COLS, TEST_ROWS);

    /*
     * readline redraws the whole edited line after Delete/Backspace:
     * carriage return, shortened buffer, erase-to-EOL, then absolute
     * cursor placement expressed as CR + CSI C.
     */
    put_string(&console, "# dd if=/dev/sd0 i");
    put_string(&console, "\r# dd if=/dev/sd0 \033[0K\r\033[18C");
    CHECK(line_equals(0, "# dd if=/dev/sd0 "));
    CHECK(console.vc_col == 18 && console.vc_row == 0);
    CHECK(cursor_col == 18 && cursor_row == 0 && cursor_visible);
    return 0;
}

static int
test_tty_rubout_and_csi(void)
{
    struct vtconsole console;

    vtconsole_init(&console, &cells[0][0], &attrs[0][0], TEST_COLS,
        TEST_COLS, TEST_ROWS, &ops, 0);
    vtconsole_set_geometry(&console, TEST_COLS, TEST_ROWS);
    put_string(&console, "abc\b \b");
    CHECK(line_equals(0, "ab"));
    CHECK(console.vc_col == 2);

    put_string(&console, "\033[7mR\033[0m");
    CHECK(cells[0][2] == 'R');
    CHECK((attrs[0][2] & VTCONSOLE_ATTR_REVERSE) != 0);
    CHECK(console.vc_attr == 0);

    put_string(&console, "\033[2D\033[0K");
    CHECK(console.vc_col == 1);
    CHECK(line_equals(0, "a"));
    return 0;
}

static int
test_cursor_visibility_and_clear(void)
{
    struct vtconsole console;

    vtconsole_init(&console, &cells[0][0], &attrs[0][0], TEST_COLS,
        TEST_COLS, TEST_ROWS, &ops, 0);
    vtconsole_set_geometry(&console, TEST_COLS, TEST_ROWS);
    put_string(&console, "one\ntwo");
    put_string(&console, "\033[?25l");
    CHECK(!cursor_visible);
    put_string(&console, "\033[H\033[2J");
    CHECK(line_equals(0, ""));
    CHECK(line_equals(1, ""));
    CHECK(console.vc_col == 0 && console.vc_row == 0);
    put_string(&console, "\033[?25h");
    CHECK(cursor_visible);
    CHECK(render_count != 0);
    return 0;
}

int
main(void)
{
    CHECK(test_readline_backspace_refresh() == 0);
    CHECK(test_tty_rubout_and_csi() == 0);
    CHECK(test_cursor_visibility_and_clear() == 0);
    puts("vtconsole_test: all tests passed");
    return 0;
}
