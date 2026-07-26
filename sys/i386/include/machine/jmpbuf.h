#ifndef _I386_MACHINE_JMPBUF_H_
#define _I386_MACHINE_JMPBUF_H_

/*
 * ebx, esi, edi, ebp, return address, stack pointer, signal-mask flag
 * and signal mask.
 */
typedef int jmp_buf[8];

#endif
