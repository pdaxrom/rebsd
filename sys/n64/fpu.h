#ifndef _N64_FPU_H_
#define _N64_FPU_H_

#ifndef __ASSEMBLER__
struct n64_fpu_state {
    unsigned fpr[32];
    unsigned fcsr;
};

void n64_fpu_save(struct n64_fpu_state *state);
void n64_fpu_restore(const struct n64_fpu_state *state);
void n64_fpu_clear(void);
#endif

#endif
