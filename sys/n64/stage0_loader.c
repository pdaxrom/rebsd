typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned int uintptr;

#define RDRAM_KSEG1_BASE        ((uintptr)0xa0000000u)
#define RDRAM_KSEG0_BASE        ((uintptr)0x80000000u)
#define N64_BASE_RDRAM_SIZE     0x00400000u
#define N64_ICACHE_LINE_SIZE    32u

#ifdef N64CART
#define N64CART_UART_BASE       ((uintptr)0xbfd01000u)
#define N64CART_UART_CTRL       0x00u
#define N64CART_UART_RXTX       0x04u
#define N64CART_UART_TX_FREE    0x02u
#endif

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

void stage0_jump_kernel(u32 entry);

extern const u8 __n64_kernel_elf_start[];
extern const u8 __n64_kernel_elf_end[];

static void
memory_barrier(void)
{
    __asm__ volatile("" ::: "memory");
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
sync_instruction_range(uintptr start, uintptr end)
{
    sync_memory();
    invalidate_instruction_cache_range(start, end);
    sync_memory();
}

#ifdef N64CART
static volatile u32 *
n64cart_reg(u32 offset)
{
    return (volatile u32 *)(N64CART_UART_BASE + offset);
}

static u32
n64cart_io_read(u32 offset)
{
    u32 value;

    memory_barrier();
    value = *n64cart_reg(offset);
    memory_barrier();
    return value;
}

static void
n64cart_io_write(u32 offset, u32 value)
{
    memory_barrier();
    *n64cart_reg(offset) = value;
    memory_barrier();
}

static void
uart_putc_raw(char ch)
{
    while ((n64cart_io_read(N64CART_UART_CTRL) & N64CART_UART_TX_FREE) == 0u)
        ;

    n64cart_io_write(N64CART_UART_RXTX, (u32)(u8)ch);
    (void)n64cart_io_read(N64CART_UART_CTRL);
}
#else
static void
uart_putc_raw(char ch)
{
    (void)ch;
}
#endif

static void
uart_putc(char ch)
{
    if (ch == '\n')
        uart_putc_raw('\r');
    uart_putc_raw(ch);
}

static void
uart_puts(const char *text)
{
    while (*text != '\0')
        uart_putc(*text++);
}

static void
uart_put_hex32(u32 value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    uart_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        uart_putc(digits[(value >> (u32)shift) & 0x0fu]);
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
    if ((addr & 0xe0000000u) == 0x80000000u) {
        *offset = addr & 0x1fffffffu;
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

        copy_from_kernel_blob(RDRAM_KSEG1_BASE + dst_offset, offset, filesz);
        zero_bytes(RDRAM_KSEG1_BASE + dst_offset + filesz, memsz - filesz);

        if (memsz != 0u) {
            if (dst_offset < loaded_start)
                loaded_start = dst_offset;
            if (dst_offset + memsz > loaded_end)
                loaded_end = dst_offset + memsz;
        }
    }

    if (loaded_end > loaded_start)
        sync_instruction_range(RDRAM_KSEG0_BASE + loaded_start,
            RDRAM_KSEG0_BASE + loaded_end);

    return 0;
}

void
stage0_main(void)
{
    struct kernel_elf kernel;

    kernel.entry = 0;
    kernel.phoff = 0;
    kernel.phnum = 0;

    uart_puts("RetroBSD N64 stage0\n");
    uart_puts("kernel blob size=");
    uart_put_hex32(kernel_blob_size());
    uart_puts("\n");

    if (validate_kernel_elf(&kernel) != 0) {
        uart_puts("kernel magic=");
        uart_put_hex32(elf_read32(E_IDENT));
        uart_puts("\nkernel ELF32 invalid\n");
        halt();
    }
    if (load_kernel_elf(&kernel) != 0) {
        uart_puts("kernel ELF32 load failed\n");
        halt();
    }

    uart_puts("jump kernel entry=");
    uart_put_hex32(kernel.entry);
    uart_puts("\n");
    stage0_jump_kernel(kernel.entry);
    halt();
}
