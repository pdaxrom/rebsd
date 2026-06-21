#ifndef _MIPS_FPU_H_
#define _MIPS_FPU_H_

#ifndef __ASSEMBLER__
struct mips_fpu_state {
    unsigned fpr[32];
    unsigned fcsr;
};

void mips_fpu_save(struct mips_fpu_state *state);
void mips_fpu_restore(const struct mips_fpu_state *state);
void mips_fpu_clear(void);
#endif

#endif
