#ifndef _N64_GDB_H_
#define _N64_GDB_H_

void n64_gdb_init(void);
int n64_gdb_exception(int *, unsigned, unsigned);
void n64_gdb_panic(int *, unsigned, unsigned);

void n64_gdb_usb_init(void);
int n64_gdb_usb_ready(void);
int n64_gdb_usb_attached(void);
void n64_gdb_usb_detach(void);
void n64_gdb_usb_poll(void);
int n64_gdb_usb_interrupt(void);
void n64_gdb_usb_clear_interrupt(void);
int n64_gdb_usb_getc(void);
int n64_gdb_usb_write(const unsigned char *, unsigned);

#endif
