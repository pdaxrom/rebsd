/*
 * CTL_MACHDEP definitions for i386 PC systems.
 */
#ifndef _I386_CPU_H_
#define _I386_CPU_H_

#define CPU_CONSDEV             1
#define CPU_ERRMSG              2
#define CPU_NLIST               3
#define CPU_FREQ_KHZ            4
#define CPU_RAM_BYTES           5
#define CPU_MAXID               6

#define HW_MACHINE_NAME         "i386"
#define HW_MODEL_NAME           "i686-pc"

#ifndef KERNEL
#define CTL_MACHDEP_NAMES { \
    { 0, 0 }, \
    { "console_device", CTLTYPE_STRUCT }, \
    { 0, 0 }, \
    { 0, 0 }, \
    { "cpu_khz", CTLTYPE_INT }, \
    { "ram_bytes", CTLTYPE_INT }, \
}
#endif

#endif
