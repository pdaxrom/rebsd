#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/joybus.h>

static void
usage(void)
{
    fprintf(stderr, "usage: n64input list\n");
    fprintf(stderr, "       n64input joypad [port]\n");
    fprintf(stderr, "       n64input mouse [port]\n");
    fprintf(stderr, "       n64input kbd [port]\n");
    fprintf(stderr, "       n64input kbd-led port value\n");
    exit(1);
}

static int
parse_port(const char *text)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno || *text == '\0' || *end != '\0' ||
        value >= N64_JOYBUS_PORT_COUNT)
        usage();
    return (int)value;
}

static unsigned
parse_uint(const char *text)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno || *text == '\0' || *end != '\0')
        usage();
    return (unsigned)value;
}

static const char *
id_name(unsigned id)
{
    switch (id) {
    case N64_JOYBUS_ID_NONE:
        return "none";
    case N64_JOYBUS_ID_VRU:
        return "vru";
    case N64_JOYBUS_ID_RANDNET_KEYBOARD:
        return "keyboard";
    case N64_JOYBUS_ID_N64_MOUSE:
        return "mouse";
    case N64_JOYBUS_ID_N64_CONTROLLER:
        return "joypad";
    default:
        return "unknown";
    }
}

static void
path_for(char *path, const char *base, int port)
{
    sprintf(path, "/dev/%s%d", base, port);
}

static int
open_dev(const char *base, int port, int flags)
{
    char path[32];
    int fd;

    path_for(path, base, port);
    fd = open(path, flags);
    if (fd < 0) {
        fprintf(stderr, "n64input: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return fd;
}

static void
print_missing(const char *kind, int port)
{
    printf("%s%d: not present\n", kind, port);
}

static void
do_list(void)
{
    struct n64joybus_port info;
    int port;
    int fd;

    for (port = 0; port < N64_JOYBUS_PORT_COUNT; ++port) {
        fd = open_dev("joypad", port, O_RDONLY);
        if (ioctl(fd, N64JOYBUSIOC_IDENTIFY, &info) < 0) {
            printf("port%d: error %s\n", port, strerror(errno));
            close(fd);
            continue;
        }
        close(fd);
        printf("port%d: id=0x%04x status=0x%02x %s\n",
            port, info.identifier, info.status, id_name(info.identifier));
    }
}

static void
do_joypad(int port)
{
    struct n64joypad_state state;
    int fd;

    fd = open_dev("joypad", port, O_RDONLY);
    if (ioctl(fd, N64JOYPADIOC_GETSTATE, &state) < 0) {
        if (errno == ENODEV)
            print_missing("joypad", port);
        else
            fprintf(stderr, "n64input: joypad%d: %s\n", port,
                strerror(errno));
        close(fd);
        return;
    }
    close(fd);
    printf("joypad%d: buttons=0x%04x stick=%d,%d status=0x%02x\n",
        port, state.buttons, state.stick_x, state.stick_y, state.status);
}

static void
do_mouse(int port)
{
    struct n64mouse_state state;
    int fd;

    fd = open_dev("mouse", port, O_RDONLY);
    if (ioctl(fd, N64MOUSEIOC_GETSTATE, &state) < 0) {
        if (errno == ENODEV)
            print_missing("mouse", port);
        else
            fprintf(stderr, "n64input: mouse%d: %s\n", port,
                strerror(errno));
        close(fd);
        return;
    }
    close(fd);
    printf("mouse%d: buttons=0x%04x delta=%d,%d status=0x%02x\n",
        port, state.buttons, state.dx, state.dy, state.status);
}

static void
do_kbd(int port)
{
    struct n64keyboard_state state;
    int fd;

    fd = open_dev("kbd", port, O_RDONLY);
    if (ioctl(fd, N64KBDIOC_GETSTATE, &state) < 0) {
        if (errno == ENODEV)
            print_missing("kbd", port);
        else
            fprintf(stderr, "n64input: kbd%d: %s\n", port, strerror(errno));
        close(fd);
        return;
    }
    close(fd);
    printf("kbd%d: led=0x%02x keys=0x%04x 0x%04x 0x%04x status=0x%02x\n",
        port, state.led, state.key[0], state.key[1], state.key[2],
        state.status);
}

static void
do_kbd_led(int port, unsigned led)
{
    int fd;

    fd = open_dev("kbd", port, O_RDWR);
    if (ioctl(fd, N64KBDIOC_SETLED, &led) < 0)
        fprintf(stderr, "n64input: kbd%d led: %s\n", port, strerror(errno));
    close(fd);
}

int
main(int argc, char **argv)
{
    int port;

    if (argc < 2)
        usage();

    if (strcmp(argv[1], "list") == 0) {
        if (argc != 2)
            usage();
        do_list();
        return 0;
    }

    if (strcmp(argv[1], "kbd-led") == 0) {
        if (argc != 4)
            usage();
        do_kbd_led(parse_port(argv[2]), parse_uint(argv[3]));
        return 0;
    }

    if (argc != 2 && argc != 3)
        usage();
    port = argc == 3 ? parse_port(argv[2]) : 0;

    if (strcmp(argv[1], "joypad") == 0)
        do_joypad(port);
    else if (strcmp(argv[1], "mouse") == 0)
        do_mouse(port);
    else if (strcmp(argv[1], "kbd") == 0)
        do_kbd(port);
    else
        usage();

    return 0;
}
