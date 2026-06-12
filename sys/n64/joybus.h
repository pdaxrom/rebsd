#ifndef _N64_JOYBUS_H_
#define _N64_JOYBUS_H_

#include <sys/ioctl.h>

#define N64_JOYBUS_PORT_COUNT          4
#define N64_JOYBUS_BLOCK_SIZE          64

#define N64_JOYBUS_ID_NONE             0x0000
#define N64_JOYBUS_ID_VRU              0x0001
#define N64_JOYBUS_ID_RANDNET_KEYBOARD 0x0002
#define N64_JOYBUS_ID_N64_MOUSE        0x0200
#define N64_JOYBUS_ID_N64_CONTROLLER   0x0500

#define N64_JOYPAD_A                   0x8000
#define N64_JOYPAD_B                   0x4000
#define N64_JOYPAD_Z                   0x2000
#define N64_JOYPAD_START               0x1000
#define N64_JOYPAD_D_UP                0x0800
#define N64_JOYPAD_D_DOWN              0x0400
#define N64_JOYPAD_D_LEFT              0x0200
#define N64_JOYPAD_D_RIGHT             0x0100
#define N64_JOYPAD_RESET               0x0080
#define N64_JOYPAD_L                   0x0020
#define N64_JOYPAD_R                   0x0010
#define N64_JOYPAD_C_UP                0x0008
#define N64_JOYPAD_C_DOWN              0x0004
#define N64_JOYPAD_C_LEFT              0x0002
#define N64_JOYPAD_C_RIGHT             0x0001

#define N64_KBD_LED_NUM_LOCK           0x01
#define N64_KBD_LED_CAPS_LOCK          0x02
#define N64_KBD_LED_POWER              0x04

struct n64joybus_port {
    unsigned port;
    unsigned identifier;
    unsigned status;
    unsigned present;
};

struct n64joypad_state {
    unsigned port;
    unsigned identifier;
    unsigned status;
    unsigned present;
    unsigned buttons;
    int stick_x;
    int stick_y;
};

struct n64mouse_state {
    unsigned port;
    unsigned identifier;
    unsigned status;
    unsigned present;
    unsigned buttons;
    int dx;
    int dy;
};

struct n64keyboard_state {
    unsigned port;
    unsigned identifier;
    unsigned status;
    unsigned present;
    unsigned led;
    unsigned key[3];
};

#define N64JOYBUSIOC_IDENTIFY   _IOR('J', 1, struct n64joybus_port)
#define N64JOYPADIOC_GETSTATE   _IOR('J', 2, struct n64joypad_state)
#define N64MOUSEIOC_GETSTATE    _IOR('J', 3, struct n64mouse_state)
#define N64KBDIOC_GETSTATE      _IOR('J', 4, struct n64keyboard_state)
#define N64KBDIOC_SETLED        _IOW('J', 5, unsigned)

#ifdef KERNEL
struct uio;

int n64joybus_identify_port(unsigned port, struct n64joybus_port *info);
int n64joypad_get_state(unsigned port, struct n64joypad_state *state);
int n64mouse_get_state(unsigned port, struct n64mouse_state *state);
int n64keyboard_get_state(unsigned port, struct n64keyboard_state *state);
void n64keyboard_console_intr(void);
int n64keyboard_console_poll(void);
int n64keyboard_console_getc(void);

int n64joypad_open(dev_t dev, int flag, int mode);
int n64joypad_close(dev_t dev, int flag, int mode);
int n64joypad_read(dev_t dev, struct uio *uio, int flag);
int n64joypad_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);

int n64mouse_open(dev_t dev, int flag, int mode);
int n64mouse_close(dev_t dev, int flag, int mode);
int n64mouse_read(dev_t dev, struct uio *uio, int flag);
int n64mouse_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);

int n64keyboard_open(dev_t dev, int flag, int mode);
int n64keyboard_close(dev_t dev, int flag, int mode);
int n64keyboard_read(dev_t dev, struct uio *uio, int flag);
int n64keyboard_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
#endif

#endif
