/*
 * CTL_MACHDEP definitions for N64.
 */
#define CPU_CONSDEV             1
#define CPU_ERRMSG              2
#define CPU_NLIST               3
#define CPU_FREQ_KHZ            4
#define CPU_COUNT_KHZ           5
#define CPU_RDRAM_BYTES         6
#define CPU_MAXID               7

#ifndef KERNEL
#define CTL_MACHDEP_NAMES { \
    { 0, 0 }, \
    { "console_device", CTLTYPE_STRUCT }, \
    { 0, 0 }, \
    { 0, 0 }, \
    { "cpu_khz", CTLTYPE_INT }, \
    { "count_khz", CTLTYPE_INT }, \
    { "rdram_bytes", CTLTYPE_INT }, \
}
#endif
