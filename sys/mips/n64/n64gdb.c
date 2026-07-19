/*
 * Minimal crash-safe GDB Remote Serial Protocol stub for the VR4300.
 *
 * This file deliberately avoids tty, networking, malloc, copyin/copyout and
 * VM lookups.  While stopped it touches only static storage, CP0 state, the
 * N64cart USB controller and directly mapped RDRAM.
 */
#include <sys/param.h>
#include <machine/io.h>
#include <machine/n64.h>
#include <machine/n64gdb.h>

#define N64_GDB_PACKET_SIZE        2048u
#define N64_GDB_REGISTER_COUNT     90u
#define N64_GDB_BREAKPOINTS        16u
#define N64_GDB_BREAK_INSN         0x0000000du
#define N64_GDB_CAUSE_IP3          0x00000800u
#define N64_GDB_ENUM_WAIT_MSEC     500u

#define N64_GDB_SIGINT             2
#define N64_GDB_SIGTRAP            5
#define N64_GDB_SIGSEGV            11

struct n64_gdb_breakpoint {
    unsigned address;
    unsigned instruction;
    unsigned char used;
    unsigned char inserted;
    unsigned char temporary;
};

static unsigned char n64_gdb_in[N64_GDB_PACKET_SIZE];
static unsigned char n64_gdb_out[N64_GDB_PACKET_SIZE];
static unsigned char n64_gdb_wire[N64_GDB_PACKET_SIZE + 4];
static unsigned char n64_gdb_bytes[(N64_GDB_PACKET_SIZE - 1) / 2];
static struct n64_gdb_breakpoint n64_gdb_breakpoints[N64_GDB_BREAKPOINTS];
static struct n64_gdb_breakpoint *n64_gdb_hit_breakpoint;
static struct n64_gdb_breakpoint *n64_gdb_reinsert_breakpoint;
static int n64_gdb_reinsert_continue;
static int n64_gdb_native_break;
static unsigned n64_gdb_rawcause;
static unsigned n64_gdb_badvaddr;
static int n64_gdb_signal = N64_GDB_SIGTRAP;

static const signed char n64_gdb_frame_word[32] = {
    -1,
    FRAME_R1, FRAME_R2, FRAME_R3, FRAME_R4, FRAME_R5, FRAME_R6,
    FRAME_R7, FRAME_R8, FRAME_R9, FRAME_R10, FRAME_R11, FRAME_R12,
    FRAME_R13, FRAME_R14, FRAME_R15, FRAME_R16, FRAME_R17, FRAME_R18,
    FRAME_R19, FRAME_R20, FRAME_R21, FRAME_R22, FRAME_R23, FRAME_R24,
    FRAME_R25, -1, -1, FRAME_GP, FRAME_SP, FRAME_FP, FRAME_RA
};

static int
n64_gdb_hex(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static unsigned char
n64_gdb_hex_digit(unsigned value)
{
    static const char digits[] = "0123456789abcdef";

    return digits[value & 15u];
}

static unsigned char *
n64_gdb_put_hex32(unsigned char *p, unsigned value)
{
    int shift;

    for (shift = 28; shift >= 0; shift -= 4)
        *p++ = n64_gdb_hex_digit(value >> (unsigned)shift);
    return p;
}

static int
n64_gdb_get_hex32(const unsigned char **pp, unsigned *value)
{
    const unsigned char *p;
    unsigned result;
    int digit;
    int i;

    p = *pp;
    result = 0;
    for (i = 0; i < 8; i++) {
        digit = n64_gdb_hex(*p++);
        if (digit < 0)
            return -1;
        result = (result << 4) | (unsigned)digit;
    }
    *pp = p;
    *value = result;
    return 0;
}

static int
n64_gdb_parse_uint(const unsigned char **pp, unsigned *value)
{
    const unsigned char *p;
    unsigned result;
    int digit;
    int count;

    p = *pp;
    result = 0;
    count = 0;
    while ((digit = n64_gdb_hex(*p)) >= 0) {
        if (count == 8)
            return -1;
        result = (result << 4) | (unsigned)digit;
        ++p;
        ++count;
    }
    if (count == 0)
        return -1;
    *pp = p;
    *value = result;
    return 0;
}

static unsigned
n64_gdb_strlen(const unsigned char *s)
{
    unsigned len;

    len = 0;
    while (s[len] != 0)
        ++len;
    return len;
}

static int
n64_gdb_starts_with(const unsigned char *s, const char *prefix)
{
    while (*prefix != 0) {
        if (*s++ != (unsigned char)*prefix++)
            return 0;
    }
    return 1;
}

static void
n64_gdb_reply(const char *s)
{
    unsigned i;

    for (i = 0; s[i] != 0 && i + 1 < sizeof(n64_gdb_out); i++)
        n64_gdb_out[i] = (unsigned char)s[i];
    n64_gdb_out[i] = 0;
}

static int
n64_gdb_get_char(void)
{
    int ch;

    do {
        ch = n64_gdb_usb_getc();
    } while (ch < 0);
    return ch;
}

static int
n64_gdb_get_packet(void)
{
    unsigned checksum;
    unsigned received;
    unsigned len;
    unsigned char ack;
    int ch;
    int digit;

    for (;;) {
        do {
            ch = n64_gdb_get_char();
        } while (ch != '$');
        checksum = 0;
        len = 0;
        for (;;) {
            ch = n64_gdb_get_char();
            if (ch == '#')
                break;
            if (ch == '$') {
                checksum = 0;
                len = 0;
                continue;
            }
            if (len + 1 >= sizeof(n64_gdb_in)) {
                len = 0;
                checksum = 1;
                do {
                    ch = n64_gdb_get_char();
                } while (ch != '#');
                break;
            }
            n64_gdb_in[len++] = (unsigned char)ch;
            checksum = (checksum + (unsigned)ch) & 0xffu;
        }
        digit = n64_gdb_hex(n64_gdb_get_char());
        if (digit < 0)
            continue;
        received = (unsigned)digit << 4;
        digit = n64_gdb_hex(n64_gdb_get_char());
        if (digit < 0)
            continue;
        received |= (unsigned)digit;
        if (received != checksum) {
            ack = '-';
            (void)n64_gdb_usb_write(&ack, 1);
            continue;
        }
        ack = '+';
        (void)n64_gdb_usb_write(&ack, 1);
        n64_gdb_in[len] = 0;
        return 0;
    }
}

static int
n64_gdb_put_packet(const unsigned char *payload)
{
    unsigned checksum;
    unsigned len;
    unsigned i;
    int ch;

    len = n64_gdb_strlen(payload);
    if (len >= N64_GDB_PACKET_SIZE)
        return -1;
    n64_gdb_wire[0] = '$';
    checksum = 0;
    for (i = 0; i < len; i++) {
        n64_gdb_wire[i + 1] = payload[i];
        checksum = (checksum + payload[i]) & 0xffu;
    }
    n64_gdb_wire[len + 1] = '#';
    n64_gdb_wire[len + 2] = n64_gdb_hex_digit(checksum >> 4);
    n64_gdb_wire[len + 3] = n64_gdb_hex_digit(checksum);
    for (;;) {
        if (n64_gdb_usb_write(n64_gdb_wire, len + 4) != 0)
            return -1;
        ch = n64_gdb_get_char();
        if (ch == '+')
            return 0;
        if (ch != '-')
            return -1;
    }
}

static int
n64_gdb_direct_range(unsigned address, unsigned len, unsigned *physical)
{
    unsigned segment;
    unsigned phys;
    unsigned rdram;

    segment = address & N64_KSEG_ADDR_MASK;
    if (segment != N64_KSEG0_BASE && segment != N64_KSEG1_BASE)
        return -1;
    phys = address & N64_KSEG_PHYS_MASK;
    rdram = n64_rdram_size();
    if (phys > rdram || len > rdram - phys)
        return -1;
    *physical = phys;
    return 0;
}

static int
n64_gdb_memory_read(unsigned address, unsigned char *buf, unsigned len)
{
    volatile const unsigned char *src;
    unsigned physical;
    unsigned i;

    if (n64_gdb_direct_range(address, len, &physical) != 0)
        return -1;
    src = (volatile const unsigned char *)(N64_KSEG0_BASE | physical);
    for (i = 0; i < len; i++)
        buf[i] = src[i];
    return 0;
}

static void
n64_gdb_sync_code(unsigned address, unsigned len)
{
    unsigned start;
    unsigned end;
    unsigned p;

    start = address & ~15u;
    end = (address + len + 31u) & ~31u;
    asm volatile ("sync" ::: "memory");
    for (p = start; p < end; p += 16u)
        asm volatile ("cache 0x15, 0(%0)" :: "r" (p) : "memory");
    asm volatile ("sync" ::: "memory");
    start = address & ~31u;
    for (p = start; p < end; p += 32u)
        asm volatile ("cache 0x10, 0(%0)" :: "r" (p) : "memory");
    asm volatile ("sync" ::: "memory");
}

static int
n64_gdb_memory_write(unsigned address, const unsigned char *buf,
    unsigned len)
{
    volatile unsigned char *dst;
    unsigned physical;
    unsigned cached;
    unsigned i;

    if (n64_gdb_direct_range(address, len, &physical) != 0)
        return -1;
    cached = N64_KSEG0_BASE | physical;
    dst = (volatile unsigned char *)cached;
    for (i = 0; i < len; i++)
        dst[i] = buf[i];
    if (len != 0)
        n64_gdb_sync_code(cached, len);
    return 0;
}

static int
n64_gdb_read_word(unsigned address, unsigned *value)
{
    unsigned char bytes[4];

    if ((address & 3u) != 0 ||
        n64_gdb_memory_read(address, bytes, sizeof(bytes)) != 0)
        return -1;
    *value = ((unsigned)bytes[0] << 24) |
        ((unsigned)bytes[1] << 16) |
        ((unsigned)bytes[2] << 8) | bytes[3];
    return 0;
}

static int
n64_gdb_write_word(unsigned address, unsigned value)
{
    unsigned char bytes[4];

    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
    return n64_gdb_memory_write(address, bytes, sizeof(bytes));
}

static struct n64_gdb_breakpoint *
n64_gdb_find_breakpoint(unsigned address)
{
    unsigned i;

    for (i = 0; i < N64_GDB_BREAKPOINTS; i++)
        if (n64_gdb_breakpoints[i].used &&
            n64_gdb_breakpoints[i].address == address)
            return &n64_gdb_breakpoints[i];
    return 0;
}

static int
n64_gdb_reinsert(struct n64_gdb_breakpoint *bp)
{
    if (bp == 0 || !bp->used || bp->inserted)
        return 0;
    if (n64_gdb_write_word(bp->address, N64_GDB_BREAK_INSN) != 0)
        return -1;
    bp->inserted = 1;
    return 0;
}

static int
n64_gdb_insert_breakpoint(unsigned address, int temporary)
{
    struct n64_gdb_breakpoint *bp;
    unsigned instruction;
    unsigned i;

    bp = n64_gdb_find_breakpoint(address);
    if (bp != 0)
        return 0;
    for (i = 0; i < N64_GDB_BREAKPOINTS; i++)
        if (!n64_gdb_breakpoints[i].used)
            break;
    if (i == N64_GDB_BREAKPOINTS ||
        n64_gdb_read_word(address, &instruction) != 0 ||
        n64_gdb_write_word(address, N64_GDB_BREAK_INSN) != 0)
        return -1;
    bp = &n64_gdb_breakpoints[i];
    bp->address = address;
    bp->instruction = instruction;
    bp->temporary = temporary != 0;
    bp->inserted = 1;
    bp->used = 1;
    return 0;
}

static void
n64_gdb_restore_breakpoint(struct n64_gdb_breakpoint *bp)
{
    if (bp != 0 && bp->used && bp->inserted) {
        (void)n64_gdb_write_word(bp->address, bp->instruction);
        bp->inserted = 0;
    }
}

static int
n64_gdb_remove_breakpoint(unsigned address)
{
    struct n64_gdb_breakpoint *bp;

    bp = n64_gdb_find_breakpoint(address);
    if (bp == 0)
        return 0;
    n64_gdb_restore_breakpoint(bp);
    if (n64_gdb_hit_breakpoint == bp)
        n64_gdb_hit_breakpoint = 0;
    if (n64_gdb_reinsert_breakpoint == bp)
        n64_gdb_reinsert_breakpoint = 0;
    bp->used = 0;
    return 0;
}

static void
n64_gdb_remove_temporary_breakpoints(void)
{
    unsigned i;

    for (i = 0; i < N64_GDB_BREAKPOINTS; i++) {
        if (!n64_gdb_breakpoints[i].used ||
            !n64_gdb_breakpoints[i].temporary)
            continue;
        n64_gdb_restore_breakpoint(&n64_gdb_breakpoints[i]);
        n64_gdb_breakpoints[i].used = 0;
    }
}

static void
n64_gdb_remove_all_breakpoints(void)
{
    unsigned i;

    for (i = 0; i < N64_GDB_BREAKPOINTS; i++) {
        if (!n64_gdb_breakpoints[i].used)
            continue;
        n64_gdb_restore_breakpoint(&n64_gdb_breakpoints[i]);
        n64_gdb_breakpoints[i].used = 0;
    }
    n64_gdb_hit_breakpoint = 0;
    n64_gdb_reinsert_breakpoint = 0;
}

static void
n64_gdb_get_register(int *frame, unsigned reg, unsigned *high,
    unsigned *low)
{
    int word;

    *high = 0;
    *low = 0;
    if (reg < 32) {
        word = n64_gdb_frame_word[reg];
        if (word >= 0) {
#if MIPS_FRAME_GPR64
            *high = frame[word - 1];
#else
            *high = frame[word] < 0 ? ~0u : 0;
#endif
            *low = frame[word];
        }
        return;
    }
    switch (reg) {
    case 32:
        *low = frame[FRAME_STATUS];
        break;
    case 33:
#if MIPS_FRAME_GPR64
        *high = frame[FRAME_LO - 1];
#endif
        *low = frame[FRAME_LO];
        break;
    case 34:
#if MIPS_FRAME_GPR64
        *high = frame[FRAME_HI - 1];
#endif
        *low = frame[FRAME_HI];
        break;
    case 35:
        *low = n64_gdb_badvaddr;
        break;
    case 36:
        *low = n64_gdb_rawcause;
        break;
    case 37:
        *low = frame[FRAME_PC];
        *high = (int)*low < 0 ? ~0u : 0;
        break;
    default:
        break;
    }
}

static void
n64_gdb_set_register(int *frame, unsigned reg, unsigned high, unsigned low)
{
    int word;

    if (reg == 0)
        return;
    if (reg < 32) {
        word = n64_gdb_frame_word[reg];
        if (word < 0)
            return;
#if MIPS_FRAME_GPR64
        frame[word - 1] = high;
#else
        (void)high;
#endif
        frame[word] = low;
        return;
    }
    switch (reg) {
    case 32:
        frame[FRAME_STATUS] = low;
        break;
    case 33:
#if MIPS_FRAME_GPR64
        frame[FRAME_LO - 1] = high;
#endif
        frame[FRAME_LO] = low;
        break;
    case 34:
#if MIPS_FRAME_GPR64
        frame[FRAME_HI - 1] = high;
#endif
        frame[FRAME_HI] = low;
        break;
    case 37:
        frame[FRAME_PC] = low;
        break;
    default:
        break;
    }
}

static void
n64_gdb_read_registers(int *frame)
{
    unsigned char *p;
    unsigned high;
    unsigned low;
    unsigned reg;

    p = n64_gdb_out;
    for (reg = 0; reg < N64_GDB_REGISTER_COUNT; reg++) {
        n64_gdb_get_register(frame, reg, &high, &low);
        p = n64_gdb_put_hex32(p, high);
        p = n64_gdb_put_hex32(p, low);
    }
    *p = 0;
}

static int
n64_gdb_write_registers(int *frame, const unsigned char *p)
{
    unsigned high;
    unsigned low;
    unsigned reg;

    for (reg = 0; reg < N64_GDB_REGISTER_COUNT; reg++) {
        if (n64_gdb_get_hex32(&p, &high) != 0 ||
            n64_gdb_get_hex32(&p, &low) != 0)
            return -1;
        n64_gdb_set_register(frame, reg, high, low);
    }
    return *p == 0 ? 0 : -1;
}

static unsigned
n64_gdb_gpr_low(int *frame, unsigned reg)
{
    int word;

    if (reg == 0 || reg >= 32)
        return 0;
    word = n64_gdb_frame_word[reg];
    return word < 0 ? 0 : (unsigned)frame[word];
}

static unsigned
n64_gdb_gpr_high(int *frame, unsigned reg)
{
    int word;

    if (reg == 0 || reg >= 32)
        return 0;
    word = n64_gdb_frame_word[reg];
    if (word < 0)
        return 0;
#if MIPS_FRAME_GPR64
    return (unsigned)frame[word - 1];
#else
    return frame[word] < 0 ? ~0u : 0;
#endif
}

static int
n64_gdb_gpr_equal(int *frame, unsigned left, unsigned right)
{
    return n64_gdb_gpr_high(frame, left) ==
        n64_gdb_gpr_high(frame, right) &&
        n64_gdb_gpr_low(frame, left) == n64_gdb_gpr_low(frame, right);
}

static int
n64_gdb_gpr_negative(int *frame, unsigned reg)
{
    return (n64_gdb_gpr_high(frame, reg) & 0x80000000u) != 0;
}

static int
n64_gdb_gpr_zero(int *frame, unsigned reg)
{
    return n64_gdb_gpr_high(frame, reg) == 0 &&
        n64_gdb_gpr_low(frame, reg) == 0;
}

static int
n64_gdb_step_addresses(int *frame, unsigned *next1, unsigned *next2)
{
    unsigned pc;
    unsigned instruction;
    unsigned opcode;
    unsigned rs;
    unsigned rt;
    unsigned funct;
    unsigned target;
    int taken;
    int immediate;

    pc = frame[FRAME_PC];
    *next1 = pc + 4;
    *next2 = 0;
    if (n64_gdb_read_word(pc, &instruction) != 0)
        return -1;
    opcode = instruction >> 26;
    rs = instruction >> 21 & 31u;
    rt = instruction >> 16 & 31u;
    immediate = (short)(instruction & 0xffffu);
    target = pc + 4 + ((unsigned)immediate << 2);
    switch (opcode) {
    case 0:
        funct = instruction & 63u;
        if (funct == 8 || funct == 9)
            *next1 = n64_gdb_gpr_low(frame, rs);
        break;
    case 1:
        taken = 0;
        switch (rt) {
        case 0:
        case 2:
        case 16:
        case 18:
            taken = n64_gdb_gpr_negative(frame, rs);
            break;
        case 1:
        case 3:
        case 17:
        case 19:
            taken = !n64_gdb_gpr_negative(frame, rs);
            break;
        default:
            return 0;
        }
        *next1 = taken ? target : pc + 8;
        break;
    case 2:
    case 3:
        *next1 = (pc + 4 & 0xf0000000u) |
            ((instruction & 0x03ffffffu) << 2);
        break;
    case 4:
    case 20:
        *next1 = n64_gdb_gpr_equal(frame, rs, rt) ? target : pc + 8;
        break;
    case 5:
    case 21:
        *next1 = !n64_gdb_gpr_equal(frame, rs, rt) ? target : pc + 8;
        break;
    case 6:
    case 22:
        *next1 = (n64_gdb_gpr_negative(frame, rs) ||
            n64_gdb_gpr_zero(frame, rs)) ?
            target : pc + 8;
        break;
    case 7:
    case 23:
        *next1 = (!n64_gdb_gpr_negative(frame, rs) &&
            !n64_gdb_gpr_zero(frame, rs)) ?
            target : pc + 8;
        break;
    case 17:
        if (rs == 8) {
            /* Avoid touching lazy FPU state: stop on either BC1 outcome. */
            *next1 = target;
            *next2 = pc + 8;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int
n64_gdb_prepare_resume(int *frame, int step)
{
    unsigned next1;
    unsigned next2;

    n64_gdb_remove_temporary_breakpoints();
    if (n64_gdb_native_break) {
        frame[FRAME_PC] += 4;
        n64_gdb_native_break = 0;
    }
    if (n64_gdb_hit_breakpoint != 0) {
        if (frame[FRAME_PC] != n64_gdb_hit_breakpoint->address) {
            if (n64_gdb_reinsert(n64_gdb_hit_breakpoint) != 0)
                return -1;
            n64_gdb_hit_breakpoint = 0;
        } else {
            n64_gdb_reinsert_breakpoint = n64_gdb_hit_breakpoint;
            n64_gdb_reinsert_continue = !step;
            n64_gdb_hit_breakpoint = 0;
            step = 1;
        }
    }
    if (!step)
        return 0;
    if (n64_gdb_step_addresses(frame, &next1, &next2) != 0 ||
        n64_gdb_insert_breakpoint(next1, 1) != 0)
        return -1;
    if (next2 != 0 && next2 != next1 &&
        n64_gdb_insert_breakpoint(next2, 1) != 0) {
        n64_gdb_remove_temporary_breakpoints();
        return -1;
    }
    return 0;
}

static int
n64_gdb_handle_memory_read(const unsigned char *p)
{
    unsigned char *out;
    unsigned address;
    unsigned len;
    unsigned i;

    if (n64_gdb_parse_uint(&p, &address) != 0 || *p++ != ',' ||
        n64_gdb_parse_uint(&p, &len) != 0 || *p != 0 ||
        len > sizeof(n64_gdb_bytes) ||
        n64_gdb_memory_read(address, n64_gdb_bytes, len) != 0)
        return -1;
    out = n64_gdb_out;
    for (i = 0; i < len; i++) {
        *out++ = n64_gdb_hex_digit(n64_gdb_bytes[i] >> 4);
        *out++ = n64_gdb_hex_digit(n64_gdb_bytes[i]);
    }
    *out = 0;
    return 0;
}

static int
n64_gdb_handle_memory_write(const unsigned char *p)
{
    unsigned address;
    unsigned len;
    unsigned i;
    int high;
    int low;

    if (n64_gdb_parse_uint(&p, &address) != 0 || *p++ != ',' ||
        n64_gdb_parse_uint(&p, &len) != 0 || *p++ != ':' ||
        len > sizeof(n64_gdb_bytes))
        return -1;
    for (i = 0; i < len; i++) {
        high = n64_gdb_hex(*p++);
        low = n64_gdb_hex(*p++);
        if (high < 0 || low < 0)
            return -1;
        n64_gdb_bytes[i] = (unsigned char)((high << 4) | low);
    }
    if (*p != 0 ||
        n64_gdb_memory_write(address, n64_gdb_bytes, len) != 0)
        return -1;
    return 0;
}

static int
n64_gdb_handle_breakpoint(const unsigned char *p, int insert)
{
    unsigned type;
    unsigned address;
    unsigned kind;

    if (n64_gdb_parse_uint(&p, &type) != 0 || *p++ != ',' ||
        n64_gdb_parse_uint(&p, &address) != 0 || *p++ != ',' ||
        n64_gdb_parse_uint(&p, &kind) != 0 || *p != 0 ||
        type != 0 || kind != 4)
        return -1;
    return insert ? n64_gdb_insert_breakpoint(address, 0) :
        n64_gdb_remove_breakpoint(address);
}

static int
n64_gdb_command(int *frame, int *resume_after_reply)
{
    const unsigned char *p;
    unsigned char *out;
    unsigned address;
    unsigned reg;
    unsigned high;
    unsigned low;

    p = n64_gdb_in;
    *resume_after_reply = 0;
    n64_gdb_out[0] = 0;
    switch (*p++) {
    case '?':
        n64_gdb_out[0] = 'S';
        n64_gdb_out[1] = n64_gdb_hex_digit((unsigned)n64_gdb_signal >> 4);
        n64_gdb_out[2] = n64_gdb_hex_digit((unsigned)n64_gdb_signal);
        n64_gdb_out[3] = 0;
        break;
    case 'g':
        n64_gdb_read_registers(frame);
        break;
    case 'G':
        n64_gdb_reply(n64_gdb_write_registers(frame, p) == 0 ?
            "OK" : "E01");
        break;
    case 'p':
        if (n64_gdb_parse_uint(&p, &reg) != 0 || *p != 0 ||
            reg >= N64_GDB_REGISTER_COUNT) {
            n64_gdb_reply("E01");
            break;
        }
        n64_gdb_get_register(frame, reg, &high, &low);
        out = n64_gdb_put_hex32(n64_gdb_out, high);
        out = n64_gdb_put_hex32(out, low);
        *out = 0;
        break;
    case 'P':
        if (n64_gdb_parse_uint(&p, &reg) != 0 || *p++ != '=' ||
            reg >= N64_GDB_REGISTER_COUNT ||
            n64_gdb_get_hex32(&p, &high) != 0 ||
            n64_gdb_get_hex32(&p, &low) != 0 || *p != 0) {
            n64_gdb_reply("E01");
            break;
        }
        n64_gdb_set_register(frame, reg, high, low);
        n64_gdb_reply("OK");
        break;
    case 'm':
        if (n64_gdb_handle_memory_read(p) != 0)
            n64_gdb_reply("E14");
        break;
    case 'M':
        n64_gdb_reply(n64_gdb_handle_memory_write(p) == 0 ?
            "OK" : "E14");
        break;
    case 'Z':
        n64_gdb_reply(n64_gdb_handle_breakpoint(p, 1) == 0 ?
            "OK" : "E16");
        break;
    case 'z':
        n64_gdb_reply(n64_gdb_handle_breakpoint(p, 0) == 0 ?
            "OK" : "E16");
        break;
    case 'c':
    case 's':
        if (*p != 0) {
            if (n64_gdb_parse_uint(&p, &address) != 0 || *p != 0) {
                n64_gdb_reply("E01");
                break;
            }
            frame[FRAME_PC] = address;
        }
        if (n64_gdb_prepare_resume(frame, n64_gdb_in[0] == 's') != 0) {
            n64_gdb_reply("E16");
            break;
        }
        return 1;
    case 'D':
        n64_gdb_remove_all_breakpoints();
        n64_gdb_usb_detach();
        n64_gdb_reply("OK");
        *resume_after_reply = 1;
        break;
    case 'H':
    case 'T':
    case '!':
        n64_gdb_reply("OK");
        break;
    case 'q':
        if (n64_gdb_starts_with(n64_gdb_in, "qSupported"))
            n64_gdb_reply("PacketSize=800;swbreak+");
        else if (n64_gdb_starts_with(n64_gdb_in, "qAttached"))
            n64_gdb_reply("1");
        else if (n64_gdb_starts_with(n64_gdb_in, "qC"))
            n64_gdb_reply("QC1");
        else if (n64_gdb_starts_with(n64_gdb_in, "qfThreadInfo"))
            n64_gdb_reply("m1");
        else if (n64_gdb_starts_with(n64_gdb_in, "qsThreadInfo"))
            n64_gdb_reply("l");
        else if (n64_gdb_starts_with(n64_gdb_in, "qOffsets"))
            n64_gdb_reply("Text=0;Data=0;Bss=0");
        else if (n64_gdb_starts_with(n64_gdb_in, "qSymbol::"))
            n64_gdb_reply("OK");
        break;
    case 'v':
        if (n64_gdb_starts_with(n64_gdb_in, "vCont?")) {
            n64_gdb_reply("vCont;c;s");
        } else if (n64_gdb_starts_with(n64_gdb_in, "vCont;c")) {
            if (n64_gdb_prepare_resume(frame, 0) != 0)
                n64_gdb_reply("E16");
            else
                return 1;
        } else if (n64_gdb_starts_with(n64_gdb_in, "vCont;s")) {
            if (n64_gdb_prepare_resume(frame, 1) != 0)
                n64_gdb_reply("E16");
            else
                return 1;
        } else if (n64_gdb_starts_with(n64_gdb_in, "vMustReplyEmpty")) {
            n64_gdb_out[0] = 0;
        }
        break;
    case 'k':
        n64_gdb_remove_all_breakpoints();
        n64_gdb_usb_detach();
        return 1;
    default:
        break;
    }
    return 0;
}

static void
n64_gdb_stop(int *frame, int signal, int announce)
{
    int resume_after_reply;

    n64_gdb_signal = signal;
    if (announce) {
        n64_gdb_out[0] = 'S';
        n64_gdb_out[1] = n64_gdb_hex_digit((unsigned)signal >> 4);
        n64_gdb_out[2] = n64_gdb_hex_digit((unsigned)signal);
        n64_gdb_out[3] = 0;
        if (n64_gdb_put_packet(n64_gdb_out) != 0)
            goto out;
    }
    for (;;) {
        (void)n64_gdb_get_packet();
        if (n64_gdb_command(frame, &resume_after_reply))
            break;
        if (n64_gdb_put_packet(n64_gdb_out) != 0)
            break;
        if (resume_after_reply)
            break;
    }
out:
    n64_gdb_usb_clear_interrupt();
}

void
n64_gdb_init(void)
{
    unsigned start;

    n64_gdb_usb_init();

    /*
     * startup() deliberately leaves ST_IE clear.  Give an already connected
     * host a polling window to complete enumeration before VM bootstrap;
     * after interrupts are enabled the ordinary cartridge IRQ path takes
     * over.  This is bounded so a debug ROM still boots without a USB host.
     */
    start = mips_read_c0_register(C0_COUNT, 0);
    while (!n64_gdb_usb_ready() &&
        (unsigned)(mips_read_c0_register(C0_COUNT, 0) - start) <
        N64_COUNT_KHZ * N64_GDB_ENUM_WAIT_MSEC)
        n64_gdb_usb_poll();
}

int
n64_gdb_exception(int *frame, unsigned rawcause, unsigned badvaddr)
{
    struct n64_gdb_breakpoint *bp;
    unsigned cause;
    int interrupt_reason;
    int was_temporary;

    cause = rawcause & CA_EXC_CODE;
    if (cause == CA_Int && (rawcause & N64_GDB_CAUSE_IP3) != 0) {
        interrupt_reason = n64_gdb_usb_interrupt();
        if (interrupt_reason == 0)
            return 1;
        if (n64_gdb_usb_ready()) {
            n64_gdb_rawcause = rawcause;
            n64_gdb_badvaddr = badvaddr;
            n64_gdb_stop(frame, N64_GDB_SIGINT,
                interrupt_reason == 1);
        }
        return 1;
    }
    if (cause != CA_Bp || !n64_gdb_usb_ready() ||
        !n64_gdb_usb_attached())
        return 0;

    n64_gdb_rawcause = rawcause;
    n64_gdb_badvaddr = badvaddr;
    bp = n64_gdb_find_breakpoint(frame[FRAME_PC]);
    was_temporary = bp != 0 && bp->temporary;
    if (bp != 0) {
        n64_gdb_restore_breakpoint(bp);
        if (was_temporary)
            bp->used = 0;
        else {
            if (n64_gdb_reinsert_breakpoint != 0 &&
                n64_gdb_reinsert_breakpoint != bp) {
                (void)n64_gdb_reinsert(n64_gdb_reinsert_breakpoint);
                n64_gdb_reinsert_breakpoint = 0;
                n64_gdb_reinsert_continue = 0;
            }
            n64_gdb_hit_breakpoint = bp;
        }
    } else {
        n64_gdb_native_break = 1;
    }

    if (was_temporary) {
        n64_gdb_remove_temporary_breakpoints();
        if (n64_gdb_reinsert_breakpoint != 0) {
            (void)n64_gdb_reinsert(n64_gdb_reinsert_breakpoint);
            n64_gdb_reinsert_breakpoint = 0;
        }
        if (n64_gdb_reinsert_continue) {
            n64_gdb_reinsert_continue = 0;
            return 1;
        }
        n64_gdb_reinsert_continue = 0;
    }
    n64_gdb_stop(frame, N64_GDB_SIGTRAP, 1);
    return 1;
}

void
n64_gdb_panic(int *frame, unsigned rawcause, unsigned badvaddr)
{
    if (!n64_gdb_usb_ready() || !n64_gdb_usb_attached())
        return;
    n64_gdb_rawcause = rawcause;
    n64_gdb_badvaddr = badvaddr;
    n64_gdb_stop(frame, N64_GDB_SIGSEGV, 1);
}
