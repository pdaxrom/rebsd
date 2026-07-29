/*
 * Host-side tests for the shared HID boot-mouse decoder.
 */

#include <stdio.h>

#include <usb/ums.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static struct mouse_event output;
static unsigned output_count;

static void
capture(void *arg, const struct mouse_event *event)
{
    (void)arg;
    output = *event;
    ++output_count;
}

int
main(void)
{
    static const unsigned char report[] = {
        0x15, 0xfe, 0x7f, 0xff
    };

    output_count = 0;
    ums_decode_boot_report(report, sizeof(report), capture, 0);
    CHECK(output_count == 1);
    CHECK(output.me_dx == -2);
    CHECK(output.me_dy == 127);
    CHECK(output.me_dz == -1);
    CHECK(output.me_buttons ==
        (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_MIDDLE | MOUSE_BUTTON_5));
    puts("ums_test: all tests passed");
    return 0;
}
