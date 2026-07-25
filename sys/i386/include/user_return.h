#ifndef _I386_USER_RETURN_H_
#define _I386_USER_RETURN_H_

struct i386_trapframe;
struct proc;

/*
 * The early image supplies test operations before the full generic signal
 * and scheduler objects are linked.  Passing NULL restores the generic
 * CURSIG/postsig/setpri/setrq/swtch path.
 */
struct i386_user_return_ops {
    int (*uro_next_signal)(struct proc *);
    void (*uro_deliver_signal)(int);
    int (*uro_set_priority)(struct proc *);
    void (*uro_enqueue)(struct proc *);
    void (*uro_switch)(void);
    volatile int *uro_reschedule;
    int *uro_current_priority;
};

void i386_user_return_set_ops(const struct i386_user_return_ops *);
void i386_user_return(struct i386_trapframe *);
int i386_user_return_handle_test(struct i386_trapframe *);
int i386_user_return_selftest(void);

#endif
