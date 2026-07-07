/*
 * CTL_MACHDEP definitions for generic MIPS.
 */
#define CPU_CONSDEV             1
#define CPU_ERRMSG              2
#define CPU_NLIST               3
#define CPU_FREQ_KHZ            4
#define CPU_COUNT_KHZ           5
#define CPU_RAM_BYTES           6
#define CPU_DM9000_STATS        7
#define CPU_TIMER_STATS         8
#define CPU_MAXID               9

#ifndef KERNEL
#define CTL_MACHDEP_NAMES { \
    { 0, 0 }, \
    { "console_device", CTLTYPE_STRUCT }, \
    { 0, 0 }, \
    { 0, 0 }, \
    { "cpu_khz", CTLTYPE_INT }, \
    { "count_khz", CTLTYPE_INT }, \
    { "ram_bytes", CTLTYPE_INT }, \
    { "dm9000_stats", CTLTYPE_STRING }, \
    { "timer_stats", CTLTYPE_STRING }, \
}
#endif
