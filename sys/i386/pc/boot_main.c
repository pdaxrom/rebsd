#include "boot.h"

static void
i386_cpuid(i386_u32 leaf, i386_u32 *eax, i386_u32 *ebx,
    i386_u32 *ecx, i386_u32 *edx)
{
    __asm__ volatile ("cpuid"
        : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
        : "a" (leaf), "c" (0));
}

static void
i386_store_cpuid_word(char *text, i386_u32 value)
{
    text[0] = (char)(value & 0xffu);
    text[1] = (char)((value >> 8) & 0xffu);
    text[2] = (char)((value >> 16) & 0xffu);
    text[3] = (char)((value >> 24) & 0xffu);
}

static void
i386_print_cpu(void)
{
    i386_u32 eax;
    i386_u32 ebx;
    i386_u32 ecx;
    i386_u32 edx;
    char vendor[13];
    unsigned index;

    i386_cpuid(0, &eax, &ebx, &ecx, &edx);
    i386_store_cpuid_word(vendor, ebx);
    i386_store_cpuid_word(vendor + 4, edx);
    i386_store_cpuid_word(vendor + 8, ecx);
    vendor[12] = '\0';

    i386_early_puts("cpu-vendor: ");
    for (index = 0; index < 12; ++index)
        i386_early_putc(vendor[index]);
    i386_early_putc('\n');

    i386_cpuid(1, &eax, &ebx, &ecx, &edx);
    i386_early_puts("cpu-signature: ");
    i386_early_put_hex32(eax);
    i386_early_putc('\n');
}

static i386_u8
i386_boot_byte(i386_u32 boot_params_phys, i386_u32 offset)
{
    const volatile i386_u8 *bytes;

    bytes = (const volatile i386_u8 *)boot_params_phys;
    return bytes[offset];
}

static const struct i386_e820_entry *
i386_boot_e820(i386_u32 boot_params_phys)
{
    return (const struct i386_e820_entry *)
        (boot_params_phys + I386_BOOT_PARAMS_E820_TABLE);
}

static unsigned
i386_print_e820(i386_u32 boot_params_phys)
{
    const struct i386_e820_entry *entry;
    unsigned count;
    unsigned index;
    unsigned shown;

    count = i386_boot_byte(boot_params_phys,
        I386_BOOT_PARAMS_E820_COUNT);
    if (count > I386_E820_MAX_ENTRIES)
        count = I386_E820_MAX_ENTRIES;

    i386_early_puts("e820-count: ");
    i386_early_put_hex32(count);
    i386_early_putc('\n');

    entry = i386_boot_e820(boot_params_phys);
    shown = count < 8u ? count : 8u;
    for (index = 0; index < shown; ++index) {
        i386_early_puts("e820[");
        i386_early_put_hex32(index);
        i386_early_puts("] base=");
        i386_early_put_hex64(entry[index].addr_hi, entry[index].addr_lo);
        i386_early_puts(" size=");
        i386_early_put_hex64(entry[index].size_hi, entry[index].size_lo);
        i386_early_puts(" type=");
        i386_early_put_hex32(entry[index].type);
        i386_early_putc('\n');
    }

    return count;
}

void
i386_boot_main(i386_u32 boot_params_phys)
{
    unsigned e820_count;

    i386_early_console_init();
    i386_early_puts("REBSD_I686_BOOT\n");
    i386_early_puts("cpu: i686\n");
    i386_early_puts("boot: linux-x86-2.02\n");
    i386_early_puts("boot-params: ");
    i386_early_put_hex32(boot_params_phys);
    i386_early_putc('\n');
    i386_print_cpu();

    e820_count = i386_print_e820(boot_params_phys);
    if (e820_count == 0) {
        i386_early_puts("memory-map: failed\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    i386_early_puts("memory-map: ok\n");
    i386_early_puts("gdt: ok\n");
    i386_early_puts("console: com1,vga\n");
    i386_early_puts("HALT\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
