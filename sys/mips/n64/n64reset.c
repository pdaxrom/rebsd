/*
 * Reset-button crash capture for N64 diagnostic kernels.
 *
 * The PIF raises CP0 IP4 roughly half a second before the unavoidable warm
 * NMI.  Save the interrupted context immediately.  A compact 64-byte record
 * below physical 0x400 survives IPL3's warm-boot clearing and is displayed
 * again after restart; a larger record supplies details while RESET is held.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <machine/console.h>
#include <machine/io.h>
#include <machine/n64.h>
#include <machine/n64reset.h>

#define N64_RESET_DUMP_MAGIC          0x52445354u /* RDST */
#define N64_RESET_DUMP_VERSION        2u
#define N64_RESET_DUMP_VERSION_MASK   0x0000ffffu
#define N64_RESET_DUMP_STATE_MASK     0xffff0000u
#define N64_RESET_DUMP_STATE_LIVE     0x00010000u
#define N64_RESET_DUMP_STATE_CAPTURED 0x00020000u
#define N64_RESET_DUMP_STATE_DISPLAYED 0x00030000u
#define N64_RESET_DUMP_STATE_MISSING  0x00040000u
#define N64_RESET_DUMP_STATE_REBOOT   0x00050000u
#define N64_RESET_DUMP_XOR            0x4e363452u /* N64R */
#define N64_RESET_TYPE_PHYS           0x0000030cu
#define N64_RESET_TYPE_NMI            1u
#define N64_RESET_DUMP_CRITICAL_PHYS  0x0000031cu
#define N64_RESET_DUMP_FULL_PHYS      0x00000800u
#define N64_RESET_DUMP_STACK_WORDS    64u
#define N64_RESET_DUMP_BT_WORDS       5u

struct n64_reset_dump_critical {
    unsigned magic;
    unsigned version;
    unsigned checksum;
    unsigned pc;
    unsigned cause;
    unsigned status;
    unsigned badvaddr;
    unsigned sp;
    unsigned ra;
    unsigned pid;
    unsigned bt_count;
    unsigned bt[N64_RESET_DUMP_BT_WORDS];
};

struct n64_reset_dump_full {
    unsigned magic;
    unsigned version;
    unsigned checksum;
    unsigned frame_words;
    unsigned cause;
    unsigned badvaddr;
    unsigned count;
    unsigned pid;
    unsigned proc;
    unsigned vmspace;
    unsigned wchan;
    unsigned stack_words;
    char comm[16];
    unsigned frame[FRAME_WORDS];
    unsigned stack[N64_RESET_DUMP_STACK_WORDS];
};

typedef char n64_reset_dump_critical_size[
    sizeof(struct n64_reset_dump_critical) == 64 ? 1 : -1];

static volatile struct n64_reset_dump_critical *const n64_reset_critical =
    (volatile struct n64_reset_dump_critical *)(N64_KSEG1_BASE |
        N64_RESET_DUMP_CRITICAL_PHYS);
static volatile struct n64_reset_dump_full *const n64_reset_full =
    (volatile struct n64_reset_dump_full *)(N64_KSEG1_BASE |
        N64_RESET_DUMP_FULL_PHYS);

extern char _etext[];

static void
n64_reset_sync(void)
{
    asm volatile ("sync" ::: "memory");
}

static unsigned
n64_reset_checksum(const volatile unsigned *words, unsigned count,
    unsigned checksum_word)
{
    unsigned value;
    unsigned i;

    value = N64_RESET_DUMP_XOR;
    for (i = 0; i < count; ++i)
        if (i != checksum_word)
            value ^= words[i];
    return value;
}

static int
n64_reset_critical_valid(void)
{
    unsigned version;
    unsigned state;

    version = n64_reset_critical->version;
    state = version & N64_RESET_DUMP_STATE_MASK;
    return n64_reset_critical->magic == N64_RESET_DUMP_MAGIC &&
        (version & N64_RESET_DUMP_VERSION_MASK) ==
            N64_RESET_DUMP_VERSION &&
        (state == 0 || state == N64_RESET_DUMP_STATE_LIVE ||
            state == N64_RESET_DUMP_STATE_CAPTURED ||
            state == N64_RESET_DUMP_STATE_DISPLAYED ||
            state == N64_RESET_DUMP_STATE_MISSING ||
            state == N64_RESET_DUMP_STATE_REBOOT) &&
        n64_reset_critical->checksum == n64_reset_checksum(
            (const volatile unsigned *)n64_reset_critical,
            sizeof(*n64_reset_critical) / sizeof(unsigned), 2);
}

static unsigned
n64_reset_critical_state(void)
{
    return n64_reset_critical->version & N64_RESET_DUMP_STATE_MASK;
}

static int
n64_reset_full_valid(void)
{
    return n64_reset_full->magic == N64_RESET_DUMP_MAGIC &&
        n64_reset_full->version == N64_RESET_DUMP_VERSION &&
        n64_reset_full->frame_words == FRAME_WORDS &&
        n64_reset_full->checksum == n64_reset_checksum(
            (const volatile unsigned *)n64_reset_full,
            sizeof(*n64_reset_full) / sizeof(unsigned), 2);
}

static int
n64_reset_direct_bytes(unsigned address, unsigned bytes)
{
    unsigned segment;
    unsigned physical;
    unsigned rdram;

    segment = address & N64_KSEG_ADDR_MASK;
    if (segment != N64_KSEG0_BASE && segment != N64_KSEG1_BASE)
        return 0;
    physical = address & N64_KSEG_PHYS_MASK;
    rdram = n64_rdram_size();
    return physical <= rdram && bytes <= rdram - physical;
}

static int
n64_reset_stack_physical(unsigned address, unsigned bytes,
    unsigned *physicalp)
{
    unsigned physical;
    unsigned rdram;
    unsigned segment;

    segment = address & N64_KSEG_ADDR_MASK;
    if (segment == N64_KSEG0_BASE || segment == N64_KSEG1_BASE)
        physical = address & N64_KSEG_PHYS_MASK;
    else if (address >= N64_USER_VADDR_START &&
        address < N64_USER_VADDR_END)
        physical = N64_USER_PHYS_START +
            (address - N64_USER_VADDR_START);
    else
        return 0;

    rdram = n64_rdram_size();
    if (physical > rdram || bytes > rdram - physical)
        return 0;
    *physicalp = physical;
    return 1;
}

static int
n64_reset_trace_address(unsigned address)
{
    unsigned physical;

    if ((address & 3u) != 0)
        return 0;
    if (address >= N64_KERNEL_LOAD_VADDR && address < (unsigned)_etext)
        return 1;
    return address >= N64_USER_VADDR_START &&
        address < N64_USER_VADDR_END &&
        n64_reset_stack_physical(address, sizeof(unsigned), &physical);
}

static void
n64_reset_collect_backtrace(int *frame)
{
    const volatile unsigned *stack;
    unsigned address;
    unsigned count;
    unsigned i;
    unsigned j;
    unsigned physical;
    unsigned sp;

    n64_reset_critical->bt_count = 0;
    for (i = 0; i < N64_RESET_DUMP_BT_WORDS; ++i)
        n64_reset_critical->bt[i] = 0;

    sp = (unsigned)frame[FRAME_SP];
    if ((sp & 3u) != 0 || !n64_reset_stack_physical(sp,
        N64_RESET_DUMP_STACK_WORDS * sizeof(unsigned), &physical))
        return;

    stack = (const volatile unsigned *)(N64_KSEG1_BASE |
        physical);
    count = 0;
    for (i = 0; i < N64_RESET_DUMP_STACK_WORDS &&
        count < N64_RESET_DUMP_BT_WORDS; ++i) {
        address = stack[i];
        if (!n64_reset_trace_address(address) ||
            address == (unsigned)frame[FRAME_PC] ||
            address == (unsigned)frame[FRAME_RA])
            continue;
        for (j = 0; j < count; ++j)
            if (n64_reset_critical->bt[j] == address)
                break;
        if (j != count)
            continue;
        n64_reset_critical->bt[count++] = address;
    }
    n64_reset_critical->bt_count = count;
}

static void
n64_reset_copy_comm(volatile char *dst, unsigned len, const char *src)
{
    unsigned i;

    for (i = 0; i < len; ++i) {
        dst[i] = src != 0 ? src[i] : 0;
        if (src != 0 && src[i] == 0)
            src = 0;
    }
}

static struct proc *
n64_reset_current_proc(const char **comm)
{
    struct proc *p;

    p = 0;
    *comm = 0;
    if (mips_curuser != 0 &&
        n64_reset_direct_bytes((unsigned)mips_curuser,
            sizeof(*mips_curuser))) {
        p = mips_curuser->u_procp;
        *comm = mips_curuser->u_comm;
    }
    if (p != 0 && !n64_reset_direct_bytes((unsigned)p, sizeof(*p)))
        p = 0;
    return p;
}

static void
n64_reset_commit_critical(void)
{
    /* Publish magic last, so a reset can never accept a half-written record. */
    n64_reset_critical->magic = 0;
    n64_reset_critical->checksum = n64_reset_checksum(
        (const volatile unsigned *)n64_reset_critical,
        sizeof(*n64_reset_critical) / sizeof(unsigned), 2) ^
        N64_RESET_DUMP_MAGIC;
    n64_reset_sync();
    n64_reset_critical->magic = N64_RESET_DUMP_MAGIC;
    n64_reset_sync();
}

static void
n64_reset_write_critical(int *frame, unsigned rawcause,
    unsigned badvaddr, struct proc *p, unsigned state, int collect_bt)
{
    unsigned i;

    n64_reset_critical->magic = 0;
    n64_reset_critical->version = N64_RESET_DUMP_VERSION | state;
    n64_reset_critical->pc = (unsigned)frame[FRAME_PC];
    n64_reset_critical->cause = rawcause;
    n64_reset_critical->status = (unsigned)frame[FRAME_STATUS];
    n64_reset_critical->badvaddr = badvaddr;
    n64_reset_critical->sp = (unsigned)frame[FRAME_SP];
    n64_reset_critical->ra = (unsigned)frame[FRAME_RA];
    n64_reset_critical->pid = p != 0 ? (unsigned)p->p_pid : ~0u;
    if (collect_bt)
        n64_reset_collect_backtrace(frame);
    else {
        n64_reset_critical->bt_count = 0;
        for (i = 0; i < N64_RESET_DUMP_BT_WORDS; ++i)
            n64_reset_critical->bt[i] = 0;
    }
    n64_reset_commit_critical();
}

static void
n64_reset_clear(void)
{
    n64_reset_critical->magic = 0;
    n64_reset_full->magic = 0;
    n64_reset_sync();
}

void
n64_reset_dump_reboot(void)
{
    unsigned i;

    n64_reset_clear();
    n64_reset_critical->version = N64_RESET_DUMP_VERSION |
        N64_RESET_DUMP_STATE_REBOOT;
    n64_reset_critical->pc = 0;
    n64_reset_critical->cause = 0;
    n64_reset_critical->status = 0;
    n64_reset_critical->badvaddr = 0;
    n64_reset_critical->sp = 0;
    n64_reset_critical->ra = 0;
    n64_reset_critical->pid = ~0u;
    n64_reset_critical->bt_count = 0;
    for (i = 0; i < N64_RESET_DUMP_BT_WORDS; ++i)
        n64_reset_critical->bt[i] = 0;
    n64_reset_commit_critical();
}

static void
n64_reset_seed(unsigned state)
{
    unsigned i;

    n64_reset_clear();
    n64_reset_critical->version = N64_RESET_DUMP_VERSION | state;
    n64_reset_critical->pc = 0;
    n64_reset_critical->cause = 0;
    n64_reset_critical->status = 0;
    n64_reset_critical->badvaddr = 0;
    n64_reset_critical->sp = 0;
    n64_reset_critical->ra = 0;
    n64_reset_critical->pid = ~0u;
    n64_reset_critical->bt_count = 0;
    for (i = 0; i < N64_RESET_DUMP_BT_WORDS; ++i)
        n64_reset_critical->bt[i] = 0;
    n64_reset_commit_critical();
}

static void
n64_reset_capture(int *frame, unsigned rawcause, unsigned badvaddr)
{
    struct proc *p;
    const char *comm;
    unsigned physical;
    unsigned sp;
    unsigned i;

    p = n64_reset_current_proc(&comm);

    n64_reset_clear();
    n64_reset_full->version = N64_RESET_DUMP_VERSION;
    n64_reset_full->frame_words = FRAME_WORDS;
    n64_reset_full->cause = rawcause;
    n64_reset_full->badvaddr = badvaddr;
    n64_reset_full->count = mips_read_c0_register(C0_COUNT, 0);
    n64_reset_full->pid = p != 0 ? (unsigned)p->p_pid : ~0u;
    n64_reset_full->proc = (unsigned)p;
    n64_reset_full->vmspace = p != 0 ? (unsigned)p->p_vmspace : 0;
    n64_reset_full->wchan = p != 0 ? (unsigned)p->p_wchan : 0;
    n64_reset_copy_comm(n64_reset_full->comm,
        sizeof(n64_reset_full->comm), comm);
    for (i = 0; i < FRAME_WORDS; ++i)
        n64_reset_full->frame[i] = (unsigned)frame[i];

    sp = (unsigned)frame[FRAME_SP];
    n64_reset_full->stack_words = 0;
    if ((sp & 3u) == 0 && n64_reset_stack_physical(sp,
        N64_RESET_DUMP_STACK_WORDS * sizeof(unsigned), &physical)) {
        const volatile unsigned *stack =
            (const volatile unsigned *)(N64_KSEG1_BASE |
                physical);

        for (i = 0; i < N64_RESET_DUMP_STACK_WORDS; ++i)
            n64_reset_full->stack[i] = stack[i];
        n64_reset_full->stack_words = N64_RESET_DUMP_STACK_WORDS;
    } else {
        for (i = 0; i < N64_RESET_DUMP_STACK_WORDS; ++i)
            n64_reset_full->stack[i] = 0;
    }
    n64_reset_full->magic = N64_RESET_DUMP_MAGIC;
    n64_reset_full->checksum = n64_reset_checksum(
        (const volatile unsigned *)n64_reset_full,
        sizeof(*n64_reset_full) / sizeof(unsigned), 2);

    n64_reset_write_critical(frame, rawcause, badvaddr, p,
        N64_RESET_DUMP_STATE_CAPTURED, 1);
}

/*
 * Keep one inexpensive retained snapshot current during normal exception and
 * timer traffic.  If RESET arrives while EXL/ERL or IE prevents delivery of
 * the maskable pre-NMI, the subsequent warm boot can still show this last
 * known context instead of silently starting over.
 */
void
n64_reset_dump_observe(int *frame, unsigned rawcause, unsigned badvaddr)
{
    struct proc *p;
    const char *comm;
    unsigned state;

    state = n64_reset_critical_valid() ? n64_reset_critical_state() : 0;
    if (state == N64_RESET_DUMP_STATE_CAPTURED ||
        state == N64_RESET_DUMP_STATE_DISPLAYED ||
        state == N64_RESET_DUMP_STATE_MISSING ||
        state == N64_RESET_DUMP_STATE_REBOOT)
        return;

    p = n64_reset_current_proc(&comm);
    n64_reset_full->magic = 0;
    n64_reset_write_critical(frame, rawcause, badvaddr, p,
        N64_RESET_DUMP_STATE_LIVE, 0);
}

static void
n64_reset_mark_state(unsigned state)
{
    n64_reset_critical->magic = 0;
    n64_reset_critical->version = N64_RESET_DUMP_VERSION |
        state;
    n64_reset_commit_critical();
}

static void
n64_reset_puts(const char *text)
{
    while (*text != 0)
        n64_console_putc(*text++);
}

static void
n64_reset_hex(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    n64_reset_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4)
        n64_console_putc(digits[(value >> (unsigned)shift) & 15u]);
}

static void
n64_reset_field(const char *name, unsigned value)
{
    n64_reset_puts(name);
    n64_reset_hex(value);
    n64_console_putc('\n');
}

static void
n64_reset_full_comm(void)
{
    unsigned i;

    n64_reset_puts("comm: ");
    for (i = 0; i < sizeof(n64_reset_full->comm); ++i) {
        int ch = n64_reset_full->comm[i];

        if (ch == 0)
            break;
        n64_console_putc(ch);
    }
    n64_console_putc('\n');
}

static void
n64_reset_backtrace_line(unsigned number, const char *kind,
    unsigned address)
{
    n64_reset_puts(" #");
    n64_console_putc('0' + (int)number);
    n64_console_putc(' ');
    n64_reset_puts(kind);
    n64_reset_puts("  ");
    n64_reset_hex(address);
    n64_console_putc('\n');
}

static void
n64_reset_backtrace(void)
{
    unsigned count;
    unsigned line;
    unsigned i;

    n64_reset_puts("backtrace (stack scan):\n");
    line = 0;
    if (n64_reset_trace_address(n64_reset_critical->pc))
        n64_reset_backtrace_line(line++, "pc ",
            n64_reset_critical->pc);
    if (n64_reset_trace_address(n64_reset_critical->ra) &&
        n64_reset_critical->ra != n64_reset_critical->pc)
        n64_reset_backtrace_line(line++, "ra ",
            n64_reset_critical->ra);
    count = n64_reset_critical->bt_count;
    if (count > N64_RESET_DUMP_BT_WORDS)
        count = N64_RESET_DUMP_BT_WORDS;
    for (i = 0; i < count; ++i)
        if (n64_reset_trace_address(n64_reset_critical->bt[i]))
            n64_reset_backtrace_line(line++, "stk",
                n64_reset_critical->bt[i]);
    if (line == 0)
        n64_reset_puts(" (unavailable)\n");
}

void
n64_reset_dump_backtrace(int *frame)
{
    struct proc *p;
    const char *comm;

    p = n64_reset_current_proc(&comm);
    n64_reset_write_critical(frame,
        mips_read_c0_register(C0_CAUSE, 0),
        mips_read_c0_register(C0_BADVADDR, 0), p,
        N64_RESET_DUMP_STATE_LIVE, 1);
    n64_reset_backtrace();
}

static void
n64_reset_draw(unsigned state)
{
    int full;

    full = n64_reset_full_valid();
    n64_console_panic_mode();
    n64_reset_puts("*** RESET CRASH DUMP ***\n");
    if (state == N64_RESET_DUMP_STATE_LIVE)
        n64_reset_puts("capture: last exception (pre-NMI missed)\n");
    else if (state == N64_RESET_DUMP_STATE_MISSING)
        n64_reset_puts("capture: unavailable (retained record invalid)\n");
    else
        n64_reset_puts("capture: exact pre-NMI\n");
    n64_reset_field("pc:      ", n64_reset_critical->pc);
    n64_reset_field("ra:      ", n64_reset_critical->ra);
    n64_reset_field("sp:      ", n64_reset_critical->sp);
    n64_reset_field("cause:   ", n64_reset_critical->cause);
    n64_reset_field("status:  ", n64_reset_critical->status);
    n64_reset_field("badva:   ", n64_reset_critical->badvaddr);
    n64_reset_field("pid:     ", n64_reset_critical->pid);
    if (full) {
        n64_reset_full_comm();
        n64_reset_field("proc:    ", n64_reset_full->proc);
        n64_reset_field("vmspace: ", n64_reset_full->vmspace);
        n64_reset_field("wchan:   ", n64_reset_full->wchan);
    }
    n64_reset_backtrace();
    n64_reset_puts("\nRelease RESET; press again to reboot\n");
}

static void
n64_reset_wait_for_nmi(void)
{
    unsigned status;

    status = mips_read_c0_register(C0_STATUS, 0);
    status &= ~(ST_IE | ST_IM);
    mips_write_c0_register(C0_STATUS, 0, status);
    for (;;)
        asm volatile ("wait");
}

int
n64_reset_dump_interrupt(int *frame, unsigned rawcause, unsigned badvaddr)
{
    unsigned state;

    state = n64_reset_critical_valid() ? n64_reset_critical_state() : 0;
    if (state == N64_RESET_DUMP_STATE_DISPLAYED ||
        state == N64_RESET_DUMP_STATE_REBOOT) {
        n64_reset_seed(N64_RESET_DUMP_STATE_REBOOT);
        n64_reset_wait_for_nmi();
    }

    if (state != N64_RESET_DUMP_STATE_CAPTURED)
        n64_reset_capture(frame, rawcause, badvaddr);
    n64_reset_draw(N64_RESET_DUMP_STATE_CAPTURED);
    n64_reset_wait_for_nmi();
    return 1;
}

void
n64_reset_dump_show(void)
{
    int valid;
    unsigned status;
    unsigned state;
    unsigned reset_type;

    reset_type = *(volatile unsigned *)(N64_KSEG1_BASE |
        N64_RESET_TYPE_PHYS);
    valid = n64_reset_critical_valid();
    if (!valid) {
        if (reset_type != N64_RESET_TYPE_NMI) {
            n64_reset_clear();
            return;
        }
        n64_reset_seed(N64_RESET_DUMP_STATE_MISSING);
    }
    state = n64_reset_critical_state();
    if (state == N64_RESET_DUMP_STATE_DISPLAYED ||
        state == N64_RESET_DUMP_STATE_REBOOT) {
        n64_reset_clear();
        return;
    }

    /* From this point any next RESET is the reboot acknowledgement. */
    n64_reset_mark_state(N64_RESET_DUMP_STATE_DISPLAYED);
    n64_reset_draw(state);

    status = mips_read_c0_register(C0_STATUS, 0);
    status &= ~(ST_IM | ST_EXL | ST_ERL | ST_KSU | ST_BEV);
    status |= ST_IE | ST_IM4;
    mips_write_c0_register(C0_STATUS, 0, status);
    for (;;)
        asm volatile ("wait");
}
