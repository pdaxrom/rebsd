#ifndef _I386_USER_STACK_H_
#define _I386_USER_STACK_H_

struct vmspace;

struct i386_user_stack {
    unsigned ius_stack_pointer;
    unsigned ius_argc;
    unsigned ius_argv;
    unsigned ius_envp;
};

int i386_user_stack_build(struct vmspace *, unsigned, unsigned,
    const char *const *, unsigned, const char *const *, unsigned,
    struct i386_user_stack *);

#endif
