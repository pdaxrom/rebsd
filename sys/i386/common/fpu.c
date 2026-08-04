#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>

#include "fpu.h"
#include "interrupt.h"

#define I386_CPUID_FEATURE_FPU       0x00000001u
#define I386_CPUID_FEATURE_FXSR      0x01000000u
#define I386_CPUID_FEATURE_SSE       0x02000000u

#define I386_CR0_MP                  0x00000002u
#define I386_CR0_EM                  0x00000004u
#define I386_CR0_TS                  0x00000008u
#define I386_CR0_NE                  0x00000020u
#define I386_CR4_OSFXSR              0x00000200u
#define I386_CR4_OSXMMEXCPT          0x00000400u
#define I386_MXCSR_DEFAULT           0x00001f80u

static struct i386_fpu_state i386_fpu_initial_state;
static int i386_fpu_ready;

static unsigned
i386_cpuid_features(void)
{
    unsigned eax;
    unsigned ebx;
    unsigned ecx;
    unsigned edx;

    eax = 1;
    __asm__ volatile ("cpuid"
        : "+a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx));
    (void)ebx;
    (void)ecx;
    return edx;
}

static void
i386_fpu_save(struct i386_fpu_state *state)
{
#ifdef I386_SSE_ENABLED
    __asm__ volatile ("fxsave %0" : "=m" (*state));
#else
    __asm__ volatile ("fnsave %0" : "=m" (*state));
#endif
}

static void
i386_fpu_restore(const struct i386_fpu_state *state)
{
#ifdef I386_SSE_ENABLED
    __asm__ volatile ("fxrstor %0" : : "m" (*state));
#else
    __asm__ volatile ("frstor %0" : : "m" (*state));
#endif
}

int
i386_fpu_init(void)
{
    unsigned features;
    unsigned cr0;
#ifdef I386_SSE_ENABLED
    unsigned cr4;
    unsigned mxcsr;
#endif

    features = i386_cpuid_features();
    if ((features & I386_CPUID_FEATURE_FPU) == 0)
        return ENODEV;
#ifdef I386_SSE_ENABLED
    if ((features & (I386_CPUID_FEATURE_FXSR | I386_CPUID_FEATURE_SSE)) !=
        (I386_CPUID_FEATURE_FXSR | I386_CPUID_FEATURE_SSE))
        return ENODEV;
#endif

    __asm__ volatile ("movl %%cr0, %0" : "=r" (cr0));
    cr0 &= ~(I386_CR0_EM | I386_CR0_TS);
    cr0 |= I386_CR0_MP | I386_CR0_NE;
    __asm__ volatile ("movl %0, %%cr0" : : "r" (cr0) : "memory");
#ifdef I386_SSE_ENABLED
    __asm__ volatile ("movl %%cr4, %0" : "=r" (cr4));
    cr4 |= I386_CR4_OSFXSR | I386_CR4_OSXMMEXCPT;
    __asm__ volatile ("movl %0, %%cr4" : : "r" (cr4) : "memory");
#endif

    __asm__ volatile ("fninit");
#ifdef I386_SSE_ENABLED
    mxcsr = I386_MXCSR_DEFAULT;
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));
#endif
    i386_fpu_save(&i386_fpu_initial_state);
    i386_fpu_restore(&i386_fpu_initial_state);
    bcopy(&i386_fpu_initial_state, &u.u_fpu,
        sizeof(i386_fpu_initial_state));
    i386_fpu_ready = 1;
    return 0;
}

void
i386_fpu_save_user(const struct i386_trapframe *frame)
{
    if (!i386_fpu_ready || frame == 0 || (frame->tf_cs & 3u) != 3u)
        return;
    i386_fpu_save(&u.u_fpu);
}

void
i386_fpu_restore_user(const struct i386_trapframe *frame)
{
    if (!i386_fpu_ready || frame == 0 || (frame->tf_cs & 3u) != 3u)
        return;
    i386_fpu_restore(&u.u_fpu);
}

void
i386_fpu_restore_current(void)
{
    if (i386_fpu_ready)
        i386_fpu_restore(&u.u_fpu);
}

void
i386_fpu_exec_reset(void)
{
    if (i386_fpu_ready)
        bcopy(&i386_fpu_initial_state, &u.u_fpu,
            sizeof(i386_fpu_initial_state));
}
