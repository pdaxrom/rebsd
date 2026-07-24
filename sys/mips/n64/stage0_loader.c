typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned int uintptr;

#include "stage0_console.h"
#include "layout.h"

#ifndef N64_ROM_BUILD_DATE
#define N64_ROM_BUILD_DATE "unknown"
#endif
#ifndef N64_ROM_KERNEL_COMPILER
#define N64_ROM_KERNEL_COMPILER "unknown"
#endif
#ifndef N64_ROM_USERLAND_COMPILER
#define N64_ROM_USERLAND_COMPILER "unknown"
#endif

#define N64_DCACHE_LINE_SIZE    16u
#define N64_ICACHE_LINE_SIZE    32u
#define N64_SP_DMEM_BOOTINFO_FLAGS_ADDR  0xa4000004u
#define N64_BOOTINFO_RESET_SHIFT         8u
#define N64_BOOTINFO_RESET_MASK          0xffu
#define N64_RESET_TYPE_ADDR              0xa000030cu
#define N64_RESET_TYPE_COLD              0u
#define N64_RESET_TYPE_NMI               1u
#define N64_RESET_DUMP_ADDR               0xa000031cu
#define N64_RESET_DUMP_BACKUP_ADDR        0xa000035cu
#define N64_RESET_DUMP_MAGIC              0x52445354u
#define N64_RESET_DUMP_VERSION            4u
#define N64_RESET_DUMP_VERSION_MASK       0x0000ffffu
#define N64_RESET_DUMP_STATE_MASK         0xffff0000u
#define N64_RESET_DUMP_STATE_LIVE         0x00010000u
#define N64_RESET_DUMP_STATE_CAPTURED     0x00020000u
#define N64_RESET_DUMP_STATE_DISPLAYED    0x00030000u
#define N64_RESET_DUMP_STATE_MISSING      0x00040000u
#define N64_RESET_DUMP_STATE_REBOOT       0x00050000u
#define N64_RESET_DUMP_XOR                0x4e363452u
#define N64_RESET_DUMP_WORDS              16u

#define N64CART_LED_CTRL_ADDR              0xbfd01008u
#define N64CART_LED_OFF                     0x00000000u
#define N64CART_LED_RESET                   0x007f0000u

#define EI_CLASS        4u
#define EI_DATA         5u
#define EI_VERSION      6u
#define ELFCLASS32      1u
#define ELFDATA2MSB     2u
#define EV_CURRENT      1u
#define ET_EXEC         2u
#define EM_MIPS         8u
#define PT_LOAD         1u

#define ELF32_EHDR_SIZE 52u
#define ELF32_PHDR_SIZE 32u

#define E_IDENT         0u
#define E_TYPE          16u
#define E_MACHINE       18u
#define E_VERSION       20u
#define E_ENTRY         24u
#define E_PHOFF         28u
#define E_EHSIZE        40u
#define E_PHENTSIZE     42u
#define E_PHNUM         44u

#define P_TYPE          0u
#define P_OFFSET        4u
#define P_VADDR         8u
#define P_PADDR         12u
#define P_FILESZ        16u
#define P_MEMSZ         20u

struct kernel_elf {
    u32 entry;
    u32 phoff;
    u16 phnum;
};

struct reset_dump_critical {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 pc;
    u32 cause;
    u32 status;
    u32 badvaddr;
    u32 sp;
    u32 ra;
    u32 pid;
    u32 bt_count;
    u32 bt[5];
};

typedef char reset_dump_critical_size[
    sizeof(struct reset_dump_critical) == 64u ? 1 : -1];

void stage0_jump_kernel(u32 entry);

extern const u8 __n64_kernel_elf_start[];
extern const u8 __n64_kernel_elf_end[];

/*
 * The default n64tool header is libdragon's non-compatibility IPL3.  Unlike
 * Nintendo/compatibility IPL3s, it passes reset type in bootinfo.flags in SP
 * DMEM and deliberately does not populate the legacy word at 0x8000030c.
 * Publish that value for the kernel before SP DMEM can be reused.  This ROM
 * embeds that exact production IPL3, so do not apply compatibility-IPL3
 * heuristics to the bootinfo words.
 */
static u32
publish_reset_type(void)
{
    volatile u32 *const boot_flags =
        (volatile u32 *)N64_SP_DMEM_BOOTINFO_FLAGS_ADDR;
    volatile u32 *const reset_type =
        (volatile u32 *)N64_RESET_TYPE_ADDR;
    u32 value;

    value = (*boot_flags >> N64_BOOTINFO_RESET_SHIFT) &
        N64_BOOTINFO_RESET_MASK;
    *reset_type = value;
    return value;
}

static u32
align_down(u32 value, u32 align)
{
    return value & ~(align - 1u);
}

static void
sync_memory(void)
{
    __asm__ volatile("sync" ::: "memory");
}

static void
cache_hit_invalidate_i(uintptr addr)
{
    __asm__ volatile("cache 0x10, 0(%0)" :: "r"(addr) : "memory");
}

static void
cache_hit_invalidate_d(uintptr addr)
{
    /*
     * The ELF image was written through uncached KSEG1.  Discard any KSEG0
     * line left by the previous kernel without writing it back over the new
     * image.
     */
    __asm__ volatile("cache 0x11, 0(%0)" :: "r"(addr) : "memory");
}

static void
invalidate_data_cache_range(uintptr start, uintptr end)
{
    uintptr addr;

    if (end <= start)
        return;

    addr = (uintptr)align_down((u32)start, N64_DCACHE_LINE_SIZE);
    while (addr < end) {
        cache_hit_invalidate_d(addr);
        addr += N64_DCACHE_LINE_SIZE;
    }
}

static void
invalidate_instruction_cache_range(uintptr start, uintptr end)
{
    uintptr addr;

    if (end <= start)
        return;

    addr = (uintptr)align_down((u32)start, N64_ICACHE_LINE_SIZE);
    while (addr < end) {
        cache_hit_invalidate_i(addr);
        addr += N64_ICACHE_LINE_SIZE;
    }
}

static void
sync_loaded_range(uintptr start, uintptr end)
{
    sync_memory();
    invalidate_data_cache_range(start, end);
    sync_memory();
    invalidate_instruction_cache_range(start, end);
    sync_memory();
}

static void
stage0_putc(char ch)
{
    if (ch == '\n')
        stage0_console_putc('\r');
    stage0_console_putc(ch);
}

static void
stage0_puts(const char *text)
{
    while (*text != '\0')
        stage0_putc(*text++);
}

static void
stage0_put_hex32(u32 value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    stage0_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        stage0_putc(digits[(value >> (u32)shift) & 0x0fu]);
}

static u32
stage0_reset_checksum(const volatile u32 *words)
{
    u32 value;
    u32 i;

    value = N64_RESET_DUMP_XOR;
    for (i = 0; i < N64_RESET_DUMP_WORDS; ++i)
        if (i != 2u)
            value ^= words[i];
    return value;
}

static int
stage0_reset_state_valid(u32 state)
{
    return state == 0u || state == N64_RESET_DUMP_STATE_LIVE ||
        state == N64_RESET_DUMP_STATE_CAPTURED ||
        state == N64_RESET_DUMP_STATE_DISPLAYED ||
        state == N64_RESET_DUMP_STATE_MISSING ||
        state == N64_RESET_DUMP_STATE_REBOOT;
}

static const char *
stage0_reset_state_name(u32 state)
{
    if (state == N64_RESET_DUMP_STATE_LIVE)
        return "last fatal exception";
    if (state == N64_RESET_DUMP_STATE_CAPTURED)
        return "exact IP4 capture";
    if (state == N64_RESET_DUMP_STATE_DISPLAYED)
        return "displayed";
    if (state == N64_RESET_DUMP_STATE_MISSING)
        return "missing";
    if (state == N64_RESET_DUMP_STATE_REBOOT)
        return "reboot acknowledgement";
    return "legacy";
}

static int
stage0_reset_dump_valid(
    const volatile struct reset_dump_critical *dump)
{
    u32 state;

    state = dump->version & N64_RESET_DUMP_STATE_MASK;
    return dump->magic == N64_RESET_DUMP_MAGIC &&
        (dump->version & N64_RESET_DUMP_VERSION_MASK) ==
            N64_RESET_DUMP_VERSION &&
        stage0_reset_state_valid(state) &&
        dump->checksum == stage0_reset_checksum(
            (const volatile u32 *)dump);
}

/*
 * Report retained context before loading or entering the kernel.  This path
 * uses only KSEG1 and the stage0 UART, so it remains useful when the kernel
 * was stuck with interrupts masked or its VM/RDRAM state was corrupt.
 */
static void
stage0_reset_dump_summary(u32 reset_type)
{
    volatile struct reset_dump_critical *dump;
    u32 state;

    dump = (volatile struct reset_dump_critical *)N64_RESET_DUMP_ADDR;
    if (!stage0_reset_dump_valid(dump))
        dump = (volatile struct reset_dump_critical *)
            N64_RESET_DUMP_BACKUP_ADDR;
    if (!stage0_reset_dump_valid(dump)) {
        if (reset_type == N64_RESET_TYPE_NMI)
            stage0_puts("RESET retained: unavailable\n");
        return;
    }

    state = dump->version & N64_RESET_DUMP_STATE_MASK;

    stage0_puts("RESET retained: ");
    stage0_puts(stage0_reset_state_name(state));
    stage0_puts(" from RDRAM");
    stage0_puts("\nreset pc=");
    stage0_put_hex32(dump->pc);
    stage0_puts(" ra=");
    stage0_put_hex32(dump->ra);
    stage0_puts(" sp=");
    stage0_put_hex32(dump->sp);
    stage0_puts("\nreset cause=");
    stage0_put_hex32(dump->cause);
    stage0_puts(" status=");
    stage0_put_hex32(dump->status);
    stage0_puts(" badva=");
    stage0_put_hex32(dump->badvaddr);
    stage0_puts(" pid=");
    stage0_put_hex32(dump->pid);
    stage0_puts("\n");
}

static u32
kernel_blob_size(void)
{
    return (u32)(__n64_kernel_elf_end - __n64_kernel_elf_start);
}

static u8
elf_read8(u32 offset)
{
    if (offset >= kernel_blob_size())
        return 0u;
    return __n64_kernel_elf_start[offset];
}

static u16
elf_read16(u32 offset)
{
    return ((u16)elf_read8(offset) << 8) |
        (u16)elf_read8(offset + 1u);
}

static u32
elf_read32(u32 offset)
{
    return ((u32)elf_read8(offset) << 24) |
        ((u32)elf_read8(offset + 1u) << 16) |
        ((u32)elf_read8(offset + 2u) << 8) |
        (u32)elf_read8(offset + 3u);
}

static void
halt(void)
{
    for (;;)
        ;
}

static void
copy_from_kernel_blob(uintptr dst, u32 offset, u32 size)
{
    u8 *d = (u8 *)dst;

    while (size != 0u) {
        *d++ = elf_read8(offset++);
        --size;
    }
}

static void
zero_bytes(uintptr dst, u32 size)
{
    u8 *d = (u8 *)dst;

    while (size != 0u) {
        *d++ = 0u;
        --size;
    }
}

static int
rdram_offset(u32 addr, u32 *offset)
{
    if (addr < N64_BASE_RDRAM_SIZE) {
        *offset = addr;
        return 0;
    }
    if ((addr & N64_KSEG_ADDR_MASK) == N64_KSEG0_BASE) {
        *offset = addr & N64_KSEG_PHYS_MASK;
        return *offset < N64_BASE_RDRAM_SIZE ? 0 : -1;
    }
    return -1;
}

static int
validate_kernel_elf(struct kernel_elf *kernel)
{
    u32 phoff;
    u16 phentsize;
    u16 phnum;

    if (kernel_blob_size() < ELF32_EHDR_SIZE)
        return -1;
    if (elf_read32(E_IDENT) != 0x7f454c46u)
        return -1;
    if (elf_read8(EI_CLASS) != ELFCLASS32 ||
        elf_read8(EI_DATA) != ELFDATA2MSB ||
        elf_read8(EI_VERSION) != EV_CURRENT)
        return -1;
    if (elf_read16(E_TYPE) != ET_EXEC ||
        elf_read16(E_MACHINE) != EM_MIPS ||
        elf_read32(E_VERSION) != EV_CURRENT ||
        elf_read16(E_EHSIZE) != ELF32_EHDR_SIZE)
        return -1;

    phoff = elf_read32(E_PHOFF);
    phentsize = elf_read16(E_PHENTSIZE);
    phnum = elf_read16(E_PHNUM);
    if (phentsize != ELF32_PHDR_SIZE || phnum == 0u || phnum > 16u)
        return -1;
    if (phoff > kernel_blob_size() ||
        phoff + ((u32)phnum * ELF32_PHDR_SIZE) > kernel_blob_size())
        return -1;

    kernel->entry = elf_read32(E_ENTRY);
    kernel->phoff = phoff;
    kernel->phnum = phnum;
    return 0;
}

static int
load_kernel_elf(const struct kernel_elf *kernel)
{
    u16 i;
    u32 loaded_start = 0xffffffffu;
    u32 loaded_end = 0u;

    for (i = 0; i < kernel->phnum; ++i) {
        u32 phdr = kernel->phoff + ((u32)i * ELF32_PHDR_SIZE);
        u32 offset;
        u32 paddr;
        u32 filesz;
        u32 memsz;
        u32 dst_offset;

        if (elf_read32(phdr + P_TYPE) != PT_LOAD)
            continue;

        offset = elf_read32(phdr + P_OFFSET);
        paddr = elf_read32(phdr + P_PADDR);
        if (paddr == 0u)
            paddr = elf_read32(phdr + P_VADDR);
        filesz = elf_read32(phdr + P_FILESZ);
        memsz = elf_read32(phdr + P_MEMSZ);

        if (filesz > memsz || rdram_offset(paddr, &dst_offset) != 0 ||
            memsz > (N64_BASE_RDRAM_SIZE - dst_offset))
            return -1;
        if (offset > kernel_blob_size() || filesz > kernel_blob_size() - offset)
            return -1;

        copy_from_kernel_blob((uintptr)N64_KSEG1_BASE + dst_offset,
            offset, filesz);
        zero_bytes((uintptr)N64_KSEG1_BASE + dst_offset + filesz,
            memsz - filesz);

        if (memsz != 0u) {
            if (dst_offset < loaded_start)
                loaded_start = dst_offset;
            if (dst_offset + memsz > loaded_end)
                loaded_end = dst_offset + memsz;
        }
    }

    if (loaded_end > loaded_start)
        sync_loaded_range((uintptr)N64_KSEG0_BASE + loaded_start,
            (uintptr)N64_KSEG0_BASE + loaded_end);

    return 0;
}

void
stage0_main(void)
{
    struct kernel_elf kernel;
    u32 reset_type;

    reset_type = publish_reset_type();

#ifdef N64_RESET_DUMP
    /* Actual NMI also gets an N64-side PI breadcrumb before kernel load. */
    *(volatile u32 *)N64CART_LED_CTRL_ADDR =
        reset_type == N64_RESET_TYPE_NMI ?
        N64CART_LED_RESET : N64CART_LED_OFF;
    __asm__ volatile ("sync" ::: "memory");
#endif

    kernel.entry = 0;
    kernel.phoff = 0;
    kernel.phnum = 0;

    stage0_puts("ReBSD N64 stage0\n");
    stage0_puts("ROM build: " N64_ROM_BUILD_DATE "\n");
    stage0_puts("ROM compilers: kernel=" N64_ROM_KERNEL_COMPILER
        " userland=" N64_ROM_USERLAND_COMPILER "\n");
    stage0_puts("UART transport: polling (default)\n");
#ifdef N64_MINIMAL_USBNET_DEBUG
    stage0_puts("USB network debug: minimal CDC ECM\n");
#endif
    stage0_puts("boot reset type: ");
    stage0_put_hex32(reset_type);
    stage0_puts("\n");
#ifdef N64_RESET_DUMP
    stage0_puts("RESET N64-side PI LED: enabled\n");
#endif
    stage0_reset_dump_summary(reset_type);
    stage0_puts("kernel blob size=");
    stage0_put_hex32(kernel_blob_size());
    stage0_puts("\n");

    if (validate_kernel_elf(&kernel) != 0) {
        stage0_puts("kernel magic=");
        stage0_put_hex32(elf_read32(E_IDENT));
        stage0_puts("\nkernel ELF32 invalid\n");
        halt();
    }
    if (load_kernel_elf(&kernel) != 0) {
        stage0_puts("kernel ELF32 load failed\n");
        halt();
    }

    stage0_puts("jump kernel entry=");
    stage0_put_hex32(kernel.entry);
    stage0_puts("\n");
    stage0_jump_kernel(kernel.entry);
    halt();
}
