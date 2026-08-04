#ifndef _I386_FPU_H_
#define _I386_FPU_H_

#define I386_FPU_STATE_BYTES 512

#ifndef __ASSEMBLER__
struct i386_fpu_state {
    unsigned char ifs_bytes[I386_FPU_STATE_BYTES];
} __attribute__((aligned(16)));

struct i386_trapframe;

int i386_fpu_init(void);
void i386_fpu_save_user(const struct i386_trapframe *);
void i386_fpu_restore_user(const struct i386_trapframe *);
void i386_fpu_restore_current(void);
void i386_fpu_exec_reset(void);
#endif

#endif /* _I386_FPU_H_ */
