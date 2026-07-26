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

#define N64_KEYBOARD_CONSOLE_QSIZE      64
#define N64_KEYBOARD_POLL_TICKS         ((HZ + 49) / 50)
#define N64_KEYBOARD_SCAN_TICKS         ((HZ + 1) / 2)

#define N64_RANDNET_KEY_CAPS_LOCK       0x0f05
#define N64_RANDNET_KEY_NUM_LOCK        0x0a05
#define N64_RANDNET_KEY_LEFT_CTRL       0x1107
#define N64_RANDNET_KEY_LEFT_SHIFT      0x0e01
#define N64_RANDNET_KEY_RIGHT_SHIFT     0x0e06
#define N64_RANDNET_KEY_BACKSPACE       0x0d06
#define N64_RANDNET_KEY_DELETE          0x1008
#define N64_RANDNET_KEY_RIGHT           0x0405
#define N64_RANDNET_KEY_LEFT            0x0205
#define N64_RANDNET_KEY_DOWN            0x0305
#define N64_RANDNET_KEY_UP              0x0204

struct n64keyboard_ascii {
    unsigned code;
    char normal;
    char shifted;
};

static unsigned n64keyboard_led[N64_JOYBUS_PORT_COUNT] = {
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
    N64_KBD_LED_POWER,
};
static unsigned char n64keyboard_console_q[N64_KEYBOARD_CONSOLE_QSIZE];
static unsigned n64keyboard_console_head;
static unsigned n64keyboard_console_tail;
static unsigned n64keyboard_console_prev[N64_JOYBUS_PORT_COUNT][3];
static int n64keyboard_console_port = -1;
static unsigned n64keyboard_console_last_poll;
static unsigned n64keyboard_console_last_scan;

static const struct n64keyboard_ascii n64keyboard_ascii_map[] = {
    { 0x0d07, 'a', 'A' }, { 0x0708, 'b', 'B' },
    { 0x0508, 'c', 'C' }, { 0x0507, 'd', 'D' },
    { 0x0601, 'e', 'E' }, { 0x0607, 'f', 'F' },
    { 0x0707, 'g', 'G' }, { 0x0807, 'h', 'H' },
    { 0x0804, 'i', 'I' }, { 0x0907, 'j', 'J' },
    { 0x0903, 'k', 'K' }, { 0x0803, 'l', 'L' },
    { 0x0908, 'm', 'M' }, { 0x0808, 'n', 'N' },
    { 0x0704, 'o', 'O' }, { 0x0604, 'p', 'P' },
    { 0x0c01, 'q', 'Q' }, { 0x0701, 'r', 'R' },
    { 0x0c07, 's', 'S' }, { 0x0801, 't', 'T' },
    { 0x0904, 'u', 'U' }, { 0x0608, 'v', 'V' },
    { 0x0501, 'w', 'W' }, { 0x0c08, 'x', 'X' },
    { 0x0901, 'y', 'Y' }, { 0x0d08, 'z', 'Z' },
    { 0x0c05, '1', '!' }, { 0x0505, '2', '@' },
    { 0x0605, '3', '#' }, { 0x0705, '4', '$' },
    { 0x0805, '5', '%' }, { 0x0905, '6', '^' },
    { 0x0906, '7', '&' }, { 0x0806, '8', '*' },
    { 0x0706, '9', '(' }, { 0x0606, '0', ')' },
    { 0x0d04, '\r', '\r' }, { 0x0a08, '\033', '\033' },
    { 0x0d01, '\t', '\t' },
    { 0x0602, ' ', ' ' }, { 0x1004, '-', '_' },
    { 0x0c04, '[', '{' }, { 0x0406, ']', '}' },
    { 0x1105, ';', ':' }, { 0x0504, '\'', '"' },
    { 0x0902, ',', '<' }, { 0x0802, '.', '>' },
    { 0x0702, '/', '?' }, { 0x0603, '*', '*' },
    { 0x0506, '-', '-' }, { 0x0c06, '+', '+' },
    { 0x1002, '1', '1' }, { 0x0e02, '2', '2' },
    { 0x1006, '3', '3' },
};

static int
n64joybus_valid_port(unsigned port)
{
    return port < N64_JOYBUS_PORT_COUNT;
}

static int
n64keyboard_code_down(unsigned code, unsigned keys[3])
{
    return keys[0] == code || keys[1] == code || keys[2] == code;
}

static void
n64keyboard_console_put(int ch)
{
    unsigned next;

    next = (n64keyboard_console_head + 1) % N64_KEYBOARD_CONSOLE_QSIZE;
    if (next == n64keyboard_console_tail)
        return;
    n64keyboard_console_q[n64keyboard_console_head] = ch & 0xff;
    n64keyboard_console_head = next;
}

static void
n64keyboard_console_puts(const char *str)
{
    while (*str != 0)
        n64keyboard_console_put(*str++);
}

static int
n64keyboard_key_is_modifier(unsigned code)
{
    return code == N64_RANDNET_KEY_LEFT_CTRL ||
        code == N64_RANDNET_KEY_LEFT_SHIFT ||
        code == N64_RANDNET_KEY_RIGHT_SHIFT;
}

static int
n64keyboard_translate_key(unsigned port, unsigned code, int shift, int ctrl)
{
    const struct n64keyboard_ascii *map;
    unsigned i;
    int ch;
    int letter;

    map = n64keyboard_ascii_map;
    for (i = 0; i < sizeof(n64keyboard_ascii_map) /
        sizeof(n64keyboard_ascii_map[0]); ++i) {
        if (map[i].code != code)
            continue;

        ch = map[i].normal;
        letter = ch >= 'a' && ch <= 'z';
        if (letter) {
            if (shift ^ ((n64keyboard_led[port] &
                N64_KBD_LED_CAPS_LOCK) != 0))
                ch = map[i].shifted;
        } else if (shift) {
            ch = map[i].shifted;
        }
        if (ctrl && letter)
            ch = (ch & 0x1f);
        return ch;
    }
    return -1;
}

static void
n64keyboard_console_key(unsigned port, unsigned code, int shift, int ctrl)
{
    int ch;

    switch (code) {
    case 0:
        return;
    case N64_RANDNET_KEY_CAPS_LOCK:
        n64keyboard_led[port] ^= N64_KBD_LED_CAPS_LOCK;
        return;
    case N64_RANDNET_KEY_NUM_LOCK:
        n64keyboard_led[port] ^= N64_KBD_LED_NUM_LOCK;
        return;
    case N64_RANDNET_KEY_BACKSPACE:
    case N64_RANDNET_KEY_DELETE:
        n64keyboard_console_put('\177');
        return;
    case N64_RANDNET_KEY_RIGHT:
        n64keyboard_console_puts("\033[C");
        return;
    case N64_RANDNET_KEY_LEFT:
        n64keyboard_console_puts("\033[D");
        return;
    case N64_RANDNET_KEY_DOWN:
        n64keyboard_console_puts("\033[B");
        return;
    case N64_RANDNET_KEY_UP:
        n64keyboard_console_puts("\033[A");
        return;
    default:
        break;
    }

    if (n64keyboard_key_is_modifier(code))
        return;

    ch = n64keyboard_translate_key(port, code, shift, ctrl);
    if (ch >= 0)
        n64keyboard_console_put(ch);
}

static void
n64keyboard_console_state(unsigned port, struct n64keyboard_state *state)
{
    unsigned i;
    int shift;
    int ctrl;

    shift = n64keyboard_code_down(N64_RANDNET_KEY_LEFT_SHIFT, state->key) ||
        n64keyboard_code_down(N64_RANDNET_KEY_RIGHT_SHIFT, state->key);
    ctrl = n64keyboard_code_down(N64_RANDNET_KEY_LEFT_CTRL, state->key);

    for (i = 0; i < 3; ++i) {
        if (state->key[i] == 0)
            continue;
        if (n64keyboard_code_down(state->key[i],
            n64keyboard_console_prev[port]))
            continue;
        n64keyboard_console_key(port, state->key[i], shift, ctrl);
    }
    bcopy(state->key, n64keyboard_console_prev[port],
        sizeof(n64keyboard_console_prev[port]));
}

static int
n64keyboard_console_find(void)
{
    struct n64joybus_port info;
    unsigned port;

    for (port = 0; port < N64_JOYBUS_PORT_COUNT; ++port) {
        if (n64joybus_identify_port(port, &info) != 0)
            continue;
        if (info.identifier == N64_JOYBUS_ID_RANDNET_KEYBOARD)
            return port;
    }
    return -1;
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

void
n64keyboard_console_intr(void)
{
    struct n64keyboard_state state;
    unsigned ticks;
    int port;
    int error;

    ticks = ct_ticks;
    if (n64keyboard_console_port < 0) {
        if ((unsigned)(ticks - n64keyboard_console_last_scan) <
            N64_KEYBOARD_SCAN_TICKS)
            return;
        n64keyboard_console_last_scan = ticks;
        port = n64keyboard_console_find();
        if (port < 0)
            return;
        n64keyboard_console_port = port;
        bzero(n64keyboard_console_prev[port],
            sizeof(n64keyboard_console_prev[port]));
    } else {
        if ((unsigned)(ticks - n64keyboard_console_last_poll) <
            N64_KEYBOARD_POLL_TICKS)
            return;
    }

    n64keyboard_console_last_poll = ticks;
    port = n64keyboard_console_port;
    error = n64keyboard_get_state(port, &state);
    if (error == EBUSY)
        return;
    if (error) {
        bzero(n64keyboard_console_prev[port],
            sizeof(n64keyboard_console_prev[port]));
        n64keyboard_console_port = -1;
        return;
    }
    n64keyboard_console_state(port, &state);
}

int
n64keyboard_console_poll(void)
{
    int ready;
    int s;

    s = spltty();
    ready = n64keyboard_console_head != n64keyboard_console_tail;
    splx(s);
    return ready;
}

int
n64keyboard_console_getc(void)
{
    int ch;
    int s;

    for (;;) {
        s = spltty();
        if (n64keyboard_console_head != n64keyboard_console_tail) {
            ch = n64keyboard_console_q[n64keyboard_console_tail];
            n64keyboard_console_tail =
                (n64keyboard_console_tail + 1) %
                N64_KEYBOARD_CONSOLE_QSIZE;
            splx(s);
            return ch;
        }
        splx(s);
        n64keyboard_console_intr();
    }
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
