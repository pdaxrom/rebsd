#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

#include "user_stack.h"

#define I386_USER_STACK_ALIGN       (2u * NBPW)
#define I386_USER_STACK_ARG_SLOTS   4u

static int
i386_user_stack_string_length(const char *string, unsigned *result)
{
    unsigned length;

    if (string == (const char *)0 || result == (unsigned *)0)
        return EINVAL;
    length = 0;
    while (string[length] != '\0') {
        if (length == ~0u - 1u)
            return E2BIG;
        ++length;
    }
    *result = length + 1u;
    return 0;
}

static int
i386_user_stack_string_bytes(const char *const *strings, unsigned count,
    unsigned *result)
{
    unsigned total;
    unsigned length;
    unsigned index;
    int error;

    if ((count != 0 && strings == (const char *const *)0) ||
        result == (unsigned *)0)
        return EINVAL;
    total = 0;
    for (index = 0; index < count; ++index) {
        error = i386_user_stack_string_length(strings[index], &length);
        if (error != 0)
            return error;
        if (length > ~total)
            return E2BIG;
        total += length;
    }
    *result = total;
    return 0;
}

static int
i386_user_stack_write_word(struct vmspace *vmspace, unsigned address,
    unsigned value)
{
    if (vmspace_write(vmspace, address, &value, sizeof(value)) != 0)
        return EFAULT;
    return 0;
}

int
i386_user_stack_build(struct vmspace *vmspace, unsigned stack_start,
    unsigned stack_end, const char *const *argv, unsigned argc,
    const char *const *envp, unsigned envc,
    struct i386_user_stack *result)
{
    unsigned argv_strings;
    unsigned env_strings;
    unsigned string_bytes;
    unsigned argv_bytes;
    unsigned env_bytes;
    unsigned top_address;
    unsigned string_address;
    unsigned argv_address;
    unsigned env_address;
    unsigned stack_pointer;
    unsigned cursor;
    unsigned length;
    unsigned index;
    int error;

    if (vmspace == (struct vmspace *)0 ||
        result == (struct i386_user_stack *)0 ||
        stack_start >= stack_end ||
        (stack_start & (NBPW - 1u)) != 0 ||
        (stack_end & (NBPW - 1u)) != 0 ||
        vmspace_check(vmspace, stack_start, stack_end - stack_start,
        VM_PROT_READ | VM_PROT_WRITE) != 0)
        return EINVAL;
    error = i386_user_stack_string_bytes(argv, argc, &argv_strings);
    if (error != 0)
        return error;
    error = i386_user_stack_string_bytes(envp, envc, &env_strings);
    if (error != 0)
        return error;
    if (env_strings > ~argv_strings)
        return E2BIG;
    string_bytes = argv_strings + env_strings;
    if (string_bytes > ~(NBPW - 1u))
        return E2BIG;
    string_bytes = (string_bytes + NBPW - 1u) & ~(NBPW - 1u);
    if (argc + 1u < argc || argc + 1u > ~0u / NBPW ||
        envc + 1u < envc || envc + 1u > ~0u / NBPW)
        return E2BIG;
    argv_bytes = (argc + 1u) * NBPW;
    env_bytes = (envc + 1u) * NBPW;

    if (stack_end - stack_start < NBPW)
        return E2BIG;
    top_address = stack_end - NBPW;
    if (string_bytes > top_address - stack_start)
        return E2BIG;
    string_address = top_address - string_bytes;
    if (env_bytes > string_address - stack_start)
        return E2BIG;
    env_address = string_address - env_bytes;
    if (argv_bytes > env_address - stack_start)
        return E2BIG;
    argv_address = env_address - argv_bytes;
    if (I386_USER_STACK_ARG_SLOTS * NBPW >
        argv_address - stack_start)
        return E2BIG;
    stack_pointer = (argv_address -
        I386_USER_STACK_ARG_SLOTS * NBPW) &
        ~(I386_USER_STACK_ALIGN - 1u);
    if (stack_pointer < stack_start)
        return E2BIG;

    error = vmspace_zero(vmspace, stack_start, stack_end - stack_start);
    if (error != 0)
        return error;
    error = i386_user_stack_write_word(vmspace, top_address,
        argv_address);
    if (error != 0)
        return error;

    cursor = string_address;
    for (index = 0; index < argc; ++index) {
        error = i386_user_stack_string_length(argv[index], &length);
        if (error != 0)
            return error;
        error = i386_user_stack_write_word(vmspace,
            argv_address + index * NBPW, cursor);
        if (error != 0 ||
            vmspace_write(vmspace, cursor, argv[index], length) != 0)
            return EFAULT;
        cursor += length;
    }
    error = i386_user_stack_write_word(vmspace,
        argv_address + argc * NBPW, 0);
    if (error != 0)
        return error;

    for (index = 0; index < envc; ++index) {
        error = i386_user_stack_string_length(envp[index], &length);
        if (error != 0)
            return error;
        error = i386_user_stack_write_word(vmspace,
            env_address + index * NBPW, cursor);
        if (error != 0 ||
            vmspace_write(vmspace, cursor, envp[index], length) != 0)
            return EFAULT;
        cursor += length;
    }
    error = i386_user_stack_write_word(vmspace,
        env_address + envc * NBPW, 0);
    if (error != 0 || cursor > top_address ||
        ((cursor + NBPW - 1u) & ~(NBPW - 1u)) != top_address)
        return error != 0 ? error : EFAULT;

    result->ius_stack_pointer = stack_pointer;
    result->ius_argc = argc;
    result->ius_argv = argv_address;
    result->ius_envp = env_address;
    return 0;
}
