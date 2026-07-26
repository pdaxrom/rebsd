#ifndef _MIPS_MACHINE_JMPBUF_H_
#define _MIPS_MACHINE_JMPBUF_H_

/*
 * Twelve saved integer registers, a signal-mask flag and a signal mask.
 * Hard-float builds additionally preserve the floating-point state.
 */
#if defined(__mips_hard_float)
typedef int jmp_buf[47];
#else
typedef int jmp_buf[14];
#endif

#endif
