/*
 * Host-side tests for the shared PS/2 keyboard and mouse decoders.
 */

#include <stdio.h>
#include <string.h>

#include <input/ps2.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static unsigned char keyboard_output[64];
static size_t keyboard_length;
static struct mouse_event mouse_output[8];
static size_t mouse_length;

static void
capture_key(void *arg, int character)
{
    (void)arg;
    if (keyboard_length < sizeof(keyboard_output))
        keyboard_output[keyboard_length++] = (unsigned char)character;
}

static void
capture_mouse(void *arg, const struct mouse_event *event)
{
    (void)arg;
    if (mouse_length < sizeof(mouse_output) / sizeof(mouse_output[0]))
        mouse_output[mouse_length++] = *event;
}

static int
test_ps2_keyboard(void)
{
    static const unsigned char stream[] = {
        0x1e, 0x9e,
        0x2a, 0x02, 0x82, 0xaa,
        0x1d, 0x2e, 0xae, 0x9d,
        0xe0, 0x48, 0xe0, 0xc8
    };
    struct ps2kbd_decoder decoder;
    size_t i;

    keyboard_length = 0;
    ps2kbd_decoder_init(&decoder);
    for (i = 0; i < sizeof(stream); ++i)
        ps2kbd_decode_byte(&decoder, stream[i], capture_key, 0);
    CHECK(keyboard_length == 6);
    CHECK(keyboard_output[0] == 'a');
    CHECK(keyboard_output[1] == '!');
    CHECK(keyboard_output[2] == 3);
    CHECK(memcmp(&keyboard_output[3], "\033[A", 3) == 0);
    return 0;
}

static int
test_ps2_mouse(void)
{
    static const unsigned char stream[] = {
        0x00,
        0x09, 0x05, 0xfd,
        0x08, 0xfe, 0x04,
        0xc8, 0x01, 0x01
    };
    struct ps2mouse_decoder decoder;
    size_t i;

    mouse_length = 0;
    ps2mouse_decoder_init(&decoder);
    for (i = 0; i < sizeof(stream); ++i)
        ps2mouse_decode_byte(&decoder, stream[i], capture_mouse, 0);
    CHECK(mouse_length == 2);
    CHECK(mouse_output[0].me_dx == 5);
    CHECK(mouse_output[0].me_dy == 3);
    CHECK(mouse_output[0].me_buttons == MOUSE_BUTTON_LEFT);
    CHECK(mouse_output[1].me_dx == -2);
    CHECK(mouse_output[1].me_dy == -4);
    CHECK(mouse_output[1].me_buttons == 0);
    return 0;
}

int
main(void)
{
    CHECK(test_ps2_keyboard() == 0);
    CHECK(test_ps2_mouse() == 0);
    puts("input_test: all tests passed");
    return 0;
}
