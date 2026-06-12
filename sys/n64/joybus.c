#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <machine/joybus.h>
#include <machine/si.h>

#define JOYBUS_CMD_END                  0xfe
#define JOYBUS_CMD_IDENTIFY             0x00
#define JOYBUS_CMD_N64_CONTROLLER_READ  0x01
#define JOYBUS_CMD_RANDNET_KBD_READ     0x13

#define JOYBUS_SEND_IDENTIFY            1
#define JOYBUS_RECV_IDENTIFY            3
#define JOYBUS_SEND_READ                1
#define JOYBUS_RECV_READ                4
#define JOYBUS_SEND_KBD                 2
#define JOYBUS_RECV_KBD                 7

static unsigned n64keyboard_led[N64_JOYBUS_PORT_COUNT] = {
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
};

static int
n64joybus_valid_port(unsigned port)
{
    return port < N64_JOYBUS_PORT_COUNT;
}

static void
n64joybus_finish_block(unsigned char *input, unsigned offset)
{
    if (offset >= N64_JOYBUS_BLOCK_SIZE - 1)
        offset = N64_JOYBUS_BLOCK_SIZE - 2;
    input[offset] = JOYBUS_CMD_END;
    input[N64_JOYBUS_BLOCK_SIZE - 1] = 1;
}

static unsigned
n64joybus_single_command(unsigned port, unsigned send_len, unsigned recv_len,
    const unsigned char *send, unsigned char *input)
{
    unsigned offset;
    unsigned i;

    bzero(input, N64_JOYBUS_BLOCK_SIZE);
    offset = port;
    input[offset++] = send_len;
    input[offset++] = recv_len;
    for (i = 0; i < send_len; ++i)
        input[offset++] = send[i];
    offset += recv_len;
    n64joybus_finish_block(input, offset);
    return port + 2 + send_len;
}

static int
n64joybus_read_command(unsigned port, unsigned send_len, unsigned recv_len,
    const unsigned char *send, unsigned char *recv)
{
    unsigned char input[N64_JOYBUS_BLOCK_SIZE];
    unsigned char output[N64_JOYBUS_BLOCK_SIZE];
    unsigned recv_offset;
    int error;

    if (!n64joybus_valid_port(port))
        return ENXIO;
    recv_offset = n64joybus_single_command(port, send_len, recv_len, send,
        input);
    error = n64_si_exec(input, output);
    if (error)
        return error;
    bcopy(output + recv_offset, recv, recv_len);
    return 0;
}

int
n64joybus_identify_port(unsigned port, struct n64joybus_port *info)
{
    unsigned char send[JOYBUS_SEND_IDENTIFY];
    unsigned char recv[JOYBUS_RECV_IDENTIFY];
    int error;

    if (info == 0)
        return EINVAL;
    if (!n64joybus_valid_port(port))
        return ENXIO;

    send[0] = JOYBUS_CMD_IDENTIFY;
    error = n64joybus_read_command(port, JOYBUS_SEND_IDENTIFY,
        JOYBUS_RECV_IDENTIFY, send, recv);
    if (error)
        return error;

    info->port = port;
    info->identifier = ((unsigned)recv[0] << 8) | recv[1];
    info->status = recv[2];
    info->present = info->identifier != N64_JOYBUS_ID_NONE;
    return 0;
}

static int
n64joybus_read_controller_like(unsigned port, unsigned expected,
    unsigned allow_mouse, struct n64joypad_state *state)
{
    struct n64joybus_port info;
    unsigned char send[JOYBUS_SEND_READ];
    unsigned char recv[JOYBUS_RECV_READ];
    int error;

    error = n64joybus_identify_port(port, &info);
    if (error)
        return error;
    if (info.identifier != expected &&
        (!allow_mouse || info.identifier != N64_JOYBUS_ID_N64_MOUSE))
        return ENODEV;

    send[0] = JOYBUS_CMD_N64_CONTROLLER_READ;
    error = n64joybus_read_command(port, JOYBUS_SEND_READ, JOYBUS_RECV_READ,
        send, recv);
    if (error)
        return error;

    state->port = port;
    state->identifier = info.identifier;
    state->status = info.status;
    state->present = 1;
    state->buttons = ((unsigned)recv[0] << 8) | recv[1];
    state->stick_x = (signed char)recv[2];
    state->stick_y = (signed char)recv[3];
    return 0;
}

int
n64joypad_get_state(unsigned port, struct n64joypad_state *state)
{
    if (state == 0)
        return EINVAL;
    return n64joybus_read_controller_like(port, N64_JOYBUS_ID_N64_CONTROLLER,
        0, state);
}

int
n64mouse_get_state(unsigned port, struct n64mouse_state *state)
{
    struct n64joypad_state raw;
    int error;

    if (state == 0)
        return EINVAL;

    error = n64joybus_read_controller_like(port, N64_JOYBUS_ID_N64_MOUSE,
        0, &raw);
    if (error)
        return error;

    state->port = raw.port;
    state->identifier = raw.identifier;
    state->status = raw.status;
    state->present = raw.present;
    state->buttons = raw.buttons;
    state->dx = raw.stick_x;
    state->dy = raw.stick_y;
    return 0;
}

int
n64keyboard_get_state(unsigned port, struct n64keyboard_state *state)
{
    struct n64joybus_port info;
    unsigned char send[JOYBUS_SEND_KBD];
    unsigned char recv[JOYBUS_RECV_KBD];
    int error;

    if (state == 0)
        return EINVAL;
    if (!n64joybus_valid_port(port))
        return ENXIO;

    error = n64joybus_identify_port(port, &info);
    if (error)
        return error;
    if (info.identifier != N64_JOYBUS_ID_RANDNET_KEYBOARD)
        return ENODEV;

    send[0] = JOYBUS_CMD_RANDNET_KBD_READ;
    send[1] = n64keyboard_led[port] & 0xff;
    error = n64joybus_read_command(port, JOYBUS_SEND_KBD, JOYBUS_RECV_KBD,
        send, recv);
    if (error)
        return error;

    state->port = port;
    state->identifier = info.identifier;
    state->status = recv[6];
    state->present = 1;
    state->led = n64keyboard_led[port];
    state->key[0] = ((unsigned)recv[0] << 8) | recv[1];
    state->key[1] = ((unsigned)recv[2] << 8) | recv[3];
    state->key[2] = ((unsigned)recv[4] << 8) | recv[5];
    return 0;
}

static int
n64joybus_ident_ioctl(dev_t dev, caddr_t data)
{
    return n64joybus_identify_port(minor(dev), (struct n64joybus_port *)data);
}

int
n64joypad_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64joypad_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64joypad_read(dev_t dev, struct uio *uio, int flag)
{
    struct n64joypad_state state;
    int error;

    (void)flag;
    error = n64joypad_get_state(minor(dev), &state);
    if (error)
        return error;
    return uiomove((caddr_t)&state, sizeof(state), uio);
}

int
n64joypad_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct n64joypad_state *state;

    (void)flag;
    if (!n64joybus_valid_port(minor(dev)))
        return ENXIO;

    switch (cmd) {
    case N64JOYBUSIOC_IDENTIFY:
        return n64joybus_ident_ioctl(dev, data);
    case N64JOYPADIOC_GETSTATE:
        state = (struct n64joypad_state *)data;
        return n64joypad_get_state(minor(dev), state);
    default:
        return ENOTTY;
    }
}

int
n64mouse_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64mouse_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64mouse_read(dev_t dev, struct uio *uio, int flag)
{
    struct n64mouse_state state;
    int error;

    (void)flag;
    error = n64mouse_get_state(minor(dev), &state);
    if (error)
        return error;
    return uiomove((caddr_t)&state, sizeof(state), uio);
}

int
n64mouse_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct n64mouse_state *state;

    (void)flag;
    if (!n64joybus_valid_port(minor(dev)))
        return ENXIO;

    switch (cmd) {
    case N64JOYBUSIOC_IDENTIFY:
        return n64joybus_ident_ioctl(dev, data);
    case N64MOUSEIOC_GETSTATE:
        state = (struct n64mouse_state *)data;
        return n64mouse_get_state(minor(dev), state);
    default:
        return ENOTTY;
    }
}

int
n64keyboard_open(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64keyboard_close(dev_t dev, int flag, int mode)
{
    (void)flag;
    (void)mode;
    return n64joybus_valid_port(minor(dev)) ? 0 : ENXIO;
}

int
n64keyboard_read(dev_t dev, struct uio *uio, int flag)
{
    struct n64keyboard_state state;
    int error;

    (void)flag;
    error = n64keyboard_get_state(minor(dev), &state);
    if (error)
        return error;
    return uiomove((caddr_t)&state, sizeof(state), uio);
}

int
n64keyboard_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    struct n64keyboard_state *state;
    unsigned led;

    (void)flag;
    if (!n64joybus_valid_port(minor(dev)))
        return ENXIO;

    switch (cmd) {
    case N64JOYBUSIOC_IDENTIFY:
        return n64joybus_ident_ioctl(dev, data);
    case N64KBDIOC_GETSTATE:
        state = (struct n64keyboard_state *)data;
        return n64keyboard_get_state(minor(dev), state);
    case N64KBDIOC_SETLED:
        led = *(unsigned *)data;
        n64keyboard_led[minor(dev)] = led & 0xff;
        return 0;
    default:
        return ENOTTY;
    }
}
