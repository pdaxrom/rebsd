#ifndef _I386_BOOT_H_
#define _I386_BOOT_H_

typedef unsigned char i386_u8;
typedef unsigned short i386_u16;
typedef unsigned int i386_u32;

#define I386_BOOT_PROTOCOL_VERSION  0x0202u
#define I386_BOOT_PARAMS_E820_COUNT 0x01e8u
#define I386_BOOT_PARAMS_CMDLINE_PTR 0x0228u
#define I386_BOOT_PARAMS_E820_TABLE 0x02d0u
#define I386_E820_MAX_ENTRIES       128u

struct i386_e820_entry {
    i386_u32 addr_lo;
    i386_u32 addr_hi;
    i386_u32 size_lo;
    i386_u32 size_hi;
    i386_u32 type;
} __attribute__((packed));

void i386_early_console_init(void);
void i386_early_putc(char ch);
void i386_early_puts(const char *text);
void i386_early_put_hex32(i386_u32 value);
void i386_early_put_hex64(i386_u32 high, i386_u32 low);
const char *i386_boot_command_line(void);
int i386_boot_command_has(const char *token);

void i386_boot_main(i386_u32 boot_params_phys)
    __attribute__((noreturn));

#endif
