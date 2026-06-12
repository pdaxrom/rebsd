#ifndef _N64_SI_H_
#define _N64_SI_H_

#define N64_SI_BLOCK_SIZE       64

#ifdef KERNEL
int n64_si_exec(const void *input, void *output);
#endif

#endif
