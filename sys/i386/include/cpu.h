#ifndef _I386_CPU_IDENT_H_
#define _I386_CPU_IDENT_H_

#include "boot.h"

#define I386_CPUID_FEATURE_FPU  0x00000001u
#define I386_CPUID_FEATURE_CX8  0x00000100u
#define I386_CPUID_FEATURE_CMOV 0x00008000u
#define I386_CPUID_FEATURE_FXSR 0x01000000u
#define I386_CPUID_FEATURE_SSE  0x02000000u

struct i386_cpu_info {
    char ci_vendor[13];
    i386_u32 ci_features;
    unsigned ci_family;
    unsigned ci_model;
    unsigned ci_stepping;
};

int i386_cpu_init(void);
const struct i386_cpu_info *i386_cpu_info(void);

#endif
