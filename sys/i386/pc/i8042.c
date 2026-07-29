/*
 * Legacy PC i8042 transport.  PS/2 scan-code and packet decoding belongs
 * to the machine-independent input layer in sys/input.
 */

#include "i8042.h"
#include "interrupt.h"
#include "io.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <input/mousevar.h>
#include <input/ps2.h>

#define I8042_DATA_PORT          0x0060u
#define I8042_STATUS_PORT        0x0064u
#define I8042_COMMAND_PORT       0x0064u

#define I8042_STATUS_OUTPUT      0x01u
#define I8042_STATUS_INPUT       0x02u
#define I8042_STATUS_AUX         0x20u
#define I8042_STATUS_TIMEOUT     0x40u
#define I8042_STATUS_PARITY      0x80u

#define I8042_CMD_READ_CONFIG    0x20u
#define I8042_CMD_WRITE_CONFIG   0x60u
#define I8042_CMD_SELF_TEST      0xaau
#define I8042_CMD_TEST_AUX       0xa9u
#define I8042_CMD_DISABLE_AUX    0xa7u
#define I8042_CMD_ENABLE_AUX     0xa8u
#define I8042_CMD_TEST_KBD       0xabu
#define I8042_CMD_DISABLE_KBD    0xadu
#define I8042_CMD_ENABLE_KBD     0xaeu
#define I8042_CMD_WRITE_AUX      0xd4u

#define I8042_CONFIG_IRQ_KBD     0x01u
#define I8042_CONFIG_IRQ_AUX     0x02u
#define I8042_CONFIG_DISABLE_KBD 0x10u
#define I8042_CONFIG_DISABLE_AUX 0x20u
#define I8042_CONFIG_TRANSLATE   0x40u

#define PS2_CMD_SET_SCANCODE     0xf0u
#define PS2_CMD_ENABLE           0xf4u
#define PS2_CMD_DEFAULTS         0xf6u
#define PS2_CMD_RESET            0xffu
#define PS2_RESPONSE_ACK         0xfau
#define PS2_RESPONSE_RESEND      0xfeu
#define PS2_RESPONSE_BAT_OK      0xaau

#define I8042_WAIT_SPINS         250000u
#define I8042_COMMAND_RETRIES    3u

static struct ps2kbd_decoder i8042_keyboard_decoder;
static struct ps2mouse_decoder i8042_mouse_decoder;
static int i8042_keyboard_present;
static int i8042_mouse_present;
static int i8042_mouse_unit = -1;

static int
i8042_wait_input_empty(void)
{
    unsigned spins;

    for (spins = 0; spins < I8042_WAIT_SPINS; ++spins) {
        if ((i386_inb(I8042_STATUS_PORT) & I8042_STATUS_INPUT) == 0)
            return 1;
        i386_io_wait();
    }
    return 0;
}

static int
i8042_write_command(unsigned char command)
{
    if (!i8042_wait_input_empty())
        return 0;
    i386_outb(I8042_COMMAND_PORT, command);
    return 1;
}

static int
i8042_write_data(unsigned char data)
{
    if (!i8042_wait_input_empty())
        return 0;
    i386_outb(I8042_DATA_PORT, data);
    return 1;
}

static int
i8042_read_data(int aux, unsigned char *value)
{
    unsigned char status;
    unsigned spins;

    for (spins = 0; spins < I8042_WAIT_SPINS; ++spins) {
        status = i386_inb(I8042_STATUS_PORT);
        if ((status & I8042_STATUS_OUTPUT) == 0) {
            i386_io_wait();
            continue;
        }
        *value = i386_inb(I8042_DATA_PORT);
        if ((status & (I8042_STATUS_TIMEOUT | I8042_STATUS_PARITY)) != 0)
            return 0;
        return ((status & I8042_STATUS_AUX) != 0) == aux;
    }
    return 0;
}

static void
i8042_flush(void)
{
    unsigned char status;
    unsigned count;

    for (count = 0; count < 32u; ++count) {
        status = i386_inb(I8042_STATUS_PORT);
        if ((status & I8042_STATUS_OUTPUT) == 0)
            break;
        (void)i386_inb(I8042_DATA_PORT);
    }
}

static int
i8042_controller_response(unsigned char command, unsigned char *response)
{
    return i8042_write_command(command) &&
        i8042_read_data(0, response);
}

static int
i8042_write_config(unsigned char config)
{
    return i8042_write_command(I8042_CMD_WRITE_CONFIG) &&
        i8042_write_data(config);
}

static int
i8042_device_write(int aux, unsigned char command)
{
    unsigned char response;
    unsigned retry;

    for (retry = 0; retry < I8042_COMMAND_RETRIES; ++retry) {
        if (aux && !i8042_write_command(I8042_CMD_WRITE_AUX))
            return 0;
        if (!i8042_write_data(command) ||
            !i8042_read_data(aux, &response))
            return 0;
        if (response == PS2_RESPONSE_ACK)
            return 1;
        if (response != PS2_RESPONSE_RESEND)
            return 0;
    }
    return 0;
}

static int
i8042_reset_keyboard(void)
{
    unsigned char response;

    if (!i8042_device_write(0, PS2_CMD_RESET) ||
        !i8042_read_data(0, &response) ||
        response != PS2_RESPONSE_BAT_OK)
        return 0;
    if (!i8042_device_write(0, PS2_CMD_DEFAULTS) ||
        !i8042_device_write(0, PS2_CMD_SET_SCANCODE) ||
        !i8042_device_write(0, 2u) ||
        !i8042_device_write(0, PS2_CMD_ENABLE))
        return 0;
    return 1;
}

static int
i8042_reset_mouse(void)
{
    unsigned char response;

    if (!i8042_device_write(1, PS2_CMD_RESET) ||
        !i8042_read_data(1, &response) ||
        response != PS2_RESPONSE_BAT_OK ||
        !i8042_read_data(1, &response))
        return 0;
    if (!i8042_device_write(1, PS2_CMD_DEFAULTS) ||
        !i8042_device_write(1, PS2_CMD_ENABLE))
        return 0;
    return 1;
}

static void
i8042_keyboard_emit(void *arg, int character)
{
    (void)arg;
    cninput(character);
}

static void
i8042_mouse_emit(void *arg, const struct mouse_event *event)
{
    (void)arg;
    if (i8042_mouse_unit >= 0)
        mouse_input((unsigned)i8042_mouse_unit, event);
}

static int
i8042_interrupt(void *arg)
{
    unsigned char data;
    unsigned char status;
    unsigned drained;

    (void)arg;
    drained = 0;
    for (;;) {
        status = i386_inb(I8042_STATUS_PORT);
        if ((status & I8042_STATUS_OUTPUT) == 0)
            break;
        data = i386_inb(I8042_DATA_PORT);
        ++drained;
        if ((status & (I8042_STATUS_TIMEOUT | I8042_STATUS_PARITY)) != 0)
            continue;
        if ((status & I8042_STATUS_AUX) != 0) {
            if (i8042_mouse_present)
                ps2mouse_decode_byte(&i8042_mouse_decoder, data,
                    i8042_mouse_emit, 0);
        } else if (i8042_keyboard_present) {
            ps2kbd_decode_byte(&i8042_keyboard_decoder, data,
                i8042_keyboard_emit, 0);
        }
    }
    return drained != 0;
}

static int
i8042_attach(void)
{
    unsigned char config;
    unsigned char response;
    int keyboard_port;
    int mouse_port;

    if (!i8042_write_command(I8042_CMD_DISABLE_KBD) ||
        !i8042_write_command(I8042_CMD_DISABLE_AUX))
        return ENXIO;
    i8042_flush();
    if (!i8042_controller_response(I8042_CMD_READ_CONFIG, &config))
        return ENXIO;
    config &= ~(I8042_CONFIG_IRQ_KBD | I8042_CONFIG_IRQ_AUX |
        I8042_CONFIG_TRANSLATE);
    config |= I8042_CONFIG_DISABLE_KBD | I8042_CONFIG_DISABLE_AUX;
    if (!i8042_write_config(config))
        return ENXIO;
    if (!i8042_controller_response(I8042_CMD_SELF_TEST, &response) ||
        response != 0x55u)
        return ENXIO;
    if (!i8042_write_config(config))
        return ENXIO;

    keyboard_port =
        i8042_controller_response(I8042_CMD_TEST_KBD, &response) &&
        response == 0;
    mouse_port =
        i8042_controller_response(I8042_CMD_TEST_AUX, &response) &&
        response == 0;

    if (keyboard_port && i8042_write_command(I8042_CMD_ENABLE_KBD))
        i8042_keyboard_present = i8042_reset_keyboard();
    if (mouse_port && i8042_write_command(I8042_CMD_ENABLE_AUX))
        i8042_mouse_present = i8042_reset_mouse();

    ps2kbd_decoder_init(&i8042_keyboard_decoder);
    ps2mouse_decoder_init(&i8042_mouse_decoder);
    if (i8042_mouse_present) {
        i8042_mouse_unit = mouse_attach("psm0");
        if (i8042_mouse_unit < 0)
            i8042_mouse_present = 0;
    }
    if (!i8042_keyboard_present && !i8042_mouse_present)
        return ENXIO;
    if (!i386_irq_establish(I386_IRQ_KEYBOARD, i8042_interrupt, 0))
        return ENXIO;
    if (i8042_mouse_present &&
        !i386_irq_establish(I386_IRQ_PS2_MOUSE, i8042_interrupt, 0))
        return ENXIO;

    config |= I8042_CONFIG_TRANSLATE;
    if (i8042_keyboard_present) {
        config &= ~I8042_CONFIG_DISABLE_KBD;
        config |= I8042_CONFIG_IRQ_KBD;
    }
    if (i8042_mouse_present) {
        config &= ~I8042_CONFIG_DISABLE_AUX;
        config |= I8042_CONFIG_IRQ_AUX;
    }
    if (!i8042_write_config(config))
        return ENXIO;
    if (i8042_keyboard_present)
        i386_pic_unmask(I386_IRQ_KEYBOARD);
    if (i8042_mouse_present)
        i386_pic_unmask(I386_IRQ_PS2_MOUSE);

    printf("i8042: controller ready, keyboard=%s mouse=%s\n",
        i8042_keyboard_present ? "present" : "absent",
        i8042_mouse_present ? "present" : "absent");
    if (i8042_keyboard_present)
        printf("pckbd0: PS/2 keyboard, set 2 with controller "
            "translation, irq %u\n", I386_IRQ_KEYBOARD);
    if (i8042_mouse_present)
        printf("psm0: PS/2 mouse, 3-byte packets, irq %u as mouse%d\n",
            I386_IRQ_PS2_MOUSE, i8042_mouse_unit);
    return 0;
}

void
i8042attach(int unit)
{
    int error;

    (void)unit;
    error = i8042_attach();
    if (error != 0)
        printf("i8042: attach failed, error=%d\n", error);
}
