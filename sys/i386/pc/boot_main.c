#include "boot.h"
#include "interrupt.h"
#include "memory.h"
#include "paging.h"
#include "syscall.h"
#include "tss.h"

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/console.h>

static i386_u32 i386_boot_params_saved;
static char i386_boot_command_line_saved[256];

extern int main(void);
static void i386_boot_fatal(const char *) __attribute__((noreturn));

static i386_u8
i386_boot_byte(i386_u32 offset)
{
    const volatile i386_u8 *bytes;

    bytes = (const volatile i386_u8 *)i386_boot_params_saved;
    return bytes[offset];
}

static i386_u32
i386_boot_word(i386_u32 offset)
{
    const volatile i386_u8 *bytes;

    bytes = (const volatile i386_u8 *)i386_boot_params_saved;
    return (i386_u32)bytes[offset] |
        ((i386_u32)bytes[offset + 1u] << 8) |
        ((i386_u32)bytes[offset + 2u] << 16) |
        ((i386_u32)bytes[offset + 3u] << 24);
}

static void
i386_boot_save_command_line(void)
{
    const volatile char *source;
    i386_u32 address;
    unsigned index;

    i386_boot_command_line_saved[0] = '\0';
    address = i386_boot_word(I386_BOOT_PARAMS_CMDLINE_PTR);
    if (address == 0 || address > 0x3fffff00u)
        return;
    source = (const volatile char *)address;
    for (index = 0; index + 1u < sizeof(i386_boot_command_line_saved);
        ++index) {
        i386_boot_command_line_saved[index] = source[index];
        if (source[index] == '\0')
            return;
    }
    i386_boot_command_line_saved[index] = '\0';
}

const char *
i386_boot_command_line(void)
{
    return i386_boot_command_line_saved;
}

static void
i386_boot_fatal(const char *message)
{
    i386_early_puts(message);
    i386_early_putc('\n');
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void
startup(void)
{
    i386_early_console_init();
    i386_boot_save_command_line();
    i386_early_puts("REBSD_I686_BOOT\n");
    i386_early_puts("cpu: i686\n");
    i386_early_puts("boot: linux-x86-2.02\n");
    if (i386_boot_byte(I386_BOOT_PARAMS_LOADER_TYPE) ==
        I386_BOOT_LOADER_BIOS &&
        i386_boot_byte(I386_BOOT_PARAMS_EXT_LOADER_VER) ==
        I386_BOOT_LOADER_BIOS_VER &&
        i386_boot_byte(I386_BOOT_PARAMS_EXT_LOADER_TYPE) ==
        I386_BOOT_LOADER_BIOS_EXT)
        i386_early_puts("boot-loader: bios-int13\n");
    else
        i386_early_puts("boot-loader: linux-protocol\n");

    if (i386_boot_byte(I386_BOOT_PARAMS_E820_COUNT) == 0)
        i386_boot_fatal("memory-map: failed");
    i386_early_puts("memory-map: ok\n");

    if (i386_tss_init() != 0)
        i386_boot_fatal("tss: failed");
    i386_tss_set_kernel_stack(
        (unsigned)(unsigned long)md_curuser + USIZE);
    i386_idt_init();
    i386_pic_init();
    if (!i386_console_irq_enable())
        i386_boot_fatal("console-irq: failed");
    i386_early_puts("interrupts: idt,pic ready\n");

    if (!i386_memory_init(i386_boot_params_saved))
        i386_boot_fatal("memory-normalized: failed");
    if (!i386_paging_init())
        i386_boot_fatal("paging: failed");
    i386_memory_handoff();
    i386_early_puts("memory-normalized: ok\n");
    i386_early_puts("paging: on\n");

    if (i386_syscall_install_production() != 0)
        i386_boot_fatal("syscall-production: failed");
    i386_early_puts("syscall-production: ok\n");
    boothowto = RB_RDONLY;
}

void
i386_boot_main(i386_u32 boot_params_phys)
{
    i386_boot_params_saved = boot_params_phys;
    (void)main();
    i386_boot_fatal("PANIC: kernel main returned");
}
