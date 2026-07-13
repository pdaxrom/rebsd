/*
 * Host-side tests for the compact HID boot-keyboard decoder.
 */

#include <stdio.h>
#include <string.h>
#include <usb/ukbd.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static unsigned char output[64];
static size_t output_length;

static void
capture(void *arg, int character)
{
    (void)arg;
    if (output_length < sizeof(output))
        output[output_length++] = (unsigned char)character;
}

static void
decode(struct ukbd_decoder *decoder, unsigned modifiers, unsigned usage)
{
    unsigned char report[UKBD_BOOT_REPORT_SIZE];

    memset(report, 0, sizeof(report));
    report[0] = (unsigned char)modifiers;
    report[2] = (unsigned char)usage;
    ukbd_decode_boot_report(decoder, report, sizeof(report), capture, 0);
}

static int
test_key_state_and_modifiers(void)
{
    struct ukbd_decoder decoder;

    output_length = 0;
    ukbd_decoder_init(&decoder);
    decode(&decoder, 0, 4);
    decode(&decoder, 0, 4);
    decode(&decoder, 0, 0);
    decode(&decoder, 0x02, 30);
    decode(&decoder, 0, 0);
    decode(&decoder, 0x01, 6);
    CHECK(output_length == 3);
    CHECK(output[0] == 'a');
    CHECK(output[1] == '!');
    CHECK(output[2] == 3);
    return 0;
}

static int
test_caps_and_sequences(void)
{
    struct ukbd_decoder decoder;

    output_length = 0;
    ukbd_decoder_init(&decoder);
    decode(&decoder, 0, 57);
    decode(&decoder, 0, 0);
    decode(&decoder, 0, 4);
    decode(&decoder, 0, 0);
    decode(&decoder, 0x02, 5);
    decode(&decoder, 0, 0);
    decode(&decoder, 0, 82);
    CHECK(output_length == 5);
    CHECK(output[0] == 'A');
    CHECK(output[1] == 'b');
    CHECK(memcmp(&output[2], "\033[A", 3) == 0);
    return 0;
}

static int
test_rollover_is_ignored(void)
{
    struct ukbd_decoder decoder;
    unsigned char report[UKBD_BOOT_REPORT_SIZE];

    output_length = 0;
    ukbd_decoder_init(&decoder);
    decode(&decoder, 0, 4);
    memset(report, 0, sizeof(report));
    report[2] = 1;
    ukbd_decode_boot_report(&decoder, report, sizeof(report), capture, 0);
    decode(&decoder, 0, 4);
    CHECK(output_length == 1 && output[0] == 'a');
    return 0;
}

int
main(void)
{
    CHECK(test_key_state_and_modifiers() == 0);
    CHECK(test_caps_and_sequences() == 0);
    CHECK(test_rollover_is_ignored() == 0);
    puts("ukbd_test: all tests passed");
    return 0;
}
